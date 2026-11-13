#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/range_control_values.hpp"
#include "gui_forms/controls/split_container.hpp"
#include "gui_forms/controls/split_container_values.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/drag_events/drag_events.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/rules/rule_set/rule_set.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "../src/core/drag_engine/drag_engine.hpp"
#include "../src/core/control/control/control_input_access.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) {
    return std::abs(left - right) < 0.001;
}

class GripRecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    // Every surface fill is one entry in its bounds, whatever the theme's
    // material fills it with: a flat, rounded or gradient fill.
    void fill_rect(Rect bounds, Color) override { fills.push_back(bounds); }
    void fill_rounded_rect(Rect bounds, double, Color) override { fills.push_back(bounds); }
    void fill_linear_gradient(Rect bounds, Point, Point, std::span<const GradientStop>) override {
        fills.push_back(bounds);
    }
    void fill_linear_gradient_spread(Rect bounds, Point, Point, std::span<const GradientStop>,
                                     GradientSpreadMode) override {
        fills.push_back(bounds);
    }
    void stroke_rect(Rect bounds, Color, double) override {
        strokes.push_back(bounds);
    }
    void draw_box_shadow(Rect bounds, double, Point, double, double,
                         Color) override {
        shadows.push_back(bounds);
    }
    void draw_line(Point, Point, Color, double) override { ++lines; }
    void draw_text_utf8(Point, std::string_view text, FontSpec,
                        Color) override {
        texts.emplace_back(text);
    }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<Rect> fills;
    std::vector<Rect> strokes;
    std::vector<Rect> shadows;
    std::vector<std::string> texts;
    std::size_t lines{};
};

// Whether `painter` filled a surface `width` by `height` units.
bool filled(const GripRecordingPainter& painter, double width, double height) {
    for (const Rect& fill : painter.fills) {
        if (near(fill.width, width) && near(fill.height, height)) return true;
    }
    return false;
}

