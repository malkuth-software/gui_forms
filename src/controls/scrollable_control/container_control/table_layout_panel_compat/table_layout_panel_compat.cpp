#include "gui_forms/controls/scrollable_control/container_control/table_layout_panel_compat/table_layout_panel_compat.hpp"
#include "../container_layout_utilities.hpp"
#include "../../scrollable_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

namespace {
constexpr std::size_t maximum_layout_tracks = 64U;

// The occupancy bitmap's row stride and capacity (the track limit).
constexpr std::size_t occupancy_stride = maximum_layout_tracks;
constexpr std::size_t occupancy_capacity = maximum_layout_tracks * maximum_layout_tracks;

// Whether the cells [column, column + column_span) by [row, row + row_span)
// lie inside the live grid and are all free.
bool region_free(std::span<const std::uint8_t> occupied, std::size_t columns, std::size_t rows,
                 std::size_t column, std::size_t row, std::size_t column_span, std::size_t row_span) noexcept {
    if (column + column_span > columns || row + row_span > rows) return false;
    for (std::size_t y = row; y < row + row_span; ++y) {
        for (std::size_t x = column; x < column + column_span; ++x) {
            if (occupied[y * occupancy_stride + x] != 0U) return false;
        }
    }
    return true;
}

void occupy(std::span<std::uint8_t> occupied, std::size_t column, std::size_t row, std::size_t column_span,
            std::size_t row_span) noexcept {
    for (std::size_t y = row; y < row + row_span; ++y) {
        for (std::size_t x = column; x < column + column_span; ++x) {
            occupied[y * occupancy_stride + x] = 1U;
        }
    }
}

// Grows the live grid to hold a positioned item, as grow_style allows. The
// bitmap's cells outside the live grid are free, so growing is a count.
bool grow_to_fit(std::size_t& columns, std::size_t& rows, TableLayoutCellPosition position,
                 std::size_t column_span, std::size_t row_span, TableLayoutGrowStyle grow_style) noexcept {
    const std::size_t required_columns = position.column + column_span;
    const std::size_t required_rows = position.row + row_span;
    if (required_columns > maximum_layout_tracks || required_rows > maximum_layout_tracks) return false;
    if (required_columns > columns) {
        if (grow_style != TableLayoutGrowStyle::add_columns) return false;
        columns = required_columns;
    }
    if (required_rows > rows) {
        if (grow_style != TableLayoutGrowStyle::add_rows) return false;
        rows = required_rows;
    }
    return true;
}

// The grid's rules, each a `rule` part (its role and family set by the
// caller) one unit across, centred on the line the grid draws.
void draw_table_grid(Painter& painter, const Control& control,
                     std::span<const double> column_widths,
                     std::span<const double> row_heights,
                     Insets inset, Size track_spacing,
                     double width, double height,
                     PartPaint rule, double offset) {
    double x = inset.left;
    rule.bounds = {x + offset - 0.5, inset.top, 1.0, height};
    paint_part(painter, control, rule);
    for (std::size_t index = 0U; index < column_widths.size(); ++index) {
        x += column_widths[index];
        const bool has_gap = index + 1U < column_widths.size();
        const double line_x = x + (has_gap ? track_spacing.width * 0.5 : 0.0);
        rule.bounds = {line_x + offset - 0.5, inset.top, 1.0, height};
        paint_part(painter, control, rule);
        if (has_gap) x += track_spacing.width;
    }
    double y = inset.top;
    rule.bounds = {inset.left, y + offset - 0.5, width, 1.0};
    paint_part(painter, control, rule);
    for (std::size_t index = 0U; index < row_heights.size(); ++index) {
        y += row_heights[index];
        const bool has_gap = index + 1U < row_heights.size();
        const double line_y = y + (has_gap ? track_spacing.height * 0.5 : 0.0);
        rule.bounds = {inset.left, line_y + offset - 0.5, width, 1.0};
        paint_part(painter, control, rule);
        if (has_gap) y += track_spacing.height;
    }
}

} // namespace

using namespace container_layout_detail;

