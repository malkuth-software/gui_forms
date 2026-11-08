#include "gui_forms/gui_forms.hpp"
#include "support/named_callbacks.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    Fixture() : window(root, {420.0, 180.0}) {
        (*editor).set_requested_bounds({20.0, 20.0, 200.0, 30.0});
        (*destination).set_requested_bounds({20.0, 70.0, 120.0, 30.0});
        (*bypass).set_requested_bounds({155.0, 70.0, 120.0, 30.0});
        (*bypass).causes_validation = false;
        (*root).add_child(editor);
        (*root).add_child(destination);
        (*root).add_child(bypass);
        window.perform_layout();
    }

    std::shared_ptr<ContainerControl> root =
        make_control<ContainerControl>(StableId("validation.root"));
    std::shared_ptr<TextBox> editor =
        make_control<TextBox>(StableId("validation.editor"));
    std::shared_ptr<Button> destination =
        make_control<Button>(StableId("validation.destination"), "Apply");
    std::shared_ptr<Button> bypass =
        make_control<Button>(StableId("validation.bypass"), "Cancel");
    Window window;
};

class ObserveFocusValidation final {
public:
    ObserveFocusValidation(const Fixture& fixture,
                           std::vector<std::string>& order)
        : fixture_(fixture), order_(order) {}

    void operator()(ControlValidationEvent& event) const {
        require(event.control == fixture_.editor.get() &&
                    event.destination == fixture_.destination.get() &&
                    !event.bulk,
                "focus validation must identify the exact retained endpoints");
        order_.push_back("validating");
    }

private:
    const Fixture& fixture_;
    std::vector<std::string>& order_;
};

class AppendFocusTransition final {
public:
    AppendFocusTransition(std::vector<std::string>& order, bool expected,
                          const char* label)
        : order_(order), expected_(expected), label_(label) {}

    void operator()(bool focused) const {
        if (focused == expected_) {
            order_.emplace_back(label_);
        }
    }

private:
    std::vector<std::string>& order_;
    bool expected_;
    const char* label_;
};

class CancelValidationWhen final {
public:
    explicit CancelValidationWhen(bool& reject) : reject_(reject) {}

    void operator()(ControlValidationEvent& event) const {
        if (reject_) {
            event.cancel = true;
        }
    }

private:
    bool& reject_;
};

class RequestBypassDuringValidation final {
public:
    RequestBypassDuringValidation(Fixture& fixture, bool& nested_result)
        : fixture_(fixture), nested_result_(nested_result) {}

    void operator()(ControlValidationEvent&) const {
        nested_result_ = fixture_.window.request_focus(fixture_.bypass);
    }

private:
    Fixture& fixture_;
    bool& nested_result_;
};

class CountBulkValidation final {
public:
    explicit CountBulkValidation(std::size_t& count) : count_(count) {}

    void operator()(ControlValidationEvent& event) const {
        if (event.bulk) {
            ++count_;
        }
    }

private:
    std::size_t& count_;
};

class DisposeDuringValidation final {
public:
    explicit DisposeDuringValidation(TextBox& editor) : editor_(editor) {}

    void operator()(ControlValidationEvent&) const { editor_.dispose(); }

private:
    TextBox& editor_;
};

