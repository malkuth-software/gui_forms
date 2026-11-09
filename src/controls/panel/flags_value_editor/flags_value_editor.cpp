#include "gui_forms/controls/panel/flags_value_editor/flags_value_editor.hpp"

#include "property_editor_drop_down_layer/property_editor_drop_down_layer.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/weak_member_callback.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <optional>
#include <string>
#include <optional>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

using detail::PropertyEditorDropDownLayer;

namespace {

// The text items of a form property's collection value; throws
// std::invalid_argument naming `property` for any other value.
std::vector<std::string> text_items(const BindingValue& value, const char* property) {
    const PropertyCollectionValue* collection = std::get_if<PropertyCollectionValue>(&value);
    if (collection == nullptr || !*collection || (*collection).item_kind() != BindingValueKind::text) {
        throw std::invalid_argument(std::string("FlagsValueEditor.") + property +
                                    " requires a text collection");
    }
    const std::span<const BindingValue> values = property_collection_items(*collection);
    std::vector<std::string> items;
    items.reserve(values.size());
    for (const BindingValue& item : values) items.push_back(std::get<std::string>(item));
    return items;
}

// `items` as a form property's text collection value.
BindingValue text_collection(const std::vector<std::string>& items) {
    std::vector<BindingValue> values;
    values.reserve(items.size());
    for (const std::string& item : items) values.emplace_back(item);
    return BindingValue{make_property_collection("String", BindingValueKind::text, std::move(values))};
}

// A form property's text; throws std::invalid_argument naming `property`
// for any other value.
const std::string& text_value(const BindingValue& value, const char* property) {
    const std::string* text = std::get_if<std::string>(&value);
    if (text == nullptr) {
        throw std::invalid_argument(std::string("FlagsValueEditor.") + property + " requires text");
    }
    return *text;
}

bool single_bit(const std::int64_t value) noexcept {
    return value > 0 && std::has_single_bit(static_cast<std::uint64_t>(value));
}

} // namespace

FlagsValueEditor::FlagsValueEditor(StableId stable_id) : Panel(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    border_style = BorderStyle::sunken;
    focusable = true;
    cursor = CursorKind::hand;
}

FlagsValueEditor::FlagsValueEditor(
    StableId stable_id, PropertyEnumDescriptor initial_descriptor,
    PropertyEnumValue initial_value)
    : FlagsValueEditor(std::move(stable_id)) {
    descriptor = std::move(initial_descriptor);
    value = std::move(initial_value);
}

BindingValue FlagsValueEditor::enumeration_name_property() const {
    return BindingValue{descriptor_.type_name};
}

void FlagsValueEditor::set_enumeration_name_property(const BindingValue& binding) {
    form_enumeration_name_ = text_value(binding, "enumeration_name");
    apply_form_flags();
}

BindingValue FlagsValueEditor::flag_names_property() const {
    std::vector<std::string> names;
    for (const PropertyEnumChoice& choice : descriptor_.choices) {
        if (single_bit(choice.value)) names.push_back(choice.name);
    }
    return text_collection(names);
}

void FlagsValueEditor::set_flag_names_property(const BindingValue& binding) {
    std::vector<std::string> names = text_items(binding, "flag_names");
    if (names.size() > 62U) {
        throw std::invalid_argument("FlagsValueEditor.flag_names holds at most 62 flags");
    }
    form_flag_names_ = std::move(names);
    apply_form_flags();
}

BindingValue FlagsValueEditor::none_name_property() const {
    for (const PropertyEnumChoice& choice : descriptor_.choices) {
        if (choice.value == 0) return BindingValue{choice.name};
    }
    return BindingValue{std::string{}};
}

void FlagsValueEditor::set_none_name_property(const BindingValue& binding) {
    form_none_name_ = text_value(binding, "none_name");
    apply_form_flags();
}

BindingValue FlagsValueEditor::checked_flags_property() const {
    std::vector<std::string> names;
    const std::uint64_t bits = static_cast<std::uint64_t>(value_.value);
    for (const PropertyEnumChoice& choice : descriptor_.choices) {
        if (single_bit(choice.value) && (bits & static_cast<std::uint64_t>(choice.value)) != 0U) {
            names.push_back(choice.name);
        }
    }
    return text_collection(names);
}

