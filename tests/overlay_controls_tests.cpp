#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/anchored_overlay_layer.hpp"
#include "gui_forms/controls/anchored_overlay_placement.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "support/named_callbacks.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool close(double left, double right) {
    return std::abs(left - right) < 0.01;
}

void test_resolution_flips_and_clamps() {
    AnchoredOverlayPlacement placement;
    placement.preferred_size = {180.0, 90.0};
    placement.horizontal_alignment = OverlayHorizontalAlignment::far;
    placement.gap = 2.0;
    const AnchoredOverlayPlacementResult below = resolve_anchored_overlay(
        {170.0, 20.0, 20.0, 20.0}, {200.0, 160.0}, placement);
    require(below.bounds == Rect{10.0, 42.0, 180.0, 90.0} &&
                !below.placed_above && !below.width_clamped &&
                !below.height_clamped,
            "anchored placement must align its far edge and prefer below");

    const AnchoredOverlayPlacementResult above = resolve_anchored_overlay(
        {170.0, 130.0, 20.0, 20.0}, {200.0, 160.0}, placement);
    require(above.placed_above && close(above.bounds.y, 38.0),
            "anchored placement must flip above when below cannot fit");

    placement.preferred_size = {500.0, 400.0};
    const AnchoredOverlayPlacementResult bounded = resolve_anchored_overlay(
        {40.0, 40.0, 10.0, 10.0}, {120.0, 80.0}, placement);
    require(bounded.bounds == Rect{4.0, 4.0, 112.0, 72.0} &&
                bounded.width_clamped && bounded.height_clamped,
            "oversize overlay content must remain inside the client margin");
}

void test_layer_dismissal_focus_and_resize() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("overlay.root"));
    std::shared_ptr<gui_forms::Button> owner = make_control<Button>(StableId("overlay.owner"), "Open");
    (*owner).set_requested_bounds({160.0, 110.0, 30.0, 24.0});
    (*root).add_child(owner);
    Window window(root, {200.0, 150.0});
    window.perform_layout();
    require(window.request_focus(owner), "overlay owner must accept initial focus");

    std::shared_ptr<gui_forms::Panel> content = make_control<Panel>(StableId("overlay.content"));
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("overlay.field"), "value");
    (*field).set_requested_bounds({5.0, 5.0, 120.0, 24.0});
    (*content).add_child(field);
    AnchoredOverlayPlacement placement;
    placement.preferred_size = {140.0, 80.0};
    placement.horizontal_alignment = OverlayHorizontalAlignment::far;
    std::shared_ptr<gui_forms::AnchoredOverlayLayer> layer = make_control<AnchoredOverlayLayer>(
        StableId("overlay.layer"), owner, placement);
    (*layer).set_content(content);
    (*layer).accessible_name = "Anchored test overlay";
    (*layer).set_requested_bounds({0.0, 0.0, 200.0, 150.0});
    std::vector<OverlayDismissReason> dismissals;
    SubscriptionToken dismissal = (*layer).dismiss_requested().subscribe(
        test_support::PushBack<std::vector<OverlayDismissReason>,
                               OverlayDismissReason>(dismissals));
    OverlayToken token = window.open_overlay(owner, layer);
    const FocusScopeId scope = window.begin_focus_scope(layer, field);
    window.perform_layout();
    require(window.focused_control() == field &&
                (*layer).resolved_placement().placed_above &&
                (*content).absolute_bounds() == Rect{50.0, 30.0, 140.0, 80.0},
            "overlay layer must contain focus and resolve against the live owner");

    require(window.dispatch_pointer({PointerAction::down,
                                     PointerButton::primary,
                                     {175.0, 122.0}}) &&
                dismissals.empty(),
            "pointer down inside the exact overlay anchor must pass through rather than be misclassified as click-away");
    static_cast<void>(window.dispatch_pointer({PointerAction::up,
                                               PointerButton::primary,
                                               {175.0, 122.0}}));

    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     {10.0, 120.0}}) &&
                dismissals == std::vector<OverlayDismissReason>{
                    OverlayDismissReason::click_away},
            "click-away must be reported by the full-client overlay layer");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                dismissals.size() == 2U &&
                dismissals.back() == OverlayDismissReason::escape_key,
            "Escape must be reported before focused child routing");

    window.resize({320.0, 220.0});
    window.perform_layout();
    require((*layer).committed_arranged_bounds() == Rect{0.0, 0.0, 320.0, 220.0} &&
                (*content).absolute_bounds() == Rect{50.0, 134.0, 140.0, 80.0},
            "open anchored overlay must reflow and flip back below on host resize");
    require(window.end_focus_scope(scope) && window.focused_control() == owner,
            "closing the overlay scope must restore its owner focus");
    token.disconnect();
}

} // namespace

int main() {
    test_resolution_flips_and_clamps();
    test_layer_dismissal_focus_and_resize();
    return 0;
}
