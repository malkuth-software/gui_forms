// DataGridView's unbound rows on the `rows` cell (in-place collections
// pilot, design accepted 2026-11-07): the edit hooks and the flush's damage
// settle. The selection, current cell and per-row state are kept by row ID,
// so an edit needs only the identity index and the rows it removed.
#include "gui_forms/controls/data_grid_view/data_grid_view.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gui_forms {

// -- the edit hooks ------------------------------------------------------------

// A refused edit throws here, before the rows change. An edit that must end
// a cell edit first cancels it here (its handlers run before anything
// changes); handlers that edited the rows meanwhile refuse this edit.
void DataGridView::prepare_edit(const decltype(rows)&, const CollectionEdit& edit,
                                std::span<DataGridViewRow> incoming) {
    require_mutable();
    require_not_sorting();
    if (row_source_ != DataGridViewRowSource::unbound) {
        throw std::logic_error("DataGridView rows are edited only as unbound rows");
    }
    for (const DataGridViewRow& row : incoming) validate_row_text(row);
    edit_takes_new_row_id_ = false;
    edit_former_current_.reset();
    switch (edit.kind()) {
    case CollectionEditKind::assign:
        prepare_rows_assignment(incoming);
        break;
    case CollectionEditKind::insert:
        prepare_rows_insertion(edit, incoming);
        break;
    case CollectionEditKind::erase:
        prepare_rows_erasure(edit);
        break;
    case CollectionEditKind::replace:
        prepare_rows_replacement(edit, incoming);
        break;
    default:
        throw std::logic_error("DataGridView received an unknown row edit");
    }
}

void DataGridView::cancel_edit_before_row_edit() {
    if (!editing_) return;
    const std::uint64_t revision = rows_revision(*this);
    cancel_edit();
    if (rows_revision(*this) != revision || row_source_ != DataGridViewRowSource::unbound) {
        throw std::logic_error("DataGridView rows changed while a row edit cancelled the cell edit");
    }
}

// As set_rows did: given IDs unique among the new rows, generated IDs
// skipping them, every row padded to the columns; the new index waits in
// edit_index_ for written.
void DataGridView::prepare_rows_assignment(std::span<DataGridViewRow> incoming) {
    RowPositions index{};
    index.reserve(incoming.size());
    for (std::size_t position = 0; position < incoming.size(); ++position) {
        const DataGridViewRow& row = incoming[position];
        if (row.stable_id.value.empty()) continue;
        if (!index.emplace(row.stable_id.value, position).second) {
            throw std::invalid_argument("DataGridView row stable IDs must be unique");
        }
    }
    for (std::size_t position = 0; position < incoming.size(); ++position) {
        DataGridViewRow& row = incoming[position];
        row.cells.resize(columns_.size());
        if (!row.stable_id.value.empty()) continue;
        row.stable_id.value = generated_row_id(index);
        index.emplace(row.stable_id.value, position);
    }
    cancel_edit_before_row_edit();
    edit_index_ = std::move(index);
}

// Given IDs unique against the rows and each other; then each row is padded,
// given a generated ID if it has none, and indexed (positions after the
// insertion are made right by written). A failed allocation unindexes the
// rows indexed so far.
void DataGridView::prepare_rows_insertion(const CollectionEdit& edit, std::span<DataGridViewRow> incoming) {
    std::unordered_set<std::string_view> given{};
    given.reserve(incoming.size());
    for (const DataGridViewRow& row : incoming) {
        const std::string& id = row.stable_id.value;
        if (id.empty()) continue;
        if (row_positions_.contains(id) || !given.insert(id).second) {
            throw std::invalid_argument("DataGridView row stable IDs must be unique");
        }
    }
    std::size_t indexed = 0U;
    try {
        for (DataGridViewRow& row : incoming) {
            row.cells.resize(columns_.size());
            if (row.stable_id.value.empty()) row.stable_id.value = generated_row_id();
            row_positions_.emplace(row.stable_id.value, edit.index() + indexed);
            ++indexed;
        }
    } catch (...) {
        for (std::size_t added = 0U; added < indexed; ++added) row_positions_.erase(incoming[added].stable_id.value);
        throw;
    }
    for (const DataGridViewRow& row : incoming) {
        if (row.stable_id.value == new_row_.stable_id.value) edit_takes_new_row_id_ = true;
    }
}

