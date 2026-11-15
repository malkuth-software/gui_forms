#pragma once

// STXlib's process state as the Application archive's code sees it, for
// tests/stxlib_state_sharing_tests.cpp. stxlib_state (the allocator with
// mimalloc, the thread identity issuer, the SIMD level) is a static archive
// linked once into each module (one module, owner decision 2026-11-15), as
// are STXlib's other archives (stxlib::boundary, stxlib::algorithms). The test
// compares what the Application's path and the program's own path reach: a
// block allocated through one and released through the other, and the thread
// identities each reads. A second stxlib_state in the module (two allocators,
// two issuers) fails it, by a refused release in tag-checking builds and by
// differing identities in every build.
//
// Private: declared here for the test only. The mark is kept, though no
// shared library exports it now.

#include "gui_forms/export/application_export.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms::runtime {

// A block of `size` bytes, aligned to 16, from STXlib's allocator through this
// library's copy of stxlib::memory, with every byte set to `fill`; null when
// the allocator refuses. The caller owns it and releases it through
// stxlib::memory::release (in any module: the allocator is stxlib_state's).
[[nodiscard]] GUI_FORMS_API void* stxlib_state_probe_allocate(std::size_t size, unsigned char fill) noexcept;

// Releases a block of STXlib's allocator through this library's copy of
// stxlib::memory::release; null is a no-op. A block from anywhere else ends
// the process in tag-checking builds (stxlib/memory.hpp).
GUI_FORMS_API void stxlib_state_probe_release(void* block) noexcept;

// The calling thread's STXlib identity (stxlib::current_thread().value()) as
// this library's copy reads it, issued on the thread's first call to the
// issuer in stxlib_state.
[[nodiscard]] GUI_FORMS_API std::uint64_t stxlib_state_probe_thread() noexcept;

} // namespace gui_forms::runtime
