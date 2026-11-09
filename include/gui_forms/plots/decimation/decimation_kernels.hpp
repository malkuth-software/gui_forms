#pragma once

// The decimation templates' definitions (decimation.hpp declares and
// documents them). Included at the end of decimation.hpp only.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace gui_forms::plots {

namespace detail {

// Appends one point to a caller's buffer; false (and nothing written) when
// it is full.
struct PointWriter final {
    std::span<Point> out{};
    std::size_t written{};
    bool truncated{};

    bool put(Point point) noexcept {
        if (written == out.size()) {
            truncated = true;
            return false;
        }
        out[written] = point;
        written = written + 1U;
        return true;
    }
};

inline bool finite_point(Point point) noexcept {
    const bool result = std::isfinite(point.x) && std::isfinite(point.y);
    return result;
}

inline Point gap_point() noexcept {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const Point point{nan, nan};
    return point;
}

// One device column's elements while the envelope collects them: element
// indices of its first, minimum, maximum and last (data y extremes, the
// earliest on ties; the y mapping is monotone, so these are the device
// extremes too).
struct EnvelopeColumn final {
    std::size_t first{};
    std::size_t minimum{};
    std::size_t maximum{};
    std::size_t last{};
    double minimum_y{};
    double maximum_y{};
    DeviceColumn column{};
};

// Emits a column's distinct elements in element order.
template <typename Source>
void emit_column(const Source& source, const EnvelopeColumn& column, const PlotTransform& transform,
                 PointWriter& writer) noexcept {
    const std::size_t low = column.minimum < column.maximum ? column.minimum : column.maximum;
    const std::size_t high = column.minimum < column.maximum ? column.maximum : column.minimum;
    const std::array<std::size_t, 4> ordered{column.first, low, high, column.last};
    std::size_t previous = static_cast<std::size_t>(-1);
    for (const std::size_t index : ordered) {
        if (index == previous) continue;
        previous = index;
        static_cast<void>(writer.put(transform.to_device(source.x(index), source.y(index))));
    }
}

} // namespace detail

template <typename Source>
DecimationStatistics decimate_envelope(const Source& source, ElementRange range, const PlotTransform& transform,
                                       std::span<Point> out) noexcept {
    DecimationStatistics statistics{};
    detail::PointWriter writer{};
    writer.out = out;
    const std::size_t end = range.end < source.size() ? range.end : source.size();
    statistics.points_in = end > range.first ? end - range.first : 0U;
    detail::EnvelopeColumn column{};
    bool open = false;
    // Whether the last thing written was a gap, or nothing has been
    // written: a gap then writes nothing.
    bool after_gap = true;
    for (std::size_t index = range.first; index < end; ++index) {
        const double x = source.x(index);
        const double y = source.y(index);
        const Point device = transform.to_device(x, y);
        if (!detail::finite_point(device)) {
            if (open) detail::emit_column(source, column, transform, writer);
            open = false;
            if (!after_gap) static_cast<void>(writer.put(detail::gap_point()));
            after_gap = true;
            continue;
        }
        const DeviceColumn current = transform.column_of(device.x);
        if (open && current == column.column) {
            column.last = index;
            if (y < column.minimum_y) {
                column.minimum_y = y;
                column.minimum = index;
            }
            if (y > column.maximum_y) {
                column.maximum_y = y;
                column.maximum = index;
            }
            continue;
        }
        if (open) detail::emit_column(source, column, transform, writer);
        if (after_gap) statistics.runs = statistics.runs + 1U;
        open = true;
        after_gap = false;
        column = detail::EnvelopeColumn{index, index, index, index, y, y, current};
    }
    if (open) detail::emit_column(source, column, transform, writer);
    statistics.points_out = writer.written;
    statistics.truncated = writer.truncated;
    return statistics;
}

