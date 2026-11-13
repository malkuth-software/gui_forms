#include "gui_forms/scene/spatial_index/spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace gui_forms {

namespace {

// The cell is first sized so that the longer side of the box spans at most
// the square root of the cap; each doubling then roughly quarters the cell
// count, so a handful always suffices. The bound keeps the loop finite for
// every input (a non-fitting grid after it is a defect, refused below).
constexpr std::size_t maximum_cell_doublings = 64U;

[[nodiscard]] bool valid_options(const SpatialIndexOptions& options) noexcept {
    const bool cell = std::isfinite(options.cell_size) && options.cell_size > 0.0;
    const bool counts = options.maximum_cells >= 4U && options.maximum_cells <= maximum_spatial_cells &&
                        options.maximum_cells_per_entry >= 1U &&
                        options.maximum_displaced <= maximum_spatial_entries;
    return cell && counts;
}

[[nodiscard]] bool matches(const WorldRect& area, const WorldRect& bounds, SpatialQueryMode mode) noexcept {
    if (mode == SpatialQueryMode::contained) return world_rect_contains(area, bounds);
    const bool touching = world_rects_intersect(area, bounds);
    return touching;
}

} // namespace

// -- cells ----------------------------------------------------------------------------

std::uint32_t SpatialIndex::column_of(double x) const noexcept {
    double cell = std::floor((x - left_) / cell_);
    // NaN compares false: it lands in the first column with the negatives.
    if (!(cell > 0.0)) cell = 0.0;
    const double last = static_cast<double>(columns_ - 1U);
    if (cell > last) cell = last;
    const std::uint32_t column = static_cast<std::uint32_t>(cell);
    return column;
}

std::uint32_t SpatialIndex::row_of(double y) const noexcept {
    double cell = std::floor((y - top_) / cell_);
    if (!(cell > 0.0)) cell = 0.0;
    const double last = static_cast<double>(rows_ - 1U);
    if (cell > last) cell = last;
    const std::uint32_t row = static_cast<std::uint32_t>(cell);
    return row;
}

SpatialIndex::CellRange SpatialIndex::range_of(const WorldRect& rect) const noexcept {
    CellRange range{};
    range.first_column = column_of(rect.x);
    range.last_column = column_of(rect.right());
    range.first_row = row_of(rect.y);
    range.last_row = row_of(rect.bottom());
    return range;
}

std::size_t SpatialIndex::cell_count_of(const CellRange& range) noexcept {
    const std::size_t columns = static_cast<std::size_t>(range.last_column - range.first_column) + 1U;
    const std::size_t rows = static_cast<std::size_t>(range.last_row - range.first_row) + 1U;
    // A sub-range of the grid, whose cell count (at most
    // maximum_spatial_cells) fits.
    const std::size_t cells = columns * rows;
    return cells;
}

bool SpatialIndex::same_cells(const EntryPlacement& placement, const CellRange& range) const noexcept {
    const bool same = placement.first_column == range.first_column && placement.last_column == range.last_column &&
                      placement.first_row == range.first_row && placement.last_row == range.last_row;
    return same;
}

// -- build ----------------------------------------------------------------------------

