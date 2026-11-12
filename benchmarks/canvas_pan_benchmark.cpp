// Canvas pan workload: the cost of one pan step of a painting canvas, as the
// CPU Skia raster meets it. Measurement only; it claims nothing by itself.
//
// It reproduces the canvas Plan Paint pans (forms_native --pan-probe): a
// 1280 x 820 window holding a 1280 x 647 canvas that paints, every frame,
//
// - a felt surround: a 128-pixel image tiled at 128 logical units
//   (fill_image_pattern), sampled up at device scale 2;
// - a light vertical gradient over the whole canvas;
// - the sheet's box shadow (blur 6, offset 1.5, 2.5) and outline;
// - the sheet itself: an opaque live surface of the visible part of the
//   sheet at device resolution, republished every step, as an application
//   converting its viewport does.
//
// Each step moves the sheet by 8 logical units and repaints the damage the
// window reports into the raster. The live surface is written before the
// timed paint, so the times are GUI.Forms's share only. Two sheets:
// "fit" lies inside the canvas (a zoom-to-fit view), "large" is a
// 3000 x 2000 sheet at zoom 1, most of it outside the canvas.
//
// The "copy" cases paint the surround so that it follows the sheet (the felt
// tiled from the sheet's corner, the gradient spanning the sheet and a
// margin) and pan by Control::scroll_content: the raster moves its pixels
// (SkiaRaster::scroll_copy, timed with the paint) and the window paints only
// the exposed band.
//
// Per case it prints the paint time per step (mean, median and worst over
// the measured steps), the damage area and the window's counters, then each
// canvas operation timed alone over the canvas.
//
// Usage: gui_forms_canvas_pan_benchmark [steps]

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/live_surface/types/live_surface_types.hpp"
#include "gui_forms/live_surface/write_lease/live_surface_write_lease.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/live_surface.hpp"
#include "skia_raster.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace gui_forms::render {
// The covers the raster promised (SkiaRaster::opaque_live_surface_cover),
// to show whether occluded replay hid the surround.
struct SkiaCanvasTestAccess final {
    static std::uint64_t covers_promised(const SkiaRaster& raster) { return raster.covers_promised_; }
};
} // namespace gui_forms::render

namespace {

using namespace gui_forms;
using gui_forms::render::SkiaRaster;
using Clock = std::chrono::steady_clock;

constexpr Size window_size{1280.0, 820.0};
constexpr Rect canvas_bounds{0.0, 120.0, 1280.0, 647.0};
constexpr double pan_step = 8.0;
constexpr std::uint32_t felt_pixels = 128U;
constexpr double felt_tile = 128.0;
constexpr std::size_t warmup_steps = 4U;

// The sheet's colour, one opaque BGRA value.
enum class CanvasOperation : std::uint8_t { felt, gradient, shadow, stroke, surface };

constexpr std::array<std::byte, 4> sheet_pixel{std::byte{200}, std::byte{170}, std::byte{120}, std::byte{255}};

class PanCanvas final : public Control {
public:
    explicit PanCanvas(StableId id) : Control(std::move(id)) {}

    ImageId felt{};
    // The sheet's top-left corner and size, in canvas coordinates.
    Point sheet_origin{};
    Size sheet_size{};
    // The visible part of the sheet at device resolution, as the
    // application's viewport conversion leaves it.
    std::shared_ptr<LiveSurface> viewport;
    Rect viewport_destination{};

    Rect sheet() const noexcept {
        const Rect result{sheet_origin.x, sheet_origin.y, sheet_size.width, sheet_size.height};
        return result;
    }

    // One operation the attribution runs leave out (time_omissions), or
    // none.
    std::optional<CanvasOperation> omitted;
    // Whether the surround follows the sheet, so that a pan is a copy.
    bool follows_sheet{};

    void paint_following_surround(Painter& painter, Rect bounds);

