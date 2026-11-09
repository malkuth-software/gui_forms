// W-4 controls: Badge, InfoBanner, EmptyState, the segmented ChoiceGroup
// presentation, Toast with its window presenter, ToggleSwitch, SearchField,
// Spinner and ProgressRing. Each test covers the control's behaviour, its refusals, its
// semantics, its published parts and states, and the capability cases: a
// write stores, an equal write does nothing, and a write from another
// thread is refused before anything is compared or stored.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/controls/collections/choice_group.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_display_controls_tests: " << message << '\n';
    std::exit(1);
}

// Writes a badge's count from another thread and records whether it was
// refused.
struct OtherThreadCount final {
    Badge* badge{};
    bool* refused{};
    void operator()() const {
        try {
            (*badge).count = std::uint32_t{5U};
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void test_badge_text_and_visibility() {
    std::shared_ptr<Badge> badge = make_control<Badge>(StableId("display.badge"));
    require(!(*badge).showing() && (*badge).display_text().empty(), "a zero count hides the badge");
    require((*badge).measure(Size{200.0, 50.0}) == (Size{0.0, 0.0}), "a hidden badge measures empty");
    (*badge).show_zero = true;
    require((*badge).showing() && (*badge).display_text() == "0", "show_zero shows a zero count");
    (*badge).count = std::uint32_t{42U};
    require((*badge).display_text() == "42", "a count up to the maximum is shown as it is");
    (*badge).count = std::uint32_t{100U};
    require((*badge).display_text() == "99+", "a count past the maximum shows the maximum and +");
    (*badge).maximum = std::uint32_t{9U};
    require((*badge).display_text() == "9+", "a smaller maximum caps sooner");
    bool refused = false;
    try {
        (*badge).maximum = std::uint32_t{0U};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*badge).maximum.read() == 9U, "a zero maximum is refused and the old one kept");
    (*badge).mode = BadgeMode::dot;
    require((*badge).showing() && (*badge).display_text().empty(), "dot mode shows a dot without text");
    const Size dot = (*badge).measure(Size{200.0, 50.0});
    require(dot.width > 0.0 && dot.width == dot.height, "a dot is square");
    const std::optional<BindingValue> by_name = (*badge).property_value("count");
    require(by_name && *by_name == BindingValue{std::uint64_t{100U}}, "the table reads the count cell");
    (*badge).set_property_value("mode", BindingValue{PropertyEnumValue{"badge_mode", "count", 0}});
    require((*badge).mode.read() == BadgeMode::count, "the table writes the mode cell");
}

void test_badge_semantics_parts_and_threads() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.root"));
    std::shared_ptr<Badge> badge = make_control<Badge>(StableId("display.badge.attached"));
    (*badge).set_requested_bounds(Rect{10.0, 10.0, 60.0, 24.0});
    (*root).add_child(badge);
    Window window(root, Size{200.0, 100.0});
    window.perform_layout();

    SemanticDescriptor hidden = (*badge).semantic_descriptor();
    require(hidden.role == SemanticRole::static_text && !hidden.exposed, "a hidden badge is not exposed");
    require(!(*badge).parts().chip.present && !(*badge).parts().label.present, "a hidden badge publishes no chip");

    (*badge).count = std::uint32_t{3U};
    window.perform_layout();
    const SemanticDescriptor shown = (*badge).semantic_descriptor();
    require(shown.exposed && shown.name == "3", "a shown badge is named by its text");
    (*badge).accessible_name = "3 new alerts";
    require((*badge).semantic_descriptor().name == "3 new alerts", "an accessible name wins over the text");
    const Badge::Parts counted = (*badge).parts();
    require(counted.chip.present && counted.label.present, "a count badge publishes its chip and label");

    const std::optional<CustomStateIndex> dot = find_custom_state((*badge).anatomy(), "dot");
    require(dot && !(*badge).anatomy_states().contains(*dot), "count mode is not the dot state");
    (*badge).mode = BadgeMode::dot;
    window.perform_layout();
    require((*badge).anatomy_states().contains(*dot), "dot mode publishes the dot state");
    const Badge::Parts dotted = (*badge).parts();
    require(dotted.chip.present && !dotted.label.present, "a dot publishes a chip without a label");
    require(!(*badge).hit_test_local(Point{4.0, 4.0}), "a badge takes no pointer input");

    bool refused = false;
    std::thread worker(OtherThreadCount{badge.get(), &refused});
    worker.join();
    require(refused && (*badge).count.read() == 3U, "a count write from another thread is refused");
}

// Counts an InfoBanner's closed events.
class ClosedRecorder final : public Component {
public:
    void on_closed() { ++closed; }
    int closed{};
};

// Writes a banner's title from another thread and records whether it was
// refused.
struct OtherThreadTitle final {
    InfoBanner* banner{};
    bool* refused{};
    void operator()() const {
        try {
            (*banner).title = std::string{"Elsewhere"};
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void test_info_banner_properties_and_states() {
    std::shared_ptr<InfoBanner> banner = make_control<InfoBanner>(StableId("display.banner"));
    require((*banner).severity.read() == NoticeSeverity::information && (*banner).closable.read(),
            "a banner starts as closable information");
    require((*banner).focusable.read(), "a closable banner takes focus");
    const ControlAnatomy& anatomy = (*banner).anatomy();
    const std::optional<CustomStateIndex> information = find_custom_state(anatomy, "information");
    const std::optional<CustomStateIndex> warning = find_custom_state(anatomy, "warning");
    const std::optional<CustomStateIndex> success = find_custom_state(anatomy, "success");
    const std::optional<CustomStateIndex> error = find_custom_state(anatomy, "error");
    require(information && warning && success && error, "the banner declares a custom state per severity");
    AnatomyStateSet states = (*banner).anatomy_states();
    require(states.contains(*information) && !states.contains(*warning) && !states.contains(*success) &&
                !states.contains(*error),
            "information is the one severity state at first");
    (*banner).severity = NoticeSeverity::error;
    states = (*banner).anatomy_states();
    require(states.contains(*error) && !states.contains(*information), "a new severity replaces the old state");

    bool refused = false;
    const std::string broken{"\xff\xfe"};
    try {
        (*banner).message = broken;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*banner).message.read().empty(), "an invalid UTF-8 message is refused");

    (*banner).title = std::string{"Saved"};
    (*banner).message = std::string{"Your changes are saved."};
    const SemanticDescriptor described = (*banner).semantic_descriptor();
    require(described.role == SemanticRole::live_region && described.exposed, "a banner is an exposed live region");
    require(described.name == "Saved" && described.description == "Your changes are saved.",
            "the title names the banner and the message describes it");
    require(described.actions.size() == 1U && described.actions[0] == SemanticAction::close,
            "a closable banner offers the close action");

    (*banner).closable = false;
    require(!(*banner).focusable.read() && (*banner).semantic_descriptor().actions.empty(),
            "a banner that is not closable takes no focus and offers no close action");
    require(!(*banner).on_semantic_action(SemanticAction::close, std::string_view{}) && (*banner).visible.read(),
            "the close action is refused while the banner is not closable");

    const std::optional<BindingValue> by_name = (*banner).property_value("severity");
    require(by_name && *by_name == BindingValue{PropertyEnumValue{"notice_severity", "error", 3}},
            "the table reads the severity cell");
    (*banner).set_property_value("severity", BindingValue{PropertyEnumValue{"notice_severity", "success", 2}});
    require((*banner).severity.read() == NoticeSeverity::success, "the table writes the severity cell");
}

void test_info_banner_closing() {
    ClosedRecorder recorder;
    std::shared_ptr<InfoBanner> banner = make_control<InfoBanner>(StableId("display.banner.closing"));
    on((*banner).closed(), recorder, &ClosedRecorder::on_closed);

    require((*banner).on_semantic_action(SemanticAction::close, std::string_view{}), "the close action is taken");
    require(!(*banner).visible.read() && recorder.closed == 1, "closing hides the banner and raises closed once");
    (*banner).close();
    require(recorder.closed == 1, "closing a hidden banner does nothing");

    (*banner).visible = true;
    KeyEvent other{};
    other.action = KeyAction::down;
    other.physical_key = PhysicalKey::enter;
    (*banner).on_key(other);
    require((*banner).visible.read() && !other.handled, "keys other than Escape pass the banner by");
    KeyEvent escape{};
    escape.action = KeyAction::down;
    escape.physical_key = PhysicalKey::escape;
    (*banner).on_key(escape);
    require(escape.handled && !(*banner).visible.read() && recorder.closed == 2, "Escape closes a shown banner");

    (*banner).visible = true;
    (*banner).closable = false;
    (*banner).close();
    require(!(*banner).visible.read() && recorder.closed == 3, "close() works whether or not the banner is closable");
}

void test_info_banner_actuator_action_and_threads() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.banner.root"));
    std::shared_ptr<InfoBanner> banner = make_control<InfoBanner>(StableId("display.banner.attached"));
    (*banner).set_requested_bounds(Rect{0.0, 0.0, 400.0, 60.0});
    (*banner).title = std::string{"Offline"};
    std::shared_ptr<Button> retry = make_control<Button>(StableId("display.banner.retry"), "Retry");
    const Control::Ptr none = (*banner).set_action(retry);
    require(!none && (*retry).parent() == banner, "the banner owns its action control");
    (*root).add_child(banner);
    Window window(root, Size{500.0, 200.0});
    window.perform_layout();

    const InfoBanner::Parts laid = (*banner).parts();
    require(laid.face.present && laid.keyline.present && laid.icon.present, "the face, keyline and icon are published");
    require(laid.caption.present && !laid.label.present, "an empty message publishes no label");
    require(laid.actuator.present && laid.content.present, "the close mark and the action slot are published");
    const Rect slot = (*retry).committed_arranged_bounds();
    require(slot.width > 0.0 && slot.x + slot.width <= 400.0, "the action is placed inside the banner");

    const std::shared_ptr<Panel> elsewhere = make_control<Panel>(StableId("display.banner.elsewhere"));
    std::shared_ptr<Button> adopted = make_control<Button>(StableId("display.banner.adopted"), "Other");
    (*elsewhere).add_child(adopted);
    bool refused = false;
    try {
        static_cast<void>((*banner).set_action(adopted));
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused && (*banner).action() == retry, "an action with a parent is refused and the old one kept");

    ClosedRecorder recorder;
    on((*banner).closed(), recorder, &ClosedRecorder::on_closed);
    const Point mark{laid.actuator.bounds.x + laid.actuator.bounds.width * 0.5,
                     laid.actuator.bounds.y + laid.actuator.bounds.height * 0.5};
    PointerEvent press{};
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = mark;
    (*banner).on_pointer(press);
    require(press.handled && (*banner).visible.read(), "pressing the close mark does not close yet");
    PointerEvent release = press;
    release.action = PointerAction::up;
    release.handled = false;
    (*banner).on_pointer(release);
    require(release.handled && !(*banner).visible.read() && recorder.closed == 1,
            "releasing over the close mark closes the banner");

    const Control::Ptr detached = (*banner).set_action(nullptr);
    require(detached == retry && !(*retry).parent() && !(*banner).action(), "clearing the action returns it detached");

    bool other_thread = false;
    std::thread worker(OtherThreadTitle{banner.get(), &other_thread});
    worker.join();
    require(other_thread && (*banner).title.read() == "Offline", "a title write from another thread is refused");
}

void test_empty_state() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.empty.root"));
    std::shared_ptr<EmptyState> empty = make_control<EmptyState>(StableId("display.empty"));
    (*empty).set_requested_bounds(Rect{0.0, 0.0, 300.0, 240.0});
    (*root).add_child(empty);
    Window window(root, Size{400.0, 300.0});
    window.perform_layout();

