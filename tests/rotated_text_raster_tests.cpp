// Text at any angle (D5 ruling of 2026-11-13; Painter::draw_rotated_text_utf8
// and draw_rotated_glyph_runs) through the Skia raster with the bundled
// faces, at scales 1 and 2:
// - pixels: at 0 degrees the run draws draw_text_utf8's bytes; at 30, 90,
//   135, 200 and -45 degrees its ink lies inside rotated_text_bounds of its
//   measured extent and runs from the anchor along the turned baseline,
//   above it; at 90 degrees its ink box is the quarter-turned run's
//   (draw_turned_text_utf8); runs the raster shaped draw the same bytes
//   through draw_rotated_glyph_runs as their text does, and foreign runs
//   fall back to their text; a non-finite angle draws nothing;
// - damage: a control turning a label centred in its bounds, its angle
//   stepped through the same angles, repaints only the damage its window
//   reports (recorded and replayed through the window's display list), and
//   what it presents equals a fresh window's full paint, byte for byte, with
//   the damage inside the control.
// - placement: rotated_text_anchor centres the turned box on its centre,
//   and rotated_text_bounds of a quarter turn is the box on its side.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/typography/shaping_workspace/shaping_workspace.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "skia_raster.hpp"
#include "../src/core/resources/fonts/bundled_font_set.hpp"
#if defined(_WIN32)
#include "../src/host/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../src/host/posix/fonts/sealed_pages_posix.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

constexpr std::array<double, 2> scales{1.0, 2.0};
constexpr std::array<double, 5> turned_angles{30.0, 90.0, 135.0, 200.0, -45.0};
constexpr std::string_view sample = "Axis title 42";
constexpr Size canvas_size{240.0, 240.0};
constexpr Point canvas_anchor{120.0, 120.0};
constexpr double pi = 3.14159265358979323846;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_rotated_text_raster_tests: " << message << '\n';
    std::exit(1);
}

FontSpec sample_font() {
    const FontSpec font{FontRole::control, 14.0, 400, false, 0.0};
    return font;
}

std::shared_ptr<detail::BundledFontSet> bundled_fonts() {
#if defined(_WIN32)
    const detail::SealedPageFunctions pages = host::windows_sealed_pages();
#else
    const detail::SealedPageFunctions pages = host::posix_sealed_pages();
#endif
    std::shared_ptr<detail::BundledFontSet> set = detail::shared_bundled_font_set(GUI_FORMS_TEST_FONT_DIRECTORY, pages);
    return set;
}

// The raster's pixels, copied row by row (BGRA, premultiplied).
std::vector<std::uint8_t> copy_pixels(const render::SkiaRaster& raster) {
    const std::size_t width_bytes = static_cast<std::size_t>(raster.pixel_width()) * 4U;
    std::vector<std::uint8_t> bytes(width_bytes * raster.pixel_height());
    const std::uint8_t* const source = static_cast<const std::uint8_t*>(raster.pixels());
    for (std::uint32_t row = 0U; row < raster.pixel_height(); ++row) {
        const std::uint8_t* const from = source + static_cast<std::size_t>(row) * raster.row_bytes();
        std::copy(from, from + width_bytes, bytes.begin() + static_cast<std::ptrdiff_t>(row * width_bytes));
    }
    return bytes;
}

// What one draw left on a white canvas: the dark pixels (blue below 128),
// in logical units.
struct Ink final {
    std::size_t count{};
    Rect bounds{};
    Point centroid{};
};

Ink ink_of(const std::vector<std::uint8_t>& bytes, std::uint32_t pixel_width, std::uint32_t pixel_height, double scale) {
    Ink ink{};
    double left = std::numeric_limits<double>::max();
    double top = std::numeric_limits<double>::max();
    double right = -std::numeric_limits<double>::max();
    double bottom = -std::numeric_limits<double>::max();
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (std::uint32_t y = 0U; y < pixel_height; ++y) {
        for (std::uint32_t x = 0U; x < pixel_width; ++x) {
            const std::uint8_t blue = bytes[(static_cast<std::size_t>(y) * pixel_width + x) * 4U];
            if (blue >= 128U) continue;
            ink.count = ink.count + 1U;
            const double logical_x = (static_cast<double>(x) + 0.5) / scale;
            const double logical_y = (static_cast<double>(y) + 0.5) / scale;
            left = std::min(left, static_cast<double>(x) / scale);
            top = std::min(top, static_cast<double>(y) / scale);
            right = std::max(right, static_cast<double>(x + 1U) / scale);
            bottom = std::max(bottom, static_cast<double>(y + 1U) / scale);
            sum_x += logical_x;
            sum_y += logical_y;
        }
    }
    if (ink.count == 0U) return ink;
    ink.bounds = Rect{left, top, right - left, bottom - top};
    ink.centroid = Point{sum_x / static_cast<double>(ink.count), sum_y / static_cast<double>(ink.count)};
    return ink;
}

