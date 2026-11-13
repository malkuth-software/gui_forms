#pragma once

// Between STXlib's frame records (stxlib/shadow.hpp, one stack per thread in
// stxlib_state) and the library's ShadowFrame, the C ABI 1.2 layout. The
// public header names no STXlib type, so these live here; exported for the C
// ABI library, whose callback gates copy their frame texts through them.

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/export/application_export.hpp"

#include <stxlib/error.hpp>
#include <stxlib/shadow.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/types.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms::detail {

// The library's frame for an STXlib record. STXlib's gate kind becomes
// ShadowFrameKind::gate; first and second become the 32-bit interface id
// and function index (the library pushes only 32-bit values there).
[[nodiscard]] GUI_FORMS_API ShadowFrame frame_of_record(const stxlib::ShadowFrameRecord& record) noexcept;

// text as STXlib text (frame texts, the fail-fast report's lines): all of
// it when it is well-formed UTF-8, else its well-formed prefix (the bytes
// before the first ill-formed one). The view borrows text.
[[nodiscard]] GUI_FORMS_API stxlib::TextView well_formed_text(std::string_view text) noexcept;

// What copy_record_frames read: the frames the record holds (which may
// exceed the number copied), the depth at the capture, recorded or not, and
// the return addresses copied into the trace.
struct RecordFrames final {
    std::size_t recorded = 0U;
    std::uint32_t depth = 0U;
    std::size_t trace_count = 0U;
};

// Copies the frames of the calling thread's shadow record reference into
// the leading elements of frames, outermost first, and its native trace
// (development builds) into the leading elements of trace, innermost first.
// A reference that is not present, or that the thread has overwritten,
// reads as nothing. Never throws or allocates.
[[nodiscard]] GUI_FORMS_API RecordFrames copy_record_frames(stxlib::ShadowReference reference,
                                                            std::span<ShadowFrame> frames,
                                                            std::span<std::uint64_t> trace) noexcept;

} // namespace gui_forms::detail