    const EmptyState::Parts bare = (*empty).parts();
    require(bare.region.present && !bare.icon.present && !bare.caption.present && !bare.label.present &&
                !bare.content.present,
            "an empty EmptyState publishes only its region");
    std::shared_ptr<EmptyState> unsized = make_control<EmptyState>(StableId("display.empty.unsized"));
    require((*unsized).measure(Size{300.0, 240.0}) == (Size{0.0, 0.0}), "nothing to show measures empty");

    bool refused = false;
    try {
        (*empty).title = std::string{"\xc3"};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*empty).title.read().empty(), "an invalid UTF-8 title is refused");

    (*empty).image = ImageId{3U};
    (*empty).title = std::string{"No messages"};
    (*empty).description = std::string{"Messages you receive appear here."};
    std::shared_ptr<Button> compose = make_control<Button>(StableId("display.empty.compose"), "Write a message");
    const Control::Ptr none = (*empty).set_action(compose);
    require(!none && (*compose).parent() == empty, "the control owns its action");
    window.perform_layout();

    const EmptyState::Parts full = (*empty).parts();
    require(full.icon.present && full.caption.present && full.label.present && full.content.present,
            "each piece with something to show is published");
    require(full.icon.bounds.y < full.caption.bounds.y && full.caption.bounds.y < full.label.bounds.y &&
                full.label.bounds.y < full.content.bounds.y,
            "the picture, title, description and action stack in order");
    const double icon_centre = full.icon.bounds.x + full.icon.bounds.width * 0.5;
    require(std::abs(icon_centre - 150.0) <= 1.0, "the column is centred across the control");
    const double top_space = full.icon.bounds.y;
    const double bottom_space = 240.0 - (full.content.bounds.y + full.content.bounds.height);
    require(std::abs(top_space - bottom_space) <= 1.0, "the column is centred down the control");
    const Rect slot = (*compose).committed_arranged_bounds();
    require(slot.width > 0.0 && slot.y == full.content.bounds.y, "the action is placed in its slot");

    const SemanticDescriptor described = (*empty).semantic_descriptor();
    require(described.role == SemanticRole::group && described.exposed && described.include_descendants,
            "the control is an exposed group around its action");
    require(described.name == "No messages" && described.description == "Messages you receive appear here.",
            "the title names the control and the description describes it");
    require(!(*empty).hit_test_local(Point{150.0, 10.0}), "the control takes no pointer input itself");
    require((*empty).property_value("image") == std::optional<BindingValue>{BindingValue{ImageId{3U}}},
            "the table reads the image cell");

    const Control::Ptr detached = (*empty).set_action(nullptr);
    require(detached == compose && !(*compose).parent(), "clearing the action returns it detached");
}

