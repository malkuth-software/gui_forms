#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/user_control.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "support/named_callbacks.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class LifecycleProbe final : public Control {
public:
    LifecycleProbe(StableId id, std::vector<std::string>& trace)
        : Control(std::move(id)), trace_(&trace) {}

    bool saw_bound_subtree{};
    bool saw_unbound_subtree{};

protected:
    void on_attached_to_window() override {
        saw_bound_subtree = attached();
        for (const Control::Ptr& child : children()) {
            saw_bound_subtree = saw_bound_subtree && (*child).attached();
        }
        (*trace_).push_back("attach:" + std::string(stable_id().value()));
    }

    void on_detached_from_window() noexcept override {
        saw_unbound_subtree = !attached();
        for (const Control::Ptr& child : children()) {
            saw_unbound_subtree = saw_unbound_subtree && !(*child).attached();
        }
        (*trace_).push_back("detach:" + std::string(stable_id().value()));
    }

private:
    std::vector<std::string>* trace_{};
};

class ThrowingAttachControl final : public Control {
public:
    explicit ThrowingAttachControl(StableId id) : Control(std::move(id)) {}

    bool detached_after_failure{};

protected:
    void on_attached_to_window() override {
        throw std::runtime_error("declared attach failure");
    }
    void on_detached_from_window() noexcept override {
        detached_after_failure = !attached();
    }
};

class InitializationEventProbe final : public Control {
public:
    explicit InitializationEventProbe(StableId id) : Control(std::move(id)) {}

    void set_value(int value) {
        require_mutable();
        if (value_ == value) return;
        value_ = value;
        invalidate(Dirty::semantics);
        publish_change(value_changed_, value_);
    }
    [[nodiscard]] int value() const noexcept { return value_; }
    [[nodiscard]] Event<int>& value_changed() noexcept { return value_changed_; }

private:
    int value_{};
    Event<int> value_changed_;
};

class RejectStructuralMutationDuringLoad final {
public:
    RejectStructuralMutationDuringLoad(Panel& root, UserControl& composed,
                                       bool& structural_rejection)
        : root_(root), composed_(composed),
          structural_rejection_(structural_rejection) {}

    void operator()() const {
        try {
            static_cast<void>(root_.remove_child(composed_.runtime_id()));
        } catch (const std::logic_error&) {
            structural_rejection_ = true;
            throw;
        }
    }

private:
    Panel& root_;
    UserControl& composed_;
    bool& structural_rejection_;
};

class ObserveCommittedText final {
public:
    ObserveCommittedText(Label& label, std::uint64_t& text_events,
                         std::vector<std::string>& trace)
        : label_(label), text_events_(text_events), trace_(trace) {}

    void operator()(const std::string& value) const {
        ++text_events_;
        trace_.push_back("text:" + value);
        require(!label_.initializing() && label_.text.read() == "second",
                "deferred observers must see the committed final state");
    }

private:
    Label& label_;
    std::uint64_t& text_events_;
    std::vector<std::string>& trace_;
};

class ObserveInitializationCompletion final {
public:
    ObserveInitializationCompletion(std::uint64_t& completions,
                                    Dirty& completed_dirty,
                                    bool& completed_subtree,
                                    std::vector<std::string>& trace)
        : completions_(completions), completed_dirty_(completed_dirty),
          completed_subtree_(completed_subtree), trace_(trace) {}

    void operator()(Dirty dirty, bool subtree) const {
        ++completions_;
        completed_dirty_ = dirty;
        completed_subtree_ = subtree;
        trace_.push_back("complete");
    }

private:
    std::uint64_t& completions_;
    Dirty& completed_dirty_;
    bool& completed_subtree_;
    std::vector<std::string>& trace_;
};

class BeginInitializationOffThread final {
public:
    BeginInitializationOffThread(Label& label, bool& rejected)
        : label_(label), rejected_(rejected) {}

