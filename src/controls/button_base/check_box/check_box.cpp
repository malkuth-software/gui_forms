#include "gui_forms/controls/button_base/check_box/check_box.hpp"
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
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

std::shared_ptr<const PropertyEnumDescriptor> check_state_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "check_state", {{"unchecked", 0}, {"checked", 1}, {"indeterminate", 2}}, false});
    return value;
}

} // namespace

void CheckBox::bind(Command& command) {
    unbind();
    ButtonBase::bind(command);
}

void CheckBox::bind(Value<bool>& model) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    std::unique_ptr<detail::ScalarBinding<CheckBox, bool>> binding =
        std::make_unique<detail::ScalarBinding<CheckBox, bool>>(
            *this, model, checked_changed_, &CheckBox::bound_checked, &CheckBox::write_bound_checked);
    // Retire the old model before the initial model-wins publication.
    unbind_command();
    value_binding_ = std::move(binding);
    (*value_binding_).synchronize();
}

void CheckBox::unbind() noexcept { value_binding_.reset(); }

CheckBox::CheckBox(StableId stable_id, std::string text)
    : ButtonBase(std::move(stable_id), std::move(text)) {
    text_alignment = ContentAlignment::middle_left;
    image_alignment = ContentAlignment::middle_left;
    // The user changes `checked`, so a form binding writes it back by
    // default; a document writes three_state before an indeterminate
    // check_state, since properties apply in document order.
}

bool CheckBox::bound_checked() const noexcept {
    return check_state_ == CheckState::checked;
}

void CheckBox::write_bound_checked(bool value) {
    checked = value;
}

// The check state's write, in set_check_state's order: validate, coerce
// indeterminate to checked unless three_state, let a bound Value refuse,
// store, then raise check_state_changed and (when it changed) checked_changed.
void CheckBox::write_view(const decltype(check_state)&, CheckState incoming) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    if (incoming != CheckState::unchecked && incoming != CheckState::checked &&
        incoming != CheckState::indeterminate) {
        throw std::invalid_argument("invalid check state");
    }
    if (!three_state.read() && incoming == CheckState::indeterminate) {
        incoming = CheckState::checked;
    }
    if (check_state_ == incoming) {
        return;
    }
    if (value_binding_) (*value_binding_).validate_update(incoming == CheckState::checked);
    if (!is_alive()) return;
    const bool previous_checked = check_state_ == CheckState::checked;
    check_state_ = incoming;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(check_state_changed_, check_state_);
    if (!is_alive()) {
        return;
    }
    const bool now_checked = check_state_ == CheckState::checked;
    if (previous_checked != now_checked) {
        publish_change(checked_changed_, now_checked);
    }
}

CheckState CheckBox::read_view(const decltype(check_state)&) const {
    return check_state_;
}

void CheckBox::write_view(const decltype(checked)&, bool incoming) {
    check_state = incoming ? CheckState::checked : CheckState::unchecked;
}

bool CheckBox::read_view(const decltype(checked)&) const {
    return check_state_ == CheckState::checked;
}

void CheckBox::prepare_write(const decltype(three_state)&, bool&) {
    require_mutable();
}

// Clearing three_state turns an indeterminate box checked (which
// invalidates and raises the events); otherwise only semantics change.
void CheckBox::written(const decltype(three_state)&) {
    if (!three_state.read() && check_state_ == CheckState::indeterminate) {
        check_state = CheckState::checked;
        return;
    }
    invalidate(Dirty::semantics);
}

namespace {

constexpr std::array<PartField<CheckBox::Parts>, 7> check_box_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &CheckBox::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &CheckBox::Parts::face},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &CheckBox::Parts::well},
    {.declaration = {.name = "indicator", .role = PartRole::indicator, .composed_from = {}},
     .member = &CheckBox::Parts::indicator},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &CheckBox::Parts::icon},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &CheckBox::Parts::label},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &CheckBox::Parts::focus},
}};

