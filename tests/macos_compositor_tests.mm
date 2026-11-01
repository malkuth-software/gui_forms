// Stage T5, phase C3's gate (notes:planning/stages/T5-compositor/PLAN.md §5 C3):
// a GUI.Forms window on the macOS host, declared non-opaque, through each
// grant, read back from AppKit and off the composed screen.
//
// The window's theme has a clear window material, so that every pixel the
// window presents is clear and the screen shows what the window server
// puts behind it. A solid green window lies behind it. The steps:
// - host: the state the host reports after attach, and the grant of a
//   material with transparent as its fallback. With the accessibility
//   option "reduce transparency" on (as on the CI runner), the host's
//   report withdraws every backdrop: the grant is opaque with the reason
//   reduced_transparency, and the window is AppKit-opaque. The test then
//   sets the application's presentation setting back, as an application
//   may, so that the adapter's own read-backs are still exercised;
// - material: the window non-opaque on a clear background, still taking
//   clicks over its clear pixels (owner decision T5-Q6), the effect view
//   shown behind the window at under-window-background. Its composed pixel
//   must be green-tinted; with reduce transparency on, AppKit draws every
//   effect view as its solid fallback, and that check is reported as not
//   exercised (MEASUREMENTS.md C3);
// - transparent: the effect view hidden, the composed pixel the green
//   behind the window;
// - opaque: the window opaque again, the composed pixel not the green.
// Each reading is printed as a JSON line for the C3 record.
//
// Without a Screen Recording grant (CGPreflightScreenCaptureAccess false)
// the composed reads see only this test's windows and the desktop; the test
// then says so in one line on standard error, naming the process that needs
// the grant, and runs as before, so a failure that follows reads as a setup
// problem.
#import <AppKit/AppKit.h>

#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window.hpp"

#include <dlfcn.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>

