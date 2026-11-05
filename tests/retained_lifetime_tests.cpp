#include "gui_forms/gui_forms.hpp"
#include "support/named_callbacks.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
namespace callbacks = gui_forms::test_support;

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

class ProbeComponent final : public Component {
public:
    explicit ProbeComponent(std::uint64_t& disposal_count)
        : disposal_count_(disposal_count) {}

private:
    void on_dispose() noexcept override { ++disposal_count_; }
    std::uint64_t& disposal_count_;
};

class ProbeControl : public Control {
public:
    explicit ProbeControl(StableId id) : Control(std::move(id)) {}

    void on_pointer(PointerEvent&) override {
        ++pointer_events;
        if (pointer_action) {
            pointer_action();
        }
    }
    void on_key(KeyEvent&) override { ++key_events; }
    void on_text_input(TextInputEvent&) override { ++text_events; }
    void on_focus_changed(bool focused) override {
        focus_state = focused;
        ++focus_events;
    }
    void on_activate() override { ++activations; }
    void on_paint(Painter&, Rect) override { ++paints; }

    std::function<void()> pointer_action;
    std::uint64_t pointer_events{};
    std::uint64_t key_events{};
    std::uint64_t text_events{};
    std::uint64_t focus_events{};
    std::uint64_t activations{};
    std::uint64_t paints{};
    bool focus_state{};
};

struct WindowFixture {
    WindowFixture() {
        (*root).set_requested_bounds({0.0, 0.0, 240.0, 160.0});
        (*left).set_requested_bounds({0.0, 0.0, 100.0, 150.0});
        (*right).set_requested_bounds({120.0, 0.0, 100.0, 150.0});
        (*child).set_requested_bounds({10.0, 10.0, 50.0, 40.0});
        (*child).set_focusable(true);
        (*left).add_child(child);
        (*root).add_child(left);
        (*root).add_child(right);
        window = std::make_unique<Window>(root, Size{240.0, 160.0});
        (*window).perform_layout();
        (*window).paint(painter);
        (*window).reset_activity_metrics();
    }

    void press_child() {
        (*window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                  {15.0, 15.0}});
        require((*window).focused_control() == child, "press must focus child");
        require((*window).captured_control() == child, "press must capture child");
        require((*window).pressed_control() == child, "press must retain pressed identity");
    }

    std::shared_ptr<ProbeControl> root = make_control<ProbeControl>(StableId("root"));
    std::shared_ptr<ProbeControl> left = make_control<ProbeControl>(StableId("left"));
    std::shared_ptr<ProbeControl> right = make_control<ProbeControl>(StableId("right"));
    std::shared_ptr<ProbeControl> child = make_control<ProbeControl>(StableId("child"));
    std::unique_ptr<Window> window;
    NullPainter painter;
};

class AddToInteger final {
public:
    explicit AddToInteger(int& total) noexcept : total_(total) {}
    void operator()(int value) const { total_ += value; }

private:
    int& total_;
};

class DisposeOwnerAfterRecording final {
public:
    DisposeOwnerAfterRecording(std::vector<int>& order,
                               std::shared_ptr<ProbeComponent> owner) noexcept
        : order_(order), owner_(std::move(owner)) {}

    void operator()() const {
        order_.push_back(1);
        (*owner_).dispose();
    }

private:
    std::vector<int>& order_;
    std::shared_ptr<ProbeComponent> owner_;
};

class FirstSnapshotCallback final {
public:
    FirstSnapshotCallback(Event<>& event, std::vector<int>& order,
                          std::optional<SubscriptionToken>& second,
                          std::optional<SubscriptionToken>& added) noexcept
        : event_(event), order_(order), second_(second), added_(added) {}

    void operator()() const {
        order_.push_back(1);
        (*second_).disconnect();
        if (!added_) {
            added_.emplace(event_.subscribe(
                callbacks::PushConstant<std::vector<int>, int>(order_, 3)));
        }
    }

private:
    Event<>& event_;
    std::vector<int>& order_;
    std::optional<SubscriptionToken>& second_;
    std::optional<SubscriptionToken>& added_;
};

