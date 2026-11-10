// ValueBar (the node engine and value bar study, section 3): the soft and
// hard ranges and the shown text; the gestures of study 3.4 as pointer and
// key scripts, each checked for its value and its value_changed and
// value_committed events (a scrub previews through value_changed and
// commits once; a restore commits nothing; a typed value is clamped to the
// hard range, never to the soft one; a refused text keeps the editor open
// with its defect); the field style's arrows; Ctrl+wheel; the semantics of
// both styles (study 3.7); the capabilities' dot syntax, read(), as_type
// and the property table; and one gesture script's event trace. Headless:
// a window that is never shown. NumericUpDown's opt-in scrub mode, the
// gesture's other adopter, is tested here too.

#include "gui_forms/gui_forms.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::abs(left - right) < 1e-9;
    return result;
}

// One event as the trace records it.
enum class TraceKind : std::uint8_t { changed, committed };

struct TraceEntry final {
    TraceKind kind{TraceKind::changed};
    double value{};
};

// Appends each event to the trace (reserved by the fixture).
struct TraceChanged final {
    std::vector<TraceEntry>* entries;
    void operator()(double value) const { (*entries).push_back(TraceEntry{TraceKind::changed, value}); }
};

struct TraceCommitted final {
    std::vector<TraceEntry>* entries;
    void operator()(double value) const { (*entries).push_back(TraceEntry{TraceKind::committed, value}); }
};

// A bar at window x 20 to 220, y 20 to 44, value 10 in a hard range of 0
// to 100 and a soft range of 0 to 50, scrubbing half a unit per unit of
// travel; every event is traced.
struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ValueBar> bar;
    std::unique_ptr<Window> window;
    std::vector<TraceEntry> trace;
    SubscriptionToken changed_token;
    SubscriptionToken committed_token;
};

std::unique_ptr<Fixture> make_fixture() {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    (*fixture).trace.reserve(64U);
    (*fixture).root = make_control<Panel>(StableId("value.root"));
    (*fixture).bar = make_control<ValueBar>(StableId("value.bar"));
    ValueBar& bar = *(*fixture).bar;
    bar.set_requested_bounds({20.0, 20.0, 200.0, 24.0});
    bar.soft_maximum = std::optional<double>{50.0};
    bar.scrub_rate = std::optional<double>{0.5};
    bar.value = 10.0;
    bar.label = std::string("Samples");
    (*(*fixture).root).add_child((*fixture).bar);
    (*fixture).window = std::make_unique<Window>((*fixture).root, Size{400.0, 100.0});
    (*(*fixture).window).perform_layout();
    (*fixture).changed_token = bar.value_changed().subscribe(TraceChanged{&(*fixture).trace});
    (*fixture).committed_token = bar.value_committed().subscribe(TraceCommitted{&(*fixture).trace});
    return fixture;
}

std::size_t count(const std::vector<TraceEntry>& trace, TraceKind kind) {
    std::size_t total = 0U;
    for (const TraceEntry& entry : trace) {
        if (entry.kind == kind) ++total;
    }
    return total;
}

PointerEvent pointer_event(PointerAction action, Point point, Modifier modifiers = Modifier::none,
                           PointerButton button = PointerButton::primary) {
    PointerEvent event{};
    event.action = action;
    event.button = action == PointerAction::move ? PointerButton::none : button;
    event.position = point;
    event.modifiers = modifiers;
    return event;
}

void pointer(Window& window, PointerAction action, Point point, Modifier modifiers = Modifier::none) {
    static_cast<void>(window.dispatch_pointer(pointer_event(action, point, modifiers)));
}

bool wheel(Window& window, Point point, double delta, Modifier modifiers) {
    PointerEvent event{};
    event.action = PointerAction::wheel;
    event.position = point;
    event.wheel_delta = Point{0.0, delta};
    event.modifiers = modifiers;
    const bool handled = window.dispatch_pointer(event);
    return handled;
}

KeyEvent key_down(std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical_key;
    event.modifiers = modifiers;
    return event;
}

void key(Window& window, std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    static_cast<void>(window.dispatch_key(key_down(physical_key, modifiers)));
}

constexpr double row = 30.0;

