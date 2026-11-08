#include "gui_forms/controls/button_base/toggle_switch/toggle_switch.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The switch's metrics, in logical units: the track's size, the space
// between the track's edge and the thumb, and the gap between the track,
// the label and the state text.
constexpr double track_width = 40.0;
constexpr double track_height = 20.0;
constexpr double thumb_inset = 3.0;
constexpr double gap = 8.0;

constexpr std::array<PartField<ToggleSwitch::Parts>, 6> toggle_switch_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ToggleSwitch::Parts::region},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}},
     .member = &ToggleSwitch::Parts::track},
    {.declaration = {.name = "thumb", .role = PartRole::indicator, .composed_from = {}},
     .member = &ToggleSwitch::Parts::thumb},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &ToggleSwitch::Parts::label},
    {.declaration = {.name = "state", .role = PartRole::label, .composed_from = {}},
     .member = &ToggleSwitch::Parts::state},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ToggleSwitch::Parts::focus},
}};

constexpr std::array<PartDeclaration, 6> toggle_switch_parts = part_declarations(toggle_switch_part_fields);

// open and closed are ButtonBase's disclosure states, reported while an
// expanded state is set (button_anatomy_states).
constexpr std::array<AnatomyState, 10> toggle_switch_states{
    AnatomyState::normal,  AnatomyState::hot,      AnatomyState::pressed, AnatomyState::checked,
    AnatomyState::enabled, AnatomyState::disabled, AnatomyState::focused, AnatomyState::focus_visible,
    AnatomyState::open,    AnatomyState::closed,
};

constexpr ControlAnatomy toggle_switch_anatomy{
    .control_id = "toggle_switch",
    .parts = toggle_switch_parts,
    .states = toggle_switch_states,
    .custom_states = {},
};

// The event ToggleSwitch adds to those of ButtonBase.
constexpr std::array<InteractionDeclaration, 1> toggle_switch_interactions{{
    {.name = "checked_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

ToggleSwitch::ToggleSwitch(StableId stable_id, std::string initial_text)
    : ButtonBase(std::move(stable_id), std::move(initial_text)) {}

const std::string& ToggleSwitch::state_text() const noexcept {
    if (checked.read()) return on_text.read();
    return off_text.read();
}

FontSpec ToggleSwitch::label_font() const noexcept {
    const FontSpec spec = text_font(font.read());
    return spec;
}

// The track at the start, centred down the control; the label after it and
// the state text after the label, each one line centred down the control.
ToggleSwitch::Placement ToggleSwitch::placement(Size size) const {
    Placement placed;
    const double centre = size.height * 0.5;
    placed.track = Rect{0.0, std::floor(centre - track_height * 0.5), track_width, track_height};
    const double thumb_side = track_height - 2.0 * thumb_inset;
    const double thumb_x = checked.read() ? placed.track.x + track_width - thumb_inset - thumb_side
                                          : placed.track.x + thumb_inset;
    placed.thumb = Rect{thumb_x, placed.track.y + thumb_inset, thumb_side, thumb_side};
    double x = track_width + gap;
    const std::string shown = display_text();
    if (!shown.empty()) {
        const Size measured = resolve_text_layout_utf8(shown, label_font()).logical_size;
        const double width = std::min(std::ceil(measured.width), std::max(0.0, size.width - x));
        const double height = std::ceil(measured.height);
        placed.label = Rect{x, std::floor(centre - height * 0.5), width, height};
        x += width + gap;
    }
    const std::string& state = state_text();
    if (!state.empty()) {
        const Size measured = resolve_text_layout_utf8(state, label_font()).logical_size;
        const double width = std::min(std::ceil(measured.width), std::max(0.0, size.width - x));
        const double height = std::ceil(measured.height);
        placed.state = Rect{x, std::floor(centre - height * 0.5), width, height};
    }
    return placed;
}

// The natural size reserves the wider of the two state texts, so that
// turning the switch does not change its size; a requested size wins on
// either axis.
Size ToggleSwitch::measure(Size available) {
    const FontSpec spec = label_font();
    double width = track_width;
    double height = track_height;
    const std::string shown = display_text();
    if (!shown.empty()) {
        const Size measured = resolve_text_layout_utf8(shown, spec).logical_size;
        width += gap + std::ceil(measured.width);
        height = std::max(height, std::ceil(measured.height));
    }
    double state_width = 0.0;
    const std::array<const std::string*, 2> states{&on_text.read(), &off_text.read()};
    for (const std::string* state : states) {
        if ((*state).empty()) continue;
        const Size measured = resolve_text_layout_utf8(*state, spec).logical_size;
        state_width = std::max(state_width, std::ceil(measured.width));
        height = std::max(height, std::ceil(measured.height));
    }
    if (state_width > 0.0) width += gap + state_width;
    const Rect requested = requested_bounds();
    if (requested.width > 0.0) width = requested.width;
    if (requested.height > 0.0) height = requested.height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

// The track and thumb take the choice family in the control's states, the
// thumb drawn as the toggle knob; the label and the state text are drawn
// as labels; the focus cue goes round the whole control.
void ToggleSwitch::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const Placement placed = placement(Size{bounds.width, bounds.height});
    const AnatomyStateSet states = anatomy_states();
    PartPaint track{.role = PartRole::track, .bounds = placed.track, .states = states,
                    .family = ControlVisualRole::choice};
    track.corner_radius = track_height * 0.5;
    track.key = PartKey{"track", {}, {}};
    paint_part(painter, *this, track);
    PartPaint thumb{.role = PartRole::indicator, .bounds = placed.thumb, .states = states,
                    .family = ControlVisualRole::choice};
    thumb.mark = PartMark::toggle_knob;
    thumb.key = PartKey{"thumb", {}, {}};
    paint_part(painter, *this, thumb);
    const FontSpec spec = label_font();
    if (placed.label.width > 0.0) {
        PartPaint label{.role = PartRole::label, .bounds = placed.label, .states = states,
                        .family = ControlVisualRole::choice};
        label.text = display_text();
        label.font = spec;
        paint_part(painter, *this, label);
    }
    if (placed.state.width > 0.0) {
        PartPaint state{.role = PartRole::label, .bounds = placed.state, .states = states,
                        .family = ControlVisualRole::choice};
        state.text = state_text();
        state.font = spec;
        paint_part(painter, *this, state);
    }
    PartPaint focus{.role = PartRole::focus, .bounds = bounds, .states = states,
                    .family = ControlVisualRole::choice};
    focus.key = PartKey{"focus", {}, {}};
    paint_part(painter, *this, focus);
}

Insets ToggleSwitch::visual_outsets() const noexcept {
    const ControlVisualContext context =
        visual_context(hovered_visual(), pressed_visual(), checked.read(), focus_cue_visible());
    const Insets outsets =
        surface_material_visual_outsets(effective_theme().resolve(ControlVisualRole::choice, context).material);
    return outsets;
}

// The switch turns over first; clicked and the bound command follow, as a
// CheckBox's do. A handler that disposes the switch ends the activation.
void ToggleSwitch::on_activate() {
    const Control::Ptr retained = weak_from_this().lock();
    checked = !checked.read();
    if (!is_alive()) return;
    ButtonBase::on_activate();
}

SemanticDescriptor ToggleSwitch::semantic_descriptor() const {
    SemanticDescriptor descriptor = ButtonBase::semantic_descriptor();
    descriptor.role = SemanticRole::toggle_switch;
    descriptor.states |= SemanticState::checkable;
    descriptor.value = checked.read() ? "checked" : "unchecked";
    if (checked.read()) descriptor.states |= SemanticState::checked;
    return descriptor;
}

const ControlAnatomy& ToggleSwitch::anatomy() const noexcept {
    return toggle_switch_anatomy;
}

AnatomyStateSet ToggleSwitch::anatomy_states() const {
    AnatomyStateSet states = button_anatomy_states();
    if (checked.read()) states.insert(AnatomyState::checked);
    return states;
}

void ToggleSwitch::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const Placement placed = placement(Size{bounds.width, bounds.height});
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.track = PartGeometry::from_bounds(placed.track);
    record.thumb = PartGeometry::from_bounds(placed.thumb);
    record.label = PartGeometry::from_bounds(placed.label);
    record.state = PartGeometry::from_bounds(placed.state);
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), toggle_switch_part_fields, record, geometry);
}

