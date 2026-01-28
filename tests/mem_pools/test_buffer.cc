#include <iostream>
#include <type_traits>
#include <cassert>
#include <cstdint>

#include "mem_pools/buffer.h"

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


// Aligned test type
struct alignas(128) AlignedDeepData {
    int values[32];
    AlignedDeepData() { for (int i = 0; i < 32; ++i) values[i] = i; }
};

void TestDynamicBufferAlignmentAndConstruction() {
    LOG_TEST("TestDynamicBufferAlignmentAndConstruction");

    Buffer<AlignedDeepData, 128, true> b; // constructs AlignedDeepData dynamically

    uintptr_t addr = reinterpret_cast<uintptr_t>(b.p_Buffer);
    ASSERT_TRUE((addr % 128) == 0);
    ASSERT_EQ(b.p_Buffer->values[0], 0);
}

void TestInlineBufferConstruction() {
    LOG_TEST("TestInlineBufferConstruction");

    Buffer<int, 64, false> bi(42);
    ASSERT_EQ(bi.m_Buffer, 42);

    // Copy/move are deleted
    static_assert(!std::is_copy_constructible_v<decltype(bi)>);
    static_assert(!std::is_move_constructible_v<decltype(bi)>);
}

void TestDynamicBufferTraits() {
    LOG_TEST("TestDynamicBufferTraits");

    using Dyn = Buffer<int, 16, true>;
    static_assert(!std::is_copy_constructible_v<Dyn>);
    static_assert(!std::is_move_constructible_v<Dyn>);

    Dyn d(5);
    ASSERT_EQ(*d.p_Buffer, 5);
}

// Tracks construction and destruction counts
struct LifetimeTracker {
    static int constructor_calls;
    static int destructor_calls;
    int data;

    LifetimeTracker(int v) : data(v) { constructor_calls++; }
    ~LifetimeTracker() { destructor_calls++; }
};
int LifetimeTracker::constructor_calls = 0;
int LifetimeTracker::destructor_calls = 0;

// Test variadic construction
struct MultiArgData {
    int a;
    float b;
    std::string c;
    MultiArgData(int a, float b, std::string c) : a(a), b(b), c(c) {}
};

void TestBufferDestructorCalling() {
    LOG_TEST("TestBufferDestructorCalling");

    LifetimeTracker::constructor_calls = 0;
    LifetimeTracker::destructor_calls = 0;

    {
        // Scope block to trigger destructor
        Buffer<LifetimeTracker, 64, true> b(123);
        ASSERT_EQ(b.p_Buffer->data, 123);
        ASSERT_EQ(LifetimeTracker::constructor_calls, 1);
    }

    // After b goes out of scope, the destructor should have been called
    ASSERT_EQ(LifetimeTracker::destructor_calls, 1);
}

void TestVariadicConstruction() {
    LOG_TEST("TestVariadicConstruction");

    // Testing passing 3 different types to the internal constructor
    Buffer<MultiArgData, 64, true> b(10, 20.5f, "hello");
    
    ASSERT_EQ(b.p_Buffer->a, 10);
    ASSERT_TRUE(b.p_Buffer->b > 20.0f);
    ASSERT_EQ(b.p_Buffer->c, "hello");
}

void TestExtremeAlignment() {
    LOG_TEST("TestExtremeAlignment");

    // 4096 is typically a memory page boundary
    constexpr size_t PAGE_ALIGN = 4096;
    Buffer<int, PAGE_ALIGN, true> b(777);

    uintptr_t addr = reinterpret_cast<uintptr_t>(b.p_Buffer);
    ASSERT_TRUE((addr % PAGE_ALIGN) == 0);
    ASSERT_EQ(*b.p_Buffer, 777);
}

void TestInlineBufferLargeObject() {
    LOG_TEST("TestInlineBufferLargeObject");

    struct Large {
        char bytes[1024];
    };

    Buffer<Large, 64, false> b;
    // Check if the inline buffer actually has enough space for Large
    static_assert(sizeof(b) >= 1024);
    
    // Ensure the internal address is aligned within the class
    uintptr_t addr = reinterpret_cast<uintptr_t>(&b.m_Buffer);
    ASSERT_TRUE((addr % 64) == 0);
}

int main() {
    TestDynamicBufferAlignmentAndConstruction();
    TestInlineBufferConstruction();
    TestDynamicBufferTraits();
    
    // New Tests
    TestBufferDestructorCalling();
    TestVariadicConstruction();
    TestExtremeAlignment();
    TestInlineBufferLargeObject();

    std::cout << "\n\033[32mAll mem_pools::Buffer tests passed successfully.\033[0m" << std::endl;
    return 0;
}
