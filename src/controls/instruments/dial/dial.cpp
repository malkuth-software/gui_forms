#include "gui_forms/controls/instruments/dial/dial.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The scale: from -135 to +135 degrees about twelve o'clock.
constexpr double sweep_start = -0.75 * std::numbers::pi;
constexpr double sweep = 1.5 * std::numbers::pi;
// The scale dots drawn without scale marks to space them; with marks, one
// dot per mark, at most this many.
constexpr std::size_t plain_dots = 33U;
constexpr std::size_t most_dots = 101U;
// A scale dot is this share of the face's side; the knob this share.
constexpr double dot_share = 0.07;
constexpr double knob_share = 0.56;
constexpr double pointer_share = 0.16;

constexpr std::array<PartField<Dial::Parts>, 7> dial_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Dial::Parts::region},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}},
     .member = &Dial::Parts::track},
    {.declaration = {.name = "fill", .role = PartRole::fill, .composed_from = {}},
     .member = &Dial::Parts::fill},
    {.declaration = {.name = "graticule", .role = PartRole::graticule, .composed_from = {}},
     .member = &Dial::Parts::graticule},
    {.declaration = {.name = "thumb", .role = PartRole::thumb, .composed_from = {}},
     .member = &Dial::Parts::thumb},
    {.declaration = {.name = "indicator", .role = PartRole::indicator, .composed_from = {}},
     .member = &Dial::Parts::indicator},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &Dial::Parts::focus},
}};

constexpr std::array<PartDeclaration, 7> dial_parts = part_declarations(dial_part_fields);

constexpr std::array<AnatomyState, 9> dial_states{
    AnatomyState::normal,  AnatomyState::pressed, AnatomyState::captured,
    AnatomyState::enabled, AnatomyState::disabled, AnatomyState::focused,
    AnatomyState::focus_visible, AnatomyState::selected, AnatomyState::hot,
};

constexpr ControlAnatomy dial_anatomy{
    .control_id = "dial",
    .parts = dial_parts,
    .states = dial_states,
    .custom_states = {},
};

Rect joined(Rect left, Rect right) noexcept {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const double x = std::min(left.x, right.x);
    const double y = std::min(left.y, right.y);
    const double right_edge = std::max(left.x + left.width, right.x + right.width);
    const double bottom = std::max(left.y + left.height, right.y + right.height);
    const Rect result{x, y, right_edge - x, bottom - y};
    return result;
}

} // namespace

Dial::Dial(StableId stable_id) : RangeControl(std::move(stable_id)) {
    focusable = true;
    cursor = CursorKind::hand;
}

double Dial::ratio() const noexcept {
    const double span = maximum.read() - minimum.read();
    if (!(span > 0.0)) return 0.0;
    const double result = std::clamp((value.read() - minimum.read()) / span, 0.0, 1.0);
    return result;
}

double Dial::pointer_angle() const noexcept {
    const double angle = sweep_start + sweep * ratio();
    return angle;
}

Dial::Face Dial::face() const noexcept {
    Face placed;
    const Rect bounds = local_bounds();
    const double side = std::floor(std::min(bounds.width, bounds.height));
    if (!(side > 0.0)) return placed;
    placed.centre = Point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    placed.dot = std::max(2.0, std::round(side * dot_share));
    placed.mark = std::max(1.0, std::round(placed.dot * 0.5));
    placed.mark_radius = std::max(0.0, side * 0.5 - placed.mark);
    placed.radius = std::max(0.0, placed.mark_radius - placed.mark - placed.dot * 0.75);
    placed.knob = std::floor(side * knob_share);
    return placed;
}

Rect Dial::dot_at(Point centre, double radius, double at, double side) noexcept {
    // Clockwise from twelve o'clock, in y-down coordinates.
    const double angle = sweep_start + sweep * at;
    const double x = centre.x + radius * std::sin(angle);
    const double y = centre.y - radius * std::cos(angle);
    const Rect square{x - side * 0.5, y - side * 0.5, side, side};
    return square;
}

