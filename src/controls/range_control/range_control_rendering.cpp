#include "range_control_rendering.hpp"


#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace gui_forms::range_control_detail {

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void paint_sunken(Painter& painter, Rect bounds,
                  const BasicControlStyle& style, Color fill) {
    painter.fill_rect(bounds, fill);
    painter.draw_line({bounds.x, bounds.y},
                      {bounds.x + bounds.width, bounds.y}, style.dark_border, 1.0);
    painter.draw_line({bounds.x, bounds.y},
                      {bounds.x, bounds.y + bounds.height}, style.dark_border, 1.0);
    painter.draw_line({bounds.x, bounds.y + bounds.height - 1.0},
                      {bounds.x + bounds.width, bounds.y + bounds.height - 1.0},
                      style.highlight, 1.0);
    painter.draw_line({bounds.x + bounds.width - 1.0, bounds.y},
                      {bounds.x + bounds.width - 1.0, bounds.y + bounds.height},
                      style.highlight, 1.0);
}

void paint_thumb(Painter& painter, Rect bounds,
                 const BasicControlStyle& style, bool focused) {
    painter.fill_rect(bounds, style.face);
    painter.fill_rect({bounds.x + 1.0, bounds.y + 1.0,
                       std::max(0.0, bounds.width - 2.0),
                       std::max(0.0, (bounds.height - 2.0) * 0.42)},
                      style.face_light);
    painter.draw_line({bounds.x, bounds.y},
                      {bounds.x + bounds.width - 1.0, bounds.y},
                      style.highlight, 1.0);
    painter.draw_line({bounds.x, bounds.y},
                      {bounds.x, bounds.y + bounds.height - 1.0},
                      style.highlight, 1.0);
    painter.draw_line({bounds.x, bounds.y + bounds.height - 1.0},
                      {bounds.x + bounds.width - 1.0,
                       bounds.y + bounds.height - 1.0}, style.dark_border, 1.0);
    painter.draw_line({bounds.x + bounds.width - 1.0, bounds.y},
                      {bounds.x + bounds.width - 1.0,
                       bounds.y + bounds.height - 1.0}, style.dark_border, 1.0);
    if (focused && bounds.width > 4.0 && bounds.height > 4.0) {
        painter.stroke_rect({bounds.x + 2.0, bounds.y + 2.0,
                             bounds.width - 4.0, bounds.height - 4.0},
                            style.accent, 1.0);
    }
}

} // namespace gui_forms::range_control_detail
