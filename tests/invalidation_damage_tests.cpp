#include "gui_forms/gui_forms.hpp"
#include "../src/core/damage/device_damage/device_damage.hpp"
#include "support/named_callbacks.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

// Each test holds its window on the heap. main inlines every test, and
// the sanitizer build gives each inlined local its own stack slot, so
// windows held on the stack add up to a frame of 74 KB in main.

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++draws; }
    void stroke_rect(Rect, Color, double) override { ++draws; }
    void draw_line(Point, Point, Color, double) override { ++draws; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++draws; }
    void draw_image(ImageId, Rect, double) override { ++draws; }

    std::uint64_t draws{};
};

class ReplayBoundaryPainter final : public Painter {
public:
    void save() override { reach_boundary(); }
    void restore() override { reach_boundary(); }
    void translate(Point) override { reach_boundary(); }
    void clip_rect(Rect) override { reach_boundary(); }
    void fill_rect(Rect, Color) override { draw(); }
    void stroke_rect(Rect, Color, double) override { draw(); }
    void draw_line(Point, Point, Color, double) override { draw(); }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {
        draw();
    }
    void draw_image(ImageId, Rect, double) override { draw(); }

    std::function<void()> on_first_command;
    bool throw_after_boundary{};
    std::uint64_t draws{};

private:
    void draw() {
        reach_boundary();
        ++draws;
    }

    void reach_boundary() {
        if (boundary_reached_) return;
        boundary_reached_ = true;
        if (on_first_command) on_first_command();
        if (throw_after_boundary) {
            throw std::runtime_error("backend replay failed");
        }
    }

    bool boundary_reached_{};
};

class ReplayPressureControl final : public Control {
public:
    explicit ReplayPressureControl(StableId id) : Control(std::move(id)) {}

    void set_value(std::uint32_t value) {
        if (value_ == value) return;
        value_ = value;
        invalidate(invalidation::paint_only);
    }

    [[nodiscard]] std::uint32_t value() const noexcept { return value_; }

    void on_paint(Painter& painter, Rect damage) override {
        ++paint_calls;
        last_painted_value = value_;
        painter.fill_rect(
            damage,
            Color::rgba(static_cast<std::uint8_t>(value_ & 0xffU), 70, 100));
    }

    void on_pointer(PointerEvent& event) override {
        const PaintLeaseState state = (*window()).paint_lease_snapshot().state;
        callback_during_lease |= state == PaintLeaseState::rendering ||
            state == PaintLeaseState::rendering_dirty;
        ++pointer_calls;
        event.handled = true;
        invalidate(invalidation::paint_only);
    }

    void on_key(KeyEvent& event) override {
        const PaintLeaseState state = (*window()).paint_lease_snapshot().state;
        callback_during_lease |= state == PaintLeaseState::rendering ||
            state == PaintLeaseState::rendering_dirty;
        ++key_calls;
        event.handled = true;
    }

    std::uint32_t last_painted_value{};
    std::uint64_t paint_calls{};
    std::uint64_t pointer_calls{};
    std::uint64_t key_calls{};
    bool callback_during_lease{};

private:
    std::uint32_t value_{};
};

class CountingControl : public Control {
public:
    explicit CountingControl(StableId id) : Control(std::move(id)) {}

    Size measure(Size available) override {
        ++measure_calls;
        return Control::measure(available);
    }
    void arrange(Rect bounds) override {
        ++arrange_calls;
        Control::arrange(bounds);
    }
    void on_paint(Painter& painter, Rect damage) override {
        ++paint_calls;
        painter.fill_rect(damage, Color::rgba(12, 34, 56));
    }

    std::uint64_t measure_calls{};
    std::uint64_t arrange_calls{};
    std::uint64_t paint_calls{};
};

class ReentrantLayoutControl final : public CountingControl {
public:
    explicit ReentrantLayoutControl(StableId id) : CountingControl(std::move(id)) {}

    void arrange(Rect bounds) override {
        CountingControl::arrange(bounds);
        Rect requested = requested_bounds();
        requested.x += 1.0;
        set_requested_bounds(requested);
    }
};

class TransactionPaintControl final : public Control {
public:
    explicit TransactionPaintControl(StableId id) : Control(std::move(id)) {}

    void on_paint(Painter& painter, Rect damage) override {
        ++paint_calls;
        if (request_nested_paint && paint_calls == 1U) {
            (*window()).paint(painter, damage);
        }
        if (invalidate_during_paint && paint_calls == 1U) {
            invalidate(invalidation::paint_only);
            state_after_touch = (*window()).paint_lease_snapshot().state;
        }
        if (throw_during_paint && paint_calls == 1U) {
            throw std::runtime_error("candidate paint failed");
        }
        painter.fill_rect(damage, Color::rgba(20, 40, 60));
    }

    bool request_nested_paint{};
    bool invalidate_during_paint{};
    bool throw_during_paint{};
    PaintLeaseState state_after_touch{PaintLeaseState::clean};
    std::uint64_t paint_calls{};
};

class DeferredInputPaintControl final : public Control {
public:
    explicit DeferredInputPaintControl(StableId id) : Control(std::move(id)) {}

    void on_paint(Painter& painter, Rect damage) override {
        ++paint_calls;
        in_application_paint = true;
        if (inject_pressure_during_next_paint) {
            inject_pressure_during_next_paint = false;
            for (std::uint32_t index = 0U; index < 100U; ++index) {
                PointerEvent move;
                move.action = PointerAction::move;
                move.pointer_id = 7U;
                move.position = {20.0 + static_cast<double>(index) * 0.1, 20.0};
                all_ingress_accepted =
                    (*window()).dispatch_pointer(std::move(move)) &&
                    all_ingress_accepted;
            }
            PointerEvent down;
            down.action = PointerAction::down;
            down.button = PointerButton::primary;
            down.pointer_id = 7U;
            down.position = {30.0, 20.0};
            all_ingress_accepted =
                (*window()).dispatch_pointer(std::move(down)) &&
                all_ingress_accepted;
            all_ingress_accepted =
                (*window()).dispatch_key(
                    {KeyAction::down, PhysicalKey::a}) &&
                all_ingress_accepted;
            TextInputEvent text;
            text.text_utf8 = "a";
            all_ingress_accepted =
                (*window()).dispatch_text(std::move(text)) &&
                all_ingress_accepted;
            during_lease = (*window()).deferred_input_snapshot();
        }
        if (inject_semantic_during_next_paint) {
            inject_semantic_during_next_paint = false;
            semantic_retained = (*window()).perform_semantic_action(
                stable_id().value(), SemanticAction::press);
            during_lease = (*window()).deferred_input_snapshot();
        }
        if (dispose_during_next_paint) {
            dispose_during_next_paint = false;
            for (std::size_t index = 0U;
                 index < maximum_deferred_inputs + 1U; ++index) {
                const bool accepted = (*window()).dispatch_key(
                    {KeyAction::down, PhysicalKey::a});
                ingress_accepted += accepted ? 1U : 0U;
                all_ingress_accepted = accepted && all_ingress_accepted;
            }
            during_lease = (*window()).deferred_input_snapshot();
            in_application_paint = false;
            dispose();
            return;
        }
        in_application_paint = false;
        painter.fill_rect(damage, Color::rgba(40, 70, 100));
    }