// The kinds of draw the pixel checks compare.
enum class Draw : std::uint8_t { text, rotated, turned_up, rotated_runs };

struct DrawRequest final {
    Draw kind{Draw::rotated};
    double degrees{};
    const GlyphRunsView* runs{};
};

std::vector<std::uint8_t> draw_once(render::SkiaRaster& raster, const DrawRequest& request) {
    DamageRegion whole{};
    whole.add(Rect{0.0, 0.0, canvas_size.width, canvas_size.height});
    raster.begin_frame(whole);
    raster.fill_rect(Rect{0.0, 0.0, canvas_size.width, canvas_size.height}, Color::rgba(255, 255, 255));
    const Color black = Color::rgba(0, 0, 0);
    switch (request.kind) {
    case Draw::text:
        raster.draw_text_utf8(canvas_anchor, sample, sample_font(), black);
        break;
    case Draw::turned_up:
        raster.draw_turned_text_utf8(canvas_anchor, sample, sample_font(), black, TextOrientation::bottom_to_top);
        break;
    case Draw::rotated_runs:
        raster.draw_rotated_glyph_runs(canvas_anchor, *request.runs, sample_font(), black, request.degrees);
        break;
    case Draw::rotated:
    default:
        raster.draw_rotated_text_utf8(canvas_anchor, sample, sample_font(), black, request.degrees);
        break;
    }
    raster.end_frame();
    std::vector<std::uint8_t> bytes = copy_pixels(raster);
    return bytes;
}

// The sample's glyph runs as the raster's engine shaped them.
struct ShapedSample final {
    std::vector<GlyphRunRecord> runs{};
    std::vector<PositionedGlyph> glyphs{};
    GlyphRunsView view{};
};

ShapedSample shape_sample(render::SkiaRaster& raster) {
    ShapedSample shaped{};
    const std::unique_ptr<ShapingWorkspace> workspace = raster.create_shaping_workspace();
    require(workspace != nullptr, "the raster makes a shaping workspace");
    (*workspace).reserve(sample.size());
    static_cast<void>(raster.shape_in_workspace_utf8(*workspace, sample, sample_font()));
    const ShapedGlyphCounts counts = (*workspace).shaped_counts();
    require(counts.runs > 0U && counts.glyphs > 0U, "the sample is shaped");
    shaped.runs.resize(counts.runs);
    shaped.glyphs.resize(counts.glyphs);
    require((*workspace).copy_glyphs(Utf8Offset{}, 0U, shaped.runs, shaped.glyphs), "the runs are copied");
    shaped.view.source = (*workspace).glyph_source();
    shaped.view.runs = shaped.runs;
    shaped.view.glyphs = shaped.glyphs;
    shaped.view.text = sample;
    return shaped;
}

void test_placement() {
    const RotatedTextBox box{80.0, 12.0, 4.0, 90.0};
    const Rect upright = rotated_text_bounds(Point{100.0, 100.0}, box);
    // A quarter turn anticlockwise: the run reads up from the anchor, the
    // ascent to its left and the descent to its right.
    require(std::fabs(upright.x - 88.0) < 1e-9 && std::fabs(upright.y - 20.0) < 1e-9, "the turned box's corner");
    require(std::fabs(upright.width - 16.0) < 1e-9 && std::fabs(upright.height - 80.0) < 1e-9,
            "a quarter turn puts the box on its side");
    for (const double degrees : turned_angles) {
        const RotatedTextBox turned{80.0, 12.0, 4.0, degrees};
        const Point centre{50.0, 60.0};
        const Point anchor = rotated_text_anchor(centre, turned);
        const Rect bounds = rotated_text_bounds(anchor, turned);
        require(std::fabs(bounds.x + bounds.width * 0.5 - centre.x) < 1e-9 &&
                    std::fabs(bounds.y + bounds.height * 0.5 - centre.y) < 1e-9,
                "the anchor centres the turned box");
    }
    const RotatedTextBox broken{80.0, 12.0, 4.0, std::numeric_limits<double>::quiet_NaN()};
    require(rotated_text_bounds(Point{3.0, 4.0}, broken).empty(), "a non-finite turn bounds nothing");
}