template <typename Source>
DecimationStatistics map_all(const Source& source, ElementRange range, const PlotTransform& transform,
                             std::span<Point> out) noexcept {
    DecimationStatistics statistics{};
    detail::PointWriter writer{};
    writer.out = out;
    const std::size_t end = range.end < source.size() ? range.end : source.size();
    statistics.points_in = end > range.first ? end - range.first : 0U;
    bool after_gap = true;
    for (std::size_t index = range.first; index < end; ++index) {
        const Point device = transform.to_device(source.x(index), source.y(index));
        if (!detail::finite_point(device)) {
            if (!after_gap) static_cast<void>(writer.put(detail::gap_point()));
            after_gap = true;
            continue;
        }
        if (after_gap) statistics.runs = statistics.runs + 1U;
        after_gap = false;
        if (!writer.put(device)) break;
    }
    statistics.points_out = writer.written;
    statistics.truncated = writer.truncated;
    return statistics;
}

// -- EnvelopePyramid ---------------------------------------------------------------

inline void EnvelopePyramid::reserve(std::size_t capacity) {
    std::size_t total = 0U;
    std::size_t buckets = capacity / pyramid_base;
    std::size_t level = 0U;
    offsets_[0] = 0U;
    while (level < maximum_levels) {
        total = total + buckets;
        offsets_[level + 1U] = total;
        level = level + 1U;
        buckets = buckets / 2U;
    }
    minima_.assign(total, 0U);
    maxima_.assign(total, 0U);
    capacity_ = capacity;
    size_ = 0U;
    level_count_ = 0U;
}

inline std::size_t EnvelopePyramid::bucket_count(std::size_t level) const noexcept {
    if (level >= level_count_) return 0U;
    std::size_t buckets = size_ / pyramid_base;
    for (std::size_t step = 0U; step < level; ++step) buckets = buckets / 2U;
    return buckets;
}

inline std::uint32_t EnvelopePyramid::minimum_index(std::size_t level, std::size_t bucket) const noexcept {
    return minima_[level_offset(level) + bucket];
}

inline std::uint32_t EnvelopePyramid::maximum_index(std::size_t level, std::size_t bucket) const noexcept {
    return maxima_[level_offset(level) + bucket];
}

template <typename Source>
void EnvelopePyramid::compute_level_zero(const Source& source, std::size_t first_bucket) noexcept {
    const std::size_t buckets = size_ / pyramid_base;
    for (std::size_t bucket = first_bucket; bucket < buckets; ++bucket) {
        const std::size_t base = bucket * pyramid_base;
        std::size_t minimum = base;
        std::size_t maximum = base;
        double minimum_y = source.y(base);
        double maximum_y = minimum_y;
        for (std::size_t offset = 1U; offset < pyramid_base; ++offset) {
            const double y = source.y(base + offset);
            if (y < minimum_y) {
                minimum_y = y;
                minimum = base + offset;
            }
            if (y > maximum_y) {
                maximum_y = y;
                maximum = base + offset;
            }
        }
        minima_[bucket] = static_cast<std::uint32_t>(minimum);
        maxima_[bucket] = static_cast<std::uint32_t>(maximum);
    }
}

template <typename Source>
void EnvelopePyramid::compute_level(const Source& source, std::size_t level, std::size_t first_bucket) noexcept {
    const std::size_t below = level_offset(level - 1U);
    const std::size_t here = level_offset(level);
    const std::size_t buckets = bucket_count(level);
    for (std::size_t bucket = first_bucket; bucket < buckets; ++bucket) {
        const std::uint32_t left_minimum = minima_[below + 2U * bucket];
        const std::uint32_t right_minimum = minima_[below + 2U * bucket + 1U];
        const std::uint32_t left_maximum = maxima_[below + 2U * bucket];
        const std::uint32_t right_maximum = maxima_[below + 2U * bucket + 1U];
        // The right bucket's elements come later: it wins only when
        // strictly more extreme, so ties keep the earliest element.
        minima_[here + bucket] = source.y(right_minimum) < source.y(left_minimum) ? right_minimum : left_minimum;
        maxima_[here + bucket] = source.y(right_maximum) > source.y(left_maximum) ? right_maximum : left_maximum;
    }
}