void FlagsValueEditor::set_checked_flags_property(const BindingValue& binding) {
    form_checked_flags_ = text_items(binding, "checked_flags");
    apply_form_flags();
}

// Builds the descriptor and value the form properties describe and gives
// them to the editor; nothing until flag names are written. Validates the
// whole before changing anything: a checked name that is no flag's, or a
// descriptor the editor refuses, throws std::invalid_argument and leaves
// the descriptor and value as they were.
void FlagsValueEditor::apply_form_flags() {
    if (form_flag_names_.empty()) return;
    PropertyEnumDescriptor built;
    // An enumeration needs a name; `Flags` until the document writes one.
    built.type_name = form_enumeration_name_.empty() ? std::string("Flags") : form_enumeration_name_;
    built.flags = true;
    built.choices.reserve(form_flag_names_.size() + 1U);
    if (!form_none_name_.empty()) built.choices.push_back({form_none_name_, 0});
    for (std::size_t index = 0U; index < form_flag_names_.size(); ++index) {
        const std::int64_t bit = std::int64_t{1} << index;
        built.choices.push_back({form_flag_names_[index], bit});
    }
    std::uint64_t bits = 0U;
    for (const std::string& checked : form_checked_flags_) {
        const std::vector<std::string>::const_iterator found =
            std::find(form_flag_names_.begin(), form_flag_names_.end(), checked);
        if (found == form_flag_names_.end()) {
            throw std::invalid_argument("FlagsValueEditor.checked_flags names no flag: " + checked);
        }
        const std::size_t index = static_cast<std::size_t>(found - form_flag_names_.begin());
        bits |= std::uint64_t{1} << index;
    }
    if (!valid_property_enum_descriptor(built)) {
        throw std::invalid_argument("FlagsValueEditor's form properties describe no valid flags enumeration");
    }
    // The current value may not belong to the new descriptor: it is
    // replaced, not normalized.
    value_ = PropertyEnumValue{};
    descriptor = std::move(built);
    value = PropertyEnumValue{descriptor_.type_name, std::string{}, static_cast<std::int64_t>(bits)};
}

PropertyEnumDescriptor FlagsValueEditor::read_view(const decltype(descriptor)&) const {
    return descriptor_;
}

