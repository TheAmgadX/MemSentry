#pragma once
#include <iostream>
#include <concepts>

namespace MEM_SENTRY::mem_pool {
    
    /**
     * @brief Concept to prevent raw C-style arrays (e.g., int[]) from being used.
     * This ensures the pool manages single objects, std::array, or complex classes 
     * without the overhead of tracking array counts.
     */
    template<typename T>
    concept NotRawArray = !std::is_array_v<T>;

    /**
     * @brief Buffer storage wrapper (dynamic or inline)
     *
     * `Buffer<T, alignment, isDynamic>` is a minimal storage wrapper used
     * by the pool implementation. There are two modes controlled by the
     * `isDynamic` boolean:
     *
     * - Dynamic (`isDynamic == true`): allocates a single `T` on the
     *   heap using aligned allocation and constructs it with the
     *   forwarded constructor arguments. This mode trades a heap
     *   allocation for flexibility.
     *
     * - Inline/Static (`isDynamic == false`): stores a single `T` inline
     *   inside the `Buffer` object. This is zero-overhead (no heap
     *   allocation) and offers better cache locality.
     *
     * Template parameters:
     * - `T`: the stored type. The `NotRawArray` concept prevents raw C-
     *   style arrays (e.g., `int[]`) from being used.
     * 
     * - `alignment`: alignment for the stored object. When using dynamic
     *   mode this alignment is passed to the aligned allocation.
     * 
     * - `isDynamic`: select dynamic (heap) or static (inline) storage.
     *
     * Behaviour & invariants:
     * - The dynamic variant performs an aligned `::operator new` and
     *   placement-new constructs `T` with the provided arguments. The
     *   destructor manually runs `T`'s destructor and calls
     *   `::operator delete` with `std::align_val_t{alignment}`.
     * 
     * - Copy and move constructors/assignment are deleted to avoid
     *   accidental ownership/aliasing issues.
     * 
     * - This wrapper stores/constructs exactly one `T` (not an array).
     *
     * Example usage:
     * - Dynamic: `Buffer<MyType, 32, true> b(arg1, arg2);` — allocates an
     *   aligned `MyType` and constructs it with `(arg1, arg2)`.
     * 
     * - Inline:  `Buffer<MyType, 64, false> b(arg1);` — constructs `MyType`
     *   inline inside the `Buffer` object.
     */
    template<NotRawArray T, size_t alignment, bool isDynamic = true>
    struct Buffer {
        T* p_Buffer;

        template<typename... Args>
        Buffer(Args&&... args) {
            void* ptr = ::operator new(sizeof(T), std::align_val_t{alignment});
            p_Buffer = new (ptr) T(std::forward<Args>(args)...);
        }

        ~Buffer() {
            if (p_Buffer) {
                p_Buffer->~T();
                ::operator delete(p_Buffer, std::align_val_t{alignment});
            }
        }

        // disable copy and move operations.
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) = delete;
        Buffer&& operator=(Buffer&&) = delete;
    };

    /**
     * Inline/static variant - stores a single `T` inside the struct.
     * This is useful when a single object or fixed-size inline storage
     * is desired instead of a heap allocation for better cache locality.
     */
    template<NotRawArray T, size_t alignment>
    struct alignas(alignment) Buffer<T, alignment, false> {
        T m_Buffer;

        template<typename... Args>
        Buffer(Args&&... args) : m_Buffer(std::forward<Args>(args)...) {}

        ~Buffer() = default;

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) = delete;
        Buffer& operator=(Buffer&&) = delete;
    };
}

