#include "ribbon_group_popup.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <utility>

namespace gui_forms {

RibbonGroupPopup::RibbonGroupPopup(StableId stable_id, Ribbon& ribbon) : Control(std::move(stable_id)), ribbon_(&ribbon) {
    set_focusable(true);
    set_paint_plane(PaintPlane::control);
}

bool RibbonGroupPopup::ribbon_alive() const noexcept {
    const bool result = ribbon_ != nullptr && (*ribbon_).is_alive();
    return result;
}

void RibbonGroupPopup::set_anchor_x(double x) {
    require_mutable();
    if (anchor_x_ == x) return;
    anchor_x_ = x;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test);
}

Size RibbonGroupPopup::measure(Size available) {
    return available;
}

// The panel at the group button's left, moved left only as far as the
// content's bounds require; the group's hosted controls at their places in
// the group's largest layout.
void RibbonGroupPopup::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    panel_ = {};
    if (!ribbon_alive()) return;
    const Ribbon& ribbon = *ribbon_;
    const Size size = ribbon.popup_panel_size();
    const double room = std::max(0.0, final_bounds.width - size.width);
    const double x = std::clamp(anchor_x_ - final_bounds.x, 0.0, room);
    panel_ = {x, 0.0, size.width, std::min(size.height, final_bounds.height)};
    if (!ribbon.selected_tab_ || !ribbon.popup_group_) return;
    const std::size_t tab = *ribbon.selected_tab_;
    const std::size_t group = *ribbon.popup_group_;
    const Ribbon::Layout placed = ribbon.popup_layout(panel_);
    const std::vector<Ribbon::CommandRecord>& records = ribbon.records_[tab][group];
    for (std::size_t command = 0; command < records.size(); ++command) {
        const Control::Ptr& control = records[command].hosted;
        if (!control || !is_current_layout_child(control)) continue;
        const Rect bounds = Rect::intersection(placed.commands[group][command], panel_);
        set_child_layout_collapsed(control, bounds.empty());
        if (!bounds.empty()) set_child_layout(control, bounds);
    }
}

void RibbonGroupPopup::on_paint(Painter& painter, Rect) {
    if (!ribbon_alive() || panel_.empty()) return;
    PartPaint part;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::shadow;
    part.bounds = panel_;
    paint_part(painter, *this, part);
    part.role = PartRole::face;
    paint_part(painter, *this, part);
    (*ribbon_).popup_paint(painter, panel_);
}

void RibbonGroupPopup::on_pointer(PointerEvent& event) {
    if (!ribbon_alive()) return;
    // The popup may close (and detach) while the ribbon handles the event.
    const Control::Ptr lifetime = shared_from_this();
    (*ribbon_).popup_pointer(event, panel_);
}

void RibbonGroupPopup::on_key(KeyEvent& event) {
    if (!ribbon_alive()) return;
    const Control::Ptr lifetime = shared_from_this();
    (*ribbon_).popup_key(event);
}

void RibbonGroupPopup::on_focus_changed(bool focused) {
    if (ribbon_alive()) (*ribbon_).popup_focus_changed(focused);
}

bool RibbonGroupPopup::hit_test_local(Point local_point) const {
    return panel_.contains(local_point);
}

Insets RibbonGroupPopup::visual_outsets() const noexcept {
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

SemanticDescriptor RibbonGroupPopup::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> RibbonGroupPopup::semantic_virtual_children() const {
    if (!ribbon_alive()) return {};
    std::vector<SemanticNode> nodes = (*ribbon_).popup_semantic_children(panel_);
    return nodes;
}

bool RibbonGroupPopup::on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                                std::string_view) {
    if (!ribbon_alive()) return false;
    const Control::Ptr lifetime = shared_from_this();
    const bool handled = (*ribbon_).popup_semantic_action(stable_id, action);
    return handled;
}

} // namespace gui_forms
