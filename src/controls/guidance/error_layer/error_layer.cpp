#include "error_layer.hpp"

namespace gui_forms {

ErrorLayer::ErrorLayer(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    focusable = false;
}

bool ErrorLayer::hit_test_local(Point) const {
    return false;
}

} // namespace gui_forms
