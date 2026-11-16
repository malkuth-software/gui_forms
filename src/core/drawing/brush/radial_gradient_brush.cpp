#include "../support/drawing_support.hpp"

RadialGradientBrush::RadialGradientBrush(PointF center, double radius, Color center_color, Color edge_color,
                                         WrapMode wrap_mode)
    : center_(center), radius_(radius), radius_y_(radius), focus_(center), center_color_(center_color),
      edge_color_(edge_color),
      wrap_mode_(wrap_mode) {
    require_finite(center, "radial gradient center");
    require_finite(radius, "radial gradient radius");
    if (radius <= 0.0 || radius > 1'000'000.0) {
        throw std::invalid_argument("radial gradient radius must be positive and bounded");
    }
    require_enum(wrap_mode, 4U, "radial gradient wrap mode");
}

void RadialGradientBrush::set_interpolation_colors(const ColorBlend& blend) {
    require_alive();
    require_blend_positions(blend.positions, blend.colors.size());
    colors_ = blend.colors;
    positions_ = blend.positions;
}

namespace {

// Whether `focus` lies strictly inside the ellipse of radii (radius_x,
// radius_y) about `center`.
bool focus_within(PointF center, double radius_x, double radius_y, PointF focus) noexcept {
    const double across = (focus.x - center.x) / radius_x;
    const double down = (focus.y - center.y) / radius_y;
    const double reach = across * across + down * down;
    return reach < 1.0;
}

} // namespace

void RadialGradientBrush::set_focus(PointF focus) {
    require_alive();
    require_finite(focus, "radial gradient focus");
    if (!focus_within(center_, radius_, radius_y_, focus)) {
        throw std::invalid_argument("radial gradient focus must lie strictly inside its circle");
    }
    focus_ = focus;
}

void RadialGradientBrush::set_radii(double radius_x, double radius_y) {
    require_alive();
    require_finite(radius_x, "radial gradient horizontal radius");
    require_finite(radius_y, "radial gradient vertical radius");
    if (radius_x <= 0.0 || radius_x > 1'000'000.0 || radius_y <= 0.0 || radius_y > 1'000'000.0) {
        throw std::invalid_argument("radial gradient radii must be positive and bounded");
    }
    if (!focus_within(center_, radius_x, radius_y, focus_)) {
        throw std::invalid_argument("radial gradient focus must stay strictly inside its ellipse");
    }
    radius_ = radius_x;
    radius_y_ = radius_y;
}

void RadialGradientBrush::set_wrap_mode(WrapMode mode) {
    require_alive();
    require_enum(mode, 4U, "radial gradient wrap mode");
    wrap_mode_ = mode;
}

BrushSnapshot RadialGradientBrush::snapshot() const {
    require_alive();
    BrushSnapshot result;
    result.kind = BrushKind::radial_gradient;
    result.primary = center_color_;
    result.secondary = edge_color_;
    result.wrap_mode = wrap_mode_;
    result.center = center_;
    result.radius = radius_;
    result.radius_y = radius_y_;
    result.focus = focus_;
    result.colors = colors_.empty() ? std::vector<Color>{center_color_, edge_color_} : colors_;
    result.positions = positions_.empty() ? std::vector<double>{0.0, 1.0} : positions_;
    return result;
}


} // namespace gui_drawing