Rect Dial::knob_bounds() const noexcept {
    const Face placed = face();
    const Rect knob{placed.centre.x - placed.knob * 0.5, placed.centre.y - placed.knob * 0.5, placed.knob,
                    placed.knob};
    return knob;
}

double Dial::value_from_window_point(Point point, double previous) const noexcept {
    const Rect absolute = absolute_bounds();
    const Face placed = face();
    const double dx = point.x - (absolute.x + placed.centre.x);
    const double dy = point.y - (absolute.y + placed.centre.y);
    if (dx == 0.0 && dy == 0.0) return previous;
    // Clockwise from twelve o'clock, from -pi to pi.
    const double angle = std::atan2(dx, -dy);
    double at = 0.0;
    if (angle < sweep_start || angle > sweep_start + sweep) {
        // In the gap at the bottom: stay at the end the value is nearer.
        const double span = maximum.read() - minimum.read();
        const double previous_ratio = span > 0.0 ? (previous - minimum.read()) / span : 0.0;
        at = previous_ratio < 0.5 ? 0.0 : 1.0;
    } else {
        at = (angle - sweep_start) / sweep;
    }
    const double result = minimum.read() + at * (maximum.read() - minimum.read());
    return result;
}

AnatomyStateSet Dial::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (tracking_) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
        states.insert(AnatomyState::captured);
    }
    return states;
}

Size Dial::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : 72.0;
    const double height = requested.height > 0.0 ? requested.height : 72.0;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void Dial::on_paint(Painter& painter, Rect) {
    const Face placed = face();
    if (!(placed.radius > 0.0)) return;
    const AnatomyStateSet states = anatomy_states();
    AnatomyStateSet lit_states = states;
    lit_states.insert(AnatomyState::selected);

    // One dot per scale mark when marks are drawn, else a plain run.
    const double frequency = tick_frequency.read();
    const double span = maximum.read() - minimum.read();
    std::size_t dots = plain_dots;
    if (frequency > 0.0 && span > 0.0) {
        const double requested = std::floor(span / frequency) + 1.0;
        dots = static_cast<std::size_t>(std::clamp(requested, 2.0, static_cast<double>(most_dots)));
    }
    const double reading = ratio();
    PartPaint dot{.states = states, .family = ControlVisualRole::progress};
    dot.corner_radius = placed.dot * 0.5;
    for (std::size_t index = 0U; index < dots; ++index) {
        const double at = static_cast<double>(index) / static_cast<double>(dots - 1U);
        const bool is_lit = show_arc.read() && at <= reading + 1e-9;
        dot.role = is_lit ? PartRole::fill : PartRole::track;
        dot.key = PartKey{is_lit ? std::string_view{"fill"} : std::string_view{"track"}, {}, {}};
        dot.states = is_lit ? lit_states : states;
        dot.bounds = dot_at(placed.centre, placed.radius, at, placed.dot);
        paint_part(painter, *this, dot);
    }

    if (frequency > 0.0) {
        PartPaint mark{.role = PartRole::graticule, .states = states, .family = ControlVisualRole::progress};
        mark.corner_radius = placed.mark * 0.5;
        for (std::size_t index = 0U; index < dots; ++index) {
            const double at = static_cast<double>(index) / static_cast<double>(dots - 1U);
            mark.bounds = dot_at(placed.centre, placed.mark_radius, at, placed.mark);
            paint_part(painter, *this, mark);
        }
    }

    const Rect knob = knob_bounds();
    PartPaint thumb{.role = PartRole::thumb, .bounds = knob, .states = states, .family = ControlVisualRole::button};
    thumb.corner_radius = knob.width * 0.5;
    thumb.key = PartKey{"thumb", {}, {}};
    paint_part(painter, *this, thumb);

    const double pointer_side = std::max(2.0, std::round(knob.width * pointer_share));
    const double pointer_radius = std::max(0.0, knob.width * 0.5 - pointer_side);
    PartPaint pointer{.role = PartRole::indicator,
                      .bounds = dot_at(placed.centre, pointer_radius, reading, pointer_side),
                      .states = lit_states,
                      .family = ControlVisualRole::progress};
    pointer.corner_radius = pointer_side * 0.5;
    pointer.key = PartKey{"indicator", {}, {}};
    paint_part(painter, *this, pointer);

    if (focused_) {
        PartPaint focus = thumb;
        focus.role = PartRole::focus;
        focus.states.insert(AnatomyState::focus_visible);
        focus.key = PartKey{"focus", {}, {}};
        paint_part(painter, *this, focus);
    }
}