    void on_pointer(PointerEvent& event) override {
        callback_during_paint |= in_application_paint;
        switch (event.action) {
        case PointerAction::enter:
            delivered.emplace_back("enter");
            break;
        case PointerAction::move:
            delivered.emplace_back("move");
            last_move = event.position;
            break;
        case PointerAction::down:
            delivered.emplace_back("down");
            invalidate(invalidation::paint_only);
            break;
        default: break; // The probe records only enter, move and down.
        }
        event.handled = true;
    }

    void on_key(KeyEvent& event) override {
        callback_during_paint |= in_application_paint;
        delivered.emplace_back("key");
        if (throw_on_key) {
            throw std::runtime_error("deferred key callback failed");
        }
        event.handled = true;
    }

    void on_text_input(TextInputEvent& event) override {
        callback_during_paint |= in_application_paint;
        delivered.emplace_back("text");
        event.handled = true;
    }

    bool on_semantic_action(SemanticAction action,
                            std::string_view) override {
        callback_during_paint |= in_application_paint;
        if (action != SemanticAction::press) return false;
        ++semantic_actions;
        invalidate(invalidation::paint_only);
        return true;
    }

    bool inject_pressure_during_next_paint{};
    bool dispose_during_next_paint{};
    bool inject_semantic_during_next_paint{};
    bool in_application_paint{};
    bool callback_during_paint{};
    bool all_ingress_accepted{true};
    bool throw_on_key{};
    bool semantic_retained{};
    std::uint64_t ingress_accepted{};
    Point last_move{};
    DeferredInputSnapshot during_lease;
    std::vector<std::string> delivered;
    std::uint64_t paint_calls{};
    std::uint64_t semantic_actions{};
};

struct TreeFixture {
    TreeFixture() {
        (*root).set_requested_bounds({0.0, 0.0, 300.0, 180.0});
        (*left).set_requested_bounds({0.0, 0.0, 140.0, 180.0});
        (*right).set_requested_bounds({160.0, 0.0, 140.0, 180.0});
        (*left_leaf).set_requested_bounds({10.0, 10.0, 40.0, 30.0});
        (*left_sibling).set_requested_bounds({10.0, 60.0, 40.0, 30.0});
        (*right_leaf).set_requested_bounds({10.0, 10.0, 40.0, 30.0});
        (*left).add_child(left_leaf);
        (*left).add_child(left_sibling);
        (*right).add_child(right_leaf);
        (*root).add_child(left);
        (*root).add_child(right);
        window = std::make_unique<Window>(root, Size{300.0, 180.0});
        (*window).perform_layout();
        paint_pending();
        (*window).reset_activity_metrics();
    }

    void paint_pending() {
        DamageRegion damage = (*window).take_damage();
        if (!damage.empty()) {
            (*window).paint(painter, damage.bounds());
        }
    }

    std::shared_ptr<CountingControl> root =
        make_control<CountingControl>(StableId("typed.root"));
    std::shared_ptr<CountingControl> left =
        make_control<CountingControl>(StableId("typed.left"));
    std::shared_ptr<CountingControl> right =
        make_control<CountingControl>(StableId("typed.right"));
    std::shared_ptr<CountingControl> left_leaf =
        make_control<CountingControl>(StableId("typed.left.leaf"));
    std::shared_ptr<CountingControl> left_sibling =
        make_control<CountingControl>(StableId("typed.left.sibling"));
    std::shared_ptr<CountingControl> right_leaf =
        make_control<CountingControl>(StableId("typed.right.leaf"));
    std::unique_ptr<Window> window;
    NullPainter painter;
};

class ApplyReplayPressure final {
public:
    ApplyReplayPressure(ReplayPressureControl& root, Window& window,
                        ReplayBoundaryPainter& painter)
        : root_(root), window_(window), painter_(painter) {}

    void operator()() const {
        for (std::uint32_t revision = 1U; revision <= 100U; ++revision) {
            root_.set_value(revision);
        }
        PointerEvent pointer;
        pointer.action = PointerAction::down;
        pointer.button = PointerButton::primary;
        pointer.pointer_id = 31U;
        pointer.position = {20.0, 20.0};
        require(window_.dispatch_pointer(std::move(pointer)),
                "synthetic pointer must be retained at the replay boundary");
        for (std::size_t nested = 0U; nested < 8U; ++nested) {
            require(!window_.paint(painter_).has_value(),
                    "native-style replay reentry must not obtain a receipt");
        }
    }

private:
    ReplayPressureControl& root_;
    Window& window_;
    ReplayBoundaryPainter& painter_;
};

class ResizeDuringReplay final {
public:
    ResizeDuringReplay(ReplayPressureControl& root, Window& window)
        : root_(root), window_(window) {}

    void operator()() const {
        root_.set_value(2U);
        window_.resize({160.0, 100.0});
    }

private:
    ReplayPressureControl& root_;
    Window& window_;
};

class RetireDuringReplay final {
public:
    RetireDuringReplay(ReplayPressureControl& root, Window& window)
        : root_(root), window_(window) {}

    void operator()() const {
        require(window_.dispatch_key({KeyAction::down, PhysicalKey::a}),
                "retirement probe key must enter the bounded lease queue");
        root_.dispose();
    }

private:
    ReplayPressureControl& root_;
    Window& window_;
};

