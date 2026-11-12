#import <AppKit/AppKit.h>

#include "gui_forms/platform/macos_host.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

// Stage DP gate on macOS: two distant damaged rectangles paint their sum, not
// their bounding box, and no pixel outside them changes on the screen. The
// screen is read twice: through the operating system's screen capture
// (/usr/sbin/screencapture, what the display shows) and through AppKit's
// cached display of the view (the host's raster). AppKit coalesces the
// host's per-rectangle display requests, so what it copies to the screen is
// reported as measured, not assumed. The record is
// notes:results/2026-10-25-dp-gate-macos.
//
// Then becoming visible again: after a change made while hidden, the first
// visible frame repaints that change only, and with nothing changed it
// repaints nothing (AppKit's whole-view request is a copy of the raster).
// After a resize while hidden the reveal repaints the whole view. Last, the
// window is miniaturized and restored for real, with and without a change
// while hidden, and the screen after the restore must equal the screen
// before it apart from the changed block: a restored window must never come
// back blank or white (the draw that copies the raster must always copy).
// A restore repaints in full, because losing and regaining key status
// changes the window's activation, which dirties the tree. So last, the
// window is occluded for real without losing key status, under a borderless
// non-activating panel, with and without a change while covered: uncovered,
// it must show what it showed before, AppKit must draw it, and with nothing
// changed it must paint nothing (the copy-only draw).

// Existing private native diagnostics and occlusion hooks, not a public API addition.
@interface NSView (DamageRectanglesProbe)
- (std::string)hostJSON;
- (void)collectDamage;
- (void)notifyOcclusion:(BOOL)occluded;
@end

namespace {
using namespace gui_forms;

constexpr const char* window_title = "GUI.Forms damage rectangles check";

void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

class ColorBlock final : public Control {
public:
    ColorBlock(StableId id, const Color color) : Control(std::move(id)), color_(color) {}
    void set_color(const Color color) {
        color_ = color;
        invalidate(Dirty::paint);
    }
protected:
    void on_paint(Painter& painter, const gui_forms::Rect) override {
        painter.fill_rect(client_rectangle(), color_);
    }
private:
    Color color_{};
};

// A read-back image: `width` x `height` device pixels, four sRGB bytes each,
// top row first.
struct Capture final {
    NSInteger width{0};
    NSInteger height{0};
    std::vector<std::uint8_t> rgba{};
};

enum class Stage {
    first_frame, drain, settle, changed, shown, hidden, revealed, bare_reveal,
    resize_hidden, resized, minimize, minimized, restored, cover, covered, uncovered
};

struct State final {
    std::shared_ptr<ColorBlock> root{};
    std::shared_ptr<ColorBlock> top_left{};
    std::shared_ptr<ColorBlock> bottom_right{};
    std::array<gui_forms::Rect, 2> blocks{};
    gui_forms::Window* model{}; // Borrowed only while the host run is alive.
    std::function<void()> close{};
    Stage stage{Stage::first_frame};
    std::chrono::steady_clock::time_point deadline{};
    std::uint32_t settle_steps{0};
    std::uint64_t previous_presented{0};
    MetricsSnapshot before_metrics{};
    MetricsSnapshot after_metrics{};
    // What AppKit handed drawRect: for the measured frame (host diagnostics).
    double native_dirty_area{0.0};
    double cg_clip_bounds_area{0.0};
    Capture raster_before{};
    Capture screen_before{};
    double scale{1.0};
    // The miniaturize passes: the second one changes a block while hidden.
    bool change_while_minimized{false};
    double draws_before{0.0};
    Capture screen_before_minimize{};
    // The real-occlusion passes: the second one changes a block while covered.
    bool change_while_covered{false};
    bool key_before_cover{false};
    NSPanel* cover{nil}; // Owned (ARC) while it covers the window.
    std::chrono::steady_clock::time_point cover_deadline{};
    Capture screen_before_cover{};
    bool finished{false};
    bool host_closed{false};
    std::string failure{};
};
struct DeferredStep final {
    std::shared_ptr<State> state{};
};

NSWindow* find_window() {
    NSWindow* result = nil;
    NSString* const title = [NSString stringWithUTF8String:window_title];
    NSArray<NSWindow*>* const windows = NSApp.windows;
    const NSUInteger count = windows.count;
    for (NSUInteger index = 0; index < count; ++index) {
        NSWindow* const candidate = [windows objectAtIndex:index];
        if ([candidate.title isEqualToString:title]) {
            result = candidate;
            break;
        }
    }
    return result;
}
void close_fixture(State& state) {
    // closed can clear the stored target synchronously. This local copy owns
    // the callable until this invocation has returned.
    const std::function<void()> request_close = state.close;
    if (request_close) request_close();
}
const char* stage_name(const Stage stage) {
    switch (stage) {
    case Stage::first_frame: return "first_frame";
    case Stage::drain: return "drain";
    case Stage::settle: return "settle";
    case Stage::changed: return "changed";
    case Stage::shown: return "shown";
    case Stage::hidden: return "hidden";
    case Stage::revealed: return "revealed";
    case Stage::bare_reveal: return "bare_reveal";
    case Stage::resize_hidden: return "resize_hidden";
    case Stage::resized: return "resized";
    case Stage::minimize: return "minimize";
    case Stage::minimized: return "minimized";
    case Stage::restored: return "restored";
    case Stage::cover: return "cover";
    case Stage::covered: return "covered";
    case Stage::uncovered: return "uncovered";
    }
    return "unknown";
}
// Each stage prints when it starts, so a timeout names the stage it stopped in.
void set_stage(State& state, const Stage stage) {
    state.stage = stage;
    state.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::uint64_t presented = 0;
    if (state.model != nullptr) presented = (*state.model).paint_lease_snapshot().presented_revision;
    std::cout << "dp-gate-stage|" << stage_name(stage) << "|presented_revision=" << presented << std::endl;
}
void run_step(void* const context);
void enqueue(const std::shared_ptr<State>& state) {
    std::unique_ptr<DeferredStep> owner = std::make_unique<DeferredStep>();
    (*owner).state = state;
    const std::int64_t delay_nanoseconds = 50'000'000;
    const dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, delay_nanoseconds);
    void* const context = owner.release();
    dispatch_after_f(when, dispatch_get_main_queue(), context, run_step);
}

