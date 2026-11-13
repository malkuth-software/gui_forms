#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/object/drawing_object.hpp"
#include "gui_forms/drawing/types/drawing_types.hpp"

namespace gui_drawing {

class GUI_FORMS_API Brush : public DrawingObject {
public:
    [[nodiscard]] virtual BrushSnapshot snapshot() const = 0;
};

class GUI_FORMS_API SolidBrush final : public Brush {
public:
    explicit SolidBrush(Color color);
    [[nodiscard]] Color color() const;
    [[nodiscard]] BrushSnapshot snapshot() const override;

private:
    Color color_;
};

} // namespace gui_drawing