void test_typed_effect_vocabulary() {
    require(has_dirty(invalidation::bounds, Dirty::measure) &&
                has_dirty(invalidation::bounds, Dirty::arrange) &&
                has_dirty(invalidation::bounds, Dirty::paint) &&
                has_dirty(invalidation::bounds, Dirty::hit_test),
            "bounds effect must explicitly declare measure/arrange/paint/hit-test");
    require(has_dirty(invalidation::text_content, Dirty::measure) &&
                has_dirty(invalidation::text_content, Dirty::accessibility),
            "text effect must remeasure and republish accessibility");
    require(invalidation::style_only == Dirty::paint,
            "a style effect is paint only (owner ruling D4: no unread style kind)");
}

void test_affected_path_layout() {
    TreeFixture fixture;
    const std::uint64_t right_measure = (*fixture.right).measure_calls;
    const std::uint64_t right_arrange = (*fixture.right).arrange_calls;
    const std::uint64_t sibling_measure = (*fixture.left_sibling).measure_calls;
    const std::uint64_t sibling_arrange = (*fixture.left_sibling).arrange_calls;

    (*fixture.left_leaf).set_requested_bounds({20.0, 15.0, 44.0, 32.0});
    require((*fixture.left_leaf).arranged_bounds() == Rect{20.0, 15.0, 44.0, 32.0},
            "read barrier must commit the affected leaf geometry");
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.controls_measured == 3 && metrics.controls_arranged == 3,
            "leaf bounds change must invoke layout only on root-to-leaf path");
    require(metrics.measure_nodes_visited == 3 && metrics.arrange_nodes_visited == 3,
            "layout visit metrics must match the affected path exactly");
    require((*fixture.right).measure_calls == right_measure &&
                (*fixture.right).arrange_calls == right_arrange &&
                (*fixture.left_sibling).measure_calls == sibling_measure &&
                (*fixture.left_sibling).arrange_calls == sibling_arrange,
            "unaffected sibling subtrees must not receive layout callbacks");
}

void test_full_subtree_invalidation_is_explicit() {
    TreeFixture fixture;
    (*fixture.window).resize({320.0, 190.0});
    (*fixture.window).perform_layout();
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    // A resize arranges the root again (its layout slot is marked), and a
    // child only when the root gives it a new slot; it measures again only
    // the controls the client size reaches. The root requests its whole
    // size, so its children keep their slots and extents, and keep their
    // arrangement.
    require(metrics.controls_arranged == 1 && metrics.arrange_nodes_visited == 1,
            "window resize must arrange only the root when no child's slot changes");
    require((*fixture.left).arrange_calls == 1U && (*fixture.right).arrange_calls == 1U &&
                (*fixture.left_leaf).arrange_calls == 1U && (*fixture.right_leaf).arrange_calls == 1U,
            "children whose slots did not change must not be arranged again");
    require(metrics.controls_measured == 1 && metrics.measure_nodes_visited == 1,
            "window resize must measure only the controls the client size reaches");
}

void test_damage_take_commits_layout_generated_geometry_damage() {
    std::shared_ptr<CountingControl> root =
        make_control<CountingControl>(StableId("damage.layout.root"));
    (*root).padding = {10.0, 10.0, 10.0, 10.0};
    std::shared_ptr<CountingControl> fill =
        make_control<CountingControl>(StableId("damage.layout.fill"));
    (*fill).set_requested_bounds({0.0, 0.0, 20.0, 20.0});
    (*fill).dock = DockStyle::fill;
    std::shared_ptr<CountingControl> top =
        make_control<CountingControl>(StableId("damage.layout.top"));
    (*top).set_requested_bounds({0.0, 0.0, 20.0, 24.0});
    (*top).dock = DockStyle::top;
    std::shared_ptr<CountingControl> left =
        make_control<CountingControl>(StableId("damage.layout.left"));
    (*left).set_requested_bounds({0.0, 0.0, 50.0, 20.0});
    (*left).dock = DockStyle::left;
    (*root).add_child(left);
    (*root).add_child(top);
    (*root).add_child(fill);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{300.0, 180.0});
    Window& window = *held_window;
    window.perform_layout();
    NullPainter painter;
    DamageRegion initial = window.take_damage();
    window.paint(painter, initial.bounds());
    const Rect old_top = (*top).arranged_bounds();
    const Rect old_fill = (*fill).arranged_bounds();

    (*left).visible = false;
    DamageRegion geometry_damage = window.take_damage();
    const Rect new_top = (*top).arranged_bounds();
    const Rect new_fill = (*fill).arranged_bounds();
    require(new_top.x < old_top.x && new_top.width > old_top.width &&
                new_fill.x < old_fill.x && new_fill.width > old_fill.width,
            "taking host damage must commit sibling geometry released by hidden Dock");
    const Rect bounds = geometry_damage.bounds();
    require(bounds.x <= new_top.x &&
                bounds.x + bounds.width >= old_top.x + old_top.width &&
                bounds.y <= new_fill.y &&
                bounds.y + bounds.height >= old_fill.y + old_fill.height,
            "host damage must cover both old and newly arranged docked sibling pixels");
    window.paint(painter, geometry_damage.bounds());
    require(!window.needs_frame(),
            "one host paint transaction must consume layout-generated damage completely");
}

// A control whose anatomy declares the hot state, as a stock control that
// draws hot from its anatomy states does (stage T4).
constexpr std::array<AnatomyState, 4> hot_pad_states{AnatomyState::normal, AnatomyState::enabled,
                                                     AnatomyState::disabled, AnatomyState::hot};
constexpr ControlAnatomy hot_pad_anatomy{
    .control_id = "hot_pad",
    .parts = {},
    .states = hot_pad_states,
    .custom_states = {},
};

class HotPad final : public Control {
public:
    explicit HotPad(StableId id) : Control(std::move(id)) {}
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override { return hot_pad_anatomy; }
};

bool is_hot(const Control& control) {
    const bool hot = control.anatomy_states().contains(AnatomyState::hot);
    return hot;
}

void move_pointer(Window& window, PointerAction action, Point position) {
    PointerEvent event{};
    event.action = action;
    event.position = position;
    static_cast<void>(window.dispatch_pointer(event));
}

