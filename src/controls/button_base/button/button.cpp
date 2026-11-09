#include "gui_forms/controls/button_base/button/button.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

std::shared_ptr<const PropertyEnumDescriptor> visual_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "button_visual_style", {{"standard", 0}, {"flat", 1}, {"accent", 2}, {"command", 3}}, false});
    return value;
}

} // namespace

Button::Button(StableId stable_id, std::string text)
    : ButtonBase(std::move(stable_id), std::move(text)) {}

void Button::notify_default(bool value) { default_button = value; }

void Button::assign_cancel_dialog_result() {
    if (dialog_result.read() == DialogResult::none) {
        dialog_result = DialogResult::cancel;
    }
}

void Button::on_activate() {
    const Control::Ptr retained = weak_from_this().lock();
    Window* owner = attached_window();
    ButtonBase::on_activate();
    // Read the value after Click: a handler may deliberately replace or clear
    // DialogResult before the Form/Window observes it.
    if (dialog_result.read() != DialogResult::none && is_alive() &&
        attached_window() == owner && owner != nullptr) {
        (*owner).set_dialog_result(dialog_result.read());
    }
}

namespace {

constexpr std::array<PartField<Button::Parts>, 6> button_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Button::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &Button::Parts::face},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &Button::Parts::icon},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &Button::Parts::label},
    {.declaration = {.name = "preselect", .role = PartRole::preselect, .composed_from = {}},
     .member = &Button::Parts::preselect},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &Button::Parts::focus},
}};

constexpr std::array<StateField<Button::States>, 11> button_state_fields{{
    {.state = AnatomyState::normal, .member = &Button::States::normal},
    {.state = AnatomyState::hot, .member = &Button::States::hot},
    {.state = AnatomyState::pressed, .member = &Button::States::pressed},
    {.state = AnatomyState::preselect, .member = &Button::States::preselect},
    {.state = AnatomyState::selected, .member = &Button::States::selected},
    {.state = AnatomyState::enabled, .member = &Button::States::enabled},
    {.state = AnatomyState::disabled, .member = &Button::States::disabled},
    {.state = AnatomyState::focused, .member = &Button::States::focused},
    {.state = AnatomyState::focus_visible, .member = &Button::States::focus_visible},
    {.state = AnatomyState::open, .member = &Button::States::open},
    {.state = AnatomyState::closed, .member = &Button::States::closed},
}};

constexpr std::array<PartDeclaration, 6> button_parts =
    part_declarations(button_part_fields);
constexpr std::array<AnatomyState, 11> button_states =
    state_declarations(button_state_fields);

constexpr ControlAnatomy button_anatomy{
    .control_id = "button",
    .parts = button_parts,
    .states = button_states,
    .custom_states = {},
};

// The theme role a button of `style` is drawn with.
ControlVisualRole button_visual_role(ButtonVisualStyle style) noexcept {
    switch (style) {
    case ButtonVisualStyle::standard: return ControlVisualRole::button;
    case ButtonVisualStyle::flat: return ControlVisualRole::button;
    case ButtonVisualStyle::accent: return ControlVisualRole::accent_button;
    case ButtonVisualStyle::command: return ControlVisualRole::command_button;
    }
    // A style outside the enumeration is drawn as a plain button.
    return ControlVisualRole::button;
}

} // namespace

const ControlAnatomy& Button::anatomy() const noexcept {
    return button_anatomy;
}

Button::Parts Button::button_parts() const {
    // Paint passes the selection to the image lookup only on the themed path.
    const bool layout_selected = selected.read();
    const bool command_alignment = visual_style.read() == ButtonVisualStyle::command;
    const ButtonBase::Parts base = push_button_parts(layout_selected, command_alignment);
    Parts record;
    record.region = base.region;
    record.face = base.face;
    record.icon = base.icon;
    record.label = base.label;
    record.preselect = base.face;
    record.focus = base.focus;
    return record;
}

void Button::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = button_parts();
    write_parts(anatomy(), button_part_fields, record, geometry);
}

AnatomyStateSet Button::anatomy_states() const {
    AnatomyStateSet states = button_anatomy_states();
    if (default_button.read()) {
        states.insert(AnatomyState::preselect);
    }
    if (selected.read()) {
        states.insert(AnatomyState::selected);
    }
    return states;
}

Button::Parts Button::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), button_part_fields, published.parts(), record);
    return record;
}

Button::States Button::states() const {
    States record;
    read_states(button_state_fields, anatomy_states(), record);
    return record;
}

