#include "gui_forms/controls/track_bar.hpp"

#include "../range_control_rendering.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "../../basic/number_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace range_control_detail;

namespace {

// The TrackBarVisualStyle enumeration, numbered as declared. Static storage; any thread
// may read it.
std::shared_ptr<const PropertyEnumDescriptor> track_bar_visual_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "track_bar_visual_style", {{"classic", 0}, {"filled", 1}, {"compact", 2}}, false});
    return value;
}

} // namespace

TrackBar::TrackBar(StableId stable_id)
    : RangeControl(std::move(stable_id)) {
    focusable = true;
    cursor = CursorKind::hand;
}

Size TrackBar::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size preferred = orientation.read() == Orientation::horizontal
        ? Size{requested.width > 0.0 ? requested.width : 160.0,
               requested.height > 0.0 ? requested.height : 28.0}
        : Size{requested.width > 0.0 ? requested.width : 28.0,
               requested.height > 0.0 ? requested.height : 160.0};
    return {std::min(available.width, preferred.width),
            std::min(available.height, preferred.height)};
}

namespace {

// The thumb is pressed and captures the pointer while dragged.
constexpr std::array<AnatomyState, 2> track_bar_thumb_states{{
    AnatomyState::pressed, AnatomyState::captured}};

constexpr std::array<PartField<TrackBar::Parts>, 6> track_bar_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &TrackBar::Parts::region},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}},
     .member = &TrackBar::Parts::track},
    {.declaration = {.name = "fill", .role = PartRole::fill, .composed_from = {}},
     .member = &TrackBar::Parts::fill},
    {.declaration = {.name = "ticks", .role = PartRole::glyph, .composed_from = {}},
     .member = &TrackBar::Parts::ticks},
    {.declaration = {.name = "thumb", .role = PartRole::thumb, .composed_from = {},
                     .states = track_bar_thumb_states},
     .member = &TrackBar::Parts::thumb},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &TrackBar::Parts::focus},
}};

