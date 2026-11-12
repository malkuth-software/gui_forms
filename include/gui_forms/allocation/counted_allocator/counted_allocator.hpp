#pragma once

// The allocator of GUI.Forms' counted paths (allocation_statistics.hpp says
// what is counted). A container of a counted path names its subsystem in its
// allocator type; every allocation and release of the container's storage is
// then counted under that subsystem.
//
// Memory: allocate calls the global operator new and deallocate the matching
// global operator delete, with the same byte count and, for over-aligned
// values, the same alignment, so the memory never leaves the library's own
// new/delete pair. Stateless: every instance of one specialization is
// interchangeable (is_always_equal), so containers move storage between them
// freely. Usable for an incomplete value type (allocator completeness), as
// nothing here depends on the value type before allocate is instantiated.

#include "gui_forms/allocation/statistics/allocation_statistics.hpp"

#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

namespace gui_forms::detail {

template <typename Value, AllocationSubsystem Subsystem>
class CountedAllocator final {
public:
    using value_type = Value;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;

    // The subsystem is a non-type parameter, which allocator_traits' default
    // rebind does not carry over; this one does.
    template <typename Other>
    struct rebind {
        using other = CountedAllocator<Other, Subsystem>;
    };

    CountedAllocator() noexcept {}
    template <typename Other>
    CountedAllocator(const CountedAllocator<Other, Subsystem>&) noexcept {}

    // Storage for `count` values, uninitialized. std::bad_array_new_length
    // when count * sizeof(Value) overflows; std::bad_alloc from operator new.
    // Counts nothing when it throws.
    [[nodiscard]] Value* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
            throw std::bad_array_new_length();
        }
        const std::size_t bytes = count * sizeof(Value);
        void* memory = nullptr;
        if constexpr (alignof(Value) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            memory = ::operator new(bytes, std::align_val_t(alignof(Value)));
        } else {
            memory = ::operator new(bytes);
        }
        count_allocation(Subsystem, bytes);
        Value* const values = static_cast<Value*>(memory);
        return values;
    }

    // Releases storage allocate(count) returned.
    void deallocate(Value* values, std::size_t count) noexcept {
        const std::size_t bytes = count * sizeof(Value);
        if constexpr (alignof(Value) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            ::operator delete(static_cast<void*>(values), bytes, std::align_val_t(alignof(Value)));
        } else {
            ::operator delete(static_cast<void*>(values), bytes);
        }
        count_release(Subsystem, bytes);
    }
};

// Instances of one subsystem are interchangeable whatever their value types.
template <typename Left, typename Right, AllocationSubsystem Subsystem>
[[nodiscard]] constexpr bool operator==(const CountedAllocator<Left, Subsystem>&,
                                        const CountedAllocator<Right, Subsystem>&) noexcept {
    return true;
}

template <typename Left, typename Right, AllocationSubsystem Subsystem>
[[nodiscard]] constexpr bool operator!=(const CountedAllocator<Left, Subsystem>&,
                                        const CountedAllocator<Right, Subsystem>&) noexcept {
    return false;
}

} // namespace gui_forms::detail