// The window's hovered control is hot (stage T4): the pointer moving onto
// a control makes it hot and off it makes it not, and the window repaints
// only a control whose anatomy declares hot. Leaving the window, or the
// hovered control leaving the interaction, clears it.
void test_hovered_control_is_hot() {
    std::shared_ptr<CountingControl> root = make_control<CountingControl>(StableId("hover.root"));
    std::shared_ptr<HotPad> pad = make_control<HotPad>(StableId("hover.pad"));
    (*pad).set_requested_bounds({0.0, 0.0, 50.0, 40.0});
    (*pad).dock = DockStyle::left;
    std::shared_ptr<CountingControl> quiet = make_control<CountingControl>(StableId("hover.quiet"));
    (*quiet).set_requested_bounds({0.0, 0.0, 50.0, 40.0});
    (*quiet).dock = DockStyle::left;
    (*root).add_child(pad);
    (*root).add_child(quiet);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 40.0});
    Window& window = *held_window;
    window.perform_layout();
    NullPainter painter;
    DamageRegion initial = window.take_damage();
    window.paint(painter, initial.bounds());
    require(!is_hot(*pad) && !is_hot(*quiet), "nothing is hot before the pointer arrives");

    const Rect pad_bounds = (*pad).absolute_bounds();
    move_pointer(window, PointerAction::move, Point{10.0, 10.0});
    require(is_hot(*pad) && !is_hot(*quiet), "the control under the pointer is hot");
    const DamageRegion entered = window.take_damage();
    require(!entered.empty() && entered.bounds().x >= pad_bounds.x &&
                entered.bounds().x + entered.bounds().width <= pad_bounds.x + pad_bounds.width,
            "a control declaring hot is repainted, within its bounds, as it becomes hot");
    window.paint(painter, entered.bounds());

    move_pointer(window, PointerAction::move, Point{70.0, 10.0});
    require(!is_hot(*pad) && is_hot(*quiet), "moving off a control makes the next one hot instead");
    const DamageRegion moved = window.take_damage();
    require(!moved.empty() && moved.bounds().x + moved.bounds().width <= pad_bounds.x + pad_bounds.width,
            "only the control declaring hot is repainted; the quiet one reports hot without a repaint");
    window.paint(painter, moved.bounds());

    move_pointer(window, PointerAction::leave, Point{70.0, 10.0});
    require(!is_hot(*quiet), "the pointer leaving the window leaves nothing hot");
    move_pointer(window, PointerAction::move, Point{10.0, 10.0});
    require(is_hot(*pad), "and returning makes the control under it hot again");

    (*pad).enabled = false;
    require(!is_hot(*pad), "a hovered control leaving the interaction is no longer hot");
}

// Hover is paint-only: the pointer moving onto and off controls that declare
// hot, back and forth across their edges, makes them hot and repaints them but runs no
// measure or arrange pass anywhere (the guard asked for after PlaySuite's
// bounce, which was a present fault, not a relayout).
void test_hover_runs_no_layout_pass() {
    std::shared_ptr<CountingControl> root = make_control<CountingControl>(StableId("hover-layout.root"));
    std::shared_ptr<HotPad> first = make_control<HotPad>(StableId("hover-layout.first"));
    (*first).set_requested_bounds({0.0, 0.0, 80.0, 30.0});
    (*first).dock = DockStyle::top;
    std::shared_ptr<HotPad> second = make_control<HotPad>(StableId("hover-layout.second"));
    (*second).set_requested_bounds({0.0, 0.0, 80.0, 30.0});
    (*second).dock = DockStyle::top;
    (*root).add_child(first);
    (*root).add_child(second);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 120.0});
    Window& window = *held_window;
    window.perform_layout();
    NullPainter painter;
    DamageRegion initial = window.take_damage();
    window.paint(painter, initial.bounds());
    window.reset_activity_metrics();

    // Across the first button's lower edge and the second's, and out of the
    // window, three times.
    const std::array<Point, 6> path{Point{40.0, 10.0}, Point{40.0, 29.5}, Point{40.0, 30.5},
                                    Point{40.0, 59.5}, Point{40.0, 60.5}, Point{40.0, 100.0}};
    bool repainted = false;
    for (int round = 0; round < 3; ++round) {
        for (const Point position : path) {
            move_pointer(window, PointerAction::move, position);
            window.perform_layout();
            const DamageRegion damage = window.take_damage();
            if (!damage.empty()) {
                repainted = true;
                window.paint(painter, damage.bounds());
            }
        }
        move_pointer(window, PointerAction::leave, Point{40.0, 100.0});
        window.perform_layout();
        const DamageRegion left = window.take_damage();
        if (!left.empty()) window.paint(painter, left.bounds());
    }
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(repainted, "hovering controls that declare hot repaints them");
    require(metrics.measure_passes == 0 && metrics.arrange_passes == 0,
            "hovering in and out across control edges runs no measure or arrange pass");
    require(metrics.resize_relayouts == 0, "and resizes nothing");
}

void test_reentrant_layout_is_bounded() {
    std::shared_ptr<ReentrantLayoutControl> root =
        make_control<ReentrantLayoutControl>(StableId("typed.reentrant"));
    (*root).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{100.0, 80.0});
    Window& window = *held_window;
    window.reset_activity_metrics();

    window.perform_layout();
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(metrics.measure_passes == 4 && metrics.arrange_passes == 4,
            "reentrant layout must stop at the declared four-pass guard");
    require(metrics.bounded_pass_limit_hits == 1,
            "an unquiescent layout must report one structured pass-limit hit");
}

void test_paint_metrics_report_chunk_work() {
    TreeFixture fixture;
    (*fixture.left_leaf).invalidate(invalidation::paint_only);
    fixture.paint_pending();
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.measure_passes == 0 && metrics.arrange_passes == 0,
            "paint-only invalidation must not trigger layout");
    require(metrics.paint_invalidations_consumed == 1,
            "paint pass must report the exact consumed invalidation count");
    require(metrics.display_chunks_rebuilt == 1 && metrics.display_chunks_reused == 2,
            "leaf paint must rebuild one chunk and reuse the two affected ancestors");
    require(metrics.display_commands_replayed > 0 && metrics.display_cache_entries == 6,
            "chunk metrics must expose replay work and the six-entry retained cache");
    require(metrics.paint_nodes_visited >= metrics.controls_painted &&
                metrics.controls_painted > 0,
            "paint visit and callback counters must describe different work");
}

