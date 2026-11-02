#include "gui_forms/controls/button_base/check_box/check_box.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/text.hpp"
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

const ControlAnatomy& CheckBox::anatomy() const noexcept {
    return check_box_anatomy;
}

void CheckBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    if (appearance_ == CheckBoxAppearance::button) {
        // The button appearance is a push button whose selection is the
        // check state; it has a face and no well or indicator.
        const bool layout_selected = !has_style_override() && checked();
        const ButtonBase::Parts base = push_button_parts(layout_selected, false);
        record.region = base.region;
        record.face = base.face;
        record.icon = base.icon;
        record.label = base.label;
        record.focus = base.focus;
        write_parts(anatomy(), check_box_part_fields, record, geometry);
        return;
    }
    const Rect bounds = local_bounds();
    const double indicator_width =
        indicator_style_ == ChoiceIndicatorStyle::toggle ? 30.0 : 15.0;
    const Rect box{1.0, std::max(1.0, (bounds.height - 15.0) * 0.5),
                   indicator_width, 15.0};
    // The mark inside the well: the toggle's knob, the check stroke's extent
    // or the indeterminate bar, as on_paint draws them.
    Rect mark{};
    if (indicator_style_ == ChoiceIndicatorStyle::toggle) {
        const double knob_x = checked() ? box.x + box.width - 13.0 : box.x + 2.0;
        mark = {knob_x, box.y + 2.0, 11.0, 11.0};
    } else if (check_state_ == CheckState::checked) {
        // Two-unit strokes through (3, 7), (6, 10) and (13, 3) from the box.
        mark = {box.x + 2.0, box.y + 2.0, 12.0, 9.0};
    } else if (check_state_ == CheckState::indeterminate) {
        mark = has_style_override() ? Rect{box.x + 3.0, box.y + 6.0, 9.0, 4.0}
                                    : Rect{box.x + 4.0, box.y + 6.0, 8.0, 3.0};
    }
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

void CheckBox::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    if (appearance_ == CheckBoxAppearance::button && !has_style_override()) {
        paint_themed_button(painter, bounds, ControlVisualRole::button, false,
                            false, checked());
        return;
    }
    if (!has_style_override()) {
        const double indicator_width =
            indicator_style_ == ChoiceIndicatorStyle::toggle ? 30.0 : 15.0;
        const Rect box{1.0, std::max(1.0, (bounds.height - 15.0) * 0.5),
                       indicator_width, 15.0};
        const ControlVisualContext context = visual_context(
            hovered_visual(), pressed_visual(), checked(), focus_cue_visible());
        const ControlVisualRecipe& recipe = effective_theme().resolve(
            ControlVisualRole::choice, context);
        SurfaceMaterial indicator = recipe.material;
        indicator.corner_radius = indicator_style_ == ChoiceIndicatorStyle::toggle
            ? 7.5 : 2.0;
        paint_surface_material(painter, box, indicator);
        if (check_state_ == CheckState::checked &&
            indicator_style_ != ChoiceIndicatorStyle::toggle) {
            painter.draw_line({box.x + 3.0, box.y + 7.0},
                              {box.x + 6.0, box.y + 10.0}, recipe.glyph, 2.0);
            painter.draw_line({box.x + 6.0, box.y + 10.0},
                              {box.x + 13.0, box.y + 3.0}, recipe.glyph, 2.0);
        } else if (check_state_ == CheckState::indeterminate) {
            painter.fill_rect({box.x + 4.0, box.y + 6.0, 8.0, 3.0},
                              recipe.glyph);
        } else if (indicator_style_ == ChoiceIndicatorStyle::toggle) {
            const double knob_x = checked() ? box.x + box.width - 13.0
                                             : box.x + 2.0;
            painter.fill_rounded_rect({knob_x, box.y + 2.0, 11.0, 11.0},
                                      5.5, recipe.text);
        }
        paint_button_content(
            painter,
            {box.x + box.width + 1.0, 0.0,
             std::max(0.0, bounds.width - box.x - box.width - 1.0),
             bounds.height},
            display_text(), recipe.text, {}, checked());
        paint_theme_cues(painter, bounds, recipe, context);
        return;
    }
    const BasicControlStyle& colors = style();
    const double indicator_width =
        indicator_style_ == ChoiceIndicatorStyle::toggle ? 30.0 : 15.0;
    const Rect box{1.0, std::max(1.0, (bounds.height - 15.0) * 0.5),
                   indicator_width, 15.0};
    if (indicator_style_ == ChoiceIndicatorStyle::classic) {
        painter.fill_rect(box, colors.paper);
        painter.draw_line({box.x, box.y}, {box.x + box.width, box.y},
                          colors.dark_border, 1.0);
        painter.draw_line({box.x, box.y}, {box.x, box.y + box.height},
                          colors.dark_border, 1.0);
        painter.draw_line({box.x, box.y + box.height - 1.0},
                          {box.x + box.width, box.y + box.height - 1.0},
                          colors.highlight, 1.0);
        painter.draw_line({box.x + box.width - 1.0, box.y},
                          {box.x + box.width - 1.0, box.y + box.height},
                          colors.highlight, 1.0);
    } else if (indicator_style_ == ChoiceIndicatorStyle::modern) {
        painter.fill_rect(box, checked() ? colors.accent : colors.paper);
        painter.stroke_rect({box.x + 0.5, box.y + 0.5, box.width - 1.0,
                             box.height - 1.0},
                            checked() ? colors.accent : colors.border, 1.0);
    } else {
        painter.fill_rect(box, checked() ? colors.accent : colors.border);
        const double knob_x = checked() ? box.x + box.width - 13.0 : box.x + 2.0;
        painter.fill_rect({knob_x, box.y + 2.0, 11.0, 11.0}, colors.paper);
    }
    if (check_state_ == CheckState::checked) {
        if (indicator_style_ != ChoiceIndicatorStyle::toggle) {
            const Color mark = indicator_style_ == ChoiceIndicatorStyle::modern
                ? colors.paper : colors.accent;
            painter.draw_line({4.0, box.y + 7.0}, {7.0, box.y + 10.0}, mark, 2.0);
            painter.draw_line({7.0, box.y + 10.0}, {14.0, box.y + 3.0}, mark, 2.0);
        }
    } else if (check_state_ == CheckState::indeterminate) {
        painter.fill_rect({4.0, box.y + 6.0, 9.0, 4.0}, colors.accent);
    }
    paint_button_content(
        painter,
        {box.x + box.width + 1.0, 0.0,
         std::max(0.0, bounds.width - box.x - box.width - 1.0), bounds.height},
        display_text(), effectively_enabled() ? colors.text : colors.disabled_text, {}, checked());
}

Insets CheckBox::visual_outsets() const noexcept {
    if (has_style_override()) return {};
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
