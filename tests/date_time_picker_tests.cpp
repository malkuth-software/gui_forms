#include "gui_forms/gui_forms.hpp"
#include "support/named_callbacks.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const SemanticNode* find_node(const std::vector<SemanticNode>& nodes,
                              std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        if (const SemanticNode* nested = find_node(node.children, stable_id)) {
            return nested;
        }
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

struct Fixture final {
    Fixture() : window(root, {520.0, 390.0}) {
        (*picker).set_requested_bounds({24.0, 32.0, 270.0, 29.0});
        (*picker).accessible_name = "Appointment date";
        (*picker).set_value({2026, 8U, 5U, 14U, 7U, 9U, 125U});
        (*root).add_child(picker);
        window.perform_layout();
        require(window.request_focus(picker), "date fixture must focus the picker");
    }

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("date.root"));
    std::shared_ptr<DateTimePicker> picker =
        make_control<DateTimePicker>(StableId("date.picker"));
    Window window;
};

class RecordCheckedState final {
public:
    explicit RecordCheckedState(std::vector<std::string>& order)
        : order_(order) {}

    void operator()(bool checked) const {
        order_.push_back(checked ? "checked" : "unchecked");
    }

private:
    std::vector<std::string>& order_;
};

class RecordDropDownState final {
public:
    explicit RecordDropDownState(std::vector<std::string>& events)
        : events_(events) {}

    void operator()(bool open) const {
        events_.push_back(open ? "open" : "close");
    }

private:
    std::vector<std::string>& events_;
};

class SetPickerValueOffThread final {
public:
    SetPickerValueOffThread(DateTimePicker& picker,
                            std::atomic<bool>& rejected)
        : picker_(picker), rejected_(rejected) {}

