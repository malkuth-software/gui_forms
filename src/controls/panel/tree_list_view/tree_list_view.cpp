#include "gui_forms/controls/panel/tree_list_view/tree_list_view.hpp"

#include "../collection_control_utilities.hpp"
#include "../details_columns.hpp"

#include "gui_forms/controls/range_control/scroll_bar/h_scroll_bar/h_scroll_bar.hpp"
#include "gui_forms/controls/range_control/scroll_bar/v_scroll_bar/v_scroll_bar.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// View geometry in logical units at text scale 1. The scroll bar extent is
// not scaled: it matches ScrollBar's own arrow extent.
constexpr double edge_inset = 2.0;
constexpr double cell_padding = 4.0;
constexpr double expander_gap = 2.0;
constexpr double minimum_row_height = 18.0;
constexpr double row_text_padding = 6.0;
constexpr double cell_text_padding = 6.0;
constexpr double default_icon = 16.0;
constexpr double divider_reach = 4.0;
constexpr double drag_threshold = 5.0;
constexpr double horizontal_key_step = 16.0;
constexpr double scroll_bar_extent = 17.0;
constexpr std::ptrdiff_t wheel_rows = 3;
constexpr std::size_t maximum_image_key_bytes = 256U;

// The "no node" and "no row" value of the structure arrays.
constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();

// Whole rows of `extent` that fit in `available`, at least one.
std::size_t fitting_rows(double available, double extent) noexcept {
    if (extent <= 0.0 || available <= 0.0) return 1U;
    const double rows = std::floor(available / extent);
    const std::size_t result = std::max<std::size_t>(1U, static_cast<std::size_t>(rows));
    return result;
}

// Rows of `extent` at least partly visible in `available`.
std::size_t touched_rows(double available, double extent) noexcept {
    if (extent <= 0.0 || available <= 0.0) return 0U;
    const double rows = std::ceil(available / extent);
    const std::size_t result = static_cast<std::size_t>(rows);
    return result;
}

PartTextAlignment text_alignment(TreeListAlignment alignment) noexcept {
    if (alignment == TreeListAlignment::center) return PartTextAlignment::center;
    if (alignment == TreeListAlignment::right) return PartTextAlignment::end;
    return PartTextAlignment::start;
}

bool valid_alignment(TreeListAlignment alignment) noexcept {
    const bool result = alignment == TreeListAlignment::left || alignment == TreeListAlignment::center ||
                        alignment == TreeListAlignment::right;
    return result;
}

bool valid_node_text(const TreeListNode& node) {
    if (!validate_utf8(node.text).valid() || !validate_utf8(node.image_key).valid()) return false;
    if (node.image_key.size() > maximum_image_key_bytes) return false;
    for (const std::string& cell : node.cells) {
        if (!validate_utf8(cell).valid()) return false;
    }
    return true;
}

// The text a node shows in one column: its text in the tree column, then
// its cells; empty past the last cell.
std::string_view column_text(const TreeListNode& node, std::size_t column) noexcept {
    if (column == 0U) return node.text;
    if (column - 1U < node.cells.size()) return node.cells[column - 1U];
    return {};
}

// The order of one node's children: by one column's text when sorted
// (ascending or descending), then by model index, so equal texts keep the
// order the nodes were given in and the result does not depend on the
// order before the sort.
struct SiblingOrder final {
    const std::vector<TreeListNode>* nodes{};
    std::optional<std::size_t> column{};
    bool descending{};

    bool operator()(std::size_t left, std::size_t right) const noexcept {
        if (column) {
            const std::string_view a = column_text((*nodes)[left], *column);
            const std::string_view b = column_text((*nodes)[right], *column);
            const bool before = descending ? details_detail::folded_text_less(b, a)
                                           : details_detail::folded_text_less(a, b);
            if (before) return true;
            const bool after = descending ? details_detail::folded_text_less(a, b)
                                          : details_detail::folded_text_less(b, a);
            if (after) return false;
        }
        return left < right;
    }
};

ImageVisualState image_state(AnatomyStateSet states) noexcept {
    if (states.contains(AnatomyState::disabled)) return ImageVisualState::disabled;
    if (states.contains(AnatomyState::selected)) return ImageVisualState::selected;
    if (states.contains(AnatomyState::hot)) return ImageVisualState::hot;
    return ImageVisualState::normal;
}

std::string column_identity(std::string_view view_id, std::string_view column_id) {
    std::string identity(view_id);
    identity.append(".column.");
    identity.append(column_id);
    return identity;
}

std::string cell_identity(std::string_view node_id, std::string_view column_id) {
    std::string identity(node_id);
    identity.append(".cell.");
    identity.append(column_id);
    return identity;
}

} // namespace

TreeListView::TreeListView(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    set_paint_plane(PaintPlane::control);
    rebuild_structure();
}

TreeListView::~TreeListView() {}

void TreeListView::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    vertical_bar_ = make_control<VScrollBar>(StableId(prefix + ".vertical-scroll-bar"));
    horizontal_bar_ = make_control<HScrollBar>(StableId(prefix + ".horizontal-scroll-bar"));
    // The bars scroll with the pointer; the keyboard scrolls the view itself.
    (*vertical_bar_).focusable = false;
    (*horizontal_bar_).focusable = false;
    add_child(vertical_bar_);
    add_child(horizontal_bar_);
    vertical_scrolled_ = (*vertical_bar_).value_changed().subscribe(
        *this, Delegate<double>::bind<TreeListView, &TreeListView::on_vertical_scroll>(*this));
    horizontal_scrolled_ = (*horizontal_bar_).value_changed().subscribe(
        *this, Delegate<double>::bind<TreeListView, &TreeListView::on_horizontal_scroll>(*this));
    set_child_layout_collapsed(vertical_bar_, true);
    set_child_layout_collapsed(horizontal_bar_, true);
}

// -- structure -----------------------------------------------------------------

// Parents and children from the preorder depths, in model order; then the
// shown order. The depth sequence was validated, so after popping, the open
// path `ancestors` is exactly as long as the node's depth.
void TreeListView::rebuild_structure() {
    const std::size_t count = nodes_.size();
    parent_.assign(count, no_index);
    child_begin_.assign(count + 2U, 0U);
    children_.assign(count, 0U);
    std::vector<std::size_t> ancestors;
    ancestors.reserve(64U);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t depth = nodes_[index].depth;
        while (ancestors.size() > depth) ancestors.pop_back();
        if (!ancestors.empty()) parent_[index] = ancestors.back();
        ancestors.push_back(index);
        const std::size_t slot = parent_[index] == no_index ? 0U : parent_[index] + 1U;
        ++child_begin_[slot + 1U];
    }
    // Counts to starts: a slot's children begin where the earlier slots'
    // children end.
    for (std::size_t slot = 1; slot < child_begin_.size(); ++slot) {
        child_begin_[slot] += child_begin_[slot - 1U];
    }
    std::vector<std::size_t> cursor(child_begin_.begin(), child_begin_.end() - 1);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t slot = parent_[index] == no_index ? 0U : parent_[index] + 1U;
        children_[cursor[slot]] = index;
        ++cursor[slot];
    }
    rebuild_order();
}

// Sorts every slot's children, records sibling positions, then lists the
// nodes in shown preorder with an explicit stack (children pushed last
// first, so the first child is visited first). Sorting siblings keeps each
// subtree contiguous after its node.
void TreeListView::rebuild_order() {
    const std::size_t count = nodes_.size();
    const bool sorted = sort_column_.has_value() && sort_order_ != TreeListSortOrder::none;
    const SiblingOrder by_order{&nodes_, sorted ? sort_column_ : std::nullopt,
                                sort_order_ == TreeListSortOrder::descending};
    sibling_position_.assign(count, 0U);
    for (std::size_t slot = 0; slot + 1U < child_begin_.size(); ++slot) {
        const std::size_t begin = child_begin_[slot];
        const std::size_t end = child_begin_[slot + 1U];
        std::sort(children_.begin() + static_cast<std::ptrdiff_t>(begin),
                  children_.begin() + static_cast<std::ptrdiff_t>(end), by_order);
        for (std::size_t position = begin; position < end; ++position) {
            sibling_position_[children_[position]] = position - begin;
        }
    }
    order_.clear();
    order_.reserve(count);
    std::vector<std::size_t> pending;
    pending.reserve(count);
    for (std::size_t position = child_begin_[1]; position > child_begin_[0]; --position) {
        pending.push_back(children_[position - 1U]);
    }
    while (!pending.empty()) {
        const std::size_t index = pending.back();
        pending.pop_back();
        order_.push_back(index);
        const std::size_t begin = child_begin_[index + 1U];
        for (std::size_t position = child_begin_[index + 2U]; position > begin; --position) {
            pending.push_back(children_[position - 1U]);
        }
    }
    rebuild_rows();
}

// The rows: the shown preorder without the nodes under a collapsed node.
void TreeListView::rebuild_rows() {
    visible_.clear();
    advance_layout_revision();
    row_of_node_.assign(nodes_.size(), no_index);
    std::optional<std::size_t> hidden_below{};
    for (const std::size_t index : order_) {
        const std::size_t depth = nodes_[index].depth;
        if (hidden_below && depth > *hidden_below) continue;
        hidden_below.reset();
        row_of_node_[index] = visible_.size();
        visible_.push_back(index);
        if (has_children(index) && !nodes_[index].expanded) hidden_below = depth;
    }
    top_row_ = visible_.empty() ? 0U : std::min(top_row_, visible_.size() - 1U);
    advance_layout_revision();
    hot_row_.reset();
}

bool TreeListView::has_children(std::size_t index) const noexcept {
    const bool result = child_begin_[index + 2U] > child_begin_[index + 1U];
    return result;
}

bool TreeListView::can_expand(std::size_t index) const noexcept {
    const bool result = has_children(index) || nodes_[index].expandable;
    return result;
}

std::size_t TreeListView::require_index(std::string_view node_id, const char* message) const {
    const NodeIndices::const_iterator found = node_indices_.find(std::string(node_id));
    if (found == node_indices_.end()) throw std::out_of_range(message);
    return (*found).second;
}