// The bitmap's pixels as sRGB bytes. Both read-backs go through this one
// conversion, so a before and after pair compares like with like.
Capture capture_from_bitmap(NSBitmapImageRep* const bitmap) {
    Capture result{};
    result.width = bitmap.pixelsWide;
    result.height = bitmap.pixelsHigh;
    require(result.width > 0 && result.height > 0 && result.width <= 4096 && result.height <= 4096,
        "bounded read-back extent");
    const std::size_t count = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height);
    result.rgba.resize(count * 4U);
    NSColorSpace* const color_space = NSColorSpace.sRGBColorSpace;
    for (NSInteger row = 0; row < result.height; ++row) {
        const std::size_t offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(result.width) * 4U;
        for (NSInteger column = 0; column < result.width; ++column) {
            NSColor* const color = [[bitmap colorAtX:column y:row] colorUsingColorSpace:color_space];
            require(color != nil, "read-back color conversion");
            const std::array<double, 4> components{static_cast<double>(color.redComponent),
                static_cast<double>(color.greenComponent), static_cast<double>(color.blueComponent),
                static_cast<double>(color.alphaComponent)};
            const std::size_t pixel = offset + static_cast<std::size_t>(column) * 4U;
            for (std::size_t channel = 0; channel < components.size(); ++channel) {
                require(std::isfinite(components[channel]), "finite read-back color");
                const double bounded = std::clamp(components[channel], 0.0, 1.0);
                const long value = std::lround(bounded * 255.0);
                result.rgba[pixel + channel] = static_cast<std::uint8_t>(value);
            }
        }
    }
    return result;
}

// Reads the host's raster while the model is occluded, so the cached display
// copies the last frame and paints nothing (counters unchanged). Unocclusion
// afterwards asks for one whole-window frame (notifyOcclusion:).
Capture capture_raster(State& state, NSView* const view) {
    [view notifyOcclusion:YES];
    require((*state.model).occluded(), "occlusion hook did not reach the model");
    const MetricsSnapshot before = (*state.model).metrics_snapshot();
    Capture result{};
    @autoreleasepool {
        NSBitmapImageRep* const bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        require(bitmap != nil, "AppKit bitmap allocation");
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
        result = capture_from_bitmap(bitmap);
    }
    const MetricsSnapshot after = (*state.model).metrics_snapshot();
    require(after.paint_passes == before.paint_passes, "the occluded readback painted");
    [view notifyOcclusion:NO];
    return result;
}