    void operator()() const {
        try {
            label_.begin_init();
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    Label& label_;
    bool& rejected_;
};

class RecordFocusObservation final {
public:
    explicit RecordFocusObservation(std::vector<bool>& trace) : trace_(trace) {}

    void operator()(bool focused) const { trace_.push_back(focused); }

private:
    std::vector<bool>& trace_;
};

class RecordCaptureObservation final {
public:
    explicit RecordCaptureObservation(std::vector<bool>& trace) : trace_(trace) {}

    void operator()(const PointerCaptureChange& change) const {
        trace_.push_back(change.captured);
    }

private:
    std::vector<bool>& trace_;
};

class CountFocusScopeClose final {
public:
    CountFocusScopeClose(FocusScopeId scope, std::uint64_t& closes)
        : scope_(scope), closes_(closes) {}

    void operator()(const FocusScopeChange& change) const {
        if (!change.opened && change.scope == scope_) {
            ++closes_;
        }
    }

private:
    FocusScopeId scope_;
    std::uint64_t& closes_;
};

void test_attach_detach_order_and_observable_state() {
    std::vector<std::string> trace;
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("lifecycle.root"));
    std::shared_ptr<LifecycleProbe> parent =
        make_control<LifecycleProbe>(StableId("lifecycle.parent"), trace);
    std::shared_ptr<LifecycleProbe> child =
        make_control<LifecycleProbe>(StableId("lifecycle.child"), trace);
    (*parent).add_child(child);
    (*root).add_child(parent);
    Window window(root, {200.0, 100.0});

    require(trace == std::vector<std::string>{"attach:lifecycle.parent",
                                              "attach:lifecycle.child"} &&
                (*parent).saw_bound_subtree && (*child).saw_bound_subtree,
            "attach callbacks must be parent-first after the complete subtree is bound");

    trace.clear();
    Control::Ptr detached = (*root).remove_child((*parent).runtime_id());
    require(detached == parent &&
                trace == std::vector<std::string>{"detach:lifecycle.child",
                                                  "detach:lifecycle.parent"} &&
                (*parent).saw_unbound_subtree && (*child).saw_unbound_subtree,
            "detach callbacks must be child-first after the complete subtree is unbound");
}

void test_user_control_load_once_across_reparent() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("user.root"));
    std::shared_ptr<gui_forms::Panel> left = make_control<Panel>(StableId("user.left"));
    std::shared_ptr<gui_forms::Panel> right = make_control<Panel>(StableId("user.right"));
    std::shared_ptr<gui_forms::UserControl> composed = make_control<UserControl>(StableId("user.composed"));
    std::shared_ptr<gui_forms::Label> field = make_control<Label>(StableId("user.composed.field"), "ready");
    (*composed).add_child(field);
    (*left).add_child(composed);
    (*root).add_child(left);
    (*root).add_child(right);

    std::uint64_t loads = 0;
    SubscriptionToken load = (*composed).loaded().subscribe(
        test_support::IncrementCounter<std::uint64_t>(loads));
    Window window(root, {200.0, 100.0});
    require(loads == 1 && (*composed).is_loaded() && (*composed).is_attached() &&
                (*composed).attachment_count() == 1,
            "UserControl must load once on its first successful attachment");

    (*right).add_child(composed);
    require((*composed).parent() == right && loads == 1 && (*composed).is_attached() &&
                (*composed).attachment_count() == 2,
            "same-window reparent must detach and reattach without repeating Load");

    static_cast<void>((*right).remove_child((*composed).runtime_id()));
    require(!(*composed).is_attached() && loads == 1,
            "detached UserControl must retain its once-per-lifetime loaded state");
}

void test_failed_attach_rolls_back_binding_and_identity() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("rollback.root"));
    Window window(root, {100.0, 60.0});
    std::shared_ptr<ThrowingAttachControl> failing =
        make_control<ThrowingAttachControl>(StableId("rollback.child"));
    bool threw = false;
    try {
        (*root).add_child(failing);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw && !(*failing).attached() && !(*failing).parent() &&
                (*failing).detached_after_failure && !window.find("rollback.child"),
            "failed attachment must roll back binding, parentage, and stable-ID registration");

    std::shared_ptr<gui_forms::Label> replacement = make_control<Label>(StableId("rollback.child"), "replacement");
    (*root).add_child(replacement);
    require(window.find("rollback.child") == replacement,
            "a stable ID from a failed attachment must be immediately reusable");

    std::shared_ptr<gui_forms::UserControl> composed = make_control<UserControl>(StableId("rollback.composed"));
    std::shared_ptr<ThrowingAttachControl> nested_failure =
        make_control<ThrowingAttachControl>(StableId("rollback.composed.failure"));
    (*composed).add_child(nested_failure);
    std::uint64_t loads = 0;
    SubscriptionToken load = (*composed).loaded().subscribe(
        test_support::IncrementCounter<std::uint64_t>(loads));
    bool nested_threw = false;
    try {
        (*root).add_child(composed);
    } catch (const std::runtime_error&) {
        nested_threw = true;
    }
    require(nested_threw && loads == 1 && (*composed).is_loaded() &&
                (*composed).attachment_count() == 0 && !(*composed).attached(),
            "a later descendant failure cannot un-observe Load but must not commit attachment");
    static_cast<void>((*composed).remove_child((*nested_failure).runtime_id()));
    (*root).add_child(composed);
    require(loads == 1 && (*composed).is_loaded() &&
                (*composed).attachment_count() == 1,
            "retry after subtree rollback must commit attachment without repeating Load");
}