std::size_t TreeListView::visible_ancestor(std::size_t index) const noexcept {
    std::size_t node = index;
    while (row_of_node_[node] == no_index && parent_[node] != no_index) node = parent_[node];
    return node;
}

bool TreeListView::lift_hidden_selection() {
    if (current_node_ && row_of_node_[*current_node_] == no_index) {
        current_node_ = visible_ancestor(*current_node_);
    }
    if (!selected_node_ || row_of_node_[*selected_node_] != no_index) return false;
    selected_node_ = visible_ancestor(*selected_node_);
    return true;
}

std::optional<std::string> TreeListView::selected_copy() const {
    if (!selected_node_) return std::nullopt;
    return nodes_[*selected_node_].stable_id;
}

void TreeListView::publish_selection(std::optional<std::string> previous) {
    invalidate(Dirty::paint | Dirty::semantics);
    const TreeListSelectionChange change{std::move(previous), selected_copy()};
    publish_change(selection_changed_, change);
}

void TreeListView::expand_ancestors(std::size_t index, std::vector<TreeListExpansionChange>& changes) {
    const std::size_t first = changes.size();
    for (std::size_t node = parent_[index]; node != no_index; node = parent_[node]) {
        if (nodes_[node].expanded) continue;
        nodes_[node].expanded = true;
        changes.push_back({nodes_[node].stable_id, true});
    }
    if (changes.size() == first) return;
    std::reverse(changes.begin() + static_cast<std::ptrdiff_t>(first), changes.end());
    rebuild_rows();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// A handler may dispose the view or replace its nodes: the changes are
// owned copies, and the loop stops once the view is gone.
void TreeListView::publish_expansions(std::span<const TreeListExpansionChange> changes) {
    for (const TreeListExpansionChange& change : changes) {
        if (!is_alive()) return;
        publish_change(expansion_changed_, change);
    }
}

// -- columns -------------------------------------------------------------------

void TreeListView::set_columns(std::vector<TreeListColumn> columns) {
    require_mutable();
    if (columns.size() > maximum_columns) {
        throw std::invalid_argument("TreeListView has more columns than maximum_columns");
    }
    std::vector<std::string_view> identities;
    identities.reserve(columns.size());
    for (const TreeListColumn& column : columns) {
        if (!valid_stable_id(column.stable_id) ||
            !validate_utf8(column.text).valid() ||
            std::find(identities.begin(), identities.end(), column.stable_id) != identities.end()) {
            throw std::invalid_argument(
                "TreeListView columns require unique stable IDs following the StableId segment rule and valid UTF-8");
        }
        if (!std::isfinite(column.width) || column.width < minimum_column_width ||
            column.width > maximum_column_width || !valid_alignment(column.alignment)) {
            throw std::invalid_argument("TreeListView column width or alignment is invalid");
        }
        identities.push_back(column.stable_id);
    }
    if ((resizing_column_ || pressed_column_) && has_pointer_capture()) set_pointer_capture(false);
    resizing_column_.reset();
    pressed_column_.reset();
    dragging_column_ = false;
    hot_column_.reset();
    columns_ = std::move(columns);
    advance_layout_revision();
    column_order_.resize(columns_.size());
    std::iota(column_order_.begin(), column_order_.end(), std::size_t{0U});
    column_position_ = column_order_;
    const bool sort_lost = sort_column_ && *sort_column_ >= columns_.size();
    if (sort_lost) {
        sort_column_.reset();
        sort_order_ = TreeListSortOrder::none;
    }
    // A kept sort column may now name different text; a lost one returns
    // to the model order.
    if (sort_lost || sort_column_) rebuild_order();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void TreeListView::set_column_width(std::size_t column, double width) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("TreeListView column index is past the end");
    if (!std::isfinite(width) || width < minimum_column_width || width > maximum_column_width) {
        throw std::invalid_argument("TreeListView column width is outside the column width bounds");
    }
    if (columns_[column].width == width) return;
    columns_[column].width = width;
    advance_layout_revision();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const TreeListColumnWidthChange change{column, width};
    publish_change(column_width_changed_, change);
}

void TreeListView::move_column(std::size_t column, std::size_t display_position) {
    require_mutable();
    if (column >= columns_.size() || display_position >= columns_.size()) {
        throw std::out_of_range("TreeListView column or display position is past the end");
    }
    if (column == 0U || display_position == 0U) {
        throw std::invalid_argument("TreeListView's tree column stays at display position 0");
    }
    if (column_position_[column] == display_position) return;
    apply_column_move(column, display_position);
}

void TreeListView::apply_column_move(std::size_t column, std::size_t display_position) {
    const std::size_t from = column_position_[column];
    column_order_.erase(column_order_.begin() + static_cast<std::ptrdiff_t>(from));
    column_order_.insert(column_order_.begin() + static_cast<std::ptrdiff_t>(display_position), column);
    for (std::size_t position = 0; position < column_order_.size(); ++position) {
        column_position_[column_order_[position]] = position;
    }
    hot_column_.reset();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const TreeListColumnMove change{column, display_position};
    publish_change(column_moved_, change);
}

// -- nodes ---------------------------------------------------------------------

void TreeListView::set_nodes(std::vector<TreeListNode> nodes) {
    require_mutable();
    if (nodes.size() > maximum_nodes) throw std::invalid_argument("TreeListView has more nodes than maximum_nodes");
    NodeIndices next_indices{};
    next_indices.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const TreeListNode& node = nodes[index];
        if (!valid_stable_id(node.stable_id) || !valid_node_text(node)) {
            throw std::invalid_argument(
                "TreeListView nodes require stable IDs following the StableId segment rule, valid UTF-8 and bounded "
                "image keys");
        }
        const bool first_is_root = index != 0U || node.depth == 0U;
        const bool one_level_deeper = index == 0U || node.depth <= nodes[index - 1U].depth + 1U;
        if (!first_is_root || !one_level_deeper) {
            throw std::invalid_argument("TreeListView requires a valid preorder depth sequence");
        }
        const std::pair<NodeIndices::iterator, bool> inserted = next_indices.emplace(node.stable_id, index);
        if (!inserted.second) throw std::invalid_argument("TreeListView node IDs must be unique");
    }
    // Carry the selection and the current node over by stable ID.
    const std::optional<std::string> previous = selected_copy();
    std::optional<std::size_t> next_selected{};
    std::optional<std::size_t> next_current{};
    if (selected_node_) {
        const NodeIndices::const_iterator found = next_indices.find(nodes_[*selected_node_].stable_id);
        if (found != next_indices.end()) next_selected = (*found).second;
    }
    if (current_node_) {
        const NodeIndices::const_iterator found = next_indices.find(nodes_[*current_node_].stable_id);
        if (found != next_indices.end()) next_current = (*found).second;
    }
    nodes_.swap(nodes);
    node_indices_.swap(next_indices);
    selected_node_ = next_selected;
    current_node_ = next_current;
    rebuild_structure();
    static_cast<void>(lift_hidden_selection());
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const std::optional<std::string> after = selected_copy();
    if (previous != after) publish_selection(previous);
}

std::optional<std::size_t> TreeListView::index_of(std::string_view node_id) const {
    const NodeIndices::const_iterator found = node_indices_.find(std::string(node_id));
    if (found == node_indices_.end()) return std::nullopt;
    return (*found).second;
}