// The soft range in force is held within the hard range and ordered; the
// fill is the value's proportion of it; a value outside it is overrange.
void ranges() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    ValueBar& bar = *(*fixture).bar;
    require(near(bar.effective_soft_minimum(), 0.0) && near(bar.effective_soft_maximum(), 50.0),
            "no soft minimum is the hard one");
    require(near(bar.soft_proportion(), 0.2) && !bar.overrange(), "10 of 0..50 fills a fifth");
    require(near(bar.fill_bounds().width, 40.0), "the fill is that proportion of the track");
    bar.soft_maximum = std::optional<double>{500.0};
    require(near(bar.effective_soft_maximum(), 100.0), "a soft limit past the hard one is held at it");
    bar.soft_minimum = std::optional<double>{80.0};
    bar.soft_maximum = std::optional<double>{60.0};
    require(near(bar.effective_soft_minimum(), 60.0) && near(bar.effective_soft_maximum(), 80.0),
            "crossed soft limits are ordered");
    require(bar.overrange() && near(bar.soft_proportion(), 0.0), "below the soft range: overrange and empty");
    require(!bar.overrange_bounds().empty() && near(bar.overrange_bounds().x, 0.0),
            "the overrange marker is at the end the value passed");
    bool refused = false;
    try {
        bar.soft_minimum = std::optional<double>{std::nan("")};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a non-finite soft limit throws");
    refused = false;
    try {
        bar.precision = 13;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && bar.precision.read() == 3, "a precision past 12 throws and changes nothing");
    require(count((*fixture).trace, TraceKind::committed) == 0U, "programmatic writes commit nothing");
}

// The shown text: the precision and the unit's display.
void shown_text() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    ValueBar& bar = *(*fixture).bar;
    require(bar.formatted_value() == "10.000", "three decimals by default");
    bar.precision = 0;
    require(bar.formatted_value() == "10", "precision 0");
    bar.maximum = 1.0;
    bar.value = 0.25;
    bar.unit = ValueUnit::percent;
    require(bar.formatted_value() == "25%", "percent shows the fraction as a percentage");
}

// Study 3.4, press and drag: the scrub previews through value_changed,
// stops at the soft maximum, and the release commits once.
void scrub_script() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {110.0, row});
    require(bar.scrubbing() && near(bar.value.read(), 15.0), "past the threshold the value follows the travel");
    require(bar.states().pressed && bar.states().captured, "a scrubbing bar is pressed and captured");
    pointer(window, PointerAction::move, {300.0, row});
    require(near(bar.value.read(), 50.0), "the scrub stops at the soft maximum");
    pointer(window, PointerAction::up, {300.0, row});
    require(!bar.scrubbing() && near(bar.value.read(), 50.0), "the release keeps the value");
    require(count((*fixture).trace, TraceKind::changed) == 2U, "the two previews change the value; the release at the limit does not");
    require(count((*fixture).trace, TraceKind::committed) == 1U && near((*fixture).trace.back().value, 50.0),
            "the release commits once");
    require(!bar.editing(), "a scrub opens no editor");
}

// Escape and a right press restore the start value and commit nothing.
void scrub_restores() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {120.0, row});
    require(near(bar.value.read(), 20.0), "scrubbed to 20");
    key(window, PhysicalKey::escape);
    require(!bar.scrubbing() && near(bar.value.read(), 10.0), "Escape restores the start value");
    pointer(window, PointerAction::up, {120.0, row});
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {130.0, row});
    static_cast<void>(window.dispatch_pointer(
        pointer_event(PointerAction::down, {130.0, row}, Modifier::none, PointerButton::secondary)));
    require(!bar.scrubbing() && near(bar.value.read(), 10.0), "a right press restores");
    pointer(window, PointerAction::up, {130.0, row});
    require(count((*fixture).trace, TraceKind::committed) == 0U, "a restore commits nothing");
}

// Shift scrubs at a tenth; Ctrl snaps to the snap step from the soft
// minimum.
void scrub_modifiers() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    bar.snap_step = std::optional<double>{5.0};
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {110.0, row});
    pointer(window, PointerAction::move, {130.0, row}, Modifier::shift);
    require(near(bar.value.read(), 16.0), "Shift: twenty units at a tenth of the rate");
    pointer(window, PointerAction::move, {133.0, row}, Modifier::control);
    require(near(bar.value.read(), 20.0), "Ctrl: 17.5 shown snapped to 20");
    pointer(window, PointerAction::up, {133.0, row}, Modifier::control);
    require(near(bar.value.read(), 20.0), "the snapped value is committed");
}

