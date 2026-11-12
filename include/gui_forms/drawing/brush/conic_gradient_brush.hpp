#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/brush/brush.hpp"

namespace gui_drawing {

// A conic (sweep) gradient (piece C9, a hue wheel): the colour at a point is
// the blend's colour at its angle about `center`, measured clockwise (y
// down) from `start_angle` degrees (0 along +x), one full turn from position
// 0 to position 1. The blend holds 2 through 4096 colours at ascending
// positions from 0 to 1, as LinearGradientBrush::set_interpolation_colors
// requires; the default is `first` to `second`.
class GUI_FORMS_API ConicGradientBrush final : public Brush {
public:
    ConicGradientBrush(PointF center, double start_angle, Color first, Color second);
    void set_interpolation_colors(const ColorBlend& blend);
    [[nodiscard]] BrushSnapshot snapshot() const override;

private:
    PointF center_;
    double start_angle_{};
    Color first_;
    Color second_;
    std::vector<Color> colors_;
    std::vector<double> positions_;
};

} // namespace gui_drawing
