#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

class RecordingPainter final : public Painter {
public:
    void save() override { ++save_count; }
    void restore() override { ++restore_count; }
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color color) override {
        ++draw_count;
        fill_colors.push_back(color);
    }
    void stroke_rect(Rect, Color, double) override { ++draw_count; }
    void draw_line(Point, Point, Color, double) override { ++draw_count; }
    void draw_text_utf8(Point, std::string_view, FontSpec font, Color) override {
        ++draw_count;
        last_font = font;
    }
    void draw_image(ImageId, Rect, double) override { ++draw_count; }

    std::uint64_t save_count{};
    std::uint64_t restore_count{};
    std::uint64_t draw_count{};
    std::optional<FontSpec> last_font;
    std::vector<Color> fill_colors;
};

class ProbeControl : public Control {
public:
    explicit ProbeControl(StableId id) : Control(std::move(id)) {}

    Size measure(Size available) override {
        ++measure_count;
        return Control::measure(available);
    }

    void arrange(Rect bounds) override {
        ++arrange_count;
        Control::arrange(bounds);
    }

    void on_paint(Painter& painter, Rect damage) override {
        ++paint_count;
        painter.fill_rect(damage, Color::rgba(10, 20, 30));
    }

    void on_pointer_preview(PointerEvent& event) override {
        phases.push_back(event.phase);
        if (handle_preview_release && event.action == PointerAction::up) {
            event.handled = true;
        }
    }

    void on_pointer(PointerEvent& event) override {
        phases.push_back(event.phase);
        if (event.action == PointerAction::up && release_callback) {
            release_callback();
        }
    }

    void on_pointer_bubble(PointerEvent& event) override {
        phases.push_back(event.phase);
    }

    void on_focus_changed(bool focused) override {
        focus_state = focused;
    }

    void on_key(KeyEvent& event) override {
        if (handle_key) event.handled = true;
    }

    void on_activate() override {
        ++activation_count;
        invalidate(Dirty::paint);
    }

    std::uint64_t measure_count{};
    std::uint64_t arrange_count{};
    std::uint64_t paint_count{};
    std::uint64_t activation_count{};
    bool focus_state{};
    bool handle_preview_release{};
    bool handle_key{};
    std::function<void()> release_callback;
    std::vector<EventPhase> phases;
};

class FaultingLayoutControl final : public Control {
public:
    explicit FaultingLayoutControl(StableId id) : Control(std::move(id)) {}

    void arrange(Rect bounds) override {
        ++arrange_attempts;
        if (throw_next_arrange) {
            throw_next_arrange = false;
            throw std::runtime_error("intentional layout fault");
        }
        Control::arrange(bounds);
    }

    bool throw_next_arrange{};
    std::uint64_t arrange_attempts{};
};

class MutatingLayoutControl final : public Control {
public:
    explicit MutatingLayoutControl(StableId id) : Control(std::move(id)) {}

    Size measure(Size available) override {
        ++measure_count;
        if (measure_callback) measure_callback();
        return is_alive() ? Control::measure(available) : Size{};
    }

    void arrange(Rect bounds) override {
        ++arrange_count;
        if (arrange_callback) arrange_callback();
        if (is_alive()) Control::arrange(bounds);
    }

    std::function<void()> measure_callback;
    std::function<void()> arrange_callback;
    std::uint64_t measure_count{};
    std::uint64_t arrange_count{};
};

class CallbackArbitrationControl final : public Control {
public:
    explicit CallbackArbitrationControl(StableId id)
        : Control(std::move(id)) {}

    void on_paint(Painter& painter, Rect damage) override {
        ++paint_count;
        if (paint_callback) paint_callback();
        if (is_alive()) painter.fill_rect(damage, Color::rgba(30, 60, 90));
    }

    void on_paint_overlay(Painter&, Rect) override {
        ++paint_overlay_count;
        if (paint_overlay_callback) paint_overlay_callback();
    }

    bool hit_test_local(Point) const override {
        ++hit_test_count;
        if (hit_test_callback) hit_test_callback();
        return true;
    }

    SemanticDescriptor semantic_descriptor() const override {
        ++semantic_count;
        if (semantic_callback) semantic_callback();
        if (!is_alive()) return {};
        SemanticDescriptor descriptor = Control::semantic_descriptor();
        descriptor.exposed = !descriptor.name.empty() ||
                             !descriptor.description.empty();
        return descriptor;
    }

    std::function<void()> paint_callback;
    std::function<void()> paint_overlay_callback;
    mutable std::function<void()> hit_test_callback;
    mutable std::function<void()> semantic_callback;
    std::uint64_t paint_count{};
    std::uint64_t paint_overlay_count{};
    mutable std::uint64_t hit_test_count{};
    mutable std::uint64_t semantic_count{};
};

class FontProbe final : public Control {
public:
    explicit FontProbe(StableId id) : Control(std::move(id)) {}

    [[nodiscard]] FontSpec font() const noexcept { return authored_font_; }
    Size measure(Size available) override {
        const FontSpec resolved = effective_font(authored_font_);
        return {std::min(available.width, resolved.size * 8.0),
                std::min(available.height, resolved.size * 1.4)};
    }
    void on_paint(Painter& painter, Rect) override {
        painter.draw_text_utf8({0.0, effective_font(authored_font_).size},
                               "Scale me", effective_font(authored_font_),
                               Color::rgba(0, 0, 0));
    }

private:
    FontSpec authored_font_{FontRole::content, 12.0, 400, false};
};

class RemoveChildOnce final {
public:
    RemoveChildOnce(Control& parent, Control& child, bool& invoked) noexcept
        : parent_(parent), child_(child), invoked_(invoked) {}

    void operator()() const {
        if (invoked_) return;
        invoked_ = true;
        static_cast<void>(parent_.remove_child(child_.runtime_id()));
    }

private:
    Control& parent_;
    Control& child_;
    bool& invoked_;
};

class AttachChildOnce final {
public:
    AttachChildOnce(Control& parent, Control::Ptr child,
                    bool& invoked) noexcept
        : parent_(parent), child_(std::move(child)), invoked_(invoked) {}

    void operator()() const {
        if (invoked_) return;
        invoked_ = true;
        parent_.add_child(child_);
    }

private:
    Control& parent_;
    Control::Ptr child_;
    bool& invoked_;
};

// Disposes its control from inside a callback the control itself stores.
// The stored callback observes the control; holding it strongly made every
// use an ownership cycle that leaked the control (LeakSanitizer, 2026-10-11).
// The call locks it, so the control is still strongly held while the
// callback runs.
class DisposeCapturedControl final {
public:
    explicit DisposeCapturedControl(const Control::Ptr& control) noexcept
        : control_(control) {}

    void operator()() const {
        const Control::Ptr control = control_.lock();
        if (control && (*control).is_alive()) (*control).dispose();
    }

private:
    std::weak_ptr<Control> control_;
};

class ReplaceChildOnce final {
public:
    ReplaceChildOnce(Control& parent, Control& removed,
                     Control::Ptr added, bool& invoked) noexcept
        : parent_(parent), removed_(removed), added_(std::move(added)), invoked_(invoked) {}

    void operator()() const {
        if (invoked_) return;
        invoked_ = true;
        static_cast<void>(parent_.remove_child(removed_.runtime_id()));
        parent_.add_child(added_);
    }

private:
    Control& parent_;
    Control& removed_;
    Control::Ptr added_;
    bool& invoked_;
};

class AlternateAccessibleName final {
public:
    AlternateAccessibleName(Control& control, bool& alternate) noexcept
        : control_(control), alternate_(alternate) {}

