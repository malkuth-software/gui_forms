#include "gui_forms/controls/panel/flags_value_editor/flags_value_editor.hpp"

#include "property_editor_drop_down_layer/property_editor_drop_down_layer.hpp"
#include "gui_forms/detail/weak_member_callback.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <string>
#include <optional>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

using detail::PropertyEditorDropDownLayer;

FlagsValueEditor::FlagsValueEditor(
    StableId stable_id, PropertyEnumDescriptor descriptor,
    PropertyEnumValue value)
    : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    set_border_style(BorderStyle::sunken);
    set_focusable(true);
    set_cursor(CursorKind::hand);
    set_descriptor(std::move(descriptor));
    set_value(std::move(value));
}

void FlagsValueEditor::set_descriptor(PropertyEnumDescriptor descriptor) {
    require_mutable();
    if (!descriptor.flags || !valid_property_enum_descriptor(descriptor)) {
        throw std::invalid_argument(
            "FlagsValueEditor requires a valid finite flags descriptor");
    }
    bool has_bit = false;
    for (const PropertyEnumChoice& choice : descriptor.choices) {
        if (choice.value > 0 &&
            std::has_single_bit(static_cast<std::uint64_t>(choice.value))) {
            has_bit = true;
            break;
        }
    }
    if (!has_bit) {
        throw std::invalid_argument(
            "FlagsValueEditor requires at least one positive single-bit choice");
    }
    if (descriptor_ == descriptor) return;
    std::optional<PropertyEnumValue> normalized_value;
    if (!value_.type_name.empty()) {
        PropertyDescriptor property;
        property.name = "value";
        property.kind = BindingValueKind::enumeration;
        property.enumeration =
            std::make_shared<const PropertyEnumDescriptor>(descriptor);
        const std::optional<BindingValue> normalized = convert_property_value(BindingValue{value_},
                                                        property);
        if (!normalized) {
            throw std::invalid_argument(
                "FlagsValueEditor descriptor cannot represent its current value");
        }
        normalized_value = std::get<PropertyEnumValue>(*normalized);
    }
    descriptor_ = std::move(descriptor);
    if (normalized_value) value_ = std::move(*normalized_value);
    if (dropped_down_) {
        close_drop_down();
        open_drop_down();
    }
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void FlagsValueEditor::set_value(PropertyEnumValue value) {
    require_mutable();
    PropertyDescriptor property;
    property.name = "value";
    property.kind = BindingValueKind::enumeration;
    property.enumeration =
        std::make_shared<const PropertyEnumDescriptor>(descriptor_);
    const std::optional<BindingValue> normalized = convert_property_value(BindingValue{std::move(value)},
                                                    property);
    if (!normalized) {
        throw std::invalid_argument(
            "FlagsValueEditor value does not belong to its descriptor");
    }
    const PropertyEnumValue next = std::get<PropertyEnumValue>(*normalized);
    if (value_ == next) return;
    value_ = next;
    synchronize_popup();
    invalidate(Dirty::paint | Dirty::semantics);
}

void FlagsValueEditor::set_dropped_down(bool dropped_down) {
    require_mutable();
    if (dropped_down == dropped_down_) return;
    if (dropped_down) open_drop_down();
    else close_drop_down();
}

void FlagsValueEditor::set_popup_width(double width) {
    require_mutable();
    if (!std::isfinite(width) || width < 80.0 || width > 1024.0) {
        throw std::invalid_argument(
            "flags editor popup width must be within [80, 1024]");
    }
    if (popup_width_ == width) return;
    popup_width_ = width;
    if (dropped_down_) {
        close_drop_down();
        open_drop_down();
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void FlagsValueEditor::open_drop_down() {
    if (dropped_down_ || !attached() || window() == nullptr) return;
    const Control::Ptr root = (*window()).root();
    if (!root) return;
    const Rect editor = absolute_bounds();
    const Size client = (*window()).client_size();
    popup_choice_indices_.clear();
    std::vector<std::string> items;
    for (std::size_t index = 0U; index < descriptor_.choices.size(); ++index) {
        const std::int64_t choice = descriptor_.choices[index].value;
        if (choice == 0 || (choice > 0 &&
            std::has_single_bit(static_cast<std::uint64_t>(choice)))) {
            popup_choice_indices_.push_back(index);
            items.push_back(display_label(descriptor_.choices[index].name));
        }
    }
    if (items.empty()) return;
    const std::size_t rows = std::min<std::size_t>(8U, items.size());
    const double height = static_cast<double>(rows) *
        26.0 * effective_text_scale() + 4.0;
    const double y = editor.y + editor.height + height <= client.height
        ? editor.y + editor.height : std::max(0.0, editor.y - height);
    const std::string prefix(stable_id().value());
    std::shared_ptr<gui_forms::detail::PropertyEditorDropDownLayer> layer = make_control<PropertyEditorDropDownLayer>(
        StableId(prefix + ".popup.layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    std::shared_ptr<gui_forms::CheckedListBox> list = make_control<CheckedListBox>(
        StableId(prefix + ".popup.list"));
    (*list).set_paint_plane(PaintPlane::overlay);
    (*list).set_items(std::move(items));
    (*list).set_check_on_click(true);
    const double popup_width = std::min(popup_width_, client.width);
    const double popup_x = std::clamp(
        editor.x, 0.0, std::max(0.0, client.width - popup_width));
    (*list).set_requested_bounds({popup_x, y, popup_width, height});
    (*layer).add_child(list);
    PopupToken token = (*window()).open_popup(shared_from_this(), layer);

    popup_layer_ = layer;
    popup_list_ = list;
    popup_token_ = std::move(token);
    const std::weak_ptr<FlagsValueEditor> weak =
        std::static_pointer_cast<FlagsValueEditor>(shared_from_this());
    popup_check_ = (*list).item_check_state_changed().subscribe(
        *this,
        detail::WeakMemberCallback<
            void (FlagsValueEditor::*)(std::size_t, CheckState)>(
                weak, &FlagsValueEditor::apply_popup_choice));
    popup_dismissal_ = (*layer).dismissed().subscribe(
        *this,
        detail::WeakMemberCallback<void (FlagsValueEditor::*)()>(
            weak, &FlagsValueEditor::close_drop_down));
    if (Event<>* closed = popup_token_.closed_event()) {
        popup_revocation_ = (*closed).subscribe(
            *this,
            detail::WeakMemberCallback<void (FlagsValueEditor::*)()>(
                weak, &FlagsValueEditor::on_popup_revoked));
    }
    popup_scope_ = (*window()).begin_focus_scope(layer, list).value;
    dropped_down_ = true;
    synchronize_popup();
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(drop_down_changed_, true);
}

void FlagsValueEditor::close_drop_down() {
    if (!dropped_down_ && !popup_layer_) return;
    closing_popup_ = true;
    popup_check_.disconnect();
    popup_dismissal_.disconnect();
    if (window() != nullptr && popup_scope_ != 0U) {
        static_cast<void>((*window()).end_focus_scope(FocusScopeId{popup_scope_}));
    }
    popup_scope_ = 0U;
    popup_token_.disconnect();
    popup_revocation_.disconnect();
    popup_list_.reset();
    popup_layer_.reset();
    popup_choice_indices_.clear();
    const bool changed = dropped_down_;
    dropped_down_ = false;
    closing_popup_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(drop_down_changed_, false);
}

void FlagsValueEditor::on_popup_revoked() {
    if (closing_popup_) return;
    popup_check_.disconnect();
    popup_dismissal_.disconnect();
    popup_revocation_.disconnect();
    if (window() != nullptr && popup_scope_ != 0U) {
        static_cast<void>((*window()).end_focus_scope(
            FocusScopeId{popup_scope_},
            FocusScopeCloseReason::owner_unavailable));
    }
    popup_scope_ = 0U;
    popup_list_.reset();
    popup_layer_.reset();
    popup_choice_indices_.clear();
    const bool changed = dropped_down_;
    dropped_down_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(drop_down_changed_, false);
}

void FlagsValueEditor::synchronize_popup() {
    if (!popup_list_) return;
    synchronizing_popup_ = true;
    const std::uint64_t bits = static_cast<std::uint64_t>(value_.value);
    for (std::size_t popup_index = 0U;
         popup_index < popup_choice_indices_.size(); ++popup_index) {
        const std::int64_t choice =
            descriptor_.choices[popup_choice_indices_[popup_index]].value;
        const bool checked = choice == 0
            ? value_.value == 0
            : (bits & static_cast<std::uint64_t>(choice)) ==
                  static_cast<std::uint64_t>(choice);
        (*popup_list_).set_item_checked(popup_index, checked);
    }
    synchronizing_popup_ = false;
}

void FlagsValueEditor::apply_popup_choice(std::size_t popup_index,
                                           CheckState state) {
    if (synchronizing_popup_ || state == CheckState::indeterminate ||
        popup_index >= popup_choice_indices_.size()) return;
    const PropertyEnumChoice& choice =
        descriptor_.choices[popup_choice_indices_[popup_index]];
    std::uint64_t bits = static_cast<std::uint64_t>(value_.value);
    if (choice.value == 0) {
        if (state == CheckState::checked) bits = 0U;
    } else if (state == CheckState::checked) {
        bits |= static_cast<std::uint64_t>(choice.value);
    } else {
        bits &= ~static_cast<std::uint64_t>(choice.value);
    }
    PropertyDescriptor property;
    property.name = "value";
    property.kind = BindingValueKind::enumeration;
    property.enumeration =
        std::make_shared<const PropertyEnumDescriptor>(descriptor_);
    const std::optional<BindingValue> normalized = convert_property_value(
        BindingValue{static_cast<std::int64_t>(bits)}, property);
    if (!normalized) {
        synchronize_popup();
        return;
    }
    const PropertyEnumValue next = std::get<PropertyEnumValue>(*normalized);
    if (next == value_) {
        synchronize_popup();
        return;
    }
    value_ = next;
    synchronize_popup();
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(value_changed_, value_);
}

// The panel's well first; then the drop-down button, its divider and arrow
// (as the combo box's), the value's text and the focus ring are parts the
// theme's engine draws in the control's anatomy states. A style override
// inks the text and the arrow as authored colours (T4-1). The ring shows
// while the field has focus or its list is open.
void FlagsValueEditor::on_paint(Painter& painter, Rect damage) {
    Panel::on_paint(painter, damage);
    const Rect bounds = local_bounds();
    const AnatomyStateSet states = anatomy_states();
    std::optional<Color> ink{};
    if (has_style_override()) ink = style().text;
    const double button_width = std::min(24.0, bounds.width);
    const double divider_x = std::max(0.0, bounds.width - button_width);
    PartPaint drop_down;
    drop_down.family = ControlVisualRole::choice;
    drop_down.role = PartRole::actuator;
    drop_down.states = states;
    drop_down.bounds = {divider_x, 1.0, std::max(0.0, button_width - 1.0), std::max(0.0, bounds.height - 2.0)};
    paint_part(painter, *this, drop_down);
    PartPaint divider = drop_down;
    divider.role = PartRole::divider;
    divider.bounds = {divider_x - 0.5, 1.0, 1.0, std::max(0.0, bounds.height - 2.0)};
    paint_part(painter, *this, divider);
    // The arrow flips and moves when the list is open, as the combo box's.
    const double center_x = bounds.width - button_width * 0.5;
    const double center_y = bounds.height * 0.5 + (dropped_down_ ? 2.0 : -1.0);
    const double square_top = dropped_down_ ? std::floor(center_y) - 3.0 : std::floor(center_y) - 4.0;
    PartPaint disclosure = drop_down;
    disclosure.role = PartRole::disclosure;
    disclosure.mark = dropped_down_ ? PartMark::arrow_up : PartMark::arrow_down;
    disclosure.bounds = {std::floor(center_x) - 4.0, square_top, 8.0, 8.0};
    disclosure.authored_foreground = ink;
    paint_part(painter, *this, disclosure);
    const FontSpec font = effective_font({FontRole::content, 10.0, 400, false});
    PartPaint content;
    content.family = ControlVisualRole::editor;
    content.role = PartRole::content;
    content.states = states;
    content.font = font;
    content.authored_foreground = ink;
    const std::string label = display_label(value_.name);
    content.text = label;
    paint_text_part_at(painter, *this, content,
                       {7.0, std::max(font.size, (bounds.height + font.size) * 0.5 - 1.0)},
                       std::max(0.0, divider_x - 11.0));
    if (focused_ || dropped_down_) {
        PartPaint ring;
        ring.family = ControlVisualRole::editor;
        ring.role = PartRole::focus;
        ring.bounds = bounds;
        ring.states = states;
        ring.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, ring);
    }
}

void FlagsValueEditor::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        set_dropped_down(!dropped_down_);
        event.handled = true;
    } else if (event.action == PointerAction::up &&
               event.button == PointerButton::primary) {
        event.handled = true;
    }
}

void FlagsValueEditor::on_key(KeyEvent& event) {
    if (!focused_ || !enabled() || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::escape && dropped_down_) {
        close_drop_down();
        event.handled = true;
    } else if (event.physical_key == PhysicalKey::f4 ||
               event.physical_key == PhysicalKey::space ||
               (event.physical_key == PhysicalKey::down &&
                has_modifier(event.modifiers, Modifier::alt))) {
        set_dropped_down(!dropped_down_);
        event.handled = true;
    }
}

void FlagsValueEditor::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor FlagsValueEditor::semantic_descriptor() const {
    SemanticDescriptor result;
    result.role = SemanticRole::combo_box;
    result.name = accessible_name();
    result.value = display_label(value_.name);
    result.description = accessible_description();
    if (dropped_down_) result.states |= SemanticState::expanded;
    result.actions = {SemanticAction::focus,
        dropped_down_ ? SemanticAction::collapse : SemanticAction::expand};
    result.exposed = true;
    return result;
}

bool FlagsValueEditor::on_semantic_action(SemanticAction action,
                                           std::string_view value) {
    if (action == SemanticAction::expand) {
        set_dropped_down(true);
        return true;
    }
    if (action == SemanticAction::collapse) {
        set_dropped_down(false);
        return true;
    }
    return Panel::on_semantic_action(action, value);
}

void FlagsValueEditor::on_detached_from_window() noexcept {
    popup_check_.disconnect();
    popup_dismissal_.disconnect();
    popup_revocation_.disconnect();
    popup_scope_ = 0U;
    popup_token_.disconnect();
    popup_list_.reset();
    popup_layer_.reset();
    popup_choice_indices_.clear();
    dropped_down_ = false;
    focused_ = false;
}


namespace {

constexpr std::array<PartField<FlagsValueEditor::Parts>, 6> flags_value_editor_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &FlagsValueEditor::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &FlagsValueEditor::Parts::edge},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &FlagsValueEditor::Parts::content},
    {.declaration = {.name = "divider", .role = PartRole::divider, .composed_from = {}},
     .member = &FlagsValueEditor::Parts::divider},
    {.declaration = {.name = "drop_down", .role = PartRole::actuator, .composed_from = "button"},
     .member = &FlagsValueEditor::Parts::drop_down},
    {.declaration = {.name = "disclosure", .role = PartRole::disclosure, .composed_from = {}},
     .member = &FlagsValueEditor::Parts::disclosure},
}};