double TableLayoutPanelCompat::resolve_tracks(
    std::span<const TableLayoutStyle> styles,
    std::span<double> minimum,
    std::span<const TrackDemand> spans,
    double available,
    std::span<double> actual) noexcept {
    for (std::size_t index = 0U; index < styles.size(); ++index) {
        if (styles[index].size_mode == TableSizeMode::absolute) {
            minimum[index] = styles[index].size;
        }
    }
    for (const TrackDemand& demand : spans) {
        const std::size_t end = std::min(styles.size(), demand.start + demand.span);
        double current = 0.0;
        double total_weight = 0.0;
        for (std::size_t index = demand.start; index < end; ++index) {
            current += minimum[index];
            if (styles[index].size_mode != TableSizeMode::absolute) {
                total_weight += styles[index].size_mode == TableSizeMode::percent
                    ? styles[index].size : 1.0;
            }
        }
        const double deficit = std::max(0.0, demand.required - current);
        if (deficit <= 0.0 || total_weight <= 0.0) continue;
        for (std::size_t index = demand.start; index < end; ++index) {
            if (styles[index].size_mode == TableSizeMode::absolute) continue;
            const double weight = styles[index].size_mode == TableSizeMode::percent
                ? styles[index].size : 1.0;
            minimum[index] += deficit * weight / total_weight;
        }
    }

    double fixed_extent = 0.0;
    double percent_weight = 0.0;
    double desired_percent_extent = 0.0;
    for (std::size_t index = 0U; index < styles.size(); ++index) {
        if (styles[index].size_mode == TableSizeMode::percent) {
            percent_weight += styles[index].size;
        } else {
            fixed_extent += minimum[index];
        }
    }
    if (percent_weight > 0.0) {
        for (std::size_t index = 0U; index < styles.size(); ++index) {
            if (styles[index].size_mode != TableSizeMode::percent) continue;
            desired_percent_extent = std::max(
                desired_percent_extent,
                minimum[index] * percent_weight / styles[index].size);
        }
    }

    const double remaining = std::max(0.0, available - fixed_extent);
    for (std::size_t index = 0U; index < styles.size(); ++index) {
        actual[index] = styles[index].size_mode == TableSizeMode::percent
            ? (percent_weight > 0.0
                   ? remaining * styles[index].size / percent_weight
                   : 0.0)
            : minimum[index];
    }
    const double desired = fixed_extent + desired_percent_extent;
    return desired;
}