namespace {

using namespace gui_forms;

constexpr const char* gate_title = "GUI.Forms compositor gate";
constexpr int channel_tolerance = 12;
// The amount by which green must exceed red and blue for a material's
// pixel over the green backdrop to count as green-tinted.
constexpr int tint_margin = 8;
constexpr CGFloat window_width = 240.0;
constexpr CGFloat window_height = 160.0;
// The backdrop extends this far past the test window on every side.
constexpr CGFloat backdrop_margin = 60.0;
// Perceptual settling time after each request, for the repaint and the
// window server's composite.
constexpr std::int64_t settle_nanoseconds = 600 * static_cast<std::int64_t>(NSEC_PER_MSEC);
constexpr std::int64_t poll_nanoseconds = 50 * static_cast<std::int64_t>(NSEC_PER_MSEC);
// Polls for the host's first compositor report before giving up.
constexpr unsigned report_polls = 100U;

struct Sample final {
    int red{};
    int green{};
    int blue{};
    bool read{};
};

bool near_sample(const Sample& left, const Sample& right) noexcept {
    const bool within = std::abs(left.red - right.red) <= channel_tolerance &&
                        std::abs(left.green - right.green) <= channel_tolerance &&
                        std::abs(left.blue - right.blue) <= channel_tolerance;
    return left.read && right.read && within;
}

bool green_tinted(const Sample& sample) noexcept {
    const bool tinted = sample.green >= sample.red + tint_margin && sample.green >= sample.blue + tint_margin;
    return sample.read && tinted;
}

bool near_black(const Sample& sample) noexcept {
    const bool black = sample.red <= channel_tolerance && sample.green <= channel_tolerance &&
                       sample.blue <= channel_tolerance;
    return sample.read && black;
}

// CGWindowListCreateImage, looked up at run time as in the C3 probe: the
// macOS 15 SDK deprecates it in favour of ScreenCaptureKit, which needs a
// screen recording grant the CI runner does not have. Its type is the SDK
// declaration's; a missing entry point leaves it null.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
using WindowListCreateImageFunction = decltype(&::CGWindowListCreateImage);
#pragma clang diagnostic pop

WindowListCreateImageFunction load_window_list_create_image() noexcept {
    void* const raw = dlsym(RTLD_DEFAULT, "CGWindowListCreateImage");
    WindowListCreateImageFunction function = nullptr;
    static_assert(sizeof(function) == sizeof(raw));
    std::memcpy(&function, &raw, sizeof(function));
    return function;
}

// The primary screen's height: AppKit's screen coordinates rise from the
// primary screen's bottom, Core Graphics' global ones fall from its top.
CGFloat primary_screen_height() {
    NSArray<NSScreen*>* const screens = NSScreen.screens;
    if (screens.count == 0U) return 0.0;
    const CGFloat height = screens[0].frame.size.height;
    return height;
}

// The pixel at (x, y) points from the top-left corner of an image covering
// `width` points; the image may be scaled to pixels.
Sample sample_image(CGImageRef image, CGFloat width, CGFloat x, CGFloat y) noexcept {
    Sample sample{};
    if (image == nullptr || width <= 0.0) return sample;
    const std::size_t pixels_wide = CGImageGetWidth(image);
    const std::size_t pixels_high = CGImageGetHeight(image);
    const CGFloat scale = static_cast<CGFloat>(pixels_wide) / width;
    const CGFloat column = std::floor(x * scale);
    const CGFloat row = std::floor(y * scale);
    if (column < 0.0 || row < 0.0 || column >= static_cast<CGFloat>(pixels_wide) ||
        row >= static_cast<CGFloat>(pixels_high)) {
        return sample;
    }
    std::array<std::uint8_t, 4> bytes{};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    const CGBitmapInfo info = static_cast<CGBitmapInfo>(static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) |
                                                        static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big));
    CGContextRef context = CGBitmapContextCreate(bytes.data(), 1U, 1U, 8U, 4U, space, info);
    CGColorSpaceRelease(space);
    if (context == nullptr) return sample;
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextSetInterpolationQuality(context, kCGInterpolationNone);
    // Core Graphics counts rows from the bottom: the image's row `row` from
    // the top lands on the context's only row.
    const CGFloat bottom = static_cast<CGFloat>(pixels_high) - 1.0 - row;
    const CGRect placed = CGRectMake(-column, -bottom, static_cast<CGFloat>(pixels_wide),
                                     static_cast<CGFloat>(pixels_high));
    CGContextDrawImage(context, placed, image);
    CGContextRelease(context);
    sample.red = bytes[0];
    sample.green = bytes[1];
    sample.blue = bytes[2];
    sample.read = true;
    return sample;
}

// The composite under and including `window`, over the window's frame and
// `backdrop_margin / 2` around it: the window's clear-pixel centre, and a
// point of the backdrop left of the window as the reference green, both
// read from one image so that colour management treats them alike.
void read_composite(NSWindow* window, Sample& centre, Sample& reference) {
    static const WindowListCreateImageFunction create = load_window_list_create_image();
    if (create == nullptr) return;
    const CGFloat margin = backdrop_margin / 2.0;
    const NSRect frame = window.frame;
    const CGFloat top = primary_screen_height() - frame.origin.y - frame.size.height - margin;
    const CGRect rectangle = CGRectMake(frame.origin.x - margin, top, frame.size.width + 2.0 * margin,
                                        frame.size.height + 2.0 * margin);
    const CGWindowID number = static_cast<CGWindowID>(window.windowNumber);
    const CGImageRef image =
        create(rectangle, kCGWindowListOptionOnScreenBelowWindow | kCGWindowListOptionIncludingWindow, number,
               kCGWindowImageNominalResolution);
    if (image == nullptr) return;
    const NSRect content = [window contentRectForFrameRect:frame];
    const CGFloat content_x = content.origin.x - frame.origin.x + margin + content.size.width / 2.0;
    const CGFloat content_y = (frame.origin.y + frame.size.height) - (content.origin.y + content.size.height) + margin +
                              content.size.height / 2.0;
    centre = sample_image(image, rectangle.size.width, content_x, content_y);
    reference = sample_image(image, rectangle.size.width, margin / 2.0, rectangle.size.height / 2.0);
    CGImageRelease(image);
}

