#include "gui_forms/controls/checked_list_box.hpp"
#include "../list_box_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include "gui_forms/detail/algorithm/binary_search.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

CheckedListBox::CheckedListBox(StableId stable_id)
    : ListBox(std::move(stable_id)) {}

void CheckedListBox::add_item(std::string item, CheckState state) {
    if (state != CheckState::unchecked && state != CheckState::checked &&
        state != CheckState::indeterminate) {
        throw std::invalid_argument("invalid CheckedListBox item state");
    }
    items.append(std::move(item));
    // The insertion gave the new item an unchecked state; its row repaints
    // at the flush either way.
    check_states_.back() = state;
}

// Reserves the check states an assignment or insertion needs, so
// on_items_changed cannot fail.
void CheckedListBox::on_items_changing(const CollectionEdit& edit) {
    switch (edit.kind()) {
    case CollectionEditKind::assign:
        check_states_.reserve(edit.count());
        break;
    case CollectionEditKind::insert:
        check_states_.reserve(check_states_.size() + edit.count());
        break;
    case CollectionEditKind::erase:
    case CollectionEditKind::replace:
    default:
        // Erasing and replacing need no new capacity.
        break;
    }
}

// New items are unchecked; a replaced item keeps its state.
void CheckedListBox::on_items_changed(const CollectionEdit& edit) noexcept {
    switch (edit.kind()) {
    case CollectionEditKind::assign:
        check_states_.assign(edit.count(), CheckState::unchecked);
        break;
    case CollectionEditKind::insert:
        check_states_.insert(check_states_.begin() + static_cast<std::ptrdiff_t>(edit.index()),
                             edit.count(), CheckState::unchecked);
        break;
    case CollectionEditKind::erase: {
        const std::vector<CheckState>::iterator first =
            check_states_.begin() + static_cast<std::ptrdiff_t>(edit.index());
        check_states_.erase(first, first + static_cast<std::ptrdiff_t>(edit.count()));
        break;
    }
    case CollectionEditKind::replace:
    default:
        // A replaced item keeps its state.
        break;
    }
}

CheckState CheckedListBox::item_check_state(std::size_t index) const {
    if (index >= check_states_.size()) {
        throw std::out_of_range("CheckedListBox item index");
    }
    return check_states_[index];
}

bool CheckedListBox::item_checked(std::size_t index) const {
    return item_check_state(index) == CheckState::checked;
}

void CheckedListBox::set_item_check_state(std::size_t index,
                                          CheckState state) {
    require_mutable();
    if (index >= check_states_.size()) {
        throw std::out_of_range("CheckedListBox item index");
    }
    if (state != CheckState::unchecked && state != CheckState::checked &&
        state != CheckState::indeterminate) {
        throw std::invalid_argument("invalid CheckedListBox item state");
    }
    if (check_states_[index] == state) return;
    ItemCheckEvent change{index, check_states_[index], state, false};
    item_checking_.emit(change);
    if (!is_alive() || change.cancel) return;
    if (change.new_state != CheckState::unchecked &&
        change.new_state != CheckState::checked &&
        change.new_state != CheckState::indeterminate) {
        throw std::invalid_argument(
            "CheckedListBox ItemCheck callback produced an invalid state");
    }
    if (check_states_[index] == change.new_state) return;
    check_states_[index] = change.new_state;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(item_check_state_changed_, index, check_states_[index]);
}

void CheckedListBox::set_item_checked(std::size_t index, bool checked) {
    set_item_check_state(index, checked ? CheckState::checked
                                        : CheckState::unchecked);
}

void CheckedListBox::toggle_item(std::size_t index) {
    const CheckState current = item_check_state(index);
    set_item_check_state(index, current == CheckState::checked
                                    ? CheckState::unchecked
                                    : CheckState::checked);
}

std::vector<std::size_t> CheckedListBox::checked_indices() const {
    std::vector<std::size_t> result;
    for (std::size_t index = 0U; index < check_states_.size(); ++index) {
        if (check_states_[index] == CheckState::checked) result.push_back(index);
    }
    return result;
}

void CheckedListBox::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) {
        ListBox::on_pointer(event);
        return;
    }
    const std::optional<std::size_t> index = event.action == PointerAction::down &&
                           event.button == PointerButton::primary
        ? index_at(event.position) : std::optional<std::size_t>{};
    const bool was_selected = index && detail::binary_search_contains(
        selected_indices(), *index);
    ListBox::on_pointer(event);
    if (!is_alive() || !index || *index >= items.read().size()) return;
    if (check_on_click.read() || was_selected) {
        toggle_item(*index);
        event.handled = true;
    }
}

void CheckedListBox::on_key(KeyEvent& event) {
    if (event.action == KeyAction::down &&
        event.physical_key == PhysicalKey::space && focused_for_extension() &&
        enabled.read()) {
        if (const std::optional<std::size_t> index = active_index_for_extension()) {
            toggle_item(*index);
            event.handled = true;
        }
        return;
    }
    ListBox::on_key(event);
}

