#include "ribbon_tool_tip_layer.hpp"

namespace gui_forms {

RibbonToolTipLayer::RibbonToolTipLayer(StableId stable_id)
    : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    focusable = false;
}

bool RibbonToolTipLayer::hit_test_local(Point) const {
    return false;
}

} // namespace gui_forms