    void operator()() const {
        try {
            picker_.set_value({2027, 1U, 1U});
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    DateTimePicker& picker_;
    std::atomic<bool>& rejected_;
};

void test_civil_arithmetic_and_validation() {
    require(days_in_month(1900, 2U) == 28U &&
                days_in_month(2000, 2U) == 29U &&
                days_in_month(2024, 13U) == 0U,
            "Gregorian leap-year and invalid-month rules must be deterministic");
    require(valid_date_time({2024, 2U, 29U, 23U, 59U, 59U, 999U}) &&
                !valid_date_time({2023, 2U, 29U}) &&
                !valid_date_time({2024, 1U, 1U, 24U}),
            "DateTimeValue must validate both civil and clock fields");
    const DateTimeValue leap = add_days({2024, 2U, 28U, 9U, 30U}, 1);
    const DateTimeValue rollover = add_days({2024, 12U, 31U, 9U, 30U}, 1);
    require(leap == DateTimeValue{2024, 2U, 29U, 9U, 30U} &&
                rollover == DateTimeValue{2025, 1U, 1U, 9U, 30U} &&
                add_days(leap, -1) == DateTimeValue{2024, 2U, 28U, 9U, 30U} &&
                day_of_week({2026, 8U, 5U}) == 3U,
            "civil arithmetic must preserve clock fields and cross month/year edges");
}

void test_formats_provider_and_transactional_validation() {
    DateTimePicker picker(StableId("date.format"));
    picker.set_value({2026, 8U, 5U, 14U, 7U, 9U});
    require(picker.formatted_value() == "Wednesday, August 5, 2026",
            "long format must use owned day and month names");
    picker.set_format(DateTimePickerFormat::short_date);
    require(picker.formatted_value() == "8/5/2026",
            "short format must apply the provider pattern");
    picker.set_format(DateTimePickerFormat::time);
    require(picker.formatted_value() == "2:07 PM",
            "time format must implement twelve-hour designators");
    picker.set_custom_format("yyyy-MM-dd 'at' HH:mm:ss");
    picker.set_format(DateTimePickerFormat::custom);
    require(picker.formatted_value() == "2026-08-05 at 14:07:09",
            "custom tokens, quoted literals, and padding must compose");

    DateTimeFormatProvider alternate =
        DateTimeFormatProvider::english_united_states();
    alternate.month_names[7] = "Thermidor";
    alternate.long_date_pattern = "d MMMM yyyy";
    picker.set_format_provider(alternate);
    picker.set_format(DateTimePickerFormat::long_date);
    require(picker.formatted_value() == "5 Thermidor 2026",
            "a caller-owned provider must drive culture-like display without globals");

    DateTimeFormatProvider invalid = alternate;
    invalid.month_names[0].clear();
    bool rejected = false;
    try {
        picker.set_format_provider(std::move(invalid));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && picker.formatted_value() == "5 Thermidor 2026",
            "invalid provider text must be rejected before changing picker state");
}

void test_range_events_checkbox_and_spinner() {
    Fixture fixture;
    std::vector<std::string> order;
    SubscriptionToken checked = (*fixture.picker).checked_changed().subscribe(
        RecordCheckedState(order));
    SubscriptionToken value = (*fixture.picker).value_changed().subscribe(
        test_support::PushConstant<std::vector<std::string>, std::string,
                                   DateTimeValue>(order, "value"));
    (*fixture.picker).set_show_check_box(true);
    (*fixture.picker).set_checked(false);
    require(!(*fixture.picker).checked() && order == std::vector<std::string>{"unchecked"},
            "nullable display state must publish one checked change");
    require(fixture.window.dispatch_key(
                {KeyAction::down, PhysicalKey::up}) && (*fixture.picker).checked() &&
                (*fixture.picker).value().day == 6U &&
                order == std::vector<std::string>{"unchecked", "checked", "value"},
            "keyboard stepping must re-enable an unchecked value before changing it");

    (*fixture.picker).set_show_up_down(true);
    require(!fixture.window.perform_semantic_action(
                "date.picker", SemanticAction::expand) &&
                !(*fixture.picker).dropped_down(),
            "up-down mode must not claim a calendar expansion action");
    require(fixture.window.dispatch_key({KeyAction::down, PhysicalKey::left}) &&
                (*fixture.picker).value().day == 5U,
            "spinner keyboard directions must use the common stepping contract");

    (*fixture.picker).set_range({2026, 8U, 5U}, {2026, 8U, 7U, 23U, 59U, 59U, 999U});
    (*fixture.picker).set_value({2026, 8U, 7U});
    require(fixture.window.dispatch_key({KeyAction::down, PhysicalKey::up}) &&
                (*fixture.picker).value() == DateTimeValue{2026, 8U, 7U},
            "stepping at the range edge must clamp without a spurious value event");
}

void test_overlay_keyboard_commit_cancel_and_outside_dismissal() {
    Fixture fixture;
    (*fixture.picker).set_drop_down_alignment(DateTimeDropDownAlignment::right);
    std::vector<std::string> events;
    SubscriptionToken values = (*fixture.picker).value_changed().subscribe(
        test_support::PushConstant<std::vector<std::string>, std::string,
                                   DateTimeValue>(events, "value"));
    SubscriptionToken overlay = (*fixture.picker).drop_down_changed().subscribe(
        RecordDropDownState(events));

    require(fixture.window.perform_semantic_action(
                "date.picker", SemanticAction::expand) &&
                (*fixture.picker).dropped_down() &&
                fixture.window.focus_scope_depth() == 1U &&
                (*fixture.window.focused_control()).stable_id().value() ==
                    "date.picker.overlay.calendar",
            "expansion must open a retained calendar and contain keyboard focus");
    const SemanticSnapshot open = fixture.window.semantic_snapshot();
    const SemanticNode* calendar =
        find_node(open.roots, "date.picker.overlay.calendar");
    const SemanticNode* selected =
        find_node(open.roots, "date.picker.overlay.calendar.day.2026-08-05");
    require(calendar && (*calendar).role == SemanticRole::calendar &&
                (*calendar).bounds.x == 8.0 && selected &&
                (*selected).role == SemanticRole::date_cell &&
                has_semantic_state((*selected).states, SemanticState::selected),
            "calendar overlay must honor right alignment and expose stable virtual date cells and selection state");
    bool invalid_alignment{};
    try {
        (*fixture.picker).set_drop_down_alignment(
            static_cast<DateTimeDropDownAlignment>(99));
    } catch (const std::invalid_argument&) {
        invalid_alignment = true;
    }
    require(invalid_alignment &&
                (*fixture.picker).drop_down_alignment() ==
                    DateTimeDropDownAlignment::right,
            "DateTimePicker overlay alignment must reject invalid vocabulary transactionally");
    require(fixture.window.dispatch_key({KeyAction::down, PhysicalKey::right}) &&
                fixture.window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*fixture.picker).value().day == 6U && !(*fixture.picker).dropped_down() &&
                fixture.window.focus_scope_depth() == 0U &&
                events == std::vector<std::string>{"open", "value", "close"},
            "arrow plus Enter must commit once, close once, and restore focus in order");

    (*fixture.picker).set_dropped_down(true);
    require(fixture.window.dispatch_key({KeyAction::down, PhysicalKey::right}) &&
                fixture.window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                (*fixture.picker).value().day == 6U && !(*fixture.picker).dropped_down(),
            "Escape must discard overlay-local selection and close the focus scope");

    (*fixture.picker).set_dropped_down(true);
    const bool pressed = fixture.window.dispatch_pointer(
        {PointerAction::down, PointerButton::primary, {490.0, 360.0}});
    const SemanticSnapshot dismissed = fixture.window.semantic_snapshot();
    require(pressed &&
                !(*fixture.picker).dropped_down() &&
                !find_node(dismissed.roots,
                           "date.picker.overlay.calendar"),
            "outside press must dismiss the retained overlay and its semantic subtree");
}

void test_semantic_date_cell_commit_and_iso_set_value() {
    Fixture fixture;
    require(fixture.window.perform_semantic_action(
                "date.picker", SemanticAction::set_value, "2026-12-24") &&
                (*fixture.picker).value() ==
                    DateTimeValue{2026, 12U, 24U, 14U, 7U, 9U, 125U},
            "semantic ISO date input must preserve the existing clock component");
    require(!fixture.window.perform_semantic_action(
                "date.picker", SemanticAction::set_value, "2026-02-30"),
            "semantic ISO input must reject invalid civil dates");
    (*fixture.picker).set_value({2026, 8U, 5U});
    (*fixture.picker).set_dropped_down(true);
    require(fixture.window.perform_semantic_action(
                "date.picker.overlay.calendar.day.2026-08-20",
                SemanticAction::press) &&
                (*fixture.picker).value() == DateTimeValue{2026, 8U, 20U} &&
                !(*fixture.picker).dropped_down(),
            "semantic date-cell press must execute the same commit-and-close path");
}

void test_optional_semantic_toggle_and_value_activation() {
    Fixture fixture;
    (*fixture.picker).set_show_check_box(true);
    (*fixture.picker).set_checked(false);
    const SemanticDescriptor unchecked = (*fixture.picker).semantic_descriptor();
    require(std::find(unchecked.actions.begin(), unchecked.actions.end(),
                      SemanticAction::press) != unchecked.actions.end() &&
                !has_semantic_state(unchecked.states, SemanticState::checked),
            "optional picker semantics must expose an unchecked toggle action");
    require(fixture.window.perform_semantic_action(
                "date.picker", SemanticAction::press) && (*fixture.picker).checked(),
            "semantic press must toggle the same optional state as its check hit region");
    (*fixture.picker).set_checked(false);
    require(fixture.window.perform_semantic_action(
                "date.picker", SemanticAction::set_value, "2026-08-09") &&
                (*fixture.picker).checked() && (*fixture.picker).value().day == 9U,
            "semantic date assignment must activate an unchecked optional value");
}

void test_semantic_month_navigation_and_range_capability() {
    Fixture fixture;
    (*fixture.picker).set_dropped_down(true);
    SemanticSnapshot snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* previous =
        find_node(snapshot.roots, "date.picker.overlay.calendar.previous-month");
    const SemanticNode* next =
        find_node(snapshot.roots, "date.picker.overlay.calendar.next-month");
    require(previous && next &&
                has_semantic_state((*previous).states, SemanticState::enabled) &&
                has_semantic_state((*next).states, SemanticState::enabled) &&
                std::find((*next).actions.begin(), (*next).actions.end(),
                          SemanticAction::press) != (*next).actions.end(),
            "calendar header navigation must be exposed as enabled semantic buttons");
    require(fixture.window.perform_semantic_action(
                "date.picker.overlay.calendar.next-month", SemanticAction::press),
            "semantic next-month press must execute the calendar navigation path");
    snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* calendar =
        find_node(snapshot.roots, "date.picker.overlay.calendar");
    require(calendar && (*calendar).name == "September 2026" &&
                (*calendar).value == "Saturday, September 5, 2026" &&
                (*fixture.picker).value() ==
                    DateTimeValue{2026, 8U, 5U, 14U, 7U, 9U, 125U},
            "month navigation must remain overlay-local until an explicit commit");
    require(fixture.window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                !(*fixture.picker).dropped_down() &&
                (*fixture.picker).value().month == 8U,
            "cancelling after semantic month navigation must restore the owner value");

    (*fixture.picker).set_range({2026, 8U, 1U},
                              {2026, 8U, 31U, 23U, 59U, 59U, 999U});
    (*fixture.picker).set_dropped_down(true);
    snapshot = fixture.window.semantic_snapshot();
    previous = find_node(snapshot.roots,
                         "date.picker.overlay.calendar.previous-month");
    next = find_node(snapshot.roots,
                     "date.picker.overlay.calendar.next-month");
    require(previous && next &&
                !has_semantic_state((*previous).states, SemanticState::enabled) &&
                !has_semantic_state((*next).states, SemanticState::enabled) &&
                (*previous).actions.empty() && (*next).actions.empty() &&
                !fixture.window.perform_semantic_action(
                    "date.picker.overlay.calendar.previous-month",
                    SemanticAction::press) &&
                !fixture.window.perform_semantic_action(
                    "date.picker.overlay.calendar.next-month",
                    SemanticAction::press),
            "a single-month range must disable and reject both month buttons");
}

void test_owner_cleanup_and_ui_thread_enforcement() {
    Fixture fixture;
    (*fixture.picker).set_dropped_down(true);
    (*fixture.picker).visible = false;
    require(!(*fixture.picker).dropped_down() &&
                fixture.window.focus_scope_depth() == 0U,
            "hiding the overlay owner must synchronously revoke overlay and focus scope");

    (*fixture.picker).visible = true;
    (*fixture.picker).set_dropped_down(true);
    (*fixture.picker).dispose();
    require(fixture.window.focus_scope_depth() == 0U &&
                !fixture.window.find("date.picker.overlay.calendar"),
            "owner disposal must leave no retained calendar or focus scope");

    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("date.thread.root"));
    std::shared_ptr<gui_forms::DateTimePicker> picker = make_control<DateTimePicker>(StableId("date.thread.picker"));
    (*root).add_child(picker);
    Window window(root, {200.0, 80.0});
    std::atomic<bool> rejected{false};
    std::thread foreign(SetPickerValueOffThread(*picker, rejected));
    foreign.join();
    require(rejected && (*picker).value() == DateTimeValue{2000, 1U, 1U},
            "attached picker mutation must reject a foreign UI thread atomically");
}

} // namespace

int main() {
    try {
        test_civil_arithmetic_and_validation();
        test_formats_provider_and_transactional_validation();
        test_range_events_checkbox_and_spinner();
        test_overlay_keyboard_commit_cancel_and_outside_dismissal();
        test_semantic_date_cell_commit_and_iso_set_value();
        test_optional_semantic_toggle_and_value_activation();
        test_semantic_month_navigation_and_range_capability();
        test_owner_cleanup_and_ui_thread_enforcement();
        std::cout << "date time picker tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "date time picker tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