void TreeListView::set_cell_text(std::string_view node_id, std::size_t column, std::string text) {
    require_mutable();
    const std::size_t index = require_index(node_id, "TreeListView cell edit names an unknown node");
    if (column >= columns_.size()) throw std::out_of_range("TreeListView cell edit column is past the end");
    if (!validate_utf8(text).valid()) throw std::invalid_argument("TreeListView cell text must be valid UTF-8");
    TreeListNode& node = nodes_[index];
    if (column == 0U) {
        node.text = std::move(text);
    } else {
        if (node.cells.size() < column) node.cells.resize(column);
        node.cells[column - 1U] = std::move(text);
    }
    const bool resort = sort_column_ == column && sort_order_ != TreeListSortOrder::none;
    if (resort) rebuild_order();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- expansion -------------------------------------------------------------------

bool TreeListView::is_expanded(std::string_view node_id) const {
    const std::size_t index = require_index(node_id, "TreeListView expansion names an unknown node");
    const bool result = can_expand(index) && nodes_[index].expanded;
    return result;
}

void TreeListView::set_expanded(std::string_view node_id, bool expanded) {
    require_mutable();
    const std::size_t index = require_index(node_id, "TreeListView expansion names an unknown node");
    if (!can_expand(index) || nodes_[index].expanded == expanded) return;
    const std::optional<std::string> previous = selected_copy();
    nodes_[index].expanded = expanded;
    rebuild_rows();
    const bool moved = lift_hidden_selection();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const TreeListExpansionChange change{nodes_[index].stable_id, expanded};
    publish_change(expansion_changed_, change);
    if (moved && is_alive()) publish_selection(previous);
}

void TreeListView::expand_subtree(std::string_view node_id) {
    require_mutable();
    const std::size_t index = require_index(node_id, "TreeListView expansion names an unknown node");
    // In model order a node's subtree is the run of deeper nodes after it.
    const std::size_t depth = nodes_[index].depth;
    std::size_t end = index + 1U;
    while (end < nodes_.size() && nodes_[end].depth > depth) ++end;
    std::vector<TreeListExpansionChange> changes;
    for (std::size_t node = index; node < end; ++node) {
        if (!can_expand(node) || nodes_[node].expanded) continue;
        nodes_[node].expanded = true;
        changes.push_back({nodes_[node].stable_id, true});
    }
    if (changes.empty()) return;
    rebuild_rows();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    publish_expansions(changes);
}

// -- rows, scrolling and selection ---------------------------------------------

std::optional<std::size_t> TreeListView::row_of(std::string_view node_id) const {
    const std::optional<std::size_t> index = index_of(node_id);
    if (!index || row_of_node_[*index] == no_index) return std::nullopt;
    return row_of_node_[*index];
}

std::string_view TreeListView::row_node_id(std::size_t row) const {
    if (row >= visible_.size()) throw std::out_of_range("TreeListView row is past the end");
    return nodes_[visible_[row]].stable_id;
}

std::size_t TreeListView::top_row() const {
    const Layout placed = layout();
    return placed.top_row;
}

void TreeListView::set_top_row(std::size_t row) {
    const Layout placed = layout();
    const std::size_t clamped = std::min(row, maximum_top_row(placed));
    if (clamped == top_row_ && clamped == placed.top_row) return;
    top_row_ = clamped;
    advance_layout_revision();
    hot_row_.reset();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    sync_scroll_bars(layout());
}

void TreeListView::scroll_rows(std::ptrdiff_t rows) {
    const Layout placed = layout();
    const std::ptrdiff_t current = static_cast<std::ptrdiff_t>(placed.top_row);
    const std::ptrdiff_t maximum = static_cast<std::ptrdiff_t>(maximum_top_row(placed));
    const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(current + rows, 0, maximum);
    set_top_row(static_cast<std::size_t>(next));
}

void TreeListView::ensure_row_visible(std::size_t row) {
    const Layout placed = layout();
    std::size_t top = placed.top_row;
    if (row < top) {
        top = row;
    } else if (row >= top + placed.page_rows) {
        top = row - placed.page_rows + 1U;
    }
    if (top != placed.top_row) set_top_row(top);
}

void TreeListView::ensure_visible(std::string_view node_id) {
    require_mutable();
    const std::size_t index = require_index(node_id, "TreeListView ensure_visible names an unknown node");
    std::vector<TreeListExpansionChange> changes;
    expand_ancestors(index, changes);
    ensure_row_visible(row_of_node_[index]);
    publish_expansions(changes);
}

void TreeListView::set_horizontal_offset(double offset) {
    require_mutable();
    if (!std::isfinite(offset) || offset < 0.0) throw std::invalid_argument("TreeListView horizontal offset is invalid");
    horizontal_offset_ = offset;
    advance_layout_revision();
    const Layout placed = layout();
    horizontal_offset_ = placed.horizontal_offset;
    advance_layout_revision();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    sync_scroll_bars(placed);
}

std::optional<std::string_view> TreeListView::current_id() const noexcept {
    if (!current_node_) return std::nullopt;
    return std::string_view(nodes_[*current_node_].stable_id);
}

void TreeListView::choose_row(std::size_t row) {
    if (row >= visible_.size()) return;
    const std::size_t index = visible_[row];
    if (!nodes_[index].enabled) return;
    const std::optional<std::string> previous = selected_copy();
    const bool changed = selected_node_ != index;
    selected_node_ = index;
    current_node_ = index;
    ensure_row_visible(row);
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_selection(previous);
}

void TreeListView::activate_row(std::size_t row) {
    if (row >= visible_.size()) return;
    const TreeListNode& node = nodes_[visible_[row]];
    if (!node.enabled) return;
    const TreeListActivation activation{node.stable_id};
    item_activated_.emit(activation);
}

// -- sorting -----------------------------------------------------------------------

void TreeListView::set_sort(std::optional<std::size_t> column, TreeListSortOrder order) {
    require_mutable();
    if (column && *column >= columns_.size()) throw std::out_of_range("TreeListView sort column is past the end");
    if (order != TreeListSortOrder::none && order != TreeListSortOrder::ascending &&
        order != TreeListSortOrder::descending) {
        throw std::invalid_argument("TreeListView sort order is invalid");
    }
    if (!column || order == TreeListSortOrder::none) {
        column.reset();
        order = TreeListSortOrder::none;
    }
    if (sort_column_ == column && sort_order_ == order) return;
    sort_column_ = column;
    sort_order_ = order;
    rebuild_order();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const TreeListSortChange change{column, order};
    publish_change(sort_changed_, change);
}

// A header click: ascending first, then the other way on the same column.
void TreeListView::toggle_sort(std::size_t column) {
    const bool ascending = sort_column_ == column && sort_order_ == TreeListSortOrder::ascending;
    set_sort(column, ascending ? TreeListSortOrder::descending : TreeListSortOrder::ascending);
}

// -- appearance --------------------------------------------------------------------

void TreeListView::image_list_changed(const ImageListChange&) {
    if (is_alive()) invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test);
}

void TreeListView::on_attached_to_window() {
    Control::on_attached_to_window();
    const std::shared_ptr<ImageList>& images = image_list.read();
    if (images && !(*images).belongs_to(*window())) {
        throw std::logic_error("TreeListView cannot attach to a Window other than its ImageList's");
    }
}

// -- layout --------------------------------------------------------------------------

double TreeListView::text_line_height() const {
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", text_font(font.read()));
    return metrics.logical_size.height;
}

Size TreeListView::icon_size() const noexcept {
    const double scale = effective_text_scale();
    Size size{default_icon, default_icon};
    const std::shared_ptr<ImageList>& images = image_list.read();
    if (images) size = (*images).image_size();
    const Size result{size.width * scale, size.height * scale};
    return result;
}

const TreeListView::Layout& TreeListView::layout() const {
    const LayoutKey key = layout_key();
    const std::uint64_t model = 0U;
    if (kept_layout_valid_ && kept_layout_key_ == key && kept_layout_model_ == model) return kept_layout_;
    // The key is taken before the layout is made: a layout that
    // writes what it reads is made again at the next call.
    kept_layout_ = layout_for(key.client);
    kept_layout_key_ = key;
    kept_layout_model_ = model;
    kept_layout_valid_ = true;
    return kept_layout_;
}

TreeListView::Layout TreeListView::layout_for(Rect client) const {
    Layout placed;
    const double scale = effective_text_scale();
    placed.scale = scale;
    placed.well = client;
    const double inset = edge_inset * scale;
    const Rect content{client.x + inset, client.y + inset, std::max(0.0, client.width - inset * 2.0),
                       std::max(0.0, client.height - inset * 2.0)};
    const double line = text_line_height();
    double row = std::max(minimum_row_height * scale, line + row_text_padding * scale);
    if (image_list.read()) row = std::max(row, icon_size().height + cell_padding * scale);
    placed.row_height = row;
    double columns_width = 0.0;
    for (const TreeListColumn& column : columns_) columns_width += column.width * scale;
    placed.columns_width = columns_width;
    // Without columns there is no tree column to show the nodes in.
    placed.shown_rows = columns_.empty() ? 0U : visible_.size();
    const double header_height = columns_.empty() ? 0.0 : row + 2.0 * scale;
    // Showing one bar can shrink the area enough to need the other. A bar
    // only shrinks the area, and a smaller area never needs fewer bars, so
    // each flag turns on at most once: two changes and a confirming pass
    // settle them, and the loop ends with `area` matching both flags.
    bool vertical = false;
    bool horizontal = false;
    Rect area{};
    for (int pass = 0; pass < 3; ++pass) {
        const double bar_width = vertical ? scroll_bar_extent : 0.0;
        const double bar_height = horizontal ? scroll_bar_extent : 0.0;
        area = {content.x, content.y + header_height, std::max(0.0, content.width - bar_width),
                std::max(0.0, content.height - header_height - bar_height)};
        placed.page_rows = fitting_rows(area.height, row);
        const bool next_vertical = placed.shown_rows > placed.page_rows;
        const bool next_horizontal = !columns_.empty() && columns_width > area.width + 0.5;
        if (next_vertical == vertical && next_horizontal == horizontal) break;
        vertical = next_vertical;
        horizontal = next_horizontal;
    }
    placed.viewport = area;
    if (header_height > 0.0) placed.header = {content.x, content.y, area.width, header_height};
    const double bar_bottom = horizontal ? scroll_bar_extent : 0.0;
    if (vertical) {
        placed.vertical_bar = {content.x + content.width - scroll_bar_extent, content.y, scroll_bar_extent,
                               std::max(0.0, content.height - bar_bottom)};
    }
    if (horizontal) {
        placed.horizontal_bar = {content.x, content.y + content.height - scroll_bar_extent,
                                 std::max(0.0, content.width - (vertical ? scroll_bar_extent : 0.0)),
                                 scroll_bar_extent};
    }
    if (vertical && horizontal) {
        placed.corner = {content.x + content.width - scroll_bar_extent,
                         content.y + content.height - scroll_bar_extent, scroll_bar_extent, scroll_bar_extent};
    }
    placed.visible_rows = touched_rows(area.height, row);
    placed.top_row = std::min(top_row_, maximum_top_row(placed));
    const double maximum_offset = std::max(0.0, (columns_width - area.width) / scale);
    placed.horizontal_offset = std::clamp(horizontal_offset_, 0.0, maximum_offset);
    return placed;
}

std::size_t TreeListView::maximum_top_row(const Layout& placed) const noexcept {
    if (placed.shown_rows <= placed.page_rows) return 0U;
    const std::size_t result = placed.shown_rows - placed.page_rows;
    return result;
}

// The realized rows are [placed.top_row, end_realized(placed)).
std::size_t TreeListView::end_realized(const Layout& placed) const noexcept {
    const std::size_t result = std::min(placed.shown_rows, placed.top_row + placed.visible_rows);
    return result;
}

Rect TreeListView::row_bounds(const Layout& placed, std::size_t row) const noexcept {
    const double relative = static_cast<double>(row) - static_cast<double>(placed.top_row);
    const Rect bounds{placed.viewport.x - placed.horizontal_offset * placed.scale,
                      placed.viewport.y + relative * placed.row_height, placed.columns_width, placed.row_height};
    return bounds;
}

// A column's header cell; its x extent is also the column's extent in every
// row. Columns are laid out in display order.
Rect TreeListView::column_cell(const Layout& placed, std::size_t column) const noexcept {
    double x = placed.viewport.x - placed.horizontal_offset * placed.scale;
    const std::size_t position = column_position_[column];
    for (std::size_t earlier = 0; earlier < position; ++earlier) {
        x += columns_[column_order_[earlier]].width * placed.scale;
    }
    const Rect cell{x, placed.header.y, columns_[column].width * placed.scale, placed.header.height};
    return cell;
}

bool TreeListView::column_visible(const Layout& placed, std::size_t column) const noexcept {
    if (placed.header.empty()) return false;
    const Rect cell = column_cell(placed, column);
    const bool result = cell.x < placed.viewport.x + placed.viewport.width && cell.x + cell.width > placed.viewport.x;
    return result;
}

