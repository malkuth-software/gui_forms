#include "gui_forms/controls/radio_button.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "../choice_indicator_property.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

RadioButton::RadioButton(StableId stable_id, std::string text)
    : ButtonBase(std::move(stable_id), std::move(text)) {
    text_alignment = ContentAlignment::middle_left;
    image_alignment = ContentAlignment::middle_left;
}

void RadioButton::set_checked_without_exclusion(bool checked_value) {
    require_mutable();
    if (checked_ == checked_value) {
        return;
    }
    checked_ = checked_value;
    invalidate(Dirty::paint | Dirty::semantics);
    on_checked_changed();
}

void RadioButton::clear_checked_peer() {
    const Control::Ptr owner = parent();
    if (!owner) return;
    for (const Control::Ptr& sibling : (*owner).children()) {
        std::shared_ptr<gui_forms::RadioButton> peer = std::dynamic_pointer_cast<RadioButton>(sibling);
        if (peer && peer.get() != this && (*peer).group_name.read() == group_name.read() &&
            (*peer).checked_) {
            (*peer).set_checked_without_exclusion(false);
            if (!is_alive()) {
                return;
            }
        }
    }
}

// Checking clears the group's checked sibling before this button stores
// and raises checked_changed, as set_checked did.
void RadioButton::write_view(const decltype(checked)&, bool incoming) {
    require_mutable();
    if (checked_ == incoming) {
        return;
    }
    if (incoming) {
        clear_checked_peer();
        if (!is_alive()) {
            return;
        }
    }
    set_checked_without_exclusion(incoming);
}

bool RadioButton::read_view(const decltype(checked)&) const {
    return checked_;
}

void RadioButton::prepare_write(const decltype(group_name)&, std::string&) {
    require_mutable();
}

// A checked button that joins a group clears that group's checked sibling.
void RadioButton::written(const decltype(group_name)&) {
    invalidate(Dirty::semantics);
    if (checked_) clear_checked_peer();
}

namespace {

constexpr std::array<PartField<RadioButton::Parts>, 6> radio_button_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &RadioButton::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &RadioButton::Parts::well},
    {.declaration = {.name = "indicator", .role = PartRole::indicator, .composed_from = {}},
     .member = &RadioButton::Parts::indicator},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &RadioButton::Parts::icon},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &RadioButton::Parts::label},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &RadioButton::Parts::focus},
}};

constexpr std::array<StateField<RadioButton::States>, 10> radio_button_state_fields{{
    {.state = AnatomyState::normal, .member = &RadioButton::States::normal},
    {.state = AnatomyState::hot, .member = &RadioButton::States::hot},
    {.state = AnatomyState::pressed, .member = &RadioButton::States::pressed},
    {.state = AnatomyState::checked, .member = &RadioButton::States::checked},
    {.state = AnatomyState::enabled, .member = &RadioButton::States::enabled},
    {.state = AnatomyState::disabled, .member = &RadioButton::States::disabled},
    {.state = AnatomyState::focused, .member = &RadioButton::States::focused},
    {.state = AnatomyState::focus_visible, .member = &RadioButton::States::focus_visible},
    {.state = AnatomyState::open, .member = &RadioButton::States::open},
    {.state = AnatomyState::closed, .member = &RadioButton::States::closed},
}};

constexpr std::array<PartDeclaration, 6> radio_button_parts =
    part_declarations(radio_button_part_fields);
constexpr std::array<AnatomyState, 10> radio_button_states =
    state_declarations(radio_button_state_fields);