class IncrementBy final {
public:
    IncrementBy(int& value, int increment) noexcept
        : value_(value), increment_(increment) {}
    void operator()() const { value_ += increment_; }

private:
    int& value_;
    int increment_;
};

class DisposeFixtureChild final {
public:
    explicit DisposeFixtureChild(WindowFixture& fixture) noexcept
        : fixture_(fixture) {}
    void operator()() const { (*fixture_.child).dispose(); }

private:
    WindowFixture& fixture_;
};

class ReparentFixtureChild final {
public:
    explicit ReparentFixtureChild(WindowFixture& fixture) noexcept
        : fixture_(fixture) {}
    void operator()() const { (*fixture_.right).add_child(fixture_.child); }

private:
    WindowFixture& fixture_;
};

void throw_callback_failure() {
    throw std::runtime_error("callback failure");
}

void disable_child(WindowFixture& fixture) {
    (*fixture.child).enabled = false;
}

void hide_left_parent(WindowFixture& fixture) {
    (*fixture.left).visible = false;
}

void remove_child(WindowFixture& fixture) {
    static_cast<void>(
        (*fixture.left).remove_child((*fixture.child).runtime_id()));
}

void reparent_child(WindowFixture& fixture) {
    (*fixture.right).add_child(fixture.child);
}

void dispose_child(WindowFixture& fixture) {
    (*fixture.child).dispose();
}

class SetBoundsOperation final {
public:
    explicit SetBoundsOperation(WindowFixture& fixture) noexcept
        : fixture_(fixture) {}
    void operator()() const {
        (*fixture_.child).set_requested_bounds({9.0, 9.0, 9.0, 9.0});
    }

private:
    WindowFixture& fixture_;
};

class DispatchPointerOperation final {
public:
    explicit DispatchPointerOperation(WindowFixture& fixture) noexcept
        : fixture_(fixture) {}
    void operator()() const {
        static_cast<void>((*fixture_.window).dispatch_pointer(
            {PointerAction::down, PointerButton::primary, {15.0, 15.0}}));
    }

private:
    WindowFixture& fixture_;
};

class PerformLayoutOperation final {
public:
    explicit PerformLayoutOperation(WindowFixture& fixture) noexcept
        : fixture_(fixture) {}
    void operator()() const { (*fixture_.window).perform_layout(); }

private:
    WindowFixture& fixture_;
};

class PaintOperation final {
public:
    PaintOperation(WindowFixture& fixture, NullPainter& painter) noexcept
        : fixture_(fixture), painter_(painter) {}
    void operator()() const { (*fixture_.window).paint(painter_); }

private:
    WindowFixture& fixture_;
    NullPainter& painter_;
};

template <typename Operation>
void count_wrong_thread_rejection(Operation operation,
                                  std::uint64_t& rejected) noexcept {
    try {
        operation();
    } catch (const std::logic_error&) {
        ++rejected;
    }
}

template <typename Operation>
void run_on_wrong_thread(Operation operation, std::uint64_t& rejected) {
    std::thread worker(count_wrong_thread_rejection<Operation>,
                       std::move(operation), std::ref(rejected));
    worker.join();
}

