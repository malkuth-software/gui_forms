// Roadmap row 2.5, the final stock controls: SevenSegmentDisplay,
// LinearGauge, RadialGauge and Dial (the instruments package), StepIndicator,
// Wizard and WizardPage, Callout, ModalLayer, LookupComboBox and AboutDialog.
// Each test covers the control's behaviour, its refusals, its semantics and
// its published parts; the generated property-family tests cover the
// capability cases.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/components/about_dialog/about_dialog.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/callout.hpp"
#include "gui_forms/controls/dial.hpp"
#include "gui_forms/controls/expandable_sections.hpp"
#include "gui_forms/controls/linear_gauge.hpp"
#include "gui_forms/controls/lookup_combo_box.hpp"
#include "gui_forms/controls/modal_layer.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/radial_gauge.hpp"
#include "gui_forms/controls/range_control_values.hpp"
#include "gui_forms/controls/seven_segment_display.hpp"
#include "gui_forms/controls/step_indicator.hpp"
#include "gui_forms/controls/wizard.hpp"
#include "gui_forms/controls/wizard_page.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/theme/gui_forms_theme/gui_forms_theme.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/controls/task_dialog.hpp"
#include "gui_forms/delegate.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_final_controls_tests: " << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool close = std::fabs(left - right) < 1e-9;
    return close;
}

// Records the events the controls raise, as text.
struct Recorder final {
    std::vector<std::string> seen{};
    // The subscriptions, disconnected when the recorder goes.
    std::vector<SubscriptionToken> tokens{};
    void value(double reading) { seen.push_back("value:" + std::to_string(static_cast<int>(std::lround(reading)))); }
    void step(std::size_t index) { seen.push_back("step:" + std::to_string(index)); }
    void page(std::uint32_t index) { seen.push_back("page:" + std::to_string(index)); }
    void finished() { seen.push_back("finished"); }
    void cancelled() { seen.push_back("cancelled"); }
    void callout_closed(CalloutCloseReason reason) { seen.push_back("callout:" + std::to_string(static_cast<int>(reason))); }
    void action() { seen.push_back("action"); }
    void modal_closed(DialogResult result) { seen.push_back("modal:" + std::to_string(static_cast<int>(result))); }
    void key(const std::string& key) { seen.push_back("key:" + key); }
    void dropped(bool open) { seen.push_back(open ? "open" : "closed"); }
};

// Refuses every page change.
struct Refuser final {
    bool refuse{true};
    void changing(WizardPageChangingEvent& event) { event.cancel = refuse; }
    void closing(ModalLayerClosingEvent& event) { event.cancel = refuse; }
};

PointerEvent press(Point at) {
    PointerEvent event{};
    event.action = PointerAction::down;
    event.button = PointerButton::primary;
    event.position = at;
    event.pointer_id = 1U;
    return event;
}

PointerEvent release(Point at) {
    PointerEvent event = press(at);
    event.action = PointerAction::up;
    return event;
}

Point centre_of(const Control& control) {
    const Rect bounds = control.absolute_bounds();
    const Point centre{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return centre;
}

// -- the instruments ----------------------------------------------------------

void test_seven_segment_patterns() {
    require(seven_segment_pattern(U'8') == 0x7FU, "eight lights every segment");
    require(seven_segment_pattern(U'1') == 0x06U, "one lights b and c");
    require(seven_segment_pattern(U'-') == 0x40U, "a minus lights g");
    require(seven_segment_pattern(U' ') == 0U, "a space lights nothing");
    require(seven_segment_pattern(U'A') == seven_segment_pattern(U'a'), "letters ignore case where the cell can");

    std::shared_ptr<SevenSegmentDisplay> display = make_control<SevenSegmentDisplay>(StableId("final.seven"));
    (*display).text = std::string{"12.5"};
    std::vector<SevenSegmentCell> cells = (*display).cells();
    require(cells.size() == 3U, "a point joins the cell before it");
    require(cells[1].point && !cells[0].point && !cells[2].point, "the point lights the cell it follows");
    (*display).digit_count = std::uint32_t{5U};
    cells = (*display).cells();
    require(cells.size() == 5U && cells[0].segments == 0U && cells[1].segments == 0U, "short readings pad on the left");
    (*display).digit_count = std::uint32_t{2U};
    cells = (*display).cells();
    require(cells.size() == 2U && (*display).overflowed() && cells[1].segments == seven_segment_pattern(U'5'),
            "a long reading keeps its last digits");
    (*display).digit_count = std::uint32_t{0U};
    (*display).text = std::string{"12:30"};
    cells = (*display).cells();
    require(cells.size() == 5U && cells[2].colon, "a colon takes a cell of its own");
    bool refused = false;
    try {
        (*display).digit_count = std::uint32_t{65U};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*display).digit_count.read() == 0U, "more than 64 digits are refused");
    refused = false;
    try {
        (*display).text = std::string{"\xff"};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*display).text.read() == "12:30", "text that is not UTF-8 is refused");
    const SemanticDescriptor semantics = (*display).semantic_descriptor();
    require(semantics.role == SemanticRole::static_text && semantics.value == "12:30", "the reading is its value");
}

void test_seven_segment_in_window() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.seven.root"));
    std::shared_ptr<SevenSegmentDisplay> display = make_control<SevenSegmentDisplay>(StableId("final.seven.attached"));
    (*display).set_requested_bounds(Rect{10.0, 10.0, 160.0, 48.0});
    (*display).text = std::string{"88.8"};
    (*root).add_child(display);
    Window window(root, Size{300.0, 100.0});
    window.perform_layout();
    const SevenSegmentDisplay::Parts parts = (*display).parts();
    require(parts.plot.present && parts.trace.present, "the display publishes its plot and lit segments");
    const Size natural = (*display).measure(Size{1000.0, 1000.0});
    require(natural.width > 0.0 && natural.height > 0.0, "the display has a natural size");
}