constexpr ControlAnatomy radio_button_anatomy{
    .control_id = "radio_button",
    .parts = radio_button_parts,
    .states = radio_button_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& RadioButton::anatomy() const noexcept {
    return radio_button_anatomy;
}

void RadioButton::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const double indicator_width =
        indicator_style.read() == ChoiceIndicatorStyle::toggle ? 30.0 : 15.0;
    const double top = std::max(1.0, (bounds.height - 15.0) * 0.5);
    const Rect well{1.0, top, indicator_width, 15.0};
    // The toggle's knob, or the round dot drawn inside the 15-unit disc.
    Rect mark{};
    if (indicator_style.read() == ChoiceIndicatorStyle::toggle) {
        const double knob_x = checked_ ? well.x + well.width - 13.0 : well.x + 2.0;
        mark = {knob_x, well.y + 2.0, 11.0, 11.0};
    } else if (checked_) {
        mark = {well.x + 4.0, well.y + 4.0, 7.0, 7.0};
    }
    const Rect content_face{well.x + well.width + 1.0, 0.0,
                            std::max(0.0, bounds.width - well.x - well.width - 1.0),
                            bounds.height};
    const ContentLayout layout =
        published_content_layout(content_face, checked_, false);
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.well = PartGeometry::from_bounds(well);
    record.indicator = PartGeometry::from_bounds(mark);
    if (layout.image.has_value()) {
        record.icon = PartGeometry::from_bounds(*layout.image);
    }
    if (layout.text.has_value()) {
        record.label = PartGeometry::from_bounds(*layout.text);
    }
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), radio_button_part_fields, record, geometry);
}

AnatomyStateSet RadioButton::anatomy_states() const {
    AnatomyStateSet states = button_anatomy_states();
    if (checked_) {
        states.insert(AnatomyState::checked);
    }
    return states;
}

RadioButton::Parts RadioButton::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), radio_button_part_fields, published.parts(), record);
    return record;
}

RadioButton::States RadioButton::states() const {
    States record;
    read_states(radio_button_state_fields, anatomy_states(), record);
    return record;
}

// The well, its mark (the dot, or the toggle's knob) and the focus cue are
// parts the theme's engine draws, in the control's anatomy states; a style
// override supplies the well's ground and the ink of the mark and the label
// as authored colours (T4-1), and a disabled radio button keeps the theme's
// disabled text.
void RadioButton::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const bool toggle = indicator_style.read() == ChoiceIndicatorStyle::toggle;
    const double indicator_width = toggle ? 30.0 : 15.0;
    const double top = std::max(1.0, (bounds.height - 15.0) * 0.5);
    const Rect box{1.0, top, indicator_width, 15.0};
    const AnatomyStateSet states = anatomy_states();
    std::optional<Color> ground{};
    std::optional<Color> ink{};
    if (has_style_override()) {
        const BasicControlStyle& colors = style();
        ground = colors.paper;
        ink = colors.text;
    }
    PartPaint well;
    well.family = ControlVisualRole::choice;
    well.role = PartRole::well;
    well.bounds = box;
    well.states = states;
    well.corner_radius = 7.5;
    well.authored_background = ground;
    well.key = PartKey{"well", {}, {}};
    paint_part(painter, *this, well);
    if (toggle || checked_) {
        PartPaint indicator;
        indicator.family = ControlVisualRole::choice;
        indicator.role = PartRole::indicator;
        indicator.states = states;
        indicator.authored_foreground = ink;
        indicator.key = PartKey{"indicator", {}, {}};
        if (toggle) {
            const double knob_x = checked_ ? box.x + box.width - 13.0 : box.x + 2.0;
            indicator.mark = PartMark::toggle_knob;
            indicator.bounds = {knob_x, box.y + 2.0, 11.0, 11.0};
        } else {
            indicator.mark = PartMark::radio_dot;
            indicator.bounds = {box.x + 4.0, box.y + 4.0, 7.0, 7.0};
        }
        paint_part(painter, *this, indicator);
    }
    const Rect content{box.x + box.width + 1.0, 0.0,
                       std::max(0.0, bounds.width - box.x - box.width - 1.0), bounds.height};
    ButtonContentInk content_ink;
    content_ink.family = ControlVisualRole::choice;
    content_ink.states = states;
    content_ink.authored = ink;
    paint_button_content(painter, content, display_text(), content_ink, {}, checked_);
    PartPaint focus;
    focus.family = ControlVisualRole::choice;
    focus.role = PartRole::focus;
    focus.bounds = bounds;
    focus.states = states;
    focus.key = PartKey{"focus", {}, {}};
    paint_part(painter, *this, focus);
}

