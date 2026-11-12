#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms {

/// A borrowed, top-to-bottom, premultiplied BGRA8 sRGB image: blue, green,
/// red, alpha in memory, each colour channel at most its alpha, as every
/// framebuffer and raster holds pixels. Row padding is permitted; the final
/// row does not require trailing padding.
/// The producer retains ownership and keeps the bytes alive for the whole call.
struct GUI_FORMS_API HostImageView final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t row_bytes{};
    std::span<const std::byte> pixels;
};

/// An owned host-service image, premultiplied BGRA8 like HostImageView.
/// Clipboard reads return independent storage. A GUI.Forms clipboard image is
/// read back byte for byte; straight-alpha native formats (PNG, DIB, icons)
/// are converted only where they meet the host, and a valid premultiplied
/// image survives that conversion unchanged.
struct GUI_FORMS_API HostImage final {
    static constexpr std::uint64_t maximum_pixels = 100'000'000U;
    static constexpr std::uint64_t maximum_bytes = maximum_pixels * 4U;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t row_bytes{};
    std::vector<std::byte> pixels;

    [[nodiscard]] HostImageView view() const noexcept {
        const std::span<const std::byte> bytes(pixels.data(), pixels.size());
        return {width, height, row_bytes, bytes};
    }
};

enum class HostImageError : std::uint8_t {
    none,
    invalid_dimensions,
    too_large,
    invalid_stride,
    short_buffer,
};

/// Validates dimensions, the 100-megapixel bound and the readable row span.
/// Performs no allocation and never multiplies an unchecked source stride.
[[nodiscard]] GUI_FORMS_API HostImageError validate_host_image(HostImageView image) noexcept;

} // namespace gui_forms
