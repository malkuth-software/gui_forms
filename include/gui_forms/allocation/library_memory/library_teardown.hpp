#pragma once

// The module's teardown and its reserve (library_memory.hpp). Kept out of
// library_memory.hpp, which most of GUI.Forms reads, so that a change here
// rebuilds only the files that use it: the seam itself, the module-level
// allocator (src/runtime/program_allocator/program_allocator_replacement.cpp)
// and the C ABI library's DllMain.
//
// A module's end (the C ABI library's unload, and the process's end). On
// Windows the library's static destructors run at its DLL_PROCESS_DETACH,
// under the loader lock and, at process end, after ExitProcess has stopped
// every other thread, when STXlib's allocator refuses whatever the calling
// thread's cached blocks cannot serve (stxlib/memory.hpp, "Process end").
// A static destructor must never meet std::bad_alloc there, so the module
// marks its teardown (begin_library_teardown, from its DllMain:
// src/surface/windows/abi/c_abi_module_teardown.cpp) and from then on:
// - every release succeeds: STXlib's release returns at process end (it
//   frees, or leaves the block to the ending process), and a block from the
//   teardown reserve is left where it is;
// - an allocation STXlib's allocator refuses comes from the teardown
//   reserve, a fixed static block of this module; one the reserve cannot
//   hold is a defect and ends the process through stxlib::fail_fast with an
//   out_of_memory record (gf.allocation.teardown_reserve), never a thrown
//   std::bad_alloc.
// On Linux and macOS no thread is stopped at exit or dlclose, so STXlib's
// allocator serves static destructors as it serves any code; only true
// exhaustion refuses there, as everywhere.

#include "gui_forms/export/application_export.hpp"

#include <cstddef>

namespace gui_forms::detail {

// Marks the module's teardown (above); never undone. Called by the C ABI
// library's DllMain at DLL_PROCESS_DETACH, and by tests.
GUI_FORMS_API void begin_library_teardown() noexcept;

// Whether begin_library_teardown has been called.
[[nodiscard]] GUI_FORMS_API bool library_teardown_begun() noexcept;

// A block of `size` bytes aligned to `alignment` (a power of two) from the
// teardown reserve, for an allocation STXlib's allocator refused after the
// teardown began. Never null and never throws: when the reserve cannot hold
// it, ends the process through stxlib::fail_fast. Its blocks are never
// reused.
[[nodiscard]] GUI_FORMS_API void* teardown_allocate(std::size_t size, std::size_t alignment) noexcept;

// Whether `block` lies in the teardown reserve; releasing such a block does
// nothing.
[[nodiscard]] GUI_FORMS_API bool teardown_reserve_owns(const void* block) noexcept;

// The bytes of the teardown reserve handed out so far, and its capacity.
[[nodiscard]] GUI_FORMS_API std::size_t teardown_reserve_used() noexcept;
[[nodiscard]] GUI_FORMS_API std::size_t teardown_reserve_capacity() noexcept;

} // namespace gui_forms::detail
