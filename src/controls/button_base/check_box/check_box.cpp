#include "gui_forms/controls/button_base/check_box/check_box.hpp"
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
            *this, model, checked_changed_, &CheckBox::checked, &CheckBox::set_checked);
    // Retire the old model before the initial model-wins publication.
    unbind_command();
    value_binding_ = std::move(binding);
    (*value_binding_).synchronize();
}

void CheckBox::unbind() noexcept { value_binding_.reset(); }

CheckBox::CheckBox(StableId stable_id, std::string text)
    : ButtonBase(std::move(stable_id), std::move(text)) {
    set_text_alignment(ContentAlignment::middle_left);
    set_image_alignment(ContentAlignment::middle_left);
    define_bindable_property({
        {"checked", BindingValueKind::boolean, "Behavior",
         "Whether the check box is in a checked state.", BindingValue{false},
         Dirty::paint | Dirty::semantics},
        detail::BindingNoexceptMethodGetter<CheckBox, bool>(
            *this, &CheckBox::checked),
        detail::ConvertedPropertySetter<CheckBox, bool>(
            *this, &CheckBox::set_checked, BindingValueKind::boolean,
            "CheckBox.Checked binding requires Boolean"),
        detail::EventChangeConnector<bool>(checked_changed_), {}, {}});

    PropertyDescriptor check_state;
    check_state.name = "check_state";
    check_state.kind = BindingValueKind::enumeration;
    check_state.category = "Behavior";
    check_state.description = "Unchecked, checked or indeterminate; checked follows it.";
    check_state.default_value = BindingValue{PropertyEnumValue{"check_state", "unchecked", 0}};
    check_state.invalidation_effects = Dirty::paint | Dirty::semantics;
    check_state.bindable = false;
    check_state.enumeration = check_state_enum();
    define_bindable_property({
        std::move(check_state),
        detail::EnumerationPropertyGetter<CheckBox, CheckState>(*this, &CheckBox::check_state,
                                                                check_state_enum()),
        detail::EnumerationPropertySetter<CheckBox, CheckState>(*this, &CheckBox::set_check_state),
        detail::EventChangeConnector<CheckState>(check_state_changed_), {}, {}});

    PropertyDescriptor indicator_style;
    indicator_style.name = "indicator_style";
    indicator_style.kind = BindingValueKind::enumeration;
    indicator_style.category = "Appearance";
    indicator_style.description = "How the check mark draws: classic, modern or toggle.";
    indicator_style.default_value = BindingValue{PropertyEnumValue{"indicator_style", "classic", 0}};
    indicator_style.invalidation_effects = Dirty::paint | Dirty::semantics;
    indicator_style.bindable = false;
    indicator_style.enumeration = button_base_detail::choice_indicator_style_enum();
    define_bindable_property({
        std::move(indicator_style),
        detail::EnumerationPropertyGetter<CheckBox, ChoiceIndicatorStyle>(
            *this, &CheckBox::indicator_style, button_base_detail::choice_indicator_style_enum()),
        detail::EnumerationPropertySetter<CheckBox, ChoiceIndicatorStyle>(*this, &CheckBox::set_indicator_style),
        {}, {}, {}});

    // A document writes three_state before an indeterminate check_state:
    // properties apply in document order.
    PropertyDescriptor three_state;
    three_state.name = "three_state";
    three_state.kind = BindingValueKind::boolean;
    three_state.category = "Behavior";
    three_state.description = "Whether the box also takes the indeterminate state; clearing it clears that state.";
    three_state.default_value = BindingValue{false};
    three_state.invalidation_effects = Dirty::paint | Dirty::semantics;
    three_state.bindable = false;
    define_bindable_property({
        std::move(three_state),
        detail::BindingNoexceptMethodGetter<CheckBox, bool>(*this, &CheckBox::three_state),
        detail::DirectPropertySetter<CheckBox, bool>(*this, &CheckBox::set_three_state), {}, {}, {}});
}

void CheckBox::set_check_state(CheckState state) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    if (state != CheckState::unchecked && state != CheckState::checked &&
        state != CheckState::indeterminate) {
        throw std::invalid_argument("invalid check state");
    }
    if (!three_state_ && state == CheckState::indeterminate) {
        state = CheckState::checked;
    }
    if (check_state_ == state) {
        return;
    }
    if (value_binding_) (*value_binding_).validate_update(state == CheckState::checked);
    if (!is_alive()) return;
    const bool previous_checked = checked();
    check_state_ = state;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(check_state_changed_, check_state_);
    if (!is_alive()) {
        return;
    }
    if (previous_checked != checked()) {
        publish_change(checked_changed_, checked());
    }
}

