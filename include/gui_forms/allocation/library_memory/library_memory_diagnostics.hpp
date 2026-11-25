#pragma once

// The library seam's diagnostics (library_memory.hpp): a test-only observer
// that sees, and may refuse, each library allocation, and the calling
// thread's counts of library allocations and releases, so tests that count
// or fail the program's allocations see the library's in the same order.
// Kept out of library_memory.hpp, which most of GUI.Forms reads, so that a
// change here rebuilds only the files that use them.

#include "gui_forms/export/application_export.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms::detail {

// The test-only observer: called with the size before each library
// allocation, from the allocating thread; false refuses the allocation (the
// caller then sees exhaustion: std::bad_alloc from the routes of
// library_memory.hpp). Must not allocate through GUI.Forms, throw or block.
// Null, the default, observes nothing. Process-wide; set it before the
// threads it should see start.
using LibraryAllocationObserver = bool (*)(std::size_t size) noexcept;
GUI_FORMS_API void set_library_allocation_observer(LibraryAllocationObserver observer) noexcept;

// The calling thread's library allocations granted and blocks released
// since it started. Neither is reset.
[[nodiscard]] GUI_FORMS_API std::uint64_t library_allocations_granted() noexcept;
[[nodiscard]] GUI_FORMS_API std::uint64_t library_blocks_released() noexcept;

} // namespace gui_forms::detail