void test_component_ownership_is_not_visual_parenting() {
    std::uint64_t nonvisual_disposals = 0;
    std::shared_ptr<ProbeComponent> nonvisual =
        std::make_shared<ProbeComponent>(nonvisual_disposals);
    std::shared_ptr<ProbeControl> root =
        make_control<ProbeControl>(StableId("component.root"));
    std::shared_ptr<ProbeControl> visual =
        make_control<ProbeControl>(StableId("component.visual"));
    (*root).add_child(visual);

    ComponentContainer components;
    components.add(nonvisual);
    components.add(visual);
    require((*visual).parent() == root, "component ownership must not change visual parent");
    require(components.components().size() == 2,
            "container must strongly own visual and nonvisual components");

    root.reset();
    require((*visual).parent() == nullptr,
            "weak visual parent must expire when its last strong owner is released");
    require(!(*visual).is_disposed(),
            "independent component ownership must keep detached control alive");
    components.dispose();
    components.dispose();
    require(nonvisual_disposals == 1, "container disposal must be idempotent");
    require((*visual).is_disposed(), "container disposal must dispose owned control");

    WindowFixture attached;
    ComponentContainer attached_components;
    attached_components.add(attached.child);
    attached_components.dispose();
    require((*attached.child).is_disposed() && !(*attached.window).find("child") &&
                (*attached.left).is_alive(),
            "disposing a component owner must detach its control without disposing visual parent");
}

void test_visual_strong_and_weak_lifetime() {
    std::shared_ptr<ProbeControl> root =
        make_control<ProbeControl>(StableId("lifetime.root"));
    std::shared_ptr<ProbeControl> retained =
        make_control<ProbeControl>(StableId("lifetime.retained"));
    (*root).add_child(retained);
    std::weak_ptr<ProbeControl> retained_weak = retained;
    retained.reset();
    require(!retained_weak.expired(), "visual parent must strongly retain child");

    Control::Ptr detached =
        (*root).remove_child((*retained_weak.lock()).runtime_id());
    require(detached != nullptr && !(*detached).parent(),
            "detached externally retained child must survive with weak parent cleared");
    std::weak_ptr<Control> released = detached;
    detached.reset();
    require(released.expired(), "unretained detached child must die immediately");

    std::shared_ptr<ProbeControl> cycle_child =
        make_control<ProbeControl>(StableId("lifetime.cycle"));
    (*root).add_child(cycle_child);
    std::weak_ptr<ProbeControl> root_weak = root;
    cycle_child.reset();
    root.reset();
    require(root_weak.expired(), "weak child-to-parent link must not form a cycle");
}

void test_disposal_contract_and_registry() {
    ProbeControl standalone(StableId("dispose.standalone"));
    standalone.dispose();
    standalone.dispose();
    require(standalone.is_disposed(),
            "detached stack-local proving control must dispose without shared ownership");

    WindowFixture fixture;
    (*fixture.child).dispose();
    (*fixture.child).dispose();
    require((*fixture.child).is_disposed(), "control must expose disposed state");
    require(!(*fixture.window).find("child"), "disposed control must leave stable-ID registry");
    require(!(*fixture.child).parent(), "disposed control must detach from visual parent");
    require((*fixture.window).metrics_snapshot().disposals == 1,
            "idempotent disposal must be counted once");

    bool mutation_rejected = false;
    try {
        (*fixture.child).visible = true;
    } catch (const std::logic_error&) {
        mutation_rejected = true;
    }
    require(mutation_rejected, "post-dispose mutation must fail without tree corruption");

    std::shared_ptr<ProbeControl> parent =
        make_control<ProbeControl>(StableId("dispose.parent"));
    std::shared_ptr<ProbeControl> child =
        make_control<ProbeControl>(StableId("dispose.child"));
    (*parent).add_child(child);
    (*parent).dispose();
    require((*parent).is_disposed() && (*child).is_disposed(),
            "disposing a visual parent must recursively dispose visual children");

    WindowFixture attached_parent;
    (*attached_parent.left).dispose();
    require((*attached_parent.left).is_disposed() && (*attached_parent.child).is_disposed() &&
                !(*attached_parent.window).find("left") &&
                !(*attached_parent.window).find("child") &&
                (*attached_parent.window).find("right") == attached_parent.right,
            "attached parent disposal must remove only its recursively disposed subtree");
    require((*attached_parent.window).metrics_snapshot().disposals == 2,
            "attached parent disposal must count every disposed visual owner");
}