// The left edge of the first surface `painter` filled `width` by `height`
// units, or NaN when there is none.
double filled_left(const GripRecordingPainter& painter, double width, double height) {
    for (const Rect& fill : painter.fills) {
        if (near(fill.width, width) && near(fill.height, height)) return fill.x;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

class CountSplitReasons final {
public:
    CountSplitReasons(std::size_t& pointer_changes,
                      std::size_t& keyboard_changes)
        : pointer_changes_(pointer_changes),
          keyboard_changes_(keyboard_changes) {}

    void operator()(const SplitChangeEvent& event) const {
        if (event.reason == SplitChangeReason::pointer) ++pointer_changes_;
        if (event.reason == SplitChangeReason::keyboard) ++keyboard_changes_;
    }

private:
    std::size_t& pointer_changes_;
    std::size_t& keyboard_changes_;
};

class SetSplitterOffThread final {
public:
    SetSplitterOffThread(SplitContainer& split, bool& rejected)
        : split_(split), rejected_(rejected) {}

    void operator()() const {
        try {
            split_.splitter_distance = 90.0;
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    SplitContainer& split_;
    bool& rejected_;
};

void test_stable_tree_and_constrained_geometry() {
    std::shared_ptr<gui_forms::SplitContainer> split = make_control<SplitContainer>(StableId("split.shell"));
    (*split).splitter_width = 3.0;
    (*split).splitter_hit_width = 9.0;
    (*split).first_minimum = 80.0;
    (*split).second_minimum = 120.0;
    (*split).splitter_distance = 140.0;
    Window window(split, {500.0, 240.0});
    window.perform_layout();

    require((*(*split).first_panel()).stable_id().value() == "split.shell.panel1" &&
                (*(*split).second_panel()).stable_id().value() == "split.shell.panel2" &&
                (*(*split).splitter_control()).stable_id().value() ==
                    "split.shell.splitter",
            "split children require durable owner-derived identities");
    require(near((*(*split).first_panel()).arranged_bounds().width, 140.0) &&
                near((*(*split).second_panel()).arranged_bounds().x, 143.0) &&
                near((*(*split).second_panel()).arranged_bounds().width, 357.0),
            "vertical split geometry must allocate two panels around the visible seam");
    require(near((*(*split).splitter_control()).arranged_bounds().width, 9.0),
            "splitter hit geometry must be wider than its three-pixel paint geometry");

    (*split).splitter_distance = 490.0;
    window.perform_layout();
    require(near((*split).splitter_distance.read(), 377.0),
            "second-panel minimum must clamp an excessive splitter distance");

    (*split).first_minimum = 400.0;
    (*split).second_minimum = 400.0;
    window.resize({203.0, 100.0});
    window.perform_layout();
    require(near((*split).splitter_distance.read(), 100.0) &&
                near((*(*split).second_panel()).arranged_bounds().width, 100.0),
            "impossible minimums must resolve by a deterministic proportional compromise");
}

void test_live_pointer_and_keyboard_resize() {
    std::shared_ptr<gui_forms::SplitContainer> split = make_control<SplitContainer>(StableId("split.input"));
    (*split).splitter_distance = 120.0;
    (*split).keyboard_increment = 4.0;
    Window window(split, {400.0, 180.0});
    window.perform_layout();

    std::size_t pointer_changes = 0;
    std::size_t keyboard_changes = 0;
    SubscriptionToken changed = (*split).splitter_changed().subscribe(
        CountSplitReasons(pointer_changes, keyboard_changes));

    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = {121.0, 70.0};
    down.pointer_id = 9;
    require(window.dispatch_pointer(down) &&
                window.captured_control() == (*split).splitter_control(),
            "splitter press must enter retained pointer capture");

    PointerEvent move = down;
    move.action = PointerAction::move;
    move.button = PointerButton::none;
    move.position = {181.0, 70.0};
    require(window.dispatch_pointer(move) && near((*split).splitter_distance.read(), 180.0),
            "splitter must update continuously during pointer movement");
    window.perform_layout();
    require(near((*(*split).first_panel()).arranged_bounds().width, 180.0) &&
                near((*(*split).second_panel()).arranged_bounds().x, 183.0) &&
                near((*(*split).second_panel()).arranged_bounds().width, 217.0),
            "pointer movement must commit both pane allocations before release");

    PointerEvent up = move;
    up.action = PointerAction::up;
    up.button = PointerButton::primary;
    require(window.dispatch_pointer(up) && !window.captured_control() &&
                pointer_changes >= 1,
            "splitter release must end capture without postponing the value update");

    require(window.focused_control() == (*split).splitter_control(),
            "pointer resizing must leave the physical splitter keyboard-focusable");
    KeyEvent right;
    right.action = KeyAction::down;
    right.physical_key = PhysicalKey::right;
    right.modifiers = Modifier::shift;
    require(window.dispatch_key(right) && near((*split).splitter_distance.read(), 220.0) &&
                keyboard_changes == 1,
            "Shift+Arrow must apply the declared larger keyboard increment");

    (*split).splitter_fixed = true;
    const double fixed = (*split).splitter_distance.read();
    require(!window.dispatch_key(right) && near((*split).splitter_distance.read(), fixed),
            "fixed splitters must reject keyboard mutation");
}

void test_pane_surface_background() {
    std::shared_ptr<gui_forms::SplitContainer> split = make_control<SplitContainer>(StableId("split.surface"));
    const Color first = Color::rgba(225, 235, 243);
    const Color second = Color::rgba(250, 250, 250);
    (*(*split).first_panel()).background = first;
    (*(*split).second_panel()).background = second;
    require((*(*split).first_panel()).background.read() == first &&
                (*(*split).second_panel()).background.read() == second,
            "splitter panels must own their allocation-filling surface color");
}

void test_collapse_focus_restore_and_fixed_panel_resize() {
    std::shared_ptr<gui_forms::SplitContainer> split = make_control<SplitContainer>(StableId("split.collapse"));
    (*split).splitter_distance = 110.0;
    std::shared_ptr<gui_forms::Button> field = make_control<Button>(StableId("split.collapse.field"), "Field");
    (*field).set_requested_bounds({8.0, 8.0, 80.0, 24.0});
    (*(*split).first_panel()).add_child(field);
    Window window(split, {400.0, 180.0});
    window.perform_layout();
    require(window.request_focus(field), "first panel fixture must accept focus");

    (*split).set_first_collapsed(true);
    window.perform_layout();
    require(!(*(*split).first_panel()).visible.read() &&
                window.focused_control() == (*split).splitter_control() &&
                near((*(*split).second_panel()).arranged_bounds().width, 397.0),
            "collapsing a pane must remove it from input and transfer focus to its seam");

    bool rejected_both = false;
    try {
        (*split).set_second_collapsed(true);
    } catch (const std::logic_error&) {
        rejected_both = true;
    }
    require(rejected_both, "a split composition may not collapse both panels");

    (*split).set_first_collapsed(false);
    window.perform_layout();
    require((*(*split).first_panel()).visible.read() && near((*split).splitter_distance.read(), 110.0),
            "restoring a panel must recover its remembered extent");

    (*split).fixed_panel = SplitFixedPanel::second;
    const double second_extent = (*(*split).second_panel()).arranged_bounds().width;
    window.resize({520.0, 180.0});
    window.perform_layout();
    require(near((*(*split).second_panel()).arranged_bounds().width, second_extent),
            "a fixed second panel must retain its extent when the container grows");
}

void test_horizontal_orientation_and_thread_guard() {
    std::shared_ptr<gui_forms::SplitContainer> split = make_control<SplitContainer>(StableId("split.horizontal"));
    (*split).orientation = Orientation::horizontal;
    (*split).splitter_distance = 70.0;
    Window window(split, {260.0, 200.0});
    window.perform_layout();
    require(near((*(*split).first_panel()).arranged_bounds().height, 70.0) &&
                near((*(*split).second_panel()).arranged_bounds().y, 73.0) &&
                (*(*split).splitter_control()).effective_cursor() ==
                    CursorKind::resize_vertical,
            "horizontal orientation must divide height and expose a vertical-resize cursor");

    bool rejected = false;
    std::thread worker(SetSplitterOffThread(*split, rejected));
    worker.join();
    require(rejected, "attached split mutation must preserve UI-thread enforcement");
}

void test_seam_tab_pointer_keyboard_and_semantic_collapse() {
    std::shared_ptr<gui_forms::SplitContainer> split = make_control<SplitContainer>(StableId("split.seam-tab"));
    (*split).splitter_distance = 270.0;
    (*split).collapse_panel = SplitFixedPanel::second;
    (*split).accessible_name = "Inspector split";
    Window window(split, {400.0, 180.0});
    window.perform_layout();
    const double remembered = (*(*split).second_panel()).arranged_bounds().width;

    const Rect seam = (*(*split).splitter_control()).absolute_bounds();
    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = {seam.x + seam.width * .5,
                     seam.y + seam.height * .5};
    require(window.dispatch_pointer(down) &&
                window.captured_control() == (*split).splitter_control(),
            "seam collapse tab must capture its compact activation target");
    PointerEvent up = down;
    up.action = PointerAction::up;
    require(window.dispatch_pointer(up) && (*split).second_collapsed(),
            "seam collapse tab activation must collapse its declared panel");
    window.perform_layout();
    require(!(*(*split).second_panel()).visible.read() &&
                window.perform_semantic_action("split.seam-tab",
                                               SemanticAction::expand),
            "collapsed seam must leave the pane out of task order and expose restore");
    window.perform_layout();
    require(!(*split).second_collapsed() &&
                near((*(*split).second_panel()).arranged_bounds().width, remembered),
            "semantic restore must recover the remembered pane extent");

    (*split).splitter_fixed = true;
    require(window.request_focus((*split).splitter_control()) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*split).second_collapsed(),
            "fixed splitter seam tab must remain keyboard-operable for collapse");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::space}) &&
                !(*split).second_collapsed(),
            "Space must restore a collapsed pane through the focused seam tab");
}

