#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/types/pixel_format/pixel_format.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gui_drawing {

struct PixelStorage;

// Immutable image identity and optional shared CPU pixel projection.
struct GUI_FORMS_API ImageSnapshot final {
    std::uint64_t stable_id{};
    std::uint32_t width{};
    std::uint32_t height{};
    PixelFormat pixel_format{PixelFormat::bgra32_premultiplied};
    std::uint64_t generation{};

    [[nodiscard]] bool has_pixels() const noexcept;
    [[nodiscard]] std::size_t row_bytes() const noexcept;
    [[nodiscard]] std::span<const std::byte> pixels() const noexcept;

private:
    std::shared_ptr<const PixelStorage> storage_;
    friend class Bitmap;
};

} // namespace gui_drawing