// Reads the view's area of the display through /usr/sbin/screencapture. The
// child is waited for with waitpid, not a run loop, so no main-queue step
// runs while it works.
Capture capture_screen(NSWindow* const window, NSView* const view, const char* const name) {
    const NSRect in_window = [view convertRect:view.bounds toView:nil];
    const NSRect on_screen = [window convertRectToScreen:in_window];
    NSArray<NSScreen*>* const screens = NSScreen.screens;
    require(screens.count > 0, "no screen");
    // screencapture -R takes points from the main display's top-left corner;
    // AppKit's screen coordinates start at its bottom-left corner.
    const double main_height = static_cast<double>([screens objectAtIndex:0].frame.size.height);
    const long left = std::lround(on_screen.origin.x);
    const long top = std::lround(main_height - (on_screen.origin.y + on_screen.size.height));
    const long width = std::lround(on_screen.size.width);
    const long height = std::lround(on_screen.size.height);
    const std::string region = std::to_string(left) + "," + std::to_string(top) + "," +
        std::to_string(width) + "," + std::to_string(height);
    const std::string path = std::string(NSTemporaryDirectory().UTF8String) + "gui_forms_dp_" +
        std::to_string(static_cast<long>(getpid())) + "_" + name + ".png";
    std::array<std::string, 7> arguments{"/usr/sbin/screencapture", "-x", "-R", region, "-t", "png", path};
    std::array<char*, 8> argv{};
    for (std::size_t index = 0; index < arguments.size(); ++index) argv[index] = arguments[index].data();
    argv[arguments.size()] = nullptr;
    pid_t child = 0;
    const int spawned = posix_spawn(&child, argv[0], nullptr, nullptr, argv.data(), environ);
    require(spawned == 0, "screencapture did not start");
    int status = 0;
    const pid_t waited = waitpid(child, &status, 0);
    require(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "screencapture failed");
    std::cout << "dp-gate-capture|" << name << "|region=" << region << std::endl;
    Capture result{};
    @autoreleasepool {
        NSData* const data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
        require(data != nil, "screencapture wrote no image");
        NSBitmapImageRep* const bitmap = [NSBitmapImageRep imageRepWithData:data];
        require(bitmap != nil, "screencapture image does not decode");
        result = capture_from_bitmap(bitmap);
    }
    std::remove(path.c_str());
    return result;
}

// Pixels that differ between two read-backs, counted by whether the pixel's
// centre (in logical units; `scale` device pixels per unit) lies in a block.
struct ChangedPixels final {
    std::size_t inside{0};
    std::size_t outside{0};
};
bool centre_inside(const NSInteger column, const NSInteger row, const double scale,
                   const std::span<const gui_forms::Rect> blocks) {
    const gui_forms::Point centre{(static_cast<double>(column) + 0.5) / scale,
                                  (static_cast<double>(row) + 0.5) / scale};
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].contains(centre)) return true;
    }
    return false;
}
ChangedPixels changed_pixels(const Capture& before, const Capture& after, const double scale,
                             const std::span<const gui_forms::Rect> blocks) {
    require(before.width == after.width && before.height == after.height, "read-backs differ in extent");
    ChangedPixels changed{};
    for (NSInteger row = 0; row < before.height; ++row) {
        for (NSInteger column = 0; column < before.width; ++column) {
            const std::size_t pixel =
                (static_cast<std::size_t>(row) * static_cast<std::size_t>(before.width) +
                 static_cast<std::size_t>(column)) * 4U;
            bool same = true;
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                if (before.rgba[pixel + channel] != after.rgba[pixel + channel]) same = false;
            }
            if (same) continue;
            if (centre_inside(column, row, scale, blocks)) {
                ++changed.inside;
            } else {
                ++changed.outside;
            }
        }
    }
    return changed;
}

// The red channel at logical point (x, y) of a read-back.
std::uint8_t red_at(const Capture& capture, const double scale, const double x, const double y) {
    const std::size_t column = static_cast<std::size_t>(x * scale);
    const std::size_t row = static_cast<std::size_t>(y * scale);
    require(column < static_cast<std::size_t>(capture.width) && row < static_cast<std::size_t>(capture.height),
        "sample outside the read-back");
    const std::size_t pixel = (row * static_cast<std::size_t>(capture.width) + column) * 4U;
    const std::uint8_t red = capture.rgba[pixel];
    return red;
}