void test_gauge_values_and_alarms() {
    std::shared_ptr<LinearGauge> gauge = make_control<LinearGauge>(StableId("final.linear"));
    (*gauge).set_range(0.0, 200.0);
    (*gauge).value = 50.0;
    require(near((*gauge).ratio_of(50.0), 0.25), "the ratio is the value's place in the range");
    require(near((*gauge).ratio_of(-10.0), 0.0) && near((*gauge).ratio_of(500.0), 1.0), "ratios clamp");
    (*gauge).units = std::string{"kPa"};
    (*gauge).decimal_places = std::uint32_t{1U};
    require((*gauge).value_text() == "50.0 kPa", "the value shows its decimals and units");
    (*gauge).tick_frequency = 50.0;
    const std::vector<double> ticks = (*gauge).tick_ratios();
    require(ticks.size() == 5U && near(ticks.front(), 0.0) && near(ticks.back(), 1.0), "one mark per 50 and the ends");
    (*gauge).tick_frequency = 30.0;
    require(near((*gauge).tick_ratios().back(), 1.0), "the maximum always carries a mark");
    (*gauge).tick_frequency = 0.0001;
    require((*gauge).tick_ratios().size() == 101U, "a fine frequency is drawn as at most 101 marks");
    (*gauge).tick_frequency = 0.0;
    require((*gauge).tick_ratios().empty(), "a zero frequency draws no marks");

    require(!(*gauge).in_alarm(), "without limits there is no alarm");
    (*gauge).high_alarm = std::optional<double>{40.0};
    require((*gauge).in_alarm(), "a value over the high limit is in alarm");
    require((*gauge).anatomy_states().contains(AnatomyState::invalid), "an alarm is the invalid state");
    SemanticDescriptor semantics = (*gauge).semantic_descriptor();
    require(semantics.role == SemanticRole::meter && semantics.value == "50.0 kPa (high alarm)" &&
                has_semantic_state(semantics.states, SemanticState::invalid),
            "assistive technology hears the alarm");
    (*gauge).high_alarm = std::optional<double>{};
    (*gauge).low_alarm = std::optional<double>{60.0};
    semantics = (*gauge).semantic_descriptor();
    require(semantics.value == "50.0 kPa (low alarm)", "a value under the low limit is a low alarm");
    bool refused = false;
    try {
        (*gauge).decimal_places = std::uint32_t{7U};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "more than six decimals are refused");
    refused = false;
    try {
        (*gauge).low_alarm = std::optional<double>{std::nan("")};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*gauge).low_alarm.read() == std::optional<double>{60.0}, "a limit that is not finite is refused");
}

void test_gauges_in_window() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.gauges.root"));
    std::shared_ptr<LinearGauge> linear = make_control<LinearGauge>(StableId("final.gauges.linear"));
    (*linear).set_requested_bounds(Rect{10.0, 10.0, 240.0, 30.0});
    (*linear).value = 50.0;
    (*linear).high_alarm = std::optional<double>{80.0};
    (*root).add_child(linear);
    std::shared_ptr<LinearGauge> vertical = make_control<LinearGauge>(StableId("final.gauges.vertical"));
    (*vertical).set_requested_bounds(Rect{260.0, 10.0, 48.0, 160.0});
    (*vertical).orientation = Orientation::vertical;
    (*vertical).value = 25.0;
    (*root).add_child(vertical);
    std::shared_ptr<RadialGauge> radial = make_control<RadialGauge>(StableId("final.gauges.radial"));
    (*radial).set_requested_bounds(Rect{10.0, 60.0, 140.0, 140.0});
    (*radial).value = 75.0;
    (*root).add_child(radial);
    Window window(root, Size{400.0, 240.0});
    window.perform_layout();

    const LinearGauge::Parts bar = (*linear).parts();
    require(bar.plot.present && bar.trace.present && bar.alarm_zone.present && bar.caption.present,
            "a bar gauge publishes its field, reading, alarm segment and value");
    require(near(bar.alarm_zone.bounds.x, bar.plot.bounds.x + std::round(bar.plot.bounds.width * 0.8)) &&
                near(bar.alarm_zone.bounds.x + bar.alarm_zone.bounds.width, bar.plot.bounds.x + bar.plot.bounds.width),
            "the high alarm segment runs from the limit to the maximum");
    require(near(bar.trace.bounds.width, std::round(bar.plot.bounds.width * 0.5)),
            "the reading fills half the field at half the range");
    const LinearGauge::Parts column = (*vertical).parts();
    require(column.trace.present && near(column.trace.bounds.y + column.trace.bounds.height,
                                         column.plot.bounds.y + column.plot.bounds.height),
            "a vertical gauge fills from the bottom");
    require(!column.alarm_zone.present, "without limits no alarm segment is published");
    const RadialGauge::Parts dial = (*radial).parts();
    require(dial.plot.present && dial.value_arc.present && dial.track.present && dial.needle.present &&
                dial.caption.present,
            "a radial gauge publishes its face, scale arc, value arc, needle and value");
    require(!dial.alarm_arc.present, "without limits no alarm segment is published");
    (*radial).high_alarm = std::optional<double>{90.0};
    window.perform_layout();
    require((*radial).parts().alarm_arc.present, "a high limit draws an alarm segment");
    (*radial).value = 0.0;
    window.perform_layout();
    require(!(*radial).parts().value_arc.present, "at the minimum no value arc is lit");
    (*radial).show_value = false;
    window.perform_layout();
    require(!(*radial).parts().caption.present, "show_value clear hides the value");
    require(!(*radial).focusable.read(), "a gauge takes no focus");
}