void test_pixels(double scale) {
    render::SkiaRaster raster{};
    require(raster.resize(canvas_size, scale), "the raster is allocated");
    require(raster.register_bundled_fonts(bundled_fonts()), "the bundled faces are registered");
    const FontSpec font = sample_font();
    const TextExtent extent = raster.measure_text_extent_utf8(sample, font);
    require(extent.logical_size.width > 40.0, "the sample is measured");

    const std::vector<std::uint8_t> level = draw_once(raster, DrawRequest{Draw::text, 0.0, nullptr});
    const std::vector<std::uint8_t> zero = draw_once(raster, DrawRequest{Draw::rotated, 0.0, nullptr});
    require(zero == level, "at 0 degrees a rotated run draws draw_text_utf8's bytes");
    const std::vector<std::uint8_t> nothing =
        draw_once(raster, DrawRequest{Draw::rotated, std::numeric_limits<double>::infinity(), nullptr});
    require(ink_of(nothing, raster.pixel_width(), raster.pixel_height(), scale).count == 0U,
            "a non-finite angle draws nothing");

    const ShapedSample shaped = shape_sample(raster);
    // Generous line box: the ascent and descent the raster reports, or the
    // font size when it reports none, and a pixel of antialiasing.
    const double ascent = std::max(extent.ascent, font.size);
    const double descent = std::max(extent.descent, font.size * 0.4);
    const double slack = 1.5;
    for (const double degrees : turned_angles) {
        const std::vector<std::uint8_t> bytes = draw_once(raster, DrawRequest{Draw::rotated, degrees, nullptr});
        const Ink ink = ink_of(bytes, raster.pixel_width(), raster.pixel_height(), scale);
        require(ink.count >= 40U, "a turned run draws ink");
        const RotatedTextBox box{extent.logical_size.width, ascent, descent, degrees};
        const Rect bounds = rotated_text_bounds(canvas_anchor, box);
        const bool inside = ink.bounds.x >= bounds.x - slack && ink.bounds.y >= bounds.y - slack &&
                            ink.bounds.x + ink.bounds.width <= bounds.x + bounds.width + slack &&
                            ink.bounds.y + ink.bounds.height <= bounds.y + bounds.height + slack;
        require(inside, "a turned run's ink lies inside rotated_text_bounds");
        // The ink's middle, in run coordinates: along the turned baseline
        // about half the advance from the anchor, and above the baseline.
        const double radians = degrees * (pi / 180.0);
        const double dx = ink.centroid.x - canvas_anchor.x;
        const double dy = ink.centroid.y - canvas_anchor.y;
        const double along = dx * std::cos(radians) - dy * std::sin(radians);
        const double across = dx * std::sin(radians) + dy * std::cos(radians);
        require(std::fabs(along - extent.logical_size.width * 0.5) < extent.logical_size.width * 0.2,
                "a turned run reads along its turned baseline");
        require(across < 0.0 && across > -font.size, "a turned run's glyphs stand above its baseline");

        const std::vector<std::uint8_t> through_runs =
            draw_once(raster, DrawRequest{Draw::rotated_runs, degrees, &shaped.view});
        require(through_runs == bytes, "the raster's own runs turn to the same bytes as their text");
        GlyphRunsView foreign = shaped.view;
        foreign.source = GlyphSource{};
        const std::vector<std::uint8_t> fallback =
            draw_once(raster, DrawRequest{Draw::rotated_runs, degrees, &foreign});
        require(fallback == bytes, "foreign runs turn as their text");
    }

    const Ink quarter =
        ink_of(draw_once(raster, DrawRequest{Draw::rotated, 90.0, nullptr}), raster.pixel_width(), raster.pixel_height(), scale);
    const Ink turned =
        ink_of(draw_once(raster, DrawRequest{Draw::turned_up, 0.0, nullptr}), raster.pixel_width(), raster.pixel_height(), scale);
    const double tolerance = 2.0;
    require(std::fabs(quarter.bounds.x - turned.bounds.x) <= tolerance &&
                std::fabs(quarter.bounds.y - turned.bounds.y) <= tolerance &&
                std::fabs(quarter.bounds.width - turned.bounds.width) <= tolerance &&
                std::fabs(quarter.bounds.height - turned.bounds.height) <= tolerance,
            "90 degrees inks where the quarter-turned run does");
}

