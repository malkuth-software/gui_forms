#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/geometry/drawing_geometry.hpp"

namespace gui_drawing {

class GUI_FORMS_API Matrix final {
public:
    constexpr Matrix() noexcept = default;
    constexpr Matrix(double m11, double m12, double m21, double m22,
                     double dx, double dy) noexcept
        : m11_(m11), m12_(m12), m21_(m21), m22_(m22), dx_(dx), dy_(dy) {}

    [[nodiscard]] static Matrix translation(double x, double y);
    [[nodiscard]] static Matrix rotation_at(double degrees, PointF center);
    [[nodiscard]] bool finite() const noexcept;
    [[nodiscard]] PointF transform(PointF point) const;
    [[nodiscard]] RectF transform_bounds(RectF rect) const;
    [[nodiscard]] Matrix followed_by(const Matrix& next) const;

    [[nodiscard]] constexpr double m11() const noexcept { return m11_; }
    [[nodiscard]] constexpr double m12() const noexcept { return m12_; }
    [[nodiscard]] constexpr double m21() const noexcept { return m21_; }
    [[nodiscard]] constexpr double m22() const noexcept { return m22_; }
    [[nodiscard]] constexpr double dx() const noexcept { return dx_; }
    [[nodiscard]] constexpr double dy() const noexcept { return dy_; }
    friend constexpr bool operator==(const Matrix& left,
                                     const Matrix& right) noexcept {
        return left.m11_ == right.m11_ && left.m12_ == right.m12_ &&
               left.m21_ == right.m21_ && left.m22_ == right.m22_ &&
               left.dx_ == right.dx_ && left.dy_ == right.dy_;
    }

private:
    double m11_{1.0};
    double m12_{};
    double m21_{};
    double m22_{1.0};
    double dx_{};
    double dy_{};
};

} // namespace gui_drawing