void test_dial() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.dial.root"));
    std::shared_ptr<Dial> dial = make_control<Dial>(StableId("final.dial"));
    (*dial).set_requested_bounds(Rect{20.0, 20.0, 100.0, 100.0});
    (*root).add_child(dial);
    Window window(root, Size{200.0, 200.0});
    window.perform_layout();
    Recorder recorder;
    recorder.tokens.push_back((*dial).value_changed().subscribe(Delegate<double>::bind<Recorder, &Recorder::value>(recorder)));

    require(near((*dial).pointer_angle(), -0.75 * 3.14159265358979323846), "the minimum faces the lower left");
    // Straight up from the centre is the middle of the sweep.
    const Point centre = centre_of(*dial);
    require(window.dispatch_pointer(press(Point{centre.x, centre.y - 40.0})), "a press on the dial is handled");
    require(near((*dial).value.read(), 50.0), "pointing straight up chooses the middle of the range");
    require((*dial).anatomy_states().contains(AnatomyState::pressed), "a dragged dial is pressed");
    PointerEvent move = press(Point{centre.x + 40.0, centre.y});
    move.action = PointerAction::move;
    move.button = PointerButton::none;
    require(window.dispatch_pointer(move), "a drag is handled");
    require(near((*dial).value.read(), 100.0 * 1.25 / 1.5), "pointing right is five sixths of the way round");
    // Into the gap at the bottom: the dial stays at the end it was nearer.
    move.position = Point{centre.x, centre.y + 40.0};
    require(window.dispatch_pointer(move) && near((*dial).value.read(), 100.0), "the gap keeps the nearer end");
    require(window.dispatch_pointer(release(Point{centre.x, centre.y + 40.0})), "the release is handled");
    require(!(*dial).anatomy_states().contains(AnatomyState::pressed), "a released dial is not pressed");

    require(window.request_focus(dial), "the dial takes focus");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::home}) && near((*dial).value.read(), 0.0), "Home is the minimum");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::up}) && near((*dial).value.read(), 1.0), "Up adds the small change");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::page_up}) && near((*dial).value.read(), 11.0),
            "Page Up adds the large change");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::end}) && near((*dial).value.read(), 100.0), "End is the maximum");
    const SemanticDescriptor semantics = (*dial).semantic_descriptor();
    require(semantics.role == SemanticRole::slider && semantics.numeric_value && near(*semantics.numeric_value, 100.0),
            "a dial is a slider to assistive technology");
    require((*dial).on_semantic_action(SemanticAction::set_value, "25") && near((*dial).value.read(), 25.0),
            "the set-value action moves the dial");
    require(!(*dial).on_semantic_action(SemanticAction::set_value, "x"), "a set value that is no number is refused");
    window.perform_layout();
    const Dial::Parts parts = (*dial).parts();
    require(parts.track.present && parts.value_arc.present && parts.thumb.present && parts.needle.present,
            "the dial publishes its scale arc, lit arc, knob and needle");
    require(near(parts.track.bounds.width, parts.track.bounds.height) &&
                near(parts.track.bounds.x + parts.track.bounds.width * 0.5,
                     parts.thumb.bounds.x + parts.thumb.bounds.width * 0.5),
            "the scale arc is inscribed in a square about the knob");
    (*dial).show_arc = false;
    window.perform_layout();
    require(!(*dial).parts().value_arc.present, "without show_arc nothing is lit");
    require(!recorder.seen.empty(), "changes raise value_changed");
}

// -- guided tasks ---------------------------------------------------------------

void test_step_indicator() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.steps.root"));
    std::shared_ptr<StepIndicator> steps = make_control<StepIndicator>(StableId("final.steps"));
    (*steps).set_requested_bounds(Rect{0.0, 0.0, 400.0, 60.0});
    (*steps).set_steps({"Account", "Details", "Review", "Done"});
    (*root).add_child(steps);
    Window window(root, Size{400.0, 100.0});
    window.perform_layout();
    Recorder recorder;
    recorder.tokens.push_back(
        (*steps).step_clicked().subscribe(Delegate<std::size_t>::bind<Recorder, &Recorder::step>(recorder)));

    require((*steps).semantic_descriptor().value == "Step 1 of 4: Account", "the first step is under way");
    (*steps).current_step = std::uint32_t{2U};
    require((*steps).semantic_descriptor().value == "Step 3 of 4: Review", "the value names the current step");
    bool refused = false;
    try {
        (*steps).current_step = std::uint32_t{4U};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*steps).current_step.read() == 2U, "a step past the last is refused");
    window.perform_layout();
    const StepIndicator::Parts parts = (*steps).parts();
    require(parts.chip.present && parts.glyph.present && parts.divider.present && parts.caption.present,
            "the indicator publishes its chips, checks, connectors and names");
    // Columns are 100 units wide; the first chip is centred at 50.
    require((*steps).step_at(Point{50.0, 12.0}) == std::optional<std::size_t>{0U}, "the first chip is step 0");
    require(window.dispatch_pointer(press(Point{50.0, 12.0})) && recorder.seen.size() == 1U &&
                recorder.seen[0] == "step:0",
            "a completed step can be pressed");
    require(!window.dispatch_pointer(press(Point{350.0, 12.0})) || recorder.seen.size() == 1U,
            "an upcoming step cannot");
    (*steps).set_steps({"Only"});
    require((*steps).current_step.read() == 0U, "fewer steps pull the current step back");
    refused = false;
    try {
        (*steps).set_steps({"\xff"});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*steps).steps().size() == 1U, "names that are not UTF-8 are refused");
}