template <typename Source>
PyramidOutcome EnvelopePyramid::build(const Source& source) noexcept {
    size_ = 0U;
    level_count_ = 0U;
    const std::size_t size = source.size();
    if (size > capacity_) return PyramidOutcome::declined_capacity;
    if (size > std::numeric_limits<std::uint32_t>::max()) return PyramidOutcome::declined_size;
    for (std::size_t index = 0U; index < size; ++index) {
        if (std::isnan(source.y(index))) return PyramidOutcome::declined_gaps;
    }
    size_ = size;
    std::size_t buckets = size / pyramid_base;
    while (buckets != 0U && level_count_ < maximum_levels) {
        level_count_ = level_count_ + 1U;
        buckets = buckets / 2U;
    }
    if (level_count_ == 0U) return PyramidOutcome::built;
    compute_level_zero(source, 0U);
    for (std::size_t level = 1U; level < level_count_; ++level) compute_level(source, level, 0U);
    return PyramidOutcome::built;
}

template <typename Source>
PyramidOutcome EnvelopePyramid::extend(const Source& source, std::size_t previous_size) noexcept {
    const std::size_t size = source.size();
    if (previous_size != size_ || size < previous_size) {
        const PyramidOutcome rebuilt = build(source);
        return rebuilt;
    }
    if (size > capacity_) {
        size_ = 0U;
        level_count_ = 0U;
        return PyramidOutcome::declined_capacity;
    }
    if (size > std::numeric_limits<std::uint32_t>::max()) return PyramidOutcome::declined_size;
    for (std::size_t index = previous_size; index < size; ++index) {
        if (!std::isnan(source.y(index))) continue;
        size_ = 0U;
        level_count_ = 0U;
        return PyramidOutcome::declined_gaps;
    }
    size_ = size;
    std::size_t buckets = size / pyramid_base;
    level_count_ = 0U;
    while (buckets != 0U && level_count_ < maximum_levels) {
        level_count_ = level_count_ + 1U;
        buckets = buckets / 2U;
    }
    // The first bucket an appended element can reach, level by level.
    std::size_t first = previous_size / pyramid_base;
    if (level_count_ == 0U) return PyramidOutcome::built;
    compute_level_zero(source, first);
    for (std::size_t level = 1U; level < level_count_; ++level) {
        first = first / 2U;
        compute_level(source, level, first);
    }
    return PyramidOutcome::built;
}

template <typename Source>
void EnvelopePyramid::range_extremes(const Source& source, std::size_t first, std::size_t end, std::size_t& minimum,
                                     std::size_t& maximum) const noexcept {
    minimum = first;
    maximum = first;
    double minimum_y = source.y(first);
    double maximum_y = minimum_y;
    std::size_t index = first + 1U;
    // Elements are visited left to right, so a candidate wins only when
    // strictly more extreme (ties keep the earliest).
    while (index < end) {
        std::size_t candidate_minimum = index;
        std::size_t candidate_maximum = index;
        std::size_t advance = 1U;
        if (index % pyramid_base == 0U && index + pyramid_base <= end && index / pyramid_base < bucket_count(0U)) {
            std::size_t level = 0U;
            while (level + 1U < level_count_) {
                const std::size_t width = pyramid_base << (level + 1U);
                const std::size_t bucket = index / width;
                if (index % width != 0U || index + width > end || bucket >= bucket_count(level + 1U)) break;
                level = level + 1U;
            }
            const std::size_t width = pyramid_base << level;
            const std::size_t bucket = index / width;
            candidate_minimum = minimum_index(level, bucket);
            candidate_maximum = maximum_index(level, bucket);
            advance = width;
        }
        const double low = source.y(candidate_minimum);
        const double high = source.y(candidate_maximum);
        if (low < minimum_y) {
            minimum_y = low;
            minimum = candidate_minimum;
        }
        if (high > maximum_y) {
            maximum_y = high;
            maximum = candidate_maximum;
        }
        index = index + advance;
    }
}