ToggleSwitch::Parts ToggleSwitch::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), toggle_switch_part_fields, published.parts(), record);
    return record;
}

void ToggleSwitch::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ButtonBase::append_interactions(interactions);
    interactions.insert(interactions.end(), toggle_switch_interactions.begin(), toggle_switch_interactions.end());
}

// property_codemod begin ToggleSwitch (tools/property_families/toggle_switch.json; generated, do not edit)

namespace {

PropertyDescriptor toggle_switch_checked_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "checked";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the switch is on.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    return descriptor;
}

const PropertyDescriptor& describe_toggle_switch_checked() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        toggle_switch_checked_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor toggle_switch_on_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "on_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The state text shown while the switch is on.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_toggle_switch_on_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        toggle_switch_on_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor toggle_switch_off_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "off_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The state text shown while the switch is off.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_toggle_switch_off_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        toggle_switch_off_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, ToggleSwitch::toggle_switch_property_count>& ToggleSwitch::toggle_switch_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"checked", &describe_toggle_switch_checked,
         &read_cell_entry<ToggleSwitch, decltype(&ToggleSwitch::checked), &ToggleSwitch::checked>,
         &write_cell_entry<ToggleSwitch, decltype(&ToggleSwitch::checked), &ToggleSwitch::checked>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<ToggleSwitch, decltype(&ToggleSwitch::checked_changed_), &ToggleSwitch::checked_changed_>},
        {"on_text", &describe_toggle_switch_on_text,
         &read_cell_entry<ToggleSwitch, decltype(&ToggleSwitch::on_text), &ToggleSwitch::on_text>,
         &write_cell_entry<ToggleSwitch, decltype(&ToggleSwitch::on_text), &ToggleSwitch::on_text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"off_text", &describe_toggle_switch_off_text,
         &read_cell_entry<ToggleSwitch, decltype(&ToggleSwitch::off_text), &ToggleSwitch::off_text>,
         &write_cell_entry<ToggleSwitch, decltype(&ToggleSwitch::off_text), &ToggleSwitch::off_text>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ToggleSwitch's property table names a property twice");
    static const std::array<PropertyEntry, toggle_switch_property_count> entries =
        join_property_tables(ButtonBase::button_base_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ToggleSwitch::property_table() const noexcept {
    const std::array<PropertyEntry, toggle_switch_property_count>& entries = toggle_switch_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ToggleSwitch::prepare_write(const decltype(checked)&, bool&) {
    require_mutable();
}

void ToggleSwitch::written(const decltype(checked)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(checked_changed_, checked.read());
}

void ToggleSwitch::prepare_write(const decltype(on_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) {
        throw std::invalid_argument("ToggleSwitch on text must be valid UTF-8");
    }
}

void ToggleSwitch::written(const decltype(on_text)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ToggleSwitch::prepare_write(const decltype(off_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) {
        throw std::invalid_argument("ToggleSwitch off text must be valid UTF-8");
    }
}

void ToggleSwitch::written(const decltype(off_text)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

// property_codemod end ToggleSwitch

} // namespace gui_forms
