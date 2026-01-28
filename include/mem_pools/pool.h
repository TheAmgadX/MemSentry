#pragma once 
#include "mem_pools/buffer.h"
#include "mem_sentry/constants.h"

#include <atomic>
#include <vector>

namespace MEM_SENTRY::mem_pool {
/**
 * @brief Used to make sure every atomic variable owns a full cache line to prevent false sharing.
 */
template<typename U>
struct alignas(MEM_SENTRY::constants::CACHE_LINE_SIZE) CacheAlignedAtomic {
    std::atomic<U> m_Value;
};


/**
 * @brief Lock-free single-producer / single-consumer ring of Buffer pointers
 * 
 * @note Implementation: Waste-One-Slot SPSC Ring Buffer.
 * To distinguish between FULL and EMPTY states without shared flags, one physical slot is always kept empty.
 * `Usable capacity = (Internal Queue Size - 1).`
 * 
 * `RingPool<T, alignment, isDynamic>` implements a simple fixed-capacity
 * circular queue of `Buffer<T, alignment, isDynamic>*` pointers intended
 * for real-time-safe handoff between one producer (writer) and one
 * consumer (reader) thread.
 *
 * Key characteristics:
 * - Capacity is `queue_size` rounded up to the next power-of-two and
 *   stored in `m_QueueSize`. Fast wrapping uses the mask `m_Mask`.
 * 
 * - The class supports two ownership modes:
 *   - "Full" mode (pool owns buffers): the constructor allocates the
 *     per-slot `Buffer` objects and the pool deletes them in the
 *     destructor.
 * 
 *   - "Empty" mode (caller owns buffers): the queue is initially
 *     empty and the caller pushes owned `Buffer*` pointers into the
 *     ring. The pool will not delete those pointers on destruction.
 * 
 * - Single-producer / single-consumer intended: `m_WriteIndex` is only
 *   modified by the producer, `m_ReadIndex` only by the consumer.
 * 
 * - The class stores raw pointers to `Buffer<T,...>`; ownership is
 *   determined by the mode described above.
 *
 * Template parameters:
 * - `T`: stored element type (must satisfy `NotRawArray`).
 * - `alignment`: alignment used for buffers.
 * - `isDynamic`: forwarded to `Buffer` template; when `true` the
 *   `Buffer` objects perform heap allocations for their `T`.
 *
 * Thread-safety and ordering notes:
 * - `push()` is intended to be called only by the producer thread.
 * - `pop()` is intended to be called only by the consumer thread.
 * 
 * - The implementation uses `std::memory_order_acquire`/`release`
 *   semantics for the hand-off points.
 *
 * Usage examples:
 * - Pool owns buffers (full):
 *   `RingPool<float, 32, true> pool(false, 8, constructor-args-for-Buffer);`
 * 
 * - Caller owns buffers (empty):
 *   `RingPool<MyType> pool(true, 8); // then push(Buffer* ownedByCaller)`
*/
template<NotRawArray T, size_t alignment = 0, bool isDynamic = true>
class RingPool {
private:
    /**
     * @brief Write pointer index in the buffer (atomic).
     *
     * Updated by the producer (writer). Marked atomic to ensure visibility
     * across threads without locks.
     */
    CacheAlignedAtomic<size_t> m_WriteIndex;

    /**
     * @brief Read pointer index in the buffer (atomic).
     *
     * Updated by the consumer (reader). Marked atomic to ensure visibility
     * across threads without locks.
     */
    CacheAlignedAtomic<size_t> m_ReadIndex;

    /**
     * @brief vector holding pointers to the allocated buffers.
     */
    alignas(MEM_SENTRY::constants::CACHE_LINE_SIZE) std::vector<MEM_SENTRY::mem_pool::Buffer<T, alignment, isDynamic> *> m_Queue;

    /**
     * @brief Total Buffers in the queue
     * 
     * Always a power of two for efficient modulo masking.
     */
    size_t m_QueueSize{0};

    /**
     * @brief Mask used for fast wrapping of indices (mSize - 1).
     */
    size_t m_Mask{0};

    /**
     * @brief Whether the buffer is initialized and ready for use.
     */
    bool m_Valid{false};