// A value typed outside the soft range (but within the hard one) stays
// there, and scrubbing moves from it without jumping into the soft range.
void typed_outside_soft_range() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    require(bar.apply_text("80") == ValueTextDefect::none && near(bar.value.read(), 80.0),
            "a typed value may leave the soft range");
    require(bar.overrange() && near(bar.soft_proportion(), 1.0), "the fill shows full, overrange");
    require(bar.apply_text("500") == ValueTextDefect::none && near(bar.value.read(), 100.0),
            "a typed value is clamped to the hard range");
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {90.0, row});
    require(near(bar.value.read(), 95.0), "a scrub from outside the soft range starts where it is");
    pointer(window, PointerAction::up, {90.0, row});
}

// Study 3.5, click to type: a click without a drag opens the editor with
// the value; an accepted text commits once (arithmetic, clamped to the hard
// range); a refused one keeps the editor open with its defect; Escape
// cancels; an unchanged text leaves the value as it was.
void click_to_type() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::up, {101.0, row});
    require(bar.editing() && bar.editor() != nullptr, "a click opens the editor");
    TextBox& editor = *bar.editor();
    require(editor.visible.read() && editor.text.read() == "10.000", "the editor shows the value");
    require(bar.semantic_descriptor().include_descendants, "the open editor is published");
    editor.text = std::string("1+");
    require(!bar.commit_edit() && bar.editing(), "a refused text keeps the editor open");
    require(bar.edit_defect() == ValueTextDefect::missing_operand && bar.states().invalid,
            "the defect is shown as invalid");
    editor.text = std::string("2 * (3 + 4)");
    key(window, PhysicalKey::enter);
    require(!bar.editing() && !editor.visible.read() && near(bar.value.read(), 14.0), "Enter commits the arithmetic");
    require(bar.edit_defect() == ValueTextDefect::none && !bar.states().invalid, "the defect is cleared");
    require(count((*fixture).trace, TraceKind::committed) == 1U, "one commit");
    require(bar.begin_edit(), "F2's path opens the editor again");
    editor.text = std::string("77");
    key(window, PhysicalKey::escape);
    require(!bar.editing() && near(bar.value.read(), 14.0), "Escape cancels");
    bar.value = 0.12345;
    require(bar.begin_edit() && editor.text.read() == "0.123", "the editor opens at the shown precision");
    require(bar.commit_edit() && near(bar.value.read(), 0.12345), "an unchanged text leaves the value unrounded");
    require(count((*fixture).trace, TraceKind::committed) == 1U, "cancel and unchanged commit nothing");
}

// Study 3.4, keys on the focused bar.
void keys() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    bar.minimum = -100.0;
    bar.default_value = 25.0;
    require(window.request_focus((*fixture).bar), "the bar takes focus");
    key(window, PhysicalKey::right);
    require(near(bar.value.read(), 11.0), "Right steps by the small change");
    key(window, PhysicalKey::right, Modifier::shift);
    require(near(bar.value.read(), 11.1), "Shift steps a tenth");
    key(window, PhysicalKey::left, Modifier::control);
    require(near(bar.value.read(), 1.1), "Ctrl steps by the large change");
    key(window, PhysicalKey::end);
    require(near(bar.value.read(), 50.0), "End goes to the soft maximum");
    key(window, PhysicalKey::home);
    require(near(bar.value.read(), -100.0), "Home goes to the soft minimum (the hard one here)");
    key(window, PhysicalKey::backspace);
    require(near(bar.value.read(), 25.0), "Backspace resets to the default");
    key(window, PhysicalKey::minus);
    require(near(bar.value.read(), -25.0), "Minus negates");
    require(count((*fixture).trace, TraceKind::committed) == 7U, "each key is one commit");
    key(window, PhysicalKey::enter);
    require(bar.editing(), "Enter opens the editor");
    key(window, PhysicalKey::escape);
    require(!bar.editing(), "and Escape closes it");
}

