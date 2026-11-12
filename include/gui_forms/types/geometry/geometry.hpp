#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cmath>

namespace gui_forms {

struct GUI_FORMS_API Point {
    double x{};
    double y{};
    friend constexpr bool operator==(const Point& left,
                                     const Point& right) noexcept {
        return left.x == right.x && left.y == right.y;
    }
};

struct GUI_FORMS_API Size {
    double width{};
    double height{};
    friend constexpr bool operator==(const Size& left,
                                     const Size& right) noexcept {
        return left.width == right.width && left.height == right.height;
    }
};

struct GUI_FORMS_API Rect {
    double x{};
    double y{};
    double width{};
    double height{};
    friend constexpr bool operator==(const Rect& left,
                                     const Rect& right) noexcept {
        return left.x == right.x && left.y == right.y &&
               left.width == right.width && left.height == right.height;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return width <= 0.0 || height <= 0.0;
    }
    [[nodiscard]] bool finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y) &&
               std::isfinite(width) && std::isfinite(height);
    }
    [[nodiscard]] constexpr double left() const noexcept { return x; }
    [[nodiscard]] constexpr double top() const noexcept { return y; }
    [[nodiscard]] constexpr double right() const noexcept { return x + width; }
    [[nodiscard]] constexpr double bottom() const noexcept { return y + height; }
    [[nodiscard]] constexpr double area() const noexcept {
        return empty() ? 0.0 : width * height;
    }
    [[nodiscard]] constexpr bool contains(Point point) const noexcept {
        return !empty() && point.x >= x && point.y >= y &&
               point.x < x + width && point.y < y + height;
    }
    [[nodiscard]] constexpr bool contains(Rect rect) const noexcept {
        return !empty() && !rect.empty() && rect.x >= x && rect.y >= y &&
               rect.x + rect.width <= x + width &&
               rect.y + rect.height <= y + height;
    }
    [[nodiscard]] static Rect intersection(Rect left, Rect right) noexcept;
    [[nodiscard]] static Rect united(Rect left, Rect right) noexcept;
};

struct GUI_FORMS_API Insets {
    double left{};
    double top{};
    double right{};
    double bottom{};
    friend constexpr bool operator==(const Insets& left,
                                     const Insets& right) noexcept {
        return left.left == right.left && left.top == right.top &&
               left.right == right.right && left.bottom == right.bottom;
    }
};

} // namespace gui_forms