void test_seam_grip_rest_proximity_focus_press_and_orientation_paint() {
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("split.grip.visual"));
    (*split).splitter_width = 3.0;
    (*split).splitter_hit_width = 9.0;
    (*split).splitter_distance = 120.0;
    (*split).collapse_panel = SplitFixedPanel::second;
    Window window(split, {400.0, 180.0});
    window.perform_layout();
    (*split).splitter_transition_duration = 0ms;
    const Control::Ptr grip = (*split).splitter_control();
    const Rect seam = (*grip).absolute_bounds();
    const Insets outsets = (*grip).visual_outsets();
    require(near(seam.width, 9.0) && near(outsets.left, 10.5) &&
                near(outsets.right, 10.5) && near(outsets.top, 0.0) &&
                near(outsets.bottom, 0.0),
            "vertical seam must retain a nine-unit hit strip and declare the complete engaged shadow outsets");

    GripRecordingPainter rest;
    (*grip).on_paint(rest, {0.0, 0.0, seam.width, seam.height});
    require(filled(rest, 3.0, seam.height) && filled(rest, 7.0, 28.0) && !filled(rest, 12.0, 42.0),
            "rest seam must paint a three-unit rail and quiet seven-by-twenty-eight actuator");

    const Point center{seam.x + seam.width * 0.5,
                       seam.y + seam.height * 0.5};
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, center}));
    GripRecordingPainter hot;
    (*grip).on_paint(hot, {0.0, 0.0, seam.width, seam.height});
    require(filled(hot, 12.0, 42.0) && near(filled_left(hot, 12.0, 42.0), -1.5) &&
                !filled(hot, 7.0, 28.0),
            "pointer proximity must enlarge the actuator before activation");

    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, {20.0, 20.0}}));
    GripRecordingPainter left;
    (*grip).on_paint(left, {0.0, 0.0, seam.width, seam.height});
    require(filled(left, 7.0, 28.0) && !filled(left, 12.0, 42.0),
            "leaving the actuator proximity must restore its quiet rest geometry");

    require(window.request_focus(grip),
            "splitter grip must accept keyboard focus");
    static_cast<void>(window.dispatch_key(
        {KeyAction::down, PhysicalKey::a}));
    GripRecordingPainter focused;
    (*grip).on_paint(focused, {0.0, 0.0, seam.width, seam.height});
    require(filled(focused, 12.0, 42.0),
            "keyboard focus cue must expose the same enlarged mechanical grip");

    static_cast<void>(window.request_focus({}));
    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = center;
    require(window.dispatch_pointer(down),
            "collapse actuator press must route through the expanded activation box");
    GripRecordingPainter pressed;
    (*grip).on_paint(pressed, {0.0, 0.0, seam.width, seam.height});
    require(filled(pressed, 12.0, 42.0),
            "pressed collapse actuator must retain its pre-activation engaged treatment");
    PointerEvent up = down;
    up.action = PointerAction::up;
    require(window.dispatch_pointer(up) && (*split).second_collapsed(),
            "engaged grip release must preserve the existing pane-collapse action");

    std::shared_ptr<SplitContainer> horizontal = make_control<SplitContainer>(
        StableId("split.grip.horizontal"));
    (*horizontal).orientation = Orientation::horizontal;
    (*horizontal).splitter_width = 3.0;
    (*horizontal).splitter_hit_width = 9.0;
    (*horizontal).splitter_distance = 50.0;
    (*horizontal).collapse_panel = SplitFixedPanel::first;
    Window horizontal_window(horizontal, {180.0, 120.0});
    horizontal_window.perform_layout();
    const Control::Ptr horizontal_grip = (*horizontal).splitter_control();
    const Insets horizontal_outsets = (*horizontal_grip).visual_outsets();
    GripRecordingPainter horizontal_rest;
    (*horizontal_grip).on_paint(
        horizontal_rest, {0.0, 0.0,
                          (*horizontal_grip).absolute_bounds().width,
                          (*horizontal_grip).absolute_bounds().height});
    require(near(horizontal_outsets.top, 8.5) &&
                near(horizontal_outsets.bottom, 12.5) &&
                filled(horizontal_rest, (*horizontal_grip).absolute_bounds().width, 3.0) &&
                filled(horizontal_rest, 28.0, 7.0),
            "horizontal grip must transpose the same seam, actuator, and asymmetric shadow outsets");
}