    void operator()() const {
        alternate_ = !alternate_;
        control_.accessible_name = alternate_ ? "Semantic oscillation A" : "Semantic oscillation B";
    }

private:
    Control& control_;
    bool& alternate_;
};

class RemoveChildDuringValidation final {
public:
    RemoveChildDuringValidation(Control& parent, Control& child) noexcept
        : parent_(parent), child_(child) {}

    void operator()(ControlValidationEvent&) const {
        static_cast<void>(parent_.remove_child(child_.runtime_id()));
    }

private:
    Control& parent_;
    Control& child_;
};

template <typename Argument>
class CountCalls final {
public:
    explicit CountCalls(std::size_t& count) noexcept : count_(count) {}

    void operator()(Argument) const { ++count_; }

private:
    std::size_t& count_;
};

class CountTrueValues final {
public:
    explicit CountTrueValues(std::size_t& count) noexcept : count_(count) {}

    void operator()(bool value) const {
        if (value) ++count_;
    }

private:
    std::size_t& count_;
};

template <typename Value>
class RecordValue final {
public:
    explicit RecordValue(Value& value) noexcept : value_(value) {}

    void operator()(const Value& value) const { value_ = value; }

private:
    Value& value_;
};

class CountSuccessfulCommand final {
public:
    explicit CountSuccessfulCommand(std::size_t& count) noexcept : count_(count) {}

    bool operator()() const {
        ++count_;
        return true;
    }

private:
    std::size_t& count_;
};

class CountWakes final {
public:
    explicit CountWakes(unsigned& count) noexcept : count_(count) {}
    void operator()() const { ++count_; }

private:
    unsigned& count_;
};

class ObserveReleasedCapture final {
public:
    ObserveReleasedCapture(Window& window, bool& observed) noexcept
        : window_(window), observed_(observed) {}

    void operator()() const { observed_ = !window_.captured_control(); }

private:
    Window& window_;
    bool& observed_;
};

class SequenceClock final {
public:
    SequenceClock(const std::vector<std::uint64_t>& values,
                  std::size_t& cursor) noexcept
        : values_(values), cursor_(cursor) {}

    std::uint64_t operator()() const { return values_[cursor_++]; }

private:
    const std::vector<std::uint64_t>& values_;
    std::size_t& cursor_;
};

Control::Ptr make_probe_control(StableId id) {
    return make_control<ProbeControl>(std::move(id));
}