// One numeric field of the host's private diagnostics JSON.
double host_number(NSView* const view, const std::string& name) {
    const std::string snapshot = [view hostJSON];
    const std::string marker = "\"" + name + "\":";
    const std::size_t position = snapshot.find(marker);
    require(position != std::string::npos, "native diagnostic missing");
    const std::string digits = snapshot.substr(position + marker.size());
    const double value = std::stod(digits);
    return value;
}

void measure(State& state, NSWindow* const window, NSView* const view) {
    const Capture screen_after = capture_screen(window, view, "after");
    const Capture raster_after = capture_raster(state, view);
    const MetricsSnapshot& before = state.before_metrics;
    const MetricsSnapshot& after = state.after_metrics;
    const double sum = state.blocks[0].area() + state.blocks[1].area();
    const double painted = after.painted_damage_area - before.painted_damage_area;
    const double bounding = after.paint_bounding_area - before.paint_bounding_area;
    const std::uint64_t presents = after.frames_presented - before.frames_presented;
    const std::uint64_t rectangles = after.retained_rectangles_presented - before.retained_rectangles_presented;
    const std::uint64_t pixels = after.retained_pixels_presented - before.retained_pixels_presented;
    const double device_pixels_per_unit = state.scale * state.scale;
    const std::uint64_t expected_pixels = static_cast<std::uint64_t>(std::llround(sum * device_pixels_per_unit));
    const ChangedPixels raster = changed_pixels(state.raster_before, raster_after, state.scale, state.blocks);
    const ChangedPixels screen = changed_pixels(state.screen_before, screen_after, state.scale, state.blocks);
    const bool layer_backed = view.layer != nil;
    const long redraw_policy = static_cast<long>(view.layerContentsRedrawPolicy);
    std::cout << "dp-gate|host=macos|scale=" << state.scale << "|sum_area=" << sum << "|painted_area=" << painted
              << "|bounding_area=" << bounding << "|presents=" << presents << "|rectangles_presented=" << rectangles
              << "|pixels_presented=" << pixels << "|expected_pixels=" << expected_pixels
              << "|native_dirty_area=" << state.native_dirty_area
              << "|cg_clip_bounds_area=" << state.cg_clip_bounds_area
              << "|raster_changed_inside=" << raster.inside << "|raster_changed_outside=" << raster.outside
              << "|screen_changed_inside=" << screen.inside << "|screen_changed_outside=" << screen.outside
              << "|wants_layer=" << (view.wantsLayer ? 1 : 0) << "|layer_backed=" << (layer_backed ? 1 : 0)
              << "|layer_contents_redraw_policy=" << redraw_policy << '\n';
    require(presents == 1U, "two distant changes did not present one frame");
    const double difference = painted - sum;
    require(difference < 1.0e-6 && difference > -1.0e-6, "painted area differs from the rectangles' sum");
    require(bounding > painted * 10.0, "the rectangles are not distant: the test cannot tell a sum from a box");
    require(rectangles >= 1U && pixels >= expected_pixels, "the host copied less than the two rectangles");
    require(raster.outside == 0U && screen.outside == 0U, "a pixel outside the damaged rectangles changed");
    require(raster.inside == static_cast<std::size_t>(expected_pixels) &&
            screen.inside == static_cast<std::size_t>(expected_pixels),
            "the damaged rectangles did not change in the raster and on the screen");
}

