#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_layer.hpp"

#include "anchored_overlay_placement_utilities.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

using anchored_overlay_detail::validate_placement;
AnchoredOverlayLayer::AnchoredOverlayLayer(
    StableId stable_id, Control::Ptr anchor, AnchoredOverlayPlacement placement)
    : Panel(std::move(stable_id)), anchor_(anchor), placement_(placement) {
    validate_placement(placement_);
    validate_anchor(anchor);
    set_paint_plane(PaintPlane::overlay);
    border_style = BorderStyle::none;
    set_background(Color::rgba(0, 0, 0, 0));
}

void AnchoredOverlayLayer::validate_anchor(const Control::Ptr& anchor) const {
    if (!anchor || !(*anchor).is_alive()) {
        throw std::invalid_argument("anchored overlay requires a live owner control");
    }
}

void AnchoredOverlayLayer::set_anchor(Control::Ptr anchor) {
    require_mutable();
    validate_anchor(anchor);
    if (anchor_.lock() == anchor) return;
    anchor_ = std::move(anchor);
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
}

void AnchoredOverlayLayer::set_content(Control::Ptr content) {
    require_mutable();
    if (content == content_) return;
    if (!content || !(*content).is_alive() || (*content).parent() ||
        (*content).attached()) {
        throw std::invalid_argument(
            "anchored overlay replacement content must be live and detached");
    }
    if (content_) {
        static_cast<void>(remove_child((*content_).runtime_id()));
    }
    content_ = std::move(content);
    add_child(content_);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void AnchoredOverlayLayer::clear_content() {
    require_mutable();
    if (!content_) return;
    const Control::Ptr released = std::move(content_);
    content_.reset();
    static_cast<void>(remove_child((*released).runtime_id()));
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void AnchoredOverlayLayer::set_placement(AnchoredOverlayPlacement placement) {
    require_mutable();
    validate_placement(placement);
    if (placement_.preferred_size == placement.preferred_size &&
        placement_.horizontal_alignment == placement.horizontal_alignment &&
        placement_.vertical_preference == placement.vertical_preference &&
        placement_.gap == placement.gap &&
        placement_.viewport_margin == placement.viewport_margin &&
        placement_.allow_vertical_flip == placement.allow_vertical_flip) {
        return;
    }
    placement_ = placement;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void AnchoredOverlayLayer::set_dismiss_on_click_away(bool enabled) {
    require_mutable();
    dismiss_on_click_away_ = enabled;
}

void AnchoredOverlayLayer::set_dismiss_on_escape(bool enabled) {
    require_mutable();
    dismiss_on_escape_ = enabled;
}

Size AnchoredOverlayLayer::measure(Size available) {
    if (content_) static_cast<void>((*content_).measure(placement_.preferred_size));
    return available;
}

void AnchoredOverlayLayer::arrange(Rect final_bounds) {
    Rect layer_bounds = final_bounds;
    if (Window* owner = window()) {
        const Size client = (*owner).client_size();
        layer_bounds = {0.0, 0.0, client.width, client.height};
    }
    arrange_self(layer_bounds);
    const Control::Ptr anchor = anchor_.lock();
    if (!content_ || !anchor || !(*anchor).attached() ||
        !(*anchor).effectively_visible()) {
        if (content_) set_child_layout(content_, {});
        resolved_ = {};
        return;
    }
    resolved_ = resolve_anchored_overlay(
        (*anchor).absolute_bounds(), {layer_bounds.width, layer_bounds.height},
        placement_);
    set_child_layout(content_, resolved_.bounds);
}

bool AnchoredOverlayLayer::hit_test_local(Point local_point) const {
    const Control::Ptr anchor = anchor_.lock();
    const Rect bounds = absolute_bounds();
    const Point absolute{bounds.x + local_point.x, bounds.y + local_point.y};
    if (anchor && (*anchor).effectively_visible() &&
        (*anchor).absolute_bounds().contains(absolute)) {
        return false;
    }
    return Panel::hit_test_local(local_point);
}

void AnchoredOverlayLayer::on_pointer(PointerEvent& event) {
    if (dismiss_on_click_away_ && event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        dismiss_requested_.emit(OverlayDismissReason::click_away);
        event.handled = true;
    }
}

void AnchoredOverlayLayer::on_key_preview(KeyEvent& event) {
    if (dismiss_on_escape_ && event.action == KeyAction::down &&
        event.physical_key == PhysicalKey::escape) {
        dismiss_requested_.emit(OverlayDismissReason::escape_key);
        event.handled = true;
    }
}

SemanticDescriptor AnchoredOverlayLayer::semantic_descriptor() const {
    SemanticDescriptor descriptor = Panel::semantic_descriptor();
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty();
    return descriptor;
}



namespace {

constexpr ControlAnatomy anchored_overlay_layer_anatomy{
    .control_id = "anchored_overlay_layer",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& AnchoredOverlayLayer::anatomy() const noexcept {
    return anchored_overlay_layer_anatomy;
}

namespace {

// The events AnchoredOverlayLayer adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 1> anchored_overlay_layer_interactions{{
    {.name = "dismiss_requested", .payload = "OverlayDismissReason", .mutable_payload = false},
}};

} // namespace

void AnchoredOverlayLayer::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), anchored_overlay_layer_interactions.begin(), anchored_overlay_layer_interactions.end());
}

} // namespace gui_forms
