// The envelope's pixel-identity proof (IMGUI_IMPLOT_STUDY B.3, decimation
// stage 2) as a raster comparison: a monotone series drawn whole and drawn
// through decimate_envelope (and through the pyramid) covers exactly the
// same device pixels under a supercover rasterizer, for random-walk, sine,
// step and spike series at several plot widths, with and without gaps.
//
// The supercover rasterizer marks, for each segment and each device column
// the segment crosses, the rows from the segment's lowest to its highest y
// within that column. Within one column a continuous path covers the rows
// between its extremes there, and the boundary segments are shared, so
// keeping each column's first, minimum, maximum and last element keeps the
// pixel set: that is the claim this test checks. The antialiased raster's
// agreement is measured separately (plot_decimation_skia_tests.cpp).
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/plots.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::plots;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

// A grid of device pixels with a one-pixel border on each side, so
// geometry just outside the plot is compared too.
class PixelSet final {
public:
    PixelSet(std::int64_t columns, std::int64_t rows)
        : columns_(columns), rows_(rows),
          cells_(static_cast<std::size_t>((columns + 2) * (rows + 2)), std::uint8_t{0}) {}

    void cover_column(std::int64_t column, double y_low, double y_high) {
        if (column < -1 || column > columns_) return;
        const std::int64_t first = std::clamp(static_cast<std::int64_t>(std::floor(y_low)), std::int64_t{-1}, rows_);
        const std::int64_t last = std::clamp(static_cast<std::int64_t>(std::floor(y_high)), std::int64_t{-1}, rows_);
        for (std::int64_t row = first; row <= last; ++row) {
            const std::size_t index = static_cast<std::size_t>((row + 1) * (columns_ + 2) + (column + 1));
            cells_[index] = 1U;
        }
    }

    [[nodiscard]] std::size_t covered() const {
        const std::size_t result = static_cast<std::size_t>(std::count(cells_.begin(), cells_.end(), std::uint8_t{1}));
        return result;
    }

    friend bool operator==(const PixelSet& left, const PixelSet& right) noexcept {
        return left.columns_ == right.columns_ && left.rows_ == right.rows_ && left.cells_ == right.cells_;
    }

private:
    std::int64_t columns_{};
    std::int64_t rows_{};
    std::vector<std::uint8_t> cells_{};
};

double y_on_segment(Point from, Point to, double x) {
    if (to.x == from.x) return from.y;
    const double fraction = (x - from.x) / (to.x - from.x);
    const double result = from.y + (to.y - from.y) * fraction;
    return result;
}

// Marks the supercover of one segment (device units = pixels; scale 1).
void cover_segment(PixelSet& pixels, Point from, Point to) {
    const Point left = from.x <= to.x ? from : to;
    const Point right = from.x <= to.x ? to : from;
    const std::int64_t first = static_cast<std::int64_t>(std::floor(left.x));
    const std::int64_t last = static_cast<std::int64_t>(std::floor(right.x));
    if (first == last) {
        pixels.cover_column(first, std::min(left.y, right.y), std::max(left.y, right.y));
        return;
    }
    // Columns far outside the plot add nothing the border does not.
    const std::int64_t begin = std::max(first, std::int64_t{-2});
    const std::int64_t end = std::min(last, std::int64_t{1} << 20);
    for (std::int64_t column = begin; column <= end; ++column) {
        const double enter = std::max(left.x, static_cast<double>(column));
        const double leave = std::min(right.x, static_cast<double>(column + 1));
        const double y_enter = y_on_segment(left, right, enter);
        const double y_leave = y_on_segment(left, right, leave);
        pixels.cover_column(column, std::min(y_enter, y_leave), std::max(y_enter, y_leave));
    }
}

bool finite_point(Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

// Marks a polyline whose non-finite points break it into runs.
void cover_polyline(PixelSet& pixels, std::span<const Point> points) {
    for (std::size_t index = 1U; index < points.size(); ++index) {
        const Point from = points[index - 1U];
        const Point to = points[index];
        if (!finite_point(from) || !finite_point(to)) continue;
        cover_segment(pixels, from, to);
    }
    // A run of one point covers its pixel.
    for (std::size_t index = 0U; index < points.size(); ++index) {
        if (!finite_point(points[index])) continue;
        const bool alone_before = index == 0U || !finite_point(points[index - 1U]);
        const bool alone_after = index + 1U == points.size() || !finite_point(points[index + 1U]);
        if (!alone_before || !alone_after) continue;
        pixels.cover_column(static_cast<std::int64_t>(std::floor(points[index].x)), points[index].y, points[index].y);
    }
}

enum class Shape : std::uint8_t { random_walk, sine, step, spikes };

const char* shape_name(Shape shape) {
    switch (shape) {
    case Shape::random_walk:
        return "random walk";
    case Shape::sine:
        return "sine";
    case Shape::step:
        return "step";
    case Shape::spikes:
        return "spikes";
    }
    return "?";
}

double unit_noise(std::uint64_t seed, std::uint64_t index) {
    const std::uint64_t bits = EffectRandom::hash(seed, 2U, index) >> 11U;
    const double result = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
    return result;
}

// Values in about [-1, 1] (the walk is scaled to it).
std::vector<double> make_series(Shape shape, std::size_t count, bool gaps) {
    std::vector<double> values(count);
    double level = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(count);
        double value = 0.0;
        switch (shape) {
        case Shape::random_walk:
            level = level + (unit_noise(11U, index) - 0.5) * 0.02;
            value = std::clamp(level, -1.0, 1.0);
            break;
        case Shape::sine:
            value = std::sin(t * 6.283185307179586 * 37.0);
            break;
        case Shape::step:
            value = (index / 997U) % 2U == 0U ? -0.75 : 0.75;
            break;
        case Shape::spikes:
            value = index % 1013U == 0U ? 1.0 : (unit_noise(5U, index) - 0.5) * 0.1;
            break;
        default:
            // An enumerator outside the declared set (a corrupt value) does nothing.
            break;
        }
        values[index] = value;
    }
    if (gaps) {
        for (std::size_t index = count / 3U; index < count / 3U + 50U && index < count; ++index) {
            values[index] = not_a_number;
        }
        if (count > 10U) values[count / 2U] = not_a_number;
    }
    return values;
}