void test_automatic_accommodation_and_user_override() {
    std::shared_ptr<gui_forms::SplitContainer> split = make_control<SplitContainer>(StableId("split.accommodation"));
    (*split).splitter_distance = 250.0;
    (*split).collapse_panel = SplitFixedPanel::second;
    (*split).automatic_collapse_threshold = 360.0;
    Window window(split, {400.0, 160.0});
    window.perform_layout();
    require(!(*split).second_collapsed(),
            "roomy split must begin expanded above its authored threshold");

    window.resize({330.0, 160.0});
    window.perform_layout();
    require((*split).second_collapsed() &&
                (*split).second_collapse_origin() ==
                    SplitCollapseOrigin::automatic_accommodation,
            "crossing below the threshold must record automatic collapse origin");

    require(window.request_focus((*split).splitter_control()) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                !(*split).second_collapsed(),
            "user must be able to restore an automatically collapsed pane");
    window.perform_layout();
    require(!(*split).second_collapsed(),
            "user restore below threshold must suppress immediate recollapse");

    window.resize({400.0, 160.0});
    window.perform_layout();
    window.resize({330.0, 160.0});
    window.perform_layout();
    require((*split).second_collapsed() &&
                (*split).second_collapse_origin() ==
                    SplitCollapseOrigin::automatic_accommodation,
            "crossing above then below must re-arm automatic accommodation");
    window.resize({400.0, 160.0});
    window.perform_layout();
    require(!(*split).second_collapsed(),
            "room restoration must restore only an automatically collapsed pane");

    (*split).set_second_collapsed(true, SplitCollapseOrigin::user);
    window.resize({500.0, 160.0});
    window.perform_layout();
    require((*split).second_collapsed() &&
                (*split).second_collapse_origin() == SplitCollapseOrigin::user,
            "room restoration must not override a user-collapsed pane");
}

void test_content_aware_maximum_extents() {
    std::shared_ptr<gui_forms::SplitContainer> right_bounded = make_control<SplitContainer>(StableId("split.max.right"));
    (*right_bounded).second_maximum = 180.0;
    (*right_bounded).splitter_distance = 100.0;
    Window right_window(right_bounded, {600.0, 160.0});
    right_window.perform_layout();
    require(near((*(*right_bounded).second_panel()).arranged_bounds().width, 180.0),
            "second maximum must stop a pane consuming space its content cannot use");
    (*right_bounded).splitter_distance = 0.0;
    right_window.perform_layout();
    require(near((*(*right_bounded).second_panel()).arranged_bounds().width, 180.0),
            "programmatic and pointer distances must share maximum constraints");

    std::shared_ptr<gui_forms::SplitContainer> left_bounded = make_control<SplitContainer>(StableId("split.max.left"));
    (*left_bounded).first_maximum = 200.0;
    (*left_bounded).splitter_distance = 500.0;
    Window left_window(left_bounded, {600.0, 160.0});
    left_window.perform_layout();
    require(near((*(*left_bounded).first_panel()).arranged_bounds().width, 200.0),
            "first maximum must symmetrically bound content-aware pane growth");

    bool invalid_rejected{};
    try {
        (*left_bounded).first_maximum = 10.0;
    } catch (const std::invalid_argument&) {
        invalid_rejected = true;
    }
    require(invalid_rejected,
            "pane maximum may not contradict its declared minimum");
}