void test_declared_mutation_guard() {
    TreeFixture fixture;
#ifndef NDEBUG
    const Dirty before = (*fixture.left_leaf).dirty();
#endif
    bool rejected = false;
    try {
        (*fixture.left_leaf).invalidate_declared(Dirty::none);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.undeclared_mutations == 1,
            "undeclared mutation attempt must be a structured diagnostic");
#ifndef NDEBUG
    require(rejected && (*fixture.left_leaf).dirty() == before,
            "development build must reject undeclared mutation without changing dirtiness");
#else
    require(!rejected &&
                has_dirty((*fixture.left_leaf).dirty(), invalidation::conservative_subtree),
            "production build must conservatively invalidate undeclared mutation subtree");
#endif
}

void test_damage_compaction_and_complexity_guard() {
    DamageRegion adjacent;
    adjacent.add({0.0, 0.0, 10.0, 10.0});
    adjacent.add({10.0, 0.0, 10.0, 10.0});
    require(adjacent.rectangle_count() == 1 && adjacent.area() == 200.0 &&
                adjacent.compaction_count() == 1,
            "exact adjacent damage must compact without adding overdraw");

    DamageRegion corner_overlap;
    corner_overlap.add({0.0, 0.0, 10.0, 10.0});
    corner_overlap.add({5.0, 5.0, 10.0, 10.0});
    require(corner_overlap.rectangle_count() == 2 && corner_overlap.area() == 175.0,
            "non-rectangular union must retain exact rectangles below complexity guard");

    DamageRegion guarded;
    for (std::size_t index = 0; index <= DamageRegion::maximum_rectangles; ++index) {
        guarded.add({static_cast<double>(index * 3U), 0.0, 1.0, 1.0});
    }
    require(guarded.rectangle_count() <= DamageRegion::maximum_rectangles &&
                guarded.collapse_count() == 1,
            "damage complexity guard must collapse an over-limit region deterministically");
}

void test_device_damage_alignment_is_outward_and_scale_exact() {
    const Rect one_percent{0.0, 0.0, 900.0, 6.2};
    require(detail::align_damage_outward(one_percent, 1.0) ==
                Rect{0.0, 0.0, 900.0, 7.0},
            "fractional 1x damage must cover the final device pixel");
    require(detail::align_damage_outward(one_percent, 2.0) ==
                Rect{0.0, 0.0, 900.0, 6.5},
            "fractional 2x damage must remain exact in logical coordinates");
    require(detail::align_damage_outward({-0.2, -0.2, 1.0, 1.0}, 2.0) ==
                Rect{-0.5, -0.5, 1.5, 1.5},
            "outward alignment must floor negative leading edges and ceil trailing edges");
    require(detail::align_damage_outward(one_percent, 0.0).empty(),
            "invalid scale must not produce a damage rectangle");
}

void test_idle_remains_quiescent_after_compacted_damage() {
    TreeFixture fixture;
    for (int index = 0; index < 20; ++index) {
        (*fixture.left_leaf).invalidate(invalidation::paint_only);
    }
    fixture.paint_pending();
    (*fixture.window).reset_activity_metrics();
    require(!(*fixture.window).needs_frame() && !(*fixture.window).next_wake().has_value(),
            "painted retained damage must return to frame-and-wake quiescence");
    const MetricsSnapshot idle = (*fixture.window).metrics_snapshot();
    require(idle.measure_passes == 0 && idle.arrange_passes == 0 &&
                idle.paint_passes == 0 && idle.paint_invalidations_consumed == 0,
            "idle snapshot must report no latent layout or paint work");
}

