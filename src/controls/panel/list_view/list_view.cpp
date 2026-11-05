#include "gui_forms/controls/panel/list_view/list_view.hpp"

#include "../collection_control_utilities.hpp"
#include "../cut_off_text/cut_off_text_reveal.hpp"
#include "../details_columns.hpp"

#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/range_control/scroll_bar/h_scroll_bar/h_scroll_bar.hpp"
#include "gui_forms/controls/range_control/scroll_bar/v_scroll_bar/v_scroll_bar.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// View geometry in logical units at text scale 1. The scroll bar extent is
// not scaled: it matches ScrollBar's own arrow extent.
constexpr double edge_inset = 2.0;
constexpr double cell_padding = 4.0;
// How far before its text a cut-off text's reveal starts.
constexpr double reveal_lead = 2.0;
constexpr double check_size = 13.0;
constexpr double check_mark_inset = 3.0;
constexpr double minimum_row_height = 18.0;
constexpr double row_text_padding = 6.0;
constexpr double sub_item_padding = 6.0;
constexpr double list_cell_width = 160.0;
constexpr double large_cell_width = 80.0;
// The default tile: the large icon, then this much text beside it, as tall
// as the icon or three lines of text, whichever is taller.
constexpr double tile_text_width = 168.0;
constexpr std::size_t tile_text_lines = 3U;
// A group header line: a line of bold text and this padding.
constexpr double group_header_padding = 10.0;
constexpr double group_caption_inset = 4.0;
constexpr double default_small_icon = 16.0;
constexpr double default_large_icon = 32.0;
constexpr double divider_reach = 4.0;
// The insertion mark's thickness across its gap, at text scale 1.
constexpr double insertion_thickness = 2.0;
constexpr double horizontal_key_step = 16.0;
constexpr double scroll_bar_extent = 17.0;
constexpr std::ptrdiff_t wheel_lines = 3;

std::size_t ceiling_quotient(std::size_t count, std::size_t divisor) noexcept {
    if (divisor == 0U) return 0U;
    const std::size_t result = count / divisor + (count % divisor != 0U ? 1U : 0U);
    return result;
}

// Whole lines of `extent` that fit in `available`, at least one.
std::size_t fitting_lines(double available, double extent) noexcept {
    if (extent <= 0.0 || available <= 0.0) return 1U;
    const double lines = std::floor(available / extent);
    const std::size_t result = std::max<std::size_t>(1U, static_cast<std::size_t>(lines));
    return result;
}

// Lines of `extent` at least partly visible in `available`.
std::size_t touched_lines(double available, double extent) noexcept {
    if (extent <= 0.0 || available <= 0.0) return 0U;
    const double lines = std::ceil(available / extent);
    const std::size_t result = static_cast<std::size_t>(lines);
    return result;
}

PartTextAlignment text_alignment(ListViewAlignment alignment) noexcept {
    if (alignment == ListViewAlignment::center) return PartTextAlignment::center;
    if (alignment == ListViewAlignment::right) return PartTextAlignment::end;
    return PartTextAlignment::start;
}

bool valid_alignment(ListViewAlignment alignment) noexcept {
    const bool result = alignment == ListViewAlignment::left ||
                        alignment == ListViewAlignment::center ||
                        alignment == ListViewAlignment::right;
    return result;
}

bool valid_view(ListViewView view) noexcept {
    const bool result = view == ListViewView::details || view == ListViewView::list ||
                        view == ListViewView::small_icon || view == ListViewView::large_icon ||
                        view == ListViewView::tile;
    return result;
}

bool valid_group(const ListViewGroup& group) {
    const bool result = valid_stable_id(group.stable_id) &&
                        validate_utf8(group.header).valid() && valid_alignment(group.header_alignment);
    return result;
}

bool valid_tile_side(double side) noexcept {
    const bool result = std::isfinite(side) && side >= ListView::minimum_tile_side &&
                        side <= ListView::maximum_tile_side;
    return result;
}

bool valid_item_text(const ListViewItem& item) {
    if (!validate_utf8(item.text).valid() || !validate_utf8(item.image_key).valid() ||
        !validate_utf8(item.group).valid()) {
        return false;
    }
    for (const std::string& sub_item : item.sub_items) {
        if (!validate_utf8(sub_item).valid()) return false;
    }
    return true;
}

// The text an item shows in one column: its text in the first, then its
// sub-items; empty past the last sub-item.
std::string_view column_text(const ListViewItem& item, std::size_t column) noexcept {
    if (column == 0U) return item.text;
    if (column - 1U < item.sub_items.size()) return item.sub_items[column - 1U];
    return {};
}

// The lines a tile shows: the item's text, then one per column after the
// first whose sub-item is not empty, at most `fitting`.
std::size_t tile_line_count(const ListViewItem& item, std::size_t columns, std::size_t fitting) noexcept {
    std::size_t lines = 1U;
    for (std::size_t column = 1; column < columns && lines < fitting; ++column) {
        if (!column_text(item, column).empty()) ++lines;
    }
    return lines;
}

// The order sort_items applies to item positions: by one column's text,
// ascending or descending. Equal texts keep their order (stable sort).
struct ColumnTextOrder final {
    const std::vector<ListViewItem>* items{};
    std::size_t column{};
    bool descending{};

    bool operator()(std::size_t left, std::size_t right) const noexcept {
        const std::string_view a = column_text((*items)[left], column);
        const std::string_view b = column_text((*items)[right], column);
        if (descending) return details_detail::folded_text_less(b, a);
        return details_detail::folded_text_less(a, b);
    }
};

ImageVisualState image_state(AnatomyStateSet states) noexcept {
    if (states.contains(AnatomyState::disabled)) return ImageVisualState::disabled;
    if (states.contains(AnatomyState::selected)) return ImageVisualState::selected;
    if (states.contains(AnatomyState::hot)) return ImageVisualState::hot;
    return ImageVisualState::normal;
}

std::string column_identity(std::string_view list_id, std::string_view column_id) {
    std::string identity(list_id);
    identity.append(".column.");
    identity.append(column_id);
    return identity;
}

std::string sub_item_identity(std::string_view item_id, std::string_view column_id) {
    std::string identity(item_id);
    identity.append(".sub.");
    identity.append(column_id);
    return identity;
}

} // namespace

ListViewItemSource::~ListViewItemSource() {}

ListView::ListView(StableId stable_id)
    : Control(std::move(stable_id)), reveal_(std::make_unique<detail::CutOffTextRevealer>()) {
    set_focusable(true);
    set_paint_plane(PaintPlane::control);
}

ListView::~ListView() {}

void ListView::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    vertical_bar_ = make_control<VScrollBar>(StableId(prefix + ".vertical-scroll-bar"));
    horizontal_bar_ = make_control<HScrollBar>(StableId(prefix + ".horizontal-scroll-bar"));
    // The bars scroll with the pointer; the keyboard scrolls the view itself.
    (*vertical_bar_).set_focusable(false);
    (*horizontal_bar_).set_focusable(false);
    add_child(vertical_bar_);
    add_child(horizontal_bar_);
    vertical_scrolled_ = (*vertical_bar_).value_changed().subscribe(
        *this, Delegate<double>::bind<ListView, &ListView::on_vertical_scroll>(*this));
    horizontal_scrolled_ = (*horizontal_bar_).value_changed().subscribe(
        *this, Delegate<double>::bind<ListView, &ListView::on_horizontal_scroll>(*this));
    set_child_layout_collapsed(vertical_bar_, true);
    set_child_layout_collapsed(horizontal_bar_, true);
}

// -- model -------------------------------------------------------------------

void ListView::set_view(ListViewView view) {
    require_mutable();
    if (!valid_view(view)) throw std::invalid_argument("ListView view is invalid");
    if (view_ == view) return;
    view_ = view;
    hot_index_.reset();
    conceal_cut_off_text();
    ++model_revision_;
    hot_column_.reset();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListView::set_columns(std::vector<ListViewColumn> columns) {
    require_mutable();
    if (columns.size() > maximum_columns) {
        throw std::invalid_argument("ListView has more columns than maximum_columns");
    }
    std::unordered_set<std::string> identities{};
    for (const ListViewColumn& column : columns) {
        if (!valid_stable_id(column.stable_id) ||
            !validate_utf8(column.text).valid() || !identities.insert(column.stable_id).second) {
            throw std::invalid_argument(
                "ListView columns require unique stable IDs following the StableId segment rule and valid UTF-8");
        }
        if (!std::isfinite(column.width) || column.width < minimum_column_width ||
            column.width > maximum_column_width || !valid_alignment(column.alignment)) {
            throw std::invalid_argument("ListView column width or alignment is invalid");
        }
    }
    if (resizing_column_ && has_pointer_capture()) set_pointer_capture(false);
    resizing_column_.reset();
    pressed_column_.reset();
    hot_column_.reset();
    columns_ = std::move(columns);
    ++model_revision_;
    if (sort_column_ && *sort_column_ >= columns_.size()) {
        sort_column_.reset();
        sort_order_ = ListViewSortOrder::none;
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListView::set_column_width(std::size_t column, double width) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("ListView column index is past the end");
    if (!std::isfinite(width) || width < minimum_column_width || width > maximum_column_width) {
        throw std::invalid_argument("ListView column width is outside the column width bounds");
    }
    if (columns_[column].width == width) return;
    columns_[column].width = width;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const ListViewColumnWidthChange change{std::string(stable_id().value()), column, width};
    publish_change(column_width_changed_, change);
}

std::size_t ListView::item_count() const noexcept {
    if (source_) return virtual_count_;
    return items_.size();
}

std::optional<std::size_t> ListView::index_of(std::string_view item_id) const {
    if (source_) return std::nullopt;
    const ItemIndices::const_iterator found = item_indices_.find(std::string(item_id));
    if (found == item_indices_.end()) return std::nullopt;
    return (*found).second;
}

void ListView::set_items(std::vector<ListViewItem> items) {
    require_mutable();
    if (source_) throw std::logic_error("ListView items cannot be replaced in virtual mode");
    if (items.size() > maximum_items) throw std::invalid_argument("ListView has more items than maximum_items");
    ItemIndices next_indices{};
    next_indices.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        const ListViewItem& item = items[index];
        if (!valid_stable_id(item.stable_id) || !valid_item_text(item)) {
            throw std::invalid_argument(
                "ListView items require stable IDs following the StableId segment rule and valid UTF-8");
        }
        const std::pair<ItemIndices::iterator, bool> inserted = next_indices.emplace(item.stable_id, index);
        if (!inserted.second) throw std::invalid_argument("ListView item IDs must be unique");
    }
    // Carry the selection, focus, anchor and hot item over by stable ID.
    std::vector<std::uint8_t> next_selected(items.size(), 0U);
    std::size_t next_count = 0U;
    bool moved = false;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (selected_[index] == 0U) continue;
        const ItemIndices::const_iterator found = next_indices.find(items_[index].stable_id);
        if (found == next_indices.end()) continue;
        next_selected[(*found).second] = 1U;
        ++next_count;
        if ((*found).second != index) moved = true;
    }
    const bool selection_changed = moved || next_count != selected_count_;
    std::optional<std::size_t> next_focus{};
    std::optional<std::size_t> next_anchor{};
    if (focused_index_) {
        const ItemIndices::const_iterator found = next_indices.find(items_[*focused_index_].stable_id);
        if (found != next_indices.end()) next_focus = (*found).second;
    }
    if (anchor_index_) {
        const ItemIndices::const_iterator found = next_indices.find(items_[*anchor_index_].stable_id);
        if (found != next_indices.end()) next_anchor = (*found).second;
    }
    items_.swap(items);
    item_indices_.swap(next_indices);
    selected_.swap(next_selected);
    selected_count_ = next_count;
    focused_index_ = next_focus;
    anchor_index_ = next_anchor;
    hot_index_.reset();
    conceal_cut_off_text();
    ++model_revision_;
    insertion_mark_.reset();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    // An edit in progress is cancelled once the items have changed.
    if (label_edit_index_) {
        cancel_label_edit();
        if (!is_alive()) return;
    }
    if (selection_changed) publish_selection();
}

void ListView::reset_after_model_change(std::size_t count) {
    selected_.resize(count, 0U);
    std::size_t next_count = 0U;
    for (const std::uint8_t flag : selected_) {
        if (flag != 0U) ++next_count;
    }
    const bool selection_changed = next_count != selected_count_;
    selected_count_ = next_count;
    if (focused_index_ && *focused_index_ >= count) focused_index_.reset();
    if (anchor_index_ && *anchor_index_ >= count) anchor_index_.reset();
    hot_index_.reset();
    conceal_cut_off_text();
    ++model_revision_;
    insertion_mark_.reset();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    // An edit in progress is cancelled once the items have changed.
    if (label_edit_index_) {
        cancel_label_edit();
        if (!is_alive()) return;
    }
    if (selection_changed) publish_selection();
}

void ListView::set_virtual_source(std::shared_ptr<ListViewItemSource> source) {
    require_mutable();
    std::size_t count = 0U;
    if (source) {
        count = (*source).item_count();
        if (count > maximum_items) throw std::invalid_argument("ListView source has more items than maximum_items");
    }
    // A new source is a new model: nothing carries over.
    items_.clear();
    item_indices_.clear();
    source_ = std::move(source);
    virtual_count_ = count;
    std::fill(selected_.begin(), selected_.end(), static_cast<std::uint8_t>(0U));
    focused_index_.reset();
    anchor_index_.reset();
    top_item_ = 0U;
    reset_after_model_change(count);
}

void ListView::refresh_virtual_items() {
    require_mutable();
    if (!source_) throw std::logic_error("ListView is not in virtual mode");
    const std::size_t count = (*source_).item_count();
    if (count > maximum_items) throw std::invalid_argument("ListView source has more items than maximum_items");
    virtual_count_ = count;
    reset_after_model_change(count);
}

const ListViewItem& ListView::item_at(std::size_t index, ListViewItem& scratch) const {
    if (!source_) return items_[index];
    (*source_).retrieve_item(index, scratch);
    if (!valid_stable_id(scratch.stable_id)) {
        scratch.stable_id = "#" + std::to_string(index);
    }
    if (!valid_item_text(scratch)) {
        scratch.text.clear();
        scratch.sub_items.clear();
        scratch.image_key.clear();
    }
    return scratch;
}

// -- checks, selection and focus ----------------------------------------------