void SpatialIndex::build(std::span<const SpatialEntry> entries, const SpatialIndexOptions& options) {
    if (!valid_options(options)) throw std::invalid_argument("SpatialIndex options out of range");
    if (entries.size() > maximum_spatial_entries) throw std::length_error("SpatialIndex holds at most 2^24 entries");
    // The box over every present entry, validating each.
    double left = std::numeric_limits<double>::infinity();
    double top = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = -std::numeric_limits<double>::infinity();
    for (const SpatialEntry& entry : entries) {
        if (!entry.present) continue;
        if (!valid_world_rect(entry.bounds)) throw std::invalid_argument("SpatialIndex entry bounds not finite");
        left = std::min(left, entry.bounds.x);
        top = std::min(top, entry.bounds.y);
        right = std::max(right, entry.bounds.right());
        bottom = std::max(bottom, entry.bounds.bottom());
    }
    if (!(left <= right) || !(top <= bottom)) {
        // Nothing present: a one-cell grid at the origin.
        left = 0.0;
        top = 0.0;
        right = 0.0;
        bottom = 0.0;
    }
    // The cell: at least the option's, large enough that the longer side
    // spans about the square root of the cap, then doubled until it fits.
    const double side_cells = std::floor(std::sqrt(static_cast<double>(options.maximum_cells)));
    const double longest = std::max(right - left, bottom - top);
    double cell = std::max(options.cell_size, longest / side_cells);
    double columns = 1.0;
    double rows = 1.0;
    bool fits = false;
    for (std::size_t attempt = 0U; attempt < maximum_cell_doublings && !fits; ++attempt) {
        columns = std::floor((right - left) / cell) + 1.0;
        rows = std::floor((bottom - top) / cell) + 1.0;
        fits = columns * rows <= static_cast<double>(options.maximum_cells);
        if (!fits) cell *= 2.0;
    }
    if (!fits || !std::isfinite(cell)) throw std::length_error("SpatialIndex grid does not fit its cap");

    // A dry run of the placement against the new grid sizes the cell lists
    // before anything changes, so a refusal leaves the index as it was.
    const double saved_left = left_;
    const double saved_top = top_;
    const double saved_cell = cell_;
    const std::uint32_t saved_columns = columns_;
    const std::uint32_t saved_rows = rows_;
    left_ = left;
    top_ = top;
    cell_ = cell;
    columns_ = static_cast<std::uint32_t>(columns);
    rows_ = static_cast<std::uint32_t>(rows);
    std::uint64_t listed = 0U;
    for (const SpatialEntry& entry : entries) {
        if (!entry.present) continue;
        const std::size_t covered = cell_count_of(range_of(entry.bounds));
        if (covered <= options.maximum_cells_per_entry) listed += covered;
    }
    if (listed > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        left_ = saved_left;
        top_ = saved_top;
        cell_ = saved_cell;
        columns_ = saved_columns;
        rows_ = saved_rows;
        throw std::length_error("SpatialIndex cell lists exceed 32-bit offsets");
    }

    const std::size_t cells = static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_);
    try {
        entries_.resize(entries.size());
        placements_.resize(entries.size());
        marks_.assign(entries.size(), 0U);
        cell_start_.assign(cells + 1U, 0U);
        cell_entries_.resize(static_cast<std::size_t>(listed));
        cell_fill_.assign(cells, 0U);
        oversize_.clear();
        displaced_.clear();
        displaced_.reserve(options.maximum_displaced);
    } catch (...) {
        clear();
        throw;
    }
    maximum_displaced_ = options.maximum_displaced;
    stamp_ = 0U;

    // Count, prefix, place (the compressed-row fill).
    for (std::size_t id = 0U; id < entries.size(); ++id) {
        const SpatialEntry& entry = entries[id];
        EntryPlacement& placement = placements_[id];
        entries_[id] = entry.bounds;
        placement = EntryPlacement{};
        if (!entry.present) continue;
        const CellRange range = range_of(entry.bounds);
        placement.first_column = range.first_column;
        placement.last_column = range.last_column;
        placement.first_row = range.first_row;
        placement.last_row = range.last_row;
        if (cell_count_of(range) > options.maximum_cells_per_entry) {
            placement.placement = Placement::oversize;
            continue;
        }
        placement.placement = Placement::cells;
        for (std::uint32_t row = range.first_row; row <= range.last_row; ++row) {
            const std::size_t row_start = static_cast<std::size_t>(row) * columns_;
            for (std::uint32_t column = range.first_column; column <= range.last_column; ++column) {
                ++cell_start_[row_start + column + 1U];
            }
        }
    }
    for (std::size_t at = 0U; at < cells; ++at) cell_start_[at + 1U] += cell_start_[at];
    std::size_t oversize_count = 0U;
    for (const EntryPlacement& placement : placements_) {
        if (placement.placement == Placement::oversize) ++oversize_count;
    }
    try {
        oversize_.reserve(oversize_count);
    } catch (...) {
        clear();
        throw;
    }
    for (std::size_t id = 0U; id < placements_.size(); ++id) {
        const EntryPlacement& placement = placements_[id];
        const std::uint32_t identity = static_cast<std::uint32_t>(id);
        if (placement.placement == Placement::oversize) {
            oversize_.push_back(identity);
            continue;
        }
        if (placement.placement != Placement::cells) continue;
        for (std::uint32_t row = placement.first_row; row <= placement.last_row; ++row) {
            const std::size_t row_start = static_cast<std::size_t>(row) * columns_;
            for (std::uint32_t column = placement.first_column; column <= placement.last_column; ++column) {
                const std::size_t cell_index = row_start + column;
                const std::size_t slot = static_cast<std::size_t>(cell_start_[cell_index]) + cell_fill_[cell_index];
                cell_entries_[slot] = identity;
                ++cell_fill_[cell_index];
            }
        }
    }

    const std::uint64_t builds = statistics_.builds + 1U;
    statistics_ = SpatialIndexStatistics{};
    statistics_.builds = builds;
    statistics_.columns = columns_;
    statistics_.rows = rows_;
    statistics_.cell_size = cell_;
    statistics_.cell_entries = cell_entries_.size();
    statistics_.oversize = oversize_.size();
}

