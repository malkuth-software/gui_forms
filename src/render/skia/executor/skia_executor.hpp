#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gui_drawing::render {

enum class RasterError : std::uint8_t {
    none,
    wrong_thread,
    invalid_argument,
    target_unavailable,
    missing_image_pixels,
    decode_failed,
    encode_failed,
    encoded_limit_exceeded,
    dimension_limit_exceeded,
    internal_error,
};

struct GUI_FORMS_API RasterResult final {
    RasterError error{RasterError::none};
    std::size_t commands_executed{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == RasterError::none;
    }
};

struct GUI_FORMS_API DecodeResult final {
    std::unique_ptr<Bitmap> bitmap;
    RasterError error{RasterError::none};

    [[nodiscard]] static DecodeResult failure(RasterError failure_error) {
        return DecodeResult{{}, failure_error};
    }
    [[nodiscard]] static DecodeResult success(
        std::unique_ptr<Bitmap> decoded_bitmap) {
        return DecodeResult{std::move(decoded_bitmap), RasterError::none};
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return bitmap != nullptr && error == RasterError::none;
    }
};

struct GUI_FORMS_API PngCodecLimits final {
    std::size_t maximum_encoded_bytes{32U * 1024U * 1024U};
    std::uint32_t maximum_width{4096};
    std::uint32_t maximum_height{4096};
    std::uint64_t maximum_pixels{4096ULL * 4096ULL};
};

class GUI_FORMS_API SkiaExecutor final {
public:
    SkiaExecutor();
    ~SkiaExecutor();
    SkiaExecutor(const SkiaExecutor&) = delete;
    SkiaExecutor& operator=(const SkiaExecutor&) = delete;

    // Registers a face in the slot named by the bold (1) and italic (2)
    // bits of style. A request for a missing slot draws the nearest
    // registered face, emboldened or sheared toward the request.
    [[nodiscard]] bool register_typeface(std::string_view family,
                                         std::span<const std::byte> encoded,
                                         std::uint32_t style = 0U);
    // Draws requests for one family with another registered family's faces
    // (stage F substitution). False when family is not registered.
    [[nodiscard]] bool alias_family(std::string_view requested, std::string_view family);
    [[nodiscard]] RasterResult execute(const GraphicsRecorder& recorder,
                                       Bitmap& target,
                                       std::size_t first_command = 0U);
    [[nodiscard]] SizeF measure_string(std::string_view utf8,
                                       const FontSnapshot& font,
                                       const StringFormatSnapshot& format,
                                       double layout_width = 0.0);
    [[nodiscard]] DecodeResult decode_png(
        std::span<const std::byte> encoded,
        const PngCodecLimits& limits = {});
    [[nodiscard]] std::vector<std::byte> encode_png(
        const Bitmap& bitmap, RasterError* error = nullptr,
        const PngCodecLimits& limits = {});

private:
    [[nodiscard]] bool owner_thread() const noexcept;

    class Impl;
    std::unique_ptr<Impl> impl_;
    std::thread::id owner_thread_;
};

} // namespace gui_drawing::render
