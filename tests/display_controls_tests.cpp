// W-4 controls: Badge, InfoBanner, EmptyState and the segmented
// ChoiceGroup presentation. Each test covers the control's behaviour, its refusals, its
// semantics, its published parts and states, and the capability cases: a
// write stores, an equal write does nothing, and a write from another
// thread is refused before anything is compared or stored.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/controls/collections/choice_group.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

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
    require((*banner).severity.read() == InfoBannerSeverity::information && (*banner).closable.read(),
            "a banner starts as closable information");
    require((*banner).focusable(), "a closable banner takes focus");
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
    (*banner).severity = InfoBannerSeverity::error;
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
    require(!(*banner).focusable() && (*banner).semantic_descriptor().actions.empty(),
            "a banner that is not closable takes no focus and offers no close action");
    require(!(*banner).on_semantic_action(SemanticAction::close, std::string_view{}) && (*banner).visible.read(),
            "the close action is refused while the banner is not closable");

    const std::optional<BindingValue> by_name = (*banner).property_value("severity");
    require(by_name && *by_name == BindingValue{PropertyEnumValue{"info_banner_severity", "error", 3}},
            "the table reads the severity cell");
    (*banner).set_property_value("severity", BindingValue{PropertyEnumValue{"info_banner_severity", "success", 2}});
    require((*banner).severity.read() == InfoBannerSeverity::success, "the table writes the severity cell");
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
    require((*group).parts().track.present && !(*group).parts().selection.present,
            "a segmented group with nothing selected publishes its track and no selection");

    (*group).set_selected_index(1);
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
    require(right.handled && (*group).selected_index() == 2 &&
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
                (*group).selected_index() == 2,
            "back to buttons, the items fill the group again and the selection is kept");
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
    std::cout << "display control tests passed\n";
    return EXIT_SUCCESS;
}