void FlagsValueEditor::write_view(const decltype(descriptor)&, PropertyEnumDescriptor incoming) {
    require_mutable();
    if (!incoming.flags || !valid_property_enum_descriptor(incoming)) {
        throw std::invalid_argument(
            "FlagsValueEditor requires a valid finite flags incoming");
    }
    bool has_bit = false;
    for (const PropertyEnumChoice& choice : incoming.choices) {
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
    if (descriptor_ == incoming) return;
    std::optional<PropertyEnumValue> normalized_value;
    if (!value_.type_name.empty()) {
        PropertyDescriptor property;
        property.name = "value";
        property.kind = BindingValueKind::enumeration;
        property.enumeration =
            std::make_shared<const PropertyEnumDescriptor>(incoming);
        const std::optional<BindingValue> normalized = convert_property_value(BindingValue{value_},
                                                        property);
        if (!normalized) {
            throw std::invalid_argument(
                "FlagsValueEditor incoming cannot represent its current value");
        }
        normalized_value = std::get<PropertyEnumValue>(*normalized);
    }
    descriptor_ = std::move(incoming);
    if (normalized_value) value_ = std::move(*normalized_value);
    if (dropped_down_) {
        close_drop_down();
        open_drop_down();
    }
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

PropertyEnumValue FlagsValueEditor::read_view(const decltype(value)&) const {
    return value_;
}

void FlagsValueEditor::write_view(const decltype(value)&, PropertyEnumValue incoming) {
    require_mutable();
    PropertyDescriptor property;
    property.name = "value";
    property.kind = BindingValueKind::enumeration;
    property.enumeration =
        std::make_shared<const PropertyEnumDescriptor>(descriptor_);
    const std::optional<BindingValue> normalized = convert_property_value(BindingValue{std::move(incoming)},
                                                    property);
    if (!normalized) {
        throw std::invalid_argument(
            "FlagsValueEditor value does not belong to its descriptor");
    }
    const PropertyEnumValue next = std::get<PropertyEnumValue>(*normalized);
    if (value_ == next) return;
    value_ = next;
    synchronize_overlay();
    invalidate(Dirty::paint | Dirty::semantics);
}

bool FlagsValueEditor::read_view(const decltype(dropped_down)&) const {
    return dropped_down_;
}

void FlagsValueEditor::write_view(const decltype(dropped_down)&, bool incoming) {
    require_mutable();
    if (incoming == dropped_down_) return;
    if (incoming) open_drop_down();
    else close_drop_down();
}

void FlagsValueEditor::prepare_write(const decltype(overlay_width)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 80.0 || incoming > 1024.0) {
        throw std::invalid_argument(
            "flags editor overlay width must be within [80, 1024]");
    }
}

// An open list reopens at the new width.
void FlagsValueEditor::written(const decltype(overlay_width)&) {
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
    overlay_choice_indices_.clear();
    std::vector<std::string> items;
    for (std::size_t index = 0U; index < descriptor_.choices.size(); ++index) {
        const std::int64_t choice = descriptor_.choices[index].value;
        if (choice == 0 || (choice > 0 &&
            std::has_single_bit(static_cast<std::uint64_t>(choice)))) {
            overlay_choice_indices_.push_back(index);
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
        StableId(prefix + ".overlay.layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    std::shared_ptr<gui_forms::CheckedListBox> list = make_control<CheckedListBox>(
        StableId(prefix + ".overlay.list"));
    (*list).set_paint_plane(PaintPlane::overlay);
    (*list).items = std::move(items);
    (*list).check_on_click = true;
    const double list_width = std::min(overlay_width.read(), client.width);
    const double overlay_x = std::clamp(
        editor.x, 0.0, std::max(0.0, client.width - list_width));
    (*list).set_requested_bounds({overlay_x, y, list_width, height});
    OverlayToken token = (*window()).open_overlay(shared_from_this(), layer);
    const std::optional<Rect> face =
        drop_down_popup_face(*window(), editor, Size{overlay_width.read(), height}, editor.x);
    const bool lifted = face.has_value() && lift_.lift(*window(), list, *face);
    if (lifted) (*layer).focusable = true;
    else (*layer).add_child(list);

    overlay_layer_ = layer;
    overlay_list_ = list;
    overlay_token_ = std::move(token);
    const std::weak_ptr<FlagsValueEditor> weak =
        std::static_pointer_cast<FlagsValueEditor>(shared_from_this());
    overlay_check_ = (*list).item_check_state_changed().subscribe(
        *this,
        detail::WeakMemberCallback<
            void (FlagsValueEditor::*)(std::size_t, CheckState)>(
                weak, &FlagsValueEditor::apply_overlay_choice));
    overlay_dismissal_ = (*layer).dismissed().subscribe(
        *this,
        detail::WeakMemberCallback<void (FlagsValueEditor::*)()>(
            weak, &FlagsValueEditor::close_drop_down));
    if (Event<>* closed = overlay_token_.closed_event()) {
        overlay_revocation_ = (*closed).subscribe(
            *this,
            detail::WeakMemberCallback<void (FlagsValueEditor::*)()>(
                weak, &FlagsValueEditor::on_overlay_revoked));
    }
    if (lifted) {
        lift_closed_ = (*window()).popup_window_closed().subscribe(
            *this, detail::WeakMemberCallback<void (FlagsValueEditor::*)(std::size_t)>(
                weak, &FlagsValueEditor::on_popup_window_closed));
    }
    const Control::Ptr preferred_focus = lifted ? Control::Ptr(layer) : Control::Ptr(list);
    overlay_scope_ = (*window()).begin_focus_scope(layer, preferred_focus).value;
    dropped_down_ = true;
    synchronize_overlay();
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(drop_down_changed_, true);
}

void FlagsValueEditor::close_drop_down() {
    if (!dropped_down_ && !overlay_layer_) return;
    closing_overlay_ = true;
    overlay_check_.disconnect();
    overlay_dismissal_.disconnect();
    lift_closed_.disconnect();
    lift_.release();
    if (window() != nullptr && overlay_scope_ != 0U) {
        static_cast<void>((*window()).end_focus_scope(FocusScopeId{overlay_scope_}));
    }
    overlay_scope_ = 0U;
    overlay_token_.disconnect();
    overlay_revocation_.disconnect();
    overlay_list_.reset();
    overlay_layer_.reset();
    overlay_choice_indices_.clear();
    const bool changed = dropped_down_;
    dropped_down_ = false;
    closing_overlay_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(drop_down_changed_, false);
}

void FlagsValueEditor::on_popup_window_closed(const std::size_t slot) {
    if (slot != Window::picker_popup_window_slot || !lift_.lifted()) return;
    close_drop_down();
}

void FlagsValueEditor::on_overlay_revoked() {
    if (closing_overlay_) return;
    overlay_check_.disconnect();
    overlay_dismissal_.disconnect();
    overlay_revocation_.disconnect();
    lift_closed_.disconnect();
    lift_.release();
    if (window() != nullptr && overlay_scope_ != 0U) {
        static_cast<void>((*window()).end_focus_scope(
            FocusScopeId{overlay_scope_},
            FocusScopeCloseReason::owner_unavailable));
    }
    overlay_scope_ = 0U;
    overlay_list_.reset();
    overlay_layer_.reset();
    overlay_choice_indices_.clear();
    const bool changed = dropped_down_;
    dropped_down_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(drop_down_changed_, false);
}

void FlagsValueEditor::synchronize_overlay() {
    if (!overlay_list_) return;
    synchronizing_overlay_ = true;
    const std::uint64_t bits = static_cast<std::uint64_t>(value_.value);
    for (std::size_t overlay_index = 0U;
         overlay_index < overlay_choice_indices_.size(); ++overlay_index) {
        const std::int64_t choice =
            descriptor_.choices[overlay_choice_indices_[overlay_index]].value;
        const bool checked = choice == 0
            ? value_.value == 0
            : (bits & static_cast<std::uint64_t>(choice)) ==
                  static_cast<std::uint64_t>(choice);
        (*overlay_list_).set_item_checked(overlay_index, checked);
    }
    synchronizing_overlay_ = false;
}

void FlagsValueEditor::apply_overlay_choice(std::size_t overlay_index,
                                           CheckState state) {
    if (synchronizing_overlay_ || state == CheckState::indeterminate ||
        overlay_index >= overlay_choice_indices_.size()) return;
    const PropertyEnumChoice& choice =
        descriptor_.choices[overlay_choice_indices_[overlay_index]];
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
        synchronize_overlay();
        return;
    }
    const PropertyEnumValue next = std::get<PropertyEnumValue>(*normalized);
    if (next == value_) {
        synchronize_overlay();
        return;
    }
    value_ = next;
    synchronize_overlay();
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
        dropped_down = !dropped_down_;
        event.handled = true;
    } else if (event.action == PointerAction::up &&
               event.button == PointerButton::primary) {
        event.handled = true;
    }
}

void FlagsValueEditor::on_key(KeyEvent& event) {
    if (!focused_ || !enabled.read() || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::escape && dropped_down_) {
        close_drop_down();
        event.handled = true;
    } else if (event.physical_key == PhysicalKey::f4 ||
               event.physical_key == PhysicalKey::space ||
               (event.physical_key == PhysicalKey::down &&
                has_modifier(event.modifiers, Modifier::alt))) {
        dropped_down = !dropped_down_;
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
    result.name = accessible_name.read();
    result.value = display_label(value_.name);
    result.description = accessible_description.read();
    if (dropped_down_) result.states |= SemanticState::expanded;
    result.actions = {SemanticAction::focus,
        dropped_down_ ? SemanticAction::collapse : SemanticAction::expand};
    result.exposed = true;
    return result;
}

bool FlagsValueEditor::on_semantic_action(SemanticAction action,
                                           std::string_view action_value) {
    if (action == SemanticAction::expand) {
        dropped_down = true;
        return true;
    }
    if (action == SemanticAction::collapse) {
        dropped_down = false;
        return true;
    }
    return Panel::on_semantic_action(action, action_value);
}

void FlagsValueEditor::on_detached_from_window() noexcept {
    overlay_check_.disconnect();
    lift_closed_.disconnect();
    lift_.release();
    overlay_dismissal_.disconnect();
    overlay_revocation_.disconnect();
    overlay_scope_ = 0U;
    overlay_token_.disconnect();
    overlay_list_.reset();
    overlay_layer_.reset();
    overlay_choice_indices_.clear();
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

namespace {

// The form properties (stage FD6.3), over descriptor and value: hand table
// entries (flags_value_editor.json extra_entries). None raises a change
// event of its own; value_changed reports user commits only.
PropertyDescriptor flags_value_editor_enumeration_name_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "enumeration_name";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Data";
    descriptor.description = "The flags enumeration's name, which the value carries.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_flags_value_editor_enumeration_name() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        flags_value_editor_enumeration_name_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor flags_value_editor_flag_names_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "flag_names";
    descriptor.kind = BindingValueKind::collection;
    descriptor.category = "Data";
    descriptor.description = "The flags' names in order, each the next bit: 1, 2, 4 and so on.";
    descriptor.default_value = text_collection({});
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.serialization_visibility = PropertySerializationVisibility::content;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_flags_value_editor_flag_names() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        flags_value_editor_flag_names_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor flags_value_editor_none_name_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "none_name";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Data";
    descriptor.description = "The name of the value with no flag set; empty when it has none.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_flags_value_editor_none_name() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        flags_value_editor_none_name_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor flags_value_editor_checked_flags_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "checked_flags";
    descriptor.kind = BindingValueKind::collection;
    descriptor.category = "Data";
    descriptor.description = "The names of the flags the value sets, among flag_names.";
    descriptor.default_value = text_collection({});
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.serialization_visibility = PropertySerializationVisibility::content;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_flags_value_editor_checked_flags() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        flags_value_editor_checked_flags_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

// property_codemod begin FlagsValueEditor (tools/property_families/flags_value_editor.json; generated, do not edit)

const std::array<PropertyEntry, FlagsValueEditor::flags_value_editor_property_count>& FlagsValueEditor::flags_value_editor_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"enumeration_name", &describe_flags_value_editor_enumeration_name,
         &read_member_entry<FlagsValueEditor, decltype(&FlagsValueEditor::enumeration_name_property), &FlagsValueEditor::enumeration_name_property>,
         &write_member_entry<FlagsValueEditor, decltype(&FlagsValueEditor::set_enumeration_name_property), &FlagsValueEditor::set_enumeration_name_property>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"flag_names", &describe_flags_value_editor_flag_names,
         &read_member_entry<FlagsValueEditor, decltype(&FlagsValueEditor::flag_names_property), &FlagsValueEditor::flag_names_property>,
         &write_member_entry<FlagsValueEditor, decltype(&FlagsValueEditor::set_flag_names_property), &FlagsValueEditor::set_flag_names_property>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"none_name", &describe_flags_value_editor_none_name,
         &read_member_entry<FlagsValueEditor, decltype(&FlagsValueEditor::none_name_property), &FlagsValueEditor::none_name_property>,
         &write_member_entry<FlagsValueEditor, decltype(&FlagsValueEditor::set_none_name_property), &FlagsValueEditor::set_none_name_property>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"checked_flags", &describe_flags_value_editor_checked_flags,
         &read_member_entry<FlagsValueEditor, decltype(&FlagsValueEditor::checked_flags_property), &FlagsValueEditor::checked_flags_property>,
         &write_member_entry<FlagsValueEditor, decltype(&FlagsValueEditor::set_checked_flags_property), &FlagsValueEditor::set_checked_flags_property>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "FlagsValueEditor's property table names a property twice");
    static const std::array<PropertyEntry, flags_value_editor_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> FlagsValueEditor::property_table() const noexcept {
    const std::array<PropertyEntry, flags_value_editor_property_count>& entries = flags_value_editor_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end FlagsValueEditor

} // namespace gui_forms
