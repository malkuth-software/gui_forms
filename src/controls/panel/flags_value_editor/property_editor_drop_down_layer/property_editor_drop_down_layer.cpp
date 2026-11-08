#include "property_editor_drop_down_layer.hpp"

#include <utility>

namespace gui_forms::detail {

PropertyEditorDropDownLayer::PropertyEditorDropDownLayer(StableId stable_id)
    : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    border_style = BorderStyle::none;
    set_background(Color::rgba(0, 0, 0, 0));
}

void PropertyEditorDropDownLayer::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        dismissed_.emit();
        event.handled = true;
    }
}

void PropertyEditorDropDownLayer::on_key_preview(KeyEvent& event) {
    if (event.action == KeyAction::down &&
        event.physical_key == PhysicalKey::escape) {
        dismissed_.emit();
        event.handled = true;
    }
}

} // namespace gui_forms::detail
