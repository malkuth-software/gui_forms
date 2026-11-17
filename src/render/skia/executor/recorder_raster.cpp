// gui_drawing::execute_recorder (drawing/recorder_raster/recorder_raster.hpp):
// a recording drawn into a caller-owned Bitmap outside paint. The C ABI's
// raster service execute (src/abi/drawing_skia_c_api.cpp) calls this same
// function, so C, C# and C++ callers share one execution.
#include "gui_forms/drawing/recorder_raster/recorder_raster.hpp"

#include "drawing_raster_state.hpp"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace gui_drawing {
namespace {

RecorderRasterStatus status_of(render::RasterError error) noexcept {
    switch (error) {
    case render::RasterError::none: return RecorderRasterStatus::executed;
    case render::RasterError::wrong_thread: return RecorderRasterStatus::wrong_thread;
    case render::RasterError::invalid_argument:
    case render::RasterError::missing_image_pixels:
    case render::RasterError::decode_failed: return RecorderRasterStatus::invalid_argument;
    case render::RasterError::encoded_limit_exceeded:
    case render::RasterError::dimension_limit_exceeded: return RecorderRasterStatus::limit_exceeded;
    case render::RasterError::target_unavailable:
    case render::RasterError::encode_failed:
    case render::RasterError::internal_error: return RecorderRasterStatus::internal_error;
    }
    return RecorderRasterStatus::internal_error;
}

RecorderRasterStatus status_of(render::RasterRecordRefusal refusal) noexcept {
    switch (refusal) {
    case render::RasterRecordRefusal::out_of_memory: return RecorderRasterStatus::out_of_memory;
    case render::RasterRecordRefusal::not_available: return RecorderRasterStatus::not_available;
    }
    return RecorderRasterStatus::not_available;
}

} // namespace

RecorderRasterResult execute_recorder(const GraphicsRecorder& recorder, Bitmap& target,
                                      std::uint64_t first_command) noexcept {
    RecorderRasterResult result{};
    // Owner thread and disposal first: the C ABI's handle registry has
    // checked them already for its callers; a C++ caller's objects are
    // checked here (the executor would see them only as a lock's exception).
    try {
        if (recorder.is_disposed() || target.is_disposed()) {
            result.status = RecorderRasterStatus::invalid_argument;
            return result;
        }
    } catch (const std::logic_error&) {
        result.status = RecorderRasterStatus::wrong_thread;
        return result;
    } catch (...) {
        result.status = RecorderRasterStatus::internal_error;
        return result;
    }
    if (first_command > std::numeric_limits<std::size_t>::max()) {
        result.status = RecorderRasterStatus::limit_exceeded;
        return result;
    }
    try {
        render::register_packaged_fonts();
        render::ensure_recorder_fonts(recorder, first_command);
        const render::RasterResult drawn =
            render::raster_executor().execute(recorder, target, static_cast<std::size_t>(first_command));
        result.status = status_of(drawn.error);
        result.commands_executed = drawn.commands_executed;
    } catch (const render::RasterRecordUnavailable& unavailable) {
        result.status = status_of(unavailable.refusal);
    } catch (...) {
        result.status = RecorderRasterStatus::internal_error;
    }
    return result;
}

} // namespace gui_drawing