template <typename Source>
DecimationStatistics EnvelopePyramid::envelope(const Source& source, ElementRange range,
                                               const PlotTransform& transform, std::span<Point> out) const noexcept {
    DecimationStatistics statistics{};
    detail::PointWriter writer{};
    writer.out = out;
    const std::size_t end = range.end < size_ ? range.end : size_;
    statistics.points_in = end > range.first ? end - range.first : 0U;
    if (statistics.points_in != 0U) statistics.runs = 1U;
    std::size_t index = range.first;
    while (index < end) {
        const DeviceColumn column = transform.column_of(transform.x().forward(source.x(index)));
        // The column's end: the first later element in another column (the
        // columns of a monotone series never come back).
        std::size_t low = index + 1U;
        std::size_t high = end;
        while (low < high) {
            const std::size_t middle = low + (high - low) / 2U;
            const DeviceColumn probe = transform.column_of(transform.x().forward(source.x(middle)));
            if (probe == column) {
                low = middle + 1U;
            } else {
                high = middle;
            }
        }
        detail::EnvelopeColumn record{};
        record.first = index;
        record.last = low - 1U;
        record.column = column;
        range_extremes(source, index, low, record.minimum, record.maximum);
        detail::emit_column(source, record, transform, writer);
        index = low;
    }
    statistics.points_out = writer.written;
    statistics.truncated = writer.truncated;
    return statistics;
}

inline bool operator==(const EnvelopePyramid& left, const EnvelopePyramid& right) noexcept {
    if (left.size_ != right.size_ || left.level_count_ != right.level_count_) return false;
    for (std::size_t level = 0U; level < left.level_count_; ++level) {
        const std::size_t buckets = left.bucket_count(level);
        for (std::size_t bucket = 0U; bucket < buckets; ++bucket) {
            if (left.minimum_index(level, bucket) != right.minimum_index(level, bucket)) return false;
            if (left.maximum_index(level, bucket) != right.maximum_index(level, bucket)) return false;
        }
    }
    return true;
}

// -- OccupancyGrid ---------------------------------------------------------------

inline void OccupancyGrid::reserve(std::size_t columns, std::size_t rows) {
    cells_.assign(columns * rows, 0U);
    columns_ = columns;
    rows_ = rows;
}

template <typename Source>
DecimationStatistics OccupancyGrid::thin(const Source& source, const PlotTransform& transform,
                                         std::span<Point> out) noexcept {
    DecimationStatistics statistics{};
    detail::PointWriter writer{};
    writer.out = out;
    statistics.points_in = source.size();
    const Rect plot = transform.plot();
    const double scale = transform.device_scale();
    const std::size_t columns = transform.column_count();
    const double row_pixels = std::ceil(plot.height * scale);
    const std::size_t rows = row_pixels < 1.0 ? 1U : static_cast<std::size_t>(row_pixels);
    if (columns > columns_ || rows > rows_) {
        statistics.truncated = true;
        return statistics;
    }
    // Clear only the plot's cells: the cost scales with pixels.
    for (std::size_t row = 0U; row < rows; ++row) {
        std::fill_n(cells_.begin() + static_cast<std::ptrdiff_t>(row * columns_), columns, std::uint8_t{0U});
    }
    for (std::size_t index = 0U; index < source.size(); ++index) {
        const Point device = transform.to_device(source.x(index), source.y(index));
        if (!detail::finite_point(device)) continue;
        const double column_pixel = std::floor((device.x - plot.x) * scale);
        const double row_pixel = std::floor((device.y - plot.y) * scale);
        if (column_pixel < 0.0 || row_pixel < 0.0) continue;
        if (column_pixel >= static_cast<double>(columns) || row_pixel >= static_cast<double>(rows)) continue;
        const std::size_t column = static_cast<std::size_t>(column_pixel);
        const std::size_t row = static_cast<std::size_t>(row_pixel);
        std::uint8_t& cell = cells_[row * columns_ + column];
        if (cell != 0U) continue;
        cell = 1U;
        const Point centre{plot.x + (column_pixel + 0.5) / scale, plot.y + (row_pixel + 0.5) / scale};
        if (!writer.put(centre)) break;
    }
    statistics.points_out = writer.written;
    statistics.truncated = writer.truncated;
    return statistics;
}

