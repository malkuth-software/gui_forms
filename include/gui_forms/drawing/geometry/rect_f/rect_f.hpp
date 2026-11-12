#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/geometry/point_f/point_f.hpp"

namespace gui_drawing {

struct GUI_FORMS_API RectF final {
    double x{};
    double y{};
    double width{};
    double height{};

    [[nodiscard]] constexpr double left() const noexcept { return x; }
    [[nodiscard]] constexpr double top() const noexcept { return y; }
    [[nodiscard]] constexpr double right() const noexcept { return x + width; }
    [[nodiscard]] constexpr double bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr bool empty() const noexcept {
        return width <= 0.0 || height <= 0.0;
    }
    [[nodiscard]] bool finite() const noexcept;
    [[nodiscard]] bool contains(PointF point) const noexcept;
    [[nodiscard]] bool contains(RectF rect) const noexcept;
    [[nodiscard]] bool intersects(RectF rect) const noexcept;
    void offset(double dx, double dy);
    void inflate(double dx, double dy);
    void intersect(RectF rect) noexcept;
    [[nodiscard]] static RectF intersection(RectF left, RectF right) noexcept;
    [[nodiscard]] static RectF united(RectF left, RectF right) noexcept;
    friend constexpr bool operator==(const RectF& left,
                                     const RectF& right) noexcept {
        return left.x == right.x && left.y == right.y &&
               left.width == right.width && left.height == right.height;
    }
};

} // namespace gui_drawing