std::shared_ptr<WizardPage> make_page(const char* identity, const char* title) {
    std::shared_ptr<WizardPage> page = make_control<WizardPage>(StableId(std::string(identity)));
    (*page).title = std::string{title};
    return page;
}

void test_wizard() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.wizard.root"));
    std::shared_ptr<Wizard> wizard = make_control<Wizard>(StableId("final.wizard"));
    (*wizard).set_requested_bounds(Rect{0.0, 0.0, 520.0, 380.0});
    std::shared_ptr<WizardPage> first = make_page("final.wizard.first", "Account");
    std::shared_ptr<WizardPage> second = make_page("final.wizard.second", "Details");
    std::shared_ptr<WizardPage> third = make_page("final.wizard.third", "Review");
    (*second).description = std::string{"Who is the account for?"};
    (*wizard).add_child(first);
    (*wizard).add_child(second);
    (*wizard).add_child(third);
    (*root).add_child(wizard);
    Window window(root, Size{600.0, 400.0});
    window.perform_layout();
    Recorder recorder;
    recorder.tokens.push_back(
        (*wizard).page_changed().subscribe(Delegate<std::uint32_t>::bind<Recorder, &Recorder::page>(recorder)));
    recorder.tokens.push_back((*wizard).finished().subscribe(Delegate<>::bind<Recorder, &Recorder::finished>(recorder)));
    recorder.tokens.push_back((*wizard).cancelled().subscribe(Delegate<>::bind<Recorder, &Recorder::cancelled>(recorder)));

    require((*wizard).page_count() == 3U, "the WizardPage children are the pages");
    require((*(*wizard).step_indicator()).steps().size() == 3U &&
                (*(*wizard).step_indicator()).steps()[1] == "Details",
            "the indicator names the pages by title");
    require(!(*(*wizard).back_button()).enabled.read(), "there is no going back from the first page");
    require((*(*wizard).next_button()).text.read() == "Next >", "Next reads Next before the last page");
    require((*first).committed_arranged_bounds().width > 0.0 && (*second).committed_arranged_bounds().width == 0.0,
            "only the current page is laid out");

    (*first).can_advance = false;
    require(!(*(*wizard).next_button()).enabled.read() && !(*wizard).go_next(), "a page's gate holds Next");
    (*first).can_advance = true;
    require((*(*wizard).next_button()).perform_click() && (*wizard).current_page.read() == 1U &&
                recorder.seen.back() == "page:1",
            "Next moves to the second page");
    window.perform_layout();
    require((*wizard).parts().label.present, "a page's description shows in the header");
    require((*wizard).semantic_descriptor().name == "Details", "the wizard is named by its page");

    Refuser refuser;
    SubscriptionToken refusing =
        (*wizard).page_changing().subscribe(Delegate<WizardPageChangingEvent&>::bind<Refuser, &Refuser::changing>(refuser));
    require(!(*wizard).go_back() && (*wizard).current_page.read() == 1U, "page_changing may refuse");
    refusing.disconnect();
    require((*wizard).go_next() && (*wizard).current_page.read() == 2U, "the move continues once nobody refuses");
    require((*(*wizard).next_button()).text.read() == "Finish", "Next reads Finish on the last page");
    require((*wizard).go_next() && recorder.seen.back() == "finished", "Finish raises finished");

    require(window.request_focus((*wizard).back_button()), "the Back button takes focus");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && recorder.seen.back() == "cancelled",
            "Escape cancels");
    (*wizard).current_page = std::uint32_t{0U};
    require(recorder.seen.back() == "page:0", "a write of current_page moves at once");
    bool refused = false;
    try {
        (*wizard).current_page = std::uint32_t{3U};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a page past the last is refused");
    (*wizard).back_text = std::string{"Previous"};
    require((*(*wizard).back_button()).text.read() == "Previous", "the button texts are properties");
}

// -- overlays ----------------------------------------------------------------------

