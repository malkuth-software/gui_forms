#include "drawing_skia.hpp"
#include "image/png_test_files.hpp"
#if defined(__APPLE__)
#include "core_text_font_bytes.hpp"
#endif

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_drawing;
using namespace gui_drawing::render;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "drawing_skia_tests:" << line << ": " << expression << '\n';
    std::exit(1);
}

#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

std::vector<std::byte> read_file(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    CHECK(static_cast<bool>(input));
    const std::streamsize size = input.tellg();
    CHECK(size > 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    CHECK(static_cast<bool>(input));
    return bytes;
}

std::uint64_t checksum(const ImageSnapshot& image) {
    std::uint64_t value = UINT64_C(1469598103934665603);
    for (const std::byte byte : image.pixels()) {
        value ^= std::to_integer<std::uint8_t>(byte);
        value *= UINT64_C(1099511628211);
    }
    return value;
}

class ExecuteSkiaOffThread final {
public:
    ExecuteSkiaOffThread(SkiaExecutor& executor,
                         const GraphicsRecorder& recorder, Bitmap& target,
                         std::atomic<RasterError>& result)
        : executor_(executor), recorder_(recorder), target_(target),
          result_(result) {}

    void operator()() const {
        result_ = executor_.execute(recorder_, target_).error;
    }

private:
    SkiaExecutor& executor_;
    const GraphicsRecorder& recorder_;
    Bitmap& target_;
    std::atomic<RasterError>& result_;
};

void command_execution_and_png_round_trip() {
    SkiaExecutor executor;
    const std::vector<std::byte> font = read_file(GUI_DRAWING_TEST_FONT);
    CHECK(executor.register_typeface("Portsmouth Rapids", font));

    Bitmap target(64, 48);
    SolidBrush red(Color::from_name("red"));
    SolidBrush green(Color::from_name("lime"));
    Pen blue(Color::from_name("blue"), 2.0);
    Font title("Portsmouth Rapids", 12.0, FontStyle::bold);
    StringFormat format;
    GraphicsPath path(FillMode::winding);
    path.add_rectangle({42, 4, 12, 10});
    path.add_ellipse({45, 7, 8, 8});

    GraphicsRecorder recorder;
    recorder.clear(Color::from_name("white"));
    recorder.fill_rectangle(red, {2, 2, 12, 10});
    recorder.draw_ellipse(blue, {18, 2, 14, 12});
    const PointF triangle[] = {{4, 20}, {16, 20}, {10, 32}};
    recorder.fill_polygon(green, triangle, FillMode::winding);
    recorder.draw_path(blue, path);
    recorder.draw_string("M11f", title, red, {34, 34}, format);
    const RasterResult rendered = executor.execute(recorder, target);
    CHECK(rendered && rendered.commands_executed == recorder.commands().size());
    CHECK(target.get_pixel(3, 3).argb() == UINT32_C(0xffff0000));
    CHECK(target.get_pixel(0, 0).argb() == UINT32_C(0xffffffff));
    CHECK(target.get_pixel(10, 24).green() > 200U);
    const std::uint64_t first_checksum = checksum(target.snapshot());
    CHECK(first_checksum != UINT64_C(1469598103934665603));

    RasterError encode_error = RasterError::internal_error;
    const std::vector<std::byte> png = executor.encode_png(target, &encode_error);
    CHECK(encode_error == RasterError::none && png.size() > 32U);
    constexpr std::array<std::uint8_t, 8> signature{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    for (std::size_t index = 0; index < signature.size(); ++index) {
        CHECK(std::to_integer<std::uint8_t>(png[index]) == signature[index]);
    }
    const std::vector<std::byte> repeated = executor.encode_png(target, &encode_error);
    CHECK(repeated == png);

    DecodeResult decoded = executor.decode_png(png);
    CHECK(decoded && (*decoded.bitmap).width() == 64U &&
          (*decoded.bitmap).height() == 48U);
    CHECK(checksum((*decoded.bitmap).snapshot()) == first_checksum);

    // Owner decision D1, option 2: GUI.Drawing decode refuses an embedded
    // ICC profile; gAMA is converted to sRGB (stage I6) unless an sRGB chunk
    // outranks it. The encoder writes an sRGB chunk; start from a file
    // without one.
    const std::vector<std::byte> untagged = gui_forms_png_files::without_chunk(png, "sRGB");
    DecodeResult untagged_decoded = executor.decode_png(untagged);
    CHECK(untagged_decoded && checksum((*untagged_decoded.bitmap).snapshot()) == first_checksum);
    const std::vector<std::byte> profiled = gui_forms_png_files::with_chunk_after_header(
        untagged, "iCCP", gui_forms_png_files::icc_profile_payload());
    CHECK(executor.decode_png(profiled).error == RasterError::decode_failed);
    const std::vector<std::byte> gamma = gui_forms_png_files::with_chunk_after_header(
        untagged, "gAMA", gui_forms_png_files::gamma_payload(100000));
    DecodeResult gamma_decoded = executor.decode_png(gamma);
    CHECK(gamma_decoded && checksum((*gamma_decoded.bitmap).snapshot()) != first_checksum);
    // sRGB outranks gAMA: the original file plus gAMA decodes unchanged.
    const std::vector<std::byte> ranked = gui_forms_png_files::with_chunk_after_header(
        png, "gAMA", gui_forms_png_files::gamma_payload(100000));
    DecodeResult ranked_decoded = executor.decode_png(ranked);
    CHECK(ranked_decoded && checksum((*ranked_decoded.bitmap).snapshot()) == first_checksum);

    std::vector<std::byte> malformed = png;
    malformed[0] = std::byte{0};
    CHECK(executor.decode_png(malformed).error == RasterError::decode_failed);
    PngCodecLimits tiny;
    tiny.maximum_encoded_bytes = 16;
    CHECK(executor.decode_png(png, tiny).error == RasterError::encoded_limit_exceeded);
    CHECK(executor.encode_png(target, &encode_error, tiny).empty());
    CHECK(encode_error == RasterError::encoded_limit_exceeded);
}

void image_attributes_clip_and_snapshot_execution() {
    SkiaExecutor executor;
    Bitmap source(2, 2);
    source.set_pixel(0, 0, Color::from_name("blue"));
    source.set_pixel(1, 0, Color::from_name("red"));
    source.set_pixel(0, 1, Color::from_name("white"));
    source.set_pixel(1, 1, Color::from_name("black"));
    ImageAttributes attributes;
    std::array<double, 25> matrix{};
    matrix[0] = matrix[6] = matrix[12] = matrix[18] = matrix[24] = 1.0;
    matrix[18] = 0.5;
    attributes.set_color_matrix(matrix);
    const ImageAttributesSnapshot::ColorRemap remap[] = {
        {Color::from_name("blue"), Color::from_name("yellow")},
    };
    attributes.set_remap_table(remap);

    GraphicsRecorder recorder;
    recorder.set_quality(SmoothingMode::anti_alias, InterpolationMode::nearest,
                         PixelOffsetMode::none, CompositingMode::source_copy,
                         CompositingQuality::high_quality);
    recorder.translate(4, 3);
    recorder.set_clip({0, 0, 4, 4});
    recorder.draw_image(source, {0, 0, 4, 4}, {0, 0, 2, 2}, attributes);
    source.set_pixel(0, 0, Color::from_name("lime"));

    Bitmap target(12, 10);
    CHECK(executor.execute(recorder, target));
    const Color retained_yellow = target.get_pixel(4, 3);
    CHECK(retained_yellow.red() > 240U && retained_yellow.green() > 240U &&
          retained_yellow.blue() < 8U && retained_yellow.alpha() == 128U);
    CHECK(target.get_pixel(8, 3).alpha() == 0U);

    GraphicsRecorder missing;
    ImageReference logical_only(99, 2, 2, PixelFormat::bgra32_premultiplied);
    missing.clear(Color::from_name("red"));
    missing.draw_image(logical_only, {0, 0, 2, 2}, {0, 0, 2, 2}, attributes);
    const ImageSnapshot before_failed_execution = target.snapshot();
    const std::uint64_t before_failed_checksum = checksum(before_failed_execution);
    const std::uint64_t before_failed_generation = target.generation();
    CHECK(executor.execute(missing, target).error == RasterError::missing_image_pixels);
    CHECK(target.generation() == before_failed_generation);
    CHECK(checksum(target.snapshot()) == before_failed_checksum);

    std::atomic<RasterError> foreign_error{RasterError::none};
    std::thread foreign(ExecuteSkiaOffThread(
        executor, recorder, target, foreign_error));
    foreign.join();
    CHECK(foreign_error.load() == RasterError::wrong_thread);
}

void texture_brush_tiles_and_mirrors_retained_pixels() {
    SkiaExecutor executor;
    Bitmap source(2, 1);
    source.set_pixel(0, 0, Color::from_name("blue"));
    source.set_pixel(1, 0, Color::from_name("red"));
    TextureBrush texture(source, WrapMode::tile_flip_x);

    GraphicsRecorder recorder;
    recorder.set_quality(SmoothingMode::none, InterpolationMode::nearest,
                         PixelOffsetMode::none, CompositingMode::source_copy,
                         CompositingQuality::high_speed);
    recorder.fill_rectangle(texture, {0.0, 0.0, 6.0, 2.0});
    source.set_pixel(0, 0, Color::from_name("lime"));

    Bitmap target(6, 2);
    CHECK(executor.execute(recorder, target));
    const std::array<std::uint32_t, 6> expected{
        Color::from_name("blue").argb(), Color::from_name("red").argb(),
        Color::from_name("red").argb(), Color::from_name("blue").argb(),
        Color::from_name("blue").argb(), Color::from_name("red").argb()};
    for (std::uint32_t x = 0; x < expected.size(); ++x) {
        CHECK(target.get_pixel(x, 0).argb() == expected[x]);
    }
}

void transparent_png_channels_are_premultiplied() {
    constexpr std::array<std::uint8_t, 70> encoded{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9c, 0x63, 0xf8, 0xf8, 0xf1, 0x23,
        0x03, 0x00, 0x08, 0x7e, 0x02, 0xd4, 0x4f, 0x05,
        0x2f, 0xd9, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    std::array<std::byte, encoded.size()> bytes{};
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        bytes[index] = static_cast<std::byte>(encoded[index]);
    }
    SkiaExecutor executor;
    DecodeResult decoded = executor.decode_png(bytes);
    CHECK(decoded && (*decoded.bitmap).width() == 1U &&
          (*decoded.bitmap).height() == 1U);
    const ImageSnapshot snapshot = (*decoded.bitmap).snapshot();
    CHECK(snapshot.pixels().size() == 4U);
    CHECK(snapshot.pixels()[0] == std::byte{0} &&
          snapshot.pixels()[1] == std::byte{0} &&
          snapshot.pixels()[2] == std::byte{0} &&
          snapshot.pixels()[3] == std::byte{0});
}

void gradients_and_hatches_are_native_raster_commands() {
    SkiaExecutor executor;
    Bitmap target(48, 16);
    LinearGradientBrush linear({0, 0, 16, 16}, Color::from_name("red"),
                               Color::from_name("blue"), 0.0,
                               WrapMode::clamp);
    HatchBrush hatch(HatchStyle::diagonal_cross, Color::from_name("white"),
                     Color::from_name("black"));
    const PointF boundary[] = {{32, 0}, {48, 0}, {48, 16}, {32, 16}};
    PathGradientBrush radial(boundary);
    radial.set_center_point({40, 8});
    radial.set_center_color(Color::from_name("white"));
    const Color surround[] = {Color::from_name("black")};
    radial.set_surround_colors(surround);

    GraphicsRecorder recorder;
    recorder.fill_rectangle(linear, {0, 0, 16, 16});
    recorder.fill_rectangle(hatch, {16, 0, 16, 16});
    recorder.fill_rectangle(radial, {32, 0, 16, 16});
    CHECK(executor.execute(recorder, target));

    CHECK(target.get_pixel(1, 8).red() > target.get_pixel(1, 8).blue());
    CHECK(target.get_pixel(14, 8).blue() > target.get_pixel(14, 8).red());
    CHECK(target.get_pixel(16, 0).argb() != target.get_pixel(17, 0).argb() ||
          target.get_pixel(16, 1).argb() != target.get_pixel(17, 1).argb());
    const Color center = target.get_pixel(40, 8);
    const Color edge = target.get_pixel(47, 8);
    CHECK(center.red() > edge.red());
}

// A gradient brush whose geometry stx_skia refuses (bounds wider than float can
// hold, so an end point is infinite) fails its command with invalid_argument and
// leaves the target untouched, instead of filling with the paint's plain color.
void refused_gradient_fails_without_drawing() {
    SkiaExecutor executor;
    Bitmap target(8, 8);
    target.set_pixel(3, 3, Color::from_name("lime"));
    const ImageSnapshot before = target.snapshot();
    const std::uint64_t before_checksum = checksum(before);
    const std::uint64_t before_generation = target.generation();

    LinearGradientBrush overflowing({0, 0, 1e39, 8}, Color::from_name("red"),
                                    Color::from_name("blue"), 0.0, WrapMode::clamp);
    GraphicsRecorder recorder;
    recorder.fill_rectangle(overflowing, {0, 0, 8, 8});
    const RasterResult refused = executor.execute(recorder, target);
    CHECK(refused.error == RasterError::invalid_argument);
    CHECK(refused.commands_executed == 0U);
    CHECK(target.generation() == before_generation);
    CHECK(checksum(target.snapshot()) == before_checksum);
}

// A pen whose dash pattern stx_skia refuses (an entry wider than float can hold)
// fails its command with invalid_argument and leaves the target untouched,
// instead of drawing a solid line.
void refused_dash_fails_without_drawing() {
    SkiaExecutor executor;
    Bitmap target(8, 8);
    target.set_pixel(3, 3, Color::from_name("lime"));
    const std::uint64_t before_checksum = checksum(target.snapshot());
    const std::uint64_t before_generation = target.generation();

    Pen overflowing(Color::from_name("blue"), 2.0);
    const double pattern[] = {1e39, 1.0};
    overflowing.set_dash_pattern(pattern);
    GraphicsRecorder recorder;
    recorder.draw_line(overflowing, {0, 4}, {8, 4});
    const RasterResult refused = executor.execute(recorder, target);
    CHECK(refused.error == RasterError::invalid_argument);
    CHECK(refused.commands_executed == 0U);
    CHECK(target.generation() == before_generation);
    CHECK(checksum(target.snapshot()) == before_checksum);
}

// The drawing-primitives commands as native raster work: each mark lands in
// its own region of one target, sampled at interior pixels.
void batched_marks_brushes_grid_and_proportion_rasterize() {
    SkiaExecutor executor;
    Bitmap target(64, 64);
    Pen green(Color::from_name("lime"), 4.0);
    green.set_line_join(LineJoin::round, 4.0);
    SolidBrush red(Color::from_name("red"));
    SolidBrush blue(Color::from_name("blue"));
    Pen thin_blue(Color::from_name("blue"), 2.0);
    Pen square_cap(Color::from_name("blue"), 4.0);
    square_cap.set_line_cap(LineCap::square);
    Pen flat_cap(Color::from_name("blue"), 4.0);

    GraphicsRecorder recorder;
    const PointF polyline[] = {{2, 4}, {16, 4}, {30, 4}};
    recorder.draw_lines(green, polyline);
    const RectF rects[] = {{0, 8, 4, 4}, {8, 8, 4, 4}};
    recorder.fill_rectangles(red, rects);
    const PointF segments[] = {{0, 20}, {10, 20}, {20, 20}, {30, 20}};
    recorder.draw_line_segments(thin_blue, segments);
    MarkerStyle marker;
    marker.shape = MarkerShape::square;
    marker.size = 6.0;
    marker.fill = Color::from_name("red");
    marker.edge = Color::empty();
    marker.edge_width = 0.0;
    const PointF centers[] = {{40, 5}, {52, 5}};
    recorder.draw_markers(centers, marker);
    ConicGradientBrush conic({40, 24}, 0.0, Color::from_name("red"), Color::from_name("blue"));
    recorder.fill_rectangle(conic, {32, 16, 16, 16});
    CheckerBrush checker(4.0, Color::from_name("white"), Color::from_name("black"));
    recorder.fill_rectangle(checker, {0, 32, 16, 16});
    GridPatternStyle grid;
    grid.origin = PointF{16, 32};
    grid.spacing = 8.0;
    grid.major_every = 2U;
    grid.minor = Color::from_name("lime");
    grid.major = Color::from_name("red");
    grid.line_width = 2.0;
    recorder.fill_grid_pattern({16, 32, 32, 16}, grid);
    recorder.fill_proportion(blue, {0, 52, 40, 10}, CornerRadii{5, 5, 5, 5}, 0.5, FillDirection::left_to_right);
    const PointF capped[] = {{50, 40}, {56, 40}};
    recorder.draw_line_segments(square_cap, capped);
    const PointF flat[] = {{50, 46}, {56, 46}};
    recorder.draw_line_segments(flat_cap, flat);
    const RasterResult result = executor.execute(recorder, target);
    CHECK(result);
    CHECK(result.commands_executed == 10U);

    CHECK(target.get_pixel(16, 4).green() > 200U);
    CHECK(target.get_pixel(16, 10).alpha() == 0U);
    CHECK(target.get_pixel(2, 10).red() > 200U);
    CHECK(target.get_pixel(6, 10).alpha() == 0U);
    CHECK(target.get_pixel(10, 10).red() > 200U);
    CHECK(target.get_pixel(5, 20).blue() > 200U);
    CHECK(target.get_pixel(15, 20).alpha() == 0U);
    CHECK(target.get_pixel(25, 20).blue() > 200U);
    CHECK(target.get_pixel(40, 5).red() > 200U);
    CHECK(target.get_pixel(46, 5).alpha() == 0U);
    CHECK(target.get_pixel(52, 5).red() > 200U);
    // Just clockwise of the start angle the sweep is still red; just
    // anticlockwise of it, one turn later, it has reached blue.
    CHECK(target.get_pixel(46, 25).red() > target.get_pixel(46, 25).blue());
    CHECK(target.get_pixel(46, 22).blue() > target.get_pixel(46, 22).red());
    // A first (white) cell's corner sits at the origin: cells alternate
    // every 4 units in both directions.
    CHECK(target.get_pixel(1, 33).red() == 255U);
    CHECK(target.get_pixel(5, 33).red() == 0U);
    CHECK(target.get_pixel(5, 33).alpha() == 255U);
    CHECK(target.get_pixel(5, 37).red() == 255U);
    // Minor line through x = 24, major through x = 32, nothing between.
    CHECK(target.get_pixel(24, 36).green() > 200U);
    CHECK(target.get_pixel(24, 36).red() < 50U);
    CHECK(target.get_pixel(32, 36).red() > 200U);
    CHECK(target.get_pixel(28, 36).alpha() == 0U);
    // Half the track filled from the left, the rounded corner left clear.
    CHECK(target.get_pixel(10, 57).blue() > 200U);
    CHECK(target.get_pixel(30, 57).alpha() == 0U);
    CHECK(target.get_pixel(0, 52).alpha() < 128U);
    // A square cap reaches half the width beyond the end; a flat one stops.
    CHECK(target.get_pixel(57, 40).blue() > 200U);
    CHECK(target.get_pixel(57, 46).alpha() == 0U);
}

// Text over a fill draws in the on-fill colour inside the fill and the
// plain colour outside, with the two parts meeting at the fill's edge.
void text_over_fill_splits_at_the_fill_edge() {
    SkiaExecutor executor;
    const std::vector<std::byte> font_bytes = read_file(GUI_DRAWING_TEST_FONT);
    CHECK(executor.register_typeface("Portsmouth Rapids", font_bytes));
    const Font font("Portsmouth Rapids", 24.0, FontStyle::regular, GraphicsUnit::pixel);
    Bitmap target(120, 40);
    SolidBrush black(Color::from_name("black"));
    SolidBrush white(Color::from_name("white"));
    GraphicsRecorder recorder;
    recorder.clear(Color::from_name("gray"));
    recorder.draw_string_over_fill("HHHHHHHH", font, black, white, {2, 4}, StringFormat(), {0, 0, 60, 40});
    CHECK(executor.execute(recorder, target));
    std::uint32_t light_left = 0U;
    std::uint32_t dark_right = 0U;
    std::uint32_t light_right = 0U;
    for (std::uint32_t y = 0; y < target.height(); ++y) {
        for (std::uint32_t x = 0; x < target.width(); ++x) {
            const std::uint8_t red_channel = target.get_pixel(x, y).red();
            const bool left = x < 60U;
            light_left += left && red_channel > 200U ? 1U : 0U;
            light_right += !left && red_channel > 200U ? 1U : 0U;
            dark_right += !left && red_channel < 60U ? 1U : 0U;
        }
    }
    CHECK(light_left > 0U);
    CHECK(dark_right > 0U);
    CHECK(light_right == 0U);
}

void connected_line_figures_fill_as_one_contour() {
    SkiaExecutor executor;
    Bitmap target(20, 20);
    SolidBrush green(Color::from_name("lime"));
    GraphicsPath path(FillMode::winding);
    path.add_line({2, 2}, {17, 2});
    path.add_line({17, 2}, {17, 17});
    path.add_line({17, 17}, {2, 17});
    path.close_figure();

    GraphicsRecorder recorder;
    recorder.fill_path(green, path);
    CHECK(executor.execute(recorder, target));
    CHECK(target.get_pixel(10, 10).green() > 240U);
    CHECK(target.get_pixel(10, 10).alpha() == 255U);
    CHECK(target.get_pixel(0, 0).alpha() == 0U);
}

void quadratic_bezier_polygon_and_pie_paths_rasterize() {
    SkiaExecutor executor;
    Bitmap target(64, 40);
    SolidBrush green(Color::from_name("lime"));
    SolidBrush blue(Color::from_name("blue"));

    GraphicsPath curved(FillMode::winding);
    curved.start_figure();
    curved.add_line({2, 20}, {2, 12});
    curved.add_quadratic({2, 12}, {10, 2}, {18, 12});
    curved.add_bezier({18, 12}, {22, 2}, {30, 2}, {34, 12});
    curved.add_line({34, 12}, {34, 28});
    curved.add_line({34, 28}, {2, 28});
    curved.close_figure();

    GraphicsPath primitives;
    const PointF triangle[] = {{40, 4}, {60, 4}, {50, 18}};
    primitives.add_polygon(triangle);
    primitives.add_pie({40, 18, 20, 20}, 180.0, 180.0);

    GraphicsRecorder recorder;
    recorder.fill_path(green, curved);
    recorder.fill_path(blue, primitives);
    CHECK(executor.execute(recorder, target));
    CHECK(target.get_pixel(16, 20).green() > 240U);
    CHECK(target.get_pixel(50, 10).blue() > 240U);
    CHECK(target.get_pixel(50, 24).blue() > 240U);
    CHECK(target.get_pixel(0, 0).alpha() == 0U);
}

// Sum of darkness on a white target: 255 - red per pixel.
std::uint64_t ink(const Bitmap& bitmap) {
    std::uint64_t total = 0;
    for (std::uint32_t y = 0; y < bitmap.height(); ++y) {
        for (std::uint32_t x = 0; x < bitmap.width(); ++x) total += 255U - bitmap.get_pixel(x, y).red();
    }
    return total;
}

std::uint64_t draw_ink(SkiaExecutor& executor, const Font& font, const Matrix& transform) {
    Bitmap target(200, 200);
    GraphicsRecorder recorder;
    recorder.clear(Color::from_name("white"));
    recorder.set_transform(transform);
    recorder.draw_string("Hamburgefonstiv", font, SolidBrush(Color::from_name("black")), {0, 0}, StringFormat());
    CHECK(executor.execute(recorder, target));
    return ink(target);
}

// ADR-004 phase 5: draw_string and measure_string use the shared HarfBuzz/
// FreeType engine and compositor, so their pixels are the same on every host.
void text_uses_the_shared_engine() {
    SkiaExecutor executor;
    const std::vector<std::byte> regular = read_file(GUI_DRAWING_TEST_FONT);
    CHECK(executor.register_typeface("Portsmouth Rapids", regular));
    const Font font("Portsmouth Rapids", 16.0, FontStyle::regular, GraphicsUnit::pixel);
    const Font bold("Portsmouth Rapids", 16.0, FontStyle::bold, GraphicsUnit::pixel);
    const Font italic("Portsmouth Rapids", 16.0, FontStyle::italic, GraphicsUnit::pixel);
    const Matrix identity;
    const Matrix shifted(1.0, 0.0, 0.0, 1.0, 20.0, 40.0);

    // Unhinted advances: the measured advance is linear in size.
    const Font large("Portsmouth Rapids", 32.0, FontStyle::regular, GraphicsUnit::pixel);
    const SizeF small_size = executor.measure_string("Hamburgefonstiv", font.snapshot(), StringFormat().snapshot());
    const SizeF large_size = executor.measure_string("Hamburgefonstiv", large.snapshot(), StringFormat().snapshot());
    const double small_advance = small_size.width - 2.0 * 16.0 / 6.0;
    const double large_advance = large_size.width - 2.0 * 32.0 / 6.0;
    CHECK(small_advance > 50.0 && std::abs(large_advance - 2.0 * small_advance) < 1e-3);
    CHECK(large_size.height > small_size.height * 1.9 && large_size.height < small_size.height * 2.1);

    // Synthesized bold thickens, italic shears; neither changes advances.
    const std::uint64_t plain_ink = draw_ink(executor, font, shifted);
    const std::uint64_t bold_ink = draw_ink(executor, bold, shifted);
    CHECK(bold_ink > plain_ink + plain_ink / 8U);
    const std::uint64_t italic_ink = draw_ink(executor, italic, shifted);
    CHECK(italic_ink * 100U > plain_ink * 94U && italic_ink * 100U < plain_ink * 106U);
    CHECK(executor.measure_string("Hamburgefonstiv", bold.snapshot(), StringFormat().snapshot()).width == small_size.width);

    // A quarter turn keeps the ink; rotated text is unhinted.
    const std::uint64_t turned_ink = draw_ink(executor, font, Matrix(0.0, 1.0, -1.0, 0.0, 120.0, 10.0));
    CHECK(turned_ink * 100U > plain_ink * 92U && turned_ink * 100U < plain_ink * 108U);

    // A registered bold face replaces synthesis.
    SkiaExecutor faced;
    CHECK(faced.register_typeface("Portsmouth Rapids", regular));
    CHECK(faced.register_typeface("Portsmouth Rapids", read_file(GUI_DRAWING_TEST_BOLD_FONT), 1U));
    CHECK(draw_ink(faced, bold, shifted) != bold_ink);
    CHECK(draw_ink(faced, font, shifted) == plain_ink);

    // One scene with every text path, hashed. The hash is the same on every
    // platform (ADR-004); it was produced on macOS arm64 and Alpine aarch64.
    Bitmap target(160, 96);
    GraphicsRecorder recorder;
    recorder.clear(Color::from_name("white"));
    recorder.draw_string("Plain 0123", font, SolidBrush(Color::from_name("black")), {2.25, 1.5}, StringFormat());
    recorder.draw_string("Bold", bold, SolidBrush(Color::from_argb(160, 0, 0, 255)), {2, 22}, StringFormat());
    recorder.draw_string("Italic", italic, SolidBrush(Color::from_name("red")), {60, 22}, StringFormat());
    StringFormat centred;
    centred.set_alignment(StringAlignment::center);
    recorder.draw_string("Two\nlines", font, SolidBrush(Color::from_name("black")), {80, 40}, centred);
    recorder.set_clip({120, 30, 20, 30});
    recorder.draw_string("Clip", large, SolidBrush(Color::from_name("green")), {110, 30}, StringFormat());
    recorder.reset_clip();
    recorder.set_quality(SmoothingMode::default_mode, InterpolationMode::default_mode, PixelOffsetMode::default_mode,
                         CompositingMode::source_copy, CompositingQuality::default_mode);
    recorder.draw_string("Copy", font, SolidBrush(Color::from_argb(128, 255, 0, 0)), {2, 76}, StringFormat());
    recorder.set_quality(SmoothingMode::default_mode, InterpolationMode::default_mode, PixelOffsetMode::default_mode,
                         CompositingMode::source_over, CompositingQuality::default_mode);
    recorder.set_transform(Matrix(0.8660254, 0.5, -0.5, 0.8660254, 100.0, 60.0));
    recorder.draw_string("Turn", font, SolidBrush(Color::from_name("black")), {0, 0}, StringFormat());
    CHECK(executor.execute(recorder, target));
    const std::uint64_t scene = checksum(target.snapshot());
    std::cout << "text scene " << std::hex << scene << std::dec << '\n';
    if (const char* path = std::getenv("GUI_DRAWING_TEXT_PNG"); path != nullptr) {
        const std::vector<std::byte> png = executor.encode_png(target);
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    if (std::getenv("GUI_DRAWING_TEXT_PRINT") == nullptr) CHECK(scene == UINT64_C(0xc63b693ed7325115));
}

// Stage F: a registered italic face draws unsheared, and an alias draws
// one family's requests with another's faces.
void italic_faces_and_aliases_draw_registered_faces() {
    const std::vector<std::byte> regular = read_file(GUI_DRAWING_TEST_FONT);
    const std::vector<std::byte> italic_face = read_file(GUI_DRAWING_TEST_ITALIC_FONT);
    const Font font("Portsmouth Rapids", 16.0, FontStyle::regular, GraphicsUnit::pixel);
    const Font italic("Portsmouth Rapids", 16.0, FontStyle::italic, GraphicsUnit::pixel);
    const Font aliased("Arial", 16.0, FontStyle::regular, GraphicsUnit::pixel);
    const Matrix shifted(1.0, 0.0, 0.0, 1.0, 20.0, 40.0);

    SkiaExecutor sheared;
    CHECK(sheared.register_typeface("Portsmouth Rapids", regular));
    SkiaExecutor faced;
    CHECK(faced.register_typeface("Portsmouth Rapids", regular));
    CHECK(faced.register_typeface("Portsmouth Rapids", italic_face, 2U));
    CHECK(draw_ink(faced, font, shifted) == draw_ink(sheared, font, shifted));
    CHECK(draw_ink(faced, italic, shifted) != draw_ink(sheared, italic, shifted));

    CHECK(!faced.alias_family("Arial", "GUI Forms Absent Family"));
    CHECK(faced.alias_family("Arial", "Portsmouth Rapids"));
    CHECK(draw_ink(faced, aliased, shifted) == draw_ink(faced, font, shifted));
    const StringFormat format;
    CHECK(faced.measure_string("Hamburgefonstiv", aliased.snapshot(), format.snapshot()).width ==
          faced.measure_string("Hamburgefonstiv", font.snapshot(), format.snapshot()).width);
}

#if defined(__APPLE__)
// ADR-004 phase 7 and stage F: the raster module asks CoreText for a family
// the substitution table does not map, with the family name mandatory, since
// Skia has no font backend. An absent family has no bytes rather than a
// CoreText substitute. Helvetica ships with every macOS and has a real bold
// face, whose advances are wider; synthesized bold keeps advances.
void installed_families_are_located_through_core_text() {
    constexpr std::size_t maximum_bytes = 64U * 1024U * 1024U;
    CHECK(!gui_drawing::render::core_text_font_bytes("GUI Forms Absent Family", false, maximum_bytes));
    const std::optional<std::vector<std::byte>> located_regular =
        gui_drawing::render::core_text_font_bytes("Helvetica", false, maximum_bytes);
    const std::optional<std::vector<std::byte>> located_bold =
        gui_drawing::render::core_text_font_bytes("Helvetica", true, maximum_bytes);
    CHECK(located_regular && located_bold);
    SkiaExecutor executor;
    CHECK(executor.register_typeface("Portsmouth Rapids", read_file(GUI_DRAWING_TEST_FONT)));
    CHECK(executor.register_typeface("Helvetica", *located_regular, 0U));
    CHECK(executor.register_typeface("Helvetica", *located_bold, 1U));
    const StringFormat format;
    const Font fallback("Portsmouth Rapids", 16.0, FontStyle::regular, GraphicsUnit::pixel);
    const Font missing("GUI Forms Absent Family", 16.0, FontStyle::regular, GraphicsUnit::pixel);
    const Font helvetica("Helvetica", 16.0, FontStyle::regular, GraphicsUnit::pixel);
    const Font helvetica_bold("Helvetica", 16.0, FontStyle::bold, GraphicsUnit::pixel);
    const double fallback_width = executor.measure_string("Hamburgefonstiv", fallback.snapshot(), format.snapshot()).width;
    const double missing_width = executor.measure_string("Hamburgefonstiv", missing.snapshot(), format.snapshot()).width;
    const double regular_width = executor.measure_string("Hamburgefonstiv", helvetica.snapshot(), format.snapshot()).width;
    const double bold_width = executor.measure_string("Hamburgefonstiv", helvetica_bold.snapshot(), format.snapshot()).width;
    CHECK(missing_width == fallback_width);
    CHECK(regular_width != fallback_width);
    CHECK(bold_width > regular_width);
    CHECK(draw_ink(executor, helvetica, Matrix(1.0, 0.0, 0.0, 1.0, 20.0, 40.0)) > 0U);
}
#endif

} // namespace

int main() {
    command_execution_and_png_round_trip();
    image_attributes_clip_and_snapshot_execution();
    texture_brush_tiles_and_mirrors_retained_pixels();
    transparent_png_channels_are_premultiplied();
    gradients_and_hatches_are_native_raster_commands();
    refused_gradient_fails_without_drawing();
    refused_dash_fails_without_drawing();
    batched_marks_brushes_grid_and_proportion_rasterize();
    text_over_fill_splits_at_the_fill_edge();
    connected_line_figures_fill_as_one_contour();
    quadratic_bezier_polygon_and_pie_paths_rasterize();
    text_uses_the_shared_engine();
    italic_faces_and_aliases_draw_registered_faces();
#if defined(__APPLE__)
    installed_families_are_located_through_core_text();
#endif
    return 0;
}