// The current row's position is kept for drop_missing_rows; a cell edit in
// an erased row is cancelled; the erased rows leave the index.
void DataGridView::prepare_rows_erasure(const CollectionEdit& edit) {
    const std::optional<std::size_t> former = current_position();
    if (former && *former >= edit.index() && *former - edit.index() < edit.count()) cancel_edit_before_row_edit();
    edit_former_current_ = former;
    const std::span<const DataGridViewRow> present = rows.read();
    for (std::size_t offset = 0U; offset < edit.count(); ++offset) {
        row_positions_.erase(present[edit.index() + offset].stable_id.value);
    }
}

// A replacement keeps the row's ID unless it names another, unused one; a
// cell edit in the row is cancelled (its editor shows the old value).
void DataGridView::prepare_rows_replacement(const CollectionEdit& edit, std::span<DataGridViewRow> incoming) {
    DataGridViewRow& row = incoming[0];
    const std::string present_id = rows.read()[edit.index()].stable_id.value;
    if (row.stable_id.value.empty()) row.stable_id.value = present_id;
    const bool rekeyed = row.stable_id.value != present_id;
    if (rekeyed && row_positions_.contains(row.stable_id.value)) {
        throw std::invalid_argument("DataGridView row stable IDs must be unique");
    }
    row.cells.resize(columns_.size());
    const std::optional<std::size_t> former = current_position();
    if (former == edit.index()) cancel_edit_before_row_edit();
    edit_former_current_ = former;
    if (!rekeyed) return;
    row_positions_.emplace(row.stable_id.value, edit.index());
    row_positions_.erase(present_id);
    edit_takes_new_row_id_ = row.stable_id.value == new_row_.stable_id.value;
}

// The rows are stored. The index takes the edit, state of removed rows is
// dropped, the new row renews an ID a row took, and the grid is touched for
// layout: the rows repaint at the flush (settle_rows_damage).
void DataGridView::written(const decltype(rows)&, const CollectionEdit& edit) {
    switch (edit.kind()) {
    case CollectionEditKind::assign:
        reset_row_state();
        row_positions_.swap(edit_index_);
        edit_index_.clear();
        if (row_positions_.contains(new_row_.stable_id.value)) renew_new_row_id(true);
        break;
    case CollectionEditKind::insert:
        index_rows_from(edit.index());
        if (edit_takes_new_row_id_) renew_new_row_id(true);
        break;
    case CollectionEditKind::erase:
        index_rows_from(edit.index());
        drop_missing_rows(edit_former_current_);
        break;
    case CollectionEditKind::replace:
    default:
        if (edit_takes_new_row_id_) renew_new_row_id(true);
        drop_missing_rows(edit_former_current_);
        break;
    }
    ++structure_revision_;
    auto_size_stale_ = true;
    row_auto_size_stale_ = true;
    Dirty effects = Dirty::arrange | Dirty::hit_test | Dirty::semantics;
    if (edit.structural()) {
        row_heights_stale_ = true;
        effects |= Dirty::measure;
    }
    invalidate(effects);
}

// -- the flush -----------------------------------------------------------------

// The design's flush intersection over the rows shown, [first_row_,
// visible_row_end): a structural edit at or above the last shown row
// repaints from that row's top to the cells' bottom, row headers included;
// one below changes only the scroll extent. A replaced row is repainted
// when shown and above the watermark.
void DataGridView::settle_rows_damage(bool whole, const GridLayout& layout) {
    settled_rows_.clear();
    const std::optional<std::size_t> structural_from = take_dirty(*this, settled_rows_);
    if (!structural_from && settled_rows_.empty()) return;
    if (whole) {
        invalidate(Dirty::paint);
        return;
    }
    const double left = layout.client.x;
    const double right = layout.data.x + layout.data.width;
    const double bottom = layout.data.y + layout.data.height;
    if (right <= left || bottom <= layout.data.y) return;
    const std::size_t window_end = visible_row_end(layout);
    std::size_t replaced_end = window_end;
    if (structural_from && *structural_from < window_end) {
        const std::size_t first = std::max(*structural_from, first_row_);
        const double top = layout.data.y + row_offset(first, layout);
        if (top < bottom) invalidate(Rect{left, top, right - left, bottom - top});
        replaced_end = first;
    }
    const DataGridViewRowHeights& heights = row_heights();
    for (const std::uint32_t row : settled_rows_) {
        const std::size_t position = static_cast<std::size_t>(row);
        if (position < first_row_) continue;
        if (position >= replaced_end || position >= heights.count()) break;
        const double top = layout.data.y + row_offset(position, layout);
        const double height = (heights.top(position + 1U) - heights.top(position)) * layout.scale;
        const double clipped = std::min(height, bottom - top);
        if (clipped > 0.0) invalidate(Rect{left, top, right - left, clipped});
    }
}

} // namespace gui_forms