void test_callout() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.callout.root"));
    std::shared_ptr<Button> target = make_control<Button>(StableId("final.callout.target"), "New");
    (*target).set_requested_bounds(Rect{20.0, 20.0, 80.0, 28.0});
    (*root).add_child(target);
    std::shared_ptr<Button> elsewhere = make_control<Button>(StableId("final.callout.elsewhere"), "Other");
    (*elsewhere).set_requested_bounds(Rect{300.0, 300.0, 80.0, 28.0});
    (*root).add_child(elsewhere);
    Window window(root, Size{500.0, 400.0});
    window.perform_layout();
    std::shared_ptr<Callout> callout = make_control<Callout>(StableId("final.callout"));
    (*callout).title = std::string{"Try the new view"};
    (*callout).text = std::string{"Switch layouts from here at any time."};
    Recorder recorder;
    recorder.tokens.push_back((*callout).closed().subscribe(Delegate<CalloutCloseReason>::bind<Recorder, &Recorder::callout_closed>(recorder)));
    recorder.tokens.push_back((*callout).action_clicked().subscribe(Delegate<>::bind<Recorder, &Recorder::action>(recorder)));

    require(!(*callout).show(nullptr), "a callout needs a target in a window");
    require((*callout).show(target) && (*callout).is_shown(), "the callout opens at its target");
    window.perform_layout();
    const Rect card = (*callout).absolute_bounds();
    require(card.y >= 20.0 + 28.0 && card.x >= 0.0 && card.width > 0.0 && card.height > 0.0,
            "the callout opens below its target");
    const Callout::Parts pointed = (*callout).parts();
    require(pointed.beak.present && pointed.face.present && near(pointed.beak.bounds.y, 0.0) &&
                pointed.face.bounds.y > 0.0,
            "below its target, the beak lies above the card");
    const Rect aimed = (*target).absolute_bounds();
    const double beak_centre = card.x + pointed.beak.bounds.x + pointed.beak.bounds.width * 0.5;
    require(std::abs(beak_centre - (aimed.x + aimed.width * 0.5)) <= 0.5 ||
                pointed.beak.bounds.x <= 12.0 + 0.5,
            "the beak points at the target's centre, clear of the card's corner");
    const SemanticDescriptor semantics = (*callout).semantic_descriptor();
    require(semantics.role == SemanticRole::dialog && !semantics.modal && semantics.name == "Try the new view",
            "a callout is a non-modal dialog named by its title");
    // Without light dismiss, a press elsewhere reaches the window.
    require(window.dispatch_pointer(press(centre_of(*elsewhere))) && (*callout).is_shown(),
            "presses outside pass through and leave the callout open");
    static_cast<void>(window.dispatch_pointer(release(centre_of(*elsewhere))));
    require(window.request_focus(callout), "focus can enter the callout");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}), "Escape is handled");
    require(!(*callout).is_shown(), "Escape closes it");
    require(!recorder.seen.empty() && recorder.seen.back() == "callout:1", "Escape reports its reason");

    (*callout).action_text = std::string{"Got it"};
    (*callout).light_dismiss = true;
    require((*callout).show(target), "a closed callout opens again");
    window.perform_layout();
    require((*callout).parts().actuator.present && (*callout).action_button(), "the action button shows");
    require((*(*callout).action_button()).perform_click() && recorder.seen.size() >= 3U &&
                recorder.seen[recorder.seen.size() - 2U] == "action" && recorder.seen.back() == "callout:3",
            "the action raises action_clicked, then closes");
    require((*callout).show(target), "it opens a third time");
    window.perform_layout();
    require(window.dispatch_pointer(press(Point{450.0, 380.0})) && !(*callout).is_shown() &&
                recorder.seen.back() == "callout:2",
            "with light dismiss, a press outside closes it");
    (*callout).action_text = std::string{};
    require(!(*callout).action_button(), "an empty action text removes the button");
    require(!(*callout).close(), "closing a closed callout does nothing");
}

// A one-shot callout whose program let it go (A-gf-controls-01-F7): while
// shown, its layer holds the only reference, so a press outside that
// dismisses it would destroy it inside finish. The callout holds itself
// to the end of finish: closed is raised, then it goes.
void test_released_callout_closes_whole() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.oneshot.root"));
    std::shared_ptr<Button> target = make_control<Button>(StableId("final.oneshot.target"), "New");
    (*target).set_requested_bounds(Rect{20.0, 20.0, 80.0, 28.0});
    (*root).add_child(target);
    Window window(root, Size{500.0, 400.0});
    window.perform_layout();
    Recorder recorder;
    std::weak_ptr<Callout> observed;
    {
        std::shared_ptr<Callout> callout = make_control<Callout>(StableId("final.oneshot"));
        (*callout).title = std::string{"Once"};
        (*callout).light_dismiss = true;
        recorder.tokens.push_back(
            (*callout).closed().subscribe(Delegate<CalloutCloseReason>::bind<Recorder, &Recorder::callout_closed>(recorder)));
        require((*callout).show(target), "the one-shot callout opens");
        observed = callout;
    }
    window.perform_layout();
    require(!observed.expired(), "while shown, the layer keeps the released callout");
    require(window.dispatch_pointer(press(Point{450.0, 380.0})), "the press outside is handled");
    static_cast<void>(window.dispatch_pointer(release(Point{450.0, 380.0})));
    require(!recorder.seen.empty() && recorder.seen.back() == "callout:2", "the released callout reports click-away");
    require(observed.expired(), "closed, the released callout is gone");
}

void test_modal_layer() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.modal.root"));
    std::shared_ptr<Button> behind = make_control<Button>(StableId("final.modal.behind"), "Behind");
    (*behind).set_requested_bounds(Rect{10.0, 10.0, 80.0, 28.0});
    (*root).add_child(behind);
    Window window(root, Size{500.0, 400.0});
    window.perform_layout();
    std::shared_ptr<ModalLayer> layer = make_control<ModalLayer>(StableId("final.modal"));
    std::shared_ptr<Panel> content = make_control<Panel>(StableId("final.modal.content"));
    (*content).set_requested_bounds(Rect{0.0, 0.0, 200.0, 120.0});
    std::shared_ptr<Button> inside = make_control<Button>(StableId("final.modal.inside"), "OK");
    (*inside).set_requested_bounds(Rect{60.0, 80.0, 80.0, 28.0});
    (*content).add_child(inside);
    Recorder recorder;
    recorder.tokens.push_back(
        (*layer).closed().subscribe(Delegate<DialogResult>::bind<Recorder, &Recorder::modal_closed>(recorder)));

    require(!(*layer).show(behind, nullptr), "a layer needs content");
    require((*layer).show(behind, content) && (*layer).is_shown(), "the layer shows its content");
    window.perform_layout();
    require((*layer).scrim_opacity() == 0.0, "the scrim fades in from nothing (row 2.6)");
    const Rect placed = (*content).absolute_bounds();
    require(near(placed.x, 150.0) && near(placed.y, 140.0), "the content is centred");
    require((*layer).parts().shadow.present, "the content casts the shadow");
    require((*layer).parts().scrim.present && near((*layer).parts().scrim.bounds.width, 500.0) &&
                near((*layer).parts().scrim.bounds.height, 400.0),
            "the scrim covers the window behind");
    require(window.focused_control() == inside, "focus moves into the content");
    require(!window.request_focus(behind), "focus cannot leave the layer");
    int clicks = 0;
    struct Counter final {
        int* count{};
        std::vector<SubscriptionToken> tokens{};
        void clicked(ButtonBase&) { ++*count; }
    } counter{&clicks, {}};
    counter.tokens.push_back((*behind).clicked().subscribe(Delegate<ButtonBase&>::bind<Counter, &Counter::clicked>(counter)));
    static_cast<void>(window.dispatch_pointer(press(centre_of(*behind))));
    static_cast<void>(window.dispatch_pointer(release(centre_of(*behind))));
    require(clicks == 0, "presses behind the layer do nothing");
    const SemanticDescriptor semantics = (*layer).semantic_descriptor();
    require(semantics.role == SemanticRole::dialog && semantics.modal, "the layer is a modal dialog");

    Refuser refuser;
    SubscriptionToken refusing =
        (*layer).closing().subscribe(Delegate<ModalLayerClosingEvent&>::bind<Refuser, &Refuser::closing>(refuser));
    require(!(*layer).close(DialogResult::ok) && (*layer).is_shown(), "closing may refuse");
    refusing.disconnect();
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && !(*layer).is_shown() &&
                recorder.seen.back() == "modal:2" && (*layer).result() == std::optional<DialogResult>{DialogResult::cancel},
            "Escape closes as cancel");
    require(!(*content).parent(), "the content leaves the layer");
    require((*layer).scrim_opacity() == 1.0, "a closed layer's fade is over");
    require((*layer).show(behind, content), "the layer shows again");
    require((*layer).close(DialogResult::ok) && recorder.seen.back() == "modal:1", "close reports its result");

    // Under reduced motion the scrim is full at once.
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    require((*layer).show(behind, content) && (*layer).scrim_opacity() == 1.0,
            "under reduced motion the scrim does not fade");
    require((*layer).close(DialogResult::ok), "the calm layer closes");
}

