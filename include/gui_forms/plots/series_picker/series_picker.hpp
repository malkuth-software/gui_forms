#pragma once

// SeriesPicker: the element nearest a device point (IMGUI_IMPLOT_STUDY B.3,
// P9). One picker serves hover readout, crosshair snapping, legend
// highlight, selection and the accessible point cursor.
//
// - Monotone x: binary search for the device x, then a window of elements
//   within ±radius device units along x, each measured.
// - Unordered x: a uniform grid over the plot in device space, built on the
//   first query after the data or the transform changed (the caller says
//   which revision it holds) and reused by later queries; its storage is
//   reserved once (reserve) and never grows while picking.
// - Bars and heatmap cells pick by containment (pick_rect).
//
// Distances are Euclidean in logical device units.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/plots/plot_transform/plot_transform.hpp"
#include "gui_forms/plots/plot_types/plot_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace gui_forms::plots {

struct GUI_FORMS_API PickResult final {
    bool found{};
    SeriesIdentity series{};
    ElementIndex element{};
    double x{};
    double y{};
    Point device{};
    double distance{std::numeric_limits<double>::infinity()};
};

class GUI_FORMS_API SeriesPicker final {
public:
    // Reserves an unordered series' grid for up to `elements` elements over
    // a grid of `cells` cells (allocates once).
    void reserve(std::size_t elements, std::size_t cells);

    // The element of monotone `source` nearest `device` within `radius`;
    // `result` is replaced when one is nearer than result.distance (so one
    // result can be carried across several series). Returns whether it
    // was replaced.
    template <typename Source>
    bool nearest_monotone(const Source& source, SeriesIdentity series, const PlotTransform& transform, Point device,
                          double radius, PickResult& result) const noexcept;

    // The same for an unordered source; builds the grid when `revision`
    // differs from the one it was built at.
    template <typename Source>
    bool nearest_unordered(const Source& source, SeriesIdentity series, std::uint64_t revision,
                           const PlotTransform& transform, Point device, double radius, PickResult& result) noexcept;

    // How many grids were built: queries between data changes reuse one.
    [[nodiscard]] std::uint64_t grid_builds() const noexcept { return grid_builds_; }

private:
    template <typename Source>
    void build_grid(const Source& source, const PlotTransform& transform) noexcept;

    // The grid: cell_start_[c] .. cell_start_[c + 1] index into elements_,
    // a counting sort of the elements by cell.
    std::vector<std::uint32_t> cell_start_{};
    std::vector<std::uint32_t> elements_{};
    std::size_t grid_columns_{};
    std::size_t grid_rows_{};
    double cell_size_{};
    Rect grid_plot_{};
    std::uint64_t built_revision_{~std::uint64_t{0}};
    std::uint64_t grid_builds_{};
    bool grid_valid_{};
};

// Whether `device` lies in `rects` (bars, heatmap cells); the first
// containing rectangle's index, or none.
struct GUI_FORMS_API RectPick final {
    bool found{};
    std::size_t index{};
};
[[nodiscard]] GUI_FORMS_API RectPick pick_rect(std::span<const Rect> rects, Point device) noexcept;

// -- definitions ---------------------------------------------------------------

template <typename Source>
bool SeriesPicker::nearest_monotone(const Source& source, SeriesIdentity series, const PlotTransform& transform,
                                    Point device, double radius, PickResult& result) const noexcept {
    if (source.size() == 0U || !(radius >= 0.0)) return false;
    const double low_x = transform.x_from_device(device.x - radius);
    const double high_x = transform.x_from_device(device.x + radius);
    const double minimum = low_x < high_x ? low_x : high_x;
    const double maximum = low_x < high_x ? high_x : low_x;
    const ElementRange range = visible_range(source, minimum, maximum);
    bool replaced = false;
    for (std::size_t index = range.first; index < range.end; ++index) {
        const double x = source.x(index);
        const double y = source.y(index);
        const Point candidate = transform.to_device(x, y);
        if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y)) continue;
        const double distance = std::hypot(candidate.x - device.x, candidate.y - device.y);
        if (distance > radius || !(distance < result.distance)) continue;
        result = PickResult{true, series, ElementIndex{index}, x, y, candidate, distance};
        replaced = true;
    }
    return replaced;
}

