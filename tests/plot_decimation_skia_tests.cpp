// The envelope drawn by the Skia raster (IMGUI_IMPLOT_STUDY B.3): a dense
// monotone series stroked whole and stroked through decimate_envelope,
// both one device pixel wide with round joins, antialiased. The supercover
// proof (plot_decimation_raster_tests.cpp) shows the two paths cross the
// same pixels; antialiasing weighs those pixels by how much of each the
// stroke covers, which the reduced path changes within a column. Checked
// here: every pixel either drawing touches lies within one pixel of a
// pixel the other touches, and the coverage difference is printed (maximum
// and mean, in alpha steps) as the measurement the study's open bound
// waits for.
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/plots.hpp"
#include "skia_raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::plots;
using gui_forms::render::SkiaRaster;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr Size scene_size{480.0, 160.0};
constexpr Color white = Color::rgba(255, 255, 255);
constexpr Color black = Color::rgba(0, 0, 0);

// The darkness of each pixel (255 - red; the scenes draw black on white),
// row-major.
std::vector<std::uint8_t> darkness(const SkiaRaster& raster) {
    const std::size_t width = raster.pixel_width();
    const std::size_t height = raster.pixel_height();
    std::vector<std::uint8_t> result(width * height);
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(raster.pixels());
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            const std::size_t offset = y * raster.row_bytes() + x * 4U;
            // BGRA: red is the third byte.
            result[y * width + x] = static_cast<std::uint8_t>(255U - bytes[offset + 2U]);
        }
    }
    return result;
}

void stroke_scene(SkiaRaster& raster, std::span<const Point> points) {
    require(raster.resize(scene_size, 1.0) || raster.pixel_width() != 0U, "the raster must have a surface");
    DamageRegion damage;
    damage.add(Rect{0.0, 0.0, scene_size.width, scene_size.height});
    raster.begin_frame(damage);
    raster.fill_rect(Rect{0.0, 0.0, scene_size.width, scene_size.height}, white);
    StrokeStyle stroke{};
    stroke.color = black;
    stroke.width = 1.0;
    stroke.join = StrokeJoin::round;
    stroke.cap = StrokeCap::round;
    raster.stroke_polyline(points, stroke);
    raster.end_frame();
}

// Whether every touched pixel of `from` has a touched pixel of `within` at
// most one pixel away.
bool touched_within_one(const std::vector<std::uint8_t>& from, const std::vector<std::uint8_t>& within,
                        std::size_t width, std::size_t height) {
    for (std::size_t y = 0U; y < height; ++y) {
        for (std::size_t x = 0U; x < width; ++x) {
            if (from[y * width + x] == 0U) continue;
            bool near = false;
            const std::size_t top = y == 0U ? 0U : y - 1U;
            const std::size_t bottom = std::min(height - 1U, y + 1U);
            const std::size_t left = x == 0U ? 0U : x - 1U;
            const std::size_t right = std::min(width - 1U, x + 1U);
            for (std::size_t row = top; row <= bottom && !near; ++row) {
                for (std::size_t column = left; column <= right && !near; ++column) {
                    near = within[row * width + column] != 0U;
                }
            }
            if (!near) return false;
        }
    }
    return true;
}

Axis axis_over(double minimum, double maximum, AxisSide side) {
    Axis axis{};
    axis.range = PlotRange{minimum, maximum};
    axis.side = side;
    return axis;
}

void check_series(const char* name, const std::vector<double>& y_values) {
    const std::size_t count = y_values.size();
    SeriesColumn<double> xs(SeriesMode::growable, count, XOrder::monotone);
    SeriesColumn<double> ys(SeriesMode::growable, count, XOrder::unordered);
    std::vector<double> x_values(count);
    for (std::size_t index = 0U; index < count; ++index) x_values[index] = static_cast<double>(index);
    require(xs.assign(x_values) == SeriesEditOutcome::applied && ys.assign(y_values) == SeriesEditOutcome::applied,
            "the series load");
    ColumnSamples<double, double> samples{};
    samples.xs = xs.read();
    samples.ys = ys.read();
    const PlotTransformResult made =
        PlotTransform::make(axis_over(0.0, static_cast<double>(count - 1U), AxisSide::bottom),
                            axis_over(-1.2, 1.2, AxisSide::left),
                            Rect{0.0, 0.0, scene_size.width, scene_size.height}, 1.0);
    require(made.check == PlotTransformCheck::valid, "the transform is valid");
    const ElementRange range{0U, count};
    std::vector<Point> full(count);
    const DecimationStatistics whole = map_all(samples, range, made.transform, full);
    std::vector<Point> reduced(envelope_capacity(made.transform.column_count()));
    const DecimationStatistics envelope = decimate_envelope(samples, range, made.transform, reduced);
    require(!whole.truncated && !envelope.truncated, "both paths fit");
    SkiaRaster whole_raster;
    SkiaRaster envelope_raster;
    stroke_scene(whole_raster, std::span<const Point>(full.data(), whole.points_out));
    stroke_scene(envelope_raster, std::span<const Point>(reduced.data(), envelope.points_out));
    const std::size_t width = whole_raster.pixel_width();
    const std::size_t height = whole_raster.pixel_height();
    const std::vector<std::uint8_t> a = darkness(whole_raster);
    const std::vector<std::uint8_t> b = darkness(envelope_raster);
    std::uint32_t largest = 0U;
    std::uint64_t total = 0U;
    std::size_t touched = 0U;
    for (std::size_t index = 0U; index < a.size(); ++index) {
        const std::uint32_t difference = a[index] > b[index] ? a[index] - b[index] : b[index] - a[index];
        largest = std::max(largest, difference);
        if (a[index] == 0U && b[index] == 0U) continue;
        total = total + difference;
        touched = touched + 1U;
    }
    const double mean = touched == 0U ? 0.0 : static_cast<double>(total) / static_cast<double>(touched);
    std::cout << name << ": " << whole.points_out << " points whole, " << envelope.points_out
              << " decimated; coverage difference max " << largest << ", mean " << mean << " over " << touched
              << " touched pixels\n";
    require(touched != 0U, "the series is drawn");
    require(touched_within_one(a, b, width, height), "the whole drawing stays within a pixel of the decimated one");
    require(touched_within_one(b, a, width, height), "the decimated drawing stays within a pixel of the whole one");
}

} // namespace

int main() {
    try {
        const std::size_t count = 100000U;
        std::vector<double> walk(count);
        std::vector<double> sine(count);
        std::vector<double> spikes(count);
        double level = 0.0;
        for (std::size_t index = 0U; index < count; ++index) {
            const std::uint64_t bits = EffectRandom::hash(21U, 0U, index) >> 11U;
            const double unit = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
            level = std::clamp(level + (unit - 0.5) * 0.02, -1.0, 1.0);
            walk[index] = level;
            sine[index] = std::sin(static_cast<double>(index) * 0.0021);
            spikes[index] = index % 1777U == 0U ? 1.0 : (unit - 0.5) * 0.1;
        }
        check_series("random walk", walk);
        check_series("sine", sine);
        check_series("spikes", spikes);
        std::cout << "gui_forms_plot_decimation_skia_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_plot_decimation_skia_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