void test_lookup_combo_box() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.lookup.root"));
    std::shared_ptr<LookupComboBox> lookup = make_control<LookupComboBox>(StableId("final.lookup"));
    (*lookup).set_requested_bounds(Rect{20.0, 20.0, 220.0, 24.0});
    (*root).add_child(lookup);
    Window window(root, Size{500.0, 400.0});
    window.perform_layout();
    (*lookup).set_columns({{"Code", 60.0}, {"Country", 140.0}});
    (*lookup).set_rows({{"de", {"DE", "Germany"}}, {"fr", {"FR", "France"}}, {"gb", {"GB", "United Kingdom"}}});
    (*lookup).display_column = std::uint32_t{1U};
    Recorder recorder;
    recorder.tokens.push_back((*lookup).selected_key_changed().subscribe(Delegate<const std::string&>::bind<Recorder, &Recorder::key>(recorder)));
    recorder.tokens.push_back((*lookup).drop_down_changed().subscribe(Delegate<bool>::bind<Recorder, &Recorder::dropped>(recorder)));

    bool refused = false;
    try {
        (*lookup).selected_key = std::string{"us"};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*lookup).selected_key.read().empty(), "a key no row has is refused");
    refused = false;
    try {
        (*lookup).set_rows({{"a", {}}, {"a", {}}});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*lookup).rows().size() == 3U, "duplicate keys are refused");
    (*lookup).selected_key = std::string{"fr"};
    require((*lookup).shown_text() == "France" && recorder.seen.back() == "key:fr", "the field shows the display cell");

    require(window.request_focus(lookup), "the lookup takes focus");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::down}) && (*lookup).selected_key.read() == "gb",
            "Down chooses the next row while closed");
    KeyEvent open{KeyAction::down, PhysicalKey::down};
    open.modifiers = Modifier::alt;
    require(window.dispatch_key(open) && (*lookup).is_dropped_down() && recorder.seen.back() == "open",
            "Alt+Down opens the list");
    require((*lookup).filtered_rows().size() == 3U && (*lookup).highlighted() == std::optional<std::size_t>{2U},
            "the open list highlights the chosen row");
    TextInputEvent typed{};
    typed.text_utf8 = "an";
    require(window.dispatch_text(typed) && (*lookup).filter() == "an" && (*lookup).filtered_rows().size() == 2U,
            "typing filters across every column");
    require((*lookup).shown_text() == "an", "the filter shows in the field");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::backspace}) && (*lookup).filter() == "a",
            "Backspace removes the last typed character");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::home}) &&
                (*lookup).highlighted() == std::optional<std::size_t>{0U},
            "Home highlights the first row");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}) && !(*lookup).is_dropped_down() &&
                (*lookup).selected_key.read() == "de" && recorder.seen.back() == "closed",
            "Enter chooses the highlighted row and closes");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::f4}) && (*lookup).is_dropped_down(), "F4 opens the list");
    // A-gf-controls-06-F6: whether or not a row fits the open list yet, Page Down moves the
    // highlight down, never up (a page of 0 - 1 rows wrapped to -1).
    const std::optional<std::size_t> before_page = (*lookup).highlighted();
    require(before_page.has_value() && window.dispatch_key({KeyAction::down, PhysicalKey::page_down}) &&
                (*lookup).highlighted().has_value() && *(*lookup).highlighted() > *before_page,
            "Page Down moves the highlight down");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && !(*lookup).is_dropped_down() &&
                (*lookup).selected_key.read() == "de",
            "Escape closes without choosing");
    const SemanticDescriptor semantics = (*lookup).semantic_descriptor();
    require(semantics.role == SemanticRole::combo_box && semantics.value == "Germany" && semantics.has_popup,
            "the lookup is a combo box valued by its shown cell");
    window.perform_layout();
    require((*lookup).parts().well.present && (*lookup).parts().actuator.present, "the field publishes its parts");
    (*lookup).set_rows({{"fr", {"FR", "France"}}});
    require((*lookup).selected_key.read().empty() && recorder.seen.back() == "key:", "a vanished key is cleared");
}

