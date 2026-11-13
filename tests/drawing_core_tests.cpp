#include "drawing_test_trace.hpp"
#include "gui_forms/drawing.hpp"

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace gui_drawing;

[[noreturn]] void fail(const char* expression, int line) {
    std::cerr << "drawing_core_tests:" << line << ": " << expression << '\n';
    std::exit(1);
}

#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

#define CHECK_THROWS(type, expression) do {                                    \
    bool drawing_expected_exception = false;                                   \
    try {                                                                       \
        static_cast<void>(expression);                                          \
    } catch (const type&) {                                                     \
        drawing_expected_exception = true;                                     \
    } catch (...) {                                                             \
        fail("unexpected exception type", __LINE__);                           \
    }                                                                           \
    if (!drawing_expected_exception) fail("expected exception", __LINE__);     \
} while (false)

class FixedMetrics final : public TextMetricsProvider {
public:
    SizeF measure(std::string_view text, const FontSnapshot& font,
                  const StringFormatSnapshot&) const override {
        return {static_cast<double>(text.size()) * font.size / 2.0,
                font.size * 1.25};
    }
};

bool color_channel_close(std::uint8_t first, std::uint8_t second) {
    return std::abs(static_cast<int>(first) - static_cast<int>(second)) <= 1;
}

class SnapshotPenOffThread final {
public:
    SnapshotPenOffThread(Pen& pen, std::atomic_bool& rejected)
        : pen_(pen), rejected_(rejected) {}