void test_physical_hairline_asymmetric_hit_geometry_and_atomic_rejection() {
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("split.physical"));
    SplitSeamGeometry geometry;
    geometry.visible_thickness = 1.0;
    geometry.thickness_policy =
        SplitSeamThicknessPolicy::device_pixel_hairline;
    geometry.hit_before = 5.0;
    geometry.hit_after = 7.0;
    geometry.minimum_hit_target = 12.0;
    (*split).splitter_geometry = geometry;
    (*split).splitter_distance = 100.0;
    Window window(split, {300.0, 160.0});
    window.perform_layout();

    SplitSeamSnapshot at_one = (*split).splitter_seam_snapshot();
    require(near(at_one.visible_bounds.width, 1.0) &&
                near(at_one.visible_device_pixels, 1.0) &&
                near(at_one.hit_bounds.x, 95.0) &&
                near(at_one.hit_bounds.width, 13.0) &&
                near(at_one.visible_bounds.x - at_one.hit_bounds.x, 5.0),
            "one-times hairline must occupy one physical pixel inside its asymmetric logical hit extents");

    window.set_scale(2.0);
    window.perform_layout();
    SplitSeamSnapshot at_two = (*split).splitter_seam_snapshot();
    require(near(at_two.visible_bounds.width, 0.5) &&
                near(at_two.visible_device_pixels, 1.0) &&
                near(at_two.hit_bounds.x, 95.0) &&
                near(at_two.hit_bounds.width, 12.5) &&
                at_two.hit_bounds.width >= geometry.minimum_hit_target &&
                near((*(*split).second_panel()).arranged_bounds().x, 100.5),
            "two-times hairline must remain one physical pixel while pane and hit geometry stay coherent");
    require(window.hit_test({95.01, 10.0}) == (*split).splitter_control() &&
                window.hit_test({107.49, 10.0}) == (*split).splitter_control(),
            "both neighboring pane edges must deterministically resolve to the overlaid seam without a dead crack");

    (*split).splitter_distance = 100.26;
    window.perform_layout();
    const SplitSeamSnapshot snapped = (*split).splitter_seam_snapshot();
    require(near(snapped.visible_bounds.x, 100.5) &&
                near(snapped.visible_bounds.x * snapped.device_scale,
                     std::round(snapped.visible_bounds.x *
                                snapped.device_scale)),
            "physical hairline origin must snap deterministically to the device grid");
    (*split).splitter_distance = 100.0;
    window.perform_layout();

    GripRecordingPainter painter;
    (*(*split).splitter_control()).on_paint(
        painter, {0.0, 0.0, at_two.hit_bounds.width,
                  at_two.hit_bounds.height});
    require(painter.fills.size() == 1U &&
                near(painter.fills.front().width, 0.5) && painter.lines == 0U,
            "physical hairline paint must commit one half-logical fill at two-times scale without wider relief strokes");

    const SplitSeamGeometry before = (*split).splitter_geometry.read();
    SplitSeamGeometry invalid = before;
    invalid.hit_before = SplitSeamGeometry::maximum_hit_extension + 1.0;
    bool rejected{};
    try {
        (*split).splitter_geometry = invalid;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*split).splitter_geometry.read() == before,
            "over-budget seam geometry must be rejected atomically without partially publishing its fields");

    invalid = before;
    invalid.minimum_hit_target = 40.0;
    rejected = false;
    try {
        (*split).splitter_geometry = invalid;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*split).splitter_geometry.read() == before,
            "a hit geometry below its declared accessibility target must be rejected atomically");
}

