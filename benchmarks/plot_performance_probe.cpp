// The plots package's performance probe (IMGUI_IMPLOT_STUDY B.7): the
// costs the study's budgets name, measured headless with a painter that
// records nothing, so only the package's own work is timed.
//
//   pyramid build    1,000,000 points, built once
//   pan frame        one frame of a 1,000,000-point line panned by 1/100
//                    of its view: culling, the pyramid's envelope, mapping
//   linear envelope  the same frame without the pyramid
//   live frame       4,000 samples pushed through a ring channel into
//                    100,000-sample rings, then the envelope
//   scatter          1,000,000 points thinned on the occupancy grid
//
// Prints the median of each workload's repetitions in microseconds. Built
// only when asked for (EXCLUDE_FROM_ALL); run in Release. No result here is
// a claim of speed without its recorded workload and machine.
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/plots.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::plots;

// Counts the batch calls and the points they carry; draws nothing.
class CountingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void stroke_polyline(std::span<const Point> points, const StrokeStyle&) override {
        calls = calls + 1U;
        points_drawn = points_drawn + points.size();
    }
    void draw_markers(std::span<const Point> centers, const MarkerStyle&) override {
        calls = calls + 1U;
        points_drawn = points_drawn + centers.size();
    }

    std::uint64_t calls{};
    std::uint64_t points_drawn{};
};

using Clock = std::chrono::steady_clock;
constexpr std::size_t repetitions = 31U;

double microseconds_since(Clock::time_point start) {
    const std::chrono::duration<double, std::micro> elapsed = Clock::now() - start;
    return elapsed.count();
}

double median(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    const double result = samples[samples.size() / 2U];
    return result;
}

void report(std::string_view name, std::vector<double>& samples, std::uint64_t points) {
    std::cout << name << ": median " << median(samples) << " us over " << samples.size() << " runs (" << points
              << " points drawn last run)\n";
}

Axis axis_over(double minimum, double maximum, AxisSide side) {
    Axis axis{};
    axis.range = PlotRange{minimum, maximum};
    axis.side = side;
    return axis;
}

std::vector<double> random_walk(std::size_t count, std::uint64_t seed) {
    std::vector<double> values(count);
    double level = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint64_t bits = EffectRandom::hash(seed, 0U, index) >> 11U;
        level = level + (static_cast<double>(bits) * (1.0 / 9007199254740992.0) - 0.5);
        values[index] = level;
    }
    return values;
}

void probe_static_line() {
    const std::size_t count = 1000000U;
    SeriesColumn<double> xs(SeriesMode::growable, count, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::growable, count, XOrder::unordered);
    std::vector<double> x_values(count);
    for (std::size_t index = 0U; index < count; ++index) x_values[index] = static_cast<double>(index);
    static_cast<void>(xs.assign(x_values));
    static_cast<void>(ys.assign(random_walk(count, 1U)));
    ColumnSamples<double, double> samples{};
    samples.xs = xs.read();
    samples.ys = ys.read();
    EnvelopePyramid pyramid;
    pyramid.reserve(count);
    std::vector<double> build_times;
    for (std::size_t run = 0U; run < repetitions; ++run) {
        const Clock::time_point start = Clock::now();
        static_cast<void>(pyramid.build(samples));
        build_times.push_back(microseconds_since(start));
    }
    report("pyramid build (1M)", build_times, 0U);
    const Rect plot{0.0, 0.0, 1920.0, 600.0};
    SeriesMarkPainter marks;
    marks.reserve(1920U, 0U);
    CountingPainter painter;
    StrokeStyle stroke{};
    std::vector<double> pyramid_times;
    std::vector<double> linear_times;
    for (std::size_t run = 0U; run < repetitions; ++run) {
        // A view of 600,000 points, panned by 6,000 a frame.
        const double left = 100000.0 + static_cast<double>(run) * 6000.0;
        const PlotTransformResult made = PlotTransform::make(axis_over(left, left + 600000.0, AxisSide::bottom),
                                                             axis_over(-2000.0, 2000.0, AxisSide::left), plot, 1.0);
        Clock::time_point start = Clock::now();
        static_cast<void>(marks.paint_line(painter, samples, XOrder::monotone, made.transform, stroke,
                                           LineDecimation::pyramid, &pyramid));
        pyramid_times.push_back(microseconds_since(start));
        start = Clock::now();
        static_cast<void>(marks.paint_line(painter, samples, XOrder::monotone, made.transform, stroke,
                                           LineDecimation::envelope, nullptr));
        linear_times.push_back(microseconds_since(start));
    }
    report("pan frame, pyramid (1M, 1920 columns)", pyramid_times, painter.points_drawn);
    report("pan frame, linear envelope (1M, 1920 columns)", linear_times, painter.points_drawn);
}