void attempt_text_scale_on_worker(Window& window, bool& rejected) noexcept {
    try {
        window.set_text_scale(1.25);
    } catch (...) {
        rejected = true;
    }
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void paint_pending(Window& window, RecordingPainter& painter) {
    DamageRegion damage = window.take_damage();
    if (!damage.empty()) {
        window.paint(painter, damage.bounds());
    }
}

struct Fixture {
    Fixture() {
        (*root).set_requested_bounds({0.0, 0.0, 200.0, 120.0});
        (*child).set_requested_bounds({10.0, 10.0, 40.0, 30.0});
        (*child).focusable = true;
        (*root).add_child(child);
        window = std::make_unique<Window>(root, Size{200.0, 120.0});
        (*window).perform_layout();
        paint_pending(*window, painter);
        (*window).reset_activity_metrics();
    }

    std::shared_ptr<ProbeControl> root = make_control<ProbeControl>(StableId("root"));
    std::shared_ptr<ProbeControl> child = make_control<ProbeControl>(StableId("child"));
    std::unique_ptr<Window> window;
    RecordingPainter painter;
};

void test_nested_scopes_and_read_barrier() {
    Fixture fixture;
    const Rect old_bounds = (*fixture.child).committed_arranged_bounds();
    {
        UpdateScope outer = (*fixture.window).begin_update();
        (*fixture.child).set_requested_bounds({20.0, 12.0, 44.0, 30.0});
        {
            UpdateScope inner = (*fixture.window).begin_update();
            (*fixture.child).set_requested_bounds({30.0, 14.0, 48.0, 32.0});
            require((*fixture.child).arranged_bounds() == old_bounds,
                    "arranged read inside update scope must return committed geometry");
        }
        require((*fixture.window).metrics_snapshot().arrange_passes == 0,
                "inner update scope must not flush layout");
    }
    const MetricsSnapshot snapshot = (*fixture.window).metrics_snapshot();
    require((*fixture.child).arranged_bounds() == Rect{30.0, 14.0, 48.0, 32.0},
            "outer update close must commit final requested geometry");
    require(snapshot.update_scopes_started == 2, "both update scopes must be counted");
    require(snapshot.maximum_update_scope_depth == 2, "maximum nesting depth must be structured");
    require(snapshot.arrange_passes == 1, "nested mutations must coalesce to one arrange pass");
    require(snapshot.flush_count == 1, "nested mutations must coalesce to one flush");
}

void test_per_control_layout_transactions() {
    Fixture fixture;
    const Rect child_committed = (*fixture.child).committed_arranged_bounds();
    std::shared_ptr<ProbeControl> sibling =
        make_control<ProbeControl>(StableId("layout-sibling"));
    (*sibling).set_requested_bounds({90.0, 10.0, 30.0, 20.0});
    (*fixture.root).add_child(sibling);
    (*fixture.window).perform_layout();
    (*fixture.window).reset_activity_metrics();

    (*fixture.child).suspend_layout();
    (*fixture.child).suspend_layout();
    (*fixture.child).set_requested_bounds({24.0, 18.0, 58.0, 36.0});
    (*sibling).set_requested_bounds({104.0, 16.0, 34.0, 22.0});
    (*fixture.window).perform_layout();

    require((*fixture.child).arranged_bounds() == child_committed,
            "a suspended control must expose its last committed geometry");
    require((*sibling).arranged_bounds() == Rect{104.0, 16.0, 34.0, 22.0},
            "a suspended subtree must not block runnable sibling layout");
    LayoutTransactionState state = (*fixture.child).layout_transaction_state();
    require(state.suspend_depth == 2U && state.deferred &&
                state.requested_revision > state.committed_revision,
            "nested suspension must retain an observable deferred request");

    (*fixture.child).resume_layout(true);
    require((*fixture.child).committed_arranged_bounds() == child_committed,
            "an inner resume must not commit a nested transaction");
    (*fixture.child).resume_layout(false);
    require((*fixture.child).committed_arranged_bounds() == child_committed,
            "ResumeLayout(false) must preserve committed geometry");

    require((*fixture.child).arranged_bounds() == Rect{24.0, 18.0, 58.0, 36.0},
            "the first read outside suspension must minimally flush geometry");
    state = (*fixture.child).layout_transaction_state();
    require(state.suspend_depth == 0U && !state.deferred &&
                state.committed_revision == state.requested_revision,
            "a completed deferred layout must publish its committed revision");

    (*fixture.child).suspend_layout();
    (*fixture.child).perform_layout();
    (*fixture.child).resume_layout(true);
    state = (*fixture.child).layout_transaction_state();
    require(!state.deferred &&
                state.committed_revision == state.requested_revision,
            "final ResumeLayout(true) must flush an explicit pending layout once");
    (*fixture.child).resume_layout(true);
    require((*fixture.child).layout_transaction_state().suspend_depth == 0U,
            "an unmatched resume must remain a harmless no-op");
}

void test_layout_fault_releases_reentry_guard() {
    std::shared_ptr<ProbeControl> root =
        make_control<ProbeControl>(StableId("fault-root"));
    std::shared_ptr<FaultingLayoutControl> child =
        make_control<FaultingLayoutControl>(StableId("fault-child"));
    (*root).set_requested_bounds({0.0, 0.0, 200.0, 120.0});
    (*child).set_requested_bounds({12.0, 10.0, 40.0, 24.0});
    (*root).add_child(child);
    Window window(root, {200.0, 120.0});
    window.perform_layout();

    (*child).throw_next_arrange = true;
    (*child).set_requested_bounds({20.0, 18.0, 52.0, 30.0});
    bool fault_observed = false;
    try {
        window.perform_layout();
    } catch (const std::runtime_error&) {
        fault_observed = true;
    }
    require(fault_observed &&
                (*child).committed_arranged_bounds() == Rect{12.0, 10.0, 40.0, 24.0},
            "a failing layout pass must preserve the last committed geometry");
    window.perform_layout();
    require((*child).arranged_bounds() == Rect{20.0, 18.0, 52.0, 30.0} &&
                (*child).arrange_attempts >= 3U,
            "a layout fault must preserve dirty state and release the re-entry guard");
}

void test_layout_callbacks_may_mutate_retained_tree() {
    {
        std::shared_ptr<ProbeControl> root =
            make_control<ProbeControl>(StableId("mutation.remove.root"));
        std::shared_ptr<MutatingLayoutControl> mutator =
            make_control<MutatingLayoutControl>(
            StableId("mutation.remove.mutator"));
        std::shared_ptr<ProbeControl> removed = make_control<ProbeControl>(
            StableId("mutation.remove.victim"));
        (*root).set_requested_bounds({0.0, 0.0, 240.0, 120.0});
        (*mutator).set_requested_bounds({0.0, 0.0, 80.0, 30.0});
        (*removed).set_requested_bounds({90.0, 0.0, 80.0, 30.0});
        (*root).add_child(mutator);
        (*root).add_child(removed);
        bool removed_once{};
        (*mutator).measure_callback =
            RemoveChildOnce(*root, *removed, removed_once);
        Window window(root, {240.0, 120.0});
        window.perform_layout();
        require(removed_once && !(*removed).attached() && !(*removed).parent() &&
                    (*removed).measure_count == 0U,
                "measure mutation must skip a removed identity from the retained snapshot");
        require(window.metrics_snapshot().bounded_pass_limit_hits == 0U,
                "measure mutation must converge within the bounded scheduler");
    }

    {
        std::shared_ptr<ProbeControl> root =
            make_control<ProbeControl>(StableId("mutation.move.root"));
        std::shared_ptr<ProbeControl> source =
            make_control<ProbeControl>(StableId("mutation.move.source"));
        std::shared_ptr<ProbeControl> destination = make_control<ProbeControl>(
            StableId("mutation.move.destination"));
        std::shared_ptr<MutatingLayoutControl> mutator =
            make_control<MutatingLayoutControl>(
            StableId("mutation.move.mutator"));
        std::shared_ptr<ProbeControl> moved =
            make_control<ProbeControl>(StableId("mutation.move.victim"));
        (*root).set_requested_bounds({0.0, 0.0, 320.0, 180.0});
        (*source).set_requested_bounds({0.0, 0.0, 150.0, 180.0});
        (*destination).set_requested_bounds({160.0, 0.0, 150.0, 180.0});
        (*mutator).set_requested_bounds({0.0, 0.0, 60.0, 30.0});
        (*moved).set_requested_bounds({70.0, 0.0, 60.0, 30.0});
        (*source).add_child(mutator);
        (*source).add_child(moved);
        (*root).add_child(source);
        (*root).add_child(destination);
        bool moved_once{};
        (*mutator).arrange_callback =
            AttachChildOnce(*destination, moved, moved_once);
        Window window(root, {320.0, 180.0});
        window.perform_layout();
        require(moved_once && (*moved).parent() == destination && (*moved).attached() &&
                    (*moved).arrange_count == 1U,
                "arrange mutation must transfer ownership and arrange only under the new parent");
        require(window.metrics_snapshot().bounded_pass_limit_hits == 0U,
                "reparenting during arrange must converge without stale work");
    }

    {
        std::shared_ptr<ProbeControl> root =
            make_control<ProbeControl>(StableId("mutation.dispose.root"));
        std::shared_ptr<MutatingLayoutControl> disposing =
            make_control<MutatingLayoutControl>(
            StableId("mutation.dispose.child"));
        (*root).set_requested_bounds({0.0, 0.0, 160.0, 90.0});
        (*disposing).set_requested_bounds({0.0, 0.0, 80.0, 30.0});
        (*root).add_child(disposing);
        (*disposing).arrange_callback = DisposeCapturedControl(disposing);
        Window window(root, {160.0, 90.0});
        window.perform_layout();
        require(!(*disposing).is_alive() && !(*disposing).attached() &&
                    (*root).children().empty(),
                "self-disposal during arrange must detach before scheduler bookkeeping");
        window.perform_layout();
        require(window.metrics_snapshot().bounded_pass_limit_hits == 0U,
                "layout must remain usable after a callback disposes its target");
    }

    {
        std::shared_ptr<ProbeControl> root = make_control<ProbeControl>(
            StableId("mutation.dispose-parent.root"));
        std::shared_ptr<ProbeControl> parent = make_control<ProbeControl>(
            StableId("mutation.dispose-parent.parent"));
        std::shared_ptr<MutatingLayoutControl> child =
            make_control<MutatingLayoutControl>(
            StableId("mutation.dispose-parent.child"));
        (*root).set_requested_bounds({0.0, 0.0, 180.0, 100.0});
        (*parent).set_requested_bounds({0.0, 0.0, 120.0, 70.0});
        (*child).set_requested_bounds({0.0, 0.0, 60.0, 30.0});
        (*parent).add_child(child);
        (*root).add_child(parent);
        (*child).measure_callback = DisposeCapturedControl(parent);
        Window window(root, {180.0, 100.0});
        window.perform_layout();
        require(!(*parent).is_alive() && !(*child).is_alive() &&
                    (*root).children().empty(),
                "a child callback may dispose its layout owner without post-callback access");
        window.perform_layout();
        require(window.metrics_snapshot().bounded_pass_limit_hits == 0U,
                "ancestor disposal during measure must leave the scheduler converged");
    }

    {
        std::shared_ptr<ProbeControl> root =
            make_control<ProbeControl>(StableId("mutation.add.root"));
        std::shared_ptr<MutatingLayoutControl> mutator =
            make_control<MutatingLayoutControl>(
            StableId("mutation.add.mutator"));
        std::shared_ptr<ProbeControl> added =
            make_control<ProbeControl>(StableId("mutation.add.child"));
        (*root).set_requested_bounds({0.0, 0.0, 180.0, 100.0});
        (*mutator).set_requested_bounds({0.0, 0.0, 70.0, 30.0});
        (*added).set_requested_bounds({80.0, 0.0, 70.0, 30.0});
        (*root).add_child(mutator);
        bool added_once{};
        (*mutator).arrange_callback = AttachChildOnce(*root, added, added_once);
        Window window(root, {180.0, 100.0});
        window.perform_layout();
        require(added_once && (*added).attached() && (*added).parent() == root &&
                    (*added).arrange_count == 1U,
                "a child added during arrange must enter a following bounded pass");
        require(window.metrics_snapshot().arrange_passes >= 2U &&
                    window.metrics_snapshot().bounded_pass_limit_hits == 0U,
                "callback addition must schedule another pass rather than mutate live traversal");
    }
}

void test_designer_scale_layout_transaction_converges() {
    std::shared_ptr<ProbeControl> root =
        make_control<ProbeControl>(StableId("designer.root"));
    (*root).set_requested_bounds({0.0, 0.0, 1280.0, 800.0});
    std::vector<std::shared_ptr<ProbeControl>> leaves;
    leaves.reserve(1024U);
    for (std::size_t row = 0U; row < 32U; ++row) {
        std::shared_ptr<ProbeControl> panel = make_control<ProbeControl>(
            StableId("designer.panel." + std::to_string(row)));
        (*panel).set_requested_bounds(
            {0.0, static_cast<double>(row) * 25.0, 1280.0, 25.0});
        for (std::size_t column = 0U; column < 32U; ++column) {
            std::shared_ptr<ProbeControl> leaf = make_control<ProbeControl>(StableId(
                "designer.leaf." + std::to_string(row) + "." +
                std::to_string(column)));
            (*leaf).set_requested_bounds(
                {static_cast<double>(column) * 40.0, 0.0, 38.0, 22.0});
            (*panel).add_child(leaf);
            leaves.push_back(std::move(leaf));
        }
        (*root).add_child(panel);
    }
    Window window(root, {1280.0, 800.0});
    window.perform_layout();
    const Rect committed = (*leaves.back()).committed_arranged_bounds();
    window.reset_activity_metrics();

    (*root).suspend_layout();
    for (std::size_t index = 0U; index < leaves.size(); ++index) {
        Rect requested = (*leaves[index]).requested_bounds();
        requested.width = 30.0 + static_cast<double>(index % 7U);
        (*leaves[index]).set_requested_bounds(requested);
    }
    window.perform_layout();
    require((*leaves.back()).committed_arranged_bounds() == committed,
            "designer-scale suspension must preserve committed descendant geometry");
    (*root).resume_layout(true);
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require((*leaves.back()).committed_arranged_bounds().width ==
                (*leaves.back()).requested_bounds().width &&
                metrics.bounded_pass_limit_hits == 0U &&
                metrics.arrange_passes <= 2U,
            "a thousand deferred child mutations must coalesce into bounded layout passes");
}

void test_callback_arbitration_snapshots_paint_hit_semantics_and_validation() {
    {
        std::shared_ptr<ProbeControl> root =
            make_control<ProbeControl>(StableId("callback.paint.root"));
        std::shared_ptr<CallbackArbitrationControl> mutator =
            make_control<CallbackArbitrationControl>(
            StableId("callback.paint.mutator"));
        std::shared_ptr<ProbeControl> removed = make_control<ProbeControl>(
            StableId("callback.paint.removed"));
        (*root).set_requested_bounds({0.0, 0.0, 180.0, 100.0});
        (*mutator).set_requested_bounds({0.0, 0.0, 80.0, 40.0});
        (*removed).set_requested_bounds({90.0, 0.0, 80.0, 40.0});
        (*root).add_child(mutator);
        (*root).add_child(removed);
        bool removed_once{};
        (*mutator).paint_callback =
            RemoveChildOnce(*root, *removed, removed_once);
        Window window(root, {180.0, 100.0});
        window.perform_layout();
        RecordingPainter painter;
        paint_pending(window, painter);
        require(removed_once && (*removed).paint_count == 0U &&
                    !(*removed).attached() && (*mutator).paint_overlay_count == 1U,
                "paint must skip a later identity removed by an earlier callback");
        paint_pending(window, painter);
        require(window.paint_lease_snapshot().state !=
                    PaintLeaseState::rendering,
                "callback-time paint mutation must release a coherent lease");
    }

    {
        std::shared_ptr<ProbeControl> root = make_control<ProbeControl>(
            StableId("callback.paint-dispose.root"));
        std::shared_ptr<CallbackArbitrationControl> disposing =
            make_control<CallbackArbitrationControl>(
            StableId("callback.paint-dispose.child"));
        (*root).set_requested_bounds({0.0, 0.0, 120.0, 70.0});
        (*disposing).set_requested_bounds({0.0, 0.0, 80.0, 40.0});
        (*root).add_child(disposing);
        (*disposing).paint_callback = DisposeCapturedControl(disposing);
        Window window(root, {120.0, 70.0});
        window.perform_layout();
        RecordingPainter painter;
        paint_pending(window, painter);
        require(!(*disposing).is_alive() &&
                    (*disposing).paint_overlay_count == 0U &&
                    (*root).children().empty(),
                "self-disposal in OnPaint must suppress overlay and chunk publication");
        paint_pending(window, painter);
    }

    {
        std::shared_ptr<ProbeControl> root =
            make_control<ProbeControl>(StableId("callback.hit.root"));
        std::shared_ptr<ProbeControl> bottom =
            make_control<ProbeControl>(StableId("callback.hit.bottom"));
        std::shared_ptr<CallbackArbitrationControl> top =
            make_control<CallbackArbitrationControl>(
            StableId("callback.hit.top"));
        (*root).set_requested_bounds({0.0, 0.0, 100.0, 60.0});
        (*bottom).set_requested_bounds({0.0, 0.0, 100.0, 60.0});
        (*top).set_requested_bounds({0.0, 0.0, 100.0, 60.0});
        (*root).add_child(bottom);
        (*root).add_child(top);
        (*top).hit_test_callback = DisposeCapturedControl(top);
        Window window(root, {100.0, 60.0});
        window.perform_layout();
        require(window.hit_test({20.0, 20.0}) == bottom &&
                    !(*top).is_alive() && (*top).hit_test_count == 1U,
                "hit testing must retry after callback disposal and return a live target");
    }

    {
        std::shared_ptr<ProbeControl> root =
            make_control<ProbeControl>(StableId("callback.semantic.root"));
        std::shared_ptr<CallbackArbitrationControl> mutator =
            make_control<CallbackArbitrationControl>(
            StableId("callback.semantic.mutator"));
        std::shared_ptr<CallbackArbitrationControl> removed =
            make_control<CallbackArbitrationControl>(
            StableId("callback.semantic.removed"));
        std::shared_ptr<CallbackArbitrationControl> added =
            make_control<CallbackArbitrationControl>(
            StableId("callback.semantic.added"));
        (*root).accessible_name = "Semantic root";
        (*mutator).accessible_name = "Semantic mutator";
        (*removed).accessible_name = "Semantic removed";
        (*added).accessible_name = "Semantic added";
        (*root).add_child(mutator);
        (*root).add_child(removed);
        bool changed_once{};
        (*mutator).semantic_callback =
            ReplaceChildOnce(*root, *removed, added, changed_once);
        Window window(root, {160.0, 90.0});
        window.perform_layout();
        const SemanticSnapshot snapshot = window.semantic_snapshot();
        const std::string json = snapshot.to_json();
        require(changed_once && snapshot.generation == window.semantic_generation() &&
                    json.find("Semantic added") != std::string::npos &&
                    json.find("Semantic removed") == std::string::npos,
                "semantic snapshots must retry to one coherent retained generation");

        bool alternate{};
        (*mutator).semantic_callback =
            AlternateAccessibleName(*mutator, alternate);
        bool bounded_fault{};
        try {
            static_cast<void>(window.semantic_snapshot());
        } catch (const std::runtime_error&) {
            bounded_fault = true;
        }
        (*mutator).semantic_callback = {};
        const MetricsSnapshot arbitration = window.metrics_snapshot();
        require(bounded_fault &&
                    arbitration.callback_arbitration_retries >= 5U &&
                    arbitration.callback_arbitration_limit_hits == 1U &&
                    window.semantic_snapshot().generation ==
                        window.semantic_generation(),
                "nonconvergent semantic callbacks must stop at a bound and release the guard");
    }

    {
        std::shared_ptr<ProbeControl> root =
            make_control<ProbeControl>(StableId("callback.validate.root"));
        std::shared_ptr<ProbeControl> first =
            make_control<ProbeControl>(StableId("callback.validate.first"));
        std::shared_ptr<ProbeControl> removed = make_control<ProbeControl>(
            StableId("callback.validate.removed"));
        (*root).add_child(first);
        (*root).add_child(removed);
        Window window(root, {120.0, 70.0});
        std::size_t removed_validations{};
        std::shared_ptr<gui_forms::Component> owner = std::make_shared<Component>();
        SubscriptionToken first_token = (*first).validating().subscribe(
            *owner, RemoveChildDuringValidation(*root, *removed));
        SubscriptionToken removed_token = (*removed).validating().subscribe(
            *owner, CountCalls<ControlValidationEvent&>(removed_validations));
        require(window.validate_children(root, ValidationConstraints::none) &&
                    removed_validations == 0U && !(*removed).attached() &&
                    first_token.connected() && removed_token.connected(),
                "bulk validation must skip identities removed by an earlier callback");
    }
}

void test_paint_only_does_not_measure() {
    Fixture fixture;
    (*fixture.child).invalidate(Dirty::paint);
    paint_pending(*fixture.window, fixture.painter);
    const MetricsSnapshot snapshot = (*fixture.window).metrics_snapshot();
    require(snapshot.measure_passes == 0, "paint-only mutation must not measure");
    require(snapshot.arrange_passes == 0, "paint-only mutation must not arrange");
    require(snapshot.paint_passes == 1, "paint-only mutation must produce one paint pass");
    require(snapshot.display_chunks_rebuilt == 1 && snapshot.display_chunks_reused == 1,
            "paint-only mutation must rebuild one chunk and reuse its retained ancestor");
    require(snapshot.paint_invalidations_consumed == 1,
            "paint-only mutation must consume one declared paint invalidation");
}

void test_layout_flushes_before_hit_test() {
    Fixture fixture;
    (*fixture.child).set_requested_bounds({80.0, 20.0, 40.0, 30.0});
    require((*fixture.window).hit_test({85.0, 25.0}) == fixture.child,
            "hit test must observe newly arranged layout");
    const MetricsSnapshot snapshot = (*fixture.window).metrics_snapshot();
    require(snapshot.read_barrier_flushes == 1,
            "position-dependent hit test must record a layout read barrier");
}

void test_topmost_hit_and_activation() {
    Fixture fixture;
    std::shared_ptr<ProbeControl> top =
        make_control<ProbeControl>(StableId("top"));
    (*top).set_requested_bounds({10.0, 10.0, 40.0, 30.0});
    (*top).focusable = true;
    (*fixture.root).add_child(top);
    (*fixture.window).perform_layout();

    require((*fixture.window).hit_test({15.0, 15.0}) == top,
            "last retained sibling must be topmost for hit testing");
    (*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                      {15.0, 15.0}});
    (*fixture.window).dispatch_pointer({PointerAction::up, PointerButton::primary,
                                      {15.0, 15.0}});
    require((*top).activation_count == 1, "eligible press/release must activate exactly once");
    require((*top).focus_state, "primary press must focus an eligible target");
    require((*fixture.window).metrics_snapshot().activations == 1,
            "activation must be counted structurally");

    (*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                      {15.0, 15.0}});
    (*fixture.window).dispatch_pointer({PointerAction::up, PointerButton::primary,
                                      {150.0, 100.0}});
    require((*top).activation_count == 1,
            "release away from pressed target must not activate");
}

