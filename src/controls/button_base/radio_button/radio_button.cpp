#include "gui_forms/controls/button_base/radio_button/radio_button.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "../choice_indicator_property.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

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
    define_bindable_property({
        {"checked", BindingValueKind::boolean, "Behavior",
         "Whether the radio button is selected within its group.",
         BindingValue{false}, Dirty::paint | Dirty::semantics},
        detail::BindingMemberGetter<RadioButton, bool>(
            *this, &RadioButton::checked_),
        detail::ConvertedPropertySetter<RadioButton, bool>(
            *this, &RadioButton::set_checked, BindingValueKind::boolean,
            "RadioButton.Checked binding requires Boolean"),
        detail::EventChangeConnector<bool>(checked_changed_), {}, {}});

    PropertyDescriptor indicator_style;
    indicator_style.name = "indicator_style";
    indicator_style.kind = BindingValueKind::enumeration;
    indicator_style.category = "Appearance";
    indicator_style.description = "How the choice mark draws: classic, modern or toggle.";
    indicator_style.default_value = BindingValue{PropertyEnumValue{"indicator_style", "classic", 0}};
    indicator_style.invalidation_effects = Dirty::paint | Dirty::semantics;
    indicator_style.bindable = false;
    indicator_style.enumeration = button_base_detail::choice_indicator_style_enum();
    define_bindable_property({
        std::move(indicator_style),
        detail::EnumerationPropertyGetter<RadioButton, ChoiceIndicatorStyle>(
            *this, &RadioButton::indicator_style, button_base_detail::choice_indicator_style_enum()),
        detail::EnumerationPropertySetter<RadioButton, ChoiceIndicatorStyle>(*this, &RadioButton::set_indicator_style),
        {}, {}, {}});

    PropertyDescriptor group_name;
    group_name.name = "group_name";
    group_name.kind = BindingValueKind::text;
    group_name.category = "Behavior";
    group_name.description = "The exclusion group among the parent's radio buttons; empty is the parent's own group.";
    group_name.default_value = BindingValue{std::string{}};
    group_name.invalidation_effects = Dirty::semantics;
    group_name.bindable = false;
    define_bindable_property({
        std::move(group_name),
        detail::BindingMemberGetter<RadioButton, std::string>(*this, &RadioButton::group_name_),
        detail::DirectPropertySetter<RadioButton, std::string>(*this, &RadioButton::set_group_name), {}, {}, {}});
}

void RadioButton::set_checked_without_exclusion(bool checked_value) {
    require_mutable();
    if (checked_ == checked_value) {
        return;
    }
    checked_ = checked_value;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(checked_changed_, checked_);
}

void RadioButton::set_checked(bool checked_value) {
    require_mutable();
    if (checked_ == checked_value) {
        return;
    }
    if (checked_value) {
        if (const Control::Ptr owner = parent()) {
            for (const Control::Ptr& sibling : (*owner).children()) {
                std::shared_ptr<gui_forms::RadioButton> peer = std::dynamic_pointer_cast<RadioButton>(sibling);
                if (peer && peer.get() != this && (*peer).group_name_ == group_name_ &&
                    (*peer).checked_) {
                    (*peer).set_checked_without_exclusion(false);
                    if (!is_alive()) {
                        return;
                    }
                }
            }
        }
    }
    set_checked_without_exclusion(checked_value);
}

void RadioButton::set_group_name(std::string name) {
    require_mutable();
    if (group_name_ == name) {
        return;
    }
    group_name_ = std::move(name);
    invalidate(Dirty::semantics);
    if (checked_) {
        if (const Control::Ptr owner = parent()) {
            for (const Control::Ptr& sibling : (*owner).children()) {
                std::shared_ptr<gui_forms::RadioButton> peer = std::dynamic_pointer_cast<RadioButton>(sibling);
                if (peer && peer.get() != this && (*peer).group_name_ == group_name_ &&
                    (*peer).checked_) {
                    (*peer).set_checked_without_exclusion(false);
                    if (!is_alive()) {
                        return;
                    }
                }
            }
        }
    }
}

void RadioButton::set_auto_check(bool enabled_value) {
    require_mutable();
    if (auto_check_ == enabled_value) {
        return;
    }
    auto_check_ = enabled_value;
    invalidate(Dirty::semantics);
}

void RadioButton::set_indicator_style(ChoiceIndicatorStyle style_value) {
    require_mutable();
    switch (style_value) {
    case ChoiceIndicatorStyle::classic:
    case ChoiceIndicatorStyle::modern:
    case ChoiceIndicatorStyle::toggle:
        break;
    default:
        throw std::invalid_argument("RadioButton indicator style is invalid");
    }
    if (indicator_style_ == style_value) {
        return;
    }
    indicator_style_ = style_value;
    invalidate(Dirty::paint | Dirty::semantics);
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
        indicator_style_ == ChoiceIndicatorStyle::toggle ? 30.0 : 15.0;
    const double top = std::max(1.0, (bounds.height - 15.0) * 0.5);
    const Rect well{1.0, top, indicator_width, 15.0};
    // The toggle's knob, or the round dot drawn inside the 15-unit disc.
    Rect mark{};
    if (indicator_style_ == ChoiceIndicatorStyle::toggle) {
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
    const bool toggle = indicator_style_ == ChoiceIndicatorStyle::toggle;
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
    const ControlVisualContext context = visual_context(
        hovered_visual(), pressed_visual(), checked_, focus_cue_visible());
    return surface_material_visual_outsets(
        effective_theme().resolve(ControlVisualRole::choice, context).material);
}

void RadioButton::on_activate() {
    if (auto_check_ && !checked_) {
        set_checked(true);
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

} // namespace gui_forms
