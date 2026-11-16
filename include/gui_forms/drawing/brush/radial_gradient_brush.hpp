#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/brush/brush.hpp"

#include <vector>

namespace gui_drawing {

// A radial gradient (owner, 2026-11-16: added to the drawing table before
// the seal). The colour at a point is the blend's colour at the offset t
// whose circle passes through it: the circle of radius t * radius centred
// at focus + t * (center - focus). Position 0 is the focus, position 1 the
// circle of `radius` about `center`; with the focus at the centre (the
// default) t is the distance from the centre over the radius. Past
// position 1 the wrap mode repeats the blend (tile), reflects it (the three
// flips) or holds its last colour (clamp, the default). The blend holds 2
// through 4096 colours at ascending positions from 0 to 1, as
// LinearGradientBrush::set_interpolation_colors requires; the default is
// `center_color` to `edge_color`. Colours interpolate premultiplied, in
// sRGB-encoded values, as every gradient does (the blending rule).
class GUI_FORMS_API RadialGradientBrush final : public Brush {
public:
    // Throws std::invalid_argument for a centre that is not finite, a
    // radius that is not positive and at most 1,000,000, or a wrap mode
    // outside the enumeration.
    RadialGradientBrush(PointF center, double radius, Color center_color, Color edge_color,
                        WrapMode wrap_mode = WrapMode::clamp);
    void set_interpolation_colors(const ColorBlend& blend);
    // Moves position 0 to `focus`, which must lie strictly inside the
    // circle (std::invalid_argument otherwise; the brush is unchanged).
    void set_focus(PointF focus);
    void set_wrap_mode(WrapMode mode);
    [[nodiscard]] BrushSnapshot snapshot() const override;

private:
    PointF center_;
    double radius_{};
    PointF focus_;
    Color center_color_;
    Color edge_color_;
    WrapMode wrap_mode_{WrapMode::clamp};
    std::vector<Color> colors_;
    std::vector<double> positions_;
};

} // namespace gui_drawing