void ListView::set_check_boxes(bool shown) {
    require_mutable();
    if (check_boxes_ == shown) return;
    check_boxes_ = shown;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

bool ListView::item_checked(std::size_t index) const {
    if (index >= item_count()) throw std::out_of_range("ListView item index is past the end");
    ListViewItem scratch;
    const ListViewItem& item = item_at(index, scratch);
    return item.checked;
}

void ListView::set_item_checked(std::size_t index, bool checked) {
    require_mutable();
    if (source_) throw std::logic_error("A virtual ListView's source owns the checked state");
    if (index >= items_.size()) throw std::out_of_range("ListView item index is past the end");
    ListViewItem& item = items_[index];
    if (item.checked == checked) return;
    const ListViewItemCheck change{std::string(stable_id().value()), index, item.stable_id, item.checked,
                                   checked, false};
    item.checked = checked;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(item_checked_, change);
}

void ListView::toggle_check(std::size_t index) {
    if (!check_boxes_ || index >= item_count()) return;
    ListViewItem scratch;
    const ListViewItem& item = item_at(index, scratch);
    ListViewItemCheck check{std::string(stable_id().value()), index, item.stable_id, item.checked,
                            !item.checked, false};
    const Control::Ptr lifetime = shared_from_this();
    item_checking_.emit(check);
    // A handler may cancel, dispose the view or replace its items.
    if (!is_alive() || check.cancel || index >= item_count()) return;
    if (!source_) {
        if (items_[index].stable_id != check.item_id || items_[index].checked == check.new_value) return;
        items_[index].checked = check.new_value;
    }
    invalidate(Dirty::paint | Dirty::semantics);
    item_checked_.emit(check);
}

void ListView::set_multi_select(bool multiple) {
    require_mutable();
    if (multi_select_ == multiple) return;
    multi_select_ = multiple;
    if (multiple || selected_count_ <= 1U) {
        invalidate(Dirty::semantics);
        return;
    }
    std::size_t keep = 0U;
    if (focused_index_ && selected_[*focused_index_] != 0U) {
        keep = *focused_index_;
    } else {
        while (keep < selected_.size() && selected_[keep] == 0U) ++keep;
    }
    static_cast<void>(select_only(keep));
    anchor_index_ = keep;
    publish_selection();
}

void ListView::set_full_row_select(bool full_row) {
    require_mutable();
    if (full_row_select_ == full_row) return;
    full_row_select_ = full_row;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

std::vector<std::size_t> ListView::selected_indices() const {
    std::vector<std::size_t> indices;
    indices.reserve(selected_count_);
    for (std::size_t index = 0; index < selected_.size(); ++index) {
        if (selected_[index] != 0U) indices.push_back(index);
    }
    return indices;
}

bool ListView::is_selected(std::size_t index) const {
    if (index >= item_count()) throw std::out_of_range("ListView item index is past the end");
    const bool result = selected_[index] != 0U;
    return result;
}

bool ListView::select_only(std::size_t index) {
    if (selected_count_ == 1U && selected_[index] != 0U) return false;
    if (selected_count_ != 0U) std::fill(selected_.begin(), selected_.end(), static_cast<std::uint8_t>(0U));
    selected_[index] = 1U;
    selected_count_ = 1U;
    return true;
}

bool ListView::select_none() {
    if (selected_count_ == 0U) return false;
    std::fill(selected_.begin(), selected_.end(), static_cast<std::uint8_t>(0U));
    selected_count_ = 0U;
    return true;
}

bool ListView::toggle_selected(std::size_t index) {
    if (selected_[index] != 0U) {
        selected_[index] = 0U;
        --selected_count_;
        return true;
    }
    if (!multi_select_) return select_only(index);
    selected_[index] = 1U;
    ++selected_count_;
    return true;
}

bool ListView::assign_selected(std::size_t index, bool selected) {
    const bool current = selected_[index] != 0U;
    if (current == selected) return false;
    if (selected && !multi_select_) return select_only(index);
    selected_[index] = selected ? 1U : 0U;
    if (selected) {
        ++selected_count_;
    } else {
        --selected_count_;
    }
    return true;
}

bool ListView::select_range(std::size_t anchor, std::size_t index, bool keep_existing) {
    const Layout placed = layout();
    if (placed.grouped) {
        // The range runs through the shown order.
        const std::size_t from = position_of(placed, anchor);
        const std::size_t to = position_of(placed, index);
        const std::size_t first = std::min(from, to);
        const std::size_t last = std::max(from, to);
        const std::size_t length = last - first + 1U;
        std::size_t already = 0U;
        for (std::size_t position = first; position <= last; ++position) {
            if (selected_[index_at(placed, position)] != 0U) ++already;
        }
        const bool changed = keep_existing ? already != length
                                           : (already != length || selected_count_ != length);
        if (!changed) return false;
        if (!keep_existing) static_cast<void>(select_none());
        for (std::size_t position = first; position <= last; ++position) {
            std::uint8_t& flag = selected_[index_at(placed, position)];
            if (flag != 0U) continue;
            flag = 1U;
            ++selected_count_;
        }
        return true;
    }
    const std::size_t first = std::min(anchor, index);
    const std::size_t last = std::max(anchor, index);
    const std::size_t length = last - first + 1U;
    std::size_t already = 0U;
    for (std::size_t position = first; position <= last; ++position) {
        if (selected_[position] != 0U) ++already;
    }
    const bool changed = keep_existing ? already != length
                                       : (already != length || selected_count_ != length);
    if (!changed) return false;
    if (!keep_existing) {
        std::fill(selected_.begin(), selected_.end(), static_cast<std::uint8_t>(0U));
        selected_count_ = 0U;
        already = 0U;
    }
    std::fill(selected_.begin() + static_cast<std::ptrdiff_t>(first),
              selected_.begin() + static_cast<std::ptrdiff_t>(last) + 1, static_cast<std::uint8_t>(1U));
    selected_count_ += length - already;
    return true;
}

void ListView::publish_selection() {
    // A reveal shows the row as it was; the next pointer move reveals the
    // row as it is.
    conceal_cut_off_text();
    invalidate(Dirty::paint | Dirty::semantics);
    const ListViewSelectionChange change{std::string(stable_id().value()), selected_count_, focused_index_};
    publish_change(selection_changed_, change);
}

void ListView::set_selected(std::size_t index, bool selected) {
    require_mutable();
    if (index >= item_count()) throw std::out_of_range("ListView item index is past the end");
    if (!assign_selected(index, selected)) return;
    if (selected) anchor_index_ = index;
    publish_selection();
}

void ListView::select_all() {
    require_mutable();
    const std::size_t count = item_count();
    if (count == 0U) return;
    if (!multi_select_) {
        // One item at most: the focused one, or the first.
        const std::size_t only = focused_index_.value_or(0U);
        if (select_only(only)) publish_selection();
        return;
    }
    if (selected_count_ == count) return;
    std::fill(selected_.begin(), selected_.end(), static_cast<std::uint8_t>(1U));
    selected_count_ = count;
    publish_selection();
}

void ListView::clear_selection() {
    require_mutable();
    if (select_none()) publish_selection();
}

void ListView::set_focused_index(std::optional<std::size_t> index) {
    require_mutable();
    if (index && *index >= item_count()) throw std::out_of_range("ListView item index is past the end");
    if (focused_index_ == index) return;
    focused_index_ = index;
    if (index) ensure_visible(*index);
    invalidate(Dirty::paint | Dirty::semantics);
}

void ListView::move_focus(std::size_t index) {
    focused_index_ = index;
    ensure_visible(index);
    invalidate(Dirty::paint | Dirty::semantics);
}

void ListView::choose(std::size_t index, Modifier modifiers, bool move_only) {
    const bool control = has_modifier(modifiers, Modifier::control) || has_modifier(modifiers, Modifier::meta);
    const bool shift = has_modifier(modifiers, Modifier::shift);
    if (move_only && multi_select_) {
        move_focus(index);
        return;
    }
    bool changed = false;
    if (multi_select_ && shift) {
        const std::size_t anchor = anchor_index_.value_or(focused_index_.value_or(index));
        changed = select_range(anchor, index, control);
        anchor_index_ = anchor;
    } else if (multi_select_ && control) {
        changed = toggle_selected(index);
        anchor_index_ = index;
    } else {
        changed = select_only(index);
        anchor_index_ = index;
    }
    move_focus(index);
    if (changed) publish_selection();
}

void ListView::activate(std::size_t index) {
    if (index >= item_count()) return;
    ListViewItem scratch;
    const ListViewItem& item = item_at(index, scratch);
    const ListViewItemActivation activation{std::string(stable_id().value()), index, item.stable_id};
    item_activated_.emit(activation);
}

// -- scrolling ---------------------------------------------------------------

std::size_t ListView::top_index() const {
    const Layout placed = layout();
    if (placed.grouped) return index_at(placed, first_realized(placed));
    const std::size_t result = std::min(placed.shown_count, placed.top_line * placed.per_line);
    return result;
}

void ListView::ensure_visible(std::size_t index) {
    if (index >= item_count()) throw std::out_of_range("ListView item index is past the end");
    const Layout placed = layout();
    if (placed.grouped) {
        // A group's first line brings its header into view with it.
        const std::size_t line = position_line(placed, position_of(placed, index));
        const GroupBand& band = placed.bands[band_of_line(placed, line)];
        const std::size_t wanted = line == band.first_line + 1U ? band.first_line : line;
        const double bottom = line_offset(placed, line + 1U);
        const double room = placed.viewport.height + 0.5;
        std::size_t top = placed.top_line;
        if (wanted < top) {
            top = wanted;
        } else if (bottom - line_offset(placed, top) > room) {
            // The first top line from which the line's bottom shows.
            std::size_t high = line;
            while (top < high) {
                const std::size_t middle = top + (high - top) / 2U;
                if (bottom - line_offset(placed, middle) <= room) {
                    high = middle;
                } else {
                    top = middle + 1U;
                }
            }
        }
        if (top != placed.top_line) set_top_line(top);
        return;
    }
    const std::size_t line = index / placed.per_line;
    std::size_t top = placed.top_line;
    if (line < top) {
        top = line;
    } else if (line >= top + placed.page_lines) {
        top = line - placed.page_lines + 1U;
    }
    if (top != placed.top_line) set_top_line(top);
}

void ListView::set_top_line(std::size_t line) {
    const Layout placed = layout();
    const std::size_t clamped = std::min(line, maximum_top_line(placed));
    if (placed.grouped) {
        if (clamped == placed.top_line) return;
        store_top_line(placed, clamped);
    } else {
        const std::size_t item = clamped * placed.per_line;
        if (item == top_item_ && clamped == placed.top_line) return;
        top_item_ = item;
        top_header_ = false;
    }
    hot_index_.reset();
    conceal_cut_off_text();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const Layout scrolled = layout();
    place_label_editor(scrolled);
    sync_scroll_bars(scrolled);
}

void ListView::scroll_lines(std::ptrdiff_t lines) {
    const Layout placed = layout();
    const std::ptrdiff_t current = static_cast<std::ptrdiff_t>(placed.top_line);
    const std::ptrdiff_t maximum = static_cast<std::ptrdiff_t>(maximum_top_line(placed));
    const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(current + lines, 0, maximum);
    set_top_line(static_cast<std::size_t>(next));
}

void ListView::set_horizontal_offset(double offset) {
    require_mutable();
    if (!std::isfinite(offset) || offset < 0.0) throw std::invalid_argument("ListView horizontal offset is invalid");
    horizontal_offset_ = offset;
    const Layout placed = layout();
    horizontal_offset_ = placed.horizontal_offset;
    conceal_cut_off_text();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    place_label_editor(placed);
    sync_scroll_bars(placed);
}

void ListView::sync_scroll_bars(const Layout& placed) {
    if (!vertical_bar_ || !horizontal_bar_) return;
    synchronizing_scroll_ = true;
    const double maximum_line = static_cast<double>(maximum_top_line(placed));
    const double page = static_cast<double>(placed.page_lines);
    const double top = static_cast<double>(placed.top_line);
    if (!placed.vertical_bar.empty() && maximum_line > 0.0) {
        VScrollBar& bar = *vertical_bar_;
        bar.set_range(0.0, maximum_line);
        bar.set_small_change(1.0);
        bar.set_large_change(page);
        bar.set_value(std::clamp(top, 0.0, maximum_line));
    }
    if (!placed.horizontal_bar.empty()) {
        HScrollBar& bar = *horizontal_bar_;
        if (view_ == ListViewView::list && maximum_line > 0.0) {
            bar.set_range(0.0, maximum_line);
            bar.set_small_change(1.0);
            bar.set_large_change(page);
            bar.set_value(std::clamp(top, 0.0, maximum_line));
        } else if (view_ == ListViewView::details) {
            const double visible = placed.viewport.width / placed.scale;
            const double maximum_offset = std::max(0.0, placed.columns_width / placed.scale - visible);
            if (maximum_offset > 0.0) {
                bar.set_range(0.0, maximum_offset);
                bar.set_small_change(horizontal_key_step);
                bar.set_large_change(std::max(1.0, visible));
                bar.set_value(std::clamp(placed.horizontal_offset, 0.0, maximum_offset));
            }
        }
    }
    synchronizing_scroll_ = false;
}

void ListView::on_vertical_scroll(double value) {
    if (synchronizing_scroll_ || !is_alive()) return;
    const double line = std::max(0.0, std::round(value));
    set_top_line(static_cast<std::size_t>(line));
}

void ListView::on_horizontal_scroll(double value) {
    if (synchronizing_scroll_ || !is_alive()) return;
    if (view_ == ListViewView::list) {
        const double line = std::max(0.0, std::round(value));
        set_top_line(static_cast<std::size_t>(line));
        return;
    }
    if (view_ != ListViewView::details || horizontal_offset_ == value) return;
    horizontal_offset_ = std::max(0.0, value);
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- sorting -----------------------------------------------------------------

void ListView::set_sort_indicator(std::optional<std::size_t> column, ListViewSortOrder order) {
    require_mutable();
    if (column && *column >= columns_.size()) throw std::out_of_range("ListView sort column is past the end");
    if (order != ListViewSortOrder::none && order != ListViewSortOrder::ascending &&
        order != ListViewSortOrder::descending) {
        throw std::invalid_argument("ListView sort order is invalid");
    }
    if (!column || order == ListViewSortOrder::none) {
        column.reset();
        order = ListViewSortOrder::none;
    }
    if (sort_column_ == column && sort_order_ == order) return;
    sort_column_ = column;
    sort_order_ = order;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ListView::sort_items(std::size_t column, ListViewSortOrder order) {
    require_mutable();
    if (source_) throw std::logic_error("A virtual ListView's source owns the item order");
    if (column >= columns_.size()) throw std::out_of_range("ListView sort column is past the end");
    set_sort_indicator(column, order);
    if (order == ListViewSortOrder::none) return;
    // `order_of[new position]` is the item's old position.
    std::vector<std::size_t> order_of(items_.size(), 0U);
    std::iota(order_of.begin(), order_of.end(), std::size_t{0U});
    const ColumnTextOrder by_text{&items_, column, order == ListViewSortOrder::descending};
    std::stable_sort(order_of.begin(), order_of.end(), by_text);
    // `position_of[old position]` is its new position.
    std::vector<std::size_t> position_of(items_.size(), 0U);
    std::vector<ListViewItem> sorted;
    sorted.reserve(items_.size());
    std::vector<std::uint8_t> sorted_flags(items_.size(), 0U);
    bool moved = false;
    for (std::size_t position = 0; position < order_of.size(); ++position) {
        const std::size_t old = order_of[position];
        position_of[old] = position;
        sorted.push_back(std::move(items_[old]));
        sorted_flags[position] = selected_[old];
        if (selected_[old] != 0U && old != position) moved = true;
    }
    items_.swap(sorted);
    selected_.swap(sorted_flags);
    item_indices_.clear();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        item_indices_.emplace(items_[index].stable_id, index);
    }
    if (focused_index_) focused_index_ = position_of[*focused_index_];
    if (anchor_index_) anchor_index_ = position_of[*anchor_index_];
    hot_index_.reset();
    conceal_cut_off_text();
    ++model_revision_;
    insertion_mark_.reset();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    // An edit in progress is cancelled once the items have changed.
    if (label_edit_index_) {
        cancel_label_edit();
        if (!is_alive()) return;
    }
    if (moved) publish_selection();
}

// -- images and font -----------------------------------------------------------

void ListView::set_image_list(std::shared_ptr<ImageList> images, std::shared_ptr<ImageList>& slot,
                              SubscriptionToken& subscription) {
    require_mutable();
    // A list must be alive and, once this view is in a window, that window's.
    const Window* const host = window();
    if (images && (!(*images).is_alive() || (host != nullptr && !(*images).belongs_to(*host)))) {
        throw std::invalid_argument("ListView requires a live ImageList of its own Window");
    }
    if (slot == images) return;
    subscription.disconnect();
    slot = std::move(images);
    if (slot) {
        const Delegate<const ImageListChange&> changed =
            Delegate<const ImageListChange&>::bind<ListView, &ListView::image_list_changed>(*this);
        subscription = (*slot).changed().subscribe(*this, changed);
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListView::set_small_image_list(std::shared_ptr<ImageList> images) {
    set_image_list(std::move(images), small_images_, small_images_changed_);
}

void ListView::set_large_image_list(std::shared_ptr<ImageList> images) {
    set_image_list(std::move(images), large_images_, large_images_changed_);
}

void ListView::image_list_changed(const ImageListChange&) {
    if (is_alive()) invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test);
}

void ListView::on_attached_to_window() {
    Control::on_attached_to_window();
    if ((small_images_ && !(*small_images_).belongs_to(*window())) ||
        (large_images_ && !(*large_images_).belongs_to(*window()))) {
        throw std::logic_error("ListView cannot attach to a Window other than its ImageLists'");
    }
}

void ListView::set_draw_mode(ItemDrawMode mode) {
    require_mutable();
    if (mode != ItemDrawMode::normal && mode != ItemDrawMode::owner_draw) {
        throw std::invalid_argument("ListView draw mode is invalid");
    }
    if (draw_mode_ == mode) return;
    draw_mode_ = mode;
    invalidate(Dirty::paint);
}

void ListView::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("ListView font specification is invalid");
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    if (label_editor_) (*label_editor_).set_font(font_);
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListView::set_tile_size(Size size) {
    require_mutable();
    const bool automatic = size.width == 0.0 && size.height == 0.0;
    if (!automatic && (!valid_tile_side(size.width) || !valid_tile_side(size.height))) {
        throw std::invalid_argument("ListView tile size is outside the tile side bounds");
    }
    if (tile_size_ == size) return;
    tile_size_ = size;
    if (view_ != ListViewView::tile) return;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- groups ------------------------------------------------------------------

void ListView::set_groups(std::vector<ListViewGroup> groups) {
    require_mutable();
    if (groups.size() > maximum_groups) throw std::invalid_argument("ListView has more groups than maximum_groups");
    std::unordered_set<std::string> identities{};
    for (const ListViewGroup& group : groups) {
        if (!valid_group(group) || !identities.insert(group.stable_id).second) {
            throw std::invalid_argument(
                "ListView groups require unique stable IDs following the StableId segment rule, valid UTF-8 and an "
                "alignment");
        }
    }
    groups_ = std::move(groups);
    ++model_revision_;
    hot_index_.reset();
    conceal_cut_off_text();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListView::set_show_groups(bool shown) {
    require_mutable();
    if (show_groups_ == shown) return;
    show_groups_ = shown;
    ++model_revision_;
    hot_index_.reset();
    conceal_cut_off_text();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListView::set_default_group_header(std::string header) {
    require_mutable();
    if (!validate_utf8(header).valid()) throw std::invalid_argument("ListView group header is not valid UTF-8");
    if (default_group_header_ == header) return;
    default_group_header_ = std::move(header);
    invalidate(Dirty::paint | Dirty::semantics);
}

bool ListView::grouped() const noexcept {
    const bool result = show_groups_ && !groups_.empty() && !source_ && view_ != ListViewView::list;
    return result;
}

const std::string& ListView::group_header(std::size_t group) const noexcept {
    if (group < groups_.size()) return groups_[group].header;
    return default_group_header_;
}

std::string ListView::group_identity(std::size_t group) const {
    std::string identity(stable_id().value());
    if (group < groups_.size()) {
        identity.append(".group.");
        identity.append(groups_[group].stable_id);
    } else {
        identity.append(".default-group");
    }
    return identity;
}

// The default group comes first, as Windows Forms' DefaultGroup does, then
// the groups in their order; items keep their model order within a group.
void ListView::refresh_display_order() const {
    if (display_valid_ && display_revision_ == model_revision_) return;
    std::unordered_map<std::string, std::size_t> group_of{};
    group_of.reserve(groups_.size());
    for (std::size_t group = 0; group < groups_.size(); ++group) group_of.emplace(groups_[group].stable_id, group);
    const std::size_t fallback = groups_.size();
    std::vector<std::size_t> membership(items_.size(), fallback);
    display_counts_.assign(groups_.size() + 1U, 0U);
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const std::unordered_map<std::string, std::size_t>::const_iterator found = group_of.find(items_[index].group);
        if (found != group_of.end()) membership[index] = (*found).second;
        ++display_counts_[membership[index]];
    }
    // Each group's first position: the default group's at 0.
    std::vector<std::size_t> next(groups_.size() + 1U, 0U);
    std::size_t position = display_counts_[fallback];
    for (std::size_t group = 0; group < groups_.size(); ++group) {
        next[group] = position;
        position += display_counts_[group];
    }
    display_order_.assign(items_.size(), 0U);
    display_position_.assign(items_.size(), 0U);
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const std::size_t at = next[membership[index]]++;
        display_order_[at] = index;
        display_position_[index] = at;
    }
    display_revision_ = model_revision_;
    display_valid_ = true;
}

std::size_t ListView::index_at(const Layout& placed, std::size_t position) const noexcept {
    if (!placed.grouped) return position;
    return display_order_[position];
}

std::size_t ListView::position_of(const Layout& placed, std::size_t index) const noexcept {
    if (!placed.grouped) return index;
    return display_position_[index];
}

// The bands of the groups that hold items, and the line count, for the
// layout's items per line.
void ListView::place_groups(Layout& placed) const {
    placed.bands.clear();
    std::size_t first = 0U;
    std::size_t line = 0U;
    const std::size_t fallback = groups_.size();
    for (std::size_t step = 0; step <= groups_.size(); ++step) {
        // The default group first, then the groups in order.
        const std::size_t group = step == 0U ? fallback : step - 1U;
        const std::size_t count = display_counts_[group];
        if (count == 0U) continue;
        placed.bands.push_back({.group = group, .first = first, .count = count, .first_line = line});
        first += count;
        line += 1U + ceiling_quotient(count, placed.per_line);
    }
    placed.line_count = line;
}

std::size_t ListView::band_of_line(const Layout& placed, std::size_t line) const noexcept {
    std::size_t low = 0U;
    std::size_t high = placed.bands.size();
    // The last band whose header line is at or before `line`.
    while (high - low > 1U) {
        const std::size_t middle = low + (high - low) / 2U;
        if (placed.bands[middle].first_line <= line) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

std::size_t ListView::band_of_position(const Layout& placed, std::size_t position) const noexcept {
    std::size_t low = 0U;
    std::size_t high = placed.bands.size();
    while (high - low > 1U) {
        const std::size_t middle = low + (high - low) / 2U;
        if (placed.bands[middle].first <= position) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

bool ListView::header_line(const Layout& placed, std::size_t line) const noexcept {
    if (!placed.grouped || placed.bands.empty()) return false;
    const bool result = placed.bands[band_of_line(placed, line)].first_line == line;
    return result;
}

std::size_t ListView::line_first(const Layout& placed, std::size_t line) const noexcept {
    if (!placed.grouped) return std::min(placed.shown_count, line * placed.per_line);
    const GroupBand& band = placed.bands[band_of_line(placed, line)];
    if (line == band.first_line) return band.first;
    const std::size_t result = band.first + (line - band.first_line - 1U) * placed.per_line;
    return result;
}

std::size_t ListView::line_items(const Layout& placed, std::size_t line) const noexcept {
    if (!placed.grouped) {
        const std::size_t first = line_first(placed, line);
        return std::min(placed.per_line, placed.shown_count - first);
    }
    const GroupBand& band = placed.bands[band_of_line(placed, line)];
    if (line == band.first_line) return 0U;
    const std::size_t first = line_first(placed, line);
    const std::size_t result = std::min(placed.per_line, band.first + band.count - first);
    return result;
}

std::size_t ListView::position_line(const Layout& placed, std::size_t position) const noexcept {
    if (!placed.grouped) return position / placed.per_line;
    const GroupBand& band = placed.bands[band_of_position(placed, position)];
    const std::size_t result = band.first_line + 1U + (position - band.first) / placed.per_line;
    return result;
}

double ListView::line_offset(const Layout& placed, std::size_t line) const noexcept {
    const double extent = view_ == ListViewView::list ? placed.cell.width : placed.cell.height;
    if (!placed.grouped || placed.bands.empty()) return static_cast<double>(line) * extent;
    const std::size_t band = band_of_line(placed, line);
    const std::size_t headers = band + (line > placed.bands[band].first_line ? 1U : 0U);
    const double result = static_cast<double>(headers) * placed.header_extent +
                          static_cast<double>(line - headers) * extent;
    return result;
}

std::size_t ListView::line_at_offset(const Layout& placed, double offset) const noexcept {
    if (placed.line_count == 0U) return 0U;
    // The last line whose top is at or before the offset.
    std::size_t low = 0U;
    std::size_t high = placed.line_count;
    while (high - low > 1U) {
        const std::size_t middle = low + (high - low) / 2U;
        if (line_offset(placed, middle) <= offset) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

std::size_t ListView::next_item_line(const Layout& placed, std::size_t line, bool forward) const noexcept {
    std::size_t next = line;
    while (true) {
        if (forward) {
            if (next + 1U >= placed.line_count) return line;
            ++next;
        } else {
            if (next == 0U) return line;
            --next;
        }
        if (!header_line(placed, next)) return next;
    }
}

void ListView::store_top_line(const Layout& placed, std::size_t line) {
    top_header_ = header_line(placed, line);
    top_item_ = line_first(placed, line);
}

// -- layout ------------------------------------------------------------------

double ListView::text_line_height() const {
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", text_font(font_));
    return metrics.logical_size.height;
}

Size ListView::icon_size(bool large) const noexcept {
    const double scale = effective_text_scale();
    const std::shared_ptr<ImageList>& images = large ? large_images_ : small_images_;
    const double fallback = large ? default_large_icon : default_small_icon;
    Size size{fallback, fallback};
    if (images) size = (*images).image_size();
    const Size result{size.width * scale, size.height * scale};
    return result;
}

ListView::Layout ListView::layout() const {
    return layout_for(client_rectangle());
}

ListView::Layout ListView::layout_for(Rect client) const {
    Layout placed;
    const double scale = effective_text_scale();
    placed.scale = scale;
    placed.well = client;
    const double inset = edge_inset * scale;
    const Rect content{client.x + inset, client.y + inset, std::max(0.0, client.width - inset * 2.0),
                       std::max(0.0, client.height - inset * 2.0)};
    const double line = text_line_height();
    placed.text_line = line;
    double row = std::max(minimum_row_height * scale, line + row_text_padding * scale);
    if (check_boxes_) row = std::max(row, (check_size + cell_padding) * scale);
    if (small_images_) row = std::max(row, icon_size(false).height + cell_padding * scale);
    placed.row_height = row;
    const bool details = view_ == ListViewView::details;
    const bool list = view_ == ListViewView::list;
    double columns_width = 0.0;
    for (const ListViewColumn& column : columns_) columns_width += column.width * scale;
    placed.columns_width = columns_width;
    // The details view shows nothing without columns, as in Windows Forms.
    placed.shown_count = details && columns_.empty() ? 0U : item_count();
    const double header_height = details && !columns_.empty() ? row + 2.0 * scale : 0.0;
    if (details) {
        placed.cell = {columns_width, row};
    } else if (view_ == ListViewView::large_icon) {
        const Size large = icon_size(true);
        placed.cell = {std::max(large_cell_width * scale, large.width + cell_padding * 4.0 * scale),
                       large.height + line + (cell_padding * 3.0 + row_text_padding) * scale};
    } else if (view_ == ListViewView::tile) {
        if (tile_size_.width > 0.0) {
            placed.cell = {tile_size_.width * scale, tile_size_.height * scale};
        } else {
            const Size large = icon_size(true);
            const double text_height = line * static_cast<double>(tile_text_lines);
            placed.cell = {large.width + (tile_text_width + cell_padding * 3.0) * scale,
                           std::max(large.height, text_height) + cell_padding * 2.0 * scale};
        }
        placed.tile_lines = fitting_lines(placed.cell.height - cell_padding * 2.0 * scale, line);
    } else {
        placed.cell = {list_cell_width * scale, row};
    }
    const double line_extent = list ? placed.cell.width : placed.cell.height;
    placed.grouped = grouped() && placed.shown_count > 0U;
    if (placed.grouped) {
        refresh_display_order();
        placed.header_extent = line + group_header_padding * scale;
    }
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
        if (details) {
            placed.per_line = 1U;
        } else if (list) {
            placed.per_line = fitting_lines(area.height, row);
        } else {
            placed.per_line = fitting_lines(area.width, placed.cell.width);
        }
        if (placed.grouped) {
            place_groups(placed);
        } else {
            placed.line_count = ceiling_quotient(placed.shown_count, placed.per_line);
        }
        placed.page_lines = fitting_lines(list ? area.width : area.height, line_extent);
        const bool overflows = placed.grouped ? line_offset(placed, placed.line_count) > area.height + 0.5
                                              : placed.line_count > placed.page_lines;
        const bool next_vertical = !list && overflows;
        const bool next_horizontal = (list && overflows) || (details && columns_width > area.width + 0.5);
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
    if (placed.grouped) {
        const std::size_t position = std::min(top_item_, placed.shown_count - 1U);
        const GroupBand& band = placed.bands[band_of_position(placed, position)];
        const std::size_t top_line =
            top_header_ && position == band.first ? band.first_line : position_line(placed, position);
        placed.top_line = std::min(top_line, maximum_top_line(placed));
        const double top = line_offset(placed, placed.top_line);
        std::size_t lines = 0U;
        while (placed.top_line + lines < placed.line_count &&
               line_offset(placed, placed.top_line + lines) - top < area.height) {
            ++lines;
        }
        placed.visible_lines = lines;
    } else {
        placed.visible_lines = touched_lines(list ? area.width : area.height, line_extent);
        const std::size_t top_line = top_item_ / placed.per_line;
        placed.top_line = std::min(top_line, maximum_top_line(placed));
    }
    if (details) {
        const double maximum_offset = std::max(0.0, (columns_width - area.width) / scale);
        placed.horizontal_offset = std::clamp(horizontal_offset_, 0.0, maximum_offset);
    }
    return placed;
}

std::size_t ListView::maximum_top_line(const Layout& placed) const noexcept {
    if (placed.grouped) {
        // The first line from which the rest fits.
        const double total = line_offset(placed, placed.line_count);
        const double room = placed.viewport.height;
        if (total <= room + 0.5) return 0U;
        std::size_t low = 0U;
        std::size_t high = placed.line_count;
        while (low < high) {
            const std::size_t middle = low + (high - low) / 2U;
            if (total - line_offset(placed, middle) <= room + 0.5) {
                high = middle;
            } else {
                low = middle + 1U;
            }
        }
        return low;
    }
    if (placed.line_count <= placed.page_lines) return 0U;
    const std::size_t result = placed.line_count - placed.page_lines;
    return result;
}

std::size_t ListView::first_realized(const Layout& placed) const noexcept {
    if (placed.grouped) return line_first(placed, placed.top_line);
    const std::size_t result = std::min(placed.shown_count, placed.top_line * placed.per_line);
    return result;
}

std::size_t ListView::end_realized(const Layout& placed) const noexcept {
    if (placed.grouped) {
        if (placed.visible_lines == 0U) return first_realized(placed);
        const std::size_t last = placed.top_line + placed.visible_lines - 1U;
        const std::size_t result = line_first(placed, last) + line_items(placed, last);
        return result;
    }
    const std::size_t lines = placed.top_line + placed.visible_lines;
    const std::size_t result = std::min(placed.shown_count, lines * placed.per_line);
    return result;
}

Rect ListView::item_bounds(const Layout& placed, std::size_t index) const noexcept {
    if (placed.grouped) {
        const std::size_t position = position_of(placed, index);
        const std::size_t line = position_line(placed, position);
        const double along = static_cast<double>(position - line_first(placed, line));
        const double y = placed.viewport.y + line_offset(placed, line) - line_offset(placed, placed.top_line);
        if (view_ == ListViewView::details) {
            const Rect row{placed.viewport.x - placed.horizontal_offset * placed.scale, y, placed.cell.width,
                           placed.cell.height};
            return row;
        }
        const Rect cell{placed.viewport.x + along * placed.cell.width, y, placed.cell.width, placed.cell.height};
        return cell;
    }
    const std::size_t line = index / placed.per_line;
    const std::size_t position = index % placed.per_line;
    const double relative = static_cast<double>(line) - static_cast<double>(placed.top_line);
    const double along = static_cast<double>(position);
    const Rect area = placed.viewport;
    if (view_ == ListViewView::details) {
        const Rect row{area.x - placed.horizontal_offset * placed.scale, area.y + relative * placed.row_height,
                       placed.cell.width, placed.cell.height};
        return row;
    }
    if (view_ == ListViewView::list) {
        const Rect cell{area.x + relative * placed.cell.width, area.y + along * placed.cell.height,
                        placed.cell.width, placed.cell.height};
        return cell;
    }
    const Rect cell{area.x + along * placed.cell.width, area.y + relative * placed.cell.height,
                    placed.cell.width, placed.cell.height};
    return cell;
}

Rect ListView::column_cell(const Layout& placed, std::size_t column) const noexcept {
    double x = placed.viewport.x - placed.horizontal_offset * placed.scale;
    for (std::size_t index = 0; index < column; ++index) x += columns_[index].width * placed.scale;
    const Rect cell{x, placed.header.y, columns_[column].width * placed.scale, placed.header.height};
    return cell;
}

bool ListView::column_visible(const Layout& placed, std::size_t column) const noexcept {
    if (placed.header.empty()) return false;
    const Rect cell = column_cell(placed, column);
    const bool result = cell.x < placed.viewport.x + placed.viewport.width && cell.x + cell.width > placed.viewport.x;
    return result;
}

std::size_t ListView::visible_column_count(const Layout& placed) const noexcept {
    std::size_t count = 0U;
    for (std::size_t column = 0; column < columns_.size(); ++column) {
        if (column_visible(placed, column)) ++count;
    }
    return count;
}

std::size_t ListView::visible_column(const Layout& placed, std::size_t position) const noexcept {
    std::size_t seen = 0U;
    for (std::size_t column = 0; column < columns_.size(); ++column) {
        if (!column_visible(placed, column)) continue;
        if (seen == position) return column;
        ++seen;
    }
    return columns_.size();
}

// One item's parts. `clip` bounds every part: the viewport when publishing,
// the item's own cell when painting (the painter clips to the viewport), so
// text scrolled partly out of view keeps its place instead of moving into
// the visible part. In the details view the check box, icon and text also
// stay inside the first column.
ListView::ItemParts ListView::item_parts(const Layout& placed, std::size_t index, Rect clip) const {
    const double scale = placed.scale;
    const Rect cell = item_bounds(placed, index);
    Rect check_well{};
    Rect icon{};
    Rect text{};
    Rect face{};
    Rect inner_clip = clip;
    if (view_ == ListViewView::large_icon) {
        const Size image = icon_size(true);
        const double top = cell.y + cell_padding * scale;
        if (large_images_) {
            icon = {std::floor(cell.x + (cell.width - image.width) * 0.5), top, image.width, image.height};
        }
        if (check_boxes_) check_well = {cell.x + 2.0 * scale, top, check_size * scale, check_size * scale};
        const double text_top = top + image.height + cell_padding * scale;
        text = {cell.x + 2.0 * scale, text_top, std::max(0.0, cell.width - 4.0 * scale),
                std::max(0.0, cell.y + cell.height - 2.0 * scale - text_top)};
        face = {cell.x + scale, cell.y + scale, std::max(0.0, cell.width - 2.0 * scale),
                std::max(0.0, cell.height - 2.0 * scale)};
    } else if (view_ == ListViewView::tile) {
        // The check box and icon sit at the tile's left, centred on it; the
        // text block beside them is centred too.
        const double middle = cell.y + cell.height * 0.5;
        double x = cell.x + cell_padding * scale;
        if (check_boxes_) {
            const double side = check_size * scale;
            check_well = {x, std::floor(middle - side * 0.5), side, side};
            x += side + cell_padding * scale;
        }
        if (large_images_) {
            const Size image = icon_size(true);
            icon = {x, std::floor(middle - image.height * 0.5), image.width, image.height};
            x += image.width + cell_padding * scale;
        }
        ListViewItem scratch;
        const ListViewItem& item = item_at(index, scratch);
        const std::size_t lines = tile_line_count(item, columns_.size(), placed.tile_lines);
        const double block = placed.text_line * static_cast<double>(lines);
        const double top = std::max(cell.y + cell_padding * scale, std::floor(middle - block * 0.5));
        text = {x, top, std::max(0.0, cell.x + cell.width - cell_padding * scale - x), placed.text_line};
        face = {cell.x + scale, cell.y + scale, std::max(0.0, cell.width - 2.0 * scale),
                std::max(0.0, cell.height - 2.0 * scale)};
    } else {
        double first_right = cell.x + cell.width;
        if (view_ == ListViewView::details) {
            first_right = cell.x + columns_.front().width * scale;
            inner_clip = Rect::intersection(clip, {cell.x, cell.y, first_right - cell.x, cell.height});
        }
        const double middle = cell.y + cell.height * 0.5;
        double x = cell.x + cell_padding * scale;
        if (check_boxes_) {
            const double side = check_size * scale;
            check_well = {x, std::floor(middle - side * 0.5), side, side};
            x += side + cell_padding * scale;
        }
        if (small_images_) {
            const Size image = icon_size(false);
            icon = {x, std::floor(middle - image.height * 0.5), image.width, image.height};
            x += image.width + cell_padding * scale;
        }
        text = {x, cell.y, std::max(0.0, first_right - cell_padding * scale - x), cell.height};
        const double face_left = x - 2.0 * scale;
        face = {face_left, cell.y, std::max(0.0, first_right - face_left), cell.height};
        if (view_ == ListViewView::details && full_row_select_) face = cell;
    }
    Rect check{};
    if (!check_well.empty()) {
        const double inset = check_mark_inset * scale;
        check = {check_well.x + inset, check_well.y + inset, std::max(0.0, check_well.width - inset * 2.0),
                 std::max(0.0, check_well.height - inset * 2.0)};
    }
    const Rect face_clip = view_ == ListViewView::details && full_row_select_ ? clip : inner_clip;
    ItemParts parts;
    parts.cell = PartGeometry::from_bounds(Rect::intersection(cell, clip));
    parts.item_face = PartGeometry::from_bounds(Rect::intersection(face, face_clip));
    parts.check_well = PartGeometry::from_bounds(Rect::intersection(check_well, inner_clip));
    parts.check = PartGeometry::from_bounds(Rect::intersection(check, inner_clip));
    parts.icon = PartGeometry::from_bounds(Rect::intersection(icon, inner_clip));
    parts.text = PartGeometry::from_bounds(Rect::intersection(text, inner_clip));
    parts.cell_focus = parts.item_face;
    return parts;
}

// One details-view cell past the first column, clipped as item_parts is.
ListView::SubItemParts ListView::sub_item_parts(const Layout& placed, std::size_t index, std::size_t column,
                                                Rect clip) const {
    const Rect row = item_bounds(placed, index);
    const Rect header = column_cell(placed, column);
    const Rect cell{header.x, row.y, header.width, row.height};
    const double padding = sub_item_padding * placed.scale;
    const Rect text{cell.x + padding, cell.y, std::max(0.0, cell.width - padding * 2.0), cell.height};
    const Rect visible = Rect::intersection(cell, clip);
    SubItemParts parts;
    parts.sub_item_cell = PartGeometry::from_bounds(visible);
    parts.sub_item_text = PartGeometry::from_bounds(Rect::intersection(text, visible));
    return parts;
}

// One column header, from the layout ObjectView's headers share. `clip` is
// the header band when publishing, the column's own cell when painting.
ListView::ColumnParts ListView::column_parts(const Layout& placed, std::size_t column, Rect clip) const {
    const bool sorted = sort_column_ == column && sort_order_ != ListViewSortOrder::none;
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

// Application-defined column states, as ObjectView declares them.
constexpr CustomStateIndex sorted_ascending_state{0U};
constexpr CustomStateIndex sorted_descending_state{1U};

} // namespace

AnatomyStateSet ListView::item_states(std::size_t index, bool checked) const {
    AnatomyStateSet states;
    if (selected_[index] != 0U) states.insert(AnatomyState::selected);
    if (hot_index_ == index) states.insert(AnatomyState::hot);
    if (focused_index_ == index) states.insert(AnatomyState::current);
    if (check_boxes_ && checked) states.insert(AnatomyState::checked);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    const Window* const host = attached_window();
    if (focused_ && focused_index_ == index && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

AnatomyStateSet ListView::column_states(std::size_t column) const {
    AnatomyStateSet states;
    if (hot_column_ == column) states.insert(AnatomyState::hot);
    if (pressed_column_ == column) states.insert(AnatomyState::pressed);
    if (resizing_column_ == column) states.insert(AnatomyState::captured);
    if (sort_column_ == column && sort_order_ == ListViewSortOrder::ascending) {
        states.insert(sorted_ascending_state);
    }
    if (sort_column_ == column && sort_order_ == ListViewSortOrder::descending) {
        states.insert(sorted_descending_state);
    }
    return states;
}

void ListView::arrange(Rect final_bounds) {
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
    if (placed.grouped) {
        store_top_line(placed, placed.top_line);
    } else {
        top_item_ = std::min(top_item_, placed.top_line * placed.per_line);
    }
    horizontal_offset_ = placed.horizontal_offset;
    place_label_editor(placed);
    sync_scroll_bars(placed);
}

// -- paint -------------------------------------------------------------------

void ListView::paint_column(Painter& painter, const Layout& placed, std::size_t column) {
    const Rect cell = column_cell(placed, column);
    const ColumnParts parts = column_parts(placed, column, cell);
    const AnatomyStateSet states = column_states(column);
    PartPaint part;
    part.font = text_font(font_);
    part.states = states;
    // Header cells are flat at rest and show a plate when hot or pressed:
    // the menu_item family's look.
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
        part.mark = sort_order_ == ListViewSortOrder::ascending ? PartMark::arrow_up : PartMark::arrow_down;
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

bool ListView::model_unchanged(std::uint64_t revision) const noexcept {
    const bool unchanged = is_alive() && model_revision_ == revision;
    return unchanged;
}

// The plate, the check box and the focus cue are painted by role in every
// mode; the content (icon, text and sub-item text) is the application's in
// the owner-draw mode, after the plate and before the focus cue.
bool ListView::paint_item(Painter& painter, const Layout& placed, std::size_t index) {
    const ListViewItem& item = item_at(index, paint_item_);
    const Rect cell = item_bounds(placed, index);
    const ItemParts parts = item_parts(placed, index, cell);
    const AnatomyStateSet states = item_states(index, item.checked);
    PartPaint part;
    part.font = text_font(font_);
    part.states = states;
    part.family = ControlVisualRole::selection;
    part.role = PartRole::face;
    part.bounds = parts.item_face.bounds;
    paint_part(painter, *this, part);
    if (parts.check_well.present) {
        part.family = ControlVisualRole::choice;
        part.role = PartRole::well;
        part.bounds = parts.check_well.bounds;
        paint_part(painter, *this, part);
        // An unchecked box shows no mark: an indicator without a mark
        // would paint a plate.
        if (states.contains(AnatomyState::checked)) {
            part.role = PartRole::indicator;
            part.bounds = parts.check.bounds;
            part.mark = PartMark::check;
            paint_part(painter, *this, part);
            part.mark = PartMark::none;
        }
        part.family = ControlVisualRole::selection;
    }
    const bool owner_drawn = draw_mode_ == ItemDrawMode::owner_draw;
    bool default_content = true;
    if (owner_drawn) {
        // The handlers see the identity through the view's own copy, which
        // stays valid for the whole raise even when one of them replaces
        // the items.
        paint_stable_id_ = item.stable_id;
        const Rect content = parts.icon.present ? Rect::united(parts.icon.bounds, parts.text.bounds)
                                                : parts.text.bounds;
        ItemContentPaint request{.painter = painter,
                                 .index = index,
                                 .column = 0U,
                                 .stable_id = paint_stable_id_,
                                 .content = content,
                                 .states = states,
                                 .font = part.font,
                                 .text = item_content_text(*this, ControlVisualRole::selection, states)};
        const std::uint64_t revision = model_revision_;
        default_content = raise_item_content_paint(draw_item_content_, request);
        if (!model_unchanged(revision)) return false;
    }
    if (default_content && parts.icon.present && !item.image_key.empty()) {
        const std::shared_ptr<ImageList>& images =
            view_ == ListViewView::large_icon || view_ == ListViewView::tile ? large_images_ : small_images_;
        const Window* const host = attached_window();
        const double density = host != nullptr ? (*host).scale() : 1.0;
        const ImageListResolution resolved = (*images).resolve(item.image_key, image_state(states), density);
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
    part.alignment = view_ == ListViewView::large_icon ? PartTextAlignment::center : PartTextAlignment::start;
    if (default_content) {
        details_detail::elide_to_width(painter, item.text, part.font, part.bounds.width, paint_text_);
        part.text = paint_text_;
        paint_part(painter, *this, part);
        part.text = {};
    }
    if (view_ == ListViewView::details) {
        AnatomyStateSet sub_states;
        if (states.contains(AnatomyState::selected)) sub_states.insert(AnatomyState::selected);
        if (states.contains(AnatomyState::disabled)) sub_states.insert(AnatomyState::disabled);
        part.states = sub_states;
        for (std::size_t column = 1; column < columns_.size(); ++column) {
            if (!column_visible(placed, column)) continue;
            const Rect header = column_cell(placed, column);
            const Rect sub_cell{header.x, cell.y, header.width, cell.height};
            const SubItemParts sub = sub_item_parts(placed, index, column, sub_cell);
            if (owner_drawn) {
                ItemContentPaint request{.painter = painter,
                                         .index = index,
                                         .column = column,
                                         .stable_id = paint_stable_id_,
                                         .content = sub.sub_item_text.bounds,
                                         .states = sub_states,
                                         .font = part.font,
                                         .text = item_content_text(*this, ControlVisualRole::selection, sub_states)};
                const std::uint64_t revision = model_revision_;
                const bool sub_default = raise_item_content_paint(draw_item_content_, request);
                if (!model_unchanged(revision)) return false;
                if (!sub_default) continue;
            }
            part.bounds = sub.sub_item_text.bounds;
            part.alignment = text_alignment(columns_[column].alignment);
            details_detail::elide_to_width(painter, column_text(item, column), part.font, part.bounds.width,
                                           paint_text_);
            part.text = paint_text_;
            paint_part(painter, *this, part);
            part.text = {};
        }
        part.states = states;
        part.alignment = PartTextAlignment::start;
    }
    if (view_ == ListViewView::tile && !paint_tile_lines(painter, placed, index, parts, states)) return false;
    part.role = PartRole::focus;
    part.bounds = parts.cell_focus.bounds;
    paint_part(painter, *this, part);
    return true;
}

// The tile view's sub-item lines under the item's text: one per column
// after the first with a non-empty sub-item, while the tile has room. False
// when an owner-draw handler changed the model or disposed the view.
bool ListView::paint_tile_lines(Painter& painter, const Layout& placed, std::size_t index,
                                const ItemParts& parts, AnatomyStateSet states) {
    const ListViewItem& item = item_at(index, paint_item_);
    const std::size_t lines = tile_line_count(item, columns_.size(), placed.tile_lines);
    const Rect first = parts.text.bounds;
    PartPaint part;
    part.font = text_font(font_);
    part.states = states;
    part.family = ControlVisualRole::selection;
    part.role = PartRole::content;
    part.alignment = PartTextAlignment::start;
    std::size_t shown = 1U;
    for (std::size_t column = 1; column < columns_.size() && shown < lines; ++column) {
        if (column_text(item, column).empty()) continue;
        const Rect bounds{first.x, first.y + placed.text_line * static_cast<double>(shown), first.width,
                          placed.text_line};
        ++shown;
        if (draw_mode_ == ItemDrawMode::owner_draw) {
            ItemContentPaint request{.painter = painter,
                                     .index = index,
                                     .column = column,
                                     .stable_id = paint_stable_id_,
                                     .content = bounds,
                                     .states = states,
                                     .font = part.font,
                                     .text = item_content_text(*this, ControlVisualRole::selection, states)};
            const std::uint64_t revision = model_revision_;
            const bool line_default = raise_item_content_paint(draw_item_content_, request);
            if (!model_unchanged(revision)) return false;
            if (!line_default) continue;
        }
        details_detail::elide_to_width(painter, column_text(item, column), part.font, bounds.width, paint_text_);
        part.bounds = bounds;
        part.text = paint_text_;
        paint_part(painter, *this, part);
        part.text = {};
    }
    return true;
}

// A group header line: the header's text, bold, over a rule.
void ListView::paint_group(Painter& painter, const Layout& placed, std::size_t band) {
    const GroupParts parts = group_parts(placed, band, placed.viewport);
    const std::size_t group = placed.bands[band].group;
    PartPaint part;
    FontSpec font = text_font(font_);
    font.weight = 700;
    part.font = font;
    if (!effectively_enabled()) part.states.insert(AnatomyState::disabled);
    part.family = ControlVisualRole::panel;
    part.role = PartRole::caption;
    part.bounds = parts.group_caption.bounds;
    part.alignment = group < groups_.size() ? text_alignment(groups_[group].header_alignment)
                                            : PartTextAlignment::start;
    details_detail::elide_to_width(painter, group_header(group), part.font, part.bounds.width, paint_text_);
    part.text = paint_text_;
    paint_part(painter, *this, part);
    part.text = {};
    part.role = PartRole::divider;
    part.bounds = parts.group_divider.bounds;
    paint_part(painter, *this, part);
}

// The header lines among the visible lines, in order.
std::size_t ListView::realized_group_count(const Layout& placed) const noexcept {
    if (!placed.grouped) return 0U;
    std::size_t count = 0U;
    for (std::size_t line = placed.top_line; line < placed.top_line + placed.visible_lines; ++line) {
        if (header_line(placed, line)) ++count;
    }
    return count;
}

std::size_t ListView::realized_group_band(const Layout& placed, std::size_t position) const noexcept {
    std::size_t seen = 0U;
    for (std::size_t line = placed.top_line; line < placed.top_line + placed.visible_lines; ++line) {
        if (!header_line(placed, line)) continue;
        if (seen == position) return band_of_line(placed, line);
        ++seen;
    }
    return placed.bands.size();
}

// A header line's parts: the band across the viewport, the caption inset
// in it, and the rule along its foot.
ListView::GroupParts ListView::group_parts(const Layout& placed, std::size_t band, Rect clip) const {
    const GroupBand& record = placed.bands[band];
    const double y = placed.viewport.y + line_offset(placed, record.first_line) - line_offset(placed, placed.top_line);
    const Rect area{placed.viewport.x, y, placed.viewport.width, placed.header_extent};
    const double inset = group_caption_inset * placed.scale;
    const double rule = placed.scale;
    const Rect caption{area.x + inset, area.y, std::max(0.0, area.width - inset * 2.0),
                       std::max(0.0, area.height - rule * 2.0)};
    const Rect divider{area.x + inset, area.y + area.height - rule * 2.0, std::max(0.0, area.width - inset * 2.0),
                       rule};
    GroupParts parts;
    parts.group_band = PartGeometry::from_bounds(Rect::intersection(area, clip));
    parts.group_caption = PartGeometry::from_bounds(Rect::intersection(caption, clip));
    parts.group_divider = PartGeometry::from_bounds(Rect::intersection(divider, clip));
    return parts;
}

void ListView::on_paint(Painter& painter, Rect) {
    const Layout placed = layout();
    AnatomyStateSet control_states;
    const Window* const host = attached_window();
    if (focused_ && host != nullptr && (*host).focus_cue_visible()) {
        control_states.insert(AnatomyState::focus_visible);
    }
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
        const std::size_t groups = realized_group_count(placed);
        for (std::size_t group = 0; group < groups; ++group) {
            paint_group(painter, placed, realized_group_band(placed, group));
        }
        const std::size_t end = end_realized(placed);
        for (std::size_t position = first_realized(placed); position < end; ++position) {
            // An owner-draw handler that changed the model ends this paint;
            // the change has already asked for the next one.
            if (!paint_item(painter, placed, index_at(placed, position))) {
                painter.restore();
                return;
            }
        }
        const Rect mark = insertion_mark_bounds(placed);
        if (!mark.empty()) {
            // The drop role has no recipe of its own until T2: a button
            // plate, as PageStrip's insertion mark.
            PartPaint bar;
            bar.family = ControlVisualRole::button;
            bar.role = PartRole::drop;
            bar.bounds = mark;
            bar.states.insert(AnatomyState::drop_accepted);
            paint_part(painter, *this, bar);
        }
        painter.restore();
    }
    if (!placed.corner.empty()) {
        PartPaint corner;
        corner.family = ControlVisualRole::panel;
        corner.role = PartRole::face;
        corner.bounds = placed.corner;
        paint_part(painter, *this, corner);
    }
    // With no item to carry the focus cue, the view itself shows it.
    if (placed.shown_count == 0U) {
        PartPaint focus;
        focus.family = ControlVisualRole::editor;
        focus.role = PartRole::focus;
        focus.bounds = placed.well;
        focus.states = control_states;
        paint_part(painter, *this, focus);
    }
}

// -- input -------------------------------------------------------------------

ListView::Hit ListView::hit_test(const Layout& placed, Point local) const {
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
    const double across = local.x - placed.viewport.x;
    const double down = local.y - placed.viewport.y;
    std::size_t index = 0U;
    if (placed.grouped) {
        const std::size_t line = line_at_offset(placed, line_offset(placed, placed.top_line) + down);
        const std::size_t along =
            view_ == ListViewView::details ? 0U : static_cast<std::size_t>(across / placed.cell.width);
        if (along >= line_items(placed, line)) return hit;
        index = index_at(placed, line_first(placed, line) + along);
    } else {
        std::size_t line = 0U;
        std::size_t position = 0U;
        if (view_ == ListViewView::details) {
            line = placed.top_line + static_cast<std::size_t>(down / placed.row_height);
        } else if (view_ == ListViewView::list) {
            line = placed.top_line + static_cast<std::size_t>(across / placed.cell.width);
            position = static_cast<std::size_t>(down / placed.cell.height);
        } else {
            line = placed.top_line + static_cast<std::size_t>(down / placed.cell.height);
            position = static_cast<std::size_t>(across / placed.cell.width);
        }
        if (position >= placed.per_line) return hit;
        index = line * placed.per_line + position;
        if (index >= placed.shown_count) return hit;
    }
    if (!item_bounds(placed, index).contains(local)) return hit;
    hit.index = index;
    hit.zone = HitZone::item;
    if (check_boxes_) {
        const ItemParts parts = item_parts(placed, index, placed.viewport);
        if (parts.check_well.present && parts.check_well.bounds.contains(local)) hit.zone = HitZone::check;
    }
    return hit;
}

// The cut-off text under the pointer, revealed in place (owner decision,
// 2026-10-28): the details view's cell under the pointer, or the list
// view's item text. The reveal starts just before the text, so the icon and
// the check box stay in view, and keeps the row's states. Owner-drawn
// content is the application's, so its text is never revealed.
void ListView::reveal_cut_off_text(const Layout& placed, const Hit& hit, Point local) {
    const bool revealing_view = view_ == ListViewView::details || view_ == ListViewView::list;
    if (hit.zone != HitZone::item || !revealing_view || draw_mode_ == ItemDrawMode::owner_draw ||
        label_edit_index_) {
        conceal_cut_off_text();
        return;
    }
    ListViewItem scratch;
    const ListViewItem& item = item_at(hit.index, scratch);
    const Rect cell = item_bounds(placed, hit.index);
    const ItemParts parts = item_parts(placed, hit.index, cell);
    AnatomyStateSet states = item_states(hit.index, item.checked);
    std::size_t column = 0U;
    Rect text_cell = parts.text.bounds;
    double row_right = parts.item_face.bounds.x + parts.item_face.bounds.width;
    if (view_ == ListViewView::details) {
        std::optional<std::size_t> under{};
        for (std::size_t candidate = 0; candidate < columns_.size() && !under; ++candidate) {
            if (!column_visible(placed, candidate)) continue;
            const Rect header = column_cell(placed, candidate);
            if (local.x >= header.x && local.x < header.x + header.width) under = candidate;
        }
        if (!under) {
            conceal_cut_off_text();
            return;
        }
        column = *under;
        const Rect header = column_cell(placed, column);
        row_right = header.x + header.width;
        if (column > 0U) {
            const Rect sub_cell{header.x, cell.y, header.width, cell.height};
            text_cell = sub_item_parts(placed, hit.index, column, sub_cell).sub_item_text.bounds;
            // A sub-item is painted selected or disabled, never hot (paint_item).
            AnatomyStateSet sub_states;
            if (states.contains(AnatomyState::selected)) sub_states.insert(AnatomyState::selected);
            if (states.contains(AnatomyState::disabled)) sub_states.insert(AnatomyState::disabled);
            states = sub_states;
        }
    }
    const std::string_view text = column_text(item, column);
    if (text.empty() || text_cell.width <= 0.0) {
        conceal_cut_off_text();
        return;
    }
    const Rect absolute = absolute_bounds();
    const double row_left = text_cell.x - reveal_lead;
    detail::CutOffText revealed{};
    revealed.key = std::to_string(hit.index) + "#" + std::to_string(column);
    revealed.row = {absolute.x + row_left, absolute.y + cell.y, std::max(0.0, row_right - row_left), cell.height};
    revealed.cell = {absolute.x + text_cell.x, absolute.y + text_cell.y, text_cell.width, text_cell.height};
    revealed.text = std::string(text);
    revealed.font = text_font(font_);
    revealed.paint = detail::RevealPaint{.family = ControlVisualRole::selection, .states = states};
    static_cast<void>((*reveal_).reveal(shared_from_this(), std::move(revealed)));
}

void ListView::conceal_cut_off_text() noexcept {
    if (reveal_) (*reveal_).conceal();
}

// -- the insertion mark --------------------------------------------------------

// The mark lies along the gap beside its item: across the top or bottom of
// a details row or a list-view cell, down the left or right of an icon
// view's cell, centred on the cell's edge and clipped to the viewport.
Rect ListView::insertion_mark_bounds(const Layout& placed) const noexcept {
    if (!insertion_mark_ || (*insertion_mark_).index >= placed.shown_count) return {};
    const ListViewInsertionMark mark = *insertion_mark_;
    const Rect cell = item_bounds(placed, mark.index);
    const double thickness = insertion_thickness * placed.scale;
    // The details and list views stack items down a line, so their gaps
    // run between rows; the icon views' gaps run between cells of a row.
    const bool between_rows = view_ == ListViewView::details || view_ == ListViewView::list;
    Rect bar{};
    if (between_rows) {
        const double edge = mark.after ? cell.y + cell.height : cell.y;
        bar = Rect{cell.x, edge - thickness * 0.5, cell.width, thickness};
    } else {
        const double edge = mark.after ? cell.x + cell.width : cell.x;
        bar = Rect{edge - thickness * 0.5, cell.y, thickness, cell.height};
    }
    const Rect shown = Rect::intersection(bar, placed.viewport);
    return shown;
}

bool ListView::show_insertion_mark(ListViewInsertionMark mark) {
    require_mutable();
    if (mark.index >= item_count()) {
        clear_insertion_mark();
        return false;
    }
    if (insertion_mark_ && *insertion_mark_ == mark) return true;
    insertion_mark_ = mark;
    invalidate(Dirty::paint);
    return true;
}

void ListView::clear_insertion_mark() noexcept {
    if (!insertion_mark_) return;
    insertion_mark_.reset();
    invalidate(Dirty::paint);
}

std::optional<ListViewInsertionMark> ListView::insertion_mark_at(Point window_point) const {
    const Layout placed = layout();
    if (placed.shown_count == 0U || placed.viewport.empty()) return std::nullopt;
    const Rect absolute = absolute_bounds();
    const Point local{window_point.x - absolute.x, window_point.y - absolute.y};
    // The point is clamped into the viewport, so that a drag past an edge
    // names the nearest item there.
    const Rect area = placed.viewport;
    const double across = std::clamp(local.x - area.x, 0.0, std::max(area.width - 1.0, 0.0));
    const double down = std::clamp(local.y - area.y, 0.0, std::max(area.height - 1.0, 0.0));
    std::size_t index = 0U;
    if (placed.grouped) {
        std::size_t line = line_at_offset(placed, line_offset(placed, placed.top_line) + down);
        // A header names its group's first line.
        if (header_line(placed, line)) ++line;
        std::size_t along =
            view_ == ListViewView::details ? 0U : static_cast<std::size_t>(across / placed.cell.width);
        along = std::min(along, line_items(placed, line) - 1U);
        index = index_at(placed, line_first(placed, line) + along);
    } else {
        std::size_t line = placed.top_line;
        std::size_t position = 0U;
        if (view_ == ListViewView::details) {
            line += static_cast<std::size_t>(down / placed.row_height);
        } else if (view_ == ListViewView::list) {
            line += static_cast<std::size_t>(across / placed.cell.width);
            position = static_cast<std::size_t>(down / placed.cell.height);
        } else {
            line += static_cast<std::size_t>(down / placed.cell.height);
            position = static_cast<std::size_t>(across / placed.cell.width);
        }
        position = std::min(position, placed.per_line - 1U);
        index = std::min(line * placed.per_line + position, placed.shown_count - 1U);
    }
    const Rect cell = item_bounds(placed, index);
    const bool between_rows = view_ == ListViewView::details || view_ == ListViewView::list;
    const Point clamped{area.x + across, area.y + down};
    const bool after = between_rows ? clamped.y >= cell.y + cell.height * 0.5
                                    : clamped.x >= cell.x + cell.width * 0.5;
    const ListViewInsertionMark mark{index, after};
    return mark;
}

std::optional<std::size_t> ListView::item_index_at(Point window_point) const {
    const Rect absolute = absolute_bounds();
    const Point local{window_point.x - absolute.x, window_point.y - absolute.y};
    const Hit hit = hit_test(layout(), local);
    if (hit.zone != HitZone::item && hit.zone != HitZone::check) return std::nullopt;
    return hit.index;
}

void ListView::finish_column_resize() {
    if (!resizing_column_) return;
    const std::size_t column = *resizing_column_;
    resizing_column_.reset();
    if (has_pointer_capture()) set_pointer_capture(false);
    invalidate(Dirty::paint | Dirty::semantics);
    if (column >= columns_.size() || columns_[column].width == resize_start_width_) return;
    const ListViewColumnWidthChange change{std::string(stable_id().value()), column, columns_[column].width};
    column_width_changed_.emit(change);
}

void ListView::request_column_click(std::size_t column) {
    if (column >= columns_.size()) return;
    const ListViewColumnClick click{std::string(stable_id().value()), column, columns_[column].stable_id};
    column_click_.emit(click);
}

void ListView::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const Control::Ptr lifetime = shared_from_this();
    if (resizing_column_) {
        conceal_cut_off_text();
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
                    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
                }
            }
            event.handled = true;
            return;
        }
    }
    if (event.action == PointerAction::down && label_edit_index_) {
        // A press in the view commits the edit, then is handled against the
        // view as the commit's handlers left it.
        end_label_edit();
        if (!is_alive() || label_edit_index_) return;
        on_pointer(event);
        return;
    }
    const Layout placed = layout();
    if (event.action == PointerAction::leave) {
        if (hot_index_ || hot_column_) {
            hot_index_.reset();
            conceal_cut_off_text();
            hot_column_.reset();
            invalidate(Dirty::paint);
        }
        return;
    }
    const Hit hit = hit_test(placed, local);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        const bool on_item = hit.zone == HitZone::item || hit.zone == HitZone::check;
        const bool on_header = hit.zone == HitZone::header;
        const std::optional<std::size_t> next_hot = on_item ? std::optional<std::size_t>(hit.index) : std::nullopt;
        const std::optional<std::size_t> next_column =
            on_header ? std::optional<std::size_t>(hit.index) : std::nullopt;
        cursor = hit.zone == HitZone::divider ? CursorKind::resize_horizontal : CursorKind::arrow;
        if (next_hot != hot_index_ || next_column != hot_column_) {
            hot_index_ = next_hot;
            hot_column_ = next_column;
            invalidate(Dirty::paint);
        }
        reveal_cut_off_text(placed, hit, local);
        return;
    }
    // A press goes to the row under the reveal, which then shows the row as
    // the press leaves it.
    if (event.action == PointerAction::down) conceal_cut_off_text();
    if (event.action == PointerAction::wheel) {
        if (event.wheel_delta.y == 0.0 && event.wheel_delta.x == 0.0) return;
        cancel_label_click();
        const bool sideways = event.wheel_delta.x != 0.0 || has_modifier(event.modifiers, Modifier::shift);
        if (sideways && view_ == ListViewView::details) {
            const double delta = event.wheel_delta.x != 0.0 ? event.wheel_delta.x : event.wheel_delta.y;
            set_horizontal_offset(std::max(0.0, horizontal_offset_ - delta * horizontal_key_step * 2.0));
        } else {
            const double delta = event.wheel_delta.y != 0.0 ? event.wheel_delta.y : event.wheel_delta.x;
            scroll_lines(delta > 0.0 ? -wheel_lines : wheel_lines);
        }
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::down) {
        if (event.button != PointerButton::primary && event.button != PointerButton::secondary) return;
        // Any press ends a pending slow click; this one may start another.
        // The press is a slow click on a label when it is a single primary
        // press, without modifiers, on the text of the item that was already
        // focused and selected while the view had focus.
        cancel_label_click();
        const bool slow_click = label_edit_ && focused_ && event.button == PointerButton::primary &&
                                event.click_count == 1U && event.modifiers == Modifier::none &&
                                hit.zone == HitZone::item && focused_index_ == hit.index &&
                                hit.index < selected_.size() && selected_[hit.index] != 0U &&
                                item_parts(placed, hit.index, placed.viewport).text.bounds.contains(local);
        if (window() != nullptr) static_cast<void>((*window()).request_focus(lifetime));
        if (!is_alive()) return;
        event.handled = true;
        if (event.button == PointerButton::secondary) {
            // A secondary press selects the item under it unless it is
            // already selected (context menus are the application's).
            if (hit.zone == HitZone::item && selected_[hit.index] == 0U) choose(hit.index, Modifier::none, false);
            return;
        }
        if (hit.zone == HitZone::divider) {
            resizing_column_ = hit.index;
            resize_start_x_ = local.x;
            resize_start_width_ = columns_[hit.index].width;
            if (window() != nullptr) (*window()).capture_pointer(lifetime, event.pointer_id);
            invalidate(Dirty::paint | Dirty::semantics);
        } else if (hit.zone == HitZone::header) {
            pressed_column_ = hit.index;
            if (window() != nullptr) (*window()).capture_pointer(lifetime, event.pointer_id);
            invalidate(Dirty::paint | Dirty::semantics);
        } else if (hit.zone == HitZone::check) {
            move_focus(hit.index);
            toggle_check(hit.index);
        } else if (hit.zone == HitZone::item) {
            if (event.click_count >= 2U) {
                activate(hit.index);
            } else {
                choose(hit.index, event.modifiers, false);
                if (slow_click && is_alive()) schedule_label_click(hit.index);
            }
        } else if (!placed.viewport.empty() && placed.viewport.contains(local) &&
                   event.modifiers == Modifier::none) {
            if (select_none()) publish_selection();
        }
        return;
    }
    if (event.action == PointerAction::up && event.button == PointerButton::primary && pressed_column_) {
        const std::size_t pressed = *pressed_column_;
        pressed_column_.reset();
        if (has_pointer_capture()) set_pointer_capture(false);
        invalidate(Dirty::paint | Dirty::semantics);
        event.handled = true;
        // A click is a press and release on the same header.
        if (hit.zone == HitZone::header && hit.index == pressed) request_column_click(pressed);
    }
}

std::optional<std::size_t> ListView::navigation_target(std::uint32_t key, std::size_t from,
                                                       const Layout& placed) const {
    if (placed.grouped) return grouped_navigation_target(key, from, placed);
    const std::size_t count = placed.shown_count;
    const std::size_t last = count - 1U;
    const std::size_t per = placed.per_line;
    const std::size_t page = placed.page_lines * per;
    // The step of each arrow: along a line (`per` items apart in the list
    // view's columns and the icon views' rows) or to the next item.
    std::size_t vertical = 1U;
    std::size_t horizontal = 0U;
    if (view_ == ListViewView::list) {
        horizontal = per;
    } else if (view_ != ListViewView::details) {
        vertical = per;
        horizontal = 1U;
    }
    if (key == PhysicalKey::home) return std::size_t{0U};
    if (key == PhysicalKey::end) return last;
    if (key == PhysicalKey::up) return from >= vertical ? from - vertical : from;
    if (key == PhysicalKey::down) return std::min(from + vertical, last);
    if (key == PhysicalKey::page_up) return from >= page ? from - page : std::size_t{0U};
    if (key == PhysicalKey::page_down) return std::min(from + page, last);
    if (horizontal == 0U) return std::nullopt;
    if (key == PhysicalKey::left) return from >= horizontal ? from - horizontal : from;
    if (key == PhysicalKey::right) return std::min(from + horizontal, last);
    return std::nullopt;
}

// The arrows in a grouped view move through the shown order: Up and Down
// to the item line above or below, across group headers, keeping the place
// along the line; Left and Right to the previous or next shown item in the
// icon and tile views.
std::optional<std::size_t> ListView::grouped_navigation_target(std::uint32_t key, std::size_t from,
                                                               const Layout& placed) const {
    const std::size_t last = placed.shown_count - 1U;
    const std::size_t position = position_of(placed, from);
    if (key == PhysicalKey::home) return index_at(placed, 0U);
    if (key == PhysicalKey::end) return index_at(placed, last);
    const bool up = key == PhysicalKey::up || key == PhysicalKey::page_up;
    const bool down = key == PhysicalKey::down || key == PhysicalKey::page_down;
    if (up || down) {
        const bool page = key == PhysicalKey::page_up || key == PhysicalKey::page_down;
        const std::size_t steps = page ? placed.page_lines : 1U;
        const std::size_t line = position_line(placed, position);
        const std::size_t along = position - line_first(placed, line);
        std::size_t target = line;
        for (std::size_t step = 0; step < steps; ++step) {
            const std::size_t next = next_item_line(placed, target, down);
            if (next == target) break;
            target = next;
        }
        // A page key with no line left goes to the first or last item.
        if (page && target == line) return index_at(placed, down ? last : 0U);
        const std::size_t moved = line_first(placed, target) + std::min(along, line_items(placed, target) - 1U);
        return index_at(placed, moved);
    }
    if (view_ == ListViewView::details) return std::nullopt;
    if (key == PhysicalKey::left) return index_at(placed, position > 0U ? position - 1U : position);
    if (key == PhysicalKey::right) return index_at(placed, std::min(position + 1U, last));
    return std::nullopt;
}

void ListView::on_key_preview(KeyEvent& event) {
    // Keys for a label edit in progress, before the editor sees them.
    if (!label_edit_index_ || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::escape) {
        cancel_label_edit();
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::enter) {
        end_label_edit();
        event.handled = true;
    }
}

void ListView::on_key(KeyEvent& event) {
    if (!focused_ || !effectively_enabled() || event.action != KeyAction::down) return;
    // A key ends a pending slow click.
    cancel_label_click();
    if (event.physical_key == PhysicalKey::escape && resizing_column_) {
        columns_[*resizing_column_].width = resize_start_width_;
        finish_column_resize();
        invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
        event.handled = true;
        return;
    }
    const Layout placed = layout();
    if (placed.shown_count == 0U) return;
    const bool control = has_modifier(event.modifiers, Modifier::control) ||
                         has_modifier(event.modifiers, Modifier::meta);
    const bool shift = has_modifier(event.modifiers, Modifier::shift);
    const std::size_t from = std::min(focused_index_.value_or(0U), placed.shown_count - 1U);
    if (event.physical_key == PhysicalKey::f2 && label_edit_ && focused_index_) {
        static_cast<void>(begin_label_edit(*focused_index_));
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::a && control && !shift) {
        select_all();
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::enter) {
        if (focused_index_) activate(*focused_index_);
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::space) {
        if (check_boxes_ && !control) {
            toggle_check(from);
        } else if (control && multi_select_) {
            const bool changed = toggle_selected(from);
            anchor_index_ = from;
            move_focus(from);
            if (changed) publish_selection();
        } else {
            choose(from, Modifier::none, false);
        }
        event.handled = true;
        return;
    }
    const std::optional<std::size_t> target = navigation_target(event.physical_key, from, placed);
    if (!target) {
        // The details view scrolls sideways with Left and Right.
        const bool sideways = event.physical_key == PhysicalKey::left || event.physical_key == PhysicalKey::right;
        if (!sideways || view_ != ListViewView::details) return;
        const double step = event.physical_key == PhysicalKey::left ? -horizontal_key_step : horizontal_key_step;
        set_horizontal_offset(std::max(0.0, placed.horizontal_offset + step));
        event.handled = true;
        return;
    }
    // Control with an arrow moves the focus without selecting; Shift extends
    // the selection from the anchor (Control and Shift adds the range).
    choose(*target, event.modifiers, control && !shift);
    event.handled = true;
}

void ListView::type_select(std::string_view text) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - last_type_time_ > collection_detail::type_timeout) type_prefix_.clear();
    last_type_time_ = now;
    type_prefix_ += collection_detail::fold_ascii(text);
    const std::size_t count = items_.size();
    if (count == 0U) return;
    // A repeated first letter steps to the next match; a longer prefix
    // starts its search at the focused item. The search follows the shown
    // order.
    const Layout placed = layout();
    const bool single = type_prefix_.size() == 1U;
    const std::size_t focus = focused_index_ ? position_of(placed, *focused_index_) : count - 1U;
    const std::size_t start = single ? (focus + 1U) % count : focus % count;
    for (std::size_t offset = 0; offset < count; ++offset) {
        const std::size_t index = index_at(placed, (start + offset) % count);
        if (collection_detail::starts_with_folded(items_[index].text, type_prefix_)) {
            choose(index, Modifier::none, false);
            return;
        }
    }
}

void ListView::on_text_input(TextInputEvent& event) {
    // Type-ahead searches the view's own items. A virtual view's items are
    // the application's: searching them is not supported yet.
    if (!focused_ || !effectively_enabled() || source_ || event.composing || event.text_utf8.empty() ||
        !validate_utf8(event.text_utf8).valid()) {
        return;
    }
    // Space belongs to on_key (check or select); it never starts a search.
    if (event.text_utf8 == " " && type_prefix_.empty()) return;
    type_select(event.text_utf8);
    event.handled = true;
}

void ListView::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused) {
        cancel_label_click();
        if (resizing_column_) finish_column_resize();
        if (pressed_column_) {
            pressed_column_.reset();
            if (has_pointer_capture()) set_pointer_capture(false);
        }
    }
    if (focused_ && !focused_index_ && item_count() != 0U) {
        std::size_t first = 0U;
        while (first < selected_.size() && selected_[first] == 0U) ++first;
        focused_index_ = first < selected_.size() ? first : top_index();
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- label editing -------------------------------------------------------------

void ListView::set_label_edit(bool enabled) {
    require_mutable();
    if (label_edit_ == enabled) return;
    label_edit_ = enabled;
    if (!enabled) {
        cancel_label_click();
        cancel_label_edit();
    }
}

// The slow click's timer finds the view through a weak reference; a view
// that is gone or disposed does nothing.
struct ListView::LabelClickElapsed final {
    std::weak_ptr<Control> target;

    void operator()(FrameTime) const {
        const std::shared_ptr<Control> control = target.lock();
        if (!control || !(*control).is_alive()) return;
        static_cast<ListView&>(*control).label_click_elapsed();
    }
};

// The wait is the double-click interval the window holds (the platform's
// setting, or GUI.Forms' stated default): a second press within it is a
// double click, which activates the item instead.
void ListView::schedule_label_click(std::size_t index) {
    Window* const host = window();
    if (host == nullptr) return;
    const std::chrono::milliseconds wait((*host).presentation_settings().double_click_milliseconds);
    label_click_ = PendingLabelClick{index, model_revision_};
    const std::weak_ptr<Control> weak = shared_from_this();
    label_click_timer_ = (*host).schedule_ui_timer(*this, std::chrono::hours(24), FrameClock::now() + wait,
                                                   LabelClickElapsed{weak});
}

void ListView::cancel_label_click() noexcept {
    label_click_timer_.disconnect();
    label_click_.reset();
}

// The edit begins only if nothing has moved on since the press: the same
// model, the item still focused and selected, the view focused, editing
// still on, and no edit in progress.
void ListView::label_click_elapsed() {
    const std::optional<PendingLabelClick> pending = label_click_;
    cancel_label_click();
    if (!pending || !label_edit_ || !focused_ || label_edit_index_) return;
    const PendingLabelClick& click = *pending;
    if (click.revision != model_revision_ || focused_index_ != click.index) return;
    if (click.index >= selected_.size() || selected_[click.index] == 0U || click.index >= item_count()) return;
    static_cast<void>(begin_label_edit(click.index));
}

// The editor covers the item's text part, as item_parts places it for the
// item's view; a scrolled-out item's editor keeps the focus at no size until
// the item is shown again.
Rect ListView::label_editor_bounds(const Layout& placed) const {
    if (!label_edit_index_ || *label_edit_index_ >= placed.shown_count) return {};
    const ItemParts parts = item_parts(placed, *label_edit_index_, placed.viewport);
    return parts.text.bounds;
}

void ListView::place_label_editor(const Layout& placed) {
    if (!label_editor_ || !label_edit_index_) return;
    set_child_layout(label_editor_, label_editor_bounds(placed));
}

// The editor is made on the view's first edit, so that a view which is never
// edited carries no TextBox.
void ListView::create_label_editor() {
    const std::string prefix(stable_id().value());
    label_editor_ = make_control<TextBox>(StableId(prefix + ".label-editor"));
    TextBox& editor = *label_editor_;
    editor.set_border_style(BorderStyle::none);
    editor.set_font(font_);
    add_child(label_editor_);
    set_child_layout_collapsed(label_editor_, true);
    label_editor_focus_ = editor.focus_observed().subscribe(
        *this, Delegate<bool>::bind<ListView, &ListView::on_label_editor_focus>(*this));
}

ListViewLabelEdit ListView::label_edit_event(std::optional<std::string> label) const {
    ListViewLabelEdit event;
    event.list_id = std::string(stable_id().value());
    event.index = label_edit_index_.value_or(0U);
    event.item_id = label_edit_item_id_;
    event.label = std::move(label);
    return event;
}

bool ListView::begin_label_edit(std::size_t index) {
    require_mutable();
    cancel_label_click();
    if (index >= item_count()) throw std::out_of_range("ListView item index is past the end");
    if (!label_edit_) return false;
    if (label_edit_index_ && *label_edit_index_ == index) return true;
    const Control::Ptr lifetime = shared_from_this();
    if (label_edit_index_) {
        end_label_edit();
        // The commit's handlers may change or end the view, or begin an edit.
        if (!is_alive() || !label_edit_ || index >= item_count()) return false;
        if (label_edit_index_) return *label_edit_index_ == index;
    }
    if (index >= layout().shown_count) return false;
    ListViewItem scratch;
    const std::string item_id = item_at(index, scratch).stable_id;
    ListViewLabelEdit request;
    request.list_id = std::string(stable_id().value());
    request.index = index;
    request.item_id = item_id;
    before_label_edit_.emit(request);
    if (request.cancel || !is_alive() || !label_edit_ || label_edit_index_) return false;
    // A handler may have changed the items: the edit begins only on the item
    // it was asked for, still shown at `index`.
    if (index >= item_count() || index >= layout().shown_count) return false;
    const ListViewItem& item = item_at(index, scratch);
    if (item.stable_id != item_id) return false;
    std::string text = item.text;
    ensure_visible(index);
    if (!label_editor_) create_label_editor();
    label_edit_index_ = index;
    label_edit_item_id_ = item_id;
    TextBox& editor = *label_editor_;
    editor.set_text(std::move(text));
    set_child_layout_collapsed(label_editor_, false);
    place_label_editor(layout());
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    Window* const host = window();
    if (host != nullptr) static_cast<void>((*host).request_focus(label_editor_));
    // Focus handlers may end the edit or the view.
    if (!is_alive() || !label_edit_index_ || *label_edit_index_ != index) return false;
    editor.select_all();
    return true;
}

void ListView::end_label_edit() {
    if (!label_edit_index_ || closing_label_editor_) return;
    const Control::Ptr lifetime = shared_from_this();
    const std::string label((*label_editor_).text());
    const std::string item_id = label_edit_item_id_;
    ListViewLabelEdit ended = label_edit_event(label);
    close_label_editor();
    if (!is_alive()) return;
    after_label_edit_.emit(ended);
    if (!is_alive() || ended.cancel) return;
    if (source_) {
        // The application stored the label; the view retrieves it again.
        invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
        return;
    }
    // The label goes to the item with the edited ID, wherever a handler has
    // moved it; an item a handler removed takes nothing.
    const std::optional<std::size_t> at = index_of(item_id);
    if (!at || items_[*at].text == label) return;
    items_[*at].text = label;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListView::cancel_label_edit() {
    if (!label_edit_index_ || closing_label_editor_) return;
    const Control::Ptr lifetime = shared_from_this();
    ListViewLabelEdit ended = label_edit_event(std::nullopt);
    close_label_editor();
    if (!is_alive()) return;
    after_label_edit_.emit(ended);
}

void ListView::close_label_editor() {
    closing_label_editor_ = true;
    Window* const host = window();
    const bool editor_focused = host != nullptr && label_editor_ && (*host).focused_control() == label_editor_;
    label_edit_index_.reset();
    label_edit_item_id_.clear();
    if (editor_focused) static_cast<void>((*host).request_focus(shared_from_this()));
    if (label_editor_) set_child_layout_collapsed(label_editor_, true);
    closing_label_editor_ = false;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ListView::on_label_editor_focus(bool focused) {
    if (focused || !label_edit_index_ || closing_label_editor_ || !is_alive()) return;
    // Focus left the editor for another control: the user has moved on, and
    // the edit commits.
    end_label_edit();
}

// -- semantics -----------------------------------------------------------------

SemanticDescriptor ListView::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    // The details view is a table (12.13) whose rows hold a cell per
    // column, sized by every item and every column, realized or not; the
    // other views are lists.
    if (view_ == ListViewView::details) {
        descriptor.role = SemanticRole::table;
        descriptor.row_count = item_count();
        descriptor.column_count = columns_.size();
    } else {
        descriptor.role = SemanticRole::list;
    }
    if (multi_select_) descriptor.states |= SemanticState::multiselectable;
    descriptor.name = accessible_name.type();
    descriptor.description = accessible_description.type();
    if (selected_count_ == 1U && focused_index_ && selected_[*focused_index_] != 0U) {
        ListViewItem scratch;
        descriptor.value = item_at(*focused_index_, scratch).text;
    } else if (selected_count_ != 0U) {
        descriptor.value = std::to_string(selected_count_) + " selected";
    }
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> ListView::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const Layout placed = layout();
    const Rect absolute = absolute_bounds();
    const std::string list_id(stable_id().value());
    const bool enabled = effectively_enabled();
    const SemanticState enabled_state = enabled ? SemanticState::enabled : SemanticState::none;
    // Column headers first, in reading order; a header's press raises
    // column_click.
    for (std::size_t column = 0; column < columns_.size(); ++column) {
        if (!column_visible(placed, column)) continue;
        const Rect bounds = Rect::intersection(column_cell(placed, column), placed.header);
        SemanticNode node{};
        node.stable_id = column_identity(list_id, columns_[column].stable_id);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::column_header;
        node.column_index = column + 1U;
        node.name = columns_[column].text;
        if (sort_column_ == column && sort_order_ == ListViewSortOrder::ascending) node.value = "ascending";
        if (sort_column_ == column && sort_order_ == ListViewSortOrder::descending) node.value = "descending";
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible | enabled_state;
        node.actions = {SemanticAction::press};
        nodes.push_back(std::move(node));
    }
    const std::size_t first = first_realized(placed);
    const std::size_t end = end_realized(placed);
    ListViewItem scratch;
    // A grouped view's items are the children of a `group` node per shown
    // group, named by its header.
    std::optional<std::size_t> open_band{};
    SemanticNode group_node{};
    for (std::size_t position = first; position < end; ++position) {
        const std::size_t index = index_at(placed, position);
        const ListViewItem& item = item_at(index, scratch);
        const Rect bounds = Rect::intersection(item_bounds(placed, index), placed.viewport);
        SemanticNode node{};
        node.stable_id = item.stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.name = item.text;
        if (view_ == ListViewView::details) {
            node.role = SemanticRole::row;
            node.row_index = position + 1U;
            append_row_cells(placed, index, item, node);
        } else {
            node.role = check_boxes_ ? SemanticRole::check_list_item : SemanticRole::list_item;
        }
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible | SemanticState::focusable | enabled_state;
        if (selected_[index] != 0U) node.states |= SemanticState::selected;
        if (focused_ && focused_index_ == index) node.states |= SemanticState::focused;
        if (check_boxes_) {
            node.value = item.checked ? "checked" : "unchecked";
            node.states |= SemanticState::checkable;
            if (item.checked) node.states |= SemanticState::checked;
        }
        node.actions = {SemanticAction::focus, SemanticAction::select, SemanticAction::press};
        if (!placed.grouped) {
            nodes.push_back(std::move(node));
            continue;
        }
        const std::size_t band = band_of_position(placed, position);
        if (open_band != band) {
            if (open_band) nodes.push_back(std::move(group_node));
            open_band = band;
            const GroupBand& record = placed.bands[band];
            const std::size_t lines = 1U + ceiling_quotient(record.count, placed.per_line);
            const double top = placed.viewport.y + line_offset(placed, record.first_line) -
                               line_offset(placed, placed.top_line);
            const double height = line_offset(placed, record.first_line + lines) -
                                  line_offset(placed, record.first_line);
            const Rect extent = Rect::intersection({placed.viewport.x, top, placed.viewport.width, height},
                                                   placed.viewport);
            group_node = SemanticNode{};
            group_node.stable_id = group_identity(record.group);
            group_node.runtime_id = virtual_semantic_runtime_id(group_node.stable_id);
            group_node.role = SemanticRole::group;
            group_node.name = group_header(record.group);
            group_node.bounds = {absolute.x + extent.x, absolute.y + extent.y, extent.width, extent.height};
            group_node.states = SemanticState::visible | enabled_state;
        }
        group_node.children.push_back(std::move(node));
    }
    if (open_band) nodes.push_back(std::move(group_node));
    return nodes;
}

// A details row's cells, one per column, for the columns whose extent meets
// the viewport (the realized columns); the first column's cell holds the
// item's text. A cell has no actions of its own; its row takes them.
void ListView::append_row_cells(const Layout& placed, std::size_t index, const ListViewItem& item,
                                SemanticNode& row_node) const {
    const Rect absolute = absolute_bounds();
    const Rect line = item_bounds(placed, index);
    const bool enabled = effectively_enabled();
    const SemanticState enabled_state = enabled ? SemanticState::enabled : SemanticState::none;
    for (std::size_t column = 0; column < columns_.size(); ++column) {
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
        cell.row_index = position_of(placed, index) + 1U;
        cell.column_index = column + 1U;
        cell.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        cell.states = SemanticState::visible | enabled_state;
        row_node.children.push_back(std::move(cell));
    }
}

std::optional<std::size_t> ListView::realized_index_of(std::string_view item_id) const {
    const std::optional<std::size_t> known = index_of(item_id);
    if (known) return known;
    const Layout placed = layout();
    const std::size_t end = end_realized(placed);
    ListViewItem scratch;
    for (std::size_t position = first_realized(placed); position < end; ++position) {
        const std::size_t index = index_at(placed, position);
        if (item_at(index, scratch).stable_id == item_id) return index;
    }
    return std::nullopt;
}

bool ListView::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    if (!effectively_enabled()) return false;
    const std::string list_id(stable_id().value());
    for (std::size_t column = 0; column < columns_.size(); ++column) {
        if (id != column_identity(list_id, columns_[column].stable_id)) continue;
        if (action != SemanticAction::press) return false;
        request_column_click(column);
        return true;
    }
    const std::optional<std::size_t> index = realized_index_of(id);
    if (!index || *index >= item_count()) return false;
    if (action != SemanticAction::focus && action != SemanticAction::select && action != SemanticAction::press) {
        return false;
    }
    const Control::Ptr lifetime = shared_from_this();
    if (window() != nullptr) static_cast<void>((*window()).request_focus(lifetime));
    if (!is_alive()) return true;
    if (action == SemanticAction::focus) {
        move_focus(*index);
    } else if (action == SemanticAction::select) {
        choose(*index, Modifier::none, false);
    } else if (check_boxes_) {
        // Press toggles a check-list item, as CheckedListBox's do.
        move_focus(*index);
        toggle_check(*index);
    } else {
        activate(*index);
    }
    return true;
}

// -- anatomy -----------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 1> insertion_mark_states{{AnatomyState::drop_accepted}};

constexpr std::array<PartField<ListView::Parts>, 9> list_view_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ListView::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &ListView::Parts::well},
    {.declaration = {.name = "header", .role = PartRole::face, .composed_from = {}},
     .member = &ListView::Parts::header},
    // The child scroll bars, each a whole scroll bar system. A ranged path
    // is the nearest role to a composed scroll bar.
    {.declaration = {.name = "vertical_scroll_bar", .role = PartRole::track, .composed_from = "v_scroll_bar"},
     .member = &ListView::Parts::vertical_scroll_bar},
    {.declaration = {.name = "horizontal_scroll_bar", .role = PartRole::track,
                     .composed_from = "h_scroll_bar"},
     .member = &ListView::Parts::horizontal_scroll_bar},
    {.declaration = {.name = "scroll_corner", .role = PartRole::face, .composed_from = {}},
     .member = &ListView::Parts::scroll_corner},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ListView::Parts::focus},
    {.declaration = {.name = "insertion_mark", .role = PartRole::drop, .composed_from = {},
                     .states = insertion_mark_states},
     .member = &ListView::Parts::insertion_mark},
    {.declaration = {.name = "label_editor", .role = PartRole::well, .composed_from = "text_box"},
     .member = &ListView::Parts::label_editor},
}};

constexpr std::array<PartField<ListView::ItemParts>, 7> list_view_item_part_fields{{
    {.declaration = {.name = "cell", .role = PartRole::region, .composed_from = {}},
     .member = &ListView::ItemParts::cell},
    {.declaration = {.name = "item_face", .role = PartRole::face, .composed_from = {}},
     .member = &ListView::ItemParts::item_face},
    {.declaration = {.name = "check_well", .role = PartRole::well, .composed_from = {}},
     .member = &ListView::ItemParts::check_well},
    {.declaration = {.name = "check", .role = PartRole::indicator, .composed_from = {}},
     .member = &ListView::ItemParts::check},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &ListView::ItemParts::icon},
    {.declaration = {.name = "text", .role = PartRole::content, .composed_from = {}},
     .member = &ListView::ItemParts::text},
    {.declaration = {.name = "cell_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ListView::ItemParts::cell_focus},
}};

constexpr std::array<StateField<ListView::ItemStates>, 6> list_view_item_state_fields{{
    {.state = AnatomyState::selected, .member = &ListView::ItemStates::selected},
    {.state = AnatomyState::hot, .member = &ListView::ItemStates::hot},
    {.state = AnatomyState::current, .member = &ListView::ItemStates::current},
    {.state = AnatomyState::checked, .member = &ListView::ItemStates::checked},
    {.state = AnatomyState::disabled, .member = &ListView::ItemStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &ListView::ItemStates::focus_visible},
}};

constexpr std::array<PartField<ListView::SubItemParts>, 2> list_view_sub_item_part_fields{{
    {.declaration = {.name = "sub_item_cell", .role = PartRole::region, .composed_from = {}},
     .member = &ListView::SubItemParts::sub_item_cell},
    {.declaration = {.name = "sub_item_text", .role = PartRole::content, .composed_from = {}},
     .member = &ListView::SubItemParts::sub_item_text},
}};

constexpr std::array<StateField<ListView::SubItemStates>, 2> list_view_sub_item_state_fields{{
    {.state = AnatomyState::selected, .member = &ListView::SubItemStates::selected},
    {.state = AnatomyState::disabled, .member = &ListView::SubItemStates::disabled},
}};

constexpr std::array<PartField<ListView::ColumnParts>, 5> list_view_column_part_fields{{
    {.declaration = {.name = "column_header", .role = PartRole::face, .composed_from = {}},
     .member = &ListView::ColumnParts::column_header},
    {.declaration = {.name = "column_label", .role = PartRole::label, .composed_from = {}},
     .member = &ListView::ColumnParts::column_label},
    {.declaration = {.name = "sort_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &ListView::ColumnParts::sort_glyph},
    {.declaration = {.name = "column_divider", .role = PartRole::divider, .composed_from = {}},
     .member = &ListView::ColumnParts::column_divider},
    {.declaration = {.name = "column_grip", .role = PartRole::grip, .composed_from = {}},
     .member = &ListView::ColumnParts::column_grip},
}};

constexpr std::array<StateField<ListView::ColumnStates>, 3> list_view_column_state_fields{{
    {.state = AnatomyState::hot, .member = &ListView::ColumnStates::hot},
    {.state = AnatomyState::pressed, .member = &ListView::ColumnStates::pressed},
    {.state = AnatomyState::captured, .member = &ListView::ColumnStates::captured},
}};

constexpr std::array<CustomStateDeclaration, 2> list_view_column_custom_states{{
    {.name = "sorted_ascending"},
    {.name = "sorted_descending"},
}};

constexpr std::array<PartField<ListView::GroupParts>, 3> list_view_group_part_fields{{
    {.declaration = {.name = "group_band", .role = PartRole::region, .composed_from = {}},
     .member = &ListView::GroupParts::group_band},
    {.declaration = {.name = "group_caption", .role = PartRole::caption, .composed_from = {}},
     .member = &ListView::GroupParts::group_caption},
    {.declaration = {.name = "group_divider", .role = PartRole::divider, .composed_from = {}},
     .member = &ListView::GroupParts::group_divider},
}};

constexpr std::array<StateField<ListView::GroupStates>, 1> list_view_group_state_fields{{
    {.state = AnatomyState::disabled, .member = &ListView::GroupStates::disabled},
}};

constexpr std::array<PartDeclaration, 9> list_view_parts = part_declarations(list_view_part_fields);
constexpr std::array<PartDeclaration, 7> list_view_item_parts = part_declarations(list_view_item_part_fields);
constexpr std::array<AnatomyState, 6> list_view_item_states = state_declarations(list_view_item_state_fields);
constexpr std::array<PartDeclaration, 2> list_view_sub_item_parts =
    part_declarations(list_view_sub_item_part_fields);
constexpr std::array<AnatomyState, 2> list_view_sub_item_states =
    state_declarations(list_view_sub_item_state_fields);
constexpr std::array<PartDeclaration, 5> list_view_column_parts = part_declarations(list_view_column_part_fields);
constexpr std::array<AnatomyState, 3> list_view_column_states = state_declarations(list_view_column_state_fields);
constexpr std::array<PartDeclaration, 3> list_view_group_parts = part_declarations(list_view_group_part_fields);
constexpr std::array<AnatomyState, 1> list_view_group_states = state_declarations(list_view_group_state_fields);
constexpr std::array<AnatomyState, 5> list_view_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ItemSystemIndex item_system{0U};
constexpr ItemSystemIndex sub_item_system{1U};
constexpr ItemSystemIndex column_system{2U};
constexpr ItemSystemIndex group_system{3U};

constexpr std::array<ItemSystemDeclaration, 4> list_view_item_systems{{
    {.name = "item", .parts = list_view_item_parts, .states = list_view_item_states, .custom_states = {}},
    {.name = "sub_item", .parts = list_view_sub_item_parts, .states = list_view_sub_item_states,
     .custom_states = {}},
    {.name = "column", .parts = list_view_column_parts, .states = list_view_column_states,
     .custom_states = list_view_column_custom_states},
    {.name = "group", .parts = list_view_group_parts, .states = list_view_group_states, .custom_states = {}},
}};

constexpr ControlAnatomy list_view_anatomy{
    .control_id = "list_view",
    .parts = list_view_parts,
    .states = list_view_states,
    .custom_states = {},
    .item_systems = list_view_item_systems,
};

} // namespace

const ControlAnatomy& ListView::anatomy() const noexcept {
    return list_view_anatomy;
}

void ListView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    Parts record;
    record.region = PartGeometry::from_bounds(placed.well);
    record.well = PartGeometry::from_bounds(placed.well);
    record.header = PartGeometry::from_bounds(placed.header);
    record.vertical_scroll_bar = PartGeometry::from_bounds(placed.vertical_bar);
    record.horizontal_scroll_bar = PartGeometry::from_bounds(placed.horizontal_bar);
    record.scroll_corner = PartGeometry::from_bounds(placed.corner);
    record.focus = PartGeometry::from_bounds(placed.well);
    record.insertion_mark = PartGeometry::from_bounds(insertion_mark_bounds(placed));
    if (record.insertion_mark.present) record.insertion_mark.states.insert(AnatomyState::drop_accepted);
    record.label_editor = PartGeometry::from_bounds(label_editor_bounds(placed));
    write_parts(anatomy(), list_view_part_fields, record, geometry);
}

ListView::Parts ListView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), list_view_part_fields, published.parts(), record);
    return record;
}

std::size_t ListView::realized_row_count(const Layout& placed) const noexcept {
    const std::size_t result = end_realized(placed) - first_realized(placed);
    return result;
}

// Sub-items are realized row by row, across the visible columns after the
// first.
std::size_t ListView::realized_sub_item_count(const Layout& placed) const noexcept {
    if (view_ != ListViewView::details || columns_.size() < 2U) return 0U;
    std::size_t columns = visible_column_count(placed);
    if (column_visible(placed, 0U)) --columns;
    const std::size_t result = realized_row_count(placed) * columns;
    return result;
}

std::size_t ListView::realized_sub_item_column(const Layout& placed, std::size_t position) const noexcept {
    const std::size_t skip = column_visible(placed, 0U) ? 1U : 0U;
    const std::size_t result = visible_column(placed, position + skip);
    return result;
}

std::size_t ListView::realized_item_count() const {
    const Layout placed = layout();
    const std::size_t columns = view_ == ListViewView::details ? visible_column_count(placed) : 0U;
    const std::size_t result =
        realized_row_count(placed) + realized_sub_item_count(placed) + columns + realized_group_count(placed);
    return result;
}

ListView::RealizedSlot ListView::realized_slot(const Layout& placed, std::size_t item) const {
    const std::size_t rows = realized_row_count(placed);
    const std::size_t sub_items = realized_sub_item_count(placed);
    const std::size_t first = first_realized(placed);
    RealizedSlot slot;
    if (item < rows) {
        slot.system = item_system;
        slot.index = index_at(placed, first + item);
        return slot;
    }
    if (item < rows + sub_items) {
        // sub_items is rows times the visible columns after the first, so a
        // nonzero count has rows and a whole number of cells per row.
        const std::size_t per_row = sub_items / rows;
        const std::size_t position = item - rows;
        slot.system = sub_item_system;
        slot.index = index_at(placed, first + position / per_row);
        slot.column = realized_sub_item_column(placed, position % per_row);
        return slot;
    }
    const std::size_t columns = view_ == ListViewView::details ? visible_column_count(placed) : 0U;
    if (item < rows + sub_items + columns) {
        slot.system = column_system;
        slot.column = visible_column(placed, item - rows - sub_items);
        return slot;
    }
    // A group's slot names its band.
    slot.system = group_system;
    slot.index = realized_group_band(placed, item - rows - sub_items - columns);
    return slot;
}

std::string ListView::realized_item_stable_id(std::size_t item) const {
    const Layout placed = layout();
    const RealizedSlot slot = realized_slot(placed, item);
    if (slot.system == column_system) {
        std::string identity = column_identity(stable_id().value(), columns_[slot.column].stable_id);
        return identity;
    }
    if (slot.system == group_system) return group_identity(placed.bands[slot.index].group);
    ListViewItem scratch;
    const ListViewItem& realized = item_at(slot.index, scratch);
    if (slot.system == item_system) return realized.stable_id;
    std::string identity = sub_item_identity(realized.stable_id, columns_[slot.column].stable_id);
    return identity;
}

ItemSystemIndex ListView::publish_realized_item_system(std::size_t item) const {
    const Layout placed = layout();
    const RealizedSlot slot = realized_slot(placed, item);
    return slot.system;
}

void ListView::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    const RealizedSlot slot = realized_slot(placed, item);
    const ItemSystemDeclaration& system = anatomy().item_systems[slot.system.value];
    if (slot.system == item_system) {
        const ItemParts record = item_parts(placed, slot.index, placed.viewport);
        write_item_parts(system, list_view_item_part_fields, record, geometry);
    } else if (slot.system == sub_item_system) {
        const SubItemParts record = sub_item_parts(placed, slot.index, slot.column, placed.viewport);
        write_item_parts(system, list_view_sub_item_part_fields, record, geometry);
    } else if (slot.system == group_system) {
        const GroupParts record = group_parts(placed, slot.index, placed.viewport);
        write_item_parts(system, list_view_group_part_fields, record, geometry);
    } else {
        const ColumnParts record = column_parts(placed, slot.column, placed.header);
        write_item_parts(system, list_view_column_part_fields, record, geometry);
    }
}