    /**
     * @brief Indicates the initial state of the queue.
     *
     * - If true, the queue starts empty (no buffers pre-initialized). 
     *   Push operations must occur before buffers can be popped.
     *
     * - If false, the queue starts full (buffers are pre-initialized and ready to be consumed). 
     *   Pop operations can proceed immediately.
     * 
     * @warning In empty mode the queue doesn't own the buffers so it doesn't free them.
     *   you must return them back to the owner!
     */
    bool m_EmptyQueue{false};

private:
    /**
     * @brief Round up to next power of 2
     */
    static constexpr size_t next_power_of_2(size_t n) {
        if (n <= 1) return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }
    
    /**
     * @brief Get available space for writing (from writer's perspective)
     */
    size_t getFreeSpace(size_t currentWrite){
        size_t currentRead = m_ReadIndex.m_Value.load(std::memory_order_acquire);

        return m_QueueSize - getAvailableBuffers(currentWrite, currentRead) - 1;
    }

    /**
     * @brief Get available data for reading (from reader's perspective)
     */
    size_t getAvailableBuffers(size_t currentWrite, size_t currentRead){
        return (currentWrite - currentRead) & m_Mask;
    }

    /**
     * @brief Free the buffer and reset indices.
     *
     * Cleans up all resources owned by the buffer. After calling this, the Queue
     * becomes invalid and must be reinitialized before reuse.
     * 
     * @warning In empty mode the queue doesn't own the buffers so it doesn't free them.
     *   you must return them back to the owner! this may cause a memory leak.
     */
    void cleanup() {
        m_Valid = false;
        m_WriteIndex.m_Value.store(0, std::memory_order_seq_cst);
        m_ReadIndex.m_Value.store(0, std::memory_order_seq_cst);

        // free only if we own the buffers
        if (!m_EmptyQueue) {
            for (auto& buffer : m_Queue) {
                if (buffer) {
                    delete buffer;
                    buffer = nullptr;
                }
            }
        }

        m_Queue.clear();

        m_QueueSize = 0;
        m_Mask = 0;
    }

    /**
     * @brief allocates buffers of the queue.
     */
    template<typename... Args>
    void allocBuffers(size_t queue_size, Args&&... args){
        //* init the buffers.
        for(size_t i = 0; i < queue_size - 1; ++i){
            /*
                NOTE: we didn't use std::forward because we need to keep the args lvalue..
                to avoid losing the data in the constructor of Buffer.
            */
            m_Queue[i] = new Buffer<T, alignment, isDynamic>(args...);

            if (!m_Queue[i]) {
                cleanup();
                return;
            }

            if constexpr (isDynamic) {
                // dynamic Buffer exposes `p_Buffer` which must be valid
                if (!m_Queue[i]->p_Buffer) {
                    cleanup();
                    return;
                }
            }
        }

        m_Valid = true;
    }

public: 
    /**
     * RingPool constructor
     *
     * Create a lock-free ring of pointers to `Buffer<T,...>` objects. The
     * queue operates in two logical modes: `Empty` and `Full`
     *
     * Parameters are deliberately flexible via variadic `Args...` so the
     * same constructor can forward arguments to `Buffer` (for the full
     * mode allocations).
     *
     * Important notes:
     * - `queue_size` is rounded up to the next power of two and stored in
     *   `m_QueueSize`. The mask `m_Mask` is `m_QueueSize - 1` and is used
     *   for fast modulo arithmetic: `index & m_Mask`.
     * 
     * - The class is intended for a single producer (writer) and single
     *   consumer (reader) scenario. `m_WriteIndex` is written by the
     *   producer, `m_ReadIndex` by the consumer. Atomics and memory
     *   ordering are used to ensure correct cross-thread visibility.
     * 
     * - Use `isValid()` to check successful initialization when operating
     *   in full mode; allocation failures will call `cleanup()` and leave
     *   the object invalid.
     */
    template <typename... Args>
    RingPool(bool empty, size_t queue_size, Args&&... args){
        m_Valid = false;

        queue_size = next_power_of_2(queue_size);
        
        if(queue_size < 1){
            queue_size = 2;
        }

        m_WriteIndex.m_Value.store(queue_size - 1, std::memory_order_relaxed);

        m_QueueSize = queue_size;
        m_Mask = queue_size - 1;

        m_Queue.resize(queue_size, nullptr);

        if(empty){
            m_Valid = true;
            m_EmptyQueue = true;
            m_WriteIndex.m_Value.store(0, std::memory_order_relaxed);
            return;
        }

        allocBuffers(queue_size, std::forward<Args>(args)...);
    }

