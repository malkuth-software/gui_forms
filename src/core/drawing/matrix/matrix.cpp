#include "../support/drawing_support.hpp"

Matrix Matrix::translation(double x, double y) {
    require_finite(x, "translation x");
    require_finite(y, "translation y");
    return {1.0, 0.0, 0.0, 1.0, x, y};
}

Matrix Matrix::rotation_at(double degrees, PointF center) {
    require_finite(degrees, "rotation degrees");
    require_finite(center, "rotation center");
    const double radians = degrees * (std::acos(-1.0) / 180.0);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const Matrix rotation{cosine, sine, -sine, cosine, 0.0, 0.0};
    return Matrix::translation(-center.x, -center.y)
        .followed_by(rotation)
        .followed_by(Matrix::translation(center.x, center.y));
}

bool Matrix::finite() const noexcept {
    return std::isfinite(m11_) && std::isfinite(m12_) &&
           std::isfinite(m21_) && std::isfinite(m22_) &&
           std::isfinite(dx_) && std::isfinite(dy_);
}

PointF Matrix::transform(PointF point) const {
    if (!finite()) throw std::invalid_argument("matrix is not finite");
    require_finite(point, "transformed point");
    const PointF result{point.x * m11_ + point.y * m21_ + dx_,
                        point.x * m12_ + point.y * m22_ + dy_};
    require_finite(result, "matrix result");
    return result;
}

RectF Matrix::transform_bounds(RectF rect) const {
    require_finite(rect, "transformed rectangle");
    const PointF points[] = {
        transform({rect.left(), rect.top()}), transform({rect.right(), rect.top()}),
        transform({rect.right(), rect.bottom()}), transform({rect.left(), rect.bottom()}),
    };
    double left = points[0].x;
    double top = points[0].y;
    double right = points[0].x;
    double bottom = points[0].y;
    for (const PointF point : points) {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    return {left, top, right - left, bottom - top};
}

Matrix Matrix::followed_by(const Matrix& next) const {
    if (!finite() || !next.finite()) {
        throw std::invalid_argument("matrix composition requires finite matrices");
    }
    const Matrix result{
        m11_ * next.m11_ + m12_ * next.m21_,
        m11_ * next.m12_ + m12_ * next.m22_,
        m21_ * next.m11_ + m22_ * next.m21_,
        m21_ * next.m12_ + m22_ * next.m22_,
        dx_ * next.m11_ + dy_ * next.m21_ + next.dx_,
        dx_ * next.m12_ + dy_ * next.m22_ + next.dy_,
    };
    if (!result.finite()) throw std::overflow_error("matrix composition overflowed");
    return result;
}


} // namespace gui_drawing