std::size_t TreeListView::visible_column_count(const Layout& placed) const noexcept {
    std::size_t count = 0U;
    for (std::size_t column = 0; column < columns_.size(); ++column) {
        if (column_visible(placed, column)) ++count;
    }
    return count;
}

std::size_t TreeListView::visible_column(const Layout& placed, std::size_t position) const noexcept {
    std::size_t seen = 0U;
    for (std::size_t display = 0; display < column_order_.size(); ++display) {
        const std::size_t column = column_order_[display];
        if (!column_visible(placed, column)) continue;
        if (seen == position) return column;
        ++seen;
    }
    return columns_.size();
}

// One row's parts. `clip` bounds every part: the viewport when publishing,
// the row's own bounds when painting (the painter clips to the viewport), so
// text scrolled partly out of view keeps its place. The expander, icon and
// text also stay inside the tree column. The expander slot (one indentation
// step wide) is kept on every row, so leaves line up with their siblings'
// text.
TreeListView::RowParts TreeListView::row_parts(const Layout& placed, std::size_t row, Rect clip) const {
    const double scale = placed.scale;
    const std::size_t index = visible_[row];
    const TreeListNode& node = nodes_[index];
    const Rect bounds = row_bounds(placed, row);
    const Rect tree_header = column_cell(placed, 0U);
    const Rect tree_cell{tree_header.x, bounds.y, tree_header.width, bounds.height};
    const Rect inner_clip = Rect::intersection(clip, tree_cell);
    const double middle = bounds.y + bounds.height * 0.5;
    const double step = indentation.read() * scale;
    double x = tree_cell.x + cell_padding * scale + static_cast<double>(node.depth) * step;
    const double side = std::min(step, bounds.height);
    Rect expander{};
    if (can_expand(index)) expander = {x, std::floor(middle - side * 0.5), side, side};
    x += side + expander_gap * scale;
    Rect icon{};
    if (image_list.read()) {
        const Size image = icon_size();
        icon = {x, std::floor(middle - image.height * 0.5), image.width, image.height};
        x += image.width + cell_padding * scale;
    }
    const double right = tree_cell.x + tree_cell.width - cell_padding * scale;
    const Rect text{x, bounds.y, std::max(0.0, right - x), bounds.height};
    RowParts parts;
    parts.row = PartGeometry::from_bounds(Rect::intersection(bounds, clip));
    parts.row_face = parts.row;
    parts.expander = PartGeometry::from_bounds(Rect::intersection(expander, inner_clip));
    parts.icon = PartGeometry::from_bounds(Rect::intersection(icon, inner_clip));
    parts.text = PartGeometry::from_bounds(Rect::intersection(text, inner_clip));
    parts.row_focus = parts.row_face;
    return parts;
}

// One cell past the tree column, clipped as row_parts is.
TreeListView::CellParts TreeListView::cell_parts(const Layout& placed, std::size_t row, std::size_t column,
                                                 Rect clip) const {
    const Rect bounds = row_bounds(placed, row);
    const Rect header = column_cell(placed, column);
    const Rect cell{header.x, bounds.y, header.width, bounds.height};
    const double padding = cell_text_padding * placed.scale;
    const Rect text{cell.x + padding, cell.y, std::max(0.0, cell.width - padding * 2.0), cell.height};
    const Rect visible = Rect::intersection(cell, clip);
    CellParts parts;
    parts.cell = PartGeometry::from_bounds(visible);
    parts.cell_text = PartGeometry::from_bounds(Rect::intersection(text, visible));
    return parts;
}

// One column header, from the layout ObjectView's and ListView's headers
// share. `clip` is the header band when publishing, the column's own cell
// when painting.
TreeListView::ColumnParts TreeListView::column_parts(const Layout& placed, std::size_t column, Rect clip) const {
    const bool sorted = sort_column_ == column && sort_order_ != TreeListSortOrder::none;
    const details_detail::ColumnHeaderGeometry header =
        details_detail::column_header_geometry(column_cell(placed, column), clip, sorted, placed.scale);
    ColumnParts parts;
    parts.column_header = PartGeometry::from_bounds(header.header);
    parts.column_label = PartGeometry::from_bounds(header.label);
    parts.sort_glyph = PartGeometry::from_bounds(header.sort_glyph);
    parts.column_divider = PartGeometry::from_bounds(header.divider);
    parts.column_grip = PartGeometry::from_bounds(header.grip);
    return parts;
}

namespace {

// Application-defined column states, as ObjectView and ListView declare them.
constexpr CustomStateIndex sorted_ascending_state{0U};
constexpr CustomStateIndex sorted_descending_state{1U};

} // namespace