void Button::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    if (visual_style.read() == ButtonVisualStyle::flat) {
        paint_flat_button(painter, bounds, flat_border_width.read());
        return;
    }
    const ControlVisualRole role = button_visual_role(visual_style.read());
    // An overridden accent button keeps its accent face and paper text while
    // enabled; disabled, it takes the override's face and disabled text, as
    // the theme's disabled accent plate does (sweep defect D11: a disabled
    // accent kept its fill). The other styles take the override's face and
    // text.
    std::optional<AuthoredButtonColours> authored{};
    if (has_style_override()) {
        const BasicControlStyle& colours = style();
        const bool accent = visual_style.read() == ButtonVisualStyle::accent && effectively_enabled();
        const bool flat = visual_style.read() == ButtonVisualStyle::flat;
        const Color face = accent ? colours.accent : flat ? colours.face_light : colours.face;
        const Color text = accent ? colours.paper : colours.text;
        authored = AuthoredButtonColours{face, text};
    }
    paint_themed_button(painter, bounds, role, default_button.read(),
                        visual_style.read() == ButtonVisualStyle::command,
                        selected.read(), authored);
}

SemanticDescriptor Button::semantic_descriptor() const {
    SemanticDescriptor descriptor = ButtonBase::semantic_descriptor();
    if (selected.read()) descriptor.states |= SemanticState::selected;
    return descriptor;
}

Insets Button::visual_outsets() const noexcept {
    const ControlVisualRole role = button_visual_role(visual_style.read());
    const ControlVisualContext context = visual_context(
        hovered_visual(), pressed_visual(), selected.read(), focus_cue_visible(),
        default_button.read());
    return resolved_visual_outsets(role, context);
}

namespace {

// The events Button adds to those of ButtonBase, in declaration order.
constexpr std::array<InteractionDeclaration, 2> button_interactions{{
    {.name = "dialog_result_changed", .payload = "DialogResult", .mutable_payload = false},
    {.name = "selected_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void Button::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ButtonBase::append_interactions(interactions);
    interactions.insert(interactions.end(), button_interactions.begin(), button_interactions.end());
}

// property_codemod begin Button (tools/property_families/button.json; generated, do not edit)

namespace {

PropertyDescriptor button_visual_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "visual_style";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Standard, flat, accent or command button.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"button_visual_style", "standard", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = visual_style_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_button_visual_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        button_visual_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor button_flat_border_width_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "flat_border_width";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Appearance";
    descriptor.description = "Border width in logical pixels of a flat button.";
    descriptor.default_value = BindingValue{1.0};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_button_flat_border_width() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        button_flat_border_width_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<ButtonVisualStyle>::type() {
    return PropertyType{BindingValueKind::enumeration, false, visual_style_enum()};
}

BindingValue PropertyValueTraits<ButtonVisualStyle>::to_binding(ButtonVisualStyle value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = visual_style_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

ButtonVisualStyle PropertyValueTraits<ButtonVisualStyle>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<ButtonVisualStyle>(choice.value);
}

const std::array<PropertyEntry, Button::button_property_count>& Button::button_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 2U> own{{
        {"visual_style", &describe_button_visual_style,
         &read_cell_entry<Button, decltype(&Button::visual_style), &Button::visual_style>,
         &write_cell_entry<Button, decltype(&Button::visual_style), &Button::visual_style>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"flat_border_width", &describe_button_flat_border_width,
         &read_cell_entry<Button, decltype(&Button::flat_border_width), &Button::flat_border_width>,
         &write_cell_entry<Button, decltype(&Button::flat_border_width), &Button::flat_border_width>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Button's property table names a property twice");
    static const std::array<PropertyEntry, button_property_count> entries =
        join_property_tables(ButtonBase::button_base_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> Button::property_table() const noexcept {
    const std::array<PropertyEntry, button_property_count>& entries = button_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void Button::prepare_write(const decltype(visual_style)&, ButtonVisualStyle&) {
    require_mutable();
}

void Button::written(const decltype(visual_style)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void Button::prepare_write(const decltype(flat_border_width)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0) throw std::invalid_argument("button flat border width must be finite and nonnegative");
}

void Button::written(const decltype(flat_border_width)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void Button::prepare_write(const decltype(default_button)&, bool&) {
    require_mutable();
}

void Button::written(const decltype(default_button)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void Button::prepare_write(const decltype(dialog_result)&, DialogResult& incoming) {
    require_mutable();
    switch (incoming) {
    case DialogResult::none:
    case DialogResult::ok:
    case DialogResult::cancel:
    case DialogResult::abort:
    case DialogResult::retry:
    case DialogResult::ignore:
    case DialogResult::yes:
    case DialogResult::no:
    case DialogResult::try_again:
    case DialogResult::continue_: break;
    default:
        throw std::invalid_argument("Button DialogResult is not a defined value");
    }
}

void Button::written(const decltype(dialog_result)&) {
    invalidate(Dirty::semantics);
    publish_change(dialog_result_changed_, dialog_result.read());
}

void Button::prepare_write(const decltype(selected)&, bool&) {
    require_mutable();
}

void Button::written(const decltype(selected)&) {
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
    publish_change(selected_changed_, selected.read());
}

// property_codemod end Button

} // namespace gui_forms
