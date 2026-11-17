// gui_drawing::execute_recorder: a recording drawn into a Bitmap the
// caller owns, outside any paint, through the static drawing raster that
// the drawing ABI's recorder_execute calls too (one implementation).
// Owned and borrowed targets, partial execution from a command, the
// owner-thread and disposal refusals, and a filled flattened path.
#include "gui_forms/drawing.hpp"
#include "gui_forms/drawing/recorder_raster/recorder_raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace {

using namespace gui_drawing;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "drawing_raster_cxx_tests:" << line << ": " << expression << '\n';
    std::exit(1);
}

#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

// Runs execute_recorder on another thread, keeping its status.
class ExecuteOffThread final {
public:
    ExecuteOffThread(const GraphicsRecorder& recorder, Bitmap& target, RecorderRasterStatus& status)
        : recorder_(recorder), target_(target), status_(status) {}

    void operator()() const {
        const RecorderRasterResult result = execute_recorder(recorder_, target_, 0U);
        status_ = result.status;
    }

private:
    const GraphicsRecorder& recorder_;
    Bitmap& target_;
    RecorderRasterStatus& status_;
};

void owned_bitmap_takes_the_recording() {
    Bitmap bitmap(8, 6);
    GraphicsRecorder recorder;
    recorder.clear(Color::from_argb(UINT32_C(0xff336699)));
    RecorderRasterResult result = execute_recorder(recorder, bitmap, 0U);
    CHECK(result.status == RecorderRasterStatus::executed);
    CHECK(result.commands_executed == 1U);
    CHECK(bitmap.get_pixel(0, 0).argb() == UINT32_C(0xff336699));
    CHECK(bitmap.get_pixel(7, 5).argb() == UINT32_C(0xff336699));

    // From a later command only; at the end, nothing; past it, refused.
    recorder.clear(Color::from_argb(UINT32_C(0xff993311)));
    result = execute_recorder(recorder, bitmap, 1U);
    CHECK(result.status == RecorderRasterStatus::executed);
    CHECK(result.commands_executed == 1U);
    CHECK(bitmap.get_pixel(3, 3).argb() == UINT32_C(0xff993311));
    result = execute_recorder(recorder, bitmap, 2U);
    CHECK(result.status == RecorderRasterStatus::executed);
    CHECK(result.commands_executed == 0U);
    result = execute_recorder(recorder, bitmap, 3U);
    CHECK(result.status == RecorderRasterStatus::invalid_argument);
    CHECK(result.commands_executed == 0U);
}

void borrowed_pixels_are_written_in_place() {
    // A 4 x 3 premultiplied BGRA buffer the test owns, 20 bytes a row (one
    // spare pixel of padding, which must stay as it was).
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 3U;
    constexpr std::size_t row_bytes = 20U;
    std::vector<std::byte> pixels(row_bytes * height, std::byte{0x5a});
    std::unique_ptr<Bitmap> target = Bitmap::borrow(width, height, row_bytes, pixels.data());
    GraphicsRecorder recorder;
    const SolidBrush brush(Color::from_argb(UINT32_C(0xff336699)));
    recorder.fill_rectangle(brush, RectF{0, 0, 4, 3});
    const RecorderRasterResult result = execute_recorder(recorder, *target, 0U);
    CHECK(result.status == RecorderRasterStatus::executed);
    CHECK(result.commands_executed == 1U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * row_bytes;
        for (std::uint32_t x = 0U; x < width; ++x) {
            const std::size_t at = row + static_cast<std::size_t>(x) * 4U;
            CHECK(pixels[at] == std::byte{0x99});
            CHECK(pixels[at + 1U] == std::byte{0x66});
            CHECK(pixels[at + 2U] == std::byte{0x33});
            CHECK(pixels[at + 3U] == std::byte{0xff});
        }
        for (std::size_t pad = 16U; pad < row_bytes; ++pad) CHECK(pixels[row + pad] == std::byte{0x5a});
    }
}