constexpr std::array<StateField<TrackBar::States>, 7> track_bar_state_fields{{
    {.state = AnatomyState::normal, .member = &TrackBar::States::normal},
    {.state = AnatomyState::pressed, .member = &TrackBar::States::pressed},
    {.state = AnatomyState::captured, .member = &TrackBar::States::captured},
    {.state = AnatomyState::enabled, .member = &TrackBar::States::enabled},
    {.state = AnatomyState::disabled, .member = &TrackBar::States::disabled},
    {.state = AnatomyState::focused, .member = &TrackBar::States::focused},
    {.state = AnatomyState::focus_visible, .member = &TrackBar::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 6> track_bar_parts =
    part_declarations(track_bar_part_fields);
constexpr std::array<AnatomyState, 7> track_bar_states =
    state_declarations(track_bar_state_fields);

constexpr ControlAnatomy track_bar_anatomy{
    .control_id = "track_bar",
    .parts = track_bar_parts,
    .states = track_bar_states,
    .custom_states = {},
};

} // namespace

Rect TrackBar::value_span_bounds() const noexcept {
    const Rect bounds = local_bounds();
    if (orientation.read() == Orientation::horizontal) {
        const Rect span{track_inset, bounds.height * 0.5 - 2.5,
                        std::max(0.0, bounds.width - track_inset * 2.0), 5.0};
        return span;
    }
    const Rect span{bounds.width * 0.5 - 2.5, track_inset, 5.0,
                    std::max(0.0, bounds.height - track_inset * 2.0)};
    return span;
}

Rect TrackBar::track_bounds() const noexcept {
    const Rect span = value_span_bounds();
    if (visual_style.read() == TrackBarVisualStyle::classic) {
        return span;
    }
    if (orientation.read() == Orientation::horizontal) {
        const Rect groove{span.x, span.y + 1.5, span.width, 2.0};
        return groove;
    }
    const Rect groove{span.x + 1.5, span.y, 2.0, span.height};
    return groove;
}

Rect TrackBar::fill_bounds() const noexcept {
    if (visual_style.read() == TrackBarVisualStyle::classic) {
        return {};
    }
    Rect filled = track_bounds();
    if (orientation.read() == Orientation::horizontal) {
        // From the minimum's end: the right of a mirrored bar.
        const double width = filled.width * normalized_value();
        if (mirrored_axis()) filled.x += filled.width - width;
        filled.width = width;
    } else {
        const double height = filled.height * normalized_value();
        filled.y += filled.height - height;
        filled.height = height;
    }
    return filled;
}

Rect TrackBar::thumb_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const Rect span = value_span_bounds();
    const double ratio = normalized_value();
    double thumb_side = 14.0;
    if (visual_style.read() == TrackBarVisualStyle::compact) {
        thumb_side = 8.0;
    } else if (visual_style.read() == TrackBarVisualStyle::filled) {
        thumb_side = 12.0;
    }
    if (thumb_extent.read() > 0.0) thumb_side = thumb_extent.read();
    if (orientation.read() == Orientation::horizontal) {
        const double along = mirrored_axis() ? 1.0 - ratio : ratio;
        const Rect thumb{span.x + along * span.width - thumb_side * 0.5,
                         std::max(2.0, (bounds.height - thumb_side) * 0.5),
                         thumb_side, thumb_side};
        return thumb;
    }
    const Rect thumb{std::max(2.0, (bounds.width - thumb_side) * 0.5),
                     span.y + (1.0 - ratio) * span.height - thumb_side * 0.5,
                     thumb_side, thumb_side};
    return thumb;
}

bool TrackBar::ticks_drawn() const noexcept {
    return show_ticks.read() && visual_style.read() != TrackBarVisualStyle::compact;
}

Rect TrackBar::ticks_bounds() const noexcept {
    if (!ticks_drawn()) {
        return {};
    }
    // Tick lines are one unit wide and centred on their positions, so the
    // band reaches half a unit past each end of the value span.
    const Rect span = value_span_bounds();
    if (orientation.read() == Orientation::horizontal) {
        const Rect band{span.x - 0.5, span.bottom() + 2.0, span.width + 1.0, 3.0};
        return band;
    }
    const Rect band{span.right() + 2.0, span.y - 0.5, 3.0, span.height + 1.0};
    return band;
}

const ControlAnatomy& TrackBar::anatomy() const noexcept {
    return track_bar_anatomy;
}

void TrackBar::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect thumb = thumb_bounds();
    Parts record;
    record.region = PartGeometry::from_bounds(local_bounds());
    record.track = PartGeometry::from_bounds(track_bounds());
    record.fill = PartGeometry::from_bounds(fill_bounds());
    record.ticks = PartGeometry::from_bounds(ticks_bounds());
    record.thumb = PartGeometry::from_bounds(thumb);
    if (tracking_) {
        record.thumb.states.insert(AnatomyState::pressed);
        record.thumb.states.insert(AnatomyState::captured);
    }
    // The track bar shows focus on its thumb.
    record.focus = PartGeometry::from_bounds(thumb);
    write_parts(anatomy(), track_bar_part_fields, record, geometry);
}

AnatomyStateSet TrackBar::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (tracking_) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
        states.insert(AnatomyState::captured);
    }
    return states;
}

TrackBar::Parts TrackBar::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), track_bar_part_fields, published.parts(), record);
    return record;
}

TrackBar::States TrackBar::states() const {
    States record;
    read_states(track_bar_state_fields, anatomy_states(), record);
    return record;
}

