// The library allocation seam (include/gui_forms/allocation/library_memory/
// library_memory.hpp): STXlib's allocator, per-thread counts and the
// test-only observer.

#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <stxlib/memory.hpp>

#include <atomic>

namespace gui_forms::detail {

namespace {

// The observer, set by tests before the threads it should see start; read
// once per allocation.
constinit std::atomic<LibraryAllocationObserver> allocation_observer{nullptr};

// The calling thread's counts: trivially destructible, written and read
// only by their own thread.
constinit thread_local std::uint64_t thread_granted = 0U;
constinit thread_local std::uint64_t thread_released = 0U;

} // namespace

void* library_allocate(const std::size_t size, const std::size_t alignment) noexcept {
    const LibraryAllocationObserver observer = allocation_observer.load(std::memory_order_acquire);
    if (observer != nullptr) {
        const bool admitted = (*observer)(size);
        if (!admitted) {
            return nullptr;
        }
    }
    void* const block = stxlib::memory::allocate(size, alignment);
    if (block != nullptr) {
        thread_granted = thread_granted + 1U;
    }
    return block;
}

void library_release(void* const block) noexcept {
    if (block == nullptr) {
        return;
    }
    stxlib::memory::release(block);
    thread_released = thread_released + 1U;
}

void set_library_allocation_observer(const LibraryAllocationObserver observer) noexcept {
    allocation_observer.store(observer, std::memory_order_release);
}

std::uint64_t library_allocations_granted() noexcept {
    return thread_granted;
}

std::uint64_t library_blocks_released() noexcept {
    return thread_released;
}

} // namespace gui_forms::detail