    void operator()() const {
        try {
            static_cast<void>(pen_.snapshot());
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    Pen& pen_;
    std::atomic_bool& rejected_;
};

class ReadBitmapOffThread final {
public:
    ReadBitmapOffThread(Bitmap& bitmap, std::atomic_bool& rejected)
        : bitmap_(bitmap), rejected_(rejected) {}

    void operator()() const {
        try {
            static_cast<void>(bitmap_.get_pixel(0, 0));
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    Bitmap& bitmap_;
    std::atomic_bool& rejected_;
};

void write_blue_pixel(BitmapEditView& edit, std::int32_t x, std::int32_t y) {
    std::byte* pixel = edit.writable_data +
        static_cast<std::size_t>(y) * edit.row_bytes +
        static_cast<std::size_t>(x) * 4U;
    pixel[0] = std::byte{255};
    pixel[1] = std::byte{0};
    pixel[2] = std::byte{0};
    pixel[3] = std::byte{255};
}

std::string record_reference_trace() {
    SolidBrush brush(Color::from_name("RED"));
    Pen pen(Color::from_html("#ffff00"), 2.0);
    pen.set_dash_style(DashStyle::dash);
    Font font("Lucida Grande", 12.0, FontStyle::bold);
    StringFormat format(4);
    format.set_alignment(StringAlignment::center);
    format.set_trimming(StringTrimming::ellipsis_character);

    GraphicsRecorder recorder;
    recorder.clear(Color::from_name("black"));
    const GraphicsStateToken token = recorder.save();
    recorder.translate(10.0, 5.0);
    recorder.set_clip({0.0, 0.0, 100.0, 50.0});
    recorder.set_quality(SmoothingMode::anti_alias, InterpolationMode::bicubic,
                         PixelOffsetMode::half, CompositingMode::source_over,
                         CompositingQuality::high_quality);
    recorder.fill_rectangle(brush, {1.0, 2.0, 30.0, 20.0});
    recorder.draw_rectangle(pen, {1.0, 2.0, 30.0, 20.0});
    recorder.draw_line(pen, {1.0, 2.0}, {31.0, 22.0});
    recorder.draw_string("field \xce\xa9", font, brush, {4.0, 7.0}, format);
    recorder.restore(token);
    recorder.close();
    return recorder.deterministic_trace();
}

void geometry_and_color_are_deterministic() {
    RectI bounds{10, 20, 30, 40};
    CHECK(bounds.contains(PointI{10, 20}));
    CHECK(bounds.contains(PointI{39, 59}));
    CHECK(!bounds.contains(PointI{40, 59}));
    CHECK(RectI::intersection(bounds, {35, 10, 20, 30}) ==
          (RectI{35, 20, 5, 20}));
    CHECK(RectI::united(bounds, {0, 0, 5, 5}) == (RectI{0, 0, 40, 60}));

    PointI saturating{std::numeric_limits<std::int32_t>::max(), 0};
    saturating.offset(1, -1);
    CHECK(saturating.x == std::numeric_limits<std::int32_t>::max());
    CHECK(saturating.y == -1);

    RectF floating{1.0, 2.0, 4.0, 6.0};
    floating.inflate(1.0, 2.0);
    CHECK(floating == (RectF{0.0, 0.0, 6.0, 10.0}));
    CHECK_THROWS(std::invalid_argument,
                 floating.offset(std::numeric_limits<double>::infinity(), 0.0));

    CHECK(Color::from_name("DodgerBlue").argb() == UINT32_C(0xff1e90ff));
    CHECK(Color::from_name("missing").is_empty());
    CHECK(Color::from_html("#abc").argb() == UINT32_C(0xffaabbcc));
    CHECK(Color::from_html("#80402010").alpha() == 0x80U);
    CHECK(std::abs(Color::from_rgb(0, 128, 255).brightness() - 0.5) < 0.001);
    CHECK(default_system_palette().control.argb() == UINT32_C(0xfff0f0f0));
}

void explicit_color_spaces_round_trip_and_report_gamut() {
    const LinearSrgb red = srgb_to_linear(Color::from_name("red"));
    CHECK(std::abs(red.red - 1.0) < 1e-12);
    CHECK(std::abs(red.green) < 1e-12 && std::abs(red.blue) < 1e-12);
    const XyzD65 xyz = linear_srgb_to_xyz_d65(red);
    CHECK(std::abs(xyz.x - 0.4124564) < 1e-7);
    CHECK(std::abs(xyz.y - 0.2126729) < 1e-7);
    CHECK(std::abs(xyz.z - 0.0193339) < 1e-7);

    const Oklab lab = linear_srgb_to_oklab(red);
    CHECK(std::abs(lab.lightness - 0.62795536) < 1e-7);
    CHECK(std::abs(lab.a - 0.22486306) < 1e-7);
    CHECK(std::abs(lab.b - 0.12584630) < 1e-7);
    const Oklch lch = oklab_to_oklch(lab);
    CHECK(std::abs(lch.chroma - 0.25768331) < 1e-7);
    CHECK(std::abs(lch.hue_degrees - 29.233885) < 1e-5);
    CHECK(oklch_to_srgb(lch).color.argb() == Color::from_name("red").argb());

    const Color samples[] = {
        Color::from_argb(17, 0, 0, 0),
        Color::from_argb(128, 12, 93, 201),
        Color::from_argb(255, 255, 255, 255),
        Color::from_argb(231, 44, 219, 71),
    };
    for (const Color sample : samples) {
        const LinearSrgb linear = srgb_to_linear(sample);
        const LinearSrgb xyz_round_trip =
            xyz_d65_to_linear_srgb(linear_srgb_to_xyz_d65(linear));
        const SrgbConversion xyz_result = linear_to_srgb(xyz_round_trip);
        CHECK(xyz_result.in_gamut);
        CHECK(color_channel_close(xyz_result.color.red(), sample.red()));
        CHECK(color_channel_close(xyz_result.color.green(), sample.green()));
        CHECK(color_channel_close(xyz_result.color.blue(), sample.blue()));
        CHECK(color_channel_close(xyz_result.color.alpha(), sample.alpha()));

        const SrgbConversion lab_result = linear_to_srgb(
            oklab_to_linear_srgb(linear_srgb_to_oklab(linear)));
        CHECK(lab_result.in_gamut);
        CHECK(color_channel_close(lab_result.color.red(), sample.red()));
        CHECK(color_channel_close(lab_result.color.green(), sample.green()));
        CHECK(color_channel_close(lab_result.color.blue(), sample.blue()));
    }

    const Oklch requested{0.72, 0.42, 40.0, 0.65};
    const SrgbConversion clipped = oklch_to_srgb(requested);
    CHECK(!clipped.in_gamut && clipped.clipped);
    const OklchGamutMapping mapped = map_oklch_to_srgb_gamut(requested);
    CHECK(mapped.srgb.in_gamut && !mapped.srgb.clipped);
    CHECK(mapped.mapped.chroma < requested.chroma);
    CHECK(mapped.mapped.lightness == requested.lightness);
    CHECK(mapped.mapped.hue_degrees == requested.hue_degrees);
    CHECK(mapped.mapped.alpha == requested.alpha);
    CHECK(mapped.srgb.color.alpha() == 166U);

    CHECK_THROWS(std::invalid_argument,
                 linear_to_srgb({0, 0, 0, 1.1}));
    CHECK_THROWS(std::invalid_argument,
                 oklch_to_oklab({0.5, -0.1, 20, 1}));
}

void transforms_resources_and_metrics_obey_contracts() {
    const Matrix transform = Matrix::translation(10.0, 20.0)
        .followed_by(Matrix::rotation_at(90.0, {10.0, 20.0}));
    CHECK(transform.finite());
    const PointF rotated = Matrix::rotation_at(90.0, {1.0, 1.0})
        .transform({2.0, 1.0});
    CHECK(std::abs(rotated.x - 1.0) < 1e-12);
    CHECK(std::abs(rotated.y - 2.0) < 1e-12);

    SolidBrush brush(Color::from_name("red"));
    Pen pen(brush, 1.5);
    const double custom_dash[] = {1.0, 2.0, 3.0};
    pen.set_dash_pattern(custom_dash);
    CHECK(pen.snapshot().dash_style == DashStyle::custom);
    CHECK(pen.snapshot().dash_pattern.size() == 3U);
    CHECK_THROWS(std::invalid_argument, pen.set_width(0.0));

    Font font("Lucida Grande", 12.0, FontStyle::regular);
    StringFormat format;
    GraphicsRecorder recorder;
    const SizeF measured = recorder.measure_string("abcd", font, format,
                                                   FixedMetrics{});
    CHECK(measured == (SizeF{24.0, 15.0}));
    CHECK(font.deterministic_height() == 15);

    recorder.set_clip({0.0, 0.0, 10.0, 10.0});
    CHECK(recorder.is_visible({9.99, 9.99}));
    CHECK(!recorder.is_visible({10.0, 10.0}));
    const GraphicsStateToken token = recorder.save();
    recorder.restore(token);
    CHECK_THROWS(std::invalid_argument, recorder.restore(token));

    recorder.draw_line(pen, {0.0, 0.0}, {1.0, 1.0});
    pen.set_width(7.0);
    CHECK(recorder.commands().back().pen.width == 1.5);

    brush.dispose();
    brush.dispose();
    CHECK(brush.state() == ObjectState::disposed);
    CHECK_THROWS(std::logic_error, brush.color());

    std::atomic_bool rejected{false};
    std::thread foreign(SnapshotPenOffThread(pen, rejected));
    foreign.join();
    CHECK(rejected.load());

    recorder.close();
    CHECK_THROWS(std::logic_error, recorder.clear(Color::from_name("black")));
}

void extended_vocabulary_snapshots_resources() {
    GraphicsPath path(FillMode::winding);
    path.start_figure();
    path.add_line({2.0, 1.0}, {2.0, 9.0});
    path.add_rectangle({-1.0, -2.0, 4.0, 5.0});
    path.add_ellipse({10.0, 20.0, 5.0, 5.0});
    path.add_arc({20.0, 10.0, 8.0, 6.0}, 0.0, 90.0);
    path.close_figure();
    CHECK(path.bounds() == (RectF{-1.0, -2.0, 29.0, 27.0}));
    CHECK(path.is_visible({0.0, 0.0}));
    CHECK(!path.is_visible({100.0, 100.0}));
    CHECK(path.path_points().size() == 23U);
    std::unique_ptr<GraphicsPath> copied_path = path.clone();
    (*copied_path).transform(Matrix::translation(4.0, 5.0));
    CHECK((*copied_path).bounds() == (RectF{3.0, 3.0, 29.0, 27.0}));
    GraphicsPath appended;
    appended.add_line({-10.0, -10.0}, {-5.0, -5.0});
    appended.add_path(*copied_path, true);
    CHECK(appended.path_points().size() == 25U);
    Region region(RectF{0, 0, 10, 10});
    region.exclude({2, 2, 4, 4});
    CHECK(region.is_visible({1, 1}));
    CHECK(!region.is_visible({3, 3}));
    region.unite(path);
    CHECK(region.is_visible({12, 22}));
    CHECK(region.bounds() == (RectF{-1, -2, 29, 27}));
    Region path_region(path);
    CHECK(path_region.is_visible({12, 22}));

    SolidBrush brush(Color::from_name("blue"));
    Pen pen(Color::from_name("white"), 3.0);
    ImageReference image(42, 640, 480, PixelFormat::bgra32_premultiplied, 7);
    ImageAttributes attributes;
    std::array<double, 25> matrix{};
    matrix[0] = matrix[6] = matrix[12] = matrix[18] = matrix[24] = 1.0;
    matrix[18] = 0.5;
    attributes.set_color_matrix(matrix);
    const ImageAttributesSnapshot::ColorRemap remaps[] = {
        {Color::from_name("blue"), Color::from_name("yellow")},
    };
    attributes.set_remap_table(remaps);

    GraphicsRecorder recorder;
    recorder.draw_ellipse(pen, {1, 2, 3, 4});
    recorder.fill_ellipse(brush, {1, 2, 3, 4});
    const PointF polygon[] = {{0, 0}, {10, 0}, {5, 8}};
    recorder.fill_polygon(brush, polygon, FillMode::winding);
    recorder.draw_path(pen, path);
    recorder.fill_path(brush, path);
    recorder.draw_image(image, {0, 0, 320, 240}, {0, 0, 640, 480}, attributes);
    path.reset();
    attributes.reset_color_matrix();
    CHECK(recorder.commands()[3].path.elements.size() == 6U);
    CHECK(recorder.commands()[5].image.stable_id == 42U);
    CHECK(recorder.commands()[5].image_attributes.color_matrix.has_value());
    CHECK(recorder.commands()[5].image_attributes.remap_table.size() == 1U);
    recorder.close();
    const std::string trace = recorder.deterministic_trace();
    CHECK(trace.find("3|draw_path|path=1;0;1,2,1,2,9") != std::string::npos);
    CHECK(trace.find("5|draw_image|image=42,640,480,0,7") != std::string::npos);

    CHECK_THROWS(std::invalid_argument,
                 ImageReference(0, 1, 1, PixelFormat::bgra32_premultiplied));
    std::array<double, 25> invalid = matrix;
    invalid[3] = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS(std::invalid_argument, attributes.set_color_matrix(invalid));
    const ImageAttributesSnapshot::ColorRemap duplicates[] = {
        {Color::from_name("red"), Color::from_name("white")},
        {Color::from_name("red"), Color::from_name("black")},
    };
    CHECK_THROWS(std::invalid_argument, attributes.set_remap_table(duplicates));

    HatchBrush hatch(HatchStyle::diagonal_cross, Color::from_name("white"),
                     Color::from_name("black"));
    CHECK(hatch.snapshot().kind == BrushKind::hatch);
    LinearGradientBrush linear({0, 0, 100, 20}, Color::from_name("red"),
                               Color::from_name("blue"), 15.0,
                               WrapMode::tile_flip_xy);
    const ColorBlend blend{{Color::from_name("red"), Color::from_name("white"),
                            Color::from_name("blue")},
                           {0.0, 0.5, 1.0}};
    linear.set_interpolation_colors(blend);
    CHECK(linear.snapshot().colors.size() == 3U);
    const double factors[] = {0.0, 0.25, 1.0};
    const double positions[] = {0.0, 0.75, 1.0};
    linear.set_blend(factors, positions);
    CHECK(linear.snapshot().colors[1].red() > linear.snapshot().colors[1].blue());
    const PointF gradient_shape[] = {{0, 0}, {20, 0}, {10, 20}};
    PathGradientBrush radial(gradient_shape);
    radial.set_center_color(Color::from_name("white"));
    const Color surrounds[] = {Color::from_name("black")};
    radial.set_surround_colors(surrounds);
    CHECK(radial.snapshot().points.size() == 3U);
    CHECK_THROWS(std::invalid_argument,
                 linear.set_interpolation_colors(
                     ColorBlend{{Color::from_name("red"), Color::from_name("blue")},
                                {0.1, 1.0}}));
}

bool trace_has(const std::string& trace, std::string_view entry) {
    return trace.find(entry) != std::string::npos;
}

// The batched commands: one command per call, its span copied, and a
// display-trace entry naming every field.
void batched_marks_record_one_command_each() {
    Pen pen(Color::from_name("red"), 2.0);
    pen.set_line_join(LineJoin::round, 4.0);
    pen.set_line_cap(LineCap::square);
    CHECK(pen.snapshot().join == LineJoin::round);
    CHECK(pen.snapshot().cap == LineCap::square);
    SolidBrush brush(Color::from_name("blue"));
    GraphicsRecorder recorder;
    std::array<PointF, 3> polyline{PointF{0, 0}, PointF{10, 5}, PointF{20, 0}};
    recorder.draw_lines(pen, polyline);
    // The recorder keeps its own copy.
    polyline[1] = PointF{99, 99};
    const RectF rects[] = {{0, 0, 2, 2}, {4, 4, 2, 2}};
    recorder.fill_rectangles(brush, rects);
    const PointF segments[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    recorder.draw_line_segments(pen, segments);
    MarkerStyle marker;
    marker.shape = MarkerShape::diamond;
    marker.size = 8.0;
    marker.fill = Color::from_name("white");
    marker.edge = Color::from_name("black");
    const PointF centers[] = {{5, 5}, {15, 5}};
    recorder.draw_markers(centers, marker);
    GridPatternStyle grid;
    grid.origin = PointF{0.5, 0.5};
    grid.spacing = 8.0;
    grid.major_every = 4U;
    grid.minor = Color::from_argb(0x40ffffffU);
    grid.major = Color::from_argb(0x80ffffffU);
    recorder.fill_grid_pattern({0, 0, 64, 32}, grid);
    recorder.fill_proportion(brush, {0, 0, 100, 10}, CornerRadii{5, 5, 5, 5}, 1.5, FillDirection::right_to_left);
    CHECK(recorder.commands().size() == 6U);
    CHECK(recorder.commands()[0].points[1] == (PointF{10, 5}));
    CHECK(recorder.commands()[5].scalar == 1.0);
    recorder.close();
    const std::string trace = recorder.deterministic_trace();
    CHECK(trace_has(trace, "0|draw_lines|points=0,0;10,5;20,0|pen=ffff0000,2,0,join=2/4,cap=1|"));
    CHECK(trace_has(trace, "1|fill_rectangles|rects=0,0,2,2;4,4,2,2|brush=ff0000ff|"));
    CHECK(trace_has(trace, "2|draw_line_segments|points=0,0;1,1;2,2;3,3|pen=ffff0000,2,0,join=2/4,cap=1|"));
    CHECK(trace_has(trace, "3|draw_markers|centers=5,5;15,5|marker=2,8,ffffffff,ff000000,1|"));
    CHECK(trace_has(trace, "4|fill_grid_pattern|rect=0,0,64,32|grid=0.5,0.5,8,4,40ffffff,80ffffff,1|"));
    CHECK(trace_has(trace,
                    "5|fill_proportion|track=0,0,100,10|radii=5,5,5,5|proportion=1|direction=1|brush=ff0000ff|"));
    CHECK(trace_has(trace, "end|commands=6|closed=1"));

    // A default pen traces as it did before joins and caps existed.
    GraphicsRecorder plain;
    Pen default_pen(Color::from_name("red"), 1.0);
    plain.draw_line(default_pen, {0, 0}, {1, 1});
    CHECK(trace_has(plain.deterministic_trace(), "|pen=ffff0000,1,0|"));

    // Validation leaves the recorder unchanged.
    GraphicsRecorder refusing;
    const PointF lonely[] = {{0, 0}};
    CHECK_THROWS(std::invalid_argument, refusing.draw_lines(pen, lonely));
    const PointF odd[] = {{0, 0}, {1, 1}, {2, 2}};
    CHECK_THROWS(std::invalid_argument, refusing.draw_line_segments(pen, odd));
    const PointF not_finite[] = {{0, 0}, {std::numeric_limits<double>::quiet_NaN(), 1}};
    CHECK_THROWS(std::invalid_argument, refusing.draw_lines(pen, not_finite));
    CHECK_THROWS(std::invalid_argument, refusing.fill_rectangles(brush, std::span<const RectF>{}));
    MarkerStyle empty_marker = marker;
    empty_marker.size = 0.0;
    CHECK_THROWS(std::invalid_argument, refusing.draw_markers(centers, empty_marker));
    MarkerStyle unknown_marker = marker;
    unknown_marker.shape = static_cast<MarkerShape>(10);
    CHECK_THROWS(std::invalid_argument, refusing.draw_markers(centers, unknown_marker));
    GridPatternStyle dense = grid;
    dense.spacing = 1.0;
    CHECK_THROWS(std::invalid_argument, refusing.fill_grid_pattern({0, 0, 64, 32}, dense));
    GridPatternStyle never_major = grid;
    never_major.major_every = 0U;
    CHECK_THROWS(std::invalid_argument, refusing.fill_grid_pattern({0, 0, 64, 32}, never_major));
    CHECK_THROWS(std::invalid_argument, refusing.fill_grid_pattern({0, 0, 1e9, 32}, grid));
    CHECK_THROWS(std::invalid_argument,
                 refusing.fill_proportion(brush, {0, 0, 10, 10}, CornerRadii{-1, 0, 0, 0}, 0.5,
                                          FillDirection::left_to_right));
    CHECK_THROWS(std::invalid_argument,
                 refusing.fill_proportion(brush, {0, 0, 10, 10}, CornerRadii{}, 0.5,
                                          static_cast<FillDirection>(4)));
    CHECK_THROWS(std::invalid_argument, pen.set_line_join(LineJoin::miter, 0.5));
    CHECK(refusing.commands().empty());
}

// The conic and checker brushes, the contrast rule, rounded path
// rectangles and text over a fill.
void primitive_brushes_contrast_and_text_over_fill() {
    ConicGradientBrush conic({8, 8}, 30.0, Color::from_name("red"), Color::from_name("blue"));
    CHECK(conic.snapshot().kind == BrushKind::conic_gradient);
    CHECK(conic.snapshot().colors.size() == 2U);
    conic.set_interpolation_colors(ColorBlend{{Color::from_name("red"), Color::from_name("white"),
                                               Color::from_name("blue")},
                                              {0.0, 0.5, 1.0}});
    CHECK(conic.snapshot().positions[1] == 0.5);
    CHECK_THROWS(std::invalid_argument, ConicGradientBrush({std::numeric_limits<double>::infinity(), 0}, 0.0,
                                                           Color::from_name("red"), Color::from_name("blue")));
    CheckerBrush checker(4.0, Color::from_name("white"), Color::from_name("silver"));
    CHECK(checker.snapshot().kind == BrushKind::checker);
    CHECK(checker.snapshot().cell_size == 4.0);
    CHECK_THROWS(std::invalid_argument, CheckerBrush(0.0, Color::from_name("white"), Color::from_name("silver")));
    GraphicsRecorder brushes;
    brushes.fill_rectangle(conic, {0, 0, 16, 16});
    brushes.fill_rectangle(checker, {0, 0, 16, 16});
    const std::string brush_trace = brushes.deterministic_trace();
    CHECK(trace_has(brush_trace, "0|fill_rectangle|rect=0,0,16,16|brush=5,"));
    CHECK(trace_has(brush_trace, "1|fill_rectangle|rect=0,0,16,16|brush=6,ffffffff,ffc0c0c0,"));
    CHECK(trace_has(brush_trace, ",checker=4|"));

    CHECK(std::abs(contrast_ratio(Color::from_name("white"), Color::from_name("black")) - 21.0) < 1e-9);
    CHECK(contrast_ratio(Color::from_name("red"), Color::from_name("red")) == 1.0);
    CHECK(contrasting_text_color(Color::from_name("yellow"), Color::from_name("white"), Color::from_name("black")) ==
          Color::from_name("black"));
    CHECK(contrasting_text_color(Color::from_name("navy"), Color::from_name("white"), Color::from_name("black")) ==
          Color::from_name("white"));

    GraphicsPath rounded;
    rounded.add_rounded_rectangle({0, 0, 20, 10}, CornerRadii{4, 100, 0, 2});
    CHECK(rounded.bounds() == (RectF{0, 0, 20, 10}));
    CHECK(rounded.is_visible({10, 5}));
    // The top-right radius is held to 5; its corner point lies outside.
    CHECK(!rounded.is_visible({19.9, 0.1}));
    CHECK(rounded.is_visible({19.9, 9.9}));
    CHECK_THROWS(std::invalid_argument, rounded.add_rounded_rectangle({0, 0, 20, 10}, CornerRadii{-1, 0, 0, 0}));

    Font font("Lucida Grande", 12.0, FontStyle::regular);
    StringFormat format(0);
    SolidBrush text_brush(Color::from_name("black"));
    SolidBrush on_fill(Color::from_name("white"));
    GraphicsRecorder text;
    text.draw_string_over_fill("50%", font, text_brush, on_fill, {0, 0}, format, {10, 0, 20, 10});
    CHECK(text.commands().size() == 5U);
    CHECK(text.commands()[0].kind == CommandKind::draw_string);
    CHECK(*text.commands()[0].state.clip == (RectF{10, 0, 20, 10}));
    CHECK(text.commands()[0].color == Color::from_name("white"));
    CHECK(text.commands()[1].color == Color::from_name("black"));
    // The left band ends exactly where the fill starts, the right band
    // starts exactly where it ends.
    CHECK((*text.commands()[3].state.clip).right() == 10.0);
    CHECK((*text.commands()[4].state.clip).x == 30.0);
    // A fill outside the clip leaves one draw in the plain colour.
    GraphicsRecorder clipped;
    clipped.set_clip({0, 0, 5, 5});
    clipped.draw_string_over_fill("50%", font, text_brush, on_fill, {0, 0}, format, {10, 10, 5, 5});
    CHECK(clipped.commands().size() == 2U);
    CHECK(clipped.commands()[1].color == Color::from_name("black"));
    CHECK(*clipped.commands()[1].state.clip == (RectF{0, 0, 5, 5}));
    // A fill covering the whole clip leaves one draw in the fill colour.
    GraphicsRecorder covered;
    covered.set_clip({0, 0, 5, 5});
    covered.draw_string_over_fill("50%", font, text_brush, on_fill, {0, 0}, format, {-10, -10, 50, 50});
    CHECK(covered.commands().size() == 2U);
    CHECK(covered.commands()[1].color == Color::from_name("white"));
    GraphicsRecorder rotated;
    rotated.set_transform(Matrix::rotation_at(30.0, {0, 0}));
    CHECK_THROWS(std::invalid_argument,
                 rotated.draw_string_over_fill("50%", font, text_brush, on_fill, {0, 0}, format, {0, 0, 5, 5}));
    CHECK(rotated.commands().size() == 1U);
}

void curve_polygon_and_pie_paths_are_retained_and_bounded() {
    GraphicsPath path;
    path.set_fill_mode(FillMode::winding);
    path.start_figure();
    path.add_quadratic({0, 10}, {5, 0}, {10, 10});
    path.add_bezier({10, 10}, {12, 20}, {18, 20}, {20, 10});
    const PointF continuation[] = {
        {20, 10}, {22, 0}, {28, 0}, {30, 10},
        {30, 10}, {32, 20}, {38, 20}, {40, 10},
    };
    // Two independent calls make the canonical 4 + 3n rule explicit.
    path.add_beziers(std::span<const PointF>(continuation, 4U));
    const PointF second_curve[] = {
        {30, 10}, {32, 20}, {38, 20}, {40, 10},
    };
    path.add_beziers(second_curve);
    path.close_figure();
    const std::size_t before_invalid = path.snapshot().elements.size();
    const PointF invalid[] = {{0, 0}, {1, 1}, {2, 2}};
    CHECK_THROWS(std::invalid_argument, path.add_beziers(invalid));
    CHECK(path.snapshot().elements.size() == before_invalid);
    CHECK(path.fill_mode() == FillMode::winding);
    CHECK(path.bounds() == (RectF{0, 0, 40, 20}));

    GraphicsPath polygon;
    const PointF triangle[] = {{2, 2}, {18, 2}, {10, 18}};
    polygon.add_polygon(triangle);
    CHECK(polygon.is_visible({10, 8}));
    CHECK(!polygon.is_visible({1, 1}));
    CHECK_THROWS(std::invalid_argument,
                 polygon.add_polygon(std::span<const PointF>(triangle, 2U)));

    GraphicsPath pie;
    pie.add_pie({0, 0, 20, 20}, 0.0, 90.0);
    CHECK(pie.is_visible({13, 13}));
    CHECK(!pie.is_visible({3, 3}));
    std::unique_ptr<GraphicsPath> transformed = pie.clone();
    (*transformed).transform(Matrix::translation(5, 7));
    CHECK((*transformed).bounds() == (RectF{5, 7, 20, 20}));
}

void bitmap_storage_and_leases_are_generation_safe() {
    Bitmap bitmap(2, 2);
    CHECK(bitmap.width() == 2U && bitmap.height() == 2U);
    CHECK(bitmap.generation() == 1U);
    bitmap.set_pixel(0, 0, Color::from_argb(128, 255, 0, 0));
    CHECK(bitmap.generation() == 2U);
    CHECK(bitmap.get_pixel(0, 0) == Color::from_argb(128, 255, 0, 0));
    CHECK_THROWS(std::out_of_range, bitmap.get_pixel(2, 0));

    const ImageSnapshot retained = bitmap.snapshot();
    CHECK(retained.has_pixels() && retained.row_bytes() == 8U);
    CHECK(retained.pixels().size() == 16U);
    CHECK(std::to_integer<std::uint8_t>(retained.pixels()[2]) == 128U);
    bitmap.set_pixel(0, 0, Color::from_name("blue"));
    CHECK(bitmap.generation() == 3U);
    CHECK(std::to_integer<std::uint8_t>(retained.pixels()[2]) == 128U);
    CHECK(bitmap.get_pixel(0, 0).argb() == Color::from_name("blue").argb());

    const BitmapLockView read = bitmap.lock(BitmapLockMode::read);
    CHECK(read.data != nullptr && read.writable_data == nullptr);
    CHECK_THROWS(std::logic_error, bitmap.snapshot());
    bitmap.unlock(read.token);
    CHECK_THROWS(std::invalid_argument, bitmap.unlock(read.token));

    const BitmapLockView write = bitmap.lock(BitmapLockMode::write);
    CHECK(write.writable_data != nullptr);
    write.writable_data[0] = std::byte{0};
    write.writable_data[1] = std::byte{255};
    write.writable_data[2] = std::byte{0};
    write.writable_data[3] = std::byte{255};
    bitmap.unlock(write.token);
    CHECK(bitmap.generation() == 4U);
    CHECK(bitmap.get_pixel(0, 0).argb() == Color::from_name("lime").argb());

    bitmap.set_pixel(1, 0, Color::from_name("red"));
    bitmap.set_pixel(0, 1, Color::from_name("blue"));
    bitmap.set_pixel(1, 1, Color::from_name("white"));
    std::unique_ptr<Bitmap> clone = bitmap.clone({1, 0, 1, 2});
    CHECK((*clone).width() == 1U && (*clone).height() == 2U);
    CHECK((*clone).get_pixel(0, 0).argb() == Color::from_name("red").argb());
    (*clone).make_transparent(Color::from_name("red"));
    CHECK((*clone).get_pixel(0, 0).alpha() == 0U);

    std::unique_ptr<Bitmap> thumbnail = bitmap.thumbnail(1, 1);
    CHECK((*thumbnail).get_pixel(0, 0).argb() ==
          Color::from_name("lime").argb());

    ImageAttributes attributes;
    std::array<double, 25> matrix{};
    matrix[0] = matrix[6] = matrix[12] = matrix[18] = matrix[24] = 1.0;
    matrix[18] = 0.5;
    std::unique_ptr<Bitmap> adjusted = bitmap.adjusted(attributes);
    CHECK((*adjusted).get_pixel(0, 1).argb() ==
          Color::from_name("blue").argb());
    attributes.set_color_matrix(matrix);
    const ImageAttributesSnapshot::ColorRemap remap[] = {
        {Color::from_name("blue"), Color::from_name("red")},
    };
    attributes.set_remap_table(remap);
    adjusted = bitmap.adjusted(attributes);
    CHECK((*adjusted).get_pixel(0, 1).alpha() == 128U);
    CHECK((*adjusted).get_pixel(0, 1).red() == 255U);

    GraphicsRecorder recorder;
    recorder.draw_image(bitmap, {0, 0, 2, 2}, {0, 0, 2, 2}, attributes);
    const std::uint64_t submitted_generation =
        recorder.commands().front().image.generation;
    bitmap.set_pixel(0, 0, Color::from_name("black"));
    CHECK(recorder.commands().front().image.generation == submitted_generation);
    const ImageSnapshot current = bitmap.snapshot();
    CHECK(recorder.commands().front().image.pixels().data() != current.pixels().data());

    TextureBrush texture(bitmap, WrapMode::tile_flip_x);
    texture.translate_transform(3.0, 4.0);
    texture.scale_transform(2.0, 1.5);
    const BrushSnapshot texture_snapshot = texture.snapshot();
    CHECK(texture_snapshot.kind == BrushKind::texture);
    CHECK(texture_snapshot.image.has_pixels());
    CHECK(texture_snapshot.wrap_mode == WrapMode::tile_flip_x);
    const PointF transformed_origin =
        texture_snapshot.transform.transform({0.0, 0.0});
    CHECK(transformed_origin.x == 6.0 && transformed_origin.y == 6.0);
    std::unique_ptr<TextureBrush> copied_texture = texture.clone();
    texture.reset_transform();
    CHECK((*copied_texture).snapshot().transform == texture_snapshot.transform);
    GraphicsRecorder texture_recorder;
    texture_recorder.fill_rectangle(*copied_texture, {0.0, 0.0, 12.0, 8.0});
    CHECK(texture_recorder.commands().front().brush.kind == BrushKind::texture);
    CHECK(texture_recorder.deterministic_trace().find("image=") !=
          std::string::npos);
    CHECK_THROWS(std::invalid_argument, texture.scale_transform(0.0, 1.0));

    CHECK_THROWS(std::invalid_argument, Bitmap(0, 1));
    CHECK_THROWS(std::length_error, Bitmap(32768, 32768));

    std::atomic_bool rejected{false};
    std::thread foreign(ReadBitmapOffThread(bitmap, rejected));
    foreign.join();
    CHECK(rejected.load());
}

void bounded_bitmap_edits_publish_local_damage_and_cancel_atomically() {
    Bitmap bitmap(8, 6);
    CHECK(bitmap.changes_since(1).empty());

    const ImageSnapshot before = bitmap.snapshot();
    BitmapEditView edit = bitmap.begin_edit({2, 1, 4, 3});
    CHECK(edit.data == edit.writable_data);
    CHECK(edit.bounds == (RectI{2, 1, 4, 3}));
    CHECK(edit.row_bytes == 32U);
    CHECK_THROWS(std::logic_error, bitmap.snapshot());
    CHECK_THROWS(std::logic_error, bitmap.unlock(edit.token));

    write_blue_pixel(edit, 0, 0);
    write_blue_pixel(edit, 1, 0);
    write_blue_pixel(edit, 0, 1);
    write_blue_pixel(edit, 1, 1);
    write_blue_pixel(edit, 3, 2);
    CHECK(bitmap.commit_edit(edit.token) == 2U);
    CHECK(bitmap.get_pixel(2, 1).argb() == Color::from_name("blue").argb());
    CHECK(before.pixels()[static_cast<std::size_t>(1) * before.row_bytes() +
                          static_cast<std::size_t>(2) * 4U + 3U] == std::byte{0});

    BitmapDamageSnapshot damage = bitmap.changes_since(1);
    CHECK(damage.history_complete);
    CHECK(damage.from_generation == 1U && damage.to_generation == 2U);
    CHECK(damage.rectangles.size() == 2U);
    CHECK(damage.rectangles[0] == (RectI{2, 1, 2, 2}));
    CHECK(damage.rectangles[1] == (RectI{5, 3, 1, 1}));
    CHECK(bitmap.changes_since(2).empty());

    edit = bitmap.begin_edit({2, 1, 1, 1});
    edit.writable_data[0] = std::byte{0};
    bitmap.cancel_edit(edit.token);
    CHECK(bitmap.generation() == 2U);
    CHECK(bitmap.get_pixel(2, 1).argb() == Color::from_name("blue").argb());
    CHECK_THROWS(std::invalid_argument, bitmap.cancel_edit(edit.token));

    edit = bitmap.begin_edit({0, 0, 1, 1});
    CHECK(bitmap.commit_edit(edit.token) == 2U);
    CHECK(bitmap.changes_since(2).empty());
    CHECK_THROWS(std::invalid_argument, bitmap.begin_edit({-1, 0, 1, 1}));
    CHECK_THROWS(std::invalid_argument, bitmap.begin_edit({7, 5, 2, 1}));
    CHECK_THROWS(std::invalid_argument, bitmap.changes_since(3));

    Bitmap history(1, 1);
    for (std::size_t index = 0; index < Bitmap::maximum_damage_history + 1U;
         ++index) {
        history.set_pixel(0, 0, index % 2U == 0U
            ? Color::from_name("red") : Color::from_name("blue"));
    }
    damage = history.changes_since(1);
    CHECK(!damage.history_complete);
    CHECK((damage.rectangles == std::vector<RectI>{{0, 0, 1, 1}}));
}

} // namespace

int main() {
    geometry_and_color_are_deterministic();
    explicit_color_spaces_round_trip_and_report_gamut();
    transforms_resources_and_metrics_obey_contracts();
    extended_vocabulary_snapshots_resources();
    curve_polygon_and_pie_paths_are_retained_and_bounded();
    batched_marks_record_one_command_each();
    primitive_brushes_contrast_and_text_over_fill();
    bitmap_storage_and_leases_are_generation_safe();
    bounded_bitmap_edits_publish_local_damage_and_cancel_atomically();
    const std::string trace = record_reference_trace();
    CHECK(trace == gui_drawing_expected_trace);
    CHECK(record_reference_trace() == trace);
    return 0;
}