void test_about_dialog() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.about.root"));
    Window window(root, Size{600.0, 400.0});
    window.perform_layout();
    AboutDialog about(StableId("final.about"), AboutInfo{.product = "Gallery",
                                                         .version = "2.5",
                                                         .description = "The GUI.Forms control gallery.",
                                                         .copyright = "Copyright the authors.",
                                                         .credits = "Portsmouth Rapids, MIT."});
    Recorder recorder;
    recorder.tokens.push_back(about.closed().subscribe(Delegate<DialogResult>::bind<Recorder, &Recorder::modal_closed>(recorder)));
    const TaskDialogPage& page = (*about.task_dialog()).page();
    require(page.caption == "About Gallery" && page.heading == "Gallery 2.5", "the heading names product and version");
    require(page.text == "The GUI.Forms control gallery.\n\nCopyright the authors.", "description, then copyright");
    require(page.expander.has_value() && page.icon == TaskDialogIcon::information, "credits behind the expander");
    require(about.show(root) && about.shown(), "the box shows over its window");
    about.close();
    require(!about.shown() && recorder.seen.back() == "modal:1", "OK closes it");
    bool refused = false;
    try {
        AboutDialog bad(StableId("final.about.bad"), AboutInfo{.product = "\xff"});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "text that is not UTF-8 is refused");
}

} // namespace

// -- the dial and overlay parts in the stock themes ------------------------------

void test_dial_parts_in_the_stock_themes() {
    require(part_role_name(PartRole::arc) == "arc" && part_role_name(PartRole::needle) == "needle" &&
                part_role_name(PartRole::beak) == "beak" && part_role_name(PartRole::scrim) == "scrim",
            "the dial and overlay roles are registered");
    const std::shared_ptr<const Theme> house = gui_forms_theme();
    ControlVisualContext resting{};
    const ControlVisualRecipe& gauge = (*house).resolve(ControlVisualRole::progress, resting);
    require(near(gauge.stroke_width, 6.0) && gauge.needle_shape == NeedleShape::tapered,
            "gui-forms draws 6-unit arcs and a tapered needle");
    const ControlVisualRecipe& window_recipe = (*house).resolve(ControlVisualRole::window, resting);
    require(window_recipe.scrim.alpha == 102U, "gui-forms dims behind a modal layer at 40 %");
    ControlVisualContext dark{};
    dark.dark = true;
    const ControlVisualRecipe& dark_window = (*house).resolve(ControlVisualRole::window, dark);
    require(dark_window.scrim.red == 0U && dark_window.scrim.alpha > window_recipe.scrim.alpha,
            "the dark variant dims deeper, in black");
    ControlVisualContext contrast{};
    contrast.high_contrast = true;
    const ControlVisualRecipe& forced = (*house).resolve(ControlVisualRole::progress, contrast);
    require(forced.needle_shape == NeedleShape::line && forced.stroke_width >= 2.0 && forced.scrim.alpha == 255U,
            "high contrast draws a line needle, a thick arc and an opaque scrim");
    const std::shared_ptr<const Theme> professional = Theme::create(windows_professional_theme_definition());
    const ControlVisualRecipe& thin = (*professional).resolve(ControlVisualRole::progress, resting);
    require(near(thin.stroke_width, 3.0) && thin.needle_shape == NeedleShape::line && thin.scrim.alpha == 77U,
            "windows-professional draws thin arcs, a line needle and a light scrim");
}

// GAPS T1: in forced high contrast the track is the band's outline (one-unit
// strokes) and the reading a solid band, so the two differ in shape where
// they share a colour.
class LinePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double width) override { widths.push_back(width); }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    std::vector<double> widths;
};

void test_high_contrast_gauge_arcs() {
    const std::shared_ptr<const Theme> house = gui_forms_theme();
    ControlVisualContext contrast{};
    contrast.high_contrast = true;
    const ControlVisualRecipe& forced = (*house).resolve(ControlVisualRole::progress, contrast);
    PartPaint track{.role = PartRole::arc, .bounds = Rect{0.0, 0.0, 120.0, 120.0},
                    .family = ControlVisualRole::progress};
    track.arc = PartArc{-2.0, 4.0};
    LinePainter outlined;
    paint_part_from_recipe(outlined, track, forced, contrast);
    bool hairlines = !outlined.widths.empty();
    for (const double width : outlined.widths) {
        if (!near(width, 1.0)) hairlines = false;
    }
    require(hairlines, "high contrast draws the track as one-unit outline strokes");
    PartPaint reading = track;
    reading.states.insert(AnatomyState::selected);
    LinePainter solid;
    paint_part_from_recipe(solid, reading, forced, contrast);
    require(!solid.widths.empty() && near(solid.widths.front(), forced.stroke_width) && forced.stroke_width > 1.0,
            "the reading stays a solid band");
    PartPaint alarm = track;
    alarm.states.insert(AnatomyState::invalid);
    LinePainter warned;
    paint_part_from_recipe(warned, alarm, forced, contrast);
    require(!warned.widths.empty() && near(warned.widths.front(), forced.stroke_width),
            "an alarm range keeps its solid band");
    ControlVisualContext resting{};
    const ControlVisualRecipe& ordinary = (*house).resolve(ControlVisualRole::progress, resting);
    LinePainter plain;
    paint_part_from_recipe(plain, track, ordinary, resting);
    require(!plain.widths.empty() && near(plain.widths.front(), ordinary.stroke_width),
            "outside high contrast the track is a solid band");
}

