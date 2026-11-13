#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/brush/brush.hpp"

namespace gui_drawing {

class GUI_FORMS_API HatchBrush final : public Brush {
public:
    HatchBrush(HatchStyle style, Color foreground,
               Color background = Color::from_argb(0U, 0U, 0U, 0U));
    [[nodiscard]] BrushSnapshot snapshot() const override;

private:
    HatchStyle style_;
    Color foreground_;
    Color background_;
};

} // namespace gui_drawing