constexpr std::array<StateField<CheckBox::States>, 11> check_box_state_fields{{
    {.state = AnatomyState::normal, .member = &CheckBox::States::normal},
    {.state = AnatomyState::hot, .member = &CheckBox::States::hot},
    {.state = AnatomyState::pressed, .member = &CheckBox::States::pressed},
    {.state = AnatomyState::checked, .member = &CheckBox::States::checked},
    {.state = AnatomyState::indeterminate, .member = &CheckBox::States::indeterminate},
    {.state = AnatomyState::enabled, .member = &CheckBox::States::enabled},
    {.state = AnatomyState::disabled, .member = &CheckBox::States::disabled},
    {.state = AnatomyState::focused, .member = &CheckBox::States::focused},
    {.state = AnatomyState::focus_visible, .member = &CheckBox::States::focus_visible},
    {.state = AnatomyState::open, .member = &CheckBox::States::open},
    {.state = AnatomyState::closed, .member = &CheckBox::States::closed},
}};

constexpr std::array<PartDeclaration, 7> check_box_parts =
    part_declarations(check_box_part_fields);
constexpr std::array<AnatomyState, 11> check_box_states =
    state_declarations(check_box_state_fields);

constexpr ControlAnatomy check_box_anatomy{
    .control_id = "check_box",
    .parts = check_box_parts,
    .states = check_box_states,
    .custom_states = {},
};

} // namespace

namespace {

// The well a check box's indicator sits in: 15 units high, centred, and
// twice as wide for the toggle.
Rect check_box_well(Rect bounds, ChoiceIndicatorStyle style) noexcept {
    const double width = style == ChoiceIndicatorStyle::toggle ? 30.0 : 15.0;
    const Rect well{1.0, std::max(1.0, (bounds.height - 15.0) * 0.5), width, 15.0};
    return well;
}

// The mark in the well: the check, the indeterminate bar, the toggle's knob
// (checked or not), or none.
PartMark check_box_mark(ChoiceIndicatorStyle style, CheckState state) noexcept {
    const bool toggle = style == ChoiceIndicatorStyle::toggle;
    if (state == CheckState::checked && !toggle) return PartMark::check;
    if (state == CheckState::indeterminate) return PartMark::indeterminate;
    if (toggle) return PartMark::toggle_knob;
    return PartMark::none;
}

// Where `mark` is drawn in `well`; the knob sits right when checked.
Rect check_box_mark_bounds(Rect well, PartMark mark, bool checked) noexcept {
    switch (mark) {
    case PartMark::check:
        return {well.x + 2.0, well.y + 2.0, 12.0, 9.0};
    case PartMark::indeterminate:
        return {well.x + 4.0, well.y + 6.0, 8.0, 3.0};
    case PartMark::toggle_knob: {
        const double knob_x = checked ? well.x + well.width - 13.0 : well.x + 2.0;
        const Rect knob{knob_x, well.y + 2.0, 11.0, 11.0};
        return knob;
    }
    default: // check_box_mark gives no other mark: nothing is drawn.
        return {};
    }
}

} // namespace

const ControlAnatomy& CheckBox::anatomy() const noexcept {
    return check_box_anatomy;
}

void CheckBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    if (appearance.read() == CheckBoxAppearance::button) {
        // The button appearance is a push button whose selection is the
        // check state; it has a face and no well or indicator.
        const ButtonBase::Parts base = push_button_parts(checked.read(), false);
        record.region = base.region;
        record.face = base.face;
        record.icon = base.icon;
        record.label = base.label;
        record.focus = base.focus;
        write_parts(anatomy(), check_box_part_fields, record, geometry);
        return;
    }
    const Rect bounds = local_bounds();
    const Rect box = check_box_well(bounds, indicator_style.read());
    const PartMark mark_kind = check_box_mark(indicator_style.read(), check_state_);
    const Rect mark = check_box_mark_bounds(box, mark_kind, checked.read());
    const Rect content_face{box.x + box.width + 1.0, 0.0,
                            std::max(0.0, bounds.width - box.x - box.width - 1.0),
                            bounds.height};
    const ContentLayout layout =
        published_content_layout(content_face, checked.read(), false);
    record.region = PartGeometry::from_bounds(bounds);
    record.well = PartGeometry::from_bounds(box);
    record.indicator = PartGeometry::from_bounds(mark);
    if (layout.image.has_value()) {
        record.icon = PartGeometry::from_bounds(*layout.image);
    }
    if (layout.text.has_value()) {
        record.label = PartGeometry::from_bounds(*layout.text);
    }
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), check_box_part_fields, record, geometry);
}