AnatomyStateSet ListView::realized_item_anatomy_states(std::size_t item) const {
    const Layout placed = layout();
    const RealizedSlot slot = realized_slot(placed, item);
    if (slot.system == column_system) return column_states(slot.column);
    if (slot.system == group_system) {
        AnatomyStateSet group_states;
        if (!effectively_enabled()) group_states.insert(AnatomyState::disabled);
        return group_states;
    }
    if (slot.system == item_system) {
        ListViewItem scratch;
        const bool checked = item_at(slot.index, scratch).checked;
        return item_states(slot.index, checked);
    }
    // A sub-item shows its row's selection and the view's enabled state.
    AnatomyStateSet states;
    if (selected_[slot.index] != 0U) states.insert(AnatomyState::selected);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    return states;
}

std::vector<ListView::Item> ListView::realized_items() const {
    const Layout placed = layout();
    const std::size_t first = first_realized(placed);
    const std::size_t count = realized_row_count(placed);
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = index_at(placed, first + item);
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems[item_system.value], list_view_item_part_fields, published.parts(),
                        record.parts);
        read_states(list_view_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

std::vector<ListView::SubItem> ListView::realized_sub_items() const {
    const Layout placed = layout();
    const std::size_t rows = realized_row_count(placed);
    const std::size_t count = realized_sub_item_count(placed);
    std::vector<SubItem> records;
    records.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        const std::size_t item = rows + position;
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        const RealizedSlot slot = realized_slot(placed, item);
        SubItem record;
        record.index = slot.index;
        record.column = slot.column;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems[sub_item_system.value], list_view_sub_item_part_fields,
                        published.parts(), record.parts);
        read_states(list_view_sub_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

std::vector<ListView::Column> ListView::realized_columns() const {
    const Layout placed = layout();
    const std::size_t count = view_ == ListViewView::details ? visible_column_count(placed) : 0U;
    std::vector<Column> records;
    records.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        const std::size_t column = visible_column(placed, position);
        Column record;
        record.index = column;
        record.stable_id = column_identity(stable_id().value(), columns_[column].stable_id);
        record.parts = column_parts(placed, column, placed.header);
        const AnatomyStateSet states = column_states(column);
        read_states(list_view_column_state_fields, states, record.states);
        record.states.sorted_ascending = states.contains(sorted_ascending_state);
        record.states.sorted_descending = states.contains(sorted_descending_state);
        records.push_back(record);
    }
    return records;
}

std::vector<ListView::Group> ListView::realized_groups() const {
    const Layout placed = layout();
    const std::size_t count = realized_group_count(placed);
    const std::size_t before = realized_item_count() - count;
    std::vector<Group> records;
    records.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        const std::size_t item = before + position;
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        const std::size_t band = realized_group_band(placed, position);
        Group record;
        record.index = placed.bands[band].group;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems[group_system.value], list_view_group_part_fields, published.parts(),
                        record.parts);
        read_states(list_view_group_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events ListView adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 9> list_view_interactions{{
    {.name = "draw_item_content", .payload = "ItemContentPaint", .mutable_payload = true},
    {.name = "selection_changed", .payload = "ListViewSelectionChange", .mutable_payload = false},
    {.name = "item_activated", .payload = "ListViewItemActivation", .mutable_payload = false},
    {.name = "item_checking", .payload = "ListViewItemCheck", .mutable_payload = true},
    {.name = "item_checked", .payload = "ListViewItemCheck", .mutable_payload = false},
    {.name = "column_click", .payload = "ListViewColumnClick", .mutable_payload = false},
    {.name = "column_width_changed", .payload = "ListViewColumnWidthChange", .mutable_payload = false},
    {.name = "before_label_edit", .payload = "ListViewLabelEdit", .mutable_payload = true},
    {.name = "after_label_edit", .payload = "ListViewLabelEdit", .mutable_payload = true},
}};

} // namespace

void ListView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), list_view_interactions.begin(), list_view_interactions.end());
}

} // namespace gui_forms