void flattened_path_fills_like_the_path() {
    // A path filled through the raster, and its flattening filled as a
    // polygon: the same pixels at their centres away from the edge.
    GraphicsPath curve;
    curve.add_bezier({2, 30}, {2, 0}, {30, 0}, {30, 30});
    curve.close_figure();
    const PathFlattening size = curve.flattened_size(0.1);
    CHECK(size.status == PathFlatteningStatus::complete && size.figure_count == 1U);
    std::vector<PointF> points(size.point_count);
    std::vector<FlattenedFigure> figures(size.figure_count);
    CHECK(curve.flatten(0.1, points, figures).status == PathFlatteningStatus::complete);
    GraphicsPath polygon;
    polygon.add_polygon(std::span<const PointF>(points.data(), points.size() - 1U));

    const SolidBrush brush(Color::from_argb(UINT32_C(0xff000000)));
    Bitmap from_curve(32, 32);
    Bitmap from_polygon(32, 32);
    GraphicsRecorder curve_recorder;
    curve_recorder.clear(Color::from_argb(UINT32_C(0xffffffff)));
    curve_recorder.fill_path(brush, curve);
    GraphicsRecorder polygon_recorder;
    polygon_recorder.clear(Color::from_argb(UINT32_C(0xffffffff)));
    polygon_recorder.fill_path(brush, polygon);
    CHECK(execute_recorder(curve_recorder, from_curve, 0U).status == RecorderRasterStatus::executed);
    CHECK(execute_recorder(polygon_recorder, from_polygon, 0U).status == RecorderRasterStatus::executed);
    std::size_t differing = 0U;
    for (std::uint32_t y = 0U; y < 32U; ++y) {
        for (std::uint32_t x = 0U; x < 32U; ++x) {
            const std::uint32_t first = from_curve.get_pixel(x, y).argb();
            const std::uint32_t second = from_polygon.get_pixel(x, y).argb();
            const bool both_solid = (first == UINT32_C(0xff000000) || first == UINT32_C(0xffffffff)) &&
                                    (second == UINT32_C(0xff000000) || second == UINT32_C(0xffffffff));
            // Antialiased edge pixels may differ by the 0.1-unit chords;
            // a solid pixel on one side and the opposite solid on the other
            // may not.
            if (both_solid && first != second) differing = differing + 1U;
        }
    }
    CHECK(differing == 0U);
    CHECK(from_curve.get_pixel(16, 20).argb() == UINT32_C(0xff000000));
    CHECK(from_polygon.get_pixel(16, 20).argb() == UINT32_C(0xff000000));
}

void foreign_and_disposed_objects_are_refused() {
    Bitmap bitmap(2, 2);
    GraphicsRecorder recorder;
    recorder.clear(Color::from_argb(UINT32_C(0xff336699)));
    RecorderRasterStatus status = RecorderRasterStatus::executed;
    std::thread other{ExecuteOffThread(recorder, bitmap, status)};
    other.join();
    CHECK(status == RecorderRasterStatus::wrong_thread);
    CHECK(bitmap.get_pixel(0, 0).argb() != UINT32_C(0xff336699));

    Bitmap disposed(2, 2);
    disposed.dispose();
    RecorderRasterResult result = execute_recorder(recorder, disposed, 0U);
    CHECK(result.status == RecorderRasterStatus::invalid_argument);
    GraphicsRecorder gone;
    gone.dispose();
    result = execute_recorder(gone, bitmap, 0U);
    CHECK(result.status == RecorderRasterStatus::invalid_argument);
}

} // namespace

int main() {
    owned_bitmap_takes_the_recording();
    borrowed_pixels_are_written_in_place();
    flattened_path_fills_like_the_path();
    foreign_and_disposed_objects_are_refused();
    std::cout << "drawing_raster_cxx_tests: passed\n";
    return 0;
}