void test_proximity_states_cancel_disable_detach_and_semantics() {
    std::shared_ptr<Panel> host = make_control<Panel>(StableId("split.proximity.host"));
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("split.proximity"));
    (*split).set_requested_bounds({0.0, 0.0, 360.0, 180.0});
    (*split).splitter_distance = 140.0;
    (*split).collapse_panel = SplitFixedPanel::second;
    std::shared_ptr<Button> field = make_control<Button>(StableId("split.proximity.field"),
                                                         "Selection keeper");
    (*field).set_requested_bounds({12.0, 12.0, 110.0, 24.0});
    (*(*split).first_panel()).add_child(field);
    (*host).add_child(split);
    Window window(host, {360.0, 180.0});
    window.perform_layout();
    require(window.request_focus(field),
            "proximity fixture requires independent content focus");

    const Rect hit = (*(*split).splitter_control()).absolute_bounds();
    PointerEvent near_event;
    near_event.action = PointerAction::move;
    near_event.position = {hit.x + 0.25, hit.y + 10.0};
    near_event.pointer_id = 7U;
    static_cast<void>(window.dispatch_pointer(near_event));
    require((*split).splitter_seam_snapshot().state == SplitSeamState::near &&
                (*split).splitter_seam_snapshot().transition_active &&
                window.focused_control() == field && window.next_wake(),
            "quiet proximity must reveal near state without stealing focus and schedule only its bounded transition");

    PointerEvent hot_event = near_event;
    hot_event.position = {hit.x + hit.width * 0.5,
                          hit.y + hit.height * 0.5};
    static_cast<void>(window.dispatch_pointer(hot_event));
    require((*split).splitter_seam_snapshot().state == SplitSeamState::hot &&
                window.focused_control() == field,
            "actuator proximity must enter hot state without changing content focus");

    PointerEvent down = near_event;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    const double original = (*split).splitter_distance.read();
    require(window.dispatch_pointer(down) &&
                (*split).splitter_seam_snapshot().state ==
                    SplitSeamState::dragging &&
                window.captured_control() == (*split).splitter_control(),
            "a press in the wide seam target must enter explicit drag state and capture");
    PointerEvent move = down;
    move.action = PointerAction::move;
    move.button = PointerButton::none;
    move.position.x += 30.0;
    require(window.dispatch_pointer(move) &&
                (*split).splitter_distance.read() > original,
            "captured seam drag must update its semantic value live");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                !window.captured_control() &&
                near((*split).splitter_distance.read(), original),
            "Escape must cancel a live seam drag, release capture, and restore its starting value");

    static_cast<void>(window.dispatch_pointer(down));
    require(window.captured_control() == (*split).splitter_control(),
            "disable fixture must begin with retained seam capture");
    (*split).enabled = false;
    require(!window.captured_control() &&
                (*split).splitter_seam_snapshot().state ==
                    SplitSeamState::disabled,
            "disabling an ancestor seam control must revoke capture and expose disabled state");
    (*split).enabled = true;
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, {20.0, 170.0}}));
    (*split).set_second_collapsed(true, SplitCollapseOrigin::user);
    window.perform_layout();
    require((*split).splitter_seam_snapshot().state ==
                SplitSeamState::collapsed,
            "a quiet collapsed seam must retain an explicit collapsed state");
    (*split).set_second_collapsed(false, SplitCollapseOrigin::user);
    window.perform_layout();

    const double before_semantic = (*split).splitter_distance.read();
    const SemanticDescriptor semantics = (*split).semantic_descriptor();
    require(semantics.numeric_value == before_semantic &&
                semantics.minimum_value.has_value() &&
                semantics.maximum_value.has_value() &&
                window.perform_semantic_action(
                    "split.proximity", SemanticAction::increment) &&
                (*split).splitter_distance.read() > before_semantic &&
                window.perform_semantic_action(
                    "split.proximity", SemanticAction::set_value, "170") &&
                near((*split).splitter_distance.read(), 170.0),
            "adjustable seam semantics must expose range value plus shared increment and set-value actions");

    const Rect detached_hit = (*(*split).splitter_control()).absolute_bounds();
    PointerEvent detach_down;
    detach_down.action = PointerAction::down;
    detach_down.button = PointerButton::primary;
    detach_down.position = {detached_hit.x + 0.25,
                            detached_hit.y + 10.0};
    require(window.dispatch_pointer(detach_down) &&
                window.captured_control() == (*split).splitter_control(),
            "detach fixture must begin with retained seam capture");
    static_cast<void>((*host).remove_child((*split).runtime_id()));
    require(!window.captured_control() && (*split).attached_window() == nullptr &&
                (*split).splitter_seam_snapshot().state == SplitSeamState::idle,
            "detaching a captured seam must release capture and clear every proximity interaction flag");
}

void test_bounded_seam_interpolation_and_reduced_motion() {
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("split.transition"));
    (*split).splitter_distance = 140.0;
    (*split).collapse_panel = SplitFixedPanel::second;
    Window window(split, {360.0, 180.0});
    window.perform_layout();

    const SplitSeamSnapshot resting = (*split).splitter_seam_snapshot();
    require(near(resting.transition_duration_milliseconds, 90.0) &&
                !resting.transition_active &&
                near(resting.actuator_bounds.width, 7.0) &&
                near(resting.actuator_bounds.height, 28.0),
            "collapse-tab interpolation must default to a quiescent 90-millisecond retained rest geometry");

    const Rect hit = (*(*split).splitter_control()).absolute_bounds();
    PointerEvent hover;
    hover.action = PointerAction::move;
    hover.position = {hit.x + hit.width * 0.5,
                      hit.y + hit.height * 0.5};
    hover.pointer_id = 41U;
    static_cast<void>(window.dispatch_pointer(hover));

    const SplitSeamSnapshot started = (*split).splitter_seam_snapshot();
    require(started.state == SplitSeamState::hot &&
                started.transition_active &&
                near(started.transition_progress, 0.0) &&
                near(started.actuator_bounds.width, 7.0) &&
                window.next_wake().has_value(),
            "hot proximity must start from the committed rest geometry and own one bounded frame lease");

    // The grip's paint-gated transition timer advances it: one tick, then a
    // paint before the next.
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + 45ms));
    const SplitSeamSnapshot middle = (*split).splitter_seam_snapshot();
    require(middle.transition_active &&
                middle.transition_progress > 0.0 &&
                middle.transition_progress < 1.0 &&
                middle.actuator_bounds.width > 7.0 &&
                middle.actuator_bounds.width < 12.0 &&
                middle.actuator_bounds.height > 28.0 &&
                middle.actuator_bounds.height < 42.0,
            "a half-duration frame must expose inspectable in-flight actuator geometry without moving the seam or hit target");
    require(middle.visible_bounds == resting.visible_bounds &&
                middle.hit_bounds == resting.hit_bounds,
            "visual interpolation must remain orthogonal to physical seam and proximity geometry");

    GripRecordingPainter transition_painter;
    window.paint(transition_painter);
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + 180ms));
    const SplitSeamSnapshot finished = (*split).splitter_seam_snapshot();
    require(!finished.transition_active &&
                near(finished.transition_progress, 1.0) &&
                near(finished.actuator_bounds.width, 12.0) &&
                near(finished.actuator_bounds.height, 42.0) &&
                !window.next_wake().has_value(),
            "the 90-millisecond transition must commit exact engaged geometry and revoke its final frame lease");

    PresentationSettings calm = window.presentation_settings();
    calm.reduced_motion = true;
    window.set_presentation_settings(calm);
    PointerEvent leave = hover;
    leave.action = PointerAction::leave;
    gui_forms::detail::ControlInputAccess::on_pointer(*(*split).splitter_control(), leave);
    const SplitSeamSnapshot reduced = (*split).splitter_seam_snapshot();
    require(!reduced.transition_active &&
                near(reduced.actuator_bounds.width, 7.0) &&
                near(reduced.actuator_bounds.height, 28.0) &&
                !window.next_wake().has_value(),
            "reduced motion must replace interpolation with an immediate exact state change");

    (*split).splitter_transition_duration = 0ms;
    require((*split).splitter_transition_duration.read() == 0ms,
            "consumers must be able to explicitly select immediate seam presentation");
    const FrameInterval before = (*split).splitter_transition_duration.read();
    bool rejected{};
    try {
        (*split).splitter_transition_duration = 1001ms;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*split).splitter_transition_duration.read() == before,
            "over-budget seam duration must be rejected atomically");
}