Insets RadioButton::visual_outsets() const noexcept {
    const ControlVisualContext context = visual_context({.hovered = hovered_visual(), .pressed = pressed_visual(), .selected = checked_, .focused = focus_cue_visible()});
    return surface_material_visual_outsets(
        effective_theme().resolve(ControlVisualRole::choice, context).material);
}

void RadioButton::on_activate() {
    if (auto_check.read() && !checked_) {
        checked = true;
        if (!is_alive()) {
            return;
        }
    }
    ButtonBase::on_activate();
}

SemanticDescriptor RadioButton::semantic_descriptor() const {
    SemanticDescriptor descriptor = ButtonBase::semantic_descriptor();
    descriptor.role = SemanticRole::radio_button;
    descriptor.value = checked_ ? "selected" : "not selected";
    if (checked_) {
        descriptor.states |= SemanticState::checked;
        descriptor.states |= SemanticState::selected;
    }
    return descriptor;
}

namespace {

// The events RadioButton adds to those of ButtonBase, in declaration order.
constexpr std::array<InteractionDeclaration, 1> radio_button_interactions{{
    {.name = "checked_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void RadioButton::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ButtonBase::append_interactions(interactions);
    interactions.insert(interactions.end(), radio_button_interactions.begin(), radio_button_interactions.end());
}

// property_codemod begin RadioButton (tools/property_families/radio_button.json; generated, do not edit)

namespace {

PropertyDescriptor radio_button_checked_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "checked";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the radio button is selected within its group.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_radio_button_checked() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        radio_button_checked_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor radio_button_group_name_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "group_name";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Behavior";
    descriptor.description = "The exclusion group among the parent's radio buttons; empty is the parent's own group.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_radio_button_group_name() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        radio_button_group_name_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor radio_button_indicator_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "indicator_style";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "How the choice mark draws: classic, modern or toggle.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"indicator_style", "classic", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = button_base_detail::choice_indicator_style_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_radio_button_indicator_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        radio_button_indicator_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, RadioButton::radio_button_property_count>& RadioButton::radio_button_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"checked", &describe_radio_button_checked,
         &read_cell_entry<RadioButton, decltype(&RadioButton::checked), &RadioButton::checked>,
         &write_cell_entry<RadioButton, decltype(&RadioButton::checked), &RadioButton::checked>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<RadioButton, decltype(&RadioButton::checked_changed_), &RadioButton::checked_changed_>},
        {"group_name", &describe_radio_button_group_name,
         &read_cell_entry<RadioButton, decltype(&RadioButton::group_name), &RadioButton::group_name>,
         &write_cell_entry<RadioButton, decltype(&RadioButton::group_name), &RadioButton::group_name>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"indicator_style", &describe_radio_button_indicator_style,
         &read_cell_entry<RadioButton, decltype(&RadioButton::indicator_style), &RadioButton::indicator_style>,
         &write_cell_entry<RadioButton, decltype(&RadioButton::indicator_style), &RadioButton::indicator_style>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "RadioButton's property table names a property twice");
    static const std::array<PropertyEntry, radio_button_property_count> entries =
        join_property_tables(ButtonBase::button_base_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> RadioButton::property_table() const noexcept {
    const std::array<PropertyEntry, radio_button_property_count>& entries = radio_button_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void RadioButton::on_checked_changed() {
    publish_change(checked_changed_, checked.read());
}

void RadioButton::prepare_write(const decltype(auto_check)&, bool&) {
    require_mutable();
}

void RadioButton::written(const decltype(auto_check)&) {
    invalidate(Dirty::semantics);
}

void RadioButton::prepare_write(const decltype(indicator_style)&, ChoiceIndicatorStyle& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(ChoiceIndicatorStyle::toggle)) throw std::invalid_argument("RadioButton indicator style is invalid");
}

void RadioButton::written(const decltype(indicator_style)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod end RadioButton

} // namespace gui_forms