void advance(State& state) {
    NSWindow* const window = find_window();
    if (state.stage == Stage::first_frame) {
        if (window == nil || !window.isVisible || window.contentView == nil) return;
        const PaintLeaseSnapshot lease = (*state.model).paint_lease_snapshot();
        if (lease.presented_revision == 0) return;
        state.scale = static_cast<double>(window.backingScaleFactor);
        require(std::isfinite(state.scale) && state.scale >= 0.5 && state.scale <= 4.0, "backing scale supported");
        [window orderFrontRegardless];
        state.raster_before = capture_raster(state, window.contentView);
        // Unocclusion asks for one whole-window frame (the same pixels); let
        // it present, so the measured frame holds only the two changes.
        // A repaint of unchanged content keeps the presented revision, so a
        // model invalidation gives the drain a revision to wait for.
        state.previous_presented = (*state.model).paint_lease_snapshot().presented_revision;
        (*state.root).invalidate(Dirty::paint);
        [window.contentView collectDamage];
        set_stage(state, Stage::drain);
        return;
    }
    require(window != nil && window.contentView != nil, "native fixture window disappeared");
    NSView* const view = window.contentView;
    require(static_cast<double>(window.backingScaleFactor) == state.scale, "backing scale changed during the fixture");
    if (state.stage == Stage::drain) {
        const PaintLeaseSnapshot drained = (*state.model).paint_lease_snapshot();
        if (drained.presented_revision <= state.previous_presented) return;
        state.settle_steps = 0;
        set_stage(state, Stage::settle);
        return;
    }
    if (state.stage == Stage::settle) {
        // Let the window server show the drained frame before the baseline.
        ++state.settle_steps;
        if (state.settle_steps < 4U) return;
        state.screen_before = capture_screen(window, view, "before");
        const double root_red = static_cast<double>(red_at(state.screen_before, state.scale, 200.0, 150.0));
        const double block_red = static_cast<double>(red_at(state.screen_before, state.scale, 30.0, 25.0));
        require(root_red - block_red > 60.0,
            "the screen capture does not show the window (no Screen Recording permission, or covered)");
        const PaintLeaseSnapshot lease = (*state.model).paint_lease_snapshot();
        state.previous_presented = lease.presented_revision;
        state.before_metrics = (*state.model).metrics_snapshot();
        (*state.top_left).set_color(Color::rgba(200, 40, 40));
        (*state.bottom_right).set_color(Color::rgba(40, 40, 200));
        [view collectDamage];
        set_stage(state, Stage::changed);
        return;
    }
    if (state.stage == Stage::changed) {
        const PaintLeaseSnapshot lease = (*state.model).paint_lease_snapshot();
        if (lease.presented_revision <= state.previous_presented) return;
        // Taken at once: the host's diagnostics describe its last drawRect:.
        state.after_metrics = (*state.model).metrics_snapshot();
        state.native_dirty_area = host_number(view, "last_native_dirty_area");
        state.cg_clip_bounds_area = host_number(view, "last_cg_clip_bounds_area");
        state.settle_steps = 0;
        set_stage(state, Stage::shown);
        return;
    }
    if (state.stage == Stage::shown) {
        // Let the window server show the measured frame before reading the screen.
        ++state.settle_steps;
        if (state.settle_steps < 4U) return;
        measure(state, window, view);
        set_stage(state, Stage::hidden);
        return;
    }
    if (state.stage == Stage::hidden) {
        // One block changes while the window is hidden.
        [view notifyOcclusion:YES];
        state.before_metrics = (*state.model).metrics_snapshot();
        (*state.top_left).set_color(Color::rgba(40, 160, 60));
        const PaintLeaseSnapshot hidden = (*state.model).paint_lease_snapshot();
        require(hidden.state == PaintLeaseState::occluded_dirty, "a change while hidden is not owed as occluded_dirty");
        [view collectDamage];
        state.previous_presented = hidden.presented_revision;
        [view notifyOcclusion:NO];
        set_stage(state, Stage::revealed);
        return;
    }
    if (state.stage == Stage::revealed) {
        const PaintLeaseSnapshot lease = (*state.model).paint_lease_snapshot();
        if (lease.presented_revision <= state.previous_presented) return;
        const MetricsSnapshot after = (*state.model).metrics_snapshot();
        const double painted = after.painted_damage_area - state.before_metrics.painted_damage_area;
        const double block = state.blocks[0].area();
        std::cout << "dp-reveal|changed_while_hidden|painted_area=" << painted << "|block_area=" << block
                  << "|presents=" << (after.frames_presented - state.before_metrics.frames_presented) << '\n';
        const double difference = painted - block;
        require(difference < 1.0e-6 && difference > -1.0e-6,
            "becoming visible repainted more than what changed while hidden");
        // Hidden and shown again with nothing changed.
        state.before_metrics = after;
        [view notifyOcclusion:YES];
        [view notifyOcclusion:NO];
        state.settle_steps = 0;
        set_stage(state, Stage::bare_reveal);
        return;
    }
    if (state.stage == Stage::bare_reveal) {
        // Let the whole-view display request of the reveal be drawn.
        ++state.settle_steps;
        if (state.settle_steps < 4U) return;
        const MetricsSnapshot after = (*state.model).metrics_snapshot();
        const double painted = after.painted_damage_area - state.before_metrics.painted_damage_area;
        const std::uint64_t passes = after.paint_passes - state.before_metrics.paint_passes;
        const double native_dirty = host_number(view, "last_native_dirty_area");
        std::cout << "dp-reveal|unchanged|painted_area=" << painted << "|paint_passes=" << passes
                  << "|native_dirty_area=" << native_dirty << '\n';
        require(painted == 0.0 && passes == 0U, "becoming visible with nothing changed repainted");
        set_stage(state, Stage::resize_hidden);
        return;
    }
    if (state.stage == Stage::resize_hidden) {
        // A new backing size while hidden: the front no longer matches.
        [view notifyOcclusion:YES];
        state.before_metrics = (*state.model).metrics_snapshot();
        state.previous_presented = (*state.model).paint_lease_snapshot().presented_revision;
        [window setContentSize:NSMakeSize(420.0, 300.0)];
        [view notifyOcclusion:NO];
        set_stage(state, Stage::resized);
        return;
    }
    if (state.stage == Stage::resized) {
        const PaintLeaseSnapshot lease = (*state.model).paint_lease_snapshot();
        if (lease.presented_revision <= state.previous_presented) return;
        const MetricsSnapshot after = (*state.model).metrics_snapshot();
        const double painted = after.painted_damage_area - state.before_metrics.painted_damage_area;
        const double extent = static_cast<double>(view.bounds.size.width * view.bounds.size.height);
        std::cout << "dp-reveal|resized_while_hidden|painted_area=" << painted << "|view_area=" << extent << '\n';
        require(extent > 400.0 * 300.0, "the window did not take the new size");
        require(painted >= extent - 1.0e-6, "a reveal after a resize while hidden did not repaint the whole view");
        state.settle_steps = 0;
        set_stage(state, Stage::minimize);
        return;
    }
    if (state.stage == Stage::minimize) {
        // Let the window server show the last frame before the baseline.
        ++state.settle_steps;
        if (state.settle_steps < 4U) return;
        state.screen_before_minimize = capture_screen(window, view, "before_minimize");
        [window miniaturize:nil];
        set_stage(state, Stage::minimized);
        return;
    }
    if (state.stage == Stage::minimized) {
        if (!window.isMiniaturized) return;
        if (state.change_while_minimized) {
            (*state.top_left).set_color(Color::rgba(200, 160, 40));
            [view collectDamage];
        }
        state.before_metrics = (*state.model).metrics_snapshot();
        state.draws_before = host_number(view, "native_draw_count");
        [window deminiaturize:nil];
        state.settle_steps = 0;
        set_stage(state, Stage::restored);
        return;
    }
    if (state.stage == Stage::cover) {
        // Let the restore's activation frame settle before the baseline.
        ++state.settle_steps;
        if (state.settle_steps < 6U) return;
        state.screen_before_cover = capture_screen(window, view, state.change_while_covered ? "before_cover_changed" : "before_cover");
        state.key_before_cover = window.isKeyWindow;
        // Larger than the window's frame, above it, and never key: the window
        // is occluded without a change of activation.
        const NSRect frame = NSInsetRect(window.frame, -24.0, -24.0);
        const NSWindowStyleMask style = NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel;
        NSPanel* const panel = [[NSPanel alloc] initWithContentRect:frame styleMask:style
                                                            backing:NSBackingStoreBuffered defer:NO];
        panel.opaque = YES;
        panel.backgroundColor = NSColor.blackColor;
        panel.hidesOnDeactivate = NO;
        panel.level = NSFloatingWindowLevel;
        panel.releasedWhenClosed = NO;
        [panel orderFrontRegardless];
        state.cover = panel;
        state.cover_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        set_stage(state, Stage::covered);
        return;
    }
    if (state.stage == Stage::covered) {
        if (!(*state.model).occluded()) {
            if (std::chrono::steady_clock::now() >= state.cover_deadline) {
                throw std::runtime_error("not exercised: the window was never reported occluded under a covering panel");
            }
            return;
        }
        if (state.change_while_covered) {
            (*state.top_left).set_color(Color::rgba(90, 40, 160));
            [view collectDamage];
        }
        state.before_metrics = (*state.model).metrics_snapshot();
        state.draws_before = host_number(view, "native_draw_count");
        [state.cover orderOut:nil];
        state.cover = nil;
        state.settle_steps = 0;
        set_stage(state, Stage::uncovered);
        return;
    }
    if (state.stage == Stage::uncovered) {
        const bool visible = !(*state.model).occluded() &&
            (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
        if (!visible) return;
        ++state.settle_steps;
        if (state.settle_steps < 6U) return;
        const Capture uncovered = capture_screen(window, view, state.change_while_covered ? "uncovered_changed" : "uncovered");
        const MetricsSnapshot after = (*state.model).metrics_snapshot();
        const double painted = after.painted_damage_area - state.before_metrics.painted_damage_area;
        const std::uint64_t passes = after.paint_passes - state.before_metrics.paint_passes;
        const double draws = host_number(view, "native_draw_count") - state.draws_before;
        const bool key_after = window.isKeyWindow;
        const std::array<gui_forms::Rect, 1> changed_block{state.blocks[0]};
        const ChangedPixels changed = changed_pixels(state.screen_before_cover, uncovered, state.scale, changed_block);
        const std::uint8_t root_red = red_at(uncovered, state.scale, 200.0, 150.0);
        std::cout << "dp-reveal|occlusion|change_while_hidden=" << (state.change_while_covered ? 1 : 0)
                  << "|native_draws=" << draws << "|painted_area=" << painted << "|paint_passes=" << passes
                  << "|key_before=" << (state.key_before_cover ? 1 : 0) << "|key_after=" << (key_after ? 1 : 0)
                  << "|screen_changed_inside=" << changed.inside << "|screen_changed_outside=" << changed.outside
                  << "|root_red=" << static_cast<unsigned>(root_red) << '\n';
        require(key_after == state.key_before_cover, "not exercised: covering the window changed its key status");
        require(draws >= 1.0, "not exercised: AppKit did not draw the uncovered window");
        require(root_red != 255U, "the uncovered window came back white");
        require(changed.outside == 0U, "the uncovered window differs from the window before it was covered");
        const double device_pixels_per_unit = state.scale * state.scale;
        const std::size_t block_pixels =
            static_cast<std::size_t>(std::llround(state.blocks[0].area() * device_pixels_per_unit));
        if (state.change_while_covered) {
            const double difference = painted - state.blocks[0].area();
            require(difference < 1.0e-6 && difference > -1.0e-6,
                "uncovering repainted more than what changed while covered");
            require(changed.inside == block_pixels, "the change made while covered is not on the screen");
            state.finished = true;
            close_fixture(state);
            return;
        }
        require(painted == 0.0 && passes == 0U, "uncovering with nothing changed repainted");
        require(changed.inside == 0U, "the uncovered window changed although nothing changed while covered");
        state.change_while_covered = true;
        state.settle_steps = 0;
        set_stage(state, Stage::cover);
        return;
    }
    const bool shown_again = !window.isMiniaturized && window.isVisible &&
        (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
    if (!shown_again) return;
    // Let the restore animation finish. The old fault (a restored window left
    // blank until mouse input) persists, so a settled capture still sees it.
    ++state.settle_steps;
    if (state.settle_steps < 10U) return;
    const Capture restored = capture_screen(window, view, state.change_while_minimized ? "restored_changed" : "restored");
    const MetricsSnapshot after = (*state.model).metrics_snapshot();
    const double painted = after.painted_damage_area - state.before_metrics.painted_damage_area;
    const std::uint64_t passes = after.paint_passes - state.before_metrics.paint_passes;
    const double draws = host_number(view, "native_draw_count") - state.draws_before;
    const std::array<gui_forms::Rect, 1> changed_block{state.blocks[0]};
    const ChangedPixels changed = changed_pixels(state.screen_before_minimize, restored, state.scale, changed_block);
    const std::uint8_t root_red = red_at(restored, state.scale, 200.0, 150.0);
    std::cout << "dp-reveal|miniaturize|change_while_hidden=" << (state.change_while_minimized ? 1 : 0)
              << "|native_draws=" << draws << "|painted_area=" << painted << "|paint_passes=" << passes
              << "|screen_changed_inside=" << changed.inside << "|screen_changed_outside=" << changed.outside
              << "|root_red=" << static_cast<unsigned>(root_red) << '\n';
    require(root_red != 255U, "the restored window came back white");
    require(changed.outside == 0U, "the restored window differs from the window before it was hidden");
    const double device_pixels_per_unit = state.scale * state.scale;
    const std::size_t block_pixels =
        static_cast<std::size_t>(std::llround(state.blocks[0].area() * device_pixels_per_unit));
    if (state.change_while_minimized) {
        require(changed.inside == block_pixels, "the change made while hidden is not on the screen after the restore");
        state.settle_steps = 0;
        set_stage(state, Stage::cover);
        return;
    }
    require(changed.inside == 0U, "the restored window changed although nothing changed while hidden");
    state.change_while_minimized = true;
    state.settle_steps = 0;
    set_stage(state, Stage::minimize);
}
void run_step(void* const context) {
    const std::unique_ptr<DeferredStep> owner(static_cast<DeferredStep*>(context));
    const std::shared_ptr<State> state = (*owner).state;
    if ((*state).host_closed || (*state).finished) return;
    @try {
        try {
            if (std::chrono::steady_clock::now() >= (*state).deadline) {
                throw std::runtime_error(std::string("bounded native stage timed out in ") +
                    stage_name((*state).stage));
            }
            advance(*state);
            if (!(*state).finished && !(*state).host_closed) enqueue(state);
        } catch (const std::exception& error) {
            (*state).failure = error.what();
            (*state).finished = true;
            close_fixture(*state);
        }
    } @catch (NSException* exception) {
        NSString* const reason = exception.reason == nil ? @"unknown AppKit exception" : exception.reason;
        const char* const message = reason.UTF8String;
        (*state).failure = message == nullptr ? "AppKit exception" : message;
        (*state).finished = true;
        close_fixture(*state);
    }
}
struct Closed final {
    std::shared_ptr<State> state{};
    void operator()() const {
        (*state).host_closed = true;
        (*state).model = nullptr;
        (*state).close = {};
    }
};
struct Ready final {
    std::shared_ptr<State> state{};
    void operator()(std::function<void()>, std::function<void()> close,
        std::function<HostDialogResult(const HostDialogRequest&)>,
        std::function<HostClipboardTextResult()>, std::function<HostServiceStatus(std::string_view)>) const {
        (*state).close = std::move(close);
        set_stage(*state, Stage::first_frame);
        enqueue(state);
    }
};
}

int main() {
    try {
        const std::shared_ptr<State> state = std::make_shared<State>();
        const Color rest = Color::rgba(90, 90, 90);
        (*state).root = make_control<ColorBlock>(StableId("dp.root"), Color::rgba(230, 230, 230));
        (*state).top_left = make_control<ColorBlock>(StableId("dp.top-left"), rest);
        (*state).bottom_right = make_control<ColorBlock>(StableId("dp.bottom-right"), rest);
        (*state).blocks = {gui_forms::Rect{10.0, 10.0, 40.0, 30.0}, gui_forms::Rect{340.0, 250.0, 40.0, 30.0}};
        (*(*state).top_left).set_requested_bounds((*state).blocks[0]);
        (*(*state).bottom_right).set_requested_bounds((*state).blocks[1]);
        (*(*state).root).add_child((*state).top_left);
        (*(*state).root).add_child((*state).bottom_right);
        gui_forms::host::MacHostOptions options{};
        options.title = window_title;
        options.initial_size = {400, 300};
        options.minimum_size = {400, 300};
        options.print_metrics_on_close = false;
        options.host_ready = Ready{state};
        options.closed = Closed{state};
        std::unique_ptr<gui_forms::Window> model =
            std::make_unique<gui_forms::Window>((*state).root, options.initial_size);
        (*state).model = model.get();
        const int result = gui_forms::host::run_macos(std::move(model), std::move(options));
        (*state).host_closed = true;
        (*state).model = nullptr;
        (*state).close = {};
        require(result == 0 && (*state).finished && (*state).failure.empty(),
            (*state).failure.empty() ? "native fixture did not complete" : (*state).failure.c_str());
        std::cout << "gui_forms_macos_damage_rectangles_tests: all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "macos damage rectangles failure: " << error.what() << '\n';
        return 1;
    }
}