void Dial::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::wheel) {
        if (event.wheel_delta.y != 0.0) {
            const bool incrementing = event.wheel_delta.y > 0.0;
            set_value_from_input(value.read() + (incrementing ? small_change.read() : -small_change.read()),
                                 incrementing ? RangeAction::small_increment : RangeAction::small_decrement);
            event.handled = true;
        }
        return;
    }
    if (event.button == PointerButton::primary && event.action == PointerAction::down) {
        tracking_ = true;
        set_value_from_input(value_from_window_point(event.position, value.read()), RangeAction::thumb_track);
        invalidate(Dirty::paint);
        event.handled = true;
    } else if (event.action == PointerAction::move && tracking_) {
        set_value_from_input(value_from_window_point(event.position, value.read()), RangeAction::thumb_track);
        event.handled = true;
    } else if (event.button == PointerButton::primary && event.action == PointerAction::up && tracking_) {
        tracking_ = false;
        set_value_from_input(value_from_window_point(event.position, value.read()), RangeAction::thumb_position);
        invalidate(Dirty::paint);
        event.handled = true;
    }
}

void Dial::on_key(KeyEvent& event) {
    if (event.action != KeyAction::down) return;
    double next = value.read();
    RangeAction action = RangeAction::thumb_position;
    bool recognized = true;
    switch (event.physical_key) {
    case PhysicalKey::left:
    case PhysicalKey::down:
        next -= small_change.read();
        action = RangeAction::small_decrement;
        break;
    case PhysicalKey::right:
    case PhysicalKey::up:
        next += small_change.read();
        action = RangeAction::small_increment;
        break;
    case PhysicalKey::page_down:
        next -= large_change.read();
        action = RangeAction::large_decrement;
        break;
    case PhysicalKey::page_up:
        next += large_change.read();
        action = RangeAction::large_increment;
        break;
    case PhysicalKey::home:
        next = minimum.read();
        action = RangeAction::first;
        break;
    case PhysicalKey::end:
        next = maximum.read();
        action = RangeAction::last;
        break;
    default:
        recognized = false;
        break;
    }
    if (recognized) {
        set_value_from_input(next, action);
        event.handled = true;
    }
}

void Dial::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused) tracking_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor Dial::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::slider;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    std::ostringstream shown;
    shown.imbue(std::locale::classic());
    shown << value.read();
    descriptor.value = shown.str();
    descriptor.numeric_value = value.read();
    descriptor.minimum_value = minimum.read();
    descriptor.maximum_value = maximum.read();
    descriptor.small_step = small_change.read();
    descriptor.large_step = large_change.read();
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment, SemanticAction::decrement,
                          SemanticAction::set_value};
    descriptor.exposed = true;
    return descriptor;
}

bool Dial::on_semantic_action(SemanticAction action, std::string_view value_text) {
    if (action == SemanticAction::increment || action == SemanticAction::decrement) {
        const bool increasing = action == SemanticAction::increment;
        set_value_from_input(value.read() + (increasing ? small_change.read() : -small_change.read()),
                             increasing ? RangeAction::small_increment : RangeAction::small_decrement);
        return true;
    }
    if (action == SemanticAction::set_value) {
        try {
            std::size_t consumed{};
            const double parsed = std::stod(std::string(value_text), &consumed);
            if (consumed != value_text.size() || !std::isfinite(parsed)) return false;
            const bool changed = set_value_from_input(parsed, RangeAction::thumb_position);
            return changed || parsed == value.read();
        } catch (const std::exception&) {
            return false;
        }
    }
    return RangeControl::on_semantic_action(action, value_text);
}