template <typename Source>
void SeriesPicker::build_grid(const Source& source, const PlotTransform& transform) noexcept {
    grid_valid_ = false;
    const std::size_t cells = cell_start_.empty() ? 0U : cell_start_.size() - 1U;
    if (cells == 0U || source.size() > elements_.size()) return;
    const Rect plot = transform.plot();
    // Square cells covering the plot, as many as the reservation allows.
    const double area = plot.width * plot.height;
    double size = std::sqrt(area / static_cast<double>(cells));
    if (!(size > 0.0)) return;
    std::size_t columns = static_cast<std::size_t>(std::ceil(plot.width / size));
    std::size_t rows = static_cast<std::size_t>(std::ceil(plot.height / size));
    while (columns * rows > cells) {
        size = size * 1.0625;
        columns = static_cast<std::size_t>(std::ceil(plot.width / size));
        rows = static_cast<std::size_t>(std::ceil(plot.height / size));
    }
    grid_columns_ = columns;
    grid_rows_ = rows;
    cell_size_ = size;
    grid_plot_ = plot;
    const std::size_t used = columns * rows;
    std::fill(cell_start_.begin(), cell_start_.begin() + static_cast<std::ptrdiff_t>(used + 1U), 0U);
    // Counting sort: count per cell (into start + 1), prefix-sum, place.
    for (std::size_t index = 0U; index < source.size(); ++index) {
        const Point point = transform.to_device(source.x(index), source.y(index));
        if (!plot.contains(point)) continue;
        const std::size_t column = static_cast<std::size_t>((point.x - plot.x) / size);
        const std::size_t row = static_cast<std::size_t>((point.y - plot.y) / size);
        const std::size_t cell = (row < rows ? row : rows - 1U) * columns + (column < columns ? column : columns - 1U);
        cell_start_[cell + 1U] = cell_start_[cell + 1U] + 1U;
    }
    for (std::size_t cell = 0U; cell < used; ++cell) cell_start_[cell + 1U] = cell_start_[cell + 1U] + cell_start_[cell];
    // Place each element at its cell's next free slot, using the cell's
    // start as a cursor and restoring the starts afterwards.
    for (std::size_t index = 0U; index < source.size(); ++index) {
        const Point point = transform.to_device(source.x(index), source.y(index));
        if (!plot.contains(point)) continue;
        const std::size_t column = static_cast<std::size_t>((point.x - plot.x) / size);
        const std::size_t row = static_cast<std::size_t>((point.y - plot.y) / size);
        const std::size_t cell = (row < rows ? row : rows - 1U) * columns + (column < columns ? column : columns - 1U);
        elements_[cell_start_[cell]] = static_cast<std::uint32_t>(index);
        cell_start_[cell] = cell_start_[cell] + 1U;
    }
    for (std::size_t cell = used; cell > 0U; --cell) cell_start_[cell] = cell_start_[cell - 1U];
    cell_start_[0] = 0U;
    grid_valid_ = true;
    grid_builds_ = grid_builds_ + 1U;
}

template <typename Source>
bool SeriesPicker::nearest_unordered(const Source& source, SeriesIdentity series, std::uint64_t revision,
                                     const PlotTransform& transform, Point device, double radius,
                                     PickResult& result) noexcept {
    if (source.size() == 0U || !(radius >= 0.0)) return false;
    if (revision != built_revision_ || !(grid_plot_ == transform.plot()) || !grid_valid_) {
        build_grid(source, transform);
        built_revision_ = revision;
    }
    if (!grid_valid_) return false;
    const double left = (device.x - radius - grid_plot_.x) / cell_size_;
    const double right = (device.x + radius - grid_plot_.x) / cell_size_;
    const double top = (device.y - radius - grid_plot_.y) / cell_size_;
    const double bottom = (device.y + radius - grid_plot_.y) / cell_size_;
    const double last_column = static_cast<double>(grid_columns_) - 1.0;
    const double last_row = static_cast<double>(grid_rows_) - 1.0;
    if (right < 0.0 || bottom < 0.0 || left > last_column || top > last_row) return false;
    const std::size_t first_column = static_cast<std::size_t>(std::fmax(0.0, std::floor(left)));
    const std::size_t end_column = static_cast<std::size_t>(std::fmin(last_column, std::floor(right))) + 1U;
    const std::size_t first_row = static_cast<std::size_t>(std::fmax(0.0, std::floor(top)));
    const std::size_t end_row = static_cast<std::size_t>(std::fmin(last_row, std::floor(bottom))) + 1U;
    bool replaced = false;
    for (std::size_t row = first_row; row < end_row; ++row) {
        for (std::size_t column = first_column; column < end_column; ++column) {
            const std::size_t cell = row * grid_columns_ + column;
            for (std::uint32_t slot = cell_start_[cell]; slot < cell_start_[cell + 1U]; ++slot) {
                const std::size_t index = elements_[slot];
                const double x = source.x(index);
                const double y = source.y(index);
                const Point candidate = transform.to_device(x, y);
                const double distance = std::hypot(candidate.x - device.x, candidate.y - device.y);
                if (distance > radius) continue;
                // Equal distances keep the earliest element, as the
                // monotone search does.
                const bool nearer = distance < result.distance ||
                                    (distance == result.distance && result.series == series &&
                                     index < result.element.value);
                if (!nearer) continue;
                result = PickResult{true, series, ElementIndex{index}, x, y, candidate, distance};
                replaced = true;
            }
        }
    }
    return replaced;
}

} // namespace gui_forms::plots
