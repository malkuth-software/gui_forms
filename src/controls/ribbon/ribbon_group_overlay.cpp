#include "ribbon_group_overlay.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <utility>

namespace gui_forms {

RibbonGroupOverlay::RibbonGroupOverlay(StableId stable_id, Ribbon& ribbon) : Control(std::move(stable_id)), ribbon_(&ribbon) {
    set_focusable(true);
    set_paint_plane(PaintPlane::control);
}

bool RibbonGroupOverlay::ribbon_alive() const noexcept {
    const bool result = ribbon_ != nullptr && (*ribbon_).is_alive();
    return result;
}

void RibbonGroupOverlay::set_anchor_x(double x) {
    require_mutable();
    if (anchor_x_ == x) return;
    anchor_x_ = x;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test);
}

Size RibbonGroupOverlay::measure(Size available) {
    return available;
}

// The panel at the group button's left, moved left only as far as the
// content's bounds require; the group's hosted controls at their places in
// the group's largest layout.
void RibbonGroupOverlay::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    panel_ = {};
    if (!ribbon_alive()) return;
    const Ribbon& ribbon = *ribbon_;
    const Size size = ribbon.overlay_panel_size();
    const double room = std::max(0.0, final_bounds.width - size.width);
    const double x = std::clamp(anchor_x_ - final_bounds.x, 0.0, room);
    panel_ = {x, 0.0, size.width, std::min(size.height, final_bounds.height)};
    if (!ribbon.selected_tab_ || !ribbon.overlay_group_) return;
    const std::size_t tab = *ribbon.selected_tab_;
    const std::size_t group = *ribbon.overlay_group_;
    const Ribbon::Layout placed = ribbon.overlay_layout(panel_);
    const std::vector<Ribbon::CommandRecord>& records = ribbon.records_[tab][group];
    for (std::size_t command = 0; command < records.size(); ++command) {
        const Control::Ptr& control = records[command].hosted;
        if (!control || !is_current_layout_child(control)) continue;
        const Rect bounds = Rect::intersection(placed.commands[group][command], panel_);
        set_child_layout_collapsed(control, bounds.empty());
        if (!bounds.empty()) set_child_layout(control, bounds);
    }
}

void RibbonGroupOverlay::on_paint(Painter& painter, Rect) {
    if (!ribbon_alive() || panel_.empty()) return;
    PartPaint part;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::shadow;
    part.bounds = panel_;
    paint_part(painter, *this, part);
    part.role = PartRole::face;
    paint_part(painter, *this, part);
    (*ribbon_).overlay_paint(painter, panel_);
}

void RibbonGroupOverlay::on_pointer(PointerEvent& event) {
    if (!ribbon_alive()) return;
    // The overlay may close (and detach) while the ribbon handles the event.
    const Control::Ptr lifetime = shared_from_this();
    (*ribbon_).overlay_pointer(event, panel_);
}

void RibbonGroupOverlay::on_key(KeyEvent& event) {
    if (!ribbon_alive()) return;
    const Control::Ptr lifetime = shared_from_this();
    (*ribbon_).overlay_key(event);
}

void RibbonGroupOverlay::on_focus_changed(bool focused) {
    if (ribbon_alive()) (*ribbon_).overlay_focus_changed(focused);
}

bool RibbonGroupOverlay::hit_test_local(Point local_point) const {
    return panel_.contains(local_point);
}

Insets RibbonGroupOverlay::visual_outsets() const noexcept {
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

SemanticDescriptor RibbonGroupOverlay::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> RibbonGroupOverlay::semantic_virtual_children() const {
    if (!ribbon_alive()) return {};
    std::vector<SemanticNode> nodes = (*ribbon_).overlay_semantic_children(panel_);
    return nodes;
}

bool RibbonGroupOverlay::on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                                std::string_view) {
    if (!ribbon_alive()) return false;
    const Control::Ptr lifetime = shared_from_this();
    const bool handled = (*ribbon_).overlay_semantic_action(stable_id, action);
    return handled;
}

} // namespace gui_forms