// The collapsing-header reveal (row 2.6): a section's body sits in a
// clipping host at its full height while the host opens from 0; closing
// hides the body at once; under reduced motion both jump.
void test_expandable_sections_reveal() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.sections.root"));
    std::shared_ptr<ExpandableSections> sections = make_control<ExpandableSections>(StableId("final.sections"));
    (*sections).set_requested_bounds(Rect{0.0, 0.0, 300.0, 300.0});
    (*root).add_child(sections);
    std::shared_ptr<Panel> first = make_control<Panel>(StableId("final.sections.first"));
    (*first).set_requested_bounds(Rect{0.0, 0.0, 300.0, 80.0});
    std::shared_ptr<Panel> second = make_control<Panel>(StableId("final.sections.second"));
    (*second).set_requested_bounds(Rect{0.0, 0.0, 300.0, 80.0});
    std::vector<SectionItem> items{};
    items.push_back(SectionItem{1, "First", first, nullptr});
    items.push_back(SectionItem{2, "Second", second, nullptr});
    (*sections).set_items(std::move(items));
    Window window(root, Size{400.0, 400.0});
    window.perform_layout();
    require(near((*sections).section_reveal(0), 0.0) && near((*sections).section_reveal(1), 0.0) &&
                !(*sections).revealing(),
            "the sections start closed, with no reveal");
    require((*first).parent() != nullptr && (*first).parent().get() != sections.get() &&
                (*(*first).parent()).parent().get() == sections.get(),
            "each body sits in a host inside the sections");

    (*sections).set_expanded(0, true);
    window.perform_layout();
    require((*first).visible.read() && (*sections).revealing() && (*sections).section_reveal(0) < 1.0,
            "opening shows the body at once and starts the reveal");
    require(near((*first).absolute_bounds().height, 80.0),
            "the body keeps its full height while its host opens");
    require((*(*first).parent()).absolute_bounds().height < 80.0, "the host is still lower than the body");

    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    (*sections).set_expanded(1, true);
    window.perform_layout();
    require(near((*sections).section_reveal(1), 1.0) && (*second).visible.read(),
            "under reduced motion a section opens at once");
    require(near((*(*second).parent()).absolute_bounds().height, 80.0), "its host is the body's height");
    (*sections).set_expanded(1, false);
    window.perform_layout();
    require(near((*sections).section_reveal(1), 0.0) && !(*second).visible.read(),
            "and closes at once, the body hidden");
    (*sections).set_expanded(0, false);
    require(!(*first).visible.read(), "closing hides the body at once");
}

// Replaces the sections' items when a host is hidden, as a handler reached
// from a host's events might.
struct ReplaceSectionsWhenHidden final {
    ExpandableSections* sections{};
    std::shared_ptr<Panel> replacement;
    void operator()(bool visible) const {
        if (visible || !replacement) return;
        std::vector<SectionItem> items{};
        items.push_back(SectionItem{7, "Replacement", replacement, nullptr});
        (*sections).set_items(std::move(items));
    }
};

// Runs the window's frames until the sections' reveal ends, at most
// `frames` of them.
void run_reveal(Window& window, const ExpandableSections& sections, int frames) {
    for (int frame = 0; frame < frames && sections.revealing(); ++frame) {
        const std::optional<FrameTime> wake = window.next_wake();
        if (!wake) return;
        static_cast<void>(window.poll_frame_schedule(*wake));
    }
}

// A host's visibility handler that replaces the items while the reveal
// tick walks them (A-gf-controls-01-F8): the tick holds the revision it
// walks and stops when it is no longer current.
void test_expandable_sections_replaced_during_a_reveal() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("final.replaced.root"));
    std::shared_ptr<ExpandableSections> sections = make_control<ExpandableSections>(StableId("final.replaced"));
    (*sections).set_requested_bounds(Rect{0.0, 0.0, 300.0, 300.0});
    (*root).add_child(sections);
    std::shared_ptr<Panel> first = make_control<Panel>(StableId("final.replaced.first"));
    (*first).set_requested_bounds(Rect{0.0, 0.0, 300.0, 80.0});
    std::shared_ptr<Panel> second = make_control<Panel>(StableId("final.replaced.second"));
    (*second).set_requested_bounds(Rect{0.0, 0.0, 300.0, 80.0});
    std::vector<SectionItem> items{};
    items.push_back(SectionItem{1, "First", first, nullptr});
    items.push_back(SectionItem{2, "Second", second, nullptr});
    (*sections).set_items(std::move(items));
    Window window(root, Size{400.0, 400.0});
    window.perform_layout();
    (*sections).set_expanded(0, true);
    run_reveal(window, *sections, 1000);
    require(!(*sections).revealing() && near((*sections).section_reveal(0), 1.0), "the first section opens");
    const Control::Ptr host = (*first).parent();
    require(host != nullptr, "the first body sits in a host");
    std::shared_ptr<Panel> replacement = make_control<Panel>(StableId("final.replaced.third"));
    (*replacement).set_requested_bounds(Rect{0.0, 0.0, 300.0, 80.0});
    const SubscriptionToken token =
        (*host).visible_changed().subscribe(ReplaceSectionsWhenHidden{sections.get(), replacement});
    (*sections).set_expanded(0, false);
    run_reveal(window, *sections, 1000);
    require((*sections).items().size() == 1U && (*sections).items()[0].id == 7,
            "the handler's items replaced the sections during the reveal");
}

int main() {
    test_seven_segment_patterns();
    test_seven_segment_in_window();
    test_gauge_values_and_alarms();
    test_gauges_in_window();
    test_dial();
    test_step_indicator();
    test_wizard();
    test_callout();
    test_released_callout_closes_whole();
    test_modal_layer();
    test_lookup_combo_box();
    test_about_dialog();
    test_dial_parts_in_the_stock_themes();
    test_high_contrast_gauge_arcs();
    test_expandable_sections_reveal();
    test_expandable_sections_replaced_during_a_reveal();
    std::cout << "final control tests passed\n";
    return EXIT_SUCCESS;
}