Axis axis_over(double minimum, double maximum, AxisSide side) {
    Axis axis{};
    axis.range = PlotRange{minimum, maximum};
    axis.side = side;
    return axis;
}

struct Case final {
    Shape shape{};
    std::size_t count{};
    double width{};
    double height{};
    bool gaps{};
};

void check_case(const Case& test) {
    SeriesColumn<double> xs(SeriesMode::growable, test.count, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::growable, test.count, XOrder::unordered);
    std::vector<double> x_values(test.count);
    for (std::size_t index = 0U; index < test.count; ++index) {
        x_values[index] = static_cast<double>(index) * 0.5 + 0.125;
    }
    const std::vector<double> y_values = make_series(test.shape, test.count, test.gaps);
    require(xs.assign(x_values) == SeriesEditOutcome::applied, "x loads");
    require(ys.assign(y_values) == SeriesEditOutcome::applied, "y loads");
    ColumnSamples<double, double> samples{};
    samples.xs = xs.read();
    samples.ys = ys.read();
    // The view shows the middle of the series, so culling at both ends is
    // part of the comparison.
    const double span = static_cast<double>(test.count) * 0.5;
    const PlotTransformResult made =
        PlotTransform::make(axis_over(span * 0.1, span * 0.9, AxisSide::bottom),
                            axis_over(-1.2, 1.2, AxisSide::left), Rect{0.0, 0.0, test.width, test.height}, 1.0);
    require(made.check == PlotTransformCheck::valid, "the transform is valid");
    const PlotTransform& transform = made.transform;
    const ElementRange range = visible_range(samples, span * 0.1, span * 0.9);
    std::vector<Point> full(range.count());
    const DecimationStatistics whole = map_all(samples, range, transform, full);
    require(!whole.truncated, "the whole series fits its buffer");
    // Each gap adds one gap point and may split a column between two runs
    // (up to four more points): the cases hold at most two gaps.
    constexpr std::size_t gap_allowance = 2U * 5U;
    std::vector<Point> reduced(envelope_capacity(transform.column_count()) + gap_allowance);
    const DecimationStatistics envelope = decimate_envelope(samples, range, transform, reduced);
    require(!envelope.truncated && envelope.runs != 0U, "the envelope fits its capacity");
    require(envelope.points_out <= envelope_capacity(transform.column_count()) + 5U * (envelope.runs - 1U),
            "the envelope is bounded by the plot's width and its gaps");
    const std::int64_t columns = static_cast<std::int64_t>(test.width);
    const std::int64_t rows = static_cast<std::int64_t>(test.height);
    PixelSet whole_pixels(columns, rows);
    PixelSet envelope_pixels(columns, rows);
    cover_polyline(whole_pixels, std::span<const Point>(full.data(), whole.points_out));
    cover_polyline(envelope_pixels, std::span<const Point>(reduced.data(), envelope.points_out));
    if (!(whole_pixels == envelope_pixels)) {
        std::cerr << shape_name(test.shape) << ", " << test.count << " points, " << test.width << " wide"
                  << (test.gaps ? ", with gaps" : "") << ": " << whole_pixels.covered() << " pixels whole, "
                  << envelope_pixels.covered() << " decimated\n";
    }
    require(whole_pixels == envelope_pixels, "the envelope covers exactly the whole series' pixels");
    require(whole_pixels.covered() != 0U, "the series is visible");
    if (test.gaps) return;
    // The pyramid's envelope is the same points, so the same pixels.
    EnvelopePyramid pyramid;
    pyramid.reserve(test.count);
    require(pyramid.build(samples) == PyramidOutcome::built, "the pyramid builds");
    std::vector<Point> levelled(envelope_capacity(transform.column_count()));
    const DecimationStatistics level = pyramid.envelope(samples, range, transform, levelled);
    PixelSet pyramid_pixels(columns, rows);
    cover_polyline(pyramid_pixels, std::span<const Point>(levelled.data(), level.points_out));
    require(pyramid_pixels == whole_pixels, "the pyramid's envelope covers the same pixels");
}

} // namespace

int main() {
    try {
        const std::array<Shape, 4> shapes{Shape::random_walk, Shape::sine, Shape::step, Shape::spikes};
        const std::array<double, 3> widths{97.0, 300.0, 1920.0};
        std::size_t cases = 0U;
        for (const Shape shape : shapes) {
            for (const double width : widths) {
                for (const bool gaps : std::array<bool, 2>{false, true}) {
                    Case test{};
                    test.shape = shape;
                    test.count = 200003U;
                    test.width = width;
                    test.height = 120.0;
                    test.gaps = gaps;
                    check_case(test);
                    cases = cases + 1U;
                }
            }
        }
        // Fewer points than columns: the envelope keeps every point.
        Case sparse{};
        sparse.shape = Shape::sine;
        sparse.count = 401U;
        sparse.width = 1920.0;
        sparse.height = 200.0;
        check_case(sparse);
        cases = cases + 1U;
        std::cout << "gui_forms_plot_decimation_raster_tests: " << cases << " cases passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_plot_decimation_raster_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