// Writes a choice group's presentation from another thread and records
// whether it was refused.
struct OtherThreadPresentation final {
    ChoiceGroup* group{};
    bool* refused{};
    void operator()() const {
        try {
            (*group).presentation = ChoiceGroupPresentation::buttons;
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

// At scale 2 the segment edges round to half units (device pixels): the
// inner 298 units are 596 pixels, edges at 4, 203, 401 and 600 pixels.
void test_segment_edges_at_scale() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.segments.scaled.root"));
    std::shared_ptr<ChoiceGroup> group = make_control<ChoiceGroup>(StableId("display.segments.scaled"));
    (*group).set_requested_bounds(Rect{0.0, 0.0, 302.0, 32.0});
    (*group).set_items({ChoiceItem{.id = 1, .label = "Day"}, ChoiceItem{.id = 2, .label = "Week"},
                        ChoiceItem{.id = 3, .label = "Month"}});
    (*group).presentation = ChoiceGroupPresentation::segmented;
    (*root).add_child(group);
    Window window(root, Size{400.0, 100.0});
    window.set_scale(2.0);
    window.perform_layout();
    const Rect first = (*group).item(0).committed_arranged_bounds();
    const Rect second = (*group).item(1).committed_arranged_bounds();
    const Rect third = (*group).item(2).committed_arranged_bounds();
    require(first.x == 2.0 && first.width == 99.5 && second.x == 101.5 && second.width == 99.0 &&
                third.x == 200.5 && third.width == 99.5,
            "segment edges round to device pixels at the window's scale");
}

void test_segmented_choice_group() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.segments.root"));
    std::shared_ptr<ChoiceGroup> group = make_control<ChoiceGroup>(StableId("display.segments"));
    (*group).set_requested_bounds(Rect{0.0, 0.0, 302.0, 32.0});
    (*group).set_items({ChoiceItem{.id = 1, .label = "Day"}, ChoiceItem{.id = 2, .label = "Week"},
                        ChoiceItem{.id = 3, .label = "Month"}});
    (*root).add_child(group);
    Window window(root, Size{400.0, 100.0});
    window.perform_layout();

    const std::optional<CustomStateIndex> segmented = find_custom_state((*group).anatomy(), "segmented");
    require(segmented && !(*group).anatomy_states().contains(*segmented), "buttons are not the segmented state");
    require((*group).item(0).committed_arranged_bounds().x == 0.0, "buttons start at the group's edge");
    require(!(*group).parts().track.present && !(*group).parts().selection.present,
            "the buttons presentation publishes no track or selection");

    (*group).presentation = ChoiceGroupPresentation::segmented;
    window.perform_layout();
    require((*group).anatomy_states().contains(*segmented), "the segmented presentation publishes its state");
    const Rect first = (*group).item(0).committed_arranged_bounds();
    const Rect second = (*group).item(1).committed_arranged_bounds();
    require(first.x == 2.0 && first.y == 2.0 && first.width == 99.0 && first.height == 28.0 && second.x == 101.0,
            "segments share the track inside its inset");
    // 298 / 3 is not whole: the edges round to 2, 101, 201 and 300, so the
    // segments tile and differ by at most one pixel.
    const Rect third = (*group).item(2).committed_arranged_bounds();
    require(second.width == 100.0 && third.x == 201.0 && third.width == 99.0,
            "segment edges are rounded to device pixels and tile the track");
    require((*group).parts().track.present && !(*group).parts().selection.present,
            "a segmented group with nothing selected publishes its track and no selection");

    (*group).selected_index = 1;
    window.perform_layout();
    const ChoiceGroup::Parts chosen = (*group).parts();
    require(chosen.selection.present && chosen.selection.bounds == second,
            "the selection indicator lies behind the selected segment");
    const SemanticDescriptor described = (*group).semantic_descriptor();
    const SemanticDescriptor segment = (*group).item(1).semantic_descriptor();
    require(described.role == SemanticRole::radio_group && segment.role == SemanticRole::radio_button &&
                has_semantic_state(segment.states, SemanticState::checked),
            "segments keep the radio-group semantics");

    KeyEvent right{};
    right.action = KeyAction::down;
    right.physical_key = PhysicalKey::right;
    (*group).on_key(right);
    window.perform_layout();
    require(right.handled && (*group).selected_index.read() == 2 &&
                (*group).parts().selection.bounds == (*group).item(2).committed_arranged_bounds(),
            "the arrow keys move the selection and the indicator follows");

    const std::optional<BindingValue> by_name = (*group).property_value("presentation");
    require(by_name && *by_name == BindingValue{PropertyEnumValue{"choice_group_presentation", "segmented", 1}},
            "the table reads the presentation cell");

    bool refused = false;
    std::thread worker(OtherThreadPresentation{group.get(), &refused});
    worker.join();
    require(refused && (*group).presentation.read() == ChoiceGroupPresentation::segmented,
            "a presentation write from another thread is refused");

    (*group).set_property_value("presentation",
                                BindingValue{PropertyEnumValue{"choice_group_presentation", "buttons", 0}});
    window.perform_layout();
    require((*group).item(0).committed_arranged_bounds().x == 0.0 && !(*group).parts().selection.present &&
                (*group).selected_index.read() == 2,
            "back to buttons, the items fill the group again and the selection is kept");
}


// Records each toast dismissal with the toast that showed when it was
// raised, so that a test can check the event comes last.
struct DismissalRecorder final {
    Window* window{};
    std::vector<ToastDismissal> seen{};
    std::vector<std::uint64_t> showing_when_raised{};
    void on_dismissed(const ToastDismissal& dismissal) {
        seen.push_back(dismissal);
        showing_when_raised.push_back((*window).toast_id());
    }
};

struct ActionRecorder final {
    int invoked{};
    void on_invoked() { ++invoked; }
};

std::shared_ptr<Toast> make_toast(const char* id, const char* action_text, bool closable) {
    ToastContent content{};
    content.message = "Message archived.";
    content.action_text = action_text;
    content.severity = NoticeSeverity::warning;
    content.closable = closable;
    std::shared_ptr<Toast> toast = make_control<Toast>(StableId(std::string(id)), std::move(content));
    return toast;
}

void test_toast_content_and_semantics() {
    bool empty_refused = false;
    try {
        static_cast<void>(make_control<Toast>(StableId("display.toast.empty"), ToastContent{}));
    } catch (const std::invalid_argument&) {
        empty_refused = true;
    }
    require(empty_refused, "a toast without a message is refused");
    bool invalid_refused = false;
    try {
        ToastContent invalid{};
        invalid.message = "Bad \xff byte";
        static_cast<void>(make_control<Toast>(StableId("display.toast.invalid"), std::move(invalid)));
    } catch (const std::invalid_argument&) {
        invalid_refused = true;
    }
    require(invalid_refused, "a message that is not UTF-8 is refused");

    const std::shared_ptr<Toast> plain = make_toast("display.toast.plain", "", false);
    require(!(*plain).action() && (*plain).children().empty(), "a toast without action text has no button");
    const SemanticDescriptor plain_semantics = (*plain).semantic_descriptor();
    require(plain_semantics.role == SemanticRole::live_region && plain_semantics.name == "Message archived." &&
                plain_semantics.actions.empty(),
            "a toast is a live region named by its message, without close when not closable");
    require(plain_semantics.exposed && plain_semantics.announces_on_appearance,
            "a toast publishes its node and announces its message as it appears");
    require(!(*plain).on_semantic_action(SemanticAction::close, std::string_view{}),
            "close does nothing on a toast that cannot be closed");

    const std::shared_ptr<Toast> full = make_toast("display.toast.full", "Undo", true);
    const std::shared_ptr<Button> undo = std::dynamic_pointer_cast<Button>((*full).action());
    require(undo && (*undo).parent() == full && (*undo).stable_id().value() == "display.toast.full.action",
            "the action text makes a button inside the toast");
    require((*full).semantic_descriptor().actions.size() == 1U, "a closable toast offers the close action");
    require((*full).anatomy_states().contains(CustomStateIndex{static_cast<std::uint8_t>(NoticeSeverity::warning)}),
            "the severity is the toast's custom state");
    require(!(*full).close(), "closing a toast in no window does nothing");
    const Size natural = (*full).measure(Size{1000.0, 1000.0});
    const Size narrow = (*make_toast("display.toast.narrow", "", false)).measure(Size{1000.0, 1000.0});
    require(natural.width > narrow.width && natural.height > 0.0, "the action and close mark widen the toast");
}

void test_window_toasts() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.toasts.root"));
    const std::shared_ptr<Button> owner = make_control<Button>(StableId("display.toasts.owner"), "Archive");
    (*owner).set_requested_bounds(Rect{10.0, 10.0, 120.0, 30.0});
    (*root).add_child(owner);
    Window window(root, Size{600.0, 400.0});
    window.perform_layout();
    DismissalRecorder recorder;
    recorder.window = &window;
    const SubscriptionToken dismissed_token = window.toast_dismissed().subscribe(
        Delegate<const ToastDismissal&>::bind<DismissalRecorder, &DismissalRecorder::on_dismissed>(recorder));
    require(!window.toast_visible() && window.toast_id() == 0U && !window.toast_content(), "no toast shows at first");