AnatomyStateSet CheckBox::anatomy_states() const {
    AnatomyStateSet states = button_anatomy_states();
    if (check_state_ == CheckState::checked) {
        states.insert(AnatomyState::checked);
    } else if (check_state_ == CheckState::indeterminate) {
        states.insert(AnatomyState::indeterminate);
    }
    return states;
}

CheckBox::Parts CheckBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), check_box_part_fields, published.parts(), record);
    return record;
}

CheckBox::States CheckBox::states() const {
    States record;
    read_states(check_box_state_fields, anatomy_states(), record);
    return record;
}

// The button appearance paints as a themed push button. Otherwise the well,
// its mark and the focus cue are parts the theme's engine draws, in the
// control's anatomy states; a style override supplies the well's ground
// and the ink of the mark and the label as authored colours (T4-1), and a
// disabled check box keeps the theme's disabled text.
void CheckBox::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    if (appearance.read() == CheckBoxAppearance::button) {
        paint_themed_button(painter, bounds, ControlVisualRole::button, false,
                            false, checked.read());
        return;
    }
    const Rect box = check_box_well(bounds, indicator_style.read());
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
    well.corner_radius = indicator_style.read() == ChoiceIndicatorStyle::toggle ? 7.5 : 2.0;
    well.authored_background = ground;
    well.key = PartKey{"well", {}, {}};
    paint_part(painter, *this, well);
    const PartMark mark = check_box_mark(indicator_style.read(), check_state_);
    if (mark != PartMark::none) {
        PartPaint indicator;
        indicator.family = ControlVisualRole::choice;
        indicator.role = PartRole::indicator;
        indicator.mark = mark;
        indicator.bounds = check_box_mark_bounds(box, mark, checked.read());
        indicator.states = states;
        indicator.authored_foreground = ink;
        indicator.key = PartKey{"indicator", {}, {}};
        paint_part(painter, *this, indicator);
    }
    const Rect content{box.x + box.width + 1.0, 0.0,
                       std::max(0.0, bounds.width - box.x - box.width - 1.0), bounds.height};
    ButtonContentInk content_ink;
    content_ink.family = ControlVisualRole::choice;
    content_ink.states = states;
    content_ink.authored = ink;
    paint_button_content(painter, content, display_text(), content_ink, {}, checked.read());
    PartPaint focus;
    focus.family = ControlVisualRole::choice;
    focus.role = PartRole::focus;
    focus.bounds = bounds;
    focus.states = states;
    focus.key = PartKey{"focus", {}, {}};
    paint_part(painter, *this, focus);
}

Insets CheckBox::visual_outsets() const noexcept {
    const ControlVisualContext context = visual_context(
        hovered_visual(), pressed_visual(), checked.read(), focus_cue_visible());
    if (appearance.read() == CheckBoxAppearance::button) {
        return resolved_visual_outsets(ControlVisualRole::button, context);
    }
    return surface_material_visual_outsets(
        effective_theme().resolve(ControlVisualRole::choice, context).material);
}

void CheckBox::on_activate() {
    const Control::Ptr retained = weak_from_this().lock();
    if (auto_check.read()) {
        CheckState next = CheckState::unchecked;
        if (check_state_ == CheckState::unchecked) {
            next = CheckState::checked;
        } else if (check_state_ == CheckState::checked && three_state.read()) {
            next = CheckState::indeterminate;
        }
        check_state = next;
        if (!is_alive()) {
            return;
        }
    }
    ButtonBase::on_activate();
}

SemanticDescriptor CheckBox::semantic_descriptor() const {
    SemanticDescriptor descriptor = ButtonBase::semantic_descriptor();
    descriptor.role = SemanticRole::check_box;
    descriptor.states |= SemanticState::checkable;
    descriptor.value = check_state_ == CheckState::checked ? "checked"
        : check_state_ == CheckState::indeterminate ? "mixed" : "unchecked";
    if (check_state_ == CheckState::checked) descriptor.states |= SemanticState::checked;
    if (check_state_ == CheckState::indeterminate) descriptor.states |= SemanticState::mixed;
    return descriptor;
}

