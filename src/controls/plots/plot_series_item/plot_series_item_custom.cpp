#include "gui_forms/controls/plot_series_item_custom.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

// The finite values' extent; [0, 1] when there are none, and a unit range
// about a single value.
[[nodiscard]] plots::PlotRange finite_extent(std::span<const double> values) noexcept {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const double value : values) {
        if (!std::isfinite(value)) continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (!(minimum <= maximum)) return plots::PlotRange{0.0, 1.0};
    if (minimum == maximum) return plots::PlotRange{minimum - 0.5, maximum + 0.5};
    const plots::PlotRange result{minimum, maximum};
    return result;
}

// The index of the equal part of [range] that `value` falls in, of
// `count` parts (the last closed), or count when it falls outside.
[[nodiscard]] std::size_t part_of(double value, plots::PlotRange range, std::size_t count) noexcept {
    if (!std::isfinite(value) || value < range.minimum || value > range.maximum || count == 0U) return count;
    const double share = (value - range.minimum) / range.extent();
    const double scaled = std::floor(share * static_cast<double>(count));
    const std::size_t index = scaled >= static_cast<double>(count) ? count - 1U : static_cast<std::size_t>(scaled);
    return index;
}

} // namespace

std::size_t PlotCustomSeries::mark_capacity() const noexcept {
    return 0U;
}

plots::PickResult PlotCustomSeries::pick(Point, const plots::PlotTransform&) const {
    const plots::PickResult nothing{};
    return nothing;
}

// -- heatmap -------------------------------------------------------------------------

void PlotHeatmapSeries::set_grid(std::span<const double> values, std::size_t rows, std::size_t columns,
                                 plots::PlotRange area_x, plots::PlotRange area_y) {
    const bool overflows = columns != 0U && rows > std::numeric_limits<std::size_t>::max() / columns;
    if (overflows || rows * columns != values.size()) {
        throw std::invalid_argument("a heatmap grid holds rows x columns values");
    }
    if (!area_x.valid() || !area_y.valid()) throw std::invalid_argument("a heatmap area must be valid");
    std::vector<double> copy(values.begin(), values.end());
    values_ = std::move(copy);
    rows_ = rows;
    columns_ = columns;
    area_x_ = area_x;
    area_y_ = area_y;
    value_extent_ = finite_extent(values_);
}

void PlotHeatmapSeries::set_domain(std::optional<plots::PlotRange> domain) {
    if (domain.has_value() && !(*domain).valid()) throw std::invalid_argument("a heatmap domain must be valid");
    domain_ = domain;
}

plots::PlotRange PlotHeatmapSeries::domain() const noexcept {
    const plots::PlotRange result = domain_.has_value() ? *domain_ : value_extent_;
    return result;
}

PlotCustomExtent PlotHeatmapSeries::extent() const {
    PlotCustomExtent result{};
    if (values_.empty()) return result;
    result.x = area_x_;
    result.y = area_y_;
    return result;
}

std::size_t PlotHeatmapSeries::mark_capacity() const noexcept {
    // paint_heatmap writes one rectangle and one colour per cell.
    return values_.size();
}

plots::MarkStatistics PlotHeatmapSeries::paint(Painter& painter, const plots::PlotTransform& transform,
                                               plots::SeriesMarkPainter& marks, Color) {
    if (values_.empty()) return plots::MarkStatistics{};
    plots::Scale scale{};
    scale.domain = domain();
    const plots::Colormap& map = plots::built_in_colormap(colormap_);
    const plots::MarkStatistics result =
        marks.paint_heatmap(painter, values_, rows_, columns_, area_x_, area_y_, map, scale, transform);
    return result;
}

plots::PickResult PlotHeatmapSeries::pick(Point device, const plots::PlotTransform& transform) const {
    plots::PickResult result{};
    if (values_.empty()) return result;
    const double x = transform.x_from_device(device.x);
    const double y = transform.y_from_device(device.y);
    const std::size_t column = part_of(x, area_x_, columns_);
    // Row 0 is at the top of the area: its maximum y.
    const std::size_t row_from_bottom = part_of(y, area_y_, rows_);
    if (column == columns_ || row_from_bottom == rows_) return result;
    const std::size_t row = rows_ - 1U - row_from_bottom;
    const std::size_t element = row * columns_ + column;
    result.found = true;
    result.element = plots::ElementIndex{element};
    result.x = x;
    result.y = values_[element];
    result.device = device;
    result.distance = 0.0;
    return result;
}

// -- histogram -----------------------------------------------------------------------

void PlotHistogramSeries::set_samples(std::span<const double> samples, std::size_t bins) {
    if (bins == 0U || bins > maximum_bins) throw std::invalid_argument("a histogram takes 1 through 65536 bins");
    std::vector<double> copy(samples.begin(), samples.end());
    std::vector<std::uint64_t> counts(bins, 0U);
    samples_ = std::move(copy);
    counts_ = std::move(counts);
    rebin();
}

void PlotHistogramSeries::set_range(std::optional<plots::PlotRange> range) {
    if (range.has_value() && !(*range).valid()) throw std::invalid_argument("a histogram range must be valid");
    range_ = range;
    rebin();
}

void PlotHistogramSeries::rebin() {
    const plots::PlotRange range = range_.has_value() ? *range_ : finite_extent(samples_);
    spec_ = plots::BinSpec{range.minimum, range.maximum, counts_.size()};
    plots::SeriesSpans<double> values{};
    values.first = std::span<const double>(samples_.data(), samples_.size());
    tally_ = plots::bin_values_into(values, spec_, counts_);
}

PlotCustomExtent PlotHistogramSeries::extent() const {
    PlotCustomExtent result{};
    if (counts_.empty()) return result;
    std::uint64_t largest = 0U;
    for (const std::uint64_t count : counts_) largest = std::max(largest, count);
    result.x = plots::PlotRange{spec_.minimum, spec_.maximum};
    result.y = plots::PlotRange{0.0, largest == 0U ? 1.0 : static_cast<double>(largest)};
    return result;
}

plots::MarkStatistics PlotHistogramSeries::paint(Painter& painter, const plots::PlotTransform& transform,
                                                 plots::SeriesMarkPainter& marks, Color color) {
    if (counts_.empty()) return plots::MarkStatistics{};
    const plots::MarkStatistics result = marks.paint_histogram(painter, counts_, spec_, transform, color);
    return result;
}

plots::PickResult PlotHistogramSeries::pick(Point device, const plots::PlotTransform& transform) const {
    plots::PickResult result{};
    if (counts_.empty()) return result;
    const double x = transform.x_from_device(device.x);
    const double y = transform.y_from_device(device.y);
    const std::size_t bin = part_of(x, plots::PlotRange{spec_.minimum, spec_.maximum}, counts_.size());
    if (bin == counts_.size()) return result;
    const double count = static_cast<double>(counts_[bin]);
    if (!(y >= 0.0 && y <= count)) return result;
    result.found = true;
    result.element = plots::ElementIndex{bin};
    result.x = x;
    result.y = count;
    result.device = device;
    result.distance = 0.0;
    return result;
}

} // namespace gui_forms