void test_routed_phases_and_consumed_release() {
    Fixture fixture;
    (*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                      {15.0, 15.0}});
    require((*fixture.root).phases ==
                std::vector<EventPhase>{EventPhase::preview, EventPhase::bubble},
            "pointer down must preview root-to-target and bubble target-to-root");
    require((*fixture.child).phases ==
                std::vector<EventPhase>{EventPhase::preview, EventPhase::target},
            "target must observe preview before its Forms-like event");

    (*fixture.child).handle_preview_release = true;
    require((*fixture.window).dispatch_pointer({PointerAction::up, PointerButton::primary,
                                              {15.0, 15.0}}),
            "preview may consume release");
    require((*fixture.child).activation_count == 0,
            "consumed release must not synthesize activation");

    std::shared_ptr<ProbeControl> other =
        make_control<ProbeControl>(StableId("other"));
    (*other).set_requested_bounds({100.0, 10.0, 40.0, 30.0});
    (*other).focusable = true;
    (*fixture.root).add_child(other);
    (*fixture.window).perform_layout();
    (*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                      {105.0, 15.0}});
    require((*fixture.window).focused_control() == other,
            "consumed physical release must still clear pointer capture");
}

void test_capture_released_before_release_callback() {
    Fixture fixture;
    bool callback_observed_release = false;
    (*fixture.child).release_callback =
        ObserveReleasedCapture(*fixture.window, callback_observed_release);
    (*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                      {15.0, 15.0}});
    require((*fixture.window).captured_control() == fixture.child,
            "primary down must capture before release ordering gate");
    (*fixture.window).dispatch_pointer({PointerAction::up, PointerButton::primary,
                                      {15.0, 15.0}});
    require(callback_observed_release,
            "pointer capture must be released before synchronous release callback");
    require((*fixture.child).activation_count == 1,
            "early capture release must preserve qualified activation");
}