// The field style: arrows at both ends step by the small change; the fill
// style has neither arrows nor a fill in the field style.
void field_style() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    bar.presentation = ValueBarStyle::field;
    (*(*fixture).window).perform_layout();
    require(bar.fill_bounds().empty() && !bar.decrement_bounds().empty(), "the field style: arrows, no fill");
    pointer(window, PointerAction::down, {25.0, row});
    pointer(window, PointerAction::up, {25.0, row});
    require(near(bar.value.read(), 9.0), "the left arrow decrements");
    pointer(window, PointerAction::down, {215.0, row});
    pointer(window, PointerAction::up, {215.0, row});
    pointer(window, PointerAction::down, {215.0, row});
    pointer(window, PointerAction::up, {215.0, row});
    require(near(bar.value.read(), 11.0), "the right arrow increments");
    require(!bar.editing(), "an arrow opens no editor");
    require(bar.semantic_descriptor().role == SemanticRole::numeric_field, "the field style is a numeric field");
}

// Ctrl+wheel steps; a plain wheel is left to the parent.
void wheel_steps() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    static_cast<void>(wheel(window, {100.0, row}, 1.0, Modifier::none));
    require(near(bar.value.read(), 10.0), "a plain wheel changes nothing");
    static_cast<void>(wheel(window, {100.0, row}, 1.0, Modifier::control));
    require(near(bar.value.read(), 11.0), "Ctrl+wheel steps");
}

// Study 3.7: a slider with its range, steps and shown text; set_value
// takes typed text with units.
void semantics() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    ValueBar& bar = *(*fixture).bar;
    const SemanticDescriptor descriptor = bar.semantic_descriptor();
    require(descriptor.role == SemanticRole::slider, "the fill style is a slider");
    require(descriptor.name == "Samples", "the label names it without an accessible name");
    require(descriptor.value == "10.000" && descriptor.numeric_value && near(*descriptor.numeric_value, 10.0),
            "the value and its text");
    require(descriptor.minimum_value && near(*descriptor.minimum_value, 0.0) && descriptor.maximum_value &&
                near(*descriptor.maximum_value, 100.0),
            "the hard range");
    require(descriptor.small_step && near(*descriptor.small_step, 1.0) && descriptor.large_step &&
                near(*descriptor.large_step, 10.0),
            "the steps");
    require(!descriptor.include_descendants, "the closed editor is not published");
    require(bar.on_semantic_action(SemanticAction::increment, {}) && near(bar.value.read(), 11.0),
            "increment steps");
    bar.maximum = 1.0;
    bar.unit = ValueUnit::percent;
    require(bar.on_semantic_action(SemanticAction::set_value, "40%") && near(bar.value.read(), 0.4),
            "set_value takes a unit");
    require(!bar.on_semantic_action(SemanticAction::set_value, "4 parsecs") && near(bar.value.read(), 0.4),
            "an unknown unit is refused");
}

// The capabilities (property-capabilities study): dot syntax writes, read()
// reads, as_type converts, and the property table reaches the same cells.
void capabilities() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    ValueBar& bar = *(*fixture).bar;
    bar.precision = 1;
    require(bar.precision.read() == 1, "dot syntax and read()");
    const std::int64_t widened = bar.precision.as_type(types::int64);
    require(widened == 1, "as_type widens");
    const std::optional<BindingValue> presentation = bar.property_value("presentation");
    require(presentation && std::get<PropertyEnumValue>(*presentation).name == "fill", "the table names the style");
    bar.set_property_value("presentation", BindingValue{PropertyEnumValue{"value_bar_style", "field", 1}});
    require(bar.presentation.read() == ValueBarStyle::field, "a write by name reaches the cell");
    bar.set_property_value("soft_minimum", BindingValue{5.0});
    require(bar.soft_minimum.read() == std::optional<double>{5.0}, "a nullable number by name");
    require(bar.anatomy().control_id == "value_bar" && bar.anatomy().parts.size() == 12U, "the anatomy");
}