void test_event_tokens_and_snapshot_rule() {
    Event<int> event;
    std::uint64_t owner_disposals = 0;
    std::shared_ptr<ProbeComponent> owner =
        std::make_shared<ProbeComponent>(owner_disposals);
    int owner_total = 0;
    SubscriptionToken owner_token =
        event.subscribe(*owner, AddToInteger(owner_total));
    event.emit(2);
    (*owner).dispose();
    require(!owner_token.connected(), "owner disposal must revoke owner-bound subscription");
    event.emit(2);
    require(owner_total == 2, "revoked owner callback must not be emitted again");

    Event<> disposal_event;
    std::shared_ptr<ProbeComponent> callback_owner =
        std::make_shared<ProbeComponent>(owner_disposals);
    std::vector<int> disposal_order;
    SubscriptionToken disposing_token = disposal_event.subscribe(
        *callback_owner,
        DisposeOwnerAfterRecording(disposal_order, callback_owner));
    SubscriptionToken skipped_token = disposal_event.subscribe(
        *callback_owner,
        callbacks::PushConstant<std::vector<int>, int>(disposal_order, 2));
    disposal_event.emit();
    require(disposal_order == std::vector<int>{1} && !disposing_token.connected() &&
                !skipped_token.connected(),
            "owner disposal inside callback must keep current call alive and skip revoked peers");

    Event<> snapshot_event;
    std::vector<int> order;
    std::optional<SubscriptionToken> second;
    std::optional<SubscriptionToken> added;
    SubscriptionToken first = snapshot_event.subscribe(
        FirstSnapshotCallback(snapshot_event, order, second, added));
    second.emplace(snapshot_event.subscribe(
        callbacks::PushConstant<std::vector<int>, int>(order, 2)));
    snapshot_event.emit();
    require(order == std::vector<int>{1},
            "disconnected pending callback must be skipped and added callback deferred");
    snapshot_event.emit();
    require(order == std::vector<int>({1, 1, 3}),
            "next emission must use current registration-order snapshot");
    const EventStatistics statistics = snapshot_event.statistics();
    require(statistics.subscriptions_connected == 3 &&
                statistics.subscriptions_disconnected == 1 &&
                statistics.callbacks_emitted == 3,
            "event statistics must report token activity deterministically");
    static_cast<void>(first);

    Event<> throwing_event;
    SubscriptionToken throwing_token = throwing_event.subscribe(
        Delegate<>::bind<throw_callback_failure>());
    bool exception_propagated = false;
    try {
        throwing_event.emit();
    } catch (const std::runtime_error&) {
        exception_propagated = true;
    }
    require(exception_propagated, "C++ event primitive must not silently swallow exceptions");
    throwing_token.disconnect();

    Event<> move_event;
    int move_callbacks = 0;
    SubscriptionToken replaced = move_event.subscribe(
        IncrementBy(move_callbacks, 1));
    SubscriptionToken replacement = move_event.subscribe(
        IncrementBy(move_callbacks, 10));
    replaced = std::move(replacement);
    move_event.emit();
    require(move_callbacks == 10,
            "move-assigning a token must disconnect the subscription it replaces");
}

void test_disposal_and_reparent_inside_callback() {
    WindowFixture fixture;
    (*fixture.child).pointer_action = DisposeFixtureChild(fixture);
    (*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                      {15.0, 15.0}});
    require((*fixture.child).is_disposed(), "event callback may dispose its target safely");
    require(!(*fixture.window).captured_control() && !(*fixture.window).pressed_control(),
            "handler-driven disposal must revoke press and capture");

    WindowFixture reparent;
    (*reparent.child).pointer_action = ReparentFixtureChild(reparent);
    (*reparent.window).dispatch_pointer({PointerAction::down, PointerButton::primary,
                                       {15.0, 15.0}});
    require((*reparent.child).parent() == reparent.right,
            "event callback may reparent target without stale route access");
    require(!(*reparent.window).captured_control() && !(*reparent.window).pressed_control(),
            "callback reparent must revoke old press and capture identity");
}