AnatomyStateSet TreeListView::row_states(std::size_t row) const {
    const std::size_t index = visible_[row];
    AnatomyStateSet states;
    if (selected_node_ == index) states.insert(AnatomyState::selected);
    if (hot_row_ == row) states.insert(AnatomyState::hot);
    if (current_node_ == index) states.insert(AnatomyState::current);
    if (can_expand(index)) states.insert(nodes_[index].expanded ? AnatomyState::open : AnatomyState::closed);
    if (!nodes_[index].enabled || !effectively_enabled()) states.insert(AnatomyState::disabled);
    const Window* const host = attached_window();
    if (focused_ && current_node_ == index && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

AnatomyStateSet TreeListView::cell_states(std::size_t row) const {
    const std::size_t index = visible_[row];
    AnatomyStateSet states;
    if (selected_node_ == index) states.insert(AnatomyState::selected);
    if (!nodes_[index].enabled || !effectively_enabled()) states.insert(AnatomyState::disabled);
    return states;
}

AnatomyStateSet TreeListView::column_states(std::size_t column) const {
    AnatomyStateSet states;
    if (hot_column_ == column) states.insert(AnatomyState::hot);
    if (pressed_column_ == column) states.insert(AnatomyState::pressed);
    // A column being resized or dragged to a new position holds the pointer.
    if (resizing_column_ == column || (dragging_column_ && pressed_column_ == column)) {
        states.insert(AnatomyState::captured);
    }
    if (sort_column_ == column && sort_order_ == TreeListSortOrder::ascending) {
        states.insert(sorted_ascending_state);
    }
    if (sort_column_ == column && sort_order_ == TreeListSortOrder::descending) {
        states.insert(sorted_descending_state);
    }
    return states;
}

void TreeListView::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Layout placed = layout_for({0.0, 0.0, final_bounds.width, final_bounds.height});
    if (vertical_bar_) {
        const bool shown = !placed.vertical_bar.empty();
        set_child_layout_collapsed(vertical_bar_, !shown);
        if (shown) set_child_layout(vertical_bar_, placed.vertical_bar);
    }
    if (horizontal_bar_) {
        const bool shown = !placed.horizontal_bar.empty();
        set_child_layout_collapsed(horizontal_bar_, !shown);
        if (shown) set_child_layout(horizontal_bar_, placed.horizontal_bar);
    }
    // Keep the stored scroll within the new bounds.
    top_row_ = placed.top_row;
    advance_layout_revision();
    horizontal_offset_ = placed.horizontal_offset;
    advance_layout_revision();
    sync_scroll_bars(placed);
}

void TreeListView::sync_scroll_bars(const Layout& placed) {
    if (!vertical_bar_ || !horizontal_bar_) return;
    synchronizing_scroll_ = true;
    const double maximum_row = static_cast<double>(maximum_top_row(placed));
    if (!placed.vertical_bar.empty() && maximum_row > 0.0) {
        VScrollBar& bar = *vertical_bar_;
        bar.set_range(0.0, maximum_row);
        bar.small_change = 1.0;
        bar.large_change = static_cast<double>(placed.page_rows);
        bar.value = std::clamp(static_cast<double>(placed.top_row), 0.0, maximum_row);
    }
    if (!placed.horizontal_bar.empty()) {
        const double visible = placed.viewport.width / placed.scale;
        const double maximum_offset = std::max(0.0, placed.columns_width / placed.scale - visible);
        if (maximum_offset > 0.0) {
            HScrollBar& bar = *horizontal_bar_;
            bar.set_range(0.0, maximum_offset);
            bar.small_change = horizontal_key_step;
            bar.large_change = std::max(1.0, visible);
            bar.value = std::clamp(placed.horizontal_offset, 0.0, maximum_offset);
        }
    }
    synchronizing_scroll_ = false;
}

void TreeListView::on_vertical_scroll(double value) {
    if (synchronizing_scroll_ || !is_alive()) return;
    const double row = std::max(0.0, std::round(value));
    set_top_row(static_cast<std::size_t>(row));
}

void TreeListView::on_horizontal_scroll(double value) {
    if (synchronizing_scroll_ || !is_alive() || horizontal_offset_ == value) return;
    horizontal_offset_ = std::max(0.0, value);
    advance_layout_revision();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- paint ---------------------------------------------------------------------------

void TreeListView::paint_column(Painter& painter, const Layout& placed, std::size_t column) {
    const Rect cell = column_cell(placed, column);
    const ColumnParts parts = column_parts(placed, column, cell);
    PartPaint part;
    part.font = text_font(font.read());
    part.states = column_states(column);
    // Header cells are flat at rest and show a plate when hot or pressed:
    // the menu_item family's look, as ListView's headers.
    part.family = ControlVisualRole::menu_item;
    part.role = PartRole::face;
    part.bounds = parts.column_header.bounds;
    paint_part(painter, *this, part);
    part.role = PartRole::label;
    part.bounds = parts.column_label.bounds;
    part.alignment = text_alignment(columns_[column].alignment);
    details_detail::elide_to_width(painter, columns_[column].text, part.font, part.bounds.width, paint_text_);
    part.text = paint_text_;
    paint_part(painter, *this, part);
    part.text = {};
    part.alignment = PartTextAlignment::start;
    if (parts.sort_glyph.present) {
        part.role = PartRole::glyph;
        part.bounds = parts.sort_glyph.bounds;
        part.mark = sort_order_ == TreeListSortOrder::ascending ? PartMark::arrow_up : PartMark::arrow_down;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    part.family = ControlVisualRole::panel;
    part.role = PartRole::divider;
    part.bounds = parts.column_divider.bounds;
    paint_part(painter, *this, part);
    // column_grip is the divider's drag zone. The divider is its visible
    // sign; painting the grip role would draw grip dots at every divider.
}

void TreeListView::paint_row(Painter& painter, const Layout& placed, std::size_t row) {
    const TreeListNode& node = nodes_[visible_[row]];
    const Rect bounds = row_bounds(placed, row);
    const RowParts parts = row_parts(placed, row, bounds);
    const AnatomyStateSet states = row_states(row);
    PartPaint part;
    part.font = text_font(font.read());
    part.states = states;
    // The row is the selection family's part: transparent until hot or
    // selected, with the selection's text and glyph colours.
    part.family = ControlVisualRole::selection;
    part.role = PartRole::face;
    part.bounds = parts.row_face.bounds;
    paint_part(painter, *this, part);
    if (parts.expander.present) {
        part.role = PartRole::disclosure;
        part.bounds = parts.expander.bounds;
        part.mark = node.expanded ? PartMark::chevron_down : PartMark::chevron_right;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    if (parts.icon.present && !node.image_key.empty()) {
        const Window* const host = attached_window();
        const double density = host != nullptr ? (*host).scale() : 1.0;
        const ImageList& images = *image_list.read();
        const ImageListResolution resolved = images.resolve(node.image_key, image_state(states), density);
        if (resolved) {
            part.role = PartRole::icon;
            part.bounds = parts.icon.bounds;
            part.image = resolved.image;
            paint_part(painter, *this, part);
            part.image = {};
        }
    }
    part.role = PartRole::content;
    part.bounds = parts.text.bounds;
    details_detail::elide_to_width(painter, node.text, part.font, part.bounds.width, paint_text_);
    part.text = paint_text_;
    paint_part(painter, *this, part);
    part.text = {};
    part.states = cell_states(row);
    for (std::size_t position = 1; position < column_order_.size(); ++position) {
        const std::size_t column = column_order_[position];
        if (!column_visible(placed, column)) continue;
        const CellParts cell = cell_parts(placed, row, column, bounds);
        part.bounds = cell.cell_text.bounds;
        part.alignment = text_alignment(columns_[column].alignment);
        details_detail::elide_to_width(painter, column_text(node, column), part.font, part.bounds.width,
                                       paint_text_);
        part.text = paint_text_;
        paint_part(painter, *this, part);
        part.text = {};
    }
    part.states = states;
    part.alignment = PartTextAlignment::start;
    part.role = PartRole::focus;
    part.bounds = parts.row_focus.bounds;
    paint_part(painter, *this, part);
}

void TreeListView::on_paint(Painter& painter, Rect) {
    const Layout placed = layout();
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.bounds = placed.well;
    paint_part(painter, *this, well);
    if (!placed.header.empty()) {
        PartPaint header;
        header.family = ControlVisualRole::panel;
        header.role = PartRole::face;
        header.bounds = placed.header;
        paint_part(painter, *this, header);
        painter.save();
        painter.clip_rect(placed.header);
        for (std::size_t column = 0; column < columns_.size(); ++column) {
            if (column_visible(placed, column)) paint_column(painter, placed, column);
        }
        painter.restore();
    }
    if (!placed.viewport.empty()) {
        painter.save();
        painter.clip_rect(placed.viewport);
        const std::size_t end = end_realized(placed);
        for (std::size_t row = placed.top_row; row < end; ++row) paint_row(painter, placed, row);
        painter.restore();
    }
    if (!placed.corner.empty()) {
        PartPaint corner;
        corner.family = ControlVisualRole::panel;
        corner.role = PartRole::face;
        corner.bounds = placed.corner;
        paint_part(painter, *this, corner);
    }
    // With no row to carry the focus cue, the view itself shows it.
    if (placed.shown_rows == 0U) {
        AnatomyStateSet control_states;
        const Window* const host = attached_window();
        if (focused_ && host != nullptr && (*host).focus_cue_visible()) {
            control_states.insert(AnatomyState::focus_visible);
        }
        PartPaint focus;
        focus.family = ControlVisualRole::editor;
        focus.role = PartRole::focus;
        focus.bounds = placed.well;
        focus.states = control_states;
        paint_part(painter, *this, focus);
    }
}

// -- input ---------------------------------------------------------------------------

TreeListView::Hit TreeListView::hit_test(const Layout& placed, Point local) const {
    Hit hit;
    if (!placed.header.empty() && placed.header.contains(local)) {
        const double reach = divider_reach * placed.scale;
        for (std::size_t column = 0; column < columns_.size(); ++column) {
            if (!column_visible(placed, column)) continue;
            const Rect cell = column_cell(placed, column);
            if (std::abs(local.x - (cell.x + cell.width)) <= reach) {
                hit.zone = HitZone::divider;
                hit.index = column;
                return hit;
            }
        }
        for (std::size_t column = 0; column < columns_.size(); ++column) {
            if (column_cell(placed, column).contains(local)) {
                hit.zone = HitZone::header;
                hit.index = column;
                return hit;
            }
        }
        return hit;
    }
    if (placed.viewport.empty() || !placed.viewport.contains(local)) return hit;
    const double down = local.y - placed.viewport.y;
    const std::size_t row = placed.top_row + static_cast<std::size_t>(down / placed.row_height);
    if (row >= placed.shown_rows) return hit;
    // The whole width of the viewport selects the row.
    hit.index = row;
    hit.zone = HitZone::row;
    const RowParts parts = row_parts(placed, row, placed.viewport);
    if (parts.expander.present && parts.expander.bounds.contains(local)) hit.zone = HitZone::expander;
    return hit;
}

std::optional<std::string> TreeListView::node_id_at(Point window_point) const {
    const Rect absolute = absolute_bounds();
    const Point local{window_point.x - absolute.x, window_point.y - absolute.y};
    const Hit hit = hit_test(layout(), local);
    if (hit.zone != HitZone::row && hit.zone != HitZone::expander) return std::nullopt;
    return nodes_[visible_[hit.index]].stable_id;
}

void TreeListView::finish_column_resize() {
    if (!resizing_column_) return;
    const std::size_t column = *resizing_column_;
    resizing_column_.reset();
    if (has_pointer_capture()) set_pointer_capture(false);
    invalidate(Dirty::paint | Dirty::semantics);
    if (column >= columns_.size() || columns_[column].width == resize_start_width_) return;
    const TreeListColumnWidthChange change{column, columns_[column].width};
    column_width_changed_.emit(change);
}

void TreeListView::end_header_press() {
    pressed_column_.reset();
    dragging_column_ = false;
    if (has_pointer_capture()) set_pointer_capture(false);
    invalidate(Dirty::paint | Dirty::semantics);
}

std::size_t TreeListView::drop_position(const Layout& placed, std::size_t column, double local_x) const noexcept {
    std::size_t position = 0U;
    for (std::size_t other = 0; other < columns_.size(); ++other) {
        if (other == column) continue;
        const Rect cell = column_cell(placed, other);
        if (cell.x + cell.width * 0.5 < local_x) ++position;
    }
    const std::size_t result = std::max<std::size_t>(1U, position);
    return result;
}

void TreeListView::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const Control::Ptr lifetime = shared_from_this();
    if (resizing_column_) {
        // The window may end capture before it delivers the release, and a
        // host or another control may take it away: either ends the drag.
        if (event.action == PointerAction::up || !has_pointer_capture()) {
            finish_column_resize();
            event.handled = event.action == PointerAction::up;
            if (event.handled) return;
        } else {
            if (event.action == PointerAction::move && *resizing_column_ < columns_.size()) {
                const double delta = (local.x - resize_start_x_) / effective_text_scale();
                const double width =
                    std::clamp(resize_start_width_ + delta, minimum_column_width, maximum_column_width);
                if (columns_[*resizing_column_].width != width) {
                    columns_[*resizing_column_].width = width;
                    advance_layout_revision();
                    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
                }
            }
            event.handled = true;
            return;
        }
    }
    const Layout placed = layout();
    if (pressed_column_) {
        if (!has_pointer_capture() && event.action != PointerAction::up) {
            end_header_press();
        } else if (event.action == PointerAction::move) {
            // A press on any header but the tree column's becomes a move
            // once the pointer travels the drag threshold.
            const bool movable = *pressed_column_ != 0U;
            const bool far = std::abs(local.x - press_x_) >= drag_threshold * placed.scale;
            if (movable && far && !dragging_column_) {
                dragging_column_ = true;
                invalidate(Dirty::paint | Dirty::semantics);
            }
            event.handled = true;
            return;
        } else if (event.action == PointerAction::up && event.button == PointerButton::primary) {
            const std::size_t column = *pressed_column_;
            const bool dragged = dragging_column_;
            end_header_press();
            event.handled = true;
            if (dragged) {
                const std::size_t position = drop_position(placed, column, local.x);
                if (column < columns_.size() && position != column_position_[column]) {
                    apply_column_move(column, position);
                }
                return;
            }
            // A click is a press and release on the same header.
            const Hit released = hit_test(placed, local);
            if (released.zone == HitZone::header && released.index == column) toggle_sort(column);
            return;
        }
    }
    if (event.action == PointerAction::leave) {
        if (hot_row_ || hot_column_) {
            hot_row_.reset();
            hot_column_.reset();
            invalidate(Dirty::paint);
        }
        return;
    }
    const Hit hit = hit_test(placed, local);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        const bool on_row = hit.zone == HitZone::row || hit.zone == HitZone::expander;
        const std::optional<std::size_t> next_row = on_row ? std::optional<std::size_t>(hit.index) : std::nullopt;
        const std::optional<std::size_t> next_column =
            hit.zone == HitZone::header ? std::optional<std::size_t>(hit.index) : std::nullopt;
        cursor = hit.zone == HitZone::divider ? CursorKind::resize_horizontal : CursorKind::arrow;
        if (next_row != hot_row_ || next_column != hot_column_) {
            hot_row_ = next_row;
            hot_column_ = next_column;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action == PointerAction::wheel) {
        if (event.wheel_delta.y == 0.0 && event.wheel_delta.x == 0.0) return;
        const bool sideways = event.wheel_delta.x != 0.0 || has_modifier(event.modifiers, Modifier::shift);
        if (sideways) {
            const double delta = event.wheel_delta.x != 0.0 ? event.wheel_delta.x : event.wheel_delta.y;
            set_horizontal_offset(std::max(0.0, horizontal_offset_ - delta * horizontal_key_step * 2.0));
        } else {
            scroll_rows(event.wheel_delta.y > 0.0 ? -wheel_rows : wheel_rows);
        }
        event.handled = true;
        return;
    }
    if (event.action != PointerAction::down) return;
    if (event.button != PointerButton::primary && event.button != PointerButton::secondary) return;
    if (window() != nullptr) static_cast<void>((*window()).request_focus(lifetime));
    if (!is_alive()) return;
    event.handled = true;
    if (event.button == PointerButton::secondary) {
        // A secondary press selects the row under it (context menus are the
        // application's).
        if (hit.zone == HitZone::row || hit.zone == HitZone::expander) choose_row(hit.index);
        return;
    }
    // A press on no zone only takes focus.
    if (hit.zone == HitZone::divider) {
        resizing_column_ = hit.index;
        resize_start_x_ = local.x;
        resize_start_width_ = columns_[hit.index].width;
        if (window() != nullptr) (*window()).capture_pointer(lifetime, event.pointer_id);
        invalidate(Dirty::paint | Dirty::semantics);
    } else if (hit.zone == HitZone::header) {
        pressed_column_ = hit.index;
        dragging_column_ = false;
        press_x_ = local.x;
        if (window() != nullptr) (*window()).capture_pointer(lifetime, event.pointer_id);
        invalidate(Dirty::paint | Dirty::semantics);
    } else if (hit.zone == HitZone::expander) {
        // The expander toggles without selecting, as TreeView's does.
        const TreeListNode& node = nodes_[visible_[hit.index]];
        set_expanded(node.stable_id, !node.expanded);
    } else if (hit.zone == HitZone::row) {
        if (event.click_count >= 2U) {
            choose_row(hit.index);
            if (is_alive()) activate_row(hit.index);
        } else {
            choose_row(hit.index);
        }
    }
}

std::optional<std::size_t> TreeListView::enabled_row_from(std::size_t row, std::ptrdiff_t step) const {
    std::ptrdiff_t position = static_cast<std::ptrdiff_t>(row);
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(visible_.size());
    while (position >= 0 && position < count) {
        if (nodes_[visible_[static_cast<std::size_t>(position)]].enabled) {
            return static_cast<std::size_t>(position);
        }
        position += step;
    }
    return std::nullopt;
}

std::optional<std::size_t> TreeListView::navigation_target(std::uint32_t key, std::size_t from,
                                                           const Layout& placed) const {
    const std::size_t last = placed.shown_rows - 1U;
    const std::size_t page = placed.page_rows;
    if (key == PhysicalKey::home) return enabled_row_from(0U, 1);
    if (key == PhysicalKey::end) return enabled_row_from(last, -1);
    if (key == PhysicalKey::up) {
        if (from == 0U) return std::nullopt;
        return enabled_row_from(from - 1U, -1);
    }
    if (key == PhysicalKey::down) {
        if (from >= last) return std::nullopt;
        return enabled_row_from(from + 1U, 1);
    }
    if (key == PhysicalKey::page_up) {
        const std::size_t target = from >= page ? from - page : 0U;
        const std::optional<std::size_t> found = enabled_row_from(target, -1);
        if (found) return found;
        return enabled_row_from(target, 1);
    }
    if (key == PhysicalKey::page_down) {
        const std::size_t target = std::min(from + page, last);
        const std::optional<std::size_t> found = enabled_row_from(target, 1);
        if (found) return found;
        return enabled_row_from(target, -1);
    }
    return std::nullopt;
}

// Right: expand a collapsed node, or move to the first child of an
// expanded one.
void TreeListView::step_into(std::size_t row) {
    const std::size_t index = visible_[row];
    if (can_expand(index) && !nodes_[index].expanded) {
        set_expanded(nodes_[index].stable_id, true);
        return;
    }
    const bool child_follows = row + 1U < visible_.size() && parent_[visible_[row + 1U]] == index;
    if (child_follows) choose_row(row + 1U);
}

// Left: collapse an expanded node, or move to the parent.
void TreeListView::step_out(std::size_t row) {
    const std::size_t index = visible_[row];
    if (can_expand(index) && nodes_[index].expanded) {
        set_expanded(nodes_[index].stable_id, false);
        return;
    }
    if (parent_[index] != no_index) choose_row(row_of_node_[parent_[index]]);
}

void TreeListView::on_key(KeyEvent& event) {
    if (!focused_ || !effectively_enabled() || event.action != KeyAction::down) return;
    const Control::Ptr lifetime = shared_from_this();
    if (event.physical_key == PhysicalKey::escape && resizing_column_) {
        columns_[*resizing_column_].width = resize_start_width_;
        advance_layout_revision();
        finish_column_resize();
        invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::escape && pressed_column_) {
        end_header_press();
        event.handled = true;
        return;
    }
    const Layout placed = layout();
    if (placed.shown_rows == 0U) return;
    const bool control = has_modifier(event.modifiers, Modifier::control) ||
                         has_modifier(event.modifiers, Modifier::meta);
    const std::size_t current_row = current_node_ ? row_of_node_[*current_node_] : no_index;
    const bool has_current = current_row != no_index;
    const std::size_t from = has_current ? current_row : placed.top_row;
    const std::uint32_t key = event.physical_key;
    if (key == PhysicalKey::enter) {
        if (has_current) activate_row(from);
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::space) {
        choose_row(from);
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::left || key == PhysicalKey::right) {
        if (control) {
            // Control with Left or Right scrolls the columns sideways.
            const double step = key == PhysicalKey::left ? -horizontal_key_step : horizontal_key_step;
            set_horizontal_offset(std::max(0.0, placed.horizontal_offset + step));
        } else if (!has_current) {
            choose_row(from);
        } else if (key == PhysicalKey::right) {
            step_into(from);
        } else {
            step_out(from);
        }
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::keypad_plus || key == PhysicalKey::keypad_minus) {
        const std::size_t index = visible_[from];
        set_expanded(nodes_[index].stable_id, key == PhysicalKey::keypad_plus);
        event.handled = true;
        return;
    }
    const bool navigation = key == PhysicalKey::up || key == PhysicalKey::down || key == PhysicalKey::home ||
                            key == PhysicalKey::end || key == PhysicalKey::page_up ||
                            key == PhysicalKey::page_down;
    if (!navigation) return;
    const std::optional<std::size_t> target = navigation_target(key, from, placed);
    if (target) choose_row(*target);
    event.handled = true;
}

// A repeated first letter steps to the next match; a longer prefix starts
// its search at the current row. Only rows (shown nodes) are searched.
void TreeListView::type_select(std::string_view text) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - last_type_time_ > collection_detail::type_timeout) type_prefix_.clear();
    last_type_time_ = now;
    type_prefix_ += collection_detail::fold_ascii(text);
    const std::size_t count = visible_.size();
    if (count == 0U || columns_.empty()) return;
    const bool single = type_prefix_.size() == 1U;
    const std::size_t current = current_node_ ? row_of_node_[*current_node_] : no_index;
    const std::size_t focus = current != no_index ? current : count - 1U;
    const std::size_t start = single ? (focus + 1U) % count : focus % count;
    for (std::size_t offset = 0; offset < count; ++offset) {
        const std::size_t row = (start + offset) % count;
        const TreeListNode& node = nodes_[visible_[row]];
        if (node.enabled && collection_detail::starts_with_folded(node.text, type_prefix_)) {
            choose_row(row);
            return;
        }
    }
}

void TreeListView::on_text_input(TextInputEvent& event) {
    if (!focused_ || !effectively_enabled() || event.composing || event.text_utf8.empty() ||
        !validate_utf8(event.text_utf8).valid()) {
        return;
    }
    // Space belongs to on_key; it never starts a search.
    if (event.text_utf8 == " " && type_prefix_.empty()) return;
    // The asterisk expands everything under the current node, as in Windows
    // Forms' TreeView (the platform reports it as text: there is no keypad
    // asterisk in PhysicalKey).
    if (event.text_utf8 == "*" && type_prefix_.empty()) {
        if (current_node_) expand_subtree(nodes_[*current_node_].stable_id);
        event.handled = true;
        return;
    }
    type_select(event.text_utf8);
    event.handled = true;
}

void TreeListView::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused) {
        if (resizing_column_) finish_column_resize();
        if (pressed_column_) end_header_press();
    }
    if (focused_ && !current_node_ && !visible_.empty()) {
        current_node_ = selected_node_ ? *selected_node_ : visible_[top_row()];
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics -----------------------------------------------------------------------

SemanticDescriptor TreeListView::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    // A tree grid (12.13): its rows are tree items, each holding a cell per
    // column. The size counts every shown row and every column, realized or
    // not.
    const Layout placed = layout();
    descriptor.role = SemanticRole::tree_grid;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.row_count = placed.shown_rows;
    descriptor.column_count = column_order_.size();
    if (selected_node_) descriptor.value = nodes_[*selected_node_].text;
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> TreeListView::semantic_virtual_children() const {
    std::vector<SemanticNode> children{};
    const Layout placed = layout();
    const Rect absolute = absolute_bounds();
    const std::string view_id(stable_id().value());
    const bool enabled = effectively_enabled();
    const SemanticState enabled_state = enabled ? SemanticState::enabled : SemanticState::none;
    // Column headers first, in display order; a header's press sorts.
    for (const std::size_t column : column_order_) {
        if (!column_visible(placed, column)) continue;
        const Rect bounds = Rect::intersection(column_cell(placed, column), placed.header);
        SemanticNode node{};
        node.stable_id = column_identity(view_id, columns_[column].stable_id);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::column_header;
        node.column_index = column_position_[column] + 1U;
        node.name = columns_[column].text;
        if (sort_column_ == column && sort_order_ == TreeListSortOrder::ascending) node.value = "ascending";
        if (sort_column_ == column && sort_order_ == TreeListSortOrder::descending) node.value = "descending";
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible | enabled_state;
        node.actions = {SemanticAction::press};
        children.push_back(std::move(node));
    }
    const std::size_t end = end_realized(placed);
    for (std::size_t row = placed.top_row; row < end; ++row) {
        const std::size_t index = visible_[row];
        const TreeListNode& item = nodes_[index];
        const Rect bounds = Rect::intersection(row_bounds(placed, row), placed.viewport);
        SemanticNode node{};
        node.stable_id = item.stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::tree_item;
        node.name = item.text;
        node.level = item.depth + 1U;
        node.row_index = row + 1U;
        append_row_cells(placed, row, node);
        const std::size_t slot = parent_[index] == no_index ? 0U : parent_[index] + 1U;
        node.position_in_set = sibling_position_[index] + 1U;
        node.set_size = child_begin_[slot + 1U] - child_begin_[slot];
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (enabled && item.enabled) node.states |= SemanticState::enabled;
        if (selected_node_ == index) node.states |= SemanticState::selected;
        if (focused_ && current_node_ == index) node.states |= SemanticState::focused;
        if (can_expand(index) && item.expanded) node.states |= SemanticState::expanded;
        node.actions = {SemanticAction::focus, SemanticAction::select, SemanticAction::press};
        if (can_expand(index)) {
            node.actions.push_back(item.expanded ? SemanticAction::collapse : SemanticAction::expand);
        }
        children.push_back(std::move(node));
    }
    return children;
}

// A shown row's cells, one per column in display order, for the columns
// whose extent meets the viewport (the realized columns). A cell has no
// actions of its own; its row takes them.
void TreeListView::append_row_cells(const Layout& placed, std::size_t row, SemanticNode& row_node) const {
    const TreeListNode& item = nodes_[visible_[row]];
    const Rect absolute = absolute_bounds();
    const Rect line = row_bounds(placed, row);
    const bool enabled = effectively_enabled() && item.enabled;
    const SemanticState enabled_state = enabled ? SemanticState::enabled : SemanticState::none;
    for (const std::size_t column : column_order_) {
        const Rect extent = column_cell(placed, column);
        const Rect cell_rect{extent.x, line.y, extent.width, line.height};
        const Rect bounds = Rect::intersection(cell_rect, placed.viewport);
        if (bounds.empty()) continue;
        SemanticNode cell{};
        cell.stable_id = item.stable_id + ".cell." + columns_[column].stable_id;
        cell.runtime_id = virtual_semantic_runtime_id(cell.stable_id);
        cell.role = SemanticRole::cell;
        cell.name = columns_[column].text;
        cell.value = std::string(column_text(item, column));
        cell.row_index = row + 1U;
        cell.column_index = column_position_[column] + 1U;
        cell.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        cell.states = SemanticState::visible | enabled_state;
        row_node.children.push_back(std::move(cell));
    }
}

bool TreeListView::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    if (!effectively_enabled()) return false;
    const std::string view_id(stable_id().value());
    for (std::size_t column = 0; column < columns_.size(); ++column) {
        if (id != column_identity(view_id, columns_[column].stable_id)) continue;
        if (action != SemanticAction::press) return false;
        toggle_sort(column);
        return true;
    }
    const std::optional<std::size_t> row = row_of(id);
    if (!row) return false;
    const std::size_t index = visible_[*row];
    const Control::Ptr lifetime = shared_from_this();
    if (action == SemanticAction::expand || action == SemanticAction::collapse) {
        if (!can_expand(index)) return false;
        set_expanded(id, action == SemanticAction::expand);
        return true;
    }
    if (action != SemanticAction::focus && action != SemanticAction::select && action != SemanticAction::press) {
        return false;
    }
    if (window() != nullptr) static_cast<void>((*window()).request_focus(lifetime));
    if (!is_alive()) return true;
    if (action == SemanticAction::focus) {
        current_node_ = index;
        ensure_row_visible(*row);
        invalidate(Dirty::paint | Dirty::semantics);
    } else if (action == SemanticAction::select) {
        choose_row(*row);
    } else {
        activate_row(*row);
    }
    return true;
}

