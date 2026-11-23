#include "gui_forms/controls/checked_combo_box.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/controls/anchored_overlay_layer.hpp"
#include "gui_forms/controls/checked_list_box.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <stxlib/result.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

struct CheckedComboBox::State final {
    std::shared_ptr<AnchoredOverlayLayer> layer_{};
    std::shared_ptr<CheckedListBox> list_{};
    // Paint scratch.
    std::string paint_text_{};
};

stxlib::Unique<CheckedComboBox::State> CheckedComboBox::make_state() {
    stxlib::OperationResult<stxlib::Unique<State>> made = stxlib::Unique<State>::make();
    if (made.failed()) throw std::bad_alloc();
    stxlib::Unique<State> state = std::move(made).take_value();
    return state;
}

std::shared_ptr<CheckedListBox> CheckedComboBox::drop_down_list() const noexcept {
    return (*state_).list_;
}

namespace {

// The field's metrics, in logical units: the drop-down button's width, its
// arrow's side, the text's inset and the natural size.
constexpr double button_width = 22.0;
constexpr double disclosure_side = 10.0;
constexpr double text_inset = 6.0;
constexpr double natural_width = 200.0;
constexpr double natural_height = 26.0;
// The list's frame, added to its rows' height.
constexpr double list_frame = 4.0;

constexpr std::array<PartField<CheckedComboBox::Parts>, 6> checked_combo_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &CheckedComboBox::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}, .states = {}},
     .member = &CheckedComboBox::Parts::well},
    {.declaration = {.name = "label", .role = PartRole::content, .composed_from = {}, .states = {}},
     .member = &CheckedComboBox::Parts::label},
    {.declaration = {.name = "actuator", .role = PartRole::actuator, .composed_from = {}, .states = {}},
     .member = &CheckedComboBox::Parts::actuator},
    {.declaration = {.name = "glyph", .role = PartRole::disclosure, .composed_from = {}, .states = {}},
     .member = &CheckedComboBox::Parts::glyph},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &CheckedComboBox::Parts::focus},
}};

constexpr std::array<PartDeclaration, 6> checked_combo_parts = part_declarations(checked_combo_part_fields);

constexpr std::array<AnatomyState, 9> checked_combo_states{
    AnatomyState::normal,  AnatomyState::hot,     AnatomyState::enabled,
    AnatomyState::disabled, AnatomyState::focused, AnatomyState::focus_visible,
    AnatomyState::open,    AnatomyState::closed,  AnatomyState::pressed,
};

constexpr ControlAnatomy checked_combo_anatomy{
    .control_id = "checked_combo_box",
    .parts = checked_combo_parts,
    .states = checked_combo_states,
    .custom_states = {},
    .item_systems = {},
};

// The events CheckedComboBox adds to those of Control, in declaration
// order.
constexpr std::array<InteractionDeclaration, 2> checked_combo_interactions{{
    {.name = "item_check_changed", .payload = "CheckedComboItemChange", .mutable_payload = false},
    {.name = "drop_down_changed", .payload = "bool", .mutable_payload = false},
}};

void validate_items(const std::vector<CheckedComboItem>& items) {
    if (items.size() > CheckedComboBox::maximum_items) {
        throw std::invalid_argument("CheckedComboBox takes at most maximum_items items");
    }
    std::unordered_set<std::string_view> seen;
    for (const CheckedComboItem& item : items) {
        if (!valid_stable_id(item.stable_id)) throw std::invalid_argument("CheckedComboBox item ID is not valid");
        if (!seen.insert(item.stable_id).second) throw std::invalid_argument("CheckedComboBox item ID repeats");
        if (!validate_utf8(item.text).valid()) throw std::invalid_argument("CheckedComboBox item text is not UTF-8");
    }
}

} // namespace

CheckedComboBox::CheckedComboBox(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    cursor = stxlib::Optional<CursorKind>::of(CursorKind::arrow);
}

CheckedComboBox::~CheckedComboBox() {
    release_overlay();
}

// -- items ------------------------------------------------------------------------------------------

void CheckedComboBox::set_items(std::vector<CheckedComboItem> items) {
    require_mutable();
    validate_items(items);
    const Control::Ptr retained = shared_from_this();
    static_cast<void>(close_drop_down());
    if (!is_alive()) return;
    items_ = std::move(items);
    invalidate(Dirty::paint | Dirty::semantics);
}

