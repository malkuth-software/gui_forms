#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/brush/brush.hpp"

namespace gui_drawing {

class GUI_FORMS_API PathGradientBrush final : public Brush {
public:
    explicit PathGradientBrush(std::span<const PointF> points,
                               WrapMode wrap_mode = WrapMode::clamp);
    void set_center_color(Color color);
    void set_center_point(PointF point);
    void set_surround_colors(std::span<const Color> colors);
    void set_interpolation_colors(const ColorBlend& blend);
    [[nodiscard]] BrushSnapshot snapshot() const override;

private:
    std::vector<PointF> points_;
    PointF center_;
    Color center_color_{Color::from_rgb(255, 255, 255)};
    std::vector<Color> surround_colors_{Color::from_rgb(0, 0, 0)};
    WrapMode wrap_mode_{WrapMode::clamp};
    std::vector<Color> colors_;
    std::vector<double> positions_;
};

} // namespace gui_drawing