// -- binning ---------------------------------------------------------------------

namespace detail {

// The bin of `value` in a valid spec, with bin k's left edge at
// minimum + extent · k / count (computed the same way every time, so a
// value on an edge lands in the bin the edge opens), or count when out of
// range.
inline std::size_t bin_of(double value, const BinSpec& spec) noexcept {
    const double extent = spec.maximum - spec.minimum;
    const double count = static_cast<double>(spec.count);
    double estimate = std::floor((value - spec.minimum) * count / extent);
    estimate = std::clamp(estimate, 0.0, count - 1.0);
    std::size_t bin = static_cast<std::size_t>(estimate);
    const double next_edge = spec.minimum + extent * static_cast<double>(bin + 1U) / count;
    if (bin + 1U < spec.count && value >= next_edge) bin = bin + 1U;
    const double edge = spec.minimum + extent * static_cast<double>(bin) / count;
    if (bin > 0U && value < edge) bin = bin - 1U;
    return bin;
}

inline bool valid_bins(const BinSpec& spec, std::size_t available) noexcept {
    const bool result = spec.count != 0U && std::isfinite(spec.minimum) && std::isfinite(spec.maximum) &&
                        spec.minimum < spec.maximum && available >= spec.count;
    return result;
}

// Tallies one value: true when it was binned (bin written to `bin`).
inline bool tally(double value, const BinSpec& spec, BinTally& tally_out, std::size_t& bin) noexcept {
    if (std::isnan(value)) {
        tally_out.gaps = tally_out.gaps + 1U;
        return false;
    }
    if (value < spec.minimum) {
        tally_out.below = tally_out.below + 1U;
        return false;
    }
    if (value > spec.maximum) {
        tally_out.above = tally_out.above + 1U;
        return false;
    }
    bin = bin_of(value, spec);
    return true;
}

} // namespace detail

template <SeriesElement Element>
BinTally bin_values(SeriesSpans<Element> values, const BinSpec& spec, std::span<std::uint64_t> counts) noexcept {
    BinTally result{};
    if (!detail::valid_bins(spec, counts.size())) {
        result.refused = true;
        return result;
    }
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const double value = series_value(values.at(ElementIndex{index}));
        std::size_t bin = 0U;
        if (!detail::tally(value, spec, result, bin)) continue;
        counts[bin] = counts[bin] + 1U;
        result.binned = result.binned + 1U;
    }
    return result;
}

template <typename Source>
BinTally bin_points(const Source& source, const BinSpec& x_spec, const BinSpec& y_spec,
                    std::span<std::uint64_t> counts) noexcept {
    BinTally result{};
    const bool fits = x_spec.count != 0U && y_spec.count <= counts.size() / x_spec.count;
    if (!fits || !detail::valid_bins(x_spec, x_spec.count) || !detail::valid_bins(y_spec, y_spec.count)) {
        result.refused = true;
        return result;
    }
    for (std::size_t index = 0U; index < source.size(); ++index) {
        std::size_t column = 0U;
        std::size_t row = 0U;
        if (!detail::tally(source.x(index), x_spec, result, column)) continue;
        if (!detail::tally(source.y(index), y_spec, result, row)) continue;
        std::uint64_t& cell = counts[row * x_spec.count + column];
        cell = cell + 1U;
        result.binned = result.binned + 1U;
    }
    return result;
}

} // namespace gui_forms::plots