void SpatialIndex::clear() noexcept {
    entries_.clear();
    placements_.clear();
    marks_.clear();
    cell_start_.clear();
    cell_entries_.clear();
    cell_fill_.clear();
    oversize_.clear();
    displaced_.clear();
    maximum_displaced_ = 0U;
    stamp_ = 0U;
    left_ = 0.0;
    top_ = 0.0;
    cell_ = 1.0;
    columns_ = 0U;
    rows_ = 0U;
    statistics_.columns = 0U;
    statistics_.rows = 0U;
    statistics_.cell_entries = 0U;
    statistics_.oversize = 0U;
    statistics_.displaced = 0U;
}

// -- in-place changes -------------------------------------------------------------------

void SpatialIndex::drop_displaced(std::uint32_t id) noexcept {
    // Swap-remove: the last displaced entry takes the leaving one's place.
    const std::uint32_t at = placements_[id].displaced_at;
    const std::uint32_t last = displaced_.back();
    displaced_[at] = last;
    placements_[last].displaced_at = at;
    displaced_.pop_back();
    statistics_.displaced = displaced_.size();
}

bool SpatialIndex::move_entry(std::uint32_t id, const WorldRect& bounds) noexcept {
    if (id >= entries_.size() || !valid_world_rect(bounds)) return false;
    EntryPlacement& placement = placements_[id];
    switch (placement.placement) {
    case Placement::absent:
        return false;
    case Placement::oversize:
        // Every query visits the oversize list, so any bounds are right.
        entries_[id] = bounds;
        ++statistics_.moved_in_place;
        return true;
    case Placement::cells: {
        const CellRange range = range_of(bounds);
        if (same_cells(placement, range)) {
            entries_[id] = bounds;
            ++statistics_.moved_in_place;
            return true;
        }
        if (displaced_.size() >= maximum_displaced_) {
            ++statistics_.moves_refused;
            return false;
        }
        // Reserved at build: this push never allocates.
        placement.displaced_at = static_cast<std::uint32_t>(displaced_.size());
        displaced_.push_back(id);
        placement.placement = Placement::displaced;
        entries_[id] = bounds;
        statistics_.displaced = displaced_.size();
        ++statistics_.moved_out_of_cells;
        return true;
    }
    case Placement::displaced: {
        const CellRange range = range_of(bounds);
        entries_[id] = bounds;
        if (same_cells(placement, range)) {
            // Back in the cells that still list it.
            drop_displaced(id);
            placement.placement = Placement::cells;
            ++statistics_.moved_in_place;
            return true;
        }
        ++statistics_.moved_out_of_cells;
        return true;
    }
    default:
        break;
    }
    // An unknown placement: refuse rather than guess.
    return false;
}