// -- anatomy -------------------------------------------------------------------------

namespace {

constexpr std::array<PartField<TreeListView::Parts>, 7> tree_list_view_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &TreeListView::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &TreeListView::Parts::well},
    {.declaration = {.name = "header", .role = PartRole::face, .composed_from = {}},
     .member = &TreeListView::Parts::header},
    // The child scroll bars, each a whole scroll bar system. A ranged path
    // is the nearest role to a composed scroll bar.
    {.declaration = {.name = "vertical_scroll_bar", .role = PartRole::track, .composed_from = "v_scroll_bar"},
     .member = &TreeListView::Parts::vertical_scroll_bar},
    {.declaration = {.name = "horizontal_scroll_bar", .role = PartRole::track,
                     .composed_from = "h_scroll_bar"},
     .member = &TreeListView::Parts::horizontal_scroll_bar},
    {.declaration = {.name = "scroll_corner", .role = PartRole::face, .composed_from = {}},
     .member = &TreeListView::Parts::scroll_corner},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &TreeListView::Parts::focus},
}};

constexpr std::array<PartField<TreeListView::RowParts>, 6> tree_list_view_row_part_fields{{
    {.declaration = {.name = "row", .role = PartRole::region, .composed_from = {}},
     .member = &TreeListView::RowParts::row},
    {.declaration = {.name = "row_face", .role = PartRole::face, .composed_from = {}},
     .member = &TreeListView::RowParts::row_face},
    {.declaration = {.name = "expander", .role = PartRole::disclosure, .composed_from = {}},
     .member = &TreeListView::RowParts::expander},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &TreeListView::RowParts::icon},
    {.declaration = {.name = "text", .role = PartRole::content, .composed_from = {}},
     .member = &TreeListView::RowParts::text},
    {.declaration = {.name = "row_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &TreeListView::RowParts::row_focus},
}};

constexpr std::array<StateField<TreeListView::RowStates>, 7> tree_list_view_row_state_fields{{
    {.state = AnatomyState::selected, .member = &TreeListView::RowStates::selected},
    {.state = AnatomyState::hot, .member = &TreeListView::RowStates::hot},
    {.state = AnatomyState::current, .member = &TreeListView::RowStates::current},
    {.state = AnatomyState::open, .member = &TreeListView::RowStates::open},
    {.state = AnatomyState::closed, .member = &TreeListView::RowStates::closed},
    {.state = AnatomyState::disabled, .member = &TreeListView::RowStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &TreeListView::RowStates::focus_visible},
}};

constexpr std::array<PartField<TreeListView::CellParts>, 2> tree_list_view_cell_part_fields{{
    {.declaration = {.name = "cell", .role = PartRole::region, .composed_from = {}},
     .member = &TreeListView::CellParts::cell},
    {.declaration = {.name = "cell_text", .role = PartRole::content, .composed_from = {}},
     .member = &TreeListView::CellParts::cell_text},
}};

constexpr std::array<StateField<TreeListView::CellStates>, 2> tree_list_view_cell_state_fields{{
    {.state = AnatomyState::selected, .member = &TreeListView::CellStates::selected},
    {.state = AnatomyState::disabled, .member = &TreeListView::CellStates::disabled},
}};

constexpr std::array<PartField<TreeListView::ColumnParts>, 5> tree_list_view_column_part_fields{{
    {.declaration = {.name = "column_header", .role = PartRole::face, .composed_from = {}},
     .member = &TreeListView::ColumnParts::column_header},
    {.declaration = {.name = "column_label", .role = PartRole::label, .composed_from = {}},
     .member = &TreeListView::ColumnParts::column_label},
    {.declaration = {.name = "sort_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &TreeListView::ColumnParts::sort_glyph},
    {.declaration = {.name = "column_divider", .role = PartRole::divider, .composed_from = {}},
     .member = &TreeListView::ColumnParts::column_divider},
    {.declaration = {.name = "column_grip", .role = PartRole::grip, .composed_from = {}},
     .member = &TreeListView::ColumnParts::column_grip},
}};

constexpr std::array<StateField<TreeListView::ColumnStates>, 3> tree_list_view_column_state_fields{{
    {.state = AnatomyState::hot, .member = &TreeListView::ColumnStates::hot},
    {.state = AnatomyState::pressed, .member = &TreeListView::ColumnStates::pressed},
    {.state = AnatomyState::captured, .member = &TreeListView::ColumnStates::captured},
}};

constexpr std::array<CustomStateDeclaration, 2> tree_list_view_column_custom_states{{
    {.name = "sorted_ascending"},
    {.name = "sorted_descending"},
}};

constexpr std::array<PartDeclaration, 7> tree_list_view_parts = part_declarations(tree_list_view_part_fields);
constexpr std::array<PartDeclaration, 6> tree_list_view_row_parts =
    part_declarations(tree_list_view_row_part_fields);
constexpr std::array<AnatomyState, 7> tree_list_view_row_states =
    state_declarations(tree_list_view_row_state_fields);
constexpr std::array<PartDeclaration, 2> tree_list_view_cell_parts =
    part_declarations(tree_list_view_cell_part_fields);
constexpr std::array<AnatomyState, 2> tree_list_view_cell_states =
    state_declarations(tree_list_view_cell_state_fields);
constexpr std::array<PartDeclaration, 5> tree_list_view_column_parts =
    part_declarations(tree_list_view_column_part_fields);
constexpr std::array<AnatomyState, 3> tree_list_view_column_states =
    state_declarations(tree_list_view_column_state_fields);
constexpr std::array<AnatomyState, 5> tree_list_view_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ItemSystemIndex row_system{0U};
constexpr ItemSystemIndex cell_system{1U};
constexpr ItemSystemIndex column_system{2U};

constexpr std::array<ItemSystemDeclaration, 3> tree_list_view_item_systems{{
    {.name = "row", .parts = tree_list_view_row_parts, .states = tree_list_view_row_states, .custom_states = {}},
    {.name = "cell", .parts = tree_list_view_cell_parts, .states = tree_list_view_cell_states,
     .custom_states = {}},
    {.name = "column", .parts = tree_list_view_column_parts, .states = tree_list_view_column_states,
     .custom_states = tree_list_view_column_custom_states},
}};

constexpr ControlAnatomy tree_list_view_anatomy{
    .control_id = "tree_list_view",
    .parts = tree_list_view_parts,
    .states = tree_list_view_states,
    .custom_states = {},
    .item_systems = tree_list_view_item_systems,
};

} // namespace

const ControlAnatomy& TreeListView::anatomy() const noexcept {
    return tree_list_view_anatomy;
}

void TreeListView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    Parts record;
    record.region = PartGeometry::from_bounds(placed.well);
    record.well = PartGeometry::from_bounds(placed.well);
    record.header = PartGeometry::from_bounds(placed.header);
    record.vertical_scroll_bar = PartGeometry::from_bounds(placed.vertical_bar);
    record.horizontal_scroll_bar = PartGeometry::from_bounds(placed.horizontal_bar);
    record.scroll_corner = PartGeometry::from_bounds(placed.corner);
    record.focus = PartGeometry::from_bounds(placed.well);
    write_parts(anatomy(), tree_list_view_part_fields, record, geometry);
}

TreeListView::Parts TreeListView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), tree_list_view_part_fields, published.parts(), record);
    return record;
}