// The track, the fill (modern style), the ticks, the thumb and the focus
// cue are parts the theme's engine draws in the control's anatomy states.
// The classic style shows the value span as its track and no fill; the
// modern style the full track and the filled part of it.
void TrackBar::on_paint(Painter& painter, Rect) {
    const bool horizontal = orientation.read() == Orientation::horizontal;
    const Rect span = value_span_bounds();
    const AnatomyStateSet states = anatomy_states();
    const bool classic = visual_style.read() == TrackBarVisualStyle::classic;
    PartPaint track;
    track.family = ControlVisualRole::progress;
    track.role = PartRole::track;
    track.bounds = classic ? span : track_bounds();
    track.states = states;
    track.key = PartKey{"track", {}, {}};
    paint_part(painter, *this, track);
    if (!classic) {
        PartPaint fill;
        fill.family = ControlVisualRole::progress;
        fill.role = PartRole::fill;
        fill.bounds = fill_bounds();
        fill.states = states;
        fill.states.insert(AnatomyState::selected);
        fill.key = PartKey{"fill", {}, {}};
        paint_part(painter, *this, fill);
    }

    if (ticks_drawn()) {
        const double value_range = maximum.read() - minimum.read();
        const double requested_count = std::floor(value_range / tick_frequency.read());
        const std::size_t bounded_count = static_cast<std::size_t>(
            std::min(requested_count, 256.0));
        PartPaint tick;
        tick.family = ControlVisualRole::panel;
        tick.role = PartRole::divider;
        tick.states = states;
        for (std::size_t index = 0; index <= bounded_count; ++index) {
            const double ratio = bounded_count == 0U
                ? 0.0 : static_cast<double>(index) /
                    static_cast<double>(bounded_count);
            if (horizontal) {
                const double x = span.x + ratio * span.width;
                tick.bounds = {x - 0.5, span.y + span.height + 2.0, 1.0, 3.0};
            } else {
                const double y = span.y + (1.0 - ratio) * span.height;
                tick.bounds = {span.x + span.width + 2.0, y - 0.5, 3.0, 1.0};
            }
            paint_part(painter, *this, tick);
        }
    }

    PartPaint thumb;
    thumb.family = ControlVisualRole::button;
    thumb.role = PartRole::thumb;
    thumb.bounds = thumb_bounds();
    thumb.states = states;
    thumb.key = PartKey{"thumb", {}, {}};
    paint_part(painter, *this, thumb);
    if (focused_) {
        PartPaint focus = thumb;
        focus.role = PartRole::focus;
        focus.states.insert(AnatomyState::focus_visible);
        focus.key = PartKey{"focus", {}, {}};
        paint_part(painter, *this, focus);
    }
}

double TrackBar::value_from_window_point(Point point) const noexcept {
    const Rect absolute = absolute_bounds();
    double ratio = 0.0;
    if (orientation.read() == Orientation::horizontal) {
        const double span = std::max(1.0, absolute.width - track_inset * 2.0);
        const double from_left = (point.x - absolute.x - track_inset) / span;
        ratio = mirrored_axis() ? 1.0 - from_left : from_left;
    } else {
        const double span = std::max(1.0, absolute.height - track_inset * 2.0);
        ratio = 1.0 - (point.y - absolute.y - track_inset) / span;
    }
    ratio = std::clamp(ratio, 0.0, 1.0);
    return minimum.read() + ratio * (maximum.read() - minimum.read());
}

void TrackBar::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::wheel) {
        if (event.wheel_delta.y != 0.0) {
            const bool incrementing = event.wheel_delta.y > 0.0;
            set_value_from_input(value.read() + (incrementing ? small_change.read() : -small_change.read()),
                                 incrementing ? RangeAction::small_increment
                                              : RangeAction::small_decrement);
            event.handled = true;
        }
        return;
    }
    if (event.button == PointerButton::primary &&
        event.action == PointerAction::down) {
        tracking_ = true;
        set_value_from_input(value_from_window_point(event.position),
                             RangeAction::thumb_track);
        event.handled = true;
    } else if (event.action == PointerAction::move && tracking_) {
        set_value_from_input(value_from_window_point(event.position),
                             RangeAction::thumb_track);
        event.handled = true;
    } else if (event.button == PointerButton::primary &&
               event.action == PointerAction::up && tracking_) {
        tracking_ = false;
        set_value_from_input(value_from_window_point(event.position),
                             RangeAction::thumb_position);
        event.handled = true;
    }
}

void TrackBar::on_key(KeyEvent& event) {
    if (event.action != KeyAction::down) {
        return;
    }
    double next = value.read();
    RangeAction action = RangeAction::thumb_position;
    bool recognized = true;
    // Left and Right move the thumb that way: on a mirrored bar Left
    // increments.
    std::uint32_t key = event.physical_key;
    if (mirrored_axis() && key == PhysicalKey::left) key = PhysicalKey::right;
    else if (mirrored_axis() && key == PhysicalKey::right) key = PhysicalKey::left;
    switch (key) {
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

void TrackBar::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused) {
        tracking_ = false;
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor TrackBar::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::slider;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    std::ostringstream value_text;
    value_text << value.read();
    descriptor.value = value_text.str();
    descriptor.numeric_value = value.read();
    descriptor.minimum_value = minimum.read();
    descriptor.maximum_value = maximum.read();
    // The arrow keys move by the small change, the page keys by the large.
    descriptor.small_step = small_change.read();
    descriptor.large_step = large_change.read();
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment,
                          SemanticAction::decrement, SemanticAction::set_value};
    descriptor.exposed = true;
    return descriptor;
}