stxlib::Optional<std::size_t> CheckedComboBox::index_of(std::string_view item_id) const {
    for (std::size_t item = 0; item < items_.size(); ++item) {
        if (items_[item].stable_id == item_id) return stxlib::Optional<std::size_t>::of(item);
    }
    return {};
}

bool CheckedComboBox::item_checked(std::string_view item_id) const {
    const stxlib::Optional<std::size_t> item = index_of(item_id);
    if (item.absent()) throw std::out_of_range("CheckedComboBox has no item with this ID");
    return items_[*item.value()].checked;
}

void CheckedComboBox::set_item_checked(std::string_view item_id, bool checked) {
    require_mutable();
    const stxlib::Optional<std::size_t> item = index_of(item_id);
    if (item.absent()) throw std::out_of_range("CheckedComboBox has no item with this ID");
    apply_check(*item.value(), checked);
}

void CheckedComboBox::set_all_checked(bool checked) {
    require_mutable();
    const Control::Ptr retained = shared_from_this();
    // A handler may replace the items: the walk stops at the end of what
    // is there.
    for (std::size_t item = 0; item < items_.size(); ++item) {
        apply_check(item, checked);
        if (!is_alive()) return;
    }
}

std::vector<std::string> CheckedComboBox::checked_ids() const {
    std::vector<std::string> ids;
    for (const CheckedComboItem& item : items_) {
        if (item.checked) ids.push_back(item.stable_id);
    }
    return ids;
}

std::string CheckedComboBox::text() const {
    std::string joined;
    bool first = true;
    for (const CheckedComboItem& item : items_) {
        if (!item.checked) continue;
        if (!first) joined += separator.read();
        joined += item.text;
        first = false;
    }
    return joined;
}

// Stores the check, mirrors it into an open list and raises the change.
void CheckedComboBox::apply_check(std::size_t item, bool checked) {
    State& state = *state_;
    if (items_[item].checked == checked) return;
    items_[item].checked = checked;
    if (state.list_ && item < (*state.list_).items.read().size()) {
        syncing_ = true;
        (*state.list_).set_item_checked(item, checked);
        syncing_ = false;
    }
    invalidate(Dirty::paint | Dirty::semantics);
    const CheckedComboItemChange change{items_[item].stable_id, checked};
    publish_change(item_check_changed_, change);
}

void CheckedComboBox::on_list_check(std::size_t index, CheckState state) {
    if (syncing_ || index >= items_.size()) return;
    apply_check(index, state == CheckState::checked);
}

// -- drop-down --------------------------------------------------------------------------------------

// The list is the field's width (at least) and tall enough for
// max_drop_down_items rows, and a check list of the items in order.
bool CheckedComboBox::show_drop_down() {
    State& state = *state_;
    require_mutable();
    Window* const host = attached_window();
    if (dropped_down_ || host == nullptr || items_.empty() || !effectively_enabled()) return false;
    const Control::Ptr retained = shared_from_this();
    const std::string prefix(stable_id().value());
    state.list_ = make_control<CheckedListBox>(StableId(prefix + ".list"));
    (*state.list_).check_on_click = true;
    for (const CheckedComboItem& item : items_) {
        (*state.list_).add_item(item.text, item.checked ? CheckState::checked : CheckState::unchecked);
    }
    (*state.list_).select_index(0U);
    const std::size_t rows = std::min<std::size_t>(items_.size(), max_drop_down_items.read());
    const double row = (*state.list_).item_height.read() * effective_text_scale();
    AnchoredOverlayPlacement placement{};
    placement.preferred_size = Size{absolute_bounds().width, row * static_cast<double>(rows) + list_frame};
    placement.gap = 1.0;
    state.layer_ = make_control<AnchoredOverlayLayer>(StableId(prefix + ".layer"), retained, placement);
    (*state.layer_).set_content(state.list_);
    overlay_ = (*host).open_overlay(retained, state.layer_);
    list_check_ = (*state.list_).item_check_state_changed().subscribe(
        *this, Delegate<std::size_t, CheckState>::bind<CheckedComboBox, &CheckedComboBox::on_list_check>(*this));
    dismissal_ = (*state.layer_).dismiss_requested().subscribe(
        *this, Delegate<OverlayDismissReason>::bind<CheckedComboBox, &CheckedComboBox::on_dismiss>(*this));
    if (Event<>* revoked = overlay_.closed_event()) {
        overlay_revoked_ = (*revoked).subscribe(
            *this, Delegate<>::bind<CheckedComboBox, &CheckedComboBox::on_overlay_revoked>(*this));
    }
    dropped_down_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    static_cast<void>((*host).request_focus(state.list_));
    if (!is_alive()) return true;
    publish_change(drop_down_changed_, true);
    return true;
}