std::size_t TreeListView::realized_row_count(const Layout& placed) const noexcept {
    const std::size_t result = end_realized(placed) - std::min(placed.top_row, end_realized(placed));
    return result;
}

// Cells are realized row by row, across the visible columns after the tree
// column (which is always first in display order).
std::size_t TreeListView::realized_cell_count(const Layout& placed) const noexcept {
    if (columns_.size() < 2U) return 0U;
    std::size_t columns = visible_column_count(placed);
    if (column_visible(placed, 0U)) --columns;
    const std::size_t result = realized_row_count(placed) * columns;
    return result;
}

std::size_t TreeListView::realized_cell_column(const Layout& placed, std::size_t position) const noexcept {
    const std::size_t skip = column_visible(placed, 0U) ? 1U : 0U;
    const std::size_t result = visible_column(placed, position + skip);
    return result;
}

std::size_t TreeListView::realized_item_count() const {
    const Layout placed = layout();
    const std::size_t result =
        realized_row_count(placed) + realized_cell_count(placed) + visible_column_count(placed);
    return result;
}

TreeListView::RealizedSlot TreeListView::realized_slot(const Layout& placed, std::size_t item) const {
    const std::size_t rows = realized_row_count(placed);
    const std::size_t cells = realized_cell_count(placed);
    RealizedSlot slot;
    if (item < rows) {
        slot.system = row_system;
        slot.row = placed.top_row + item;
        return slot;
    }
    if (item < rows + cells) {
        // cells is rows times the visible columns after the tree column, so
        // a nonzero count has rows and a whole number of cells per row.
        const std::size_t per_row = cells / rows;
        const std::size_t position = item - rows;
        slot.system = cell_system;
        slot.row = placed.top_row + position / per_row;
        slot.column = realized_cell_column(placed, position % per_row);
        return slot;
    }
    slot.system = column_system;
    slot.column = visible_column(placed, item - rows - cells);
    return slot;
}