namespace {

// The events CheckBox adds to those of ButtonBase, in declaration order.
constexpr std::array<InteractionDeclaration, 2> check_box_interactions{{
    {.name = "check_state_changed", .payload = "CheckState", .mutable_payload = false},
    {.name = "checked_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void CheckBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ButtonBase::append_interactions(interactions);
    interactions.insert(interactions.end(), check_box_interactions.begin(), check_box_interactions.end());
}

// property_codemod begin CheckBox (tools/property_families/check_box.json; generated, do not edit)

namespace {

PropertyDescriptor check_box_check_state_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "check_state";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Behavior";
    descriptor.description = "Unchecked, checked or indeterminate; checked follows it.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"check_state", "unchecked", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = check_state_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_check_box_check_state() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        check_box_check_state_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor check_box_checked_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "checked";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the check box is in a checked state.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.binding_direction = PropertyBindingDirection::two_way;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_check_box_checked() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        check_box_checked_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor check_box_three_state_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "three_state";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the box also takes the indeterminate state; clearing it clears that state.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_check_box_three_state() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        check_box_three_state_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor check_box_indicator_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "indicator_style";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "How the check mark draws: classic, modern or toggle.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"indicator_style", "classic", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = button_base_detail::choice_indicator_style_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_check_box_indicator_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        check_box_indicator_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<CheckState>::type() {
    return PropertyType{BindingValueKind::enumeration, false, check_state_enum()};
}

BindingValue PropertyValueTraits<CheckState>::to_binding(CheckState value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = check_state_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

CheckState PropertyValueTraits<CheckState>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<CheckState>(choice.value);
}

PropertyType PropertyValueTraits<ChoiceIndicatorStyle>::type() {
    return PropertyType{BindingValueKind::enumeration, false, button_base_detail::choice_indicator_style_enum()};
}

BindingValue PropertyValueTraits<ChoiceIndicatorStyle>::to_binding(ChoiceIndicatorStyle value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = button_base_detail::choice_indicator_style_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

ChoiceIndicatorStyle PropertyValueTraits<ChoiceIndicatorStyle>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<ChoiceIndicatorStyle>(choice.value);
}

const std::array<PropertyEntry, CheckBox::check_box_property_count>& CheckBox::check_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"check_state", &describe_check_box_check_state,
         &read_cell_entry<CheckBox, decltype(&CheckBox::check_state), &CheckBox::check_state>,
         &write_cell_entry<CheckBox, decltype(&CheckBox::check_state), &CheckBox::check_state>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<CheckBox, decltype(&CheckBox::check_state_changed_), &CheckBox::check_state_changed_>},
        {"checked", &describe_check_box_checked,
         &read_cell_entry<CheckBox, decltype(&CheckBox::checked), &CheckBox::checked>,
         &write_cell_entry<CheckBox, decltype(&CheckBox::checked), &CheckBox::checked>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<CheckBox, decltype(&CheckBox::checked_changed_), &CheckBox::checked_changed_>},
        {"three_state", &describe_check_box_three_state,
         &read_cell_entry<CheckBox, decltype(&CheckBox::three_state), &CheckBox::three_state>,
         &write_cell_entry<CheckBox, decltype(&CheckBox::three_state), &CheckBox::three_state>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"indicator_style", &describe_check_box_indicator_style,
         &read_cell_entry<CheckBox, decltype(&CheckBox::indicator_style), &CheckBox::indicator_style>,
         &write_cell_entry<CheckBox, decltype(&CheckBox::indicator_style), &CheckBox::indicator_style>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "CheckBox's property table names a property twice");
    static const std::array<PropertyEntry, check_box_property_count> entries =
        join_property_tables(ButtonBase::button_base_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> CheckBox::property_table() const noexcept {
    const std::array<PropertyEntry, check_box_property_count>& entries = check_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void CheckBox::prepare_write(const decltype(auto_check)&, bool&) {
    require_mutable();
}

void CheckBox::written(const decltype(auto_check)&) {
    invalidate(Dirty::semantics);
}

void CheckBox::prepare_write(const decltype(appearance)&, CheckBoxAppearance& incoming) {
    require_mutable();
    if (incoming != CheckBoxAppearance::normal && incoming != CheckBoxAppearance::button) throw std::invalid_argument("invalid CheckBox appearance");
}

void CheckBox::written(const decltype(appearance)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void CheckBox::prepare_write(const decltype(indicator_style)&, ChoiceIndicatorStyle& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(ChoiceIndicatorStyle::toggle)) throw std::invalid_argument("CheckBox indicator style is invalid");
}

void CheckBox::written(const decltype(indicator_style)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod end CheckBox

} // namespace gui_forms
