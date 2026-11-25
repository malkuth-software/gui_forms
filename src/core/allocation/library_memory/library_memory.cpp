// The library allocation seam (include/gui_forms/allocation/library_memory/
// library_memory.hpp): STXlib's allocator, per-thread counts, the
// test-only observer, and the teardown reserve.

#include "gui_forms/allocation/library_memory/library_memory.hpp"
#include "gui_forms/allocation/library_memory/library_memory_diagnostics.hpp"
#include "gui_forms/allocation/library_memory/library_teardown.hpp"
#include "gui_forms/allocation/library_memory/program_allocator.hpp"

#include <stxlib/error.hpp>
#include <stxlib/fail_fast.hpp>
#include <stxlib/memory.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gui_forms::detail {

namespace {

// The observer, set by tests before the threads it should see start; read
// once per allocation.
constinit std::atomic<LibraryAllocationObserver> allocation_observer{nullptr};

// What the program's replacement of operator new reported (the header's
// ProgramAllocator); constant-initialized, so a static initializer of any
// translation unit may set it.
constinit std::atomic<ProgramAllocator> program_allocator_state{ProgramAllocator::not_linked};

// The calling thread's counts: trivially destructible, written and read
// only by their own thread.
constinit thread_local std::uint64_t thread_granted = 0U;
constinit thread_local std::uint64_t thread_released = 0U;

// The teardown reserve: a fixed block of this module, static storage
// zeroed by the loader, so serving from it needs no allocator, no lock and
// no other thread. Its size bounds what a module's static destructors may
// allocate after STXlib's allocator refuses: they release far more than
// they allocate, and a destructor that needs more is the defect the
// fail-fast record names.
constexpr std::size_t teardown_reserve_bytes = 256U * 1024U;
alignas(64) constinit unsigned char teardown_reserve[teardown_reserve_bytes] = {};

// The reserve's bytes handed out: blocks are carved in order and never
// reused. Atomic, since a thread that has not been stopped (an unload, not
// the process's end) may allocate beside the detaching one.
constinit std::atomic<std::size_t> teardown_reserve_offset{0U};

// Set once the module's teardown began; never cleared.
constinit std::atomic<bool> teardown_begun{false};

[[nodiscard]] std::uintptr_t teardown_reserve_base() noexcept {
    const void* const first = static_cast<const void*>(&teardown_reserve[0]);
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(first);
    return base;
}

// The reserve cannot hold an allocation made during the module's teardown.
[[noreturn]] void fail_teardown_reserve() noexcept {
    const stxlib::ErrorRecord record =
        stxlib::ErrorRecord::make(stxlib::ErrorKind::out_of_memory, "gf.allocation.teardown_reserve");
    stxlib::fail_fast(record);
}

} // namespace

void begin_library_teardown() noexcept {
    teardown_begun.store(true, std::memory_order_release);
}

bool library_teardown_begun() noexcept {
    const bool begun = teardown_begun.load(std::memory_order_acquire);
    return begun;
}

void* teardown_allocate(const std::size_t size, const std::size_t alignment) noexcept {
    const std::uintptr_t base = teardown_reserve_base();
    const std::size_t asked = library_alignment(alignment);
    const std::size_t bytes = size == 0U ? 1U : size;
    if (asked > teardown_reserve_bytes) {
        fail_teardown_reserve();
    }
    std::size_t offset = teardown_reserve_offset.load(std::memory_order_relaxed);
    while (true) {
        // The first address at or after the free bytes with the alignment
        // asked for; both terms are below the reserve's end plus one
        // alignment, so the sum cannot wrap.
        const std::uintptr_t unaligned = base + static_cast<std::uintptr_t>(offset);
        const std::uintptr_t mask = static_cast<std::uintptr_t>(asked) - 1U;
        const std::uintptr_t start = (unaligned + mask) & ~mask;
        const std::size_t begin = static_cast<std::size_t>(start - base);
        if (begin > teardown_reserve_bytes || bytes > teardown_reserve_bytes - begin) {
            fail_teardown_reserve();
        }
        const std::size_t end = begin + bytes;
        const bool claimed = teardown_reserve_offset.compare_exchange_weak(offset, end, std::memory_order_acq_rel,
                                                                           std::memory_order_relaxed);
        if (claimed) {
            void* const block = static_cast<void*>(&teardown_reserve[begin]);
            return block;
        }
    }
}

bool teardown_reserve_owns(const void* const block) noexcept {
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(block);
    const std::uintptr_t base = teardown_reserve_base();
    const bool owned = address >= base && address - base < static_cast<std::uintptr_t>(teardown_reserve_bytes);
    return owned;
}

std::size_t teardown_reserve_used() noexcept {
    const std::size_t used = teardown_reserve_offset.load(std::memory_order_acquire);
    return used;
}

std::size_t teardown_reserve_capacity() noexcept {
    return teardown_reserve_bytes;
}

void* library_allocate(const std::size_t size, const std::size_t alignment) noexcept {
    const LibraryAllocationObserver observer = allocation_observer.load(std::memory_order_acquire);
    bool admitted = true;
    if (observer != nullptr) {
        admitted = (*observer)(size);
    }
    void* block = nullptr;
    if (admitted) {
        block = stxlib::memory::allocate(size, alignment);
    }
    if (block == nullptr) {
        // Refused: exhaustion for the caller, unless the module's teardown
        // began, when a static destructor must not see it.
        if (!library_teardown_begun()) {
            return nullptr;
        }
        block = teardown_allocate(size, alignment);
    }
    thread_granted = thread_granted + 1U;
    return block;
}

void library_release(void* const block) noexcept {
    if (block == nullptr) {
        return;
    }
    if (!teardown_reserve_owns(block)) {
        stxlib::memory::release(block);
    }
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

void note_program_allocator(const ProgramAllocator state) noexcept {
    program_allocator_state.store(state, std::memory_order_release);
}

ProgramAllocator program_allocator() noexcept {
    const ProgramAllocator state = program_allocator_state.load(std::memory_order_acquire);
    return state;
}

} // namespace gui_forms::detail