double CheckedListBox::row_text_left() const noexcept {
    return indicator_size.read() + 14.0;
}

void CheckedListBox::paint_row_adornment(Painter& painter, std::size_t index,
                                          Rect row, bool selected,
                                          bool focused) const {
    if (index >= check_states_.size()) return;
    const double size = std::min(indicator_size.read(),
                                 std::max(8.0, row.height - 8.0));
    // At the row's right edge right to left (WinForms' CheckedListBox).
    const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    const double x = mirrored ? row.x + row.width - 5.0 - size : row.x + 5.0;
    const double y = row.y + (row.height - size) * 0.5;
    // The row's check box: a choice well and its mark, as parts the theme's
    // engine draws; a style override supplies the well's ground and the
    // mark's ink (T4-1).
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    if (selected) states.insert(AnatomyState::selected);
    PartPaint well;
    well.family = ControlVisualRole::choice;
    well.role = PartRole::well;
    well.bounds = {x, y, size, size};
    well.states = states;
    // The theme's choice corners up to 2 px, as a check box's well.
    const ControlVisualContext context = part_visual_context(*this, states);
    const double theme_radius = effective_theme().resolve(ControlVisualRole::choice, context).material.corner_radius;
    well.corner_radius = std::min(2.0, theme_radius);
    if (has_style_override()) well.authored_background = selected && focused ? style().highlight : style().paper;
    paint_part(painter, *this, well);
    const CheckState state = check_states_[index];
    if (state == CheckState::unchecked) return;
    PartPaint mark;
    mark.family = ControlVisualRole::choice;
    mark.role = PartRole::indicator;
    mark.states = states;
    if (has_style_override()) mark.authored_foreground = style().accent;
    if (state == CheckState::checked) {
        mark.mark = PartMark::check;
        mark.states.insert(AnatomyState::checked);
        mark.bounds = {x + 2.0, y + 1.5, std::max(0.0, size - 3.5), std::max(0.0, size - 3.5)};
    } else {
        mark.mark = PartMark::indeterminate;
        mark.states.insert(AnatomyState::indeterminate);
        mark.bounds = {x + 3.0, y + size * 0.5 - 1.5, std::max(0.0, size - 6.0), 3.0};
    }
    paint_part(painter, *this, mark);
}

std::vector<SemanticNode> CheckedListBox::semantic_virtual_children() const {
    // ListBox publishes a window of the rows; each node's position_in_set
    // names its row.
    std::vector<SemanticNode> nodes = ListBox::semantic_virtual_children();
    for (SemanticNode& node : nodes) {
        if (!node.position_in_set || *node.position_in_set == 0U) continue;
        const std::size_t index = *node.position_in_set - 1U;
        if (index >= check_states_.size()) continue;
        node.role = SemanticRole::check_list_item;
        node.states |= SemanticState::checkable;
        if (check_states_[index] == CheckState::checked) {
            node.states |= SemanticState::checked;
            node.value = "checked";
        } else if (check_states_[index] == CheckState::indeterminate) {
            node.states |= SemanticState::mixed;
            node.value = "indeterminate";
        } else {
            node.value = "unchecked";
        }
    }
    return nodes;
}

bool CheckedListBox::on_semantic_child_action(std::string_view stable_id,
                                               SemanticAction action,
                                               std::string_view value) {
    if (action != SemanticAction::press) {
        return ListBox::on_semantic_child_action(stable_id, action, value);
    }
    const std::string prefix = std::string((*this).stable_id().value()) + ".item.";
    if (!stable_id.starts_with(prefix)) return false;
    const std::string_view suffix = stable_id.substr(prefix.size());
    std::size_t index{};
    const std::from_chars_result parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(),
                                        index);
    if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() ||
        index >= items.read().size()) return false;
    if (window() != nullptr) {
        static_cast<void>((*window()).request_focus(shared_from_this()));
    }
    select_index(index, false, false);
    if (is_alive()) toggle_item(index);
    return true;
}