// A label centred in the control and turned about its middle; the whole
// turned box stays inside the bounds, so the control's damage covers it.
class TurningLabel final : public Control {
public:
    explicit TurningLabel(StableId stable_id) : Control(std::move(stable_id)) {}

    void turn_to(double degrees) {
        degrees_ = degrees;
        invalidate(Dirty::paint);
    }

    void on_paint(Painter& painter, Rect local_damage) override {
        static_cast<void>(local_damage);
        const Rect bounds = client_rectangle();
        painter.fill_rect(bounds, Color::rgba(250, 250, 246));
        const FontSpec font = sample_font();
        const TextExtent extent = painter.measure_text_extent_utf8(sample, font);
        const RotatedTextBox box{extent.logical_size.width, std::max(extent.ascent, font.size),
                                 std::max(extent.descent, font.size * 0.4), degrees_};
        const Point centre{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
        const Point anchor = rotated_text_anchor(centre, box);
        painter.draw_rotated_text_utf8(anchor, sample, font, Color::rgba(20, 30, 60), degrees_);
    }

private:
    double degrees_{};
};

struct Scene final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<TurningLabel> label;
    std::unique_ptr<Window> window;
};

constexpr Rect label_place{40.0, 30.0, 170.0, 170.0};

std::unique_ptr<Scene> make_scene(double scale, double degrees, render::SkiaRaster& raster) {
    std::unique_ptr<Scene> held = std::make_unique<Scene>();
    Scene& scene = *held;
    scene.root = make_control<Panel>(StableId("root"));
    (*scene.root).set_background(Color::rgba(225, 231, 237, 255));
    scene.label = make_control<TurningLabel>(StableId("label"));
    (*scene.label).set_requested_bounds(label_place);
    (*scene.label).turn_to(degrees);
    (*scene.root).add_child(scene.label);
    scene.window = std::make_unique<Window>(scene.root, canvas_size);
    (*scene.window).set_scale(scale);
    (*scene.window).set_text_metrics_provider(&raster);
    (*scene.window).perform_layout();
    return held;
}

DamageRegion paint_damage(Window& window, render::SkiaRaster& raster) {
    DamageRegion damage = window.take_damage();
    if (damage.empty()) return damage;
    raster.begin_frame(damage);
    raster.save();
    raster.clip_damage(damage);
    const stxlib::Optional<PaintReceipt> receipt = window.paint(raster, damage.rectangles());
    raster.restore();
    raster.end_frame();
    if (receipt.present()) static_cast<void>(window.notify_presented(*receipt.value()));
    return damage;
}

void test_damage(double scale) {
    render::SkiaRaster raster{};
    require(raster.resize(canvas_size, scale), "the raster is allocated");
    require(raster.register_bundled_fonts(bundled_fonts()), "the bundled faces are registered");
    const std::unique_ptr<Scene> scene = make_scene(scale, 0.0, raster);
    Window& window = *(*scene).window;
    static_cast<void>(paint_damage(window, raster));
    for (const double degrees : turned_angles) {
        (*(*scene).label).turn_to(degrees);
        window.perform_layout();
        const DamageRegion damage = paint_damage(window, raster);
        require(!damage.empty(), "turning the label damages it");
        const Rect reach = damage.bounds();
        const double pixel = 1.0 / scale;
        require(reach.x >= label_place.x - pixel && reach.y >= label_place.y - pixel &&
                    reach.x + reach.width <= label_place.x + label_place.width + pixel &&
                    reach.y + reach.height <= label_place.y + label_place.height + pixel,
                "the damage stays inside the turning control");
        render::SkiaRaster reference{};
        require(reference.resize(canvas_size, scale), "the reference raster is allocated");
        require(reference.register_bundled_fonts(bundled_fonts()), "the reference faces are registered");
        const std::unique_ptr<Scene> fresh = make_scene(scale, degrees, reference);
        static_cast<void>(paint_damage(*(*fresh).window, reference));
        require(copy_pixels(raster) == copy_pixels(reference),
                "repainting the damage presents a full paint's bytes at every angle");
        (*(*fresh).window).set_text_metrics_provider(nullptr);
    }
    window.set_text_metrics_provider(nullptr);
}

} // namespace

int main() {
    test_placement();
    for (const double scale : scales) {
        test_pixels(scale);
        test_damage(scale);
    }
    std::cout << "gui_forms_rotated_text_raster_tests passed\n";
    return 0;
}