    /**
     * @brief Destructor - clean up all allocated memory
     * 
     * @note Safe to call even if buffer was never valid (will just no-op).
     * 
     * @warning In empty mode the queue doesn't own the buffers so it doesn't free them.
     *   you must return them back to the owner! 
     */
    ~RingPool(){
        cleanup();
    }

    /**
     * @brief Check if the buffer is valid (properly initialized).
     * must be used after initializing before any processing.
     * 
     * @return true if valid, false otherwise.
     */
    bool isValid() const noexcept {
        return m_Valid;
    }

    /**
     * Try to push a buffer pointer into the ring.
     *
     * - In empty-mode the caller supplies buffers created/owned externally
     *   and the queue will store the pointer without taking ownership.
     * 
     * - In full-mode this API may be used by a producer returning a buffer
     *   back into the pool (i.e. after consumer has finished with it).
     *
     * Threading/ordering:
     * - The producer should be the only thread calling `push`.
     * 
     * - `push` is non-blocking and uses atomic operations; it returns
     *   `false` when the queue is full.
     *
     * Returns `true` on success, `false` if no free slot was available.
     */
    bool push(Buffer<T, alignment, isDynamic>* buffer);


    /**
     * Pop a buffer pointer from the ring.
     *
     * - Called by the single consumer (reader) thread.
     * 
     * - Returns `nullptr` if the queue is currently empty.
     * 
     * - When a valid pointer is returned the consumer owns the pointer
     *   until it returns it (for example via `push` again or other
     *   external ownership protocol). In full-mode the pool retains
     *   ownership semantics; in empty-mode the caller remains owner.
     *
     * This function is non-blocking and uses atomic operations.
     */
    Buffer<T, alignment, isDynamic>* pop();

    /**
     * @brief Get the total size (capacity) of the Queue.
     * @return Number of buffers in the queue.
     */
    size_t queueSize() const noexcept {
        return m_QueueSize;
    }

    /**
     * @brief the current size of the queue.
     * @note perform atomic operations (aquire loads) for both m_WriteIndex, m_ReadIndex.
     * 
     * @return the current number of buffers in the queue.
     */
    size_t currentSize();
};
}

template<MEM_SENTRY::mem_pool::NotRawArray  T, size_t alignment, bool isDynamic>
size_t MEM_SENTRY::mem_pool::RingPool<T, alignment, isDynamic>::currentSize() {
    size_t currentRead  = m_ReadIndex.m_Value.load(std::memory_order_acquire);
    size_t currentWrite = m_WriteIndex.m_Value.load(std::memory_order_acquire);

    return getAvailableBuffers(currentWrite, currentRead);
}

template<MEM_SENTRY::mem_pool::NotRawArray T, size_t alignment, bool isDynamic>
bool MEM_SENTRY::mem_pool::RingPool<T, alignment, isDynamic>::push(MEM_SENTRY::mem_pool::Buffer<T, alignment, isDynamic> *buffer) {
    if(!buffer){
        return false;
    }

    size_t currentWrite = m_WriteIndex.m_Value.load(std::memory_order_relaxed); 

    size_t space = getFreeSpace(currentWrite);
    
    if(space == 0){
        return false;
    }

    m_Queue[currentWrite] = buffer;
    m_WriteIndex.m_Value.store((currentWrite + 1) & m_Mask, std::memory_order_release);

    return true;
}

template<MEM_SENTRY::mem_pool::NotRawArray  T, size_t alignment, bool isDynamic>
MEM_SENTRY::mem_pool::Buffer<T, alignment, isDynamic>* MEM_SENTRY::mem_pool::RingPool<T, alignment, isDynamic>::pop() {
    size_t currentWrite = m_WriteIndex.m_Value.load(std::memory_order_acquire);
    
    size_t currentRead = m_ReadIndex.m_Value.load(std::memory_order_relaxed);
    
    size_t buffers = getAvailableBuffers(currentWrite, currentRead);

    if(buffers == 0){
        return nullptr;
    }
    
    Buffer<T, alignment, isDynamic>* buffer = m_Queue[currentRead];
    m_Queue[currentRead] = nullptr;

    size_t new_index = (currentRead + 1) & m_Mask;
    
    m_ReadIndex.m_Value.store(new_index, std::memory_order_release);

    return buffer;
}