std::string TreeListView::realized_item_stable_id(std::size_t item) const {
    const Layout placed = layout();
    const RealizedSlot slot = realized_slot(placed, item);
    if (slot.system == column_system) {
        std::string identity = column_identity(stable_id().value(), columns_[slot.column].stable_id);
        return identity;
    }
    const std::string& node_id = nodes_[visible_[slot.row]].stable_id;
    if (slot.system == row_system) return node_id;
    std::string identity = cell_identity(node_id, columns_[slot.column].stable_id);
    return identity;
}

ItemSystemIndex TreeListView::publish_realized_item_system(std::size_t item) const {
    const Layout placed = layout();
    const RealizedSlot slot = realized_slot(placed, item);
    return slot.system;
}

void TreeListView::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    const RealizedSlot slot = realized_slot(placed, item);
    const ItemSystemDeclaration& system = anatomy().item_systems[slot.system.value];
    if (slot.system == row_system) {
        const RowParts record = row_parts(placed, slot.row, placed.viewport);
        write_item_parts(system, tree_list_view_row_part_fields, record, geometry);
    } else if (slot.system == cell_system) {
        const CellParts record = cell_parts(placed, slot.row, slot.column, placed.viewport);
        write_item_parts(system, tree_list_view_cell_part_fields, record, geometry);
    } else {
        const ColumnParts record = column_parts(placed, slot.column, placed.header);
        write_item_parts(system, tree_list_view_column_part_fields, record, geometry);
    }
}

AnatomyStateSet TreeListView::realized_item_anatomy_states(std::size_t item) const {
    const Layout placed = layout();
    const RealizedSlot slot = realized_slot(placed, item);
    if (slot.system == row_system) return row_states(slot.row);
    if (slot.system == cell_system) return cell_states(slot.row);
    return column_states(slot.column);
}

std::vector<TreeListView::Row> TreeListView::realized_rows() const {
    const Layout placed = layout();
    const std::size_t count = realized_row_count(placed);
    std::vector<Row> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Row record;
        record.row = placed.top_row + item;
        record.index = visible_[record.row];
        record.stable_id = nodes_[record.index].stable_id;
        read_item_parts(anatomy().item_systems[row_system.value], tree_list_view_row_part_fields,
                        published.parts(), record.parts);
        read_states(tree_list_view_row_state_fields, row_states(record.row), record.states);
        records.push_back(record);
    }
    return records;
}

std::vector<TreeListView::Cell> TreeListView::realized_cells() const {
    const Layout placed = layout();
    const std::size_t rows = realized_row_count(placed);
    const std::size_t count = realized_cell_count(placed);
    std::vector<Cell> records;
    records.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        const std::size_t item = rows + position;
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        const RealizedSlot slot = realized_slot(placed, item);
        Cell record;
        record.row = slot.row;
        record.column = slot.column;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems[cell_system.value], tree_list_view_cell_part_fields,
                        published.parts(), record.parts);
        read_states(tree_list_view_cell_state_fields, cell_states(record.row), record.states);
        records.push_back(record);
    }
    return records;
}

std::vector<TreeListView::Column> TreeListView::realized_columns() const {
    const Layout placed = layout();
    const std::size_t count = visible_column_count(placed);
    std::vector<Column> records;
    records.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        const std::size_t column = visible_column(placed, position);
        Column record;
        record.index = column;
        record.display_position = column_position_[column];
        record.stable_id = column_identity(stable_id().value(), columns_[column].stable_id);
        record.parts = column_parts(placed, column, placed.header);
        const AnatomyStateSet states = column_states(column);
        read_states(tree_list_view_column_state_fields, states, record.states);
        record.states.sorted_ascending = states.contains(sorted_ascending_state);
        record.states.sorted_descending = states.contains(sorted_descending_state);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events TreeListView adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 6> tree_list_view_interactions{{
    {.name = "selection_changed", .payload = "TreeListSelectionChange", .mutable_payload = false},
    {.name = "expansion_changed", .payload = "TreeListExpansionChange", .mutable_payload = false},
    {.name = "item_activated", .payload = "TreeListActivation", .mutable_payload = false},
    {.name = "sort_changed", .payload = "TreeListSortChange", .mutable_payload = false},
    {.name = "column_width_changed", .payload = "TreeListColumnWidthChange", .mutable_payload = false},
    {.name = "column_moved", .payload = "TreeListColumnMove", .mutable_payload = false},
}};

} // namespace

void TreeListView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), tree_list_view_interactions.begin(), tree_list_view_interactions.end());
}

// The image list's hooks: a list must be alive and, once this view is in a
// window, that window's; a new list is subscribed to for its changes.
void TreeListView::prepare_write(const decltype(image_list)&, std::shared_ptr<ImageList>& incoming) {
    require_mutable();
    const Window* const host = window();
    if (incoming && (!(*incoming).is_alive() || (host != nullptr && !(*incoming).belongs_to(*host)))) {
        throw std::invalid_argument("TreeListView requires a live ImageList of its own Window");
    }
}

void TreeListView::written(const decltype(image_list)&) {
    images_changed_.disconnect();
    const std::shared_ptr<ImageList>& images = image_list.read();
    if (images) {
        const Delegate<const ImageListChange&> changed =
            Delegate<const ImageListChange&>::bind<TreeListView, &TreeListView::image_list_changed>(*this);
        images_changed_ = (*images).changed().subscribe(*this, changed);
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// The selection's view: selects a node (expanding its ancestors and
// scrolling it into view) or nothing, moves the current node with it, and
// raises selection_changed. Throws std::out_of_range for an unknown node.
void TreeListView::write_view(const decltype(selected_id)&, std::optional<std::string> incoming) {
    require_mutable();
    const std::optional<std::string> previous = selected_copy();
    if (!incoming) {
        if (!selected_node_) return;
        selected_node_.reset();
        publish_selection(previous);
        return;
    }
    const std::size_t index = require_index(*incoming, "TreeListView selection names an unknown node");
    std::vector<TreeListExpansionChange> changes;
    expand_ancestors(index, changes);
    const bool changed = selected_node_ != index;
    selected_node_ = index;
    current_node_ = index;
    ensure_row_visible(row_of_node_[index]);
    invalidate(Dirty::paint | Dirty::semantics);
    publish_expansions(changes);
    if (changed && is_alive()) publish_selection(previous);
}

std::optional<std::string> TreeListView::read_view(const decltype(selected_id)&) const {
    std::optional<std::string> current = selected_copy();
    return current;
}

// property_codemod begin TreeListView (tools/property_families/tree_list_view.json; generated, do not edit)

void TreeListView::prepare_write(const decltype(indentation)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < minimum_indentation || incoming > maximum_indentation) {
        throw std::invalid_argument("TreeListView indentation is outside the indentation bounds");
    }
}

void TreeListView::written(const decltype(indentation)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void TreeListView::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("TreeListView font specification is invalid");
    set_font_from_code(true);
}

void TreeListView::written(const decltype(font)&) {
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    on_font_changed();
}

void TreeListView::on_font_changed() {}

// property_codemod end TreeListView

} // namespace gui_forms
