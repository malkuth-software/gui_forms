#pragma once

// Decimation and level of detail (IMGUI_IMPLOT_STUDY B.3, P6; owner
// decision 2: the exact min-max envelope is the default, never a
// "representative" decimation such as LTTB). Each stage reads one buffer
// and writes the next, all reserved by the caller at configuration; none
// allocates while drawing.
//
// 1. Cull: the visible element range, by binary search for monotone x
//    (visible_range), with one element beyond each end so lines entering
//    from outside are drawn.
// 2. Envelope (lines over monotone x): per device pixel column the first,
//    minimum, maximum and last element, in element order (the M4 scheme).
//    Claim, proved by gui_forms_plot_decimation_raster_tests: for a
//    1-device-pixel aliased stroke the decimated polyline covers exactly
//    the pixels of the full polyline. Within one column every segment
//    between the column's elements lies inside the column, so the full
//    polyline covers that column's rows from its minimum's to its
//    maximum's, as the decimated one does; the segments joining columns
//    run between the same elements (each column's last and the next one's
//    first) in both.
// 3. Pyramid (EnvelopePyramid): min-max levels over a static or growing
//    series, built once in O(n) and updated per appended block; a column's
//    minimum and maximum come from O(log n) buckets, so a frame costs
//    O(columns · log n), not O(n). Its envelope equals the linear one
//    element for element.
// 4. Occupancy (scatter): one marker per occupied device pixel, centred on
//    the pixel's centre. Exact for opaque markers of one colour whose
//    centres are snapped to pixel centres: every point in a pixel draws the
//    same marker.
// 5. Binning: fixed bins, right-open with the last bin closed; NaN and
//    out-of-range values counted apart.
//
// NaN is a gap: the envelope emits one NaN point between runs (the
// Painter's stroke_polyline breaks there) and the pyramid declines a series
// holding gaps (the caller falls back to the linear envelope).

#include "gui_forms/plots/plot_transform/plot_transform.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace gui_forms::plots {

// -- sample sources --------------------------------------------------------------
//
// The kernels are templates over a sample source (a declared choice, chosen
// before the loop): something with size(), x(index) and y(index) returning
// double. Two are provided.

// x and y from two columns' spans of equal length.
template <SeriesElement XElement, SeriesElement YElement>
struct ColumnSamples final {
    SeriesSpans<XElement> xs{};
    SeriesSpans<YElement> ys{};

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t result = xs.size() < ys.size() ? xs.size() : ys.size();
        return result;
    }
    [[nodiscard]] double x(std::size_t index) const noexcept { return series_value(xs.at(ElementIndex{index})); }
    [[nodiscard]] double y(std::size_t index) const noexcept { return series_value(ys.at(ElementIndex{index})); }
};

// y from a column, x implied: x = x_start + index · x_step (a sampled
// signal). Monotone whenever x_step ≥ 0.
template <SeriesElement YElement>
struct ImplicitXSamples final {
    SeriesSpans<YElement> ys{};
    double x_start{};
    double x_step{1.0};

    [[nodiscard]] std::size_t size() const noexcept { return ys.size(); }
    [[nodiscard]] double x(std::size_t index) const noexcept {
        const double result = x_start + static_cast<double>(index) * x_step;
        return result;
    }
    [[nodiscard]] double y(std::size_t index) const noexcept { return series_value(ys.at(ElementIndex{index})); }
};

// -- cull ------------------------------------------------------------------------

// Elements [first, end) of a monotone source.
struct ElementRange final {
    std::size_t first{};
    std::size_t end{};

    [[nodiscard]] std::size_t count() const noexcept { return end > first ? end - first : 0U; }
};

// The elements whose x lies in [minimum, maximum], plus one beyond each
// end, by binary search over a monotone source (NaN x sorts as it lies:
// the search tolerates gaps only where they do not hide the boundary, so a
// monotone column with gaps should hold NaN in y, not x).
template <typename Source>
[[nodiscard]] ElementRange visible_range(const Source& source, double minimum, double maximum) noexcept {
    std::size_t low = 0U;
    std::size_t high = source.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (source.x(middle) < minimum) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    const std::size_t first = low;
    high = source.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (source.x(middle) <= maximum) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    ElementRange range{};
    range.first = first > 0U ? first - 1U : 0U;
    range.end = low < source.size() ? low + 1U : low;
    return range;
}

// -- envelope ----------------------------------------------------------------------

// What a decimation pass did.
struct DecimationStatistics final {
    std::size_t points_in{};
    std::size_t points_out{};
    // Runs of finite points separated by gaps.
    std::size_t runs{};
    // Points left out because `out` was full.
    bool truncated{};
};

// The capacity an envelope output needs for `columns` device columns and
// no gaps: four per column, plus the two edge columns of the culling
// margin. Each gap needs at most five more.
[[nodiscard]] constexpr std::size_t envelope_capacity(std::size_t columns) noexcept {
    return 4U * (columns + 2U);
}

// The envelope of elements `range` of a monotone `source`, mapped by
// `transform` and written to `out` in element order. Columns are device
// pixel columns (PlotTransform::column_of). A NaN y (or x) ends a run: the
// run's open column is emitted, then one NaN point.
template <typename Source>
DecimationStatistics decimate_envelope(const Source& source, ElementRange range, const PlotTransform& transform,
                                       std::span<Point> out) noexcept;