void test_disabled_control_is_ineligible() {
    Fixture fixture;
    (*fixture.child).enabled = false;
    require(!(*fixture.window).request_focus(fixture.child),
            "disabled control must reject focus");
    (*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                      {15.0, 15.0}});
    (*fixture.window).dispatch_pointer({PointerAction::up, PointerButton::primary,
                                      {15.0, 15.0}});
    require((*fixture.child).activation_count == 0, "disabled control must not activate");
}

void test_stable_ids_and_detached_lifetime() {
    Fixture fixture;
    require((*fixture.window).find("child") == fixture.child, "stable ID must resolve attached control");
    const MetricsSnapshot before = (*fixture.window).metrics_snapshot();
    require(before.control_count == 2 && before.stable_id_count == 2,
            "population snapshot must count retained controls and IDs");

    std::shared_ptr<ProbeControl> duplicate =
        make_control<ProbeControl>(StableId("child"));
    bool duplicate_rejected = false;
    try {
        (*fixture.root).add_child(duplicate);
    } catch (const std::logic_error&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected, "duplicate stable ID must be rejected");

    Control::Ptr detached = (*fixture.root).remove_child((*fixture.child).runtime_id());
    require(detached == fixture.child, "removal must return a strong detached handle");
    require(!(*detached).parent(), "detached control must have a weak empty parent");
    require(!(*fixture.window).find("child"), "detached stable ID must leave window registry");
}

// Stable IDs are '.'-separated segments of lowercase ASCII letters and digits
// joined by single dashes. A literal is checked when the program is compiled
// (these static_asserts and every StableId("...") in the tree); a runtime
// string throws when it is constructed.
static_assert(valid_stable_id("root") && valid_stable_id("paint.editor") && valid_stable_id("list-view.item.0") &&
              valid_stable_id("a1-b2.c3"));
static_assert(!valid_stable_id("") && !valid_stable_id("list_view") && !valid_stable_id("List") &&
              !valid_stable_id("a..b") && !valid_stable_id(".a") && !valid_stable_id("a.") &&
              !valid_stable_id("-a") && !valid_stable_id("a-") && !valid_stable_id("a--b") &&
              !valid_stable_id("a.-b") && !valid_stable_id("a b") && !valid_stable_id("caf\xc3\xa9"));