void test_exclusive_paint_lease_defers_reentry_and_tracks_release() {
    std::shared_ptr<TransactionPaintControl> root =
        make_control<TransactionPaintControl>(StableId("paint.lease"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).request_nested_paint = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    const DamageRegion damage = window.take_damage();
    window.paint(painter, damage.bounds());

    PaintLeaseSnapshot snapshot = window.paint_lease_snapshot();
    require(snapshot.leases_started == 1U && snapshot.leases_completed == 1U &&
                snapshot.reentrant_requests_deferred == 1U &&
                snapshot.state == PaintLeaseState::dirty_queued && window.needs_frame(),
            "a nested paint request must defer one later pass without recursive callbacks");
    require((*root).paint_calls == 1U,
            "exclusive paint lease must invoke application paint only once");

    const DamageRegion deferred = window.take_damage();
    window.paint(painter, deferred.bounds());
    snapshot = window.paint_lease_snapshot();
    require(snapshot.state == PaintLeaseState::ready &&
                snapshot.rendered_revision == snapshot.content_revision,
            "a coherent completed lease must become ready before presentation");
    window.notify_presented(25U);
    snapshot = window.paint_lease_snapshot();
    require(snapshot.state == PaintLeaseState::clean &&
                snapshot.presented_revision == snapshot.rendered_revision,
            "host release must advance only the completely rendered revision");
}

void test_paint_touch_during_render_survives_current_lease() {
    std::shared_ptr<TransactionPaintControl> root =
        make_control<TransactionPaintControl>(StableId("paint.touch"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).invalidate_during_paint = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    const DamageRegion damage = window.take_damage();
    window.paint(painter, damage.bounds());
    const PaintLeaseSnapshot snapshot = window.paint_lease_snapshot();
    require((*root).state_after_touch == PaintLeaseState::rendering_dirty &&
                snapshot.dirty_after_render &&
                snapshot.state == PaintLeaseState::dirty_queued &&
                snapshot.content_revision > snapshot.rendered_revision &&
                window.needs_frame(),
            "touch during paint must expose rendering-dirty and survive as one later revision");
}

void test_failed_candidate_preserves_damage_and_never_replays_partial_commands() {
    std::shared_ptr<TransactionPaintControl> root =
        make_control<TransactionPaintControl>(StableId("paint.failure"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).throw_during_paint = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    const DamageRegion initial = window.take_damage();
    bool failed = false;
    try {
        window.paint(painter, initial.bounds());
    } catch (const std::runtime_error&) {
        failed = true;
    }
    PaintLeaseSnapshot snapshot = window.paint_lease_snapshot();
    require(failed && painter.draws == 0U && snapshot.leases_abandoned == 1U &&
                snapshot.state == PaintLeaseState::dirty_queued && window.needs_frame(),
            "failed owner paint must abandon the candidate before host replay and retain damage");

    const DamageRegion retry = window.take_damage();
    require(!retry.empty(), "abandoned paint must republish damage for retry");
    window.paint(painter, retry.bounds());
    require(painter.draws > 0U &&
                window.paint_lease_snapshot().leases_completed == 1U,
            "a later valid lease must replace the abandoned candidate");
}

// The Window reuses its paint-pass checkpoints from pass to pass; a pass,
// completed or abandoned, must not keep a removed control alive.
void test_paint_pass_releases_its_checkpoints() {
    std::shared_ptr<TransactionPaintControl> root =
        make_control<TransactionPaintControl>(StableId("paint.checkpoints"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    std::shared_ptr<TransactionPaintControl> child =
        make_control<TransactionPaintControl>(StableId("paint.checkpoints.child"));
    (*child).set_requested_bounds({10.0, 10.0, 40.0, 30.0});
    (*root).add_child(child);
    std::shared_ptr<TransactionPaintControl> failing =
        make_control<TransactionPaintControl>(StableId("paint.checkpoints.failing"));
    (*failing).set_requested_bounds({60.0, 10.0, 40.0, 30.0});
    (*root).add_child(failing);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    (*failing).throw_during_paint = true;
    bool failed = false;
    try {
        window.paint(painter, window.take_damage().bounds());
    } catch (const std::runtime_error&) {
        failed = true;
    }
    const RuntimeId failing_id = (*failing).runtime_id();
    const std::weak_ptr<TransactionPaintControl> failing_observer = failing;
    failing.reset();
    static_cast<void>((*root).remove_child(failing_id));
    require(failed && failing_observer.expired(),
            "an abandoned paint pass must release the controls it checkpointed");

    window.paint(painter, window.take_damage().bounds());
    const RuntimeId child_id = (*child).runtime_id();
    const std::weak_ptr<TransactionPaintControl> child_observer = child;
    child.reset();
    static_cast<void>((*root).remove_child(child_id));
    require(window.paint_lease_snapshot().leases_completed == 1U && child_observer.expired(),
            "a completed paint pass must release the controls it checkpointed");
}

void test_retained_input_waits_for_lease_and_compacts_move_pressure() {
    std::shared_ptr<DeferredInputPaintControl> root =
        make_control<DeferredInputPaintControl>(StableId("paint.input"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).focusable = true;
    (*root).inject_pressure_during_next_paint = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    const DamageRegion initial = window.take_damage();
    window.paint(painter, initial.bounds());

    const DeferredInputSnapshot pending = window.deferred_input_snapshot();
    require((*root).all_ingress_accepted && (*root).delivered.empty() &&
                !(*root).callback_during_paint &&
                (*root).during_lease.deferred == 103U &&
                (*root).during_lease.coalesced_moves == 99U &&
                (*root).during_lease.pending == 4U &&
                pending.pending == 4U && pending.drain_queued &&
                window.dispatcher_snapshot().pending == 1U,
            "paint-time input must compact obsolete moves and post one bounded drain");

    const DispatchDrainResult drain = window.drain_posted_work();
    const DeferredInputSnapshot delivered = window.deferred_input_snapshot();
    require(drain.invoked == 1U && drain.remaining == 0U &&
                (*root).delivered ==
                    std::vector<std::string>{"enter", "move", "down", "key", "text"} &&
                !(*root).callback_during_paint &&
                (*root).last_move.x > 29.89 && (*root).last_move.x < 29.91 &&
                (*root).last_move.y == 20.0 &&
                delivered.pending == 0U && delivered.delivered == 4U &&
                delivered.drains == 1U && !delivered.drain_queued &&
                delivered.rejected_capacity == 0U &&
                window.needs_frame(),
            "deferred input must preserve critical ordering outside paint and keep only the latest adjacent move");

    const DamageRegion follow_up = window.take_damage();
    window.paint(painter, follow_up.bounds());
    require((*root).paint_calls == 2U &&
                window.deferred_input_snapshot().pending == 0U &&
                window.dispatcher_snapshot().pending == 0U,
            "input mutation must produce one ordinary later paint without a catch-up queue");
}

void test_retained_input_is_abandoned_when_paint_owner_retires() {
    std::shared_ptr<DeferredInputPaintControl> root =
        make_control<DeferredInputPaintControl>(StableId("paint.input.dispose"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).focusable = true;
    (*root).dispose_during_next_paint = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    const DamageRegion initial = window.take_damage();
    window.paint(painter, initial.bounds());

    const DeferredInputSnapshot snapshot = window.deferred_input_snapshot();
    require(!(*root).is_alive() &&
                (*root).ingress_accepted == maximum_deferred_inputs &&
                !(*root).all_ingress_accepted &&
                (*root).during_lease.pending == maximum_deferred_inputs &&
                (*root).during_lease.rejected_capacity == 1U &&
                (*root).delivered.empty() && snapshot.pending == 0U &&
                snapshot.abandoned == maximum_deferred_inputs &&
                snapshot.rejected_capacity == 1U && !snapshot.drain_queued &&
                window.dispatcher_snapshot().pending == 0U &&
                window.paint_lease_snapshot().leases_abandoned == 1U,
            "the input queue must stay bounded and abandon its retained events when the paint root retires");
}

void test_retained_input_fault_does_not_drop_later_events() {
    std::shared_ptr<DeferredInputPaintControl> root =
        make_control<DeferredInputPaintControl>(StableId("paint.input.fault"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).focusable = true;
    (*root).inject_pressure_during_next_paint = true;
    (*root).throw_on_key = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    const DamageRegion initial = window.take_damage();
    window.paint(painter, initial.bounds());

    const DispatchDrainResult drain = window.drain_posted_work();
    const DeferredInputSnapshot snapshot = window.deferred_input_snapshot();
    require(drain.faulted == 1U && drain.remaining == 0U &&
                (*root).delivered ==
                    std::vector<std::string>{"enter", "move", "down", "key", "text"} &&
                snapshot.pending == 0U && snapshot.delivered == 3U &&
                snapshot.faults == 1U && snapshot.drains == 1U &&
                !snapshot.drain_queued,
            "one deferred input fault must be reported without dropping later retained input");
}

void test_semantic_action_waits_for_paint_lease_release() {
    std::shared_ptr<DeferredInputPaintControl> root =
        make_control<DeferredInputPaintControl>(StableId("paint.semantic"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).inject_semantic_during_next_paint = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    const DamageRegion initial = window.take_damage();
    window.paint(painter, initial.bounds());

    const DeferredInputSnapshot pending = window.deferred_input_snapshot();
    require((*root).semantic_retained && (*root).semantic_actions == 0U &&
                !(*root).callback_during_paint &&
                (*root).during_lease.pending == 1U &&
                pending.pending == 1U && pending.drain_queued,
            "semantic action must retain its stable identity without entering paint");
    const DispatchDrainResult drain = window.drain_posted_work();
    require(drain.invoked == 1U && drain.remaining == 0U &&
                (*root).semantic_actions == 1U &&
                !(*root).callback_during_paint &&
                window.deferred_input_snapshot().pending == 0U &&
                window.needs_frame(),
            "semantic action must resolve once after release and use ordinary invalidation");
    const DamageRegion follow_up = window.take_damage();
    window.paint(painter, follow_up.bounds());
    require((*root).paint_calls == 2U &&
                window.dispatcher_snapshot().pending == 0U,
            "semantic mutation must produce one later paint without residual work");
}

void test_slow_replay_pressure_coalesces_to_latest_state_and_exact_receipt() {
    std::shared_ptr<ReplayPressureControl> root =
        make_control<ReplayPressureControl>(StableId("paint.pressure"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).focusable = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    std::uint64_t wake_calls{};
    window.set_paint_wake_handler(
        test_support::IncrementCounter<std::uint64_t>(wake_calls));
    const DamageRegion initial = window.take_damage();
    const std::uint64_t wake_calls_before_pressure = wake_calls;

    ReplayBoundaryPainter slow;
    slow.on_first_command = ApplyReplayPressure(*root, window, slow);
    const std::optional<PaintReceipt> first =
        window.paint(slow, initial.bounds());
    const PaintLeaseSnapshot pressured = window.paint_lease_snapshot();
    const DeferredInputSnapshot pending_input = window.deferred_input_snapshot();
    require(first.has_value() && (*root).last_painted_value == 0U &&
                pressured.content_revision > (*first).rendered_revision &&
                pressured.rendered_revision == (*first).rendered_revision &&
                pressured.leases_started == 1U &&
                pressured.leases_completed == 1U &&
                pressured.reentrant_requests_deferred == 8U &&
                pressured.render_wake_queued &&
                wake_calls == wake_calls_before_pressure + 1U &&
                pressured.render_wakes_coalesced >= 99U &&
                pending_input.pending == 1U && pending_input.drain_queued &&
                (*root).pointer_calls == 0U && !(*root).callback_during_lease,
            "slow replay pressure must retain one coherent old receipt, one wake, and one deferred input drain");

    const DispatchDrainResult input_drain = window.drain_posted_work();
    require(input_drain.invoked == 1U && input_drain.remaining == 0U &&
                (*root).pointer_calls == 1U && !(*root).callback_during_lease,
            "replay-boundary input must deliver once only after lease release");

    const DamageRegion latest_damage = window.take_damage();
    NullPainter latest_painter;
    const std::optional<PaintReceipt> latest =
        window.paint(latest_painter, latest_damage.bounds());
    require(latest.has_value() && (*root).last_painted_value == 100U &&
                (*latest).rendered_revision ==
                    window.paint_lease_snapshot().content_revision,
            "one follow-up must render the latest state without intermediate jobs");
    require(window.notify_presented(*latest, 50U) &&
                !window.notify_presented(*first, 500U),
            "an out-of-order slow-presenter receipt must not move presentation backward");
    const PaintLeaseSnapshot idle = window.paint_lease_snapshot();
    require(idle.state == PaintLeaseState::clean &&
                idle.presentation_receipts_accepted == 1U &&
                idle.presentation_receipts_rejected == 1U &&
                !idle.render_wake_queued && !window.needs_frame() &&
                window.dispatcher_snapshot().pending == 0U &&
                window.deferred_input_snapshot().pending == 0U,
            "pressure drain must return to zero retained paint and input work");
}

void test_resize_during_backend_replay_withholds_stale_receipt() {
    std::shared_ptr<ReplayPressureControl> root =
        make_control<ReplayPressureControl>(StableId("paint.epoch"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter initial_painter;
    const DamageRegion initial_damage = window.take_damage();
    const std::optional<PaintReceipt> initial = window.paint(initial_painter, initial_damage.bounds());
    require(initial.has_value() && window.notify_presented(*initial),
            "baseline surface must present before the stale-epoch probe");

    (*root).set_value(1U);
    const DamageRegion damage = window.take_damage();
    ReplayBoundaryPainter resizing;
    resizing.on_first_command = ResizeDuringReplay(*root, window);
    const std::optional<PaintReceipt> stale = window.paint(resizing, damage.bounds());
    const PaintLeaseSnapshot abandoned = window.paint_lease_snapshot();
    require(!stale.has_value() && abandoned.leases_abandoned == 1U &&
                abandoned.presented_revision == (*initial).rendered_revision &&
                abandoned.surface_epoch != (*initial).surface_epoch &&
                !window.notify_presented(*initial),
            "a resize entered by backend replay must withhold the stale receipt and preserve the last presentation revision");

    const DamageRegion replacement_damage = window.take_damage();
    NullPainter replacement_painter;
    const std::optional<PaintReceipt> replacement =
        window.paint(replacement_painter, replacement_damage.bounds());
    require(replacement.has_value() && (*root).last_painted_value == 2U &&
                (*replacement).surface_epoch == abandoned.surface_epoch &&
                window.notify_presented(*replacement) && !window.needs_frame(),
            "the replacement epoch must render and release only the latest state");
}

void test_occluded_mutation_storm_has_one_exposure_wake() {
    std::shared_ptr<ReplayPressureControl> root =
        make_control<ReplayPressureControl>(StableId("paint.occlusion"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{120.0, 80.0});
    Window& window = *held_window;
    NullPainter painter;
    const std::optional<PaintReceipt> initial = window.paint(painter, window.take_damage().bounds());
    require(initial.has_value() && window.notify_presented(*initial),
            "occlusion probe requires a clean baseline surface");
    std::uint64_t wakes{};
    window.set_paint_wake_handler(
        test_support::IncrementCounter<std::uint64_t>(wakes));
    const FrameTime transition = FrameClock::now();
    window.set_occluded(true, transition);
    for (std::uint32_t revision = 1U; revision <= 100U; ++revision) {
        (*root).set_value(revision);
    }
    const std::uint64_t paint_calls_before = (*root).paint_calls;
    require(window.occluded() && wakes == 0U && window.needs_frame() &&
                window.paint_lease_snapshot().state ==
                    PaintLeaseState::occluded_dirty &&
                !window.paint(painter).has_value() &&
                (*root).paint_calls == paint_calls_before,
            "occlusion must merge a mutation storm without waking or painting");

    window.set_occluded(false, transition + std::chrono::milliseconds(10));
    require(wakes == 1U &&
                window.paint_lease_snapshot().render_wake_queued,
            "exposure must request exactly one latest-state render");
    const std::optional<PaintReceipt> exposed = window.paint(painter, window.take_damage().bounds());
    require(exposed.has_value() && (*root).last_painted_value == 100U &&
                (*root).paint_calls == paint_calls_before + 1U &&
                window.notify_presented(*exposed) && !window.needs_frame() &&
                !window.paint_lease_snapshot().render_wake_queued,
            "exposure must render one latest revision and return idle");
}

void test_backend_replay_fault_and_retirement_never_issue_receipt() {
    std::shared_ptr<ReplayPressureControl> fault_root =
        make_control<ReplayPressureControl>(StableId("paint.backend-fault"));
    (*fault_root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    const std::unique_ptr<Window> held_fault_window = std::make_unique<Window>(fault_root, Size{120.0, 80.0});
    Window& fault_window = *held_fault_window;
    const DamageRegion fault_damage = fault_window.take_damage();
    ReplayBoundaryPainter faulting;
    faulting.throw_after_boundary = true;
    bool threw{};
    try {
        static_cast<void>(fault_window.paint(faulting, fault_damage.bounds()));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw && fault_window.paint_lease_snapshot().leases_abandoned == 1U &&
                fault_window.paint_lease_snapshot().rendered_revision == 0U &&
                fault_window.needs_frame(),
            "a backend replay fault must abandon without a complete revision");
    NullPainter retry_painter;
    const std::optional<PaintReceipt> retry =
        fault_window.paint(retry_painter, fault_window.take_damage().bounds());
    require(retry.has_value() && fault_window.notify_presented(*retry),
            "a later clean replay must replace the failed candidate");

    std::shared_ptr<ReplayPressureControl> retiring_root =
        make_control<ReplayPressureControl>(StableId("paint.backend-retire"));
    (*retiring_root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    const std::unique_ptr<Window> held_retiring_window = std::make_unique<Window>(retiring_root, Size{120.0, 80.0});
    Window& retiring_window = *held_retiring_window;
    const DamageRegion retiring_damage = retiring_window.take_damage();
    ReplayBoundaryPainter retiring;
    retiring.on_first_command =
        RetireDuringReplay(*retiring_root, retiring_window);
    const std::optional<PaintReceipt> retired =
        retiring_window.paint(retiring, retiring_damage.bounds());
    const DeferredInputSnapshot retired_input =
        retiring_window.deferred_input_snapshot();
    require(!retired.has_value() && !(*retiring_root).is_alive() &&
                retired_input.pending == 0U && retired_input.abandoned == 1U &&
                (*retiring_root).key_calls == 0U &&
                retiring_window.paint_lease_snapshot().leases_abandoned == 1U,
            "backend-boundary retirement must withhold presentation and abandon queued input");
}

void test_buffering_styles_are_retained_compatibility_facts() {
    std::shared_ptr<gui_forms::Control> control = make_control<Control>(StableId("styles.buffering"));
    require(!(*control).double_buffered.read() && (*control).has_style(ControlStyles::resize_redraw),
            "coherent baseline and historical buffering request must be independent");
    bool unknown_rejected = false;
    try {
        (*control).set_style(static_cast<ControlStyles>(1U << 2U), true);
    } catch (const std::invalid_argument&) {
        unknown_rejected = true;
    }
    require(unknown_rejected, "a removed Windows Forms style bit is rejected as unknown (audit A17)");
    (*control).double_buffered = true;
    require((*control).double_buffered.read() &&
                (*control).has_style(ControlStyles::double_buffer) &&
                (*control).has_style(ControlStyles::optimized_double_buffer),
            "DoubleBuffered must retain both compatible buffering bits");
    (*control).set_style(ControlStyles::resize_redraw, true);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(control, Size{40.0, 30.0});
    Window& window = *held_window;
    require((*control).double_buffered.read() &&
                (*control).has_style(ControlStyles::resize_redraw),
            "control styles must survive attachment and handle-equivalent lifetime");
    (*control).double_buffered = false;
    require(!(*control).double_buffered.read(),
            "clearing a compatibility request must not alter baseline paint safety");
}

} // namespace

int main() {
    try {
        test_typed_effect_vocabulary();
        test_affected_path_layout();
        test_full_subtree_invalidation_is_explicit();
        test_damage_take_commits_layout_generated_geometry_damage();
        test_reentrant_layout_is_bounded();
        test_paint_metrics_report_chunk_work();
        test_declared_mutation_guard();
        test_damage_compaction_and_complexity_guard();
        test_device_damage_alignment_is_outward_and_scale_exact();
        test_idle_remains_quiescent_after_compacted_damage();
        test_exclusive_paint_lease_defers_reentry_and_tracks_release();
        test_paint_touch_during_render_survives_current_lease();
        test_failed_candidate_preserves_damage_and_never_replays_partial_commands();
        test_paint_pass_releases_its_checkpoints();
        test_retained_input_waits_for_lease_and_compacts_move_pressure();
        test_retained_input_is_abandoned_when_paint_owner_retires();
        test_retained_input_fault_does_not_drop_later_events();
        test_semantic_action_waits_for_paint_lease_release();
        test_slow_replay_pressure_coalesces_to_latest_state_and_exact_receipt();
        test_resize_during_backend_replay_withholds_stale_receipt();
        test_occluded_mutation_storm_has_one_exposure_wake();
        test_backend_replay_fault_and_retirement_never_issue_receipt();
        test_buffering_styles_are_retained_compatibility_facts();
        test_hovered_control_is_hot();
        test_hover_runs_no_layout_pass();
        std::cout << "gui_forms_invalidation_damage_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_invalidation_damage_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
