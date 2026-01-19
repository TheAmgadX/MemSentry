# TODO

- Thread Safety: Add multi-threading support in the Heap class using `std::mutex` for list management and `std::atomic` for lock-free Id generation.

- Operator Overloads: Override array allocation operators `new[]`, `delete[]`, `alignas()`.

- Double-Free Detection: Implement robust detection and error reporting for double-free attempts.

- Reporting Interface: Expose an interface with strategy pattern to support custom reporting for `heap->ReportMemory()`.

- Zero-Byte Handling: Ensure handling of zero-byte allocation requests.

- Hierarchical Heaps: Implement a hierarchical heap system using a graph-based representation.