void test_stable_id_rule() {
    require(StableId(std::string("list-view.item.7")).value() == "list-view.item.7",
            "a valid runtime stable ID keeps its spelling");
    require(StableId("paint.editor") == StableId(std::string("paint.editor")),
            "a literal and a runtime string with one spelling are one stable ID");
    require(StableId(std::string_view("tab-page.panel")).value() == "tab-page.panel",
            "a runtime std::string_view is checked like a std::string");
    for (const char* invalid : {"", "list_view", "Upper", "a..b", "trailing.", "a--b", "tab\tid"}) {
        bool refused = false;
        try {
            static_cast<void>(StableId(std::string(invalid)));
        } catch (const std::invalid_argument& error) {
            refused = std::string_view(error.what()).find("stable ID") != std::string_view::npos;
        }
        require(refused, "an invalid runtime stable ID throws std::invalid_argument");
    }
}

void test_static_tree_factory() {
    ControlFactory factory;
    factory.register_type("probe", make_probe_control);
    StaticNode description{"probe", "static.root", {0.0, 0.0, 100.0, 50.0},
                           {{"probe", "static.child", {2.0, 3.0, 20.0, 10.0}, {}}}};
    Control::Ptr root = build_static_tree(description, factory);
    Window window(root, {100.0, 50.0});
    require(window.find("static.child") != nullptr,
            "compiled static description must retain stable child ID");
}

void test_cursor_inheritance_and_override() {
    Fixture fixture;
    require((*fixture.child).effective_cursor() == CursorKind::arrow,
            "controls must default to the portable arrow cursor");
    (*fixture.root).cursor = CursorKind::hand;
    require((*fixture.child).effective_cursor() == CursorKind::hand,
            "unset child cursor must inherit from the retained parent");
    (*fixture.child).cursor = CursorKind::text;
    require((*fixture.child).effective_cursor() == CursorKind::text,
            "explicit child cursor must override inherited cursor");
    (*fixture.child).cursor = std::nullopt;
    require((*fixture.child).effective_cursor() == CursorKind::hand,
            "clearing a cursor override must restore inheritance");
}

void test_control_identity_geometry_constraints_and_z_order() {
    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("control.root"));
    std::shared_ptr<gui_forms::Control> first = make_control<Control>(StableId("control.first"));
    std::shared_ptr<gui_forms::Control> second = make_control<Control>(StableId("control.second"));
    std::shared_ptr<gui_forms::Control> nested = make_control<Control>(StableId("control.second.nested"));
    std::string observed_name;
    SubscriptionToken changed = (*first).name_changed().subscribe(
        RecordValue<std::string>(observed_name));
    (*first).name = "primary-field";
    require((*first).name.read() == "primary-field" &&
                observed_name == "primary-field" &&
                (*first).stable_id().value() == "control.first",
            "mutable Forms Name must remain distinct from immutable retained identity");

    (*first).minimum_size = {40.0, 20.0};
    (*first).maximum_size = {80.0, 60.0};
    (*first).set_requested_bounds({10.0, 12.0, 5.0, 100.0});
    require((*first).requested_bounds() == Rect{10.0, 12.0, 40.0, 60.0} &&
                (*first).left() == 10.0 && (*first).top() == 12.0 &&
                (*first).right() == 50.0 && (*first).bottom() == 72.0,
            "requested bounds must apply finite minimum/maximum constraints");
    bool invalid_rejected = false;
    try {
        (*first).set_requested_bounds({0.0, 0.0, -1.0, 1.0});
    } catch (const std::invalid_argument&) {
        invalid_rejected = true;
    }
    require(invalid_rejected,
            "negative control extents must be rejected before retained layout mutation");

    (*second).set_requested_bounds({10.0, 12.0, 40.0, 60.0});
    (*nested).set_requested_bounds({2.0, 2.0, 8.0, 8.0});
    (*nested).tab_index = 5U;
    (*second).tab_index = 10U;
    (*first).tab_index = 20U;
    (*second).add_child(nested);
    (*root).add_child(first);
    (*root).add_child(second);
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 90.0});
    Window window(root, {120.0, 90.0});
    window.perform_layout();
    require((*root).contains(*first) && !(*first).contains(*root) &&
                (*first).point_to_window({2.0, 3.0}) == Point{12.0, 15.0} &&
                (*first).point_from_window({12.0, 15.0}) == Point{2.0, 3.0} &&
                (*first).rectangle_to_window({2.0, 3.0, 5.0, 7.0}) ==
                    Rect{12.0, 15.0, 5.0, 7.0} &&
                (*first).rectangle_from_window({12.0, 15.0, 5.0, 7.0}) ==
                    Rect{2.0, 3.0, 5.0, 7.0},
            "containment and window-coordinate conversion must use committed retained geometry");
    require((*root).child_index((*second).runtime_id()) == 0U &&
                (*root).child_index((*first).runtime_id()) == 1U &&
                !(*root).child_index((*nested).runtime_id()),
            "child indices must expose topmost-first direct-child z order");
    require((*root).get_child_at_point({20.0, 20.0}) == second,
            "direct child lookup must return the topmost overlapping child");
    window.capture_pointer(second, 1U);
    (*second).hit_test_transparent = true;
    require((*root).get_child_at_point(
                {20.0, 20.0}, GetChildAtPointSkip::transparent) == first &&
                window.hit_test({20.0, 20.0}) == first &&
                window.captured_control() == nullptr,
            "transparent child lookup and ordinary pointer routing must pass through and revoke capture coherently");
    (*second).hit_test_transparent = false;
    (*second).enabled = false;
    require((*root).get_child_at_point(
                {20.0, 20.0}, GetChildAtPointSkip::disabled) == first,
            "disabled child lookup exclusion must reveal the next z-order candidate");
    (*second).enabled = true;
    (*second).visible = false;
    require((*root).get_child_at_point(
                {20.0, 20.0}, GetChildAtPointSkip::invisible) == first,
            "invisible child lookup exclusion must reveal the next z-order candidate");
    (*second).visible = true;
    require((*root).get_next_control({}, true) == second &&
                (*root).get_next_control(second, true) == nested &&
                (*root).get_next_control(nested, true) == first &&
                (*root).get_next_control(first, true) == nullptr &&
                (*root).get_next_control(first, false) == nested,
            "GetNextControl must traverse stable nested tab order without wrapping");

    (*first).set_bounds({25.0, 30.0, 70.0, 50.0},
                      BoundsSpecified::location | BoundsSpecified::width);
    require((*first).requested_bounds() == Rect{25.0, 30.0, 70.0, 60.0},
            "masked bounds mutation must preserve unspecified constrained fields");
    bool invalid_bounds_mask_rejected = false;
    try {
        (*first).set_bounds({}, static_cast<BoundsSpecified>(0x80U));
    } catch (const std::invalid_argument&) {
        invalid_bounds_mask_rejected = true;
    }
    require(invalid_bounds_mask_rejected,
            "unknown BoundsSpecified bits must be rejected before mutation");

    (*first).bring_to_front();
    require((*root).children().back() == first &&
                (*root).child_index((*first).runtime_id()) == 0U,
            "BringToFront must move the child to retained topmost z order");
    (*first).send_to_back();
    require((*root).children().front() == first &&
                (*root).child_index((*first).runtime_id()) == 1U,
            "SendToBack must move the child to retained backmost z order");

    std::shared_ptr<gui_forms::Control> sizing = make_control<Control>(StableId("control.autosize"));
    std::shared_ptr<gui_forms::Control> content = make_control<Control>(StableId("control.autosize.content"));
    (*sizing).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    (*sizing).padding = {2.0, 2.0, 2.0, 2.0};
    (*content).set_requested_bounds({10.0, 8.0, 40.0, 20.0});
    (*sizing).add_child(content);
    std::size_t auto_size_events{};
    SubscriptionToken auto_size_token = (*sizing).auto_size_changed().subscribe(
        CountTrueValues(auto_size_events));
    (*sizing).auto_size = true;
    require((*sizing).get_preferred_size({200.0, 200.0}) == Size{100.0, 80.0} &&
                auto_size_events == 1U,
            "GrowOnly AutoSize must retain authored minimum extent and publish one change");
    (*sizing).auto_size_mode = AutoSizeMode::grow_and_shrink;
    require((*sizing).get_preferred_size({200.0, 200.0}) == Size{55.0, 33.0},
            "GrowAndShrink AutoSize must derive deterministic child, margin, and padding extent");
}

