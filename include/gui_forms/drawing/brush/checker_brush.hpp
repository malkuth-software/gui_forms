#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/brush/brush.hpp"

namespace gui_drawing {

// An alpha checkerboard brush (piece C7): square cells `cell_size` units
// wide in the recorder's coordinates, alternating `first` and `second`, with
// the corner of a `first` cell at the coordinate origin, so the pattern
// stays put as the filled shape moves. `cell_size` is finite and positive.
class GUI_FORMS_API CheckerBrush final : public Brush {
public:
    CheckerBrush(double cell_size, Color first, Color second);
    [[nodiscard]] BrushSnapshot snapshot() const override;

private:
    double cell_size_{};
    Color first_;
    Color second_;
};

} // namespace gui_drawing