constexpr std::array<StateField<FlagsValueEditor::States>, 11> flags_value_editor_state_fields{{
    {.state = AnatomyState::normal, .member = &FlagsValueEditor::States::normal},
    {.state = AnatomyState::pressed, .member = &FlagsValueEditor::States::pressed},
    {.state = AnatomyState::captured, .member = &FlagsValueEditor::States::captured},
    {.state = AnatomyState::enabled, .member = &FlagsValueEditor::States::enabled},
    {.state = AnatomyState::disabled, .member = &FlagsValueEditor::States::disabled},
    {.state = AnatomyState::focused, .member = &FlagsValueEditor::States::focused},
    {.state = AnatomyState::focus_visible, .member = &FlagsValueEditor::States::focus_visible},
    {.state = AnatomyState::open, .member = &FlagsValueEditor::States::open},
    {.state = AnatomyState::closed, .member = &FlagsValueEditor::States::closed},
    {.state = AnatomyState::invalid, .member = &FlagsValueEditor::States::invalid},
    {.state = AnatomyState::pending, .member = &FlagsValueEditor::States::pending},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 6>
    flags_value_editor_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(flags_value_editor_extra_part_fields));
constexpr std::array<AnatomyState, 11> flags_value_editor_states =
    state_declarations(flags_value_editor_state_fields);

constexpr ControlAnatomy flags_value_editor_anatomy{
    .control_id = "flags_value_editor",
    .parts = flags_value_editor_parts,
    .states = flags_value_editor_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& FlagsValueEditor::anatomy() const noexcept {
    return flags_value_editor_anatomy;
}

void FlagsValueEditor::publish_part_geometry(std::span<PartGeometry> geometry) const {

    const Rect bounds = local_bounds();
    const Panel::Parts panel = panel_parts();
    const double button_width = std::min(24.0, bounds.width);
    const double divider_x = std::max(0.0, bounds.width - button_width);
    const Rect button{divider_x, 1.0, std::max(0.0, button_width - 1.0),
                      std::max(0.0, bounds.height - 2.0)};
    const Rect divider{divider_x - 0.5, 1.0, 1.0, std::max(0.0, bounds.height - 2.0)};
    const Rect content{4.0, 2.0, std::max(0.0, divider_x - 8.0),
                       std::max(0.0, bounds.height - 4.0)};
    // The chevron's two one-unit strokes span 8 x 4 about the button centre.
    const double center_x = bounds.width - button_width * 0.5;
    const double center_y = bounds.height * 0.5;
    const Rect chevron{center_x - 4.5, center_y - 2.5, 9.0, 5.0};
    Parts record;
    record.scrollable = panel.scrollable;
    record.face = panel.face;
    record.edge = panel.edge;
    record.content = PartGeometry::from_bounds(content);
    record.divider = PartGeometry::from_bounds(divider);
    record.drop_down = PartGeometry::from_bounds(button);
    record.disclosure = PartGeometry::from_bounds(chevron);
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), flags_value_editor_extra_part_fields, record, geometry);
}

FlagsValueEditor::Parts FlagsValueEditor::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), flags_value_editor_extra_part_fields, published.parts(), record);
    return record;
}

AnatomyStateSet FlagsValueEditor::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (dropped_down_) {
        states.insert(AnatomyState::open);
    } else {
        states.insert(AnatomyState::closed);
    }
    if (visual_status() == ControlVisualStatus::invalid) {
        states.insert(AnatomyState::invalid);
    } else if (visual_status() == ControlVisualStatus::pending) {
        states.insert(AnatomyState::pending);
    }
    return states;
}

FlagsValueEditor::States FlagsValueEditor::states() const {
    States record;
    read_states(flags_value_editor_state_fields, anatomy_states(), record);
    return record;
}

namespace {

// The events FlagsValueEditor adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 2> flags_value_editor_interactions{{
    {.name = "value_changed", .payload = "PropertyEnumValue", .mutable_payload = false},
    {.name = "drop_down_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void FlagsValueEditor::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), flags_value_editor_interactions.begin(), flags_value_editor_interactions.end());
}

} // namespace gui_forms