void test_damage_and_idle_metrics() {
    DamageRegion region;
    region.add({0.0, 0.0, 10.0, 10.0});
    region.add({5.0, 0.0, 10.0, 10.0});
    require(region.area() == 150.0, "damage area must not double count overlaps");

    Fixture fixture;
    require(!(*fixture.window).needs_frame(), "fully painted retained tree must idle without a frame");
    (*fixture.window).metrics().set_renderer("recording-cpu", true);
    (*fixture.window).metrics().record_present(1000);
    (*fixture.window).metrics().record_present(2500);
    const MetricsSnapshot snapshot = (*fixture.window).metrics_snapshot();
    require(snapshot.frames_presented == 2 && snapshot.worst_present_duration_nanoseconds == 2500,
            "present durations must be structured and retain worst observation");
    require(snapshot.to_json().find("\"cpu_only\":true") != std::string::npos,
            "machine snapshot must declare CPU-only renderer capability");
}

void test_overlay_surface_composites_after_every_application_plane() {
    const Color application_overlay = Color::rgba(170, 20, 30);
    const Color overlay_backplane = Color::rgba(20, 170, 30);
    std::shared_ptr<ProbeControl> root =
        make_control<ProbeControl>(StableId("overlay-order.root"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).set_paint_plane(PaintPlane::overlay);
    std::shared_ptr<ProbeControl> owner =
        make_control<ProbeControl>(StableId("overlay-order.owner"));
    (*owner).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
    (*root).add_child(owner);
    Window window(root, {120.0, 80.0});

    class ColoredOverlayRoot final : public Control {
    public:
        ColoredOverlayRoot(StableId id, Color color)
            : Control(std::move(id)), color_(color) {
            set_paint_plane(PaintPlane::backplane);
        }
        void on_paint(Painter& painter, Rect damage) override {
            painter.fill_rect(damage, color_);
        }
    private:
        Color color_;
    };

    // ProbeControl's fixed paint is replaced by a child in the overlay paint
    // plane so the final application command has a distinctive color.
    class OverlayPlaneControl final : public Control {
    public:
        OverlayPlaneControl(StableId id, Color color)
            : Control(std::move(id)), color_(color) {
            set_paint_plane(PaintPlane::overlay);
        }
        void on_paint(Painter& painter, Rect damage) override {
            painter.fill_rect(damage, color_);
        }
    private:
        Color color_;
    };
    std::shared_ptr<OverlayPlaneControl> application = make_control<OverlayPlaneControl>(
        StableId("overlay-order.application-overlay"), application_overlay);
    (*application).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*root).add_child(application);
    std::shared_ptr<ColoredOverlayRoot> overlay = make_control<ColoredOverlayRoot>(
        StableId("overlay-order.overlay"), overlay_backplane);
    (*overlay).set_requested_bounds({10.0, 10.0, 60.0, 40.0});
    OverlayToken token = window.open_overlay(owner, overlay);
    RecordingPainter painter;
    window.paint(painter);
    require(token.connected() && !painter.fill_colors.empty() &&
                painter.fill_colors.back() == overlay_backplane,
            "an overlay root's backplane must composite after the application's overlay paint plane");
}

// The window owns a shown overlay root (owner, 2026-11-05): a caller that
// passes a temporary still sees it shown until it closes it.
void test_window_owns_a_shown_overlay() {
    Fixture fixture;
    Window& window = *fixture.window;
    OverlayToken token = window.open_overlay(
        fixture.root, make_control<ProbeControl>(StableId("owned-overlay.root")));
    window.perform_layout();
    paint_pending(window, fixture.painter);
    const Control::Ptr shown = window.find("owned-overlay.root");
    require(token.connected() && shown && (*shown).attached_surface_root() == &window,
            "an overlay whose caller kept no handle stays shown");
    const Control::WeakPtr observed = shown;
    token.disconnect();
    require(!window.find("owned-overlay.root") && !token.connected() && !observed.expired(),
            "closing it detaches it while the test still holds it");
}

// A control that closes an overlay from its own attach notification: the
// window does not detach during a lifecycle notification, so the overlay,
// which only the window owned, is destroyed while still attached.
class ClosesOverlayOnAttach final : public ProbeControl {
public:
    explicit ClosesOverlayOnAttach(StableId id) : ProbeControl(std::move(id)) {}
    void on_attached_to_window() override {
        ProbeControl::on_attached_to_window();
        if (token != nullptr) (*token).disconnect();
    }
    OverlayToken* token{};
};

// The destructor backstop (owner, 2026-11-05): a control destroyed while
// attached leaves no address in the window's registries, so its pending
// touch is dropped and the next settle, layout and paint run safely.
void test_destroyed_attached_control_is_forgotten() {
    Fixture fixture;
    Window& window = *fixture.window;
    OverlayToken token = window.open_overlay(
        fixture.root, make_control<ProbeControl>(StableId("forgotten-overlay.root")));
    Control::Ptr overlay = window.find("forgotten-overlay.root");
    require(overlay != nullptr, "the overlay is attached");
    const Control::WeakPtr observed = overlay;
    // A pending touch for the settle to reach.
    (*overlay).set_requested_bounds({5.0, 5.0, 30.0, 20.0});
    // The test lets go of the overlay; only the window holds it now.
    overlay.reset();
    std::shared_ptr<ClosesOverlayOnAttach> closer =
        make_control<ClosesOverlayOnAttach>(StableId("forgotten-overlay.closer"));
    (*closer).token = &token;
    (*fixture.root).add_child(closer);
    require(observed.expired(), "the overlay was destroyed while attached");
    window.perform_layout();
    paint_pending(window, fixture.painter);
    require(!window.find("forgotten-overlay.root") && !token.connected(),
            "the destroyed overlay is gone from the window");
}

void test_tokenized_accelerator_runs_after_focused_route() {
    Fixture fixture;
    require((*fixture.window).request_focus(fixture.child),
            "accelerator test requires a focused retained target");
    std::shared_ptr<gui_forms::Component> owner = std::make_shared<Component>();
    std::size_t invocations{};
    AcceleratorToken token = (*fixture.window).register_accelerator(
        *owner, {PhysicalKey::left, Modifier::alt},
        CountSuccessfulCommand(invocations));
    require(token.connected() && (*fixture.window).dispatch_key(
                {KeyAction::down, PhysicalKey::left, Modifier::alt}) &&
                invocations == 1U,
            "an exact unhandled chord must invoke its window accelerator once");
    require(!(*fixture.window).dispatch_key(
                {KeyAction::up, PhysicalKey::left, Modifier::alt}) &&
                invocations == 1U,
            "key release must never execute an accelerator");
    (*fixture.child).handle_key = true;
    require((*fixture.window).dispatch_key(
                {KeyAction::down, PhysicalKey::left, Modifier::alt}) &&
                invocations == 1U,
            "focused controls must retain precedence over global accelerators");
    (*fixture.child).handle_key = false;
    (*owner).dispose();
    require(!token.connected() && !(*fixture.window).dispatch_key(
                {KeyAction::down, PhysicalKey::left, Modifier::alt}) &&
                invocations == 1U,
            "owner disposal must deterministically revoke its accelerator");
}