void test_focus_validation_order_cancellation_and_policy() {
    Fixture fixture;
    std::vector<std::string> order;
    SubscriptionToken validating = (*fixture.editor).validating().subscribe(
        ObserveFocusValidation(fixture, order));
    SubscriptionToken validated = (*fixture.editor).validated().subscribe(
        test_support::PushConstant<std::vector<std::string>, std::string>(
            order, "validated"));
    SubscriptionToken editor_focus = (*fixture.editor).focus_observed().subscribe(
        AppendFocusTransition(order, false, "lost"));
    SubscriptionToken destination_focus =
        (*fixture.destination).focus_observed().subscribe(
            AppendFocusTransition(order, true, "gained"));
    require(fixture.window.request_focus(fixture.editor) &&
                fixture.window.request_focus(fixture.destination) &&
                order == std::vector<std::string>{
                    "validating", "validated", "lost", "gained"},
            "successful validation must finish before deterministic focus notifications");

    require(fixture.window.request_focus(fixture.editor),
            "editor must regain focus for cancellation coverage");
    bool reject = true;
    SubscriptionToken cancel = (*fixture.editor).validating().subscribe(
        CancelValidationWhen(reject));
    require(!fixture.window.request_focus(fixture.destination) &&
                fixture.window.focused_control() == fixture.editor,
            "prevent-focus-change validation must retain focus after cancellation");
    std::size_t activations = 0U;
    SubscriptionToken clicked = (*fixture.destination).clicked().subscribe(
        test_support::IncrementCounter<std::size_t, ButtonBase&>(activations));
    const Rect destination_bounds = (*fixture.destination).absolute_bounds();
    const Point destination_point{
        destination_bounds.x + destination_bounds.width * 0.5,
        destination_bounds.y + destination_bounds.height * 0.5};
    const bool rejected_down = fixture.window.dispatch_pointer(
        {PointerAction::down, PointerButton::primary, destination_point});
    static_cast<void>(fixture.window.dispatch_pointer(
        {PointerAction::up, PointerButton::primary, destination_point}));
    require(rejected_down && activations == 0U &&
                fixture.window.focused_control() == fixture.editor,
            "a validation-rejected pointer press must not leak a Button activation");
    require(fixture.window.request_focus(fixture.bypass) &&
                fixture.window.focused_control() == fixture.bypass,
            "a destination with CausesValidation false must bypass automatic validation");

    require(fixture.window.request_focus(fixture.editor),
            "editor must regain focus for allow-focus policy coverage");
    (*fixture.root).set_auto_validate(AutoValidate::enable_allow_focus_change);
    std::shared_ptr<gui_forms::ContainerControl> nested = make_control<ContainerControl>(
        StableId("validation.nested.policy"));
    (*fixture.root).add_child(nested);
    require((*nested).effective_auto_validate() ==
                AutoValidate::enable_allow_focus_change,
            "nested containers must inherit the nearest authored AutoValidate policy");
    (*nested).set_auto_validate(AutoValidate::disable);
    require((*nested).effective_auto_validate() == AutoValidate::disable,
            "a nested authored AutoValidate policy must override its ancestor");
    require(fixture.window.request_focus(fixture.destination) &&
                fixture.window.focused_control() == fixture.destination,
            "allow-focus-change validation must report cancellation without trapping focus");
    const ValidationSnapshot snapshot = fixture.window.validation_snapshot();
    require(snapshot.cancelled >= 3U && snapshot.focus_moves_blocked == 2U,
            "validation cancellation and focus blocking must remain independently queryable");
    static_cast<void>(validating);
    static_cast<void>(validated);
    static_cast<void>(editor_focus);
    static_cast<void>(destination_focus);
    static_cast<void>(cancel);
    static_cast<void>(clicked);
}

void test_reentrancy_and_bulk_constraints_are_bounded() {
    Fixture fixture;
    require(fixture.window.request_focus(fixture.editor),
            "editor must focus for reentrancy coverage");
    bool nested_result = true;
    SubscriptionToken nested = (*fixture.editor).validating().subscribe(
        RequestBypassDuringValidation(fixture, nested_result));
    require(fixture.window.request_focus(fixture.destination) && !nested_result &&
                fixture.window.validation_snapshot().reentrant_requests_rejected == 1U,
            "validation-time focus mutation must be rejected without a nested focus pump");

    std::size_t bulk = 0U;
    SubscriptionToken editor_validation =
        (*fixture.editor).validating().subscribe(CountBulkValidation(bulk));
    SubscriptionToken destination_validation =
        (*fixture.destination).validating().subscribe(CountBulkValidation(bulk));
    (*fixture.destination).enabled = false;
    require((*fixture.root).validate_children(
                ValidationConstraints::selectable |
                ValidationConstraints::enabled) && bulk == 1U,
            "bulk validation constraints must skip disabled selectable controls while retaining deterministic traversal");
    // The editor and bypass are visited; bypass CausesValidation=false emits no
    // event, so only the editor contributes here after the earlier destination
    // validation subscription.
    require(fixture.window.validation_snapshot().bulk_controls_visited == 2U,
            "bulk validation must expose the exact number of selected controls");
    static_cast<void>(nested);
    static_cast<void>(editor_validation);
    static_cast<void>(destination_validation);
}