bool CheckedComboBox::close_drop_down() {
    if (!dropped_down_) return false;
    const Control::Ptr retained = shared_from_this();
    const bool return_focus = focus_within_list();
    dropped_down_ = false;
    release_overlay();
    invalidate(Dirty::paint | Dirty::semantics);
    Window* const host = attached_window();
    if (return_focus && host != nullptr) static_cast<void>((*host).request_focus(retained));
    if (!is_alive()) return true;
    publish_change(drop_down_changed_, false);
    return true;
}

bool CheckedComboBox::focus_within_list() const {
    const Window* const host = window();
    if (host == nullptr || !(*state_).layer_) return false;
    for (Control::Ptr at = (*host).focused_control(); at; at = (*at).parent()) {
        if (at.get() == (*state_).layer_.get()) return true;
    }
    return false;
}

void CheckedComboBox::release_overlay() noexcept {
    State& state = *state_;
    list_check_.disconnect();
    dismissal_.disconnect();
    overlay_revoked_.disconnect();
    if (state.layer_) (*state.layer_).clear_content();
    overlay_.disconnect();
    state.list_.reset();
    state.layer_.reset();
}

void CheckedComboBox::on_dismiss(OverlayDismissReason) {
    static_cast<void>(close_drop_down());
}

// The window closed the overlay itself (as when it closes).
void CheckedComboBox::on_overlay_revoked() {
    State& state = *state_;
    if (!dropped_down_) return;
    dropped_down_ = false;
    list_check_.disconnect();
    dismissal_.disconnect();
    overlay_revoked_.disconnect();
    if (state.layer_) (*state.layer_).clear_content();
    state.list_.reset();
    state.layer_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(drop_down_changed_, false);
}

void CheckedComboBox::on_detached_from_window() noexcept {
    dropped_down_ = false;
    release_overlay();
    hot_ = false;
    focused_ = false;
}

// -- layout and painting ------------------------------------------------------------------------------

FontSpec CheckedComboBox::field_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.field);
    return font;
}

// The drop-down button sits at the trailing end (the left, right to left).
CheckedComboBox::Placement CheckedComboBox::placement() const {
    Placement placed;
    const Rect bounds = client_rectangle();
    const double button = std::min(button_width, bounds.width);
    placed.well = bounds;
    placed.actuator = Rect{std::max(0.0, bounds.width - button), 1.0, std::max(0.0, button - 1.0),
                           std::max(0.0, bounds.height - 2.0)};
    placed.glyph = Rect{std::floor(placed.actuator.x + (placed.actuator.width - disclosure_side) * 0.5),
                        std::floor((bounds.height - disclosure_side) * 0.5), disclosure_side, disclosure_side};
    placed.label = Rect{text_inset, 0.0, std::max(0.0, bounds.width - button - text_inset - 3.0), bounds.height};
    if (effective_layout_direction() == LayoutDirection::right_to_left) {
        placed.actuator.x = bounds.width - placed.actuator.x - placed.actuator.width;
        placed.glyph.x = bounds.width - placed.glyph.x - placed.glyph.width;
        placed.label.x = bounds.width - placed.label.x - placed.label.width;
    }
    return placed;
}

Size CheckedComboBox::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : natural_width;
    const double height = requested.height > 0.0 ? requested.height : natural_height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