// One script's event trace: scrub with a preview, restore by Escape, scrub
// again and commit, then a typed commit.
void trace_script() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    ValueBar& bar = *(*fixture).bar;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {106.0, row});
    key(window, PhysicalKey::escape);
    pointer(window, PointerAction::up, {106.0, row});
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {108.0, row});
    pointer(window, PointerAction::up, {110.0, row});
    require(bar.apply_text("3^2") == ValueTextDefect::none, "a typed power");
    const std::vector<TraceEntry> expected{
        {TraceKind::changed, 13.0},   {TraceKind::changed, 10.0},  {TraceKind::changed, 14.0},
        {TraceKind::changed, 15.0},   {TraceKind::committed, 15.0}, {TraceKind::changed, 9.0},
        {TraceKind::committed, 9.0},
    };
    const std::vector<TraceEntry>& seen = (*fixture).trace;
    require(seen.size() == expected.size(), "the trace's length");
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        require(seen[index].kind == expected[index].kind && near(seen[index].value, expected[index].value),
                "the trace's entries");
    }
    std::cout << "trace:";
    for (const TraceEntry& entry : seen) {
        std::cout << ' ' << (entry.kind == TraceKind::changed ? "changed " : "committed ") << entry.value;
    }
    std::cout << '\n';
}

// NumericUpDown's opt-in scrub mode (the shared ScrubGesture): off, a drag
// on the field changes nothing; on, a drag on the unfocused field scrubs
// by the increment per unit, rounded to the shown decimals; a click focuses
// the field with its text selected, after which the pointer edits text;
// Escape restores.
void numeric_up_down_scrub() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("spin.root"));
    std::shared_ptr<NumericUpDown> spin = make_control<NumericUpDown>(StableId("spin.value"));
    (*spin).set_requested_bounds({20.0, 40.0, 120.0, 24.0});
    (*spin).value = 10.0;
    std::shared_ptr<Button> other = make_control<Button>(StableId("spin.other"));
    (*other).set_requested_bounds({200.0, 40.0, 80.0, 24.0});
    (*root).add_child(spin);
    (*root).add_child(other);
    Window window(root, Size{400.0, 100.0});
    window.perform_layout();
    const double line = 52.0;
    pointer(window, PointerAction::down, {40.0, line});
    pointer(window, PointerAction::move, {60.0, line});
    pointer(window, PointerAction::up, {60.0, line});
    require(near((*spin).value.read(), 10.0) && !(*spin).scrubbing(), "scrub off: a drag changes nothing");
    require(window.request_focus(other), "the focus leaves the field");
    (*spin).scrub = true;
    require((*spin).scrub.read(), "scrub mode on");
    pointer(window, PointerAction::down, {40.0, line});
    pointer(window, PointerAction::move, {50.0, line});
    require((*spin).scrubbing() && near((*spin).value.read(), 20.0), "a drag scrubs by the increment per unit");
    pointer(window, PointerAction::move, {53.0, line}, Modifier::shift);
    require(near((*spin).value.read(), 20.0), "Shift: 0.3 rounds away at no decimals");
    pointer(window, PointerAction::up, {53.0, line});
    require(!(*spin).scrubbing() && near((*spin).value.read(), 20.0), "the release keeps the value");
    require(window.focused_control() != (*spin).editor(), "a scrub does not focus the field");
    pointer(window, PointerAction::down, {40.0, line});
    pointer(window, PointerAction::move, {60.0, line});
    key(window, PhysicalKey::escape);
    pointer(window, PointerAction::up, {60.0, line});
    require(near((*spin).value.read(), 20.0), "Escape restores the start value");
    pointer(window, PointerAction::down, {40.0, line});
    pointer(window, PointerAction::up, {41.0, line});
    require(window.focused_control() == (*spin).editor(), "a click focuses the field");
    pointer(window, PointerAction::down, {40.0, line});
    pointer(window, PointerAction::move, {70.0, line});
    pointer(window, PointerAction::up, {70.0, line});
    require(near((*spin).value.read(), 20.0), "a focused field edits text instead of scrubbing");
}

} // namespace

int main() {
    ranges();
    shown_text();
    scrub_script();
    scrub_restores();
    scrub_modifiers();
    typed_outside_soft_range();
    click_to_type();
    keys();
    field_style();
    wheel_steps();
    semantics();
    capabilities();
    trace_script();
    numeric_up_down_scrub();
    std::cout << "value bar: ranges, text, scrubbing, typing, keys, arrows, wheel, semantics and capabilities passed\n";
    return 0;
}