void probe_live_ring() {
    const std::size_t capacity = 100000U;
    SeriesRingChannel channel(8192U, 2U);
    std::unique_ptr<SeriesWriteLease> lease = channel.acquire_write_lease();
    SeriesColumn<double> xs(SeriesMode::ring, capacity, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::ring, capacity, XOrder::unordered);
    std::array<SeriesColumn<double>*, 2> columns{&xs, &ys};
    std::vector<double> scratch(8192U * 2U);
    std::vector<double> block(4000U * 2U);
    SeriesMarkPainter marks;
    marks.reserve(1920U, 0U);
    CountingPainter painter;
    StrokeStyle stroke{};
    const Rect plot{0.0, 0.0, 1920.0, 600.0};
    std::uint64_t sample = 0U;
    std::vector<double> times;
    for (std::size_t run = 0U; run < repetitions + 30U; ++run) {
        for (std::size_t index = 0U; index < 4000U; ++index) {
            block[index * 2U] = static_cast<double>(sample) / 4000.0;
            const std::uint64_t bits = EffectRandom::hash(3U, 0U, sample) >> 11U;
            block[index * 2U + 1U] = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
            sample = sample + 1U;
        }
        (*lease).write(block);
        const double newest = static_cast<double>(sample - 1U) / 4000.0;
        const Clock::time_point start = Clock::now();
        static_cast<void>(channel.consume(columns, scratch));
        ColumnSamples<double, double> samples{};
        samples.xs = xs.read();
        samples.ys = ys.read();
        const PlotTransformResult made = PlotTransform::make(axis_over(newest - 25.0, newest, AxisSide::bottom),
                                                             axis_over(0.0, 1.0, AxisSide::left), plot, 1.0);
        static_cast<void>(marks.paint_line(painter, samples, XOrder::monotone, made.transform, stroke,
                                           LineDecimation::envelope, nullptr));
        // The first frames fill the ring; only full-ring frames count.
        if (run >= 30U) times.push_back(microseconds_since(start));
    }
    report("live frame (4,000 samples into 100,000-sample rings)", times, painter.points_drawn);
}

void probe_scatter() {
    const std::size_t count = 1000000U;
    SeriesColumn<double> xs(SeriesMode::growable, count, XOrder::unordered);
    SeriesColumn<double> ys(SeriesMode::growable, count, XOrder::unordered);
    std::vector<double> x_values(count);
    std::vector<double> y_values(count);
    for (std::size_t index = 0U; index < count; ++index) {
        x_values[index] = static_cast<double>(EffectRandom::hash(4U, 0U, index) >> 11U) * (1.0 / 9007199254740992.0);
        y_values[index] = static_cast<double>(EffectRandom::hash(4U, 1U, index) >> 11U) * (1.0 / 9007199254740992.0);
    }
    static_cast<void>(xs.assign(x_values));
    static_cast<void>(ys.assign(y_values));
    ColumnSamples<double, double> samples{};
    samples.xs = xs.read();
    samples.ys = ys.read();
    const Rect plot{0.0, 0.0, 800.0, 600.0};
    const PlotTransformResult made =
        PlotTransform::make(axis_over(0.0, 1.0, AxisSide::bottom), axis_over(0.0, 1.0, AxisSide::left), plot, 1.0);
    OccupancyGrid grid;
    grid.reserve(800U, 600U);
    SeriesMarkPainter marks;
    marks.reserve(800U, 800U * 600U);
    CountingPainter painter;
    MarkerStyle marker{};
    marker.fill = Color::rgba(0, 0, 0);
    std::vector<double> times;
    for (std::size_t run = 0U; run < repetitions; ++run) {
        const Clock::time_point start = Clock::now();
        static_cast<void>(marks.paint_scatter(painter, samples, made.transform, marker, &grid));
        times.push_back(microseconds_since(start));
    }
    report("scatter on the occupancy grid (1M, 800 x 600)", times, painter.points_drawn);
}

} // namespace

int main() {
    probe_static_line();
    probe_live_ring();
    probe_scatter();
    return EXIT_SUCCESS;
}