void test_presentation_settings_separate_text_and_device_scale() {
    std::shared_ptr<FontProbe> label =
        make_control<FontProbe>(StableId("presentation.label"));
    Window window(label, {400.0, 120.0});
    const Size normal = (*label).measure({400.0, 120.0});
    std::size_t changes{};
    std::shared_ptr<gui_forms::Component> owner = std::make_shared<Component>();
    SubscriptionToken token = window.presentation_changed().subscribe(
        *owner, CountCalls<const PresentationSettings&>(changes));
    std::shared_ptr<ProbeControl> overlay =
        make_control<ProbeControl>(StableId("presentation.overlay"));
    (*overlay).set_requested_bounds({0.0, 0.0, 40.0, 20.0});
    OverlayToken overlay_token = window.open_overlay(label, overlay);

    window.set_scale(2.0);
    require(window.scale() == 2.0 && (*label).effective_text_scale() == 1.0,
            "device scale must not mutate logical text scale");
    require((*label).measure({400.0, 120.0}) == normal,
            "device scale must not change retained logical measurement");

    window.set_text_scale(2.0);
    const Size enlarged = (*label).measure({400.0, 120.0});
    require(window.presentation_settings().text_scale == 2.0 &&
                (*label).effective_font((*label).font()).size == (*label).font().size * 2.0 &&
                enlarged.width > normal.width && enlarged.height > normal.height &&
                changes == 1U,
            "text scale must coherently affect public effective fonts and measurement");
    require(!overlay_token.connected(),
            "text-scale changes must dismiss transient geometry resolved at the old scale");
    window.set_text_scale(2.0);
    require(changes == 1U,
            "idempotent presentation mutation must not publish duplicate change events");

    RecordingPainter painter;
    window.paint(painter);
    require(painter.last_font && (*painter.last_font).size == (*label).font().size * 2.0,
            "paint must consume the same effective font used by measurement");

    bool rejected{};
    try {
        window.set_text_scale(0.49);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "presentation text scale must reject values outside its contract");

    bool wrong_thread_rejected{};
    std::thread worker(attempt_text_scale_on_worker, std::ref(window),
                       std::ref(wrong_thread_rejected));
    worker.join();
    require(wrong_thread_rejected,
            "presentation settings must retain Window UI-thread enforcement");
    require(token.connected(), "presentation subscription must remain tokenized");
}

void test_semantic_feedback_is_clocked_bounded_and_sound_optional() {
    std::shared_ptr<ProbeControl> root =
        make_control<ProbeControl>(StableId("feedback.root"));
    Window window(root, {100.0, 60.0});
    std::vector<std::uint64_t> times{10U, 10U, 12U};
    std::size_t cursor{};
    SemanticFeedback feedback(window, SequenceClock(times, cursor));
    feedback.set_maximum_records(2U);
    const SemanticFeedbackRecord location = feedback.emit(
        SemanticFeedbackKind::location_changed);
    PresentationSettings muted = window.presentation_settings();
    muted.sound_enabled = false;
    window.set_presentation_settings(muted);
    const SemanticFeedbackRecord option = feedback.emit(
        SemanticFeedbackKind::option_committed);
    const SemanticFeedbackRecord conflict = feedback.emit(
        SemanticFeedbackKind::conflict);
    require(location.timestamp_nanoseconds == 10U &&
                option.timestamp_nanoseconds == 11U &&
                conflict.timestamp_nanoseconds == 12U,
            "feedback must normalize an injected clock to strict monotonic order");
    require(location.cue == HostSoundCue::notification &&
                option.cue == HostSoundCue::success &&
                conflict.cue == HostSoundCue::warning &&
                location.sound_enabled && !option.sound_enabled &&
                !conflict.sound_enabled,
            "semantic feedback must map state kinds while keeping sound policy optional");
    require(feedback.records().size() == 2U &&
                feedback.dropped_record_count() == 1U &&
                feedback.records().front().sequence == 2U &&
                feedback.trace().find("feedback=option_committed sequence=2") !=
                    std::string::npos &&
                feedback.trace().find("sound=off") != std::string::npos,
            "feedback history and trace must be deterministic and bounded");
}

void test_paint_wake_is_coalesced_and_rearmed_after_damage_consumption() {
    std::shared_ptr<ProbeControl> root =
        make_control<ProbeControl>(StableId("paint-wake.root"));
    Window window(root, {120.0, 80.0});
    unsigned wakes{};
    window.set_paint_wake_handler(CountWakes(wakes));
    require(wakes == 1U,
            "installing a host paint seam on a dirty Window must request one wake");
    static_cast<void>(window.take_damage());

    (*root).invalidate(Dirty::paint);
    (*root).invalidate(Dirty::paint);
    require(wakes == 2U,
            "repeated retained invalidation must coalesce before host damage consumption");
    const PaintLeaseSnapshot coalesced = window.paint_lease_snapshot();
    require(coalesced.state == PaintLeaseState::dirty_queued &&
                coalesced.render_wake_queued &&
                coalesced.render_wakes_coalesced >= 1U,
            "the availability snapshot must expose one queued render and merged touches");
    static_cast<void>(window.take_damage());
    (*root).invalidate(Dirty::paint);
    require(wakes == 3U,
            "consuming damage must rearm the next independent paint wake");
    static_cast<void>(window.take_damage());

    window.set_occluded(true, FrameClock::now());
    (*root).invalidate(Dirty::paint);
    require(wakes == 3U,
            "explicitly occluded Window must retain dirtiness without waking raster work");
    window.set_occluded(false, FrameClock::now());
    require(wakes == 4U,
            "exposure must wake exactly once for retained occluded dirtiness");
    window.set_paint_wake_handler({});
}

} // namespace

int main() {
    try {
        test_nested_scopes_and_read_barrier();
        test_per_control_layout_transactions();
        test_layout_fault_releases_reentry_guard();
        test_layout_callbacks_may_mutate_retained_tree();
        test_designer_scale_layout_transaction_converges();
        test_callback_arbitration_snapshots_paint_hit_semantics_and_validation();
        test_paint_only_does_not_measure();
        test_layout_flushes_before_hit_test();
        test_topmost_hit_and_activation();
        test_routed_phases_and_consumed_release();
        test_capture_released_before_release_callback();
        test_disabled_control_is_ineligible();
        test_stable_ids_and_detached_lifetime();
        test_stable_id_rule();
        test_static_tree_factory();
        test_cursor_inheritance_and_override();
        test_control_identity_geometry_constraints_and_z_order();
        test_damage_and_idle_metrics();
        test_overlay_surface_composites_after_every_application_plane();
        test_window_owns_a_shown_overlay();
        test_destroyed_attached_control_is_forgotten();
        test_tokenized_accelerator_runs_after_focused_route();
        test_presentation_settings_separate_text_and_device_scale();
        test_semantic_feedback_is_clocked_bounded_and_sound_optional();
        test_paint_wake_is_coalesced_and_rearmed_after_damage_consumption();
        std::cout << "gui_forms_core_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_core_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