const ControlAnatomy& Dial::anatomy() const noexcept {
    return dial_anatomy;
}

void Dial::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Face placed = face();
    Parts record;
    record.region = PartGeometry::from_bounds(local_bounds());
    if (placed.radius > 0.0) {
        Rect track{};
        Rect fill{};
        const double reading = ratio();
        for (std::size_t index = 0U; index < plain_dots; ++index) {
            const double at = static_cast<double>(index) / static_cast<double>(plain_dots - 1U);
            const Rect square = dot_at(placed.centre, placed.radius, at, placed.dot);
            track = joined(track, square);
            if (show_arc.read() && at <= reading + 1e-9) fill = joined(fill, square);
        }
        record.track = PartGeometry::from_bounds(track);
        record.fill = PartGeometry::from_bounds(fill);
        if (tick_frequency.read() > 0.0) {
            const Rect low = dot_at(placed.centre, placed.mark_radius, 0.0, placed.mark);
            const Rect top = dot_at(placed.centre, placed.mark_radius, 0.5, placed.mark);
            const Rect high = dot_at(placed.centre, placed.mark_radius, 1.0, placed.mark);
            const Rect left_side = dot_at(placed.centre, placed.mark_radius, 1.0 / 6.0, placed.mark);
            const Rect right_side = dot_at(placed.centre, placed.mark_radius, 5.0 / 6.0, placed.mark);
            record.graticule = PartGeometry::from_bounds(joined(joined(joined(low, top), joined(high, left_side)),
                                                                right_side));
        }
        const Rect knob = knob_bounds();
        record.thumb = PartGeometry::from_bounds(knob);
        const double pointer_side = std::max(2.0, std::round(knob.width * pointer_share));
        const double pointer_radius = std::max(0.0, knob.width * 0.5 - pointer_side);
        record.indicator = PartGeometry::from_bounds(dot_at(placed.centre, pointer_radius, reading, pointer_side));
        record.focus = PartGeometry::from_bounds(knob);
    }
    write_parts(anatomy(), dial_part_fields, record, geometry);
}

Dial::Parts Dial::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), dial_part_fields, published.parts(), record);
    return record;
}

// property_codemod begin Dial (tools/property_families/dial.json; generated, do not edit)

namespace {

PropertyDescriptor dial_tick_frequency_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "tick_frequency";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Appearance";
    descriptor.description = "Distance in value units between scale marks.";
    descriptor.default_value = BindingValue{10.0};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_dial_tick_frequency() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        dial_tick_frequency_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor dial_show_arc_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_arc";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the scale lights the arc up to the value.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_dial_show_arc() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        dial_show_arc_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, Dial::dial_property_count>& Dial::dial_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 2U> own{{
        {"tick_frequency", &describe_dial_tick_frequency,
         &read_cell_entry<Dial, decltype(&Dial::tick_frequency), &Dial::tick_frequency>,
         &write_cell_entry<Dial, decltype(&Dial::tick_frequency), &Dial::tick_frequency>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"show_arc", &describe_dial_show_arc,
         &read_cell_entry<Dial, decltype(&Dial::show_arc), &Dial::show_arc>,
         &write_cell_entry<Dial, decltype(&Dial::show_arc), &Dial::show_arc>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Dial's property table names a property twice");
    static const std::array<PropertyEntry, dial_property_count> entries =
        join_property_tables(RangeControl::range_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> Dial::property_table() const noexcept {
    const std::array<PropertyEntry, dial_property_count>& entries = dial_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void Dial::prepare_write(const decltype(tick_frequency)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0) throw std::invalid_argument("dial tick frequency must be finite and not negative");
}

void Dial::written(const decltype(tick_frequency)&) {
    invalidate(Dirty::paint);
}

void Dial::prepare_write(const decltype(show_arc)&, bool&) {
    require_mutable();
}

void Dial::written(const decltype(show_arc)&) {
    invalidate(Dirty::paint);
}

// property_codemod end Dial

} // namespace gui_forms