// The view the GUI.Forms window rasterises into: the content view itself,
// or the second subview of the backdrop container (macos_compositor.hpp).
NSView* raster_of(NSWindow* window) {
    NSView* const content = window.contentView;
    if (content == nil || content.subviews.count != 2U) return content;
    NSView* const raster = content.subviews[1];
    return raster;
}

NSVisualEffectView* effect_of(NSWindow* window) {
    NSView* const content = window.contentView;
    if (content == nil || content.subviews.count != 2U) return nil;
    NSView* const first = content.subviews[0];
    if (![first isKindOfClass:[NSVisualEffectView class]]) return nil;
    NSVisualEffectView* const effect = static_cast<NSVisualEffectView*>(first);
    return effect;
}

// The raster view's own alpha at its centre, 0 to 255, or -1 when it
// cannot be read.
int raster_alpha(NSView* raster) {
    if (raster == nil) return -1;
    NSBitmapImageRep* const bitmap = [raster bitmapImageRepForCachingDisplayInRect:raster.bounds];
    if (bitmap == nil) return -1;
    [raster cacheDisplayInRect:raster.bounds toBitmapImageRep:bitmap];
    NSColor* const sample = [bitmap colorAtX:bitmap.pixelsWide / 2 y:bitmap.pixelsHigh / 2];
    if (sample == nil) return -1;
    const int alpha = static_cast<int>(std::lround(sample.alphaComponent * 255.0));
    return alpha;
}

// The professional theme with a clear window material.
std::shared_ptr<const Theme> clear_theme() {
    const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    SurfaceMaterial clear{};
    clear.fills.clear();
    clear.fills.push_back(MaterialFillLayer::solid(Color::rgba(0, 0, 0, 0)));
    ControlRoleRecipes& window = definition.roles[static_cast<std::size_t>(ControlVisualRole::window)];
    for (ControlVisualRecipe& recipe : window.ordinary) recipe.material = clear;
    std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

enum class GateStep : std::uint8_t {
    waiting_for_report,
    host,
    material,
    transparent,
    opaque,
    finished,
};

const char* step_name(GateStep step) noexcept {
    switch (step) {
    case GateStep::waiting_for_report: return "waiting_for_report";
    case GateStep::host: return "host";
    case GateStep::material: return "material";
    case GateStep::transparent: return "transparent";
    case GateStep::opaque: return "opaque";
    case GateStep::finished: return "finished";
    }
    return "unknown";
}

// The gate's state, owned by main for the whole run; the ready callback and
// the queued steps reach it through `active_gate`, which lives no longer.
struct GateContext final {
    NSWindow* __strong backdrop{};
    NSWindow* __strong window{};
    Window* model{};
    ApplicationWindowHandle handle{};
    GateStep step{GateStep::waiting_for_report};
    unsigned polls{};
    unsigned readings{};
    // The host reported reduce transparency on, and the test set the
    // application's setting back after recording the host's withdrawal.
    bool host_reduced{};
    const char* failure{};
};

GateContext* active_gate{};

void fail(GateContext& gate, const char* message) {
    if (gate.failure == nullptr) gate.failure = message;
}

void run_gate_step(void* context);

void queue_gate_step(std::int64_t delay_nanoseconds) {
    const dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, delay_nanoseconds);
    dispatch_after_f(deadline, dispatch_get_main_queue(), nullptr, &run_gate_step);
}

void finish(GateContext& gate) {
    gate.step = GateStep::finished;
    static_cast<void>(gate.handle.request_close());
}