namespace {

constexpr std::array<PartField<CheckedListBox::ItemParts>, 5> checked_item_part_fields{{
    {.declaration = {.name = "row", .role = PartRole::region, .composed_from = {}},
     .member = &CheckedListBox::ItemParts::row},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &CheckedListBox::ItemParts::label},
    {.declaration = {.name = "row_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &CheckedListBox::ItemParts::row_focus},
    {.declaration = {.name = "check_well", .role = PartRole::well, .composed_from = {}},
     .member = &CheckedListBox::ItemParts::check_well},
    {.declaration = {.name = "check", .role = PartRole::indicator, .composed_from = {}},
     .member = &CheckedListBox::ItemParts::check},
}};

constexpr std::array<StateField<CheckedListBox::ItemStates>, 5> checked_item_state_fields{{
    {.state = AnatomyState::selected, .member = &CheckedListBox::ItemStates::selected},
    {.state = AnatomyState::hot, .member = &CheckedListBox::ItemStates::hot},
    {.state = AnatomyState::current, .member = &CheckedListBox::ItemStates::current},
    {.state = AnatomyState::checked, .member = &CheckedListBox::ItemStates::checked},
    {.state = AnatomyState::indeterminate, .member = &CheckedListBox::ItemStates::indeterminate},
}};

constexpr std::array<PartDeclaration, 5> checked_item_parts_declared =
    part_declarations(checked_item_part_fields);
constexpr std::array<AnatomyState, 5> checked_item_states_declared =
    state_declarations(checked_item_state_fields);

constexpr std::array<ItemSystemDeclaration, 1> checked_list_box_item_systems{{
    {.name = "item", .parts = checked_item_parts_declared, .states = checked_item_states_declared,
     .custom_states = {}},
}};

constexpr ControlAnatomy checked_list_box_anatomy{
    .control_id = "checked_list_box",
    .parts = list_box_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = checked_list_box_item_systems,
};

} // namespace

const ControlAnatomy& CheckedListBox::anatomy() const noexcept {
    return checked_list_box_anatomy;
}

void CheckedListBox::publish_realized_item_geometry(std::size_t item,
                                                    std::span<PartGeometry> geometry) const {
    const ListBox::ItemParts base = list_item_parts(item);
    ItemParts record;
    record.row = base.row;
    record.label = base.label;
    record.row_focus = base.row_focus;
    const std::size_t index = realized_item_index(item);
    if (base.row.present && index < check_states_.size()) {
        // The check box paint_row_adornment draws, centred in the row.
        const Rect row = base.row.bounds;
        const double size = std::min(indicator_size.read(), std::max(8.0, row.height - 8.0));
        const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
        const double x = mirrored ? row.x + row.width - 5.0 - size : row.x + 5.0;
        const double y = row.y + (row.height - size) * 0.5;
        record.check_well = PartGeometry::from_bounds({x, y, size, size});
        if (check_states_[index] == CheckState::checked) {
            // Two-unit strokes from (3, 0.52 size) to (size - 2.5, 2.5).
            const Rect mark{x + 2.0, y + 1.5, std::max(0.0, size - 3.5),
                            std::max(0.0, size - 3.5)};
            record.check = PartGeometry::from_bounds(mark);
        } else if (check_states_[index] == CheckState::indeterminate) {
            const Rect bar{x + 3.0, y + size * 0.5 - 1.5, std::max(0.0, size - 6.0), 3.0};
            record.check = PartGeometry::from_bounds(bar);
        } else {
            // An unchecked item draws no mark.
        }
    }
    write_item_parts(anatomy().item_systems.front(), checked_item_part_fields, record, geometry);
}

AnatomyStateSet CheckedListBox::realized_item_anatomy_states(std::size_t item) const {
    const std::size_t index = realized_item_index(item);
    AnatomyStateSet states = list_item_states(index);
    if (index < check_states_.size()) {
        if (check_states_[index] == CheckState::checked) {
            states.insert(AnatomyState::checked);
        } else if (check_states_[index] == CheckState::indeterminate) {
            states.insert(AnatomyState::indeterminate);
        } else {
            // An unchecked item adds no state.
        }
    }
    return states;
}

std::vector<CheckedListBox::Item> CheckedListBox::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = realized_item_index(item);
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), checked_item_part_fields, published.parts(), record.parts);
        read_states(checked_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events CheckedListBox adds to those of ListBox, in declaration order.
constexpr std::array<InteractionDeclaration, 2> checked_list_box_interactions{{
    {.name = "item_checking", .payload = "ItemCheckEvent", .mutable_payload = true},
    {.name = "item_check_state_changed", .payload = "std::size_t, CheckState", .mutable_payload = false},
}};

} // namespace

void CheckedListBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ListBox::append_interactions(interactions);
    interactions.insert(interactions.end(), checked_list_box_interactions.begin(), checked_list_box_interactions.end());
}

// property_codemod begin CheckedListBox (tools/property_families/checked_list_box.json; generated, do not edit)

void CheckedListBox::prepare_write(const decltype(check_on_click)&, bool&) {
    require_mutable();
}

void CheckedListBox::written(const decltype(check_on_click)&) {
    invalidate(Dirty::semantics);
}

void CheckedListBox::prepare_write(const decltype(indicator_size)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 8.0 || incoming > 32.0) {
        throw std::out_of_range("CheckedListBox indicator size must be finite and between 8 and 32");
    }
}

void CheckedListBox::written(const decltype(indicator_size)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

// property_codemod end CheckedListBox

} // namespace gui_forms
