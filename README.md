# 🛡️ MemSentry

**A surgical memory tracking and management system for C++.**

MemSentry is a lightweight library that intercepts memory allocations to organize objects into specific **Heaps**, track usage in real-time, and detect memory leaks upon shutdown.

## ✨ Features

* **Per-Class Heap Assignment:** Route different classes (e.g., `Audio`, `Physics`, `AI`) to their own dedicated memory heaps.
* **Automatic Leak Detection:** Reports total bytes remaining in every heap.
* **Zero-boilerplate Interface:** Simply inherit from `ISentry<T>` and you are done.

## 🚀 Usage

### 1. Define Your Class
Inherit from `MEM_SENTRY::sentry::ISentry<YourClass>`. This automatically hooks up the memory manager.

```cpp
#include "mem_sentry/sentry.h"

// Pass the class name into the template (CRTP)
class Enemy : public MEM_SENTRY::sentry::ISentry<Enemy> {
public:
    Enemy() { /* ... */ }
    ~Enemy() { /* ... */ }
};
```

### 2. Configure Heaps in `main()`
Create your heaps and assign them to your classes before allocation.

```cpp
#include "mem_sentry/heap.h"
#include "enemy.h"

int main() {
    using namespace MEM_SENTRY::heap;

    // 1. Create Heaps
    Heap* enemyHeap = new Heap("Enemy_Heap");

    // 2. Bind Class to Heap
    Enemy::setHeap(enemyHeap);

    // 3. Allocate (Automatically goes to Enemy_Heap)
    Enemy* grunt = new Enemy(); 

    // 4. Cleanup
    delete grunt;      // Tracks freeing
    delete enemyHeap;  // Reports leaks if any

    return 0;
}
```

## 📊 Sample Output

MemSentry provides detailed logging of allocation and deallocation events.

```text
-----------------
Allocating 104 bytes on heap: DefaultHeap
Current total size is: 104 bytes on heap: DefaultHeap
-----------------
-----------------
Allocating 136 bytes on heap: Enemy_Heap
Current total size is: 136 bytes on heap: Enemy_Heap
-----------------
Enemy Constructor
-----------------
Allocating 8 bytes on heap: DefaultHeap
Current total size is: 112 bytes on heap: DefaultHeap
-----------------
Enemy Destructor
-----------------
Freeing 8 bytes from heap: DefaultHeap
Current total size is: 104 bytes on heap: DefaultHeap
-----------------
-----------------
Freeing 136 bytes from heap: Enemy_Heap
Current total size is: 0 bytes on heap: Enemy_Heap
-----------------
-----------------
Freeing 104 bytes from heap: DefaultHeap
Current total size is: 0 bytes on heap: DefaultHeap
-----------------
```