// What the window's compositor state must be on macOS: present, every
// backdrop supported, high contrast never set (owner decision T5-Q7).
void check_state(GateContext& gate) {
    const CompositorState& state = (*gate.model).compositor_state();
    std::printf("{\"gate\":\"macos_compositor\",\"state\":%s}\n", state.to_json().c_str());
    if (state.compositing != CompositorPresence::present) fail(gate, "the window server always composites");
    if (state.support_of(WindowBackdrop::transparent) != WindowBackdropSupport::supported ||
        state.support_of(WindowBackdrop::window_material) != WindowBackdropSupport::supported ||
        state.support_of(WindowBackdrop::transient_material) != WindowBackdropSupport::supported) {
        fail(gate, "macOS 14 and later support every backdrop");
    }
    if (!state.transparency_enabled) fail(gate, "macOS has no transparency-effects switch");
    if (state.high_contrast) fail(gate, "increase contrast never sets high_contrast (T5-Q7)");
    const bool workspace_reduced = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceTransparency == YES;
    if (state.reduced_transparency != workspace_reduced) fail(gate, "reduced transparency is the workspace's option");
}

void request_for(GateContext& gate, GateStep step) {
    WindowBackdropRequest request = WindowBackdropRequest::preferring(WindowBackdrop::opaque);
    if (step == GateStep::host || step == GateStep::material) {
        request = WindowBackdropRequest::with_fallback(WindowBackdrop::window_material, WindowBackdrop::transparent);
    } else if (step == GateStep::transparent) {
        request = WindowBackdropRequest::preferring(WindowBackdrop::transparent);
    }
    static_cast<void>((*gate.model).request_backdrop(request));
}

// AppKit's state and the screen after a step, against the grant.
void read_step(GateContext& gate) {
    NSWindow* const window = gate.window;
    const WindowBackdropGrant grant = (*gate.model).backdrop_grant();
    NSVisualEffectView* const effect = effect_of(window);
    const bool effect_shown = effect != nil && effect.hidden == NO;
    const bool effect_material = effect != nil && effect.material == NSVisualEffectMaterialUnderWindowBackground;
    const bool effect_behind = effect != nil && effect.blendingMode == NSVisualEffectBlendingModeBehindWindow;
    const bool clear_background = window.backgroundColor.alphaComponent < 0.01;
    const int alpha = raster_alpha(raster_of(window));
    Sample centre{};
    Sample reference{};
    read_composite(window, centre, reference);
    const NSRect content = [window contentRectForFrameRect:window.frame];
    const NSPoint middle = NSMakePoint(NSMidX(content), NSMidY(content));
    const NSInteger hit = [NSWindow windowNumberAtPoint:middle belowWindowWithWindowNumber:0];
    const bool hits_window = hit == window.windowNumber;
    std::printf("{\"gate\":\"macos_compositor\",\"step\":\"%s\",\"granted\":\"%s\",\"reason\":\"%s\","
                "\"opaque\":%s,\"clear_background\":%s,\"ignores_mouse\":%s,\"effect_shown\":%s,"
                "\"raster_alpha\":%d,\"clear\":[%d,%d,%d],\"backdrop\":[%d,%d,%d],\"clear_hits_window\":%s}\n",
                step_name(gate.step), window_backdrop_name(grant.granted), window_backdrop_reason_name(grant.reason),
                window.opaque ? "true" : "false", clear_background ? "true" : "false",
                window.ignoresMouseEvents ? "true" : "false", effect_shown ? "true" : "false", alpha, centre.red,
                centre.green, centre.blue, reference.red, reference.green, reference.blue,
                hits_window ? "true" : "false");
    gate.readings = gate.readings + 1U;
    if (!centre.read || !reference.read) fail(gate, "the composed screen is readable");
    if (!green_tinted(reference)) fail(gate, "the backdrop shows green");
    if (!hits_window) fail(gate, "a clear pixel still hits the window (no click-through)");
    if (effect == nil) fail(gate, "a non-opaque window is made inside the backdrop container");
    const bool non_opaque = grant.granted != WindowBackdrop::opaque;
    if (non_opaque) {
        if (window.opaque) fail(gate, "a non-opaque grant makes the window non-opaque");
        if (!clear_background) fail(gate, "a non-opaque grant clears the window's background");
        if (window.ignoresMouseEvents) fail(gate, "a non-opaque window still takes clicks (T5-Q6)");
        if (alpha < 0 || alpha > 2) fail(gate, "the raster keeps the clear theme's alpha");
    } else {
        if (!window.opaque) fail(gate, "an opaque grant makes the window opaque");
        if (effect_shown) fail(gate, "an opaque grant hides the effect view");
        if (alpha < 253) fail(gate, "an opaque grant presents opaque pixels");
        if (near_sample(centre, reference)) fail(gate, "an opaque window shows nothing behind it");
    }
    switch (gate.step) {
    case GateStep::host:
        if (gate.host_reduced) {
            if (grant.granted != WindowBackdrop::opaque || grant.reason != WindowBackdropReason::reduced_transparency) {
                fail(gate, "the host's reduce-transparency report withdraws the backdrop");
            }
        } else if (grant.granted != WindowBackdrop::window_material) {
            fail(gate, "the material is granted");
        }
        break;
    case GateStep::material:
        if (grant.granted != WindowBackdrop::window_material) fail(gate, "the material is granted");
        if (!effect_shown || !effect_material || !effect_behind) {
            fail(gate, "the material shows the effect view behind the window at under-window-background");
        }
        if (gate.host_reduced) {
            std::printf("{\"gate\":\"macos_compositor\",\"step\":\"material\",\"material_screen\":"
                        "\"not exercised: reduce transparency on\"}\n");
        } else if (!green_tinted(centre) || near_black(centre)) {
            fail(gate, "the material is green-tinted by the backdrop behind it");
        }
        break;
    case GateStep::transparent:
        if (grant.granted != WindowBackdrop::transparent) fail(gate, "transparent is granted");
        if (effect_shown) fail(gate, "transparent hides the effect view");
        if (!near_sample(centre, reference)) fail(gate, "a transparent window shows what lies behind it");
        break;
    case GateStep::opaque:
        if (grant.granted != WindowBackdrop::opaque) fail(gate, "opaque is granted");
        break;
    case GateStep::waiting_for_report:
    case GateStep::finished:
    default:
        // Ignored: no step to check. The gate reads a step only between its
        // first report and its finish (advance).
        break;
    }
}