TableLayoutPanelCompat::TableLayoutPanelCompat(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {}

void TableLayoutPanelCompat::validate_style(TableLayoutStyle style) {
    if (!std::isfinite(style.size) || style.size < 0.0 ||
        (style.size_mode == TableSizeMode::percent && style.size <= 0.0)) {
        throw std::invalid_argument(
            "TableLayoutPanelCompat style size must be finite and nonnegative; percent must be positive");
    }
}

void TableLayoutPanelCompat::set_column_count(std::size_t count) {
    require_mutable();
    if (count == 0U || count > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat column count must be 1 through 64");
    }
    if (column_count_ == count) return;
    column_count_ = count;
    column_styles_.resize(count);
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_row_count(std::size_t count) {
    require_mutable();
    if (count == 0U || count > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat row count must be 1 through 64");
    }
    if (row_count_ == count) return;
    row_count_ = count;
    row_styles_.resize(count);
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_grow_style(TableLayoutGrowStyle style) {
    require_mutable();
    if (grow_style_ == style) return;
    grow_style_ = style;
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_cell_border_style(TableCellBorderStyle style) {
    require_mutable();
    if (cell_border_style_ == style) return;
    cell_border_style_ = style;
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void TableLayoutPanelCompat::set_track_spacing(Size spacing) {
    require_mutable();
    if (!std::isfinite(spacing.width) || !std::isfinite(spacing.height) ||
        spacing.width < 0.0 || spacing.height < 0.0 ||
        spacing.width > 256.0 || spacing.height > 256.0) {
        throw std::invalid_argument(
            "TableLayoutPanelCompat track spacing must be finite and between zero and 256");
    }
    if (track_spacing_ == spacing) return;
    track_spacing_ = spacing;
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_column_style(std::size_t column,
                                        TableLayoutStyle style) {
    require_mutable();
    if (column >= column_count_) {
        throw std::out_of_range("TableLayoutPanelCompat column style index");
    }
    validate_style(style);
    if (column_styles_[column] == style) return;
    column_styles_[column] = style;
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_row_style(std::size_t row, TableLayoutStyle style) {
    require_mutable();
    if (row >= row_count_) throw std::out_of_range("TableLayoutPanelCompat row style index");
    validate_style(style);
    if (row_styles_[row] == style) return;
    row_styles_[row] = style;
    invalidate(invalidation::bounds);
}

TableLayoutPanelCompat::CellMetadata& TableLayoutPanelCompat::metadata_for(
    const Control& child) {
    if (child.parent().get() != this || !child.is_alive()) {
        throw std::invalid_argument(
            "TableLayoutPanelCompat metadata requires a live direct child");
    }
    return metadata_[child.runtime_id().value];
}

const TableLayoutPanelCompat::CellMetadata* TableLayoutPanelCompat::metadata_for(
    const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return nullptr;
    const MetadataMap::const_iterator found =
        metadata_.find(child.runtime_id().value);
    return found == metadata_.end() ? nullptr : &(*found).second;
}

void TableLayoutPanelCompat::set_cell_position(
    const Control& child, TableLayoutCellPosition position) {
    require_mutable();
    if (position.column >= maximum_layout_tracks ||
        position.row >= maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat cell position exceeds 64 tracks");
    }
    CellMetadata& metadata = metadata_for(child);
    if (metadata.position == position) return;
    metadata.position = position;
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::clear_cell_position(const Control& child) {
    require_mutable();
    CellMetadata& metadata = metadata_for(child);
    if (!metadata.position) return;
    metadata.position.reset();
    invalidate(invalidation::bounds);
}

std::optional<TableLayoutCellPosition> TableLayoutPanelCompat::cell_position(
    const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return std::nullopt;
    if (attached_window() != nullptr) static_cast<void>(arranged_bounds());
    const ResolvedCellMap::const_iterator resolved =
        resolved_cells_.find(child.runtime_id().value);
    if (resolved != resolved_cells_.end()) return (*resolved).second.position;
    const CellMetadata* metadata = metadata_for(child);
    return metadata == nullptr ? std::nullopt : (*metadata).position;
}

void TableLayoutPanelCompat::set_column_span(const Control& child, std::size_t span) {
    require_mutable();
    if (span == 0U || span > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat column span must be 1 through 64");
    }
    CellMetadata& metadata = metadata_for(child);
    if (metadata.column_span == span) return;
    metadata.column_span = span;
    invalidate(invalidation::bounds);
}

std::size_t TableLayoutPanelCompat::column_span(const Control& child) const {
    const CellMetadata* metadata = metadata_for(child);
    return metadata == nullptr ? 1U : (*metadata).column_span;
}

void TableLayoutPanelCompat::set_row_span(const Control& child, std::size_t span) {
    require_mutable();
    if (span == 0U || span > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat row span must be 1 through 64");
    }
    CellMetadata& metadata = metadata_for(child);
    if (metadata.row_span == span) return;
    metadata.row_span = span;
    invalidate(invalidation::bounds);
}

std::size_t TableLayoutPanelCompat::row_span(const Control& child) const {
    const CellMetadata* metadata = metadata_for(child);
    return metadata == nullptr ? 1U : (*metadata).row_span;
}

void TableLayoutPanelCompat::reconcile_metadata(std::uint64_t pass) {
    for (const Control::Ptr& child : children()) {
        if (!child || !(*child).is_alive() || (*child).parent().get() != this) continue;
        const MetadataMap::iterator found = metadata_.find((*child).runtime_id().value);
        if (found != metadata_.end()) (*found).second.seen_pass = pass;
    }
    MetadataMap::iterator metadata = metadata_.begin();
    while (metadata != metadata_.end()) {
        if ((*metadata).second.seen_pass != pass) {
            metadata = metadata_.erase(metadata);
        } else {
            ++metadata;
        }
    }
}

void TableLayoutPanelCompat::place_items(Workspace& workspace) {
    std::vector<std::uint8_t>& occupied = workspace.occupied;
    occupied.assign(occupancy_capacity, 0U);
    std::size_t columns = column_count_;
    std::size_t rows = row_count_;
    for (const Control::Ptr& child : workspace.retained) {
        if (!is_current_layout_child(child) || !(*child).visible.read()) continue;
        const CellMetadata* metadata = std::as_const(*this).metadata_for(*child);
        if (metadata == nullptr || !(*metadata).position) {
            workspace.automatic.push_back(child);
            continue;
        }
        const TableLayoutCellPosition position = *(*metadata).position;
        const std::size_t column_span = (*metadata).column_span;
        const std::size_t row_span = (*metadata).row_span;
        if (!grow_to_fit(columns, rows, position, column_span, row_span, grow_style_) ||
            !region_free(occupied, columns, rows, position.column, position.row, column_span, row_span)) {
            workspace.overflow.push_back(child);
            continue;
        }
        occupy(occupied, position.column, position.row, column_span, row_span);
        workspace.items.push_back(TableItem{child, position, column_span, row_span, Size{}, (*child).margin.read()});
    }

    for (const Control::Ptr& child : workspace.automatic) {
        const CellMetadata* metadata = std::as_const(*this).metadata_for(*child);
        const std::size_t column_span = metadata == nullptr ? 1U : (*metadata).column_span;
        const std::size_t row_span = metadata == nullptr ? 1U : (*metadata).row_span;
        if ((column_span > columns && grow_style_ != TableLayoutGrowStyle::add_columns) ||
            (row_span > rows && grow_style_ != TableLayoutGrowStyle::add_rows)) {
            workspace.overflow.push_back(child);
            continue;
        }
        columns = std::max(columns, column_span);
        rows = std::max(rows, row_span);
        std::optional<TableLayoutCellPosition> position;
        while (!position) {
            for (std::size_t row = 0U; row < rows && !position; ++row) {
                for (std::size_t column = 0U; column < columns; ++column) {
                    if (region_free(occupied, columns, rows, column, row, column_span, row_span)) {
                        position = TableLayoutCellPosition{column, row};
                        break;
                    }
                }
            }
            if (position) break;
            if (grow_style_ == TableLayoutGrowStyle::add_rows && rows < maximum_layout_tracks) {
                ++rows;
            } else if (grow_style_ == TableLayoutGrowStyle::add_columns && columns < maximum_layout_tracks) {
                ++columns;
            } else {
                break;
            }
        }
        if (!position) {
            workspace.overflow.push_back(child);
            continue;
        }
        occupy(occupied, (*position).column, (*position).row, column_span, row_span);
        workspace.items.push_back(TableItem{child, *position, column_span, row_span, Size{}, (*child).margin.read()});
    }
    workspace.columns = columns;
    workspace.rows = rows;
}

Size TableLayoutPanelCompat::layout_children(Size available, bool assign) {
    if (workspace_busy_) {
        // A child's measure laid this panel out again inside a pass: the
        // nested pass takes its own storage, so the outer pass's survives.
        Workspace nested{};
        const Size nested_size = layout_pass(available, assign, nested);
        return nested_size;
    }
    workspace_busy_ = true;
    struct Release final {
        bool& busy;
        ~Release() { busy = false; }
    } release{workspace_busy_};
    const Size size = layout_pass(available, assign, workspace_);
    return size;
}

Size TableLayoutPanelCompat::layout_pass(Size available, bool assign, Workspace& workspace) {
    const std::uint64_t pass = ++pass_;
    reconcile_metadata(pass);
    const Insets inset = padding.read();
    const Size inner{std::max(0.0, available.width - horizontal_extent(inset)),
                     std::max(0.0, available.height - vertical_extent(inset))};
    layout_overflowed_ = false;

    // The children, held for the pass: a measure callback may change the
    // tree, so each is revalidated after it is measured.
    const std::span<const Control::Ptr> all = children();
    workspace.retained.assign(all.begin(), all.end());
    workspace.automatic.clear();
    workspace.overflow.clear();
    workspace.items.clear();
    workspace.items.reserve(workspace.retained.size());
    place_items(workspace);
    workspace.retained.clear();
    workspace.automatic.clear();
    const std::size_t columns = workspace.columns;
    const std::size_t rows = workspace.rows;

    std::vector<TableLayoutStyle>& column_styles = workspace.column_styles;
    std::vector<TableLayoutStyle>& row_styles = workspace.row_styles;
    column_styles.assign(column_styles_.begin(), column_styles_.end());
    row_styles.assign(row_styles_.begin(), row_styles_.end());
    column_styles.resize(columns);
    row_styles.resize(rows);
    const double horizontal_spacing = track_spacing_.width *
        static_cast<double>(columns > 0U ? columns - 1U : 0U);
    const double vertical_spacing = track_spacing_.height *
        static_cast<double>(rows > 0U ? rows - 1U : 0U);
    std::vector<double>& column_minimum = workspace.column_minimum;
    std::vector<double>& row_minimum = workspace.row_minimum;
    column_minimum.assign(columns, 0.0);
    row_minimum.assign(rows, 0.0);
    workspace.column_spans.clear();
    workspace.row_spans.clear();
    for (TableItem& item : workspace.items) {
        if (!is_current_layout_child(item.control) || !(*item.control).visible.read()) {
            item.control.reset();
            continue;
        }
        item.desired = preferred_child_size(item.control, inner);
        if (!is_alive()) return {};
        if (!is_current_layout_child(item.control) || !(*item.control).visible.read()) {
            item.control.reset();
            continue;
        }
        const double required_width = item.desired.width + horizontal_extent(item.margin);
        const double required_height = item.desired.height + vertical_extent(item.margin);
        if (item.column_span == 1U && column_styles[item.position.column].size_mode != TableSizeMode::absolute) {
            column_minimum[item.position.column] = std::max(column_minimum[item.position.column], required_width);
        } else if (item.column_span > 1U) {
            const double spacing = track_spacing_.width * static_cast<double>(item.column_span - 1U);
            workspace.column_spans.push_back(
                TrackDemand{item.position.column, item.column_span, std::max(0.0, required_width - spacing)});
        }
        if (item.row_span == 1U && row_styles[item.position.row].size_mode != TableSizeMode::absolute) {
            row_minimum[item.position.row] = std::max(row_minimum[item.position.row], required_height);
        } else if (item.row_span > 1U) {
            const double spacing = track_spacing_.height * static_cast<double>(item.row_span - 1U);
            workspace.row_spans.push_back(
                TrackDemand{item.position.row, item.row_span, std::max(0.0, required_height - spacing)});
        }
    }

    std::vector<double>& column_actual = workspace.column_actual;
    std::vector<double>& row_actual = workspace.row_actual;
    column_actual.assign(columns, 0.0);
    row_actual.assign(rows, 0.0);
    const double desired_width = resolve_tracks(column_styles, column_minimum, workspace.column_spans,
                                                std::max(0.0, inner.width - horizontal_spacing), column_actual);
    const double desired_height = resolve_tracks(row_styles, row_minimum, workspace.row_spans,
                                                 std::max(0.0, inner.height - vertical_spacing), row_actual);
    std::vector<double>& column_offsets = workspace.column_offsets;
    std::vector<double>& row_offsets = workspace.row_offsets;
    column_offsets.assign(columns + 1U, 0.0);
    row_offsets.assign(rows + 1U, 0.0);
    for (std::size_t index = 0U; index < columns; ++index) {
        column_offsets[index + 1U] = column_offsets[index] + column_actual[index] +
                                     (index + 1U < columns ? track_spacing_.width : 0.0);
    }
    for (std::size_t index = 0U; index < rows; ++index) {
        row_offsets[index + 1U] = row_offsets[index] + row_actual[index] +
                                  (index + 1U < rows ? track_spacing_.height : 0.0);
    }
    // Right to left mirrors each placed rectangle about the panel, once, at
    // output: x becomes the width less the logical right edge.
    const bool right_to_left = effective_layout_direction() == LayoutDirection::right_to_left;
    if (assign) {
        column_widths_.assign(column_actual.begin(), column_actual.end());
        row_heights_.assign(row_actual.begin(), row_actual.end());
        painted_column_widths_.assign(column_actual.begin(), column_actual.end());
        if (right_to_left) std::reverse(painted_column_widths_.begin(), painted_column_widths_.end());
        const double grid_width = column_offsets[columns];
        grid_left_ = right_to_left ? available.width - inset.left - grid_width : inset.left;
    }

    for (const TableItem& item : workspace.items) {
        if (!item.control || !is_current_layout_child(item.control)) continue;
        ResolvedCell& resolved = resolved_cells_[(*item.control).runtime_id().value];
        resolved.position = item.position;
        resolved.seen_pass = pass;
        if (!assign) continue;
        const std::size_t column_end = item.position.column + item.column_span;
        const std::size_t row_end = item.position.row + item.row_span;
        const double cell_width = column_offsets[column_end] - column_offsets[item.position.column] -
                                  (column_end < columns ? track_spacing_.width : 0.0);
        const double cell_height = row_offsets[row_end] - row_offsets[item.position.row] -
                                   (row_end < rows ? track_spacing_.height : 0.0);
        const double available_width = std::max(0.0, cell_width - horizontal_extent(item.margin));
        const double available_height = std::max(0.0, cell_height - vertical_extent(item.margin));
        const double cell_x = inset.left + column_offsets[item.position.column] + item.margin.left;
        const double cell_y = inset.top + row_offsets[item.position.row] + item.margin.top;
        const Rect area{cell_x, cell_y, available_width, available_height};
        Rect placed = place_in_cell((*item.control).dock.read(), (*item.control).anchor.read(), area, item.desired);
        if (right_to_left) placed.x = available.width - placed.x - placed.width;
        set_child_layout(item.control, placed);
    }
    // The cells this pass did not resolve go.
    ResolvedCellMap::iterator stale = resolved_cells_.begin();
    while (stale != resolved_cells_.end()) {
        if ((*stale).second.seen_pass != pass) {
            stale = resolved_cells_.erase(stale);
        } else {
            ++stale;
        }
    }
    layout_overflowed_ = !workspace.overflow.empty();
    if (assign) {
        const double corner = right_to_left ? available.width - inset.left : inset.left;
        for (const Control::Ptr& child : workspace.overflow) {
            if (is_current_layout_child(child)) set_child_layout(child, {corner, inset.top, 0.0, 0.0});
        }
    }
    // Releases the pass's references to the children; the capacity stays.
    workspace.items.clear();
    workspace.overflow.clear();
    return {desired_width + horizontal_spacing + horizontal_extent(inset),
            desired_height + vertical_spacing + vertical_extent(inset)};
}

Control::Ptr TableLayoutPanelCompat::control_from_position(std::size_t column,
                                                      std::size_t row) const {
    if (attached_window() != nullptr) static_cast<void>(arranged_bounds());
    for (const Control::Ptr& child : children()) {
        if (!child || !(*child).is_alive() || !(*child).visible.read()) continue;
        const ResolvedCellMap::const_iterator position =
            resolved_cells_.find((*child).runtime_id().value);
        if (position == resolved_cells_.end()) continue;
        const CellMetadata* metadata = metadata_for(*child);
        const std::size_t column_span = metadata == nullptr
            ? 1U : (*metadata).column_span;
        const std::size_t row_span = metadata == nullptr ? 1U : (*metadata).row_span;
        const TableLayoutCellPosition cell = (*position).second.position;
        if (column >= cell.column && column < cell.column + column_span &&
            row >= cell.row && row < cell.row + row_span) {
            return child;
        }
    }
    return {};
}

Size TableLayoutPanelCompat::measure(Size available) {
    available = {std::max(0.0, available.width),
                 std::max(0.0, available.height)};
    if (auto_size.read()) {
        const Size desired = layout_children(available, false);
        return {std::min(available.width, desired.width),
                std::min(available.height, desired.height)};
    }
    const Rect requested = requested_bounds();
    return {std::min(available.width,
                     requested.width > 0.0 ? requested.width : available.width),
            std::min(available.height,
                     requested.height > 0.0 ? requested.height : available.height)};
}

void TableLayoutPanelCompat::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    static_cast<void>(layout_children({final_bounds.width, final_bounds.height}, true));
}

void TableLayoutPanelCompat::on_paint(Painter& painter, Rect) {
    if (cell_border_style_ == TableCellBorderStyle::none ||
        column_widths_.empty() || row_heights_.empty()) {
        return;
    }
    const Insets inset = padding.read();
    const double width = std::accumulate(column_widths_.begin(),
                                         column_widths_.end(), 0.0) +
        track_spacing_.width * static_cast<double>(column_widths_.size() - 1U);
    const double height = std::accumulate(row_heights_.begin(),
                                          row_heights_.end(), 0.0) +
        track_spacing_.height * static_cast<double>(row_heights_.size() - 1U);
    // The grid starts at its left edge (the right padding less its width,
    // right to left), its columns in painted order.
    Insets origin = inset;
    origin.left = grid_left_;
    const std::span<const double> columns = painted_column_widths_;
    // Single borders are keylines; inset and outset borders a keyline and a
    // seam a unit apart, the keyline above and left for inset and below and
    // right for outset, in the theme's rule colours.
    PartPaint keyline;
    keyline.family = ControlVisualRole::panel;
    keyline.role = PartRole::keyline;
    PartPaint seam = keyline;
    seam.role = PartRole::seam;
    if (cell_border_style_ == TableCellBorderStyle::single) {
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, keyline, 0.0);
    } else if (cell_border_style_ == TableCellBorderStyle::inset) {
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, keyline, 0.0);
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, seam, 1.0);
    } else {
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, seam, 0.0);
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, keyline, 1.0);
    }
}

SemanticDescriptor TableLayoutPanelCompat::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty() || !descriptor.description.empty();
    return descriptor;
}


namespace {

constexpr std::array<PartField<TableLayoutPanelCompat::Parts>, 1> table_layout_panel_compat_extra_fields{{
    {.declaration = {.name = "cell_grid", .role = PartRole::divider, .composed_from = {}},
     .member = &TableLayoutPanelCompat::Parts::cell_grid},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 1>
    table_layout_panel_compat_parts = joined_declarations(
        scrollable_detail::parts, part_declarations(table_layout_panel_compat_extra_fields));

constexpr ControlAnatomy table_layout_panel_compat_anatomy{
    .control_id = "table_layout_panel_compat",
    .parts = table_layout_panel_compat_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& TableLayoutPanelCompat::anatomy() const noexcept {
    return table_layout_panel_compat_anatomy;
}

void TableLayoutPanelCompat::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();
    if (cell_border_style_ != TableCellBorderStyle::none &&
        !column_widths_.empty() && !row_heights_.empty()) {
        // The extent on_paint rules: every track and the spacing between
        // them, from the padding origin.
        const Insets inset = padding.read();
        const double width = std::accumulate(column_widths_.begin(),
                                             column_widths_.end(), 0.0) +
            track_spacing_.width * static_cast<double>(column_widths_.size() - 1U);
        const double height = std::accumulate(row_heights_.begin(),
                                              row_heights_.end(), 0.0) +
            track_spacing_.height * static_cast<double>(row_heights_.size() - 1U);
        const Rect grid{grid_left_, inset.top, width, height};
        record.cell_grid = PartGeometry::from_bounds(grid);
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), table_layout_panel_compat_extra_fields, record, geometry);
}

TableLayoutPanelCompat::Parts TableLayoutPanelCompat::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), table_layout_panel_compat_extra_fields, published.parts(), record);
    return record;
}

} // namespace gui_forms