    const std::shared_ptr<Toast> first = make_toast("display.toasts.first", "Undo", true);
    const std::uint64_t first_id = window.show_toast(owner, first, std::chrono::milliseconds{0});
    window.perform_layout();
    require(first_id == 1U && window.toast_visible() && window.toast_content() == first,
            "the first toast shows with ID 1");
    require(!window.next_wake().has_value(), "a toast without a duration arms no wake");
    const Rect placed = (*first).committed_arranged_bounds();
    require(placed.width > 0.0 && placed.y + placed.height == 400.0 - 16.0 &&
                placed.x == std::floor((600.0 - placed.width) * 0.5),
            "the toast is centred 16 units above the bottom of the client area");
    require(window.focused_control() != first && window.focused_control() != (*first).action(),
            "showing a toast takes no focus");

    window.resize(Size{800.0, 300.0});
    window.perform_layout();
    const Rect moved = (*first).committed_arranged_bounds();
    require(moved.y + moved.height == 300.0 - 16.0 && moved.x == std::floor((800.0 - moved.width) * 0.5),
            "a resized window places its toast again");

    const std::shared_ptr<Toast> second = make_toast("display.toasts.second", "", true);
    const std::uint64_t second_id = window.show_toast(owner, second, std::chrono::milliseconds{0});
    require(second_id == 2U && window.toast_content() == second && !(*first).parent() &&
                (*first).attached_surface_root() == nullptr,
            "a new toast replaces the one that shows, which leaves detached");
    require(recorder.seen.size() == 1U && recorder.seen[0].id == 1U &&
                recorder.seen[0].reason == ToastDismissReason::replaced && recorder.showing_when_raised[0] == 2U,
            "the replaced dismissal is raised after the new toast shows");
    require(!(*first).close(), "a toast that no longer shows does nothing when closed");

    require((*second).on_semantic_action(SemanticAction::close, std::string_view{}) && !window.toast_visible(),
            "the close action dismisses the toast");
    require(recorder.seen.size() == 2U && recorder.seen[1].reason == ToastDismissReason::closed &&
                recorder.showing_when_raised[1] == 0U,
            "closing raises closed once nothing shows");

    // The detached first toast may be shown again; its action dismisses it.
    ActionRecorder actions;
    const SubscriptionToken invoked_token =
        (*first).action_invoked().subscribe(Delegate<>::bind<ActionRecorder, &ActionRecorder::on_invoked>(actions));
    const std::uint64_t third_id = window.show_toast(owner, first, std::chrono::milliseconds{0});
    window.perform_layout();
    const std::shared_ptr<Button> undo = std::dynamic_pointer_cast<Button>((*first).action());
    require(third_id == 3U && undo && (*undo).perform_click(), "a detached toast shows again and its action clicks");
    require(actions.invoked == 1 && !window.toast_visible() && recorder.seen.size() == 3U &&
                recorder.seen[2].reason == ToastDismissReason::action,
            "the action raises action_invoked, then dismisses the toast");

    const std::shared_ptr<Toast> marked = make_toast("display.toasts.marked", "", true);
    static_cast<void>(window.show_toast(owner, marked, std::chrono::milliseconds{0}));
    window.perform_layout();
    const Toast::Parts parts = (*marked).parts();
    require(parts.face.present && parts.icon.present && parts.label.present && parts.close.present &&
                !parts.actuator.present,
            "the toast publishes its face, mark, message and close mark, and no action slot");
    const Point mark{parts.close.bounds.x + parts.close.bounds.width * 0.5,
                     parts.close.bounds.y + parts.close.bounds.height * 0.5};
    PointerEvent press{};
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = mark;
    (*marked).on_pointer(press);
    require(press.handled && window.toast_visible(), "pressing the close mark does not close yet");
    PointerEvent release = press;
    release.action = PointerAction::up;
    release.handled = false;
    (*marked).on_pointer(release);
    require(release.handled && !window.toast_visible() && recorder.seen.back().reason == ToastDismissReason::closed,
            "releasing over the close mark dismisses the toast");

    const std::shared_ptr<Toast> timed = make_toast("display.toasts.timed", "", true);
    static_cast<void>(window.show_toast(owner, timed, std::chrono::milliseconds{50}));
    const std::optional<FrameTime> wake = window.next_wake();
    require(wake.has_value(), "a toast with a duration arms a wake");
    static_cast<void>(window.poll_frame_schedule(*wake));
    require(!window.toast_visible() && recorder.seen.back().reason == ToastDismissReason::timed_out &&
                !window.next_wake().has_value(),
            "the wake closes the toast when its duration runs out");

    const std::shared_ptr<Toast> hidden = make_toast("display.toasts.hidden", "", true);
    static_cast<void>(window.show_toast(owner, hidden));
    window.hide_toast();
    require(!window.toast_visible() && recorder.seen.back().reason == ToastDismissReason::hidden &&
                !window.next_wake().has_value(),
            "hide_toast closes the toast and drops its wake");
    const std::size_t before_idle_hide = recorder.seen.size();
    window.hide_toast();
    require(recorder.seen.size() == before_idle_hide, "hiding with nothing showing raises nothing");