// The host's first report has arrived: record the host's grant, and with
// reduce transparency on set the application's setting back.
void begin_steps(GateContext& gate) {
    check_state(gate);
    gate.host_reduced = (*gate.model).compositor_state().reduced_transparency;
    gate.step = GateStep::host;
    request_for(gate, gate.step);
    queue_gate_step(settle_nanoseconds);
}

void advance(GateContext& gate) {
    read_step(gate);
    if (gate.step == GateStep::host) {
        if (gate.host_reduced) {
            PresentationSettings settings = (*gate.model).presentation_settings();
            settings.reduced_transparency = false;
            (*gate.model).set_presentation_settings(settings);
            std::printf("{\"gate\":\"macos_compositor\",\"override\":\"reduced_transparency=false\"}\n");
        }
        gate.step = GateStep::material;
    } else if (gate.step == GateStep::material) {
        gate.step = GateStep::transparent;
    } else if (gate.step == GateStep::transparent) {
        gate.step = GateStep::opaque;
    } else {
        finish(gate);
        return;
    }
    request_for(gate, gate.step);
    queue_gate_step(settle_nanoseconds);
}

void continue_gate(GateContext& gate) {
    if (gate.step == GateStep::finished) return;
    if (gate.step != GateStep::waiting_for_report) {
        advance(gate);
        return;
    }
    if ((*gate.model).compositor_state().generation.value != 0U) {
        begin_steps(gate);
        return;
    }
    gate.polls = gate.polls + 1U;
    if (gate.polls >= report_polls) {
        fail(gate, "the host reports its compositor state after attach");
        finish(gate);
        return;
    }
    queue_gate_step(poll_nanoseconds);
}

void run_gate_step(void* context) {
    static_cast<void>(context);
    GateContext* const held = active_gate;
    if (held == nullptr) return;
    GateContext& gate = *held;
    try {
        continue_gate(gate);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        fail(gate, "a gate step threw");
        finish(gate);
    }
}

