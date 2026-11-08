#include "calendar_overlay_layer.hpp"

#include <utility>

namespace gui_forms {

CalendarOverlayLayer::CalendarOverlayLayer(StableId stable_id)
    : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    border_style = BorderStyle::none;
    set_background(Color::rgba(0, 0, 0, 0));
}

void CalendarOverlayLayer::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        dismissed_.emit();
        event.handled = true;
    }
}

} // namespace gui_forms