// The text is the checked items' texts, or the placeholder in the muted
// tone while none is checked.
void CheckedComboBox::on_paint(Painter& painter, Rect) {
    State& state = *state_;
    const Placement placed = placement();
    const AnatomyStateSet states = anatomy_states();
    PartPaint well{.role = PartRole::well, .bounds = placed.well, .states = states,
                   .family = ControlVisualRole::editor};
    paint_part(painter, *this, well);
    PartPaint drop_down{.role = PartRole::actuator, .bounds = placed.actuator, .states = states,
                        .family = ControlVisualRole::choice};
    paint_part(painter, *this, drop_down);
    PartPaint disclosure{.role = PartRole::disclosure, .bounds = placed.glyph, .states = states,
                         .family = ControlVisualRole::choice};
    disclosure.mark = dropped_down_ ? PartMark::arrow_up : PartMark::arrow_down;
    paint_part(painter, *this, disclosure);
    state.paint_text_ = text();
    PartPaint content{.role = PartRole::content, .bounds = placed.label, .states = states,
                      .family = ControlVisualRole::editor};
    if (state.paint_text_.empty()) {
        state.paint_text_ = placeholder_text.read();
        content.tone = PartTextTone::secondary;
    }
    content.text = lend_paint_text(state.paint_text_);
    content.font = field_font();
    paint_part(painter, *this, content);
    PartPaint ring{.role = PartRole::focus, .bounds = placed.well, .states = states,
                   .family = ControlVisualRole::editor};
    paint_part(painter, *this, ring);
}

// -- input ------------------------------------------------------------------------------------------

void CheckedComboBox::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

void CheckedComboBox::on_mouse_move(MouseEventArgs& arguments) {
    if (eligible_for_input() && !hot_) {
        hot_ = true;
        invalidate(Dirty::paint);
    }
    Control::on_mouse_move(arguments);
}

void CheckedComboBox::on_mouse_leave() {
    if (hot_) {
        hot_ = false;
        invalidate(Dirty::paint);
    }
    Control::on_mouse_leave();
}

// A press on the field opens the list, or closes an open one.
void CheckedComboBox::on_mouse_down(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.button == PointerButton::primary) {
        arguments.handled = true;
        const Control::Ptr retained = shared_from_this();
        Window* const host = attached_window();
        if (host != nullptr && !dropped_down_) static_cast<void>((*host).request_focus(retained));
        if (!is_alive()) return;
        if (dropped_down_) {
            static_cast<void>(close_drop_down());
        } else {
            static_cast<void>(show_drop_down());
        }
        if (!is_alive()) return;
    }
    Control::on_mouse_down(arguments);
}

// F4, Alt+Down and Alt+Up open the list, or close an open one.
void CheckedComboBox::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled || !eligible_for_input()) return;
    const std::uint32_t key = arguments.physical_key;
    const bool alt = arguments.modifiers == Modifier::alt;
    const bool plain = arguments.modifiers == Modifier::none;
    if (!(plain && key == PhysicalKey::f4) && !(alt && (key == PhysicalKey::down || key == PhysicalKey::up))) {
        return;
    }
    arguments.handled = true;
    if (dropped_down_) {
        static_cast<void>(close_drop_down());
        return;
    }
    static_cast<void>(show_drop_down());
}

// -- semantics --------------------------------------------------------------------------------------

SemanticDescriptor CheckedComboBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::combo_box;
    descriptor.name = accessible_name.read();
    descriptor.description =
        accessible_description.read().empty() ? placeholder_text.read() : accessible_description.read();
    descriptor.value = text();
    descriptor.has_popup = true;
    if ((*state_).list_) descriptor.controlled_panel = std::string((*(*state_).list_).stable_id().value());
    if (dropped_down_) descriptor.states |= SemanticState::expanded;
    descriptor.states |= SemanticState::read_only;
    descriptor.actions = {SemanticAction::focus, dropped_down_ ? SemanticAction::collapse : SemanticAction::expand};
    descriptor.exposed = true;
    return descriptor;
}

