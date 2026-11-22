#pragma once

// GUI.Drawing's raster for C++ callers: a recording executed into a
// Bitmap the caller owns, outside any paint (for an image editor's own
// buffers, exports and tests). It is part of the static drawing raster
// archive (gui_drawing_skia), which the Application archive carries, and
// it is the execution the drawing ABI's recorder_execute calls too, so C,
// C# and C++ callers share one implementation. The renderer-free drawing
// library (GUIForms::Drawing) stays renderer-free.
//
// Provisional, in the drawing ABI's 1.1 (GD_ABI_VERSION_1_1), outside the
// sealed drawing ABI 1.0; its names and status values may still change. The C ABI is
// unchanged; C callers keep recorder_execute.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/bitmap/bitmap.hpp"
#include "gui_forms/drawing/graphics_recorder/graphics_recorder.hpp"

#include <cstdint>

namespace gui_drawing {

enum class RecorderRasterStatus : std::uint8_t {
    // The commands ran (commands_executed of them, possibly none).
    executed = 0,
    // The recorder or the bitmap belongs to another thread.
    wrong_thread = 1,
    // A disposed recorder or bitmap, a first command past the recording's
    // end, or a command the raster refuses (a missing image's pixels, a
    // malformed path).
    invalid_argument = 2,
    // A command exceeds the raster's dimension or size limits.
    limit_exceeded = 3,
    // The raster module could not make its state for this thread.
    out_of_memory = 4,
    // The raster module's per-thread state is ending or has ended on this
    // thread (the thread is exiting, or the process is shutting down).
    not_available = 5,
    // The renderer failed (a target it could not wrap, an internal error).
    internal_error = 6,
};

struct GUI_FORMS_API RecorderRasterResult final {
    RecorderRasterStatus status{RecorderRasterStatus::executed};
    // The commands drawn from `first_command` on.
    std::uint64_t commands_executed{};
};

// Draws `recorder`'s commands from `first_command` on into `target`, in
// place, on the calling thread, which must own both objects. The caller
// keeps both alive for the call; nothing is retained after it. `target`
// may borrow the caller's pixels (Bitmap::borrow), which are then written
// directly, with no copy.
//
// What a failure leaves: for a bitmap owning its pixels the commands are
// checked first and a refusal leaves it untouched; a borrowed bitmap, or
// a failure during drawing, keeps the commands before the failing one
// drawn (commands_executed counts them). Fonts the commands name are
// resolved and registered on this thread first, as recorder_execute does;
// no gdr_initialize call is needed. Never throws.
[[nodiscard]] GUI_FORMS_API RecorderRasterResult execute_recorder(const GraphicsRecorder& recorder, Bitmap& target,
                                                                  std::uint64_t first_command) noexcept;

} // namespace gui_drawing