void test_validation_callback_disposal_rechecks_focus_endpoints() {
    Fixture fixture;
    require(fixture.window.request_focus(fixture.editor),
            "editor must focus for disposal recheck coverage");
    SubscriptionToken disposal = (*fixture.editor).validating().subscribe(
        DisposeDuringValidation(*fixture.editor));
    require(fixture.window.request_focus(fixture.destination) &&
                fixture.window.focused_control() == fixture.destination &&
                !(*fixture.editor).is_alive(),
            "disposing the previous endpoint during validation must not strand or dereference stale focus");
    static_cast<void>(disposal);
}

void test_on_validation_binding_and_error_provider_share_focus_transaction() {
    Fixture fixture;
    std::shared_ptr<gui_forms::BindingSource> source = std::make_shared<BindingSource>(fixture.window);
    BindingRecord first{"amount.first", {{"amount", 12.5}}, true};
    first.errors["amount"] = "Amount is outside the recommended range.";
    first.errors["profile.amount"] = "Profile amount requires review.";
    BindingRecord second{"amount.second", {{"amount", 8.0}}, true};
    (*source).set_records({first, second});

    BindingOptions options;
    options.formatting_enabled = true;
    options.data_source_update_mode = DataSourceUpdateMode::on_validation;
    const std::shared_ptr<Binding> binding = (*fixture.editor).data_bindings().add(
        "Text", source, "Amount", options);
    ErrorProvider errors(fixture.window);
    errors.set_blink_style(ErrorBlinkStyle::never_blink);
    errors.bind_to_data_and_errors(source);
    require(errors.error(*fixture.editor) ==
                "Amount is outside the recommended range.",
            "current-record errors must project through bindings onto their retained targets");
    errors.set_data_member("profile");
    require(errors.error(*fixture.editor) == "Profile amount requires review.",
            "ErrorProvider DataMember must select a scoped retained error path");
    errors.set_data_member({});

    require(fixture.window.request_focus(fixture.editor),
            "bound editor must focus");
    (*fixture.editor).text = "not-a-number";
    require(!fixture.window.request_focus(fixture.destination) &&
                fixture.window.focused_control() == fixture.editor &&
                (*source).current_field("amount") ==
                    std::optional<BindingValue>{12.5} &&
                errors.error(*fixture.editor).find("cannot convert") !=
                    std::string::npos,
            "failed OnValidation parsing must preserve source, retain focus, and surface the binding error");

    (*fixture.editor).text = "13.75";
    require(fixture.window.request_focus(fixture.destination) &&
                (*source).current_field("amount") ==
                    std::optional<BindingValue>{13.75} &&
                errors.error(*fixture.editor) ==
                    "Amount is outside the recommended range.",
            "successful focus validation must commit the source and clear only the transient binding failure");
    require((*source).move_next() && errors.error(*fixture.editor).empty(),
            "currency movement must replace stale record validation adornments");
    (*source).dispose();
    require(!errors.has_errors() && !errors.data_source(),
            "source disposal must synchronously revoke bound provider state");
    static_cast<void>(binding);
}

} // namespace

int main() {
    try {
        test_focus_validation_order_cancellation_and_policy();
        test_reentrancy_and_bulk_constraints_are_bounded();
        test_validation_callback_disposal_rechecks_focus_endpoints();
        test_on_validation_binding_and_error_provider_share_focus_transaction();
        std::cout << "validation tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "validation tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
