#include "ribbon_tool_tip_layer.hpp"

namespace gui_forms {

RibbonToolTipLayer::RibbonToolTipLayer(StableId stable_id)
    : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    focusable = false;
    // A tooltip's ink and plate are the tool_tip role's: an authored
    // ForeColor or BackColor on the window or its panels does not reach it.
    stop_inherited_colours();
}

bool RibbonToolTipLayer::hit_test_local(Point) const {
    return false;
}

} // namespace gui_forms