    void on_paint(Painter& painter, Rect) override {
        const Rect bounds = client_rectangle();
        if (follows_sheet) {
            paint_following_surround(painter, bounds);
        } else {
            if (omitted != CanvasOperation::felt) {
                painter.fill_image_pattern(felt,
                                           {static_cast<double>(felt_pixels), static_cast<double>(felt_pixels)},
                                           bounds, {felt_tile, felt_tile});
            }
            const GradientStop light[] = {{0.0, Color::rgba(255, 255, 255, 22)},
                                          {1.0, Color::rgba(60, 77, 95, 15)}};
            if (omitted != CanvasOperation::gradient) {
                painter.fill_linear_gradient(bounds, {0.0, 0.0}, {0.0, bounds.height}, light);
            }
        }
        const Rect page = sheet();
        if (omitted != CanvasOperation::shadow) {
            painter.draw_box_shadow(page, 0.0, {1.5, 2.5}, 6.0, 0.0, Color::rgba(42, 53, 67, 75));
        }
        if (omitted != CanvasOperation::stroke) {
            painter.stroke_rect({page.x - 0.5, page.y - 0.5, page.width + 1.0, page.height + 1.0},
                                Color::rgba(93, 111, 130, 155), 1.0);
        }
        if (viewport && omitted != CanvasOperation::surface) {
            painter.draw_live_surface(viewport, viewport_destination);
        }
    }
};

// The felt tiled from the sheet's corner and the gradient over the sheet's
// rows and a margin, both drawn under a clip to the canvas: a pan moves
// them with the sheet. The felt is opaque, so the canvas reports its cover.
void PanCanvas::paint_following_surround(Painter& painter, Rect bounds) {
    constexpr double gradient_margin = 300.0;
    const double left = sheet_origin.x - felt_tile * std::ceil((sheet_origin.x - bounds.x) / felt_tile);
    const double top = sheet_origin.y - felt_tile * std::ceil((sheet_origin.y - bounds.y) / felt_tile);
    const Rect tiled{left, top, bounds.right() - left, bounds.bottom() - top};
    const GradientStop light[] = {{0.0, Color::rgba(255, 255, 255, 22)}, {1.0, Color::rgba(60, 77, 95, 15)}};
    painter.save();
    painter.clip_rect(bounds);
    if (omitted != CanvasOperation::felt) {
        painter.fill_image_pattern(felt, {static_cast<double>(felt_pixels), static_cast<double>(felt_pixels)},
                                   tiled, {felt_tile, felt_tile});
    }
    if (omitted != CanvasOperation::gradient) {
        const Point start{0.0, sheet_origin.y - gradient_margin};
        const Point end{0.0, sheet_origin.y + sheet_size.height + gradient_margin};
        painter.fill_linear_gradient(bounds, start, end, light);
    }
    painter.restore();
    if (omitted != CanvasOperation::felt) report_opaque_cover(painter, bounds);
}

// Writes the visible part of the sheet, at device resolution, into the
// canvas's viewport surface (made or remade at that size).
void publish_viewport(PanCanvas& canvas, double scale) {
    const Rect visible = Rect::intersection(canvas.sheet(), {0.0, 0.0, canvas_bounds.width, canvas_bounds.height});
    if (visible.empty()) {
        canvas.viewport.reset();
        return;
    }
    const double left = std::ceil((canvas_bounds.x + visible.x) * scale);
    const double top = std::ceil((canvas_bounds.y + visible.y) * scale);
    const std::uint32_t width =
        static_cast<std::uint32_t>(std::floor((canvas_bounds.x + visible.right()) * scale) - left);
    const std::uint32_t height =
        static_cast<std::uint32_t>(std::floor((canvas_bounds.y + visible.bottom()) * scale) - top);
    canvas.viewport_destination = {left / scale - canvas_bounds.x, top / scale - canvas_bounds.y,
                                   width / scale, height / scale};
    const bool resized = !canvas.viewport || (*canvas.viewport).snapshot().description.width != width ||
                         (*canvas.viewport).snapshot().description.height != height;
    if (resized) {
        LiveSurfaceDescription description{};
        description.width = width;
        description.height = height;
        description.pixel_format = native_live_surface_pixel_format();
        description.opaque = true;
        canvas.viewport = LiveSurface::create(description);
    }
    LiveSurfaceWriteLease write = (*canvas.viewport).try_acquire_write();
    if (!write) throw std::runtime_error("viewport write refused");
    for (std::uint32_t y = 0U; y < height; ++y) {
        std::byte* row = write.pixels().data() + static_cast<std::size_t>(y) * write.row_bytes();
        for (std::uint32_t x = 0U; x < width; ++x) {
            std::memcpy(row + static_cast<std::size_t>(x) * 4U, sheet_pixel.data(), 4U);
        }
    }
    static_cast<void>(write.publish());
}

struct CaseResult final {
    double mean_ms{};
    double median_ms{};
    double worst_ms{};
    double damage_area{};
    double controls_painted{};
    double commands_replayed{};
    double chunks_rebuilt{};
    double covers{};
};

double milliseconds_since(Clock::time_point start) {
    const std::chrono::duration<double, std::milli> elapsed = Clock::now() - start;
    return elapsed.count();
}

void paint_damage(SkiaRaster& raster, Window& window, const DamageRegion& damage) {
    if (!raster.synchronize_images(window.image_resources())) throw std::runtime_error("image sync failed");
    raster.begin_frame(damage);
    static_cast<void>(window.paint(raster, damage.rectangles()));
    raster.end_frame();
}

CaseResult run_case(SkiaRaster& raster, Window& window, PanCanvas& canvas, double scale, Size sheet,
                    std::size_t steps) {
    window.set_scale(scale);
    window.perform_layout();
    // False when the size and scale are unchanged; the surface is then kept.
    static_cast<void>(raster.resize(window_size, scale));
    if (raster.byte_size() == 0U) throw std::runtime_error("raster resize failed");
    canvas.sheet_size = sheet;
    const bool fits = sheet.width <= canvas_bounds.width && sheet.height <= canvas_bounds.height;
    canvas.sheet_origin = fits ? Point{(canvas_bounds.width - sheet.width) * 0.5,
                                       (canvas_bounds.height - sheet.height) * 0.5}
                               : Point{-200.0, -300.0};
    publish_viewport(canvas, scale);
    canvas.invalidate(Dirty::paint);
    window.perform_layout();
    DamageRegion full;
    full.add({0.0, 0.0, window_size.width, window_size.height});
    static_cast<void>(window.take_damage());
    paint_damage(raster, window, full);

    std::vector<double> samples;
    samples.reserve(steps);
    std::vector<ScrollCopy> copies;
    window.take_scroll_copies(copies);
    CaseResult result{};
    MetricsSnapshot before{};
    std::uint64_t covers_before = 0U;
    // Pans right then left, so a fitted sheet stays inside the canvas.
    for (std::size_t step = 0U; step < warmup_steps + steps; ++step) {
        if (step == warmup_steps) {
            before = window.metrics_snapshot();
            covers_before = render::SkiaCanvasTestAccess::covers_promised(raster);
        }
        const double direction = ((step / 16U) % 2U) == 0U ? 1.0 : -1.0;
        canvas.sheet_origin.x += direction * pan_step;
        publish_viewport(canvas, scale);
        if (canvas.follows_sheet) {
            static_cast<void>(canvas.scroll_content(canvas.client_rectangle(), {direction * pan_step, 0.0}));
        } else {
            canvas.invalidate(Dirty::paint);
        }
        window.perform_layout();
        const DamageRegion damage = window.take_damage();
        copies.clear();
        window.take_scroll_copies(copies);
        const Clock::time_point start = Clock::now();
        for (const ScrollCopy& copy : copies) {
            if (!raster.scroll_copy(copy.clip, copy.offset)) throw std::runtime_error("scroll copy refused");
        }
        paint_damage(raster, window, damage);
        const double elapsed = milliseconds_since(start);
        if (step < warmup_steps) continue;
        samples.push_back(elapsed);
        result.damage_area += damage.area();
    }
    const MetricsSnapshot after = window.metrics_snapshot();
    const double count = static_cast<double>(steps);
    for (const double sample : samples) {
        result.mean_ms += sample;
        result.worst_ms = std::max(result.worst_ms, sample);
    }
    result.mean_ms /= count;
    std::sort(samples.begin(), samples.end());
    result.median_ms = samples[samples.size() / 2U];
    result.damage_area /= count;
    result.controls_painted = static_cast<double>(after.controls_painted - before.controls_painted) / count;
    result.commands_replayed =
        static_cast<double>(after.display_commands_replayed - before.display_commands_replayed) / count;
    result.chunks_rebuilt = static_cast<double>(after.display_chunks_rebuilt - before.display_chunks_rebuilt) / count;
    result.covers =
        static_cast<double>(render::SkiaCanvasTestAccess::covers_promised(raster) - covers_before) / count;
    return result;
}

// One canvas operation, drawn once under each of `clips` (canvas
// coordinates), `repeats` times; milliseconds per repeat.
double time_operation(SkiaRaster& raster, const PanCanvas& canvas, CanvasOperation operation,
                      std::span<const Rect> clips) {
    constexpr int repeats = 10;
    const Rect bounds{0.0, 0.0, canvas_bounds.width, canvas_bounds.height};
    const GradientStop light[] = {{0.0, Color::rgba(255, 255, 255, 22)}, {1.0, Color::rgba(60, 77, 95, 15)}};
    const Clock::time_point mark = Clock::now();
    for (int repeat = 0; repeat < repeats; ++repeat) {
        for (const Rect clip : clips) {
            raster.save();
            raster.clip_rect(clip);
            switch (operation) {
            case CanvasOperation::felt:
                raster.fill_image_pattern(canvas.felt,
                                          {static_cast<double>(felt_pixels), static_cast<double>(felt_pixels)},
                                          bounds, {felt_tile, felt_tile}, ImagePatternWrap::tile, 1.0);
                break;
            case CanvasOperation::gradient:
                raster.fill_linear_gradient(bounds, {0.0, 0.0}, {0.0, bounds.height}, light);
                break;
            case CanvasOperation::shadow:
                raster.draw_box_shadow(canvas.sheet(), 0.0, {1.5, 2.5}, 6.0, 0.0, Color::rgba(42, 53, 67, 75));
                break;
            case CanvasOperation::stroke:
                break;
            case CanvasOperation::surface:
                if (canvas.viewport) raster.draw_live_surface(canvas.viewport, canvas.viewport_destination, 1.0);
                break;
            }
            raster.restore();
        }
    }
    return milliseconds_since(mark) / repeats;
}

// Each canvas operation timed over the whole canvas, then over the four
// bands around the sheet that occluded replay leaves showing.
void time_operations(SkiaRaster& raster, const PanCanvas& canvas) {
    DamageRegion full;
    full.add({0.0, 0.0, window_size.width, window_size.height});
    raster.begin_frame(full);
    raster.save();
    raster.translate({canvas_bounds.x, canvas_bounds.y});
    const Rect bounds{0.0, 0.0, canvas_bounds.width, canvas_bounds.height};
    const Rect hidden = Rect::intersection(canvas.viewport_destination, bounds);
    const std::array<Rect, 1> whole{bounds};
    const std::array<Rect, 4> bands{{
        {bounds.x, bounds.y, bounds.width, hidden.y - bounds.y},
        {bounds.x, hidden.bottom(), bounds.width, bounds.bottom() - hidden.bottom()},
        {bounds.x, hidden.y, hidden.x - bounds.x, hidden.height},
        {hidden.right(), hidden.y, bounds.right() - hidden.right(), hidden.height},
    }};
    constexpr std::array<CanvasOperation, 4> operations{CanvasOperation::felt, CanvasOperation::gradient,
                                                        CanvasOperation::shadow, CanvasOperation::surface};
    std::array<double, 4> alone{};
    std::array<double, 4> banded{};
    for (std::size_t index = 0U; index < operations.size(); ++index) {
        alone[index] = time_operation(raster, canvas, operations[index], whole);
        if (!hidden.empty()) banded[index] = time_operation(raster, canvas, operations[index], bands);
    }
    raster.restore();
    raster.end_frame();
    std::printf("    alone: felt %.3f ms, gradient %.3f ms, shadow %.3f ms, live surface %.3f ms\n", alone[0],
                alone[1], alone[2], alone[3]);
    std::printf("    in the bands around the sheet: felt %.3f ms, gradient %.3f ms, shadow %.3f ms\n", banded[0],
                banded[1], banded[2]);
}

// The fitted sheet's pan at scale 2 with each canvas operation left out in
// turn: what each one costs within a whole frame.
void time_omissions(SkiaRaster& raster, Window& window, PanCanvas& canvas, Size sheet, std::size_t steps) {
    constexpr std::array<CanvasOperation, 5> operations{CanvasOperation::felt, CanvasOperation::gradient,
                                                        CanvasOperation::shadow, CanvasOperation::stroke,
                                                        CanvasOperation::surface};
    constexpr std::array<const char*, 5> names{"felt", "gradient", "shadow", "outline", "live surface"};
    std::printf("  fit sheet at scale 2, median paint without one operation:");
    for (std::size_t index = 0U; index < operations.size(); ++index) {
        canvas.omitted = operations[index];
        const CaseResult result = run_case(raster, window, canvas, 2.0, sheet, steps);
        std::printf(" %s %.3f ms%s", names[index], result.median_ms, index + 1U < operations.size() ? "," : "\n");
    }
    canvas.omitted.reset();
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::size_t steps = 60U;
        if (argc > 1) steps = std::max<std::size_t>(1U, std::strtoull(argv[1], nullptr, 10));
        const Control::Ptr root = make_control<Control>(StableId("pan.root"));
        (*root).set_requested_bounds({0.0, 0.0, window_size.width, window_size.height});
        const std::shared_ptr<PanCanvas> canvas = make_control<PanCanvas>(StableId("pan.canvas"));
        (*canvas).set_requested_bounds(canvas_bounds);
        (*root).add_child(canvas);
        Window window(root, window_size);
        window.set_scroll_copies_supported(true);

        std::vector<std::byte> felt(static_cast<std::size_t>(felt_pixels) * felt_pixels * 4U);
        for (std::uint32_t y = 0U; y < felt_pixels; ++y) {
            for (std::uint32_t x = 0U; x < felt_pixels; ++x) {
                const std::size_t at = (static_cast<std::size_t>(y) * felt_pixels + x) * 4U;
                const unsigned grain = (x * 37U + y * 101U + ((x ^ y) * 13U)) % 24U;
                felt[at] = static_cast<std::byte>(200U + grain);
                felt[at + 1U] = static_cast<std::byte>(196U + grain);
                felt[at + 2U] = static_cast<std::byte>(186U + grain);
                felt[at + 3U] = std::byte{255};
            }
        }
        const ImageLoadResult loaded = window.load_bgra32_premultiplied(felt_pixels, felt_pixels, felt_pixels * 4U, felt);
        if (!loaded) throw std::runtime_error("felt image refused");
        (*canvas).felt = loaded.image;

        SkiaRaster raster;
        constexpr std::array<double, 2> scales{1.0, 2.0};
        const std::array<Size, 2> sheets{Size{1232.0, 600.0}, Size{3000.0, 2000.0}};
        const std::array<const char*, 2> sheet_names{"fit", "large"};
        std::printf("canvas pan: window %.0f x %.0f, canvas %.0f x %.0f, %zu steps of %.0f units\n",
                    window_size.width, window_size.height, canvas_bounds.width, canvas_bounds.height, steps, pan_step);
        constexpr std::array<bool, 2> follows{false, true};
        for (const bool following : follows) {
            (*canvas).follows_sheet = following;
            for (std::size_t sheet = 0U; sheet < sheets.size(); ++sheet) {
                for (const double scale : scales) {
                    const CaseResult result = run_case(raster, window, *canvas, scale, sheets[sheet], steps);
                    std::printf("  %s%s sheet %.0f x %.0f, scale %.0f: paint %.3f ms mean, %.3f median, %.3f worst; "
                                "damage %.0f; controls %.1f, commands %.1f, chunks rebuilt %.1f, covers %.1f per step\n",
                                sheet_names[sheet], following ? " copy" : "", sheets[sheet].width,
                                sheets[sheet].height, scale, result.mean_ms,
                                result.median_ms, result.worst_ms, result.damage_area, result.controls_painted,
                                result.commands_replayed, result.chunks_rebuilt, result.covers);
                    if (!following) time_operations(raster, *canvas);
                }
            }
        }
        (*canvas).follows_sheet = false;
        time_omissions(raster, window, *canvas, sheets[0], steps);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "canvas pan benchmark failed: %s\n", error.what());
        return 1;
    }
}