bool TrackBar::on_semantic_action(SemanticAction action, std::string_view value_text) {
    if (action == SemanticAction::increment || action == SemanticAction::decrement) {
        const bool increasing = action == SemanticAction::increment;
        set_value_from_input(value.read() + (increasing ? small_change.read() : -small_change.read()),
                             increasing ? RangeAction::small_increment
                                        : RangeAction::small_decrement);
        return true;
    }
    if (action == SemanticAction::set_value) {
        double parsed{};
        if (!number_text::parse_finite_decimal(value_text, parsed)) return false;
        // A refusal by a bound value's validation fails the client's request.
        try {
            return set_value_from_input(parsed, RangeAction::thumb_position) || parsed == value.read();
        } catch (const std::exception&) {
            return false;
        }
    }
    return RangeControl::on_semantic_action(action, value_text);
}

// property_codemod begin TrackBar (tools/property_families/track_bar.json; generated, do not edit)

namespace {

PropertyDescriptor track_bar_tick_frequency_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "tick_frequency";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Appearance";
    descriptor.description = "Distance in value units between tick marks.";
    descriptor.default_value = BindingValue{10.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_track_bar_tick_frequency() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        track_bar_tick_frequency_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor track_bar_visual_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "visual_style";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "How the rail and thumb draw: classic ticks, a filled rail or a compact rail.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"track_bar_visual_style", "classic", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = track_bar_visual_style_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_track_bar_visual_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        track_bar_visual_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor track_bar_thumb_extent_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "thumb_extent";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Appearance";
    descriptor.description = "The thumb's side in pixels; 0 takes the visual style's.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_track_bar_thumb_extent() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        track_bar_thumb_extent_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<TrackBarVisualStyle>::type() {
    return PropertyType{BindingValueKind::enumeration, false, track_bar_visual_style_enum()};
}

BindingValue PropertyValueTraits<TrackBarVisualStyle>::to_binding(TrackBarVisualStyle value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = track_bar_visual_style_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

TrackBarVisualStyle PropertyValueTraits<TrackBarVisualStyle>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<TrackBarVisualStyle>(choice.value);
}

const std::array<PropertyEntry, TrackBar::track_bar_property_count>& TrackBar::track_bar_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"tick_frequency", &describe_track_bar_tick_frequency,
         &read_cell_entry<TrackBar, decltype(&TrackBar::tick_frequency), &TrackBar::tick_frequency>,
         &write_cell_entry<TrackBar, decltype(&TrackBar::tick_frequency), &TrackBar::tick_frequency>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"visual_style", &describe_track_bar_visual_style,
         &read_cell_entry<TrackBar, decltype(&TrackBar::visual_style), &TrackBar::visual_style>,
         &write_cell_entry<TrackBar, decltype(&TrackBar::visual_style), &TrackBar::visual_style>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"thumb_extent", &describe_track_bar_thumb_extent,
         &read_cell_entry<TrackBar, decltype(&TrackBar::thumb_extent), &TrackBar::thumb_extent>,
         &write_cell_entry<TrackBar, decltype(&TrackBar::thumb_extent), &TrackBar::thumb_extent>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "TrackBar's property table names a property twice");
    static const std::array<PropertyEntry, track_bar_property_count> entries =
        join_property_tables(RangeControl::range_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> TrackBar::property_table() const noexcept {
    const std::array<PropertyEntry, track_bar_property_count>& entries = track_bar_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void TrackBar::prepare_write(const decltype(tick_frequency)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "tick frequency must be finite");
    if (incoming <= 0.0) throw std::invalid_argument("tick frequency must be positive");
}

void TrackBar::written(const decltype(tick_frequency)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void TrackBar::prepare_write(const decltype(show_ticks)&, bool&) {
    require_mutable();
}

void TrackBar::written(const decltype(show_ticks)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void TrackBar::prepare_write(const decltype(visual_style)&, TrackBarVisualStyle&) {
    require_mutable();
}

void TrackBar::written(const decltype(visual_style)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void TrackBar::prepare_write(const decltype(thumb_extent)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "thumb extent must be finite");
    if (incoming < 0.0 || incoming > 64.0) throw std::invalid_argument("thumb extent must be between 0 and 64");
}

void TrackBar::written(const decltype(thumb_extent)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod end TrackBar

} // namespace gui_forms
