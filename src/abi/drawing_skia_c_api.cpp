#include "gui_forms/drawing_c_api.h"
#include "gui_forms/drawing/recorder_raster/recorder_raster.hpp"

#include "drawing_raster_service.h"
#include "drawing_raster_state.hpp"
#include "drawing_skia.hpp"
#include "font_resolution_record.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

// The drawing ABI's raster service: the C callbacks the gd_ table's
// raster_initialize installs
// (recorder execution, PNG coding, measured text, font resolution) over the
// raster state the C++ entry shares (drawing_raster_state.hpp).

namespace {

using gui_drawing::Bitmap;
using gui_drawing::GraphicsRecorder;
using gui_drawing::render::DecodeResult;
using gui_drawing::render::FontRequest;
using gui_drawing::render::RasterError;
using gui_drawing::render::RasterRecordRefusal;
using gui_drawing::render::RasterRecordUnavailable;
using gui_drawing::render::ensure_font;
using gui_drawing::render::raster_executor;
using gui_drawing::render::register_packaged_fonts;

gd_result translate_error(RasterError error) noexcept {
    switch (error) {
    case RasterError::none: return GD_OK;
    case RasterError::wrong_thread: return GD_ERROR_WRONG_THREAD;
    case RasterError::invalid_argument:
    case RasterError::missing_image_pixels:
    case RasterError::decode_failed: return GD_ERROR_INVALID_ARGUMENT;
    case RasterError::encoded_limit_exceeded:
    case RasterError::dimension_limit_exceeded: return GD_ERROR_LIMIT_EXCEEDED;
    case RasterError::target_unavailable:
    case RasterError::encode_failed:
    case RasterError::internal_error: return GD_ERROR_INTERNAL;
    }
    return GD_ERROR_INTERNAL;
}

gd_result refusal_result(const RasterRecordUnavailable& unavailable) noexcept {
    switch (unavailable.refusal) {
    case RasterRecordRefusal::out_of_memory: return GD_ERROR_OUT_OF_MEMORY;
    case RasterRecordRefusal::not_available: return GD_ERROR_NOT_AVAILABLE;
    }
    return GD_ERROR_NOT_AVAILABLE;
}

// The drawing ABI's recorder_execute, with handles the registry has
// validated: the C++ execute_recorder (src/render/skia/executor/
// recorder_raster.cpp), the one execution C and C++ callers share.
gd_result execute(const void* recorder, void* bitmap,
                  std::uint64_t first_command,
                  std::uint64_t* commands_executed) {
    if (recorder == nullptr || bitmap == nullptr || commands_executed == nullptr) {
        return GD_ERROR_INVALID_ARGUMENT;
    }
    const gui_drawing::RecorderRasterResult drawn = gui_drawing::execute_recorder(
        *static_cast<const GraphicsRecorder*>(recorder), *static_cast<Bitmap*>(bitmap), first_command);
    *commands_executed = drawn.commands_executed;
    switch (drawn.status) {
    case gui_drawing::RecorderRasterStatus::executed: return GD_OK;
    case gui_drawing::RecorderRasterStatus::wrong_thread: return GD_ERROR_WRONG_THREAD;
    case gui_drawing::RecorderRasterStatus::invalid_argument: return GD_ERROR_INVALID_ARGUMENT;
    case gui_drawing::RecorderRasterStatus::limit_exceeded: return GD_ERROR_LIMIT_EXCEEDED;
    case gui_drawing::RecorderRasterStatus::out_of_memory: return GD_ERROR_OUT_OF_MEMORY;
    case gui_drawing::RecorderRasterStatus::not_available: return GD_ERROR_NOT_AVAILABLE;
    case gui_drawing::RecorderRasterStatus::internal_error: return GD_ERROR_INTERNAL;
    }
    return GD_ERROR_INTERNAL;
}

gd_result encode_png(const void* bitmap, void* buffer, std::uint64_t capacity,
                     std::uint64_t* required_size) {
    if (bitmap == nullptr || required_size == nullptr) {
        return GD_ERROR_INVALID_ARGUMENT;
    }
    try {
        RasterError error = RasterError::internal_error;
        const std::vector<std::byte> png = raster_executor().encode_png(
            *static_cast<const Bitmap*>(bitmap), &error);
        if (error != RasterError::none) return translate_error(error);
        *required_size = png.size();
        if (capacity < png.size() || (!png.empty() && buffer == nullptr)) {
            return GD_ERROR_BUFFER_TOO_SMALL;
        }
        if (!png.empty()) std::memcpy(buffer, png.data(), png.size());
        return GD_OK;
    } catch (const RasterRecordUnavailable& refusal) {
        return refusal_result(refusal);
    } catch (...) {
        return GD_ERROR_INTERNAL;
    }
}

gd_result decode_png(const void* data, std::uint64_t size, void** bitmap) {
    if (data == nullptr || size == 0U || bitmap == nullptr ||
        size > std::numeric_limits<std::size_t>::max()) {
        return GD_ERROR_INVALID_ARGUMENT;
    }
    *bitmap = nullptr;
    try {
        DecodeResult result = raster_executor().decode_png(
            {static_cast<const std::byte*>(data), static_cast<std::size_t>(size)});
        if (!result) return translate_error(result.error);
        *bitmap = result.bitmap.release();
        return GD_OK;
    } catch (const RasterRecordUnavailable& refusal) {
        return refusal_result(refusal);
    } catch (...) {
        return GD_ERROR_INTERNAL;
    }
}

gd_result measure_string(const void* font, const void* format,
                         gd_string_view text, double layout_width,
                         gd_size* measured) {
    if (font == nullptr || measured == nullptr ||
        (text.size != 0U && text.data == nullptr) ||
        text.size > gui_drawing::GraphicsRecorder::maximum_text_bytes ||
        !std::isfinite(layout_width) || layout_width < 0.0) {
        return GD_ERROR_INVALID_ARGUMENT;
    }
    try {
        register_packaged_fonts();
        const gui_drawing::Font& native_font = *static_cast<const gui_drawing::Font*>(font);
        const gui_drawing::FontSnapshot font_snapshot = native_font.snapshot();
        static_cast<void>(ensure_font(font_snapshot.family, font_snapshot.style));
        gui_drawing::StringFormatSnapshot native_format{};
        if (format != nullptr) {
            native_format = (*static_cast<const gui_drawing::StringFormat*>(format)).snapshot();
        }
        const gui_drawing::SizeF size = raster_executor().measure_string(
            {text.data, static_cast<std::size_t>(text.size)},
            font_snapshot, native_format, layout_width);
        (*measured).width = size.width;
        (*measured).height = size.height;
        return GD_OK;
    } catch (const RasterRecordUnavailable& refusal) {
        return refusal_result(refusal);
    } catch (...) {
        return GD_ERROR_INTERNAL;
    }
}

gd_result resolve_font(const void* font, gd_font_resolution* resolution) {
    if (font == nullptr || resolution == nullptr) return GD_ERROR_INVALID_ARGUMENT;
    try {
        register_packaged_fonts();
        const gui_drawing::FontSnapshot snapshot =
            (*static_cast<const gui_drawing::Font*>(font)).snapshot();
        const FontRequest& request = ensure_font(snapshot.family, snapshot.style);
        gui_drawing::abi::fill_font_resolution(request.resolution, request.face_loaded, *resolution);
        return GD_OK;
    } catch (const RasterRecordUnavailable& refusal) {
        return refusal_result(refusal);
    } catch (...) {
        return GD_ERROR_INTERNAL;
    }
}

} // namespace

extern "C" gd_result gui_drawing_raster_module_initialize(void) {
    gd_api_v0 api{};
    api.struct_size = sizeof(api);
    const gd_result loaded = gd_get_api_v0(GD_ABI_VERSION_0_1, &api);
    if (loaded != GD_OK) return loaded;
    if (api.raster_service_install == nullptr) return GD_ERROR_UNSUPPORTED_VERSION;
    const gd_raster_service_v0 service{
        sizeof(gd_raster_service_v0), GD_ABI_VERSION_0_1,
        &execute, &encode_png, &decode_png, &measure_string, &resolve_font,
    };
    const gd_result installed = api.raster_service_install(&service);
    if (installed != GD_OK) return installed;
    // The packaged faces on the installing thread. A failure (no record on
    // this thread, or no memory) leaves them to the thread's first raster
    // call, which registers them or refuses itself.
    try {
        register_packaged_fonts();
    } catch (...) {
        static_cast<void>(0);
    }
    return installed;
}