bool SpatialIndex::remove_entry(std::uint32_t id) noexcept {
    if (id >= entries_.size()) return false;
    EntryPlacement& placement = placements_[id];
    if (placement.placement == Placement::absent) return false;
    if (placement.placement == Placement::displaced) drop_displaced(id);
    // Cell and oversize lists keep the identity; queries skip it by its
    // placement.
    placement.placement = Placement::absent;
    return true;
}

bool SpatialIndex::present(std::uint32_t id) const noexcept {
    if (id >= placements_.size()) return false;
    const bool listed = placements_[id].placement != Placement::absent;
    return listed;
}

WorldRect SpatialIndex::bounds(std::uint32_t id) const noexcept {
    if (!present(id)) return WorldRect{};
    return entries_[id];
}

// -- query ----------------------------------------------------------------------------

void SpatialIndex::advance_stamp() noexcept {
    if (stamp_ == std::numeric_limits<std::uint32_t>::max()) {
        // Wrapping: clear every mark so no old stamp can equal a new one.
        std::fill(marks_.begin(), marks_.end(), 0U);
        stamp_ = 0U;
    }
    ++stamp_;
}

void SpatialIndex::emit(std::uint32_t id, std::span<std::uint32_t> out, SpatialQueryResult& result) noexcept {
    if (result.written < out.size()) {
        out[result.written] = id;
        ++result.written;
    }
    ++result.matched;
}

SpatialQueryResult SpatialIndex::query(const WorldRect& area, SpatialQueryMode mode,
                                       std::span<std::uint32_t> out) noexcept {
    SpatialQueryResult result{};
    ++statistics_.queries;
    if (columns_ == 0U || rows_ == 0U || !valid_world_rect(area)) return result;
    advance_stamp();
    // The cells the area covers, clamped: an entry beyond the built box was
    // listed in the edge cells, so clamping the query meets it there.
    const CellRange range = range_of(area);
    for (std::uint32_t row = range.first_row; row <= range.last_row; ++row) {
        const std::size_t row_start = static_cast<std::size_t>(row) * columns_;
        for (std::uint32_t column = range.first_column; column <= range.last_column; ++column) {
            const std::size_t cell_index = row_start + column;
            const std::uint32_t first = cell_start_[cell_index];
            const std::uint32_t end = cell_start_[cell_index + 1U];
            ++statistics_.cells_visited;
            for (std::uint32_t at = first; at < end; ++at) {
                const std::uint32_t id = cell_entries_[at];
                ++statistics_.entries_visited;
                // A displaced or removed entry is still listed here; only
                // its own list (or nothing) answers for it, so it is skipped
                // before it is marked.
                if (placements_[id].placement != Placement::cells) continue;
                if (marks_[id] == stamp_) continue;
                marks_[id] = stamp_;
                if (matches(area, entries_[id], mode)) emit(id, out, result);
            }
        }
    }
    for (const std::uint32_t id : oversize_) {
        ++statistics_.entries_visited;
        if (placements_[id].placement != Placement::oversize || marks_[id] == stamp_) continue;
        marks_[id] = stamp_;
        if (matches(area, entries_[id], mode)) emit(id, out, result);
    }
    // Displaced entries were skipped in the cells and are never oversize.
    for (const std::uint32_t id : displaced_) {
        ++statistics_.entries_visited;
        if (marks_[id] == stamp_) continue;
        marks_[id] = stamp_;
        if (matches(area, entries_[id], mode)) emit(id, out, result);
    }
    return result;
}

} // namespace gui_forms