void CheckBox::set_checked(bool checked_value) {
    set_check_state(checked_value ? CheckState::checked : CheckState::unchecked);
}

void CheckBox::set_three_state(bool enabled_value) {
    require_mutable();
    if (three_state_ == enabled_value) {
        return;
    }
    three_state_ = enabled_value;
    if (!three_state_ && check_state_ == CheckState::indeterminate) {
        set_check_state(CheckState::checked);
        return;
    }
    invalidate(Dirty::semantics);
}

void CheckBox::set_auto_check(bool enabled_value) {
    require_mutable();
    if (auto_check_ == enabled_value) {
        return;
    }
    auto_check_ = enabled_value;
    invalidate(Dirty::semantics);
}

void CheckBox::set_appearance(const CheckBoxAppearance appearance_value) {
    require_mutable();
    if (appearance_value != CheckBoxAppearance::normal &&
        appearance_value != CheckBoxAppearance::button) {
        throw std::invalid_argument("invalid CheckBox appearance");
    }
    if (appearance_ == appearance_value) return;
    appearance_ = appearance_value;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void CheckBox::set_indicator_style(ChoiceIndicatorStyle style_value) {
    require_mutable();
    switch (style_value) {
    case ChoiceIndicatorStyle::classic:
    case ChoiceIndicatorStyle::modern:
    case ChoiceIndicatorStyle::toggle:
        break;
    default:
        throw std::invalid_argument("CheckBox indicator style is invalid");
    }
    if (indicator_style_ == style_value) {
        return;
    }
    indicator_style_ = style_value;
    invalidate(Dirty::paint | Dirty::semantics);
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
    if (appearance_ == CheckBoxAppearance::button) {
        // The button appearance is a push button whose selection is the
        // check state; it has a face and no well or indicator.
        const ButtonBase::Parts base = push_button_parts(checked(), false);
        record.region = base.region;
        record.face = base.face;
        record.icon = base.icon;
        record.label = base.label;
        record.focus = base.focus;
        write_parts(anatomy(), check_box_part_fields, record, geometry);
        return;
    }
    const Rect bounds = local_bounds();
    const Rect box = check_box_well(bounds, indicator_style_);
    const PartMark mark_kind = check_box_mark(indicator_style_, check_state_);
    const Rect mark = check_box_mark_bounds(box, mark_kind, checked());
    const Rect content_face{box.x + box.width + 1.0, 0.0,
                            std::max(0.0, bounds.width - box.x - box.width - 1.0),
                            bounds.height};
    const ContentLayout layout =
        published_content_layout(content_face, checked(), false);
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
    if (appearance_ == CheckBoxAppearance::button) {
        paint_themed_button(painter, bounds, ControlVisualRole::button, false,
                            false, checked());
        return;
    }
    const Rect box = check_box_well(bounds, indicator_style_);
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
    well.corner_radius = indicator_style_ == ChoiceIndicatorStyle::toggle ? 7.5 : 2.0;
    well.authored_background = ground;
    well.key = PartKey{"well", {}, {}};
    paint_part(painter, *this, well);
    const PartMark mark = check_box_mark(indicator_style_, check_state_);
    if (mark != PartMark::none) {
        PartPaint indicator;
        indicator.family = ControlVisualRole::choice;
        indicator.role = PartRole::indicator;
        indicator.mark = mark;
        indicator.bounds = check_box_mark_bounds(box, mark, checked());
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
    paint_button_content(painter, content, display_text(), content_ink, {}, checked());
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
        hovered_visual(), pressed_visual(), checked(), focus_cue_visible());
    if (appearance_ == CheckBoxAppearance::button) {
        return resolved_visual_outsets(ControlVisualRole::button, context);
    }
    return surface_material_visual_outsets(
        effective_theme().resolve(ControlVisualRole::choice, context).material);
}

void CheckBox::on_activate() {
    const Control::Ptr retained = weak_from_this().lock();
    if (auto_check_) {
        CheckState next = CheckState::unchecked;
        if (check_state_ == CheckState::unchecked) {
            next = CheckState::checked;
        } else if (check_state_ == CheckState::checked && three_state_) {
            next = CheckState::indeterminate;
        }
        set_check_state(next);
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

} // namespace gui_forms