void test_lifecycle_callbacks_cannot_mutate_tree_structure() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("guard.root"));
    Window window(root, {100.0, 60.0});
    std::shared_ptr<gui_forms::UserControl> composed = make_control<UserControl>(StableId("guard.composed"));
    bool structural_rejection = false;
    SubscriptionToken load = (*composed).loaded().subscribe(
        RejectStructuralMutationDuringLoad(
            *root, *composed, structural_rejection));
    bool attach_rejected = false;
    try {
        (*root).add_child(composed);
    } catch (const std::logic_error&) {
        attach_rejected = true;
    }
    require(structural_rejection && attach_rejected && !(*composed).attached() &&
                !(*composed).parent() && !window.find("guard.composed"),
            "lifecycle callbacks must reject structural mutation and roll back attachment");
}

void test_nested_initialization_defers_and_coalesces_observers() {
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("init.label"), "initial");
    (*label).set_requested_bounds({0.0, 0.0, 120.0, 24.0});
    Window window(label, {120.0, 24.0});
    window.perform_layout();
    window.reset_activity_metrics();

    std::uint64_t text_events = 0;
    std::uint64_t completions = 0;
    Dirty completed_dirty = Dirty::none;
    bool completed_subtree = false;
    std::vector<std::string> trace;
    SubscriptionToken text = (*label).text_changed().subscribe(
        ObserveCommittedText(*label, text_events, trace));
    SubscriptionToken complete = (*label).initialization_completed().subscribe(
        ObserveInitializationCompletion(
            completions, completed_dirty, completed_subtree, trace));

    (*label).begin_init();
    (*label).text = "first";
    (*label).begin_init();
    (*label).text = "second";
    (*label).invalidate_subtree(Dirty::semantics);
    (*label).end_init();
    require(text_events == 0 && completions == 0 &&
                window.metrics_snapshot().mutations == 0,
            "nested initialization must not publish callbacks into partial state");
    (*label).end_init();

    require(text_events == 1 && completions == 1 &&
                trace == std::vector<std::string>{"text:second", "complete"} &&
                completed_subtree &&
                has_dirty(completed_dirty, invalidation::text_content) &&
                has_dirty(completed_dirty, Dirty::semantics) &&
                window.metrics_snapshot().mutations == 1,
            "outer EndInit must commit dirtiness, publish one final property value, then complete");

    bool underflow = false;
    try {
        (*label).end_init();
    } catch (const std::logic_error&) {
        underflow = true;
    }
    require(underflow, "EndInit without BeginInit must be rejected");

    std::shared_ptr<InitializationEventProbe> custom =
        make_control<InitializationEventProbe>(
            StableId("init.custom.publication"));
    std::vector<int> custom_values;
    SubscriptionToken custom_changed = (*custom).value_changed().subscribe(
        test_support::PushBack<std::vector<int>, int>(custom_values));
    (*custom).begin_init();
    (*custom).set_value(1);
    (*custom).set_value(2);
    require(custom_values.empty(),
            "native custom controls must share the initialization publication boundary");
    (*custom).end_init();
    (*custom).set_value(3);
    require(custom_values == std::vector<int>{2, 3},
            "custom state events must coalesce in initialization and remain synchronous outside it");
}