template <typename Mutation>
void verify_ineligibility_cleanup(Mutation mutation, const char* message) {
    WindowFixture fixture;
    fixture.press_child();
    mutation(fixture);
    require(!(*fixture.window).focused_control() && !(*fixture.window).captured_control() &&
                !(*fixture.window).pressed_control(), message);
    (*fixture.window).dispatch_pointer({PointerAction::up, PointerButton::primary,
                                      {15.0, 15.0}});
    (*fixture.window).dispatch_key({KeyAction::down, 36});
    (*fixture.window).dispatch_text({"x"});
    require((*fixture.child).activations == 0 && (*fixture.child).key_events == 0 &&
                (*fixture.child).text_events == 0,
            "ineligible control must receive no later activation, key, or text event");
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.focus_revocations == 1 && metrics.capture_revocations == 1 &&
                metrics.press_revocations == 1,
            "focus/capture/press revocations must be structured counts");
}

void test_complete_interaction_cleanup() {
    verify_ineligibility_cleanup(disable_child,
                                 "disable must revoke focus/capture/press");
    verify_ineligibility_cleanup(
        hide_left_parent,
        "ancestor hide must revoke descendant focus/capture/press");
    verify_ineligibility_cleanup(remove_child,
                                 "removal must revoke focus/capture/press");
    verify_ineligibility_cleanup(reparent_child,
                                 "reparent must revoke focus/capture/press");
    verify_ineligibility_cleanup(dispose_child,
                                 "disposal must revoke focus/capture/press");
}

void test_wrong_thread_rejection_is_transactional() {
    WindowFixture fixture;
    const Rect before = (*fixture.child).requested_bounds();
    const std::uint64_t pointer_events = (*fixture.child).pointer_events;
    NullPainter painter;
    std::uint64_t rejected = 0;

    run_on_wrong_thread<SetBoundsOperation>(SetBoundsOperation(fixture),
                                            rejected);
    run_on_wrong_thread<DispatchPointerOperation>(
        DispatchPointerOperation(fixture), rejected);
    run_on_wrong_thread<PerformLayoutOperation>(
        PerformLayoutOperation(fixture), rejected);
    run_on_wrong_thread<PaintOperation>(PaintOperation(fixture, painter),
                                        rejected);

    require(rejected == 4, "all wrong-thread operations must reject deterministically");
    require((*fixture.child).requested_bounds() == before &&
                (*fixture.child).pointer_events == pointer_events && painter.draws == 0,
            "wrong-thread rejection must leave mutation, dispatch, and paint state unchanged");
    require((*fixture.window).metrics_snapshot().rejected_wrong_thread_operations == 4,
            "wrong-thread rejections must be atomically observable");
}

void test_duplicate_ids_and_parent_cycles_fail_safely() {
    WindowFixture fixture;
    std::shared_ptr<ProbeControl> duplicate =
        make_control<ProbeControl>(StableId("child"));
    bool duplicate_failed = false;
    try {
        (*fixture.root).add_child(duplicate);
    } catch (const std::logic_error&) {
        duplicate_failed = true;
    }
    require(duplicate_failed && !(*duplicate).parent() && (*fixture.window).find("child") == fixture.child,
            "duplicate ID rejection must leave both trees consistent");

    bool cycle_failed = false;
    try {
        (*fixture.child).add_child(fixture.root);
    } catch (const std::logic_error&) {
        cycle_failed = true;
    }
    require(cycle_failed && (*fixture.child).parent() == fixture.left,
            "parent cycle rejection must leave existing parentage intact");
}

} // namespace

int main() {
    try {
        test_component_ownership_is_not_visual_parenting();
        test_visual_strong_and_weak_lifetime();
        test_disposal_contract_and_registry();
        test_event_tokens_and_snapshot_rule();
        test_disposal_and_reparent_inside_callback();
        test_complete_interaction_cleanup();
        test_wrong_thread_rejection_is_transactional();
        test_duplicate_ids_and_parent_cycles_fail_safely();
        std::cout << "gui_forms_retained_lifetime_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_retained_lifetime_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