bool CheckedComboBox::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::expand && !dropped_down_) {
        const bool opened = show_drop_down();
        return opened;
    }
    if (action == SemanticAction::collapse && dropped_down_) {
        const bool closed = close_drop_down();
        return closed;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

// -- anatomy ----------------------------------------------------------------------------------------

const ControlAnatomy& CheckedComboBox::anatomy() const noexcept {
    return checked_combo_anatomy;
}

AnatomyStateSet CheckedComboBox::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (hot_) states.insert(AnatomyState::hot);
    states.insert(dropped_down_ ? AnatomyState::open : AnatomyState::closed);
    if (dropped_down_) states.insert(AnatomyState::pressed);
    if (focused_) states.insert(AnatomyState::focused);
    const Window* const host = window();
    if (focused_ && host != nullptr && (*host).focus_cue_visible()) states.insert(AnatomyState::focus_visible);
    return states;
}

void CheckedComboBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Placement placed = placement();
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.well = PartGeometry::from_bounds(placed.well);
    record.label = PartGeometry::from_bounds(placed.label);
    record.actuator = PartGeometry::from_bounds(placed.actuator);
    record.glyph = PartGeometry::from_bounds(placed.glyph);
    record.focus = PartGeometry::from_bounds(placed.well);
    write_parts(anatomy(), checked_combo_part_fields, record, geometry);
}

CheckedComboBox::Parts CheckedComboBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), checked_combo_part_fields, published.parts(), record);
    return record;
}

void CheckedComboBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), checked_combo_interactions.begin(), checked_combo_interactions.end());
}

// property_codemod begin CheckedComboBox (tools/property_families/checked_combo_box.json; generated, do not edit)

namespace {

PropertyDescriptor checked_combo_box_separator_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "separator";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "What the field's text puts between the checked items.";
    descriptor.default_value = BindingValue{std::string(", ")};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_checked_combo_box_separator() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        checked_combo_box_separator_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor checked_combo_box_placeholder_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "placeholder_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The hint the field shows while nothing is checked.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_checked_combo_box_placeholder_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        checked_combo_box_placeholder_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor checked_combo_box_max_drop_down_items_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "max_drop_down_items";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Behavior";
    descriptor.description = "The items the open list shows before it scrolls.";
    descriptor.default_value = BindingValue{std::uint64_t{8U}};
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_checked_combo_box_max_drop_down_items() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        checked_combo_box_max_drop_down_items_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, CheckedComboBox::checked_combo_box_property_count>& CheckedComboBox::checked_combo_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"separator", &describe_checked_combo_box_separator,
         &read_cell_entry<CheckedComboBox, decltype(&CheckedComboBox::separator), &CheckedComboBox::separator>,
         &write_cell_entry<CheckedComboBox, decltype(&CheckedComboBox::separator), &CheckedComboBox::separator>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"placeholder_text", &describe_checked_combo_box_placeholder_text,
         &read_cell_entry<CheckedComboBox, decltype(&CheckedComboBox::placeholder_text), &CheckedComboBox::placeholder_text>,
         &write_cell_entry<CheckedComboBox, decltype(&CheckedComboBox::placeholder_text), &CheckedComboBox::placeholder_text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"max_drop_down_items", &describe_checked_combo_box_max_drop_down_items,
         &read_cell_entry<CheckedComboBox, decltype(&CheckedComboBox::max_drop_down_items), &CheckedComboBox::max_drop_down_items>,
         &write_cell_entry<CheckedComboBox, decltype(&CheckedComboBox::max_drop_down_items), &CheckedComboBox::max_drop_down_items>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "CheckedComboBox's property table names a property twice");
    static const std::array<PropertyEntry, checked_combo_box_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> CheckedComboBox::property_table() const noexcept {
    const std::array<PropertyEntry, checked_combo_box_property_count>& entries = checked_combo_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void CheckedComboBox::prepare_write(const decltype(separator)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("CheckedComboBox separator must be valid UTF-8");
}

void CheckedComboBox::written(const decltype(separator)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void CheckedComboBox::prepare_write(const decltype(placeholder_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("CheckedComboBox placeholder must be valid UTF-8");
}

void CheckedComboBox::written(const decltype(placeholder_text)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void CheckedComboBox::prepare_write(const decltype(max_drop_down_items)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming == 0U || incoming > 64U) throw std::invalid_argument("CheckedComboBox drop-down items must be from 1 to 64");
}

void CheckedComboBox::written(const decltype(max_drop_down_items)&) {
    invalidate(Dirty::semantics);
}

// property_codemod end CheckedComboBox

} // namespace gui_forms