void test_initialization_disposal_and_thread_guards() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("init.dispose.root"));
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("init.dispose.label"), "alive");
    (*root).add_child(label);
    Window window(root, {100.0, 40.0});
    std::uint64_t completions = 0;
    SubscriptionToken complete = (*label).initialization_completed().subscribe(
        test_support::IncrementCounter<std::uint64_t, Dirty, bool>(completions));
    (*label).begin_init();
    (*label).text = "pending";
    (*label).dispose();
    require((*label).is_disposed() && completions == 0,
            "disposal during initialization must discard pending completion work");

    std::shared_ptr<gui_forms::Label> attached = make_control<Label>(StableId("init.thread"), "thread");
    Window thread_window(attached, {100.0, 24.0});
    bool rejected = false;
    std::thread worker(BeginInitializationOffThread(*attached, rejected));
    worker.join();
    require(rejected && (*attached).initialization_depth() == 0,
            "initialization scopes must preserve attached-control UI-thread enforcement");
}

void test_initialization_revokes_interaction_until_commit() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("init.interaction.root"));
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("init.interaction.button"),
                                       "Ready");
    (*button).set_requested_bounds({10.0, 8.0, 80.0, 24.0});
    (*root).add_child(button);
    Window window(root, {120.0, 48.0});
    window.perform_layout();
    const Rect button_bounds = (*button).absolute_bounds();
    const Point button_center{
        button_bounds.x + button_bounds.width * 0.5,
        button_bounds.y + button_bounds.height * 0.5};
    require(window.request_focus(button),
            "initialization interaction specimen requires focus");
    window.capture_pointer(button, 1U);
    const FocusScopeId scope = window.begin_focus_scope(button, button);
    std::shared_ptr<gui_forms::Panel> overlay = make_control<Panel>(StableId("init.interaction.overlay"));
    (*overlay).set_requested_bounds({12.0, 12.0, 60.0, 24.0});
    OverlayToken overlay_token = window.open_overlay(button, overlay);

    std::uint64_t clicks{};
    std::uint64_t overlay_closes{};
    std::uint64_t scope_closes{};
    std::vector<bool> focus_trace;
    std::vector<bool> capture_trace;
    SubscriptionToken clicked = (*button).clicked().subscribe(
        test_support::IncrementCounter<std::uint64_t, ButtonBase&>(clicks));
    SubscriptionToken focused = (*button).focus_observed().subscribe(
        RecordFocusObservation(focus_trace));
    SubscriptionToken captured = window.pointer_capture_changed().subscribe(
        RecordCaptureObservation(capture_trace));
    SubscriptionToken overlay_closed = (*overlay_token.closed_event()).subscribe(
        test_support::IncrementCounter<std::uint64_t>(overlay_closes));
    SubscriptionToken scope_changed = window.focus_scope_changed().subscribe(
        CountFocusScopeClose(scope, scope_closes));

    (*button).begin_init();
    require(!(*button).eligible_for_input() && !window.focused_control() &&
                !window.captured_control() && focus_trace.empty() &&
                capture_trace.empty() && !overlay_token.connected() &&
                overlay_closes == 0U && window.focus_scope_depth() == 0U &&
                scope_closes == 0U &&
                !window.dispatch_pointer(
                    {PointerAction::down, PointerButton::primary,
                     button_center, {}, Modifier::none, 1U}),
            "BeginInit must revoke focus/capture and block routed input without callbacks");
    window.release_pointer();
    capture_trace.clear();
    (*button).end_init();
    require((*button).eligible_for_input() &&
                focus_trace == std::vector<bool>{false} &&
                capture_trace == std::vector<bool>{false} &&
                overlay_closes == 1U && scope_closes == 1U,
            "EndInit must publish deferred eligibility effects only after commit");
    const bool down = window.dispatch_pointer(
        {PointerAction::down, PointerButton::primary,
         button_center, {}, Modifier::none, 1U});
    const bool up = window.dispatch_pointer(
        {PointerAction::up, PointerButton::primary,
         button_center, {}, Modifier::none, 1U});
    require(down && up && clicks == 1U,
            "committed controls must resume ordinary interaction deterministically");
}

} // namespace

int main() {
    try {
        test_attach_detach_order_and_observable_state();
        test_user_control_load_once_across_reparent();
        test_failed_attach_rolls_back_binding_and_identity();
        test_lifecycle_callbacks_cannot_mutate_tree_structure();
        test_nested_initialization_defers_and_coalesces_observers();
        test_initialization_disposal_and_thread_guards();
        test_initialization_revokes_interaction_until_commit();
        std::cout << "lifecycle controls tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "lifecycle controls test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