// The seam on the drag engine (roadmap 2.1, step M1): every distance change
// as "old>new reason;", so a script's trace can be compared whole.
class SplitTrace final {
public:
    explicit SplitTrace(std::string& out) : out_(&out) {}

    void operator()(const SplitChangeEvent& event) const {
        std::array<char, 64> line{};
        const int written = std::snprintf(line.data(), line.size(), "%.0f>%.0f %s;", event.old_distance,
                                          event.new_distance, reason_name(event.reason));
        if (written > 0) (*out_).append(line.data());
    }

private:
    [[nodiscard]] static const char* reason_name(SplitChangeReason reason) noexcept {
        switch (reason) {
        case SplitChangeReason::pointer: return "pointer";
        case SplitChangeReason::keyboard: return "keyboard";
        case SplitChangeReason::cancel: return "cancel";
        case SplitChangeReason::programmatic:
        case SplitChangeReason::semantic:
        case SplitChangeReason::collapse:
        case SplitChangeReason::container_resize:
        default: return "other";
        }
    }

    std::string* out_;
};

// Counts the seam's typed drag interactions and keeps the last end.
struct SeamInteractions final : public Component {
    int started{};
    int dropped{};
    int ended{};
    DragEnd last_end{DragEnd::none};

    void on_started(const DragInteraction&) { ++started; }
    void on_dropped(const DragInteraction&) { ++dropped; }
    void on_ended(const DragInteraction& interaction) {
        ++ended;
        last_end = interaction.end;
    }
};

PointerEvent seam_pointer(PointerAction action, double x) {
    PointerEvent event;
    event.action = action;
    event.button = action == PointerAction::move ? PointerButton::none : PointerButton::primary;
    event.position = {x, 70.0};
    event.pointer_id = 11U;
    return event;
}

KeyEvent seam_key(std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    KeyEvent event;
    event.action = KeyAction::down;
    event.physical_key = physical_key;
    event.modifiers = modifiers;
    return event;
}

// The pointer script gives the trace the code before the engine gave: the
// distance follows the pointer by its grab offset, one change per move, the
// release sets the last distance. Beyond either end the distance stops at the
// constrained range (before the engine a move left of the container asked
// for a negative distance, which set_distance refused by throwing).
void test_seam_drag_engine_parity() {
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("split.parity"));
    (*split).splitter_distance = 120.0;
    Window window(split, {400.0, 180.0});
    window.perform_layout();
    std::string trace;
    SubscriptionToken changed = (*split).splitter_changed().subscribe(SplitTrace(trace));
    SeamInteractions interactions;
    on((*split).drag_started(), interactions, &SeamInteractions::on_started);
    on((*split).drag_dropped(), interactions, &SeamInteractions::on_dropped);
    on((*split).drag_ended(), interactions, &SeamInteractions::on_ended);

    require(window.dispatch_pointer(seam_pointer(PointerAction::down, 121.0)) &&
                window.captured_control() == (*split).splitter_control() &&
                window.drag_engine().phase() == DragPhase::dragging &&
                (*split).splitter_seam_snapshot().state == SplitSeamState::dragging,
            "a seam press starts the engine's drag with the grip holding the capture");
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::move, 141.0)));
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::move, 161.0)));
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::move, 181.0)));
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::up, 191.0)));
    require(trace == "120>140 pointer;140>160 pointer;160>180 pointer;180>190 pointer;",
            "the seam's pointer trace matches the behaviour before the engine");
    require(!window.captured_control() && window.drag_engine().phase() == DragPhase::idle &&
                (*split).splitter_seam_snapshot().state != SplitSeamState::dragging,
            "the release ends the drag and its capture");
    require(interactions.started == 1 && interactions.dropped == 1 && interactions.ended == 1 &&
                interactions.last_end == DragEnd::free,
            "the seam raises its typed drag interactions (phase E4)");

    window.perform_layout();
    const SemanticDescriptor range = (*split).semantic_descriptor();
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::down, 191.0)));
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::move, 1000.0)));
    require(near((*split).splitter_distance.read(), *range.maximum_value), "a drag past the end stops at the maximum");
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::move, -50.0)));
    require(near((*split).splitter_distance.read(), *range.minimum_value),
            "a drag past the start stops at the minimum, with no refusal");
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::up, -50.0)));
    require(near((*split).splitter_distance.read(), *range.minimum_value), "the release keeps the clamped distance");
}

