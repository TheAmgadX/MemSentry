#include <iostream>
#include <vector>
#include <cassert>
#include <cstdint>

#include "mem_pools/pool.h"
#include "mem_pools/buffer.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace MEM_SENTRY::mem_pool;

// ----------------------------------------------------------------------------
// HELPER MACROS
// ----------------------------------------------------------------------------
#define ASSERT_EQ(val, expected) \
    do { \
        if((val) != (expected)) { \
            std::cerr << "[\033[31mFAIL\033[0m] " << __FUNCTION__ << " line " << __LINE__ \
                      << ": Expected " << #val << " == " << expected \
                      << ", but got " << (val) << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { \
        if(!(cond)) { \
            std::cerr << "[\033[31mFAIL\033[0m] " << __FUNCTION__ << " line " << __LINE__ \
                      << ": Assertion " << #cond << " failed.\n"; \
            std::exit(1); \
        } \
    } while(0)

#define LOG_TEST(name) std::cout << "[\033[32mRUN\033[0m] " << name << "..." << std::endl


void TestFullModePool() {
    LOG_TEST("TestFullModePool");

    // Full mode: pool owns buffers; Buffer<int> constructed with value 7
    RingPool<int, alignof(int), true> pool(false, 4, 7);
    ASSERT_TRUE(pool.isValid());
    ASSERT_EQ(pool.queueSize(), 4);

    // Pop all items - should get four buffers constructed with 7
    for (int i = 0; i < static_cast<int>(pool.queueSize()) - 1; ++i) {
        auto* b = pool.pop();
        ASSERT_TRUE(b != nullptr);
        ASSERT_EQ(*b->p_Buffer, 7);
        // In full-mode pool owns buffers, but pop hands pointer to consumer.
        // Return it back to pool to mimic reuse.
        bool ok = pool.push(b);
        ASSERT_TRUE(ok);
    }

    // Now drain them again
    for (int i = 0; i < static_cast<int>(pool.queueSize()) - 1; ++i) {
        auto* b = pool.pop();
        ASSERT_TRUE(b != nullptr);
        ASSERT_EQ(*b->p_Buffer, 7);
    }

    // Now empty: further pops return nullptr
    ASSERT_TRUE(pool.pop() == nullptr);
}

void TestEmptyModeCallerOwned() {
    LOG_TEST("TestEmptyModeCallerOwned");

    RingPool<int, alignof(int), true> pool(true, 3);
    ASSERT_TRUE(pool.isValid());
    // queue_size is rounded up to a power-of-two internally; verify
    // that the returned capacity is a power-of-two and at least the
    // requested size (3).
    auto is_power_of_two = [](size_t v){ return v && ((v & (v-1)) == 0); };
    ASSERT_TRUE(is_power_of_two(pool.queueSize()));
    ASSERT_TRUE(pool.queueSize() >= 3);

    // Push up to the usable capacity (power-of-two minus one).
    const size_t capacity = pool.queueSize() - 1;
    std::vector<Buffer<int, alignof(int), true>*> owned;
    for (size_t i = 0; i < capacity; ++i) {
        owned.push_back(new Buffer<int, alignof(int), true>(static_cast<int>(i+1)));
        bool ok = pool.push(owned.back());
        ASSERT_TRUE(ok);
    }

    // Now pool is full; further push must fail
    auto extra = new Buffer<int, alignof(int), true>(99);
    ASSERT_TRUE(!pool.push(extra));
    delete extra;

    // Pop and validate ownership: popped pointers are those we pushed
    for (size_t i = 0; i < capacity; ++i) {
        auto* b = pool.pop();
        ASSERT_TRUE(b != nullptr);
        ASSERT_EQ(*b->p_Buffer, static_cast<int>(i+1));
        // consumer is responsible for freeing when in empty-mode
        delete b;
    }

    // Empty now
    ASSERT_TRUE(pool.pop() == nullptr);
}

void TestWrapAroundBehavior() {
    LOG_TEST("TestWrapAroundBehavior");

    RingPool<int, alignof(int), true> pool(true, 3);
    ASSERT_TRUE(pool.isValid());

    auto* a = new Buffer<int, alignof(int), true>(10);
    auto* b = new Buffer<int, alignof(int), true>(20);
    auto* c = new Buffer<int, alignof(int), true>(30);

    ASSERT_TRUE(pool.push(a));
    ASSERT_TRUE(pool.push(b));
    ASSERT_TRUE(pool.push(c));

    // Now full
    ASSERT_TRUE(!pool.push(new Buffer<int, alignof(int), true>(40)));

    auto* p1 = pool.pop();
    ASSERT_EQ(*p1->p_Buffer, 10);
    delete p1;

    // push again after pop should succeed (wrap)
    auto* d = new Buffer<int, alignof(int), true>(40);
    ASSERT_TRUE(pool.push(d));

    auto* p2 = pool.pop();
    ASSERT_EQ(*p2->p_Buffer, 20);
    delete p2;

    auto* p3 = pool.pop();
    ASSERT_EQ(*p3->p_Buffer, 30);
    delete p3;
}

void TestProducerConsumerSimulation() {
    LOG_TEST("TestProducerConsumerSimulation (multi-threaded)");

    constexpr int CAPACITY = 8;
    constexpr int ITEMS = 1000;

    RingPool<int, alignof(int), true> pool(true, CAPACITY);
    ASSERT_TRUE(pool.isValid());

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> producer_done{false};

    // Producer thread: create caller-owned buffers and push them into pool.
    std::thread producer([&]() {
        for (int i = 0; i < ITEMS; ++i) {
            auto* b = new Buffer<int, alignof(int), true>(i);
            // spin until pushed successfully
            while (!pool.push(b)) {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        producer_done.store(true);
    });

    // Consumer thread: pop and delete until all items consumed
    std::thread consumer([&]() {
        while (!producer_done.load(std::memory_order_acquire) || produced.load(std::memory_order_relaxed) != consumed.load(std::memory_order_relaxed)) {
            auto* b = pool.pop();
            if (b) {
                consumed.fetch_add(1, std::memory_order_relaxed);
                delete b;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(produced.load(), ITEMS);
    ASSERT_EQ(consumed.load(), ITEMS);
}

void TestAlignmentGuarantees() {
    LOG_TEST("TestAlignmentGuarantees");

    // Test with 64-byte alignment (typical for AVX-512 cache lines)
    constexpr size_t ALIGN = 64;
    RingPool<int, ALIGN, true> pool(false, 4, 100);

    for (int i = 0; i < static_cast<int>(pool.queueSize()) - 1; ++i) {
        auto* b = pool.pop();
        uintptr_t addr = reinterpret_cast<uintptr_t>(b->p_Buffer);
        
        // Check if the address is a multiple of ALIGN
        if (addr % ALIGN != 0) {
            std::cerr << "Address " << std::hex << addr << " is not aligned to " << ALIGN << "\n";
            ASSERT_TRUE(addr % ALIGN == 0);
        }
        ASSERT_EQ(*b->p_Buffer, 100);
    }
}

static std::atomic<int> g_lifeCount{0};
struct Spy {
    Spy() { g_lifeCount.fetch_add(1); }
    ~Spy() { g_lifeCount.fetch_sub(1); }
};

void TestLifecycleManagement() {
    LOG_TEST("TestLifecycleManagement");
    g_lifeCount.store(0);

    {
        // 1. Full Mode: Pool should immediately construct 8 Spies
        RingPool<Spy, 16, true> pool(false, 8);
        ASSERT_EQ(g_lifeCount.load(), pool.queueSize() - 1);

        // 2. Pop one: count should stay 8 (it's just moving the pointer)
        auto* b = pool.pop();
        ASSERT_TRUE(b != nullptr);
        ASSERT_EQ(g_lifeCount.load(), pool.queueSize() - 1);

        // 3. Manually push it back: still 8
        pool.push(b);
        ASSERT_EQ(g_lifeCount.load(), pool.queueSize() - 1);
    } 
    
    // 4. Out of Scope: The Pool is destroyed. 
    // It must destroy its Buffers, which must destroy the Spies.
    // If this is not 0, you have a memory leak!
    ASSERT_EQ(g_lifeCount.load(), 0);
}

void TestHighPressureContention() {
    LOG_TEST("TestHighPressureContention (No Sleep)");

    constexpr int ITEMS = 10000000; 
    constexpr int LOG_INTERVAL = ITEMS / 10;
    
    // Capacity 1024 is enough for a high-speed spin test
    RingPool<size_t, 64, true> pool(true, 1024);

    std::atomic<size_t> sum_produced{0};
    std::atomic<size_t> sum_consumed{0};
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    std::atomic<bool> start_flag{false};

    std::thread producer([&]() {
        // Wait for start signal
        while(!start_flag.load(std::memory_order_acquire)); 

        for (size_t i = 1; i <= ITEMS; ++i) {
            auto* b = new Buffer<size_t, 64, true>(i);
            
            // Spin until push succeeds
            while (!pool.push(b)) {
                std::this_thread::yield(); // Help consumer catch up if full
            }

            sum_produced.fetch_add(i, std::memory_order_relaxed);
            // Use release to make sure the data is visible to consumer
            int current = produced_count.fetch_add(1, std::memory_order_release) + 1;
            
            if (current % LOG_INTERVAL == 0) {
                std::cout << "[Producer] Progress: " << (current * 100 / ITEMS) << "%" << std::endl;
            }
        }
        std::cout << "[Producer] Finished sending all items." << std::endl;
    });

    std::thread consumer([&]() {
        while(!start_flag.load(std::memory_order_acquire));

        while (consumed_count.load(std::memory_order_acquire) < ITEMS) {
            auto* b = pool.pop();
            if (b) {
                sum_consumed.fetch_add(*b->p_Buffer, std::memory_order_relaxed);
                delete b;
                
                int current = consumed_count.fetch_add(1, std::memory_order_release) + 1;
                if (current % LOG_INTERVAL == 0) {
                    std::cout << "[Consumer] Progress: " << (current * 100 / ITEMS) << "%" << std::endl;
                }
            } else {
                // If pool is empty, yield to let producer work
                std::this_thread::yield(); 
            }
        }
        std::cout << "[Consumer] Finished consuming all items." << std::endl;
    });

    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Release both threads at once
    start_flag.store(true, std::memory_order_release);

    std::cout << "[Main] Waiting for Producer to join..." << std::endl;
    producer.join();
    std::cout << "[Main] Producer joined. Waiting for Consumer..." << std::endl;
    consumer.join();
    std::cout << "[Main] Consumer joined." << std::endl;

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    std::cout << "[DONE] Processed " << ITEMS << " items in " << diff.count() << "s (" 
              << static_cast<long>(ITEMS / diff.count()) << " ops/sec)" << std::endl;

    ASSERT_EQ(sum_produced.load(), sum_consumed.load());
}

int main() {
    TestFullModePool();
    TestEmptyModeCallerOwned();
    TestWrapAroundBehavior();
    TestProducerConsumerSimulation();

    TestAlignmentGuarantees();
    TestLifecycleManagement();
    TestHighPressureContention();
    std::cout << "\n\033[32m[PASSED]\033[0m All MEM_SENTRY tests completed successfully." << std::endl;
    return 0;
}
