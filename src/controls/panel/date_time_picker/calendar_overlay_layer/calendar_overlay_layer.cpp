#include "calendar_overlay_layer.hpp"

#include <utility>

namespace gui_forms {

CalendarOverlayLayer::CalendarOverlayLayer(StableId stable_id)
    : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    border_style = BorderStyle::none;
    set_background(Color::rgba(0, 0, 0, 0));
}

void CalendarOverlayLayer::on_mouse_down(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary) {
        dismissed_.emit();
        arguments.handled = true;
    }
    Control::on_mouse_down(arguments);
}

} // namespace gui_forms
