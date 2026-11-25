#pragma once

// The whole-program allocator's report (library_memory.hpp): what the
// program's replacement of operator new and delete
// (src/runtime/program_allocator/program_allocator_replacement.cpp, compiled
// into a program by gui_forms_use_library_allocator) said at static
// initialization. Kept out of library_memory.hpp, which most of GUI.Forms
// reads, so that a change here rebuilds only the files that use it.

#include "gui_forms/export/application_export.hpp"

namespace gui_forms::detail {

// not_linked: the program did not opt in; left_out: it opted in, and the
// replacement left itself out (a sanitizer build, or
// GUI_FORMS_HOST_OPERATOR_NEW); replaced: operator new and delete are
// STXlib's allocator. Read it after static initialization (from main on).
enum class ProgramAllocator : unsigned char { not_linked = 0, left_out = 1, replaced = 2 };
GUI_FORMS_API void note_program_allocator(ProgramAllocator state) noexcept;
[[nodiscard]] GUI_FORMS_API ProgramAllocator program_allocator() noexcept;

} // namespace gui_forms::detail
