#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/brush/brush.hpp"

namespace gui_drawing {

class GUI_FORMS_API LinearGradientBrush final : public Brush {
public:
    LinearGradientBrush(RectF bounds, Color first, Color second,
                        double angle = 0.0,
                        WrapMode wrap_mode = WrapMode::tile);
    void set_blend(std::span<const double> factors,
                   std::span<const double> positions);
    void set_interpolation_colors(const ColorBlend& blend);
    void set_wrap_mode(WrapMode mode);
    [[nodiscard]] BrushSnapshot snapshot() const override;

private:
    RectF bounds_;
    Color first_;
    Color second_;
    double angle_{};
    WrapMode wrap_mode_{WrapMode::tile};
    std::vector<Color> colors_;
    std::vector<double> positions_;
};

} // namespace gui_drawing