NSWindow* find_gate_window() {
    NSString* const title = [NSString stringWithUTF8String:gate_title];
    NSWindow* found = nil;
    for (NSWindow* candidate in NSApp.windows) {
        if ([candidate.title isEqualToString:title]) {
            found = candidate;
            break;
        }
    }
    return found;
}

// A solid green window around the test window, just below it; both float
// above other windows while the test runs.
NSWindow* make_backdrop(NSWindow* window) {
    const NSRect frame = NSInsetRect(window.frame, -backdrop_margin, -backdrop_margin);
    NSWindow* const backdrop = [[NSWindow alloc] initWithContentRect:frame
                                                           styleMask:NSWindowStyleMaskBorderless
                                                             backing:NSBackingStoreBuffered
                                                               defer:NO];
    backdrop.releasedWhenClosed = NO;
    backdrop.backgroundColor = [NSColor colorWithSRGBRed:0.0 green:160.0 / 255.0 blue:0.0 alpha:1.0];
    backdrop.opaque = YES;
    backdrop.level = NSFloatingWindowLevel;
    backdrop.ignoresMouseEvents = YES;
    [backdrop orderFront:nil];
    return backdrop;
}

// The window is ready: find it, float it over the backdrop, give it the
// clear theme and wait for the host's first compositor report.
struct GateReady final {
    void operator()(Window& model, ApplicationWindowHandle handle) const {
        GateContext* const held = active_gate;
        if (held == nullptr) return;
        GateContext& gate = *held;
        gate.model = &model;
        gate.handle = handle;
        gate.window = find_gate_window();
        if (gate.window == nil) {
            fail(gate, "the test finds its own window");
            finish(gate);
            return;
        }
        if (!model.non_opaque_declared()) fail(gate, "the window option declares the window non-opaque");
        gate.window.level = NSFloatingWindowLevel;
        [gate.window setFrameOrigin:NSMakePoint(160.0, 200.0)];
        gate.backdrop = make_backdrop(gate.window);
        [gate.window orderWindow:NSWindowAbove relativeTo:gate.backdrop.windowNumber];
        [gate.window makeKeyWindow];
        model.set_theme(clear_theme());
        queue_gate_step(poll_nanoseconds);
    }
};

} // namespace

int main() {
    if (!CGPreflightScreenCaptureAccess()) {
        std::fprintf(stderr,
                     "macos compositor tests: Screen Recording is not granted to the process that started them (over "
                     "ssh on the M4: sshd-keygen-wrapper; grant it in System Settings, Privacy & Security, Screen & "
                     "System Audio Recording); composed-screen reads see only this test's windows and the desktop\n");
    }
    const HostCapabilities capabilities = Application::capabilities();
    if (!capabilities.supports(HostCapability::transparent_windows) ||
        !capabilities.supports(HostCapability::window_backdrops) ||
        !capabilities.supports(HostCapability::compositor_notifications) ||
        !capabilities.supports(HostCapability::presentation_preferences)) {
        std::fprintf(stderr, "the macOS host advertises the compositor capabilities\n");
        return 1;
    }
    GateContext gate{};
    active_gate = &gate;
    const std::shared_ptr<Control> root = make_control<Control>(StableId("compositor.gate.root"));
    std::unique_ptr<Window> model = std::make_unique<Window>(root, gui_forms::Size{window_width, window_height});
    ApplicationWindowOptions options{};
    options.title = gate_title;
    options.initial_size = gui_forms::Size{window_width, window_height};
    options.non_opaque = true;
    options.ready = GateReady{};
    const ApplicationResult result = Application::run(std::move(model), std::move(options));
    active_gate = nullptr;
    if (gate.backdrop != nil) [gate.backdrop close];
    std::fflush(stdout);
    if (!result.accepted()) {
        std::fprintf(stderr, "the application runs\n");
        return 1;
    }
    if (gate.failure != nullptr) {
        std::fprintf(stderr, "%s\n", gate.failure);
        return 1;
    }
    if (gate.readings != 4U) {
        std::fprintf(stderr, "every step was read\n");
        return 1;
    }
    std::printf("macos compositor tests passed\n");
    return 0;
}
