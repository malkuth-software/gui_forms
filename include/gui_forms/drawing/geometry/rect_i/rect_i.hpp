#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/geometry/point_i/point_i.hpp"

#include <cstdint>

namespace gui_drawing {

struct GUI_FORMS_API RectI final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] constexpr std::int64_t left() const noexcept { return x; }
    [[nodiscard]] constexpr std::int64_t top() const noexcept { return y; }
    [[nodiscard]] constexpr std::int64_t right() const noexcept {
        return static_cast<std::int64_t>(x) + width;
    }
    [[nodiscard]] constexpr std::int64_t bottom() const noexcept {
        return static_cast<std::int64_t>(y) + height;
    }
    [[nodiscard]] constexpr bool empty() const noexcept {
        return width <= 0 || height <= 0;
    }
    [[nodiscard]] bool contains(PointI point) const noexcept;
    [[nodiscard]] bool contains(RectI rect) const noexcept;
    [[nodiscard]] bool intersects(RectI rect) const noexcept;
    void offset(std::int32_t dx, std::int32_t dy) noexcept;
    void inflate(std::int32_t dx, std::int32_t dy) noexcept;
    void intersect(RectI rect) noexcept;
    [[nodiscard]] static RectI intersection(RectI left, RectI right) noexcept;
    [[nodiscard]] static RectI united(RectI left, RectI right) noexcept;
    friend constexpr bool operator==(const RectI& left,
                                     const RectI& right) noexcept {
        return left.x == right.x && left.y == right.y &&
               left.width == right.width && left.height == right.height;
    }
};

} // namespace gui_drawing