    const std::shared_ptr<Toast> orphaned = make_toast("display.toasts.orphaned", "", true);
    static_cast<void>(window.show_toast(owner, orphaned, std::chrono::milliseconds{0}));
    (*owner).enabled = false;
    require(window.toast_visible(), "a toast is passive: disabling its owner keeps it");
    const Control::Ptr removed = (*root).remove_child((*owner).runtime_id());
    require(removed == owner && !window.toast_visible() && !(*orphaned).parent() &&
                recorder.seen.back().reason == ToastDismissReason::owner_gone,
            "an owner leaving the window closes its toast");
}

// Each refusal is std::invalid_argument and changes nothing.
void test_window_toast_refusals() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.toast-refusals.root"));
    const std::shared_ptr<Button> owner = make_control<Button>(StableId("display.toast-refusals.owner"), "Owner");
    (*root).add_child(owner);
    Window window(root, Size{400.0, 300.0});
    window.perform_layout();
    const std::shared_ptr<Toast> showing = make_toast("display.toast-refusals.showing", "", true);
    static_cast<void>(window.show_toast(owner, showing, std::chrono::milliseconds{0}));

    int refused = 0;
    const std::shared_ptr<Button> stranger = make_control<Button>(StableId("display.toast-refusals.stranger"), "No");
    const std::shared_ptr<Toast> fresh = make_toast("display.toast-refusals.fresh", "", true);
    try {
        static_cast<void>(window.show_toast(nullptr, fresh));
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    try {
        static_cast<void>(window.show_toast(stranger, fresh));
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    try {
        static_cast<void>(window.show_toast(owner, nullptr));
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    try {
        static_cast<void>(window.show_toast(owner, showing));
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    const std::shared_ptr<Panel> holder = make_control<Panel>(StableId("display.toast-refusals.holder"));
    const std::shared_ptr<Toast> held = make_toast("display.toast-refusals.held", "", true);
    (*holder).add_child(held);
    try {
        static_cast<void>(window.show_toast(owner, held));
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    try {
        static_cast<void>(window.show_toast(owner, fresh, std::chrono::milliseconds{-1}));
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    require(refused == 6, "every invalid show_toast is refused");
    require(window.toast_content() == showing && window.toast_id() == 1U && !(*fresh).parent(),
            "a refused show_toast leaves the showing toast in place");
    require(!window.dismiss_toast(*fresh, ToastDismissReason::closed) && window.toast_visible(),
            "dismissing content that does not show changes nothing");
}

// The motion clock's time, set by the test (engine_pop_tests' pattern), so
// that no motion test depends on the host's clock. Declared before the
// window whose clock borrows it, so it outlives the installation.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// Advances the test's time by `step` and polls the window there.
void advance(Window& window, ManualTime& time, std::chrono::milliseconds step) {
    time.time += step;
    static_cast<void>(window.poll_frame_schedule(time.time));
}

// Records the order of a switch's checked_changed and clicked events.
struct SwitchRecorder final {
    std::vector<std::string> seen{};
    void on_checked(bool value) { seen.push_back(value ? "checked:on" : "checked:off"); }
    void on_clicked(ButtonBase&) { seen.push_back("clicked"); }
};

struct OtherThreadChecked final {
    ToggleSwitch* toggle{};
    bool* refused{};
    void operator()() const {
        try {
            (*toggle).checked = true;
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void test_toggle_switch() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.switch.root"));
    const std::shared_ptr<ToggleSwitch> toggle = make_control<ToggleSwitch>(StableId("display.switch"), "Wi-Fi");
    (*toggle).set_requested_bounds(Rect{10.0, 10.0, 240.0, 28.0});
    (*root).add_child(toggle);
    ManualTime time;
    Window window(root, Size{400.0, 200.0});
    window.motion_clock().set_time_source(&time);
    window.perform_layout();
    SwitchRecorder recorder;
    const SubscriptionToken checked_token =
        (*toggle).checked_changed().subscribe(Delegate<bool>::bind<SwitchRecorder, &SwitchRecorder::on_checked>(recorder));
    const SubscriptionToken clicked_token = (*toggle).clicked().subscribe(
        Delegate<ButtonBase&>::bind<SwitchRecorder, &SwitchRecorder::on_clicked>(recorder));

    require(!(*toggle).checked.read() && (*toggle).state_text().empty(), "a new switch is off with no state text");
    const ToggleSwitch::Parts off_parts = (*toggle).parts();
    require(off_parts.track.present && off_parts.thumb.present && off_parts.label.present && !off_parts.state.present,
            "the track, thumb and label are published; empty state text is not");

    (*toggle).checked = true;
    require(recorder.seen.size() == 1U && recorder.seen[0] == "checked:on", "a write raises checked_changed once");
    // The thumb travels on the motion clock; a frame past the move's end
    // settles it.
    advance(window, time, std::chrono::milliseconds(150));
    (*toggle).checked = true;
    require(recorder.seen.size() == 1U, "an equal write raises nothing");
    require((*toggle).anatomy_states().contains(AnatomyState::checked), "an on switch is in the checked state");
    window.perform_layout();
    const ToggleSwitch::Parts on_parts = (*toggle).parts();
    require(on_parts.thumb.bounds.x > off_parts.thumb.bounds.x, "the thumb moves to the track's end when on");

    (*toggle).on_text = std::string{"On"};
    (*toggle).off_text = std::string{"Off"};
    require((*toggle).state_text() == "On", "an on switch shows on_text");
    bool invalid_refused = false;
    try {
        (*toggle).on_text = std::string{"\xff"};
    } catch (const std::invalid_argument&) {
        invalid_refused = true;
    }
    require(invalid_refused && (*toggle).on_text.read() == "On", "state text that is not UTF-8 is refused");
    const Size on_size = (*toggle).measure(Size{1000.0, 100.0});

    recorder.seen.clear();
    require((*toggle).perform_click(), "the switch clicks");
    require(!(*toggle).checked.read() && recorder.seen.size() == 2U && recorder.seen[0] == "checked:off" &&
                recorder.seen[1] == "clicked",
            "activation turns the switch over, then raises clicked");
    require((*toggle).state_text() == "Off", "an off switch shows off_text");
    require((*toggle).measure(Size{1000.0, 100.0}) == on_size, "turning the switch does not change its size");
    window.perform_layout();
    require((*toggle).parts().state.present, "state text is published once set");

    const SemanticDescriptor off_semantics = (*toggle).semantic_descriptor();
    require(off_semantics.role == SemanticRole::toggle_switch &&
                semantic_role_fallback(SemanticRole::toggle_switch) == SemanticRole::check_box &&
                has_semantic_state(off_semantics.states, SemanticState::checkable) &&
                !has_semantic_state(off_semantics.states, SemanticState::checked) &&
                off_semantics.value == "unchecked" && off_semantics.name == "Wi-Fi",
            "the switch is a toggle_switch named by its label, unchecked while off");
    require(window.perform_semantic_action("display.switch", SemanticAction::press) && (*toggle).checked.read() &&
                (*toggle).semantic_descriptor().value == "checked",
            "the press action turns the switch on");

    bool other_thread = false;
    (*toggle).checked = false;
    std::thread worker(OtherThreadChecked{toggle.get(), &other_thread});
    worker.join();
    require(other_thread && !(*toggle).checked.read(), "a write from another thread is refused");
}

// Records a search field's events in order.
struct SearchRecorder final {
    std::vector<std::string> seen{};
    // The subscriptions watch made; they end with the recorder.
    std::vector<SubscriptionToken> tokens{};
    void on_text(const std::string& value) { seen.push_back("text:" + value); }
    void on_search(const std::string& query) { seen.push_back("search:" + query); }
    void on_cleared() { seen.push_back("cleared"); }
};

void watch(SearchField& field, SearchRecorder& recorder) {
    recorder.tokens.push_back(field.text_changed().subscribe(
        Delegate<const std::string&>::bind<SearchRecorder, &SearchRecorder::on_text>(recorder)));
    recorder.tokens.push_back(field.search_requested().subscribe(
        Delegate<const std::string&>::bind<SearchRecorder, &SearchRecorder::on_search>(recorder)));
    recorder.tokens.push_back(
        field.cleared().subscribe(Delegate<>::bind<SearchRecorder, &SearchRecorder::on_cleared>(recorder)));
}

void test_search_field_alone() {
    const std::shared_ptr<SearchField> field = make_control<SearchField>(StableId("display.search.alone"));
    const std::shared_ptr<TextBox> editor = (*field).editor();
    require(editor && (*editor).parent() == field && (*editor).stable_id().value() == "display.search.alone.editor" &&
                !(*field).focusable.read(),
            "the field owns its editor, which takes the focus instead of the field");
    SearchRecorder recorder;
    watch(*field, recorder);

    (*field).text = std::string{"maps"};
    require((*editor).text.read() == "maps" && recorder.seen.size() == 2U && recorder.seen[0] == "text:maps" &&
                recorder.seen[1] == "search:maps",
            "outside a window a change raises text_changed, then searches at once");
    require((*editor).accepts_escape.read(), "the editor takes Escape while the field has text");
    (*field).placeholder_text = std::string{"Search"};
    require((*editor).placeholder_text.read() == "Search" && (*field).semantic_descriptor().description == "Search",
            "the placeholder is the editor's and describes the field");

    recorder.seen.clear();
    require((*field).clear() && (*field).text.read().empty() && !(*editor).accepts_escape.read(),
            "clear empties the field and lets Escape go on");
    require(recorder.seen.size() == 4U && recorder.seen[0] == "text:" && recorder.seen[2] == "cleared" &&
                recorder.seen[3] == "search:",
            "clearing raises text_changed, cleared, then the empty query");
    recorder.seen.clear();
    require(!(*field).clear() && recorder.seen.empty(), "clearing an empty field raises nothing");

    int refused = 0;
    try {
        (*field).search_delay = std::chrono::milliseconds{-1};
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    try {
        (*field).search_delay = std::chrono::milliseconds{10001};
    } catch (const std::invalid_argument&) {
        ++refused;
    }
    require(refused == 2 && (*field).search_delay.read() == std::chrono::milliseconds{300},
            "a negative or over-long delay is refused and the default kept");

    (*field).busy = true;
    const SemanticDescriptor busy_semantics = (*field).semantic_descriptor();
    require((*field).anatomy_states().contains(AnatomyState::pending) &&
                has_semantic_state(busy_semantics.states, SemanticState::busy) &&
                busy_semantics.role == SemanticRole::search_box &&
                semantic_role_fallback(SemanticRole::search_box) == SemanticRole::text_box,
            "a busy field is pending, and busy to assistive technology as a search_box");
    require(!(*field).on_semantic_action(SemanticAction::set_value, "trains") && (*field).text.read().empty(),
            "outside a window the set-value action is refused, as for any control not eligible for input");
}

void test_search_field_in_window() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.search.root"));
    const std::shared_ptr<SearchField> field = make_control<SearchField>(StableId("display.search"));
    (*field).set_requested_bounds(Rect{10.0, 10.0, 300.0, 32.0});
    (*root).add_child(field);
    Window window(root, Size{400.0, 200.0});
    window.perform_layout();
    SearchRecorder recorder;
    watch(*field, recorder);
    (*field).search_delay = std::chrono::milliseconds{200};

    const SearchField::Parts empty_parts = (*field).parts();
    require(empty_parts.well.present && empty_parts.icon.present && empty_parts.content.present &&
                !empty_parts.actuator.present && !empty_parts.activity.present,
            "an empty, idle field publishes no clear mark and no activity strip");
    const Rect slot = (*(*field).editor()).committed_arranged_bounds();
    require(slot.width > 0.0 && slot.x > empty_parts.icon.bounds.x, "the editor sits after the search mark");

    (*field).text = std::string{"r"};
    (*field).text = std::string{"ri"};
    require(recorder.seen.size() == 2U && recorder.seen[1] == "text:ri", "typing does not search before the pause");
    const std::optional<FrameTime> wake = window.next_wake();
    require(wake.has_value(), "a change arms the search delay");
    static_cast<void>(window.poll_frame_schedule(*wake));
    require(recorder.seen.size() == 3U && recorder.seen[2] == "search:ri" && !window.next_wake().has_value(),
            "once the pause is over the field searches once, for the latest text");

    (*field).busy = true;
    window.perform_layout();
    const SearchField::Parts busy_parts = (*field).parts();
    require(busy_parts.actuator.present && busy_parts.activity.present,
            "a field with text shows its clear mark, and a busy one its activity strip");
    require((*field).on_semantic_action(SemanticAction::set_value, "trains") && (*field).text.read() == "trains" &&
                (*field).semantic_descriptor().value == "trains",
            "the set-value action writes the query");
    require(!(*field).on_semantic_action(SemanticAction::set_value, "\xff") && (*field).text.read() == "trains",
            "a set value that is not UTF-8 is refused");
    (*field).text = std::string{"ri"};

    require(window.request_focus((*field).editor()) && (*field).anatomy_states().contains(AnatomyState::focused),
            "the editor's focus is the field's focused state");
    recorder.seen.clear();
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}) && recorder.seen.size() == 1U &&
                recorder.seen[0] == "search:ri",
            "Enter searches now");
    recorder.seen.clear();
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && (*field).text.read().empty() &&
                recorder.seen.size() == 3U && recorder.seen[1] == "cleared" && recorder.seen[2] == "search:",
            "Escape clears the field and searches for everything");
    // The focused editor's caret keeps a wake of its own, so the dropped delay
    // shows as no further search once the pause has passed.
    static_cast<void>(
        window.poll_frame_schedule(FrameClock::now() + (*field).search_delay.read() + std::chrono::seconds(1)));
    require(recorder.seen.size() == 3U, "Escape leaves no search delay armed");

    (*field).text = std::string{"x"};
    window.perform_layout();
    const SearchField::Parts parts = (*field).parts();
    const Point mark{parts.actuator.bounds.x + parts.actuator.bounds.width * 0.5,
                     parts.actuator.bounds.y + parts.actuator.bounds.height * 0.5};
    PointerEvent press{};
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = mark;
    (*field).on_pointer(press);
    require(press.handled && (*field).text.read() == "x", "pressing the clear mark does not clear yet");
    PointerEvent release = press;
    release.action = PointerAction::up;
    release.handled = false;
    (*field).on_pointer(release);
    require(release.handled && (*field).text.read().empty(), "releasing over the clear mark clears the field");
}

// Draws nothing: on_paint runs for its effects on the motion clock.
class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_linear_gradient_spread(Rect, Point, Point, std::span<const GradientStop>, GradientSpreadMode) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

void set_reduced_motion(Window& window, bool reduced) {
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = reduced;
    window.set_presentation_settings(settings);
}

// The thumb travels on the motion clock and settles at its end; reduced
// motion puts it there at once, and outside a window it jumps.
void test_toggle_switch_motion() {
    const std::shared_ptr<ToggleSwitch> loose = make_control<ToggleSwitch>(StableId("display.switch.loose"), "Loose");
    (*loose).checked = true;
    require((*loose).thumb_position() == 1.0, "outside a window the thumb stands at its end at once");

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.switch-motion.root"));
    const std::shared_ptr<ToggleSwitch> toggle = make_control<ToggleSwitch>(StableId("display.switch.motion"), "Moving");
    (*toggle).set_requested_bounds(Rect{10.0, 10.0, 200.0, 28.0});
    (*root).add_child(toggle);
    ManualTime time;
    Window window(root, Size{300.0, 100.0});
    window.motion_clock().set_time_source(&time);
    window.perform_layout();
    MotionClock& clock = window.motion_clock();
    require((*toggle).thumb_position() == 0.0 && clock.moving_count() == 0U, "an off switch rests at the start");

    (*toggle).checked = true;
    require((*toggle).checked.read() && (*toggle).thumb_position() < 1.0 && clock.moving_count() == 1U &&
                window.next_wake().has_value(),
            "turning the switch on starts the thumb's travel; the state changes at once");
    advance(window, time, std::chrono::milliseconds(75));
    const double midway = (*toggle).thumb_position();
    require(midway == 0.5, "halfway through the 150 ms smoothstep the thumb is halfway");
    advance(window, time, std::chrono::milliseconds(75));
    require((*toggle).thumb_position() == 1.0 && clock.moving_count() == 0U && !window.next_wake().has_value(),
            "past the move's end the thumb settles and the window rests");

    (*toggle).checked = false;
    require(clock.moving_count() == 1U, "turning it off starts the travel back");
    set_reduced_motion(window, true);
    require((*toggle).thumb_position() == 0.0 && clock.moving_count() == 0U,
            "turning reduced motion on puts the moving thumb at its end");
    (*toggle).checked = true;
    require((*toggle).thumb_position() == 1.0 && clock.moving_count() == 0U && !window.next_wake().has_value(),
            "under reduced motion the thumb has no travel");
    window.perform_layout();
    const ToggleSwitch::Parts parts = (*toggle).parts();
    require(parts.thumb.bounds.x + parts.thumb.bounds.width + 3.0 == parts.track.bounds.x + parts.track.bounds.width,
            "the thumb stands at the track's end, one inset in");
}

// The busy strip's segment runs on the motion clock while the field is
// drawn; it drops off the clock when the field is not drawn, and is a
// still, full strip under reduced motion.
void test_search_field_busy_motion() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.search-motion.root"));
    const std::shared_ptr<SearchField> field = make_control<SearchField>(StableId("display.search.motion"));
    (*field).set_requested_bounds(Rect{10.0, 10.0, 300.0, 32.0});
    (*root).add_child(field);
    ManualTime time;
    Window window(root, Size{400.0, 100.0});
    window.motion_clock().set_time_source(&time);
    window.perform_layout();
    MotionClock& clock = window.motion_clock();
    const Rect bounds{0.0, 0.0, 300.0, 32.0};
    NullPainter idle_painter;
    (*field).on_paint(idle_painter, bounds);
    require(!clock.control_ambient(*field) && !window.next_wake().has_value(), "an idle field is not on the clock");

    (*field).busy = true;
    NullPainter first;
    (*field).on_paint(first, bounds);
    const SearchField::Parts at_start = (*field).parts();
    require(clock.control_ambient(*field) && window.next_wake().has_value() && at_start.activity.present &&
                at_start.activity.bounds.width < 296.0,
            "a drawn busy field sweeps a segment on the motion clock");
    advance(window, time, std::chrono::milliseconds(300));
    const SearchField::Parts later = (*field).parts();
    // A quarter of the 1.2 s sweep: a quarter of the strip less the segment.
    const double expected_x = at_start.activity.bounds.x + (296.0 - 296.0 * busy_segment_share) * 0.25;
    require(std::abs(later.activity.bounds.x - expected_x) < 1e-9,
            "a frame moves the segment along the strip by the elapsed share of the sweep");
    advance(window, time, std::chrono::milliseconds(300));
    advance(window, time, std::chrono::milliseconds(300));
    require(!clock.control_ambient(*field), "a field not drawn between frames drops off the clock");

    set_reduced_motion(window, true);
    NullPainter still;
    (*field).on_paint(still, bounds);
    const SearchField::Parts reduced = (*field).parts();
    require(!clock.control_ambient(*field) && reduced.activity.present && reduced.activity.bounds.width == 296.0,
            "under reduced motion the strip is still and as wide as the field");
    set_reduced_motion(window, false);
    NullPainter again;
    (*field).on_paint(again, bounds);
    require(clock.control_ambient(*field), "with motion allowed again the next paint sweeps");
    (*field).busy = false;
    require(!clock.control_ambient(*field) && !(*field).parts().activity.present,
            "turning busy off withdraws the strip and its motion");
}


// Writes a spinner's active flag from another thread and records whether
// it was refused.
struct OtherThreadActive final {
    Spinner* spinner{};
    bool* refused{};
    void operator()() const {
        try {
            (*spinner).active = false;
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

// Spinner: busy semantics while active, nothing while inactive; its turn
// runs on the motion clock one dot per step while drawn, drops off when not
// drawn, stands still at twelve o'clock under reduced motion, and leaves
// the clock when deactivated.
void test_spinner() {
    const std::shared_ptr<Spinner> loose = make_control<Spinner>(StableId("display.spinner.loose"));
    require((*loose).active.read() && (*loose).measure(Size{200.0, 200.0}) == (Size{24.0, 24.0}),
            "a spinner is active and measures 24 units square");
    const SemanticDescriptor busy = (*loose).semantic_descriptor();
    require(busy.role == SemanticRole::progress_bar && busy.exposed && has_semantic_state(busy.states, SemanticState::busy) &&
                !busy.numeric_value,
            "an active spinner is a busy progress bar without a value");
    require(!(*loose).hit_test_local(Point{4.0, 4.0}), "a spinner takes no pointer input");

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.spinner.root"));
    const std::shared_ptr<Spinner> spinner = make_control<Spinner>(StableId("display.spinner"));
    (*spinner).set_requested_bounds(Rect{10.0, 10.0, 48.0, 48.0});
    (*root).add_child(spinner);
    ManualTime time;
    Window window(root, Size{200.0, 100.0});
    window.motion_clock().set_time_source(&time);
    window.perform_layout();
    MotionClock& clock = window.motion_clock();
    require((*spinner).parts().activity.present, "an active spinner publishes its activity part");
    require(!clock.control_ambient(*spinner), "a spinner not drawn yet is not on the clock");
    const Rect bounds{0.0, 0.0, 48.0, 48.0};
    NullPainter first;
    (*spinner).on_paint(first, bounds);
    require(clock.control_ambient(*spinner) && (*spinner).moving() && window.next_wake().has_value() &&
                (*spinner).phase() == 0U,
            "a drawn spinner turns on the motion clock from twelve o'clock");
    advance(window, time, std::chrono::milliseconds(100));
    require((*spinner).phase() == 1U, "one step period moves the lit run one dot");
    NullPainter second;
    (*spinner).on_paint(second, bounds);
    advance(window, time, std::chrono::milliseconds(250));
    require((*spinner).phase() == 3U, "the phase follows the elapsed time, a dot per 100 ms");
    advance(window, time, std::chrono::milliseconds(100));
    advance(window, time, std::chrono::milliseconds(100));
    require(!clock.control_ambient(*spinner), "a spinner not drawn between frames drops off the clock");

    set_reduced_motion(window, true);
    NullPainter still;
    (*spinner).on_paint(still, bounds);
    require(!clock.control_ambient(*spinner) && (*spinner).phase() == 0U,
            "under reduced motion the ring stands still at twelve o'clock");
    require(has_semantic_state((*spinner).semantic_descriptor().states, SemanticState::busy),
            "a still spinner is still busy");
    require((*spinner).anatomy_states().contains(AnatomyState::reduced_motion), "the reduced_motion state is published");
    set_reduced_motion(window, false);
    NullPainter again;
    (*spinner).on_paint(again, bounds);
    require(clock.control_ambient(*spinner), "with motion allowed again the next paint turns it");

    (*spinner).active = false;
    window.perform_layout();
    const SemanticDescriptor idle = (*spinner).semantic_descriptor();
    require(!clock.control_ambient(*spinner) && !idle.exposed && !has_semantic_state(idle.states, SemanticState::busy),
            "deactivating withdraws the turn and the busy node");
    require(!(*spinner).parts().activity.present, "an inactive spinner publishes no activity part");

    bool refused = false;
    std::thread worker(OtherThreadActive{spinner.get(), &refused});
    worker.join();
    require(refused && !(*spinner).active.read(), "an active write from another thread is refused");
}

// ProgressRing: the lit dots follow value over maximum, clamped; bad values
// are refused; semantics carry the value, or busy while indeterminate,
// whose run turns on the motion clock and stands still under reduced
// motion.
void test_progress_ring() {
    const std::shared_ptr<ProgressRing> loose = make_control<ProgressRing>(StableId("display.ring.loose"));
    require((*loose).fraction() == 0.0 && (*loose).lit_dots() == 0U, "an empty ring lights nothing");
    (*loose).value = 25.0;
    require((*loose).fraction() == 0.25 && (*loose).lit_dots() == 10U, "a quarter lights a quarter of the dots");
    (*loose).value = 0.5;
    require((*loose).lit_dots() == 1U, "a positive value lights at least one dot");
    (*loose).value = 1000.0;
    require((*loose).fraction() == 1.0 && (*loose).lit_dots() == ProgressRing::dot_count, "past maximum the ring is full");
    (*loose).value = -5.0;
    require((*loose).lit_dots() == 0U, "below zero the ring is empty");
    bool refused_maximum = false;
    try {
        (*loose).maximum = 0.0;
    } catch (const std::invalid_argument&) {
        refused_maximum = true;
    }
    require(refused_maximum && (*loose).maximum.read() == 100.0, "a maximum of zero is refused");
    bool refused_value = false;
    try {
        (*loose).value = std::numeric_limits<double>::quiet_NaN();
    } catch (const std::invalid_argument&) {
        refused_value = true;
    }
    require(refused_value && (*loose).value.read() == -5.0, "a value that is not finite is refused");
    (*loose).maximum = 50.0;
    (*loose).value = 20.0;
    const SemanticDescriptor amount = (*loose).semantic_descriptor();
    require(amount.role == SemanticRole::progress_bar && amount.exposed && amount.numeric_value == 20.0 &&
                amount.minimum_value == 0.0 && amount.maximum_value == 50.0 && amount.value == "20" &&
                !has_semantic_state(amount.states, SemanticState::busy),
            "a determinate ring publishes its value and range");

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("display.ring.root"));
    const std::shared_ptr<ProgressRing> ring = make_control<ProgressRing>(StableId("display.ring"));
    (*ring).set_requested_bounds(Rect{10.0, 10.0, 64.0, 64.0});
    (*ring).value = 40.0;
    (*root).add_child(ring);
    ManualTime time;
    Window window(root, Size{200.0, 100.0});
    window.motion_clock().set_time_source(&time);
    window.perform_layout();
    MotionClock& clock = window.motion_clock();
    const ProgressRing::Parts determinate = (*ring).parts();
    require(determinate.track.present && determinate.fill.present && !determinate.activity.present,
            "a determinate ring publishes its track and fill");
    const Rect bounds{0.0, 0.0, 64.0, 64.0};
    NullPainter painted;
    (*ring).on_paint(painted, bounds);
    require(!clock.control_ambient(*ring), "a determinate ring is off the motion clock");

    // The turning ring is the standard indeterminate state, as on ProgressBar.
    require(!(*ring).anatomy_states().contains(AnatomyState::indeterminate),
            "determinate is not the indeterminate state");
    (*ring).indeterminate = true;
    window.perform_layout();
    require((*ring).anatomy_states().contains(AnatomyState::indeterminate), "indeterminate publishes its state");
    const ProgressRing::Parts spinning = (*ring).parts();
    require(spinning.activity.present && !spinning.fill.present, "an indeterminate ring publishes its activity part");
    const SemanticDescriptor busy = (*ring).semantic_descriptor();
    require(has_semantic_state(busy.states, SemanticState::busy) && !busy.numeric_value,
            "an indeterminate ring is busy without a value");
    NullPainter first;
    (*ring).on_paint(first, bounds);
    require(clock.control_ambient(*ring) && (*ring).moving(), "a drawn indeterminate ring turns on the motion clock");
    advance(window, time, std::chrono::milliseconds(90));
    require((*ring).phase() == 3U, "the run moves a dot per 30 ms");
    set_reduced_motion(window, true);
    NullPainter still;
    (*ring).on_paint(still, bounds);
    require(!clock.control_ambient(*ring) && (*ring).phase() == 0U,
            "under reduced motion the run stands still at twelve o'clock");
    set_reduced_motion(window, false);
    (*ring).indeterminate = false;
    NullPainter settled;
    (*ring).on_paint(settled, bounds);
    require(!clock.control_ambient(*ring) && (*ring).parts().fill.present, "turning determinate again leaves the clock");
}

} // namespace

int main() {
    test_badge_text_and_visibility();
    test_badge_semantics_parts_and_threads();
    test_info_banner_properties_and_states();
    test_info_banner_closing();
    test_info_banner_actuator_action_and_threads();
    test_empty_state();
    test_segmented_choice_group();
    test_segment_edges_at_scale();
    test_toast_content_and_semantics();
    test_window_toasts();
    test_window_toast_refusals();
    test_toggle_switch();
    test_search_field_alone();
    test_search_field_in_window();
    test_toggle_switch_motion();
    test_search_field_busy_motion();
    test_spinner();
    test_progress_ring();
    std::cout << "display control tests passed\n";
    return EXIT_SUCCESS;
}