// No decimation: every element of `range` mapped (unordered lines, small
// series). NaN points pass through as gaps.
template <typename Source>
DecimationStatistics map_all(const Source& source, ElementRange range, const PlotTransform& transform,
                             std::span<Point> out) noexcept;

// -- pyramid -----------------------------------------------------------------------

// Min-max levels over a series' y values (decimation stage 3). Level 0's
// buckets hold `pyramid_base` elements; each level's buckets join two of
// the level below. A bucket stores the element indices of its minimum and
// maximum (the earliest on ties), so its envelope keeps element order.
// Memory: at most one index per element in total (n/2 + n/4 + ... minima
// and maxima), reserved by reserve() for the column's capacity.
inline constexpr std::size_t pyramid_base = 4U;

enum class PyramidOutcome : std::uint8_t {
    built,
    // The series holds gaps: the pyramid is empty and the caller uses the
    // linear envelope.
    declined_gaps,
    // More elements than reserve() made room for.
    declined_capacity,
    // More elements than the 32-bit indices reach.
    declined_size,
};

class EnvelopePyramid final {
public:
    // Reserves the levels for up to `capacity` elements (allocates once).
    void reserve(std::size_t capacity);
    // Rebuilds from `source` (y only) in O(n).
    template <typename Source>
    PyramidOutcome build(const Source& source) noexcept;
    // Extends after elements [previous_size, source.size()) were appended:
    // recomputes only the buckets that hold them, at every level.
    template <typename Source>
    PyramidOutcome extend(const Source& source, std::size_t previous_size) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    // The elements reserve() made room for.
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t level_count() const noexcept { return level_count_; }
    // Buckets at `level` (level 0 holds size()/pyramid_base whole buckets).
    [[nodiscard]] std::size_t bucket_count(std::size_t level) const noexcept;
    // The minimum's and maximum's element indices of bucket `bucket` at
    // `level`.
    [[nodiscard]] std::uint32_t minimum_index(std::size_t level, std::size_t bucket) const noexcept;
    [[nodiscard]] std::uint32_t maximum_index(std::size_t level, std::size_t bucket) const noexcept;

    // The envelope of `range`, as decimate_envelope writes it, with each
    // column's minimum and maximum taken from the pyramid. `source` must be
    // the one built (no gaps, monotone x).
    template <typename Source>
    DecimationStatistics envelope(const Source& source, ElementRange range, const PlotTransform& transform,
                                  std::span<Point> out) const noexcept;

    friend bool operator==(const EnvelopePyramid& left, const EnvelopePyramid& right) noexcept;

private:
    template <typename Source>
    void compute_level_zero(const Source& source, std::size_t first_bucket) noexcept;
    template <typename Source>
    void compute_level(const Source& source, std::size_t level, std::size_t first_bucket) noexcept;
    // Where `level` starts in minima_ and maxima_.
    [[nodiscard]] std::size_t level_offset(std::size_t level) const noexcept { return offsets_[level]; }
    // The extreme indices over elements [first, end) of `source`, through
    // the largest aligned buckets.
    template <typename Source>
    void range_extremes(const Source& source, std::size_t first, std::size_t end, std::size_t& minimum,
                        std::size_t& maximum) const noexcept;

    static constexpr std::size_t maximum_levels = 40U;
    std::vector<std::uint32_t> minima_{};
    std::vector<std::uint32_t> maxima_{};
    std::array<std::size_t, maximum_levels + 1U> offsets_{};
    std::size_t capacity_{};
    std::size_t size_{};
    std::size_t level_count_{};
};

// -- occupancy ------------------------------------------------------------------------

// A device pixel grid over a plot, reused between frames (decimation
// stage 4). Caller-owned; reserve() allocates.
class OccupancyGrid final {
public:
    void reserve(std::size_t columns, std::size_t rows);
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }

    // Writes to `out` one centre per occupied device pixel of `transform`'s
    // plot, in the order the pixels were first hit, at the pixel's centre in
    // logical units; points outside the plot or with a NaN coordinate are
    // skipped. The grid must have been reserved at least at the plot's
    // device size (else nothing is written and truncated is set).
    template <typename Source>
    DecimationStatistics thin(const Source& source, const PlotTransform& transform, std::span<Point> out) noexcept;

private:
    std::vector<std::uint8_t> cells_{};
    std::size_t columns_{};
    std::size_t rows_{};
};

// -- binning ----------------------------------------------------------------------------

// Fixed bins over [minimum, maximum]: `count` bins of equal width,
// right-open except the last, which is closed.
struct BinSpec final {
    double minimum{};
    double maximum{1.0};
    std::size_t count{10U};
};

struct BinTally final {
    std::uint64_t binned{};
    std::uint64_t gaps{};
    std::uint64_t below{};
    std::uint64_t above{};
    // The spec is refused (empty or non-finite range, no bins, or `counts`
    // shorter than the bin count): nothing was counted.
    bool refused{};
};

// Adds each value of `values` to `counts` (spec.count entries, not
// cleared first, so blocks accumulate).
template <SeriesElement Element>
BinTally bin_values(SeriesSpans<Element> values, const BinSpec& spec, std::span<std::uint64_t> counts) noexcept;

// Two-dimensional bins (histogram 2D, heatmap resample): `counts` is
// x_spec.count × y_spec.count, row-major by y.
template <typename Source>
BinTally bin_points(const Source& source, const BinSpec& x_spec, const BinSpec& y_spec,
                    std::span<std::uint64_t> counts) noexcept;

} // namespace gui_forms::plots

#include "gui_forms/plots/decimation/decimation_kernels.hpp"
