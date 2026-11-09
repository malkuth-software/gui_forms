#include "../support/drawing_support.hpp"

ConicGradientBrush::ConicGradientBrush(PointF center, double start_angle, Color first, Color second)
    : center_(center), start_angle_(start_angle), first_(first), second_(second) {
    require_finite(center, "conic gradient center");
    require_finite(start_angle, "conic gradient start angle");
    if (std::abs(start_angle) > 1'000'000.0) {
        throw std::invalid_argument("conic gradient start angle must be bounded");
    }
}

void ConicGradientBrush::set_interpolation_colors(const ColorBlend& blend) {
    require_alive();
    require_blend_positions(blend.positions, blend.colors.size());
    colors_ = blend.colors;
    positions_ = blend.positions;
}

BrushSnapshot ConicGradientBrush::snapshot() const {
    require_alive();
    BrushSnapshot result;
    result.kind = BrushKind::conic_gradient;
    result.primary = first_;
    result.secondary = second_;
    result.wrap_mode = WrapMode::clamp;
    result.center = center_;
    result.angle = start_angle_;
    result.colors = colors_.empty() ? std::vector<Color>{first_, second_} : colors_;
    result.positions = positions_.empty() ? std::vector<double>{0.0, 1.0} : positions_;
    return result;
}


} // namespace gui_drawing