// Every end but a release puts the distance back with the cancel reason:
// Escape, a lost capture and the window's deactivation. Before the engine a
// lost capture kept the dragged distance; the engine's cancel rule restores,
// as the Splitter's does (recorded in the Windows Forms divergences).
void test_seam_drag_cancels_restore() {
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("split.cancels"));
    (*split).splitter_distance = 120.0;
    Window window(split, {400.0, 180.0});
    window.perform_layout();
    std::string trace;
    SubscriptionToken changed = (*split).splitter_changed().subscribe(SplitTrace(trace));

    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::down, 121.0)));
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::move, 151.0)));
    require(window.dispatch_key(seam_key(PhysicalKey::escape)) && near((*split).splitter_distance.read(), 120.0) &&
                !window.captured_control(),
            "Escape restores and releases the capture");
    window.perform_layout();

    trace.clear();
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::down, 121.0)));
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::move, 141.0)));
    (*(*split).splitter_control()).set_pointer_capture(false);
    require(trace == "120>140 pointer;140>120 cancel;" && window.drag_engine().phase() == DragPhase::idle,
            "a lost capture ends the drag and restores");
    window.perform_layout();

    window.set_active(true);
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::down, 121.0)));
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::move, 171.0)));
    window.set_active(false);
    require(near((*split).splitter_distance.read(), 120.0) && window.drag_engine().phase() == DragPhase::idle,
            "the window's deactivation cancels and restores");

    // Escape before any move changed nothing, so nothing is restored.
    window.set_active(true);
    trace.clear();
    static_cast<void>(window.dispatch_pointer(seam_pointer(PointerAction::down, 121.0)));
    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::escape)));
    require(trace.empty(), "Escape right after the press reports no change");
}

// The keyboard drag through the begin_keyboard_drag rule action on the grip:
// arrows along the axis step by keyboard_increment (Ctrl ten steps), Enter
// keeps the position, Escape restores it, and each end is announced. A fixed
// seam refuses.
void test_seam_keyboard_drag() {
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("split.keyboard"));
    (*split).splitter_distance = 120.0;
    (*split).keyboard_increment = 4.0;
    Window window(split, {400.0, 180.0});
    window.perform_layout();
    std::string trace;
    SubscriptionToken changed = (*split).splitter_changed().subscribe(SplitTrace(trace));
    Rule move_key{};
    move_key.trigger = trigger::key_pressed(PhysicalKey::f3);
    move_key.actions.push_back(builtin::begin_keyboard_drag(*(*split).splitter_control()));
    require(window.rules().add(move_key).refusal == RuleRefusal::none, "a move-key rule is armed");

    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::f3)));
    DragEngine& engine = window.drag_engine();
    require(engine.mode() == DragInputMode::keyboard, "the rule action on the grip starts a keyboard drag");
    require((*split).splitter_seam_snapshot().state == SplitSeamState::dragging,
            "the rule action on the grip starts the container's keyboard drag");
    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::right)));
    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::right, Modifier::control)));
    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::down)));
    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::enter)));
    require(trace == "120>124 keyboard;124>164 keyboard;" && engine.phase() == DragPhase::idle,
            "arrows step by the increment, Ctrl by ten, across the axis nothing, and Enter keeps it");
    require(engine.announcement() == "Split position 164, done", "the end is announced");

    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::f3)));
    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::left)));
    static_cast<void>(window.dispatch_key(seam_key(PhysicalKey::escape)));
    require(near((*split).splitter_distance.read(), 164.0) && engine.announcement() == "Split position 164, restored",
            "Escape restores and says so");

    (*split).splitter_fixed = true;
    require(!(*(*split).splitter_control()).begin_keyboard_drag() && engine.phase() == DragPhase::idle,
            "a fixed seam refuses the keyboard drag");
}

} // namespace

int main() {
    try {
        test_stable_tree_and_constrained_geometry();
        test_live_pointer_and_keyboard_resize();
        test_pane_surface_background();
        test_collapse_focus_restore_and_fixed_panel_resize();
        test_horizontal_orientation_and_thread_guard();
        test_seam_tab_pointer_keyboard_and_semantic_collapse();
        test_seam_grip_rest_proximity_focus_press_and_orientation_paint();
        test_automatic_accommodation_and_user_override();
        test_content_aware_maximum_extents();
        test_physical_hairline_asymmetric_hit_geometry_and_atomic_rejection();
        test_proximity_states_cancel_disable_detach_and_semantics();
        test_bounded_seam_interpolation_and_reduced_motion();
        test_seam_drag_engine_parity();
        test_seam_drag_cancels_restore();
        test_seam_keyboard_drag();
        std::cout << "split-container-tests: pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "split-container-tests: fail: " << error.what() << '\n';
        return 1;
    }
}
