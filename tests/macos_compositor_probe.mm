// Stage T5, phase C3's measurement (notes:planning/stages/T5-compositor/PLAN.md
// §1.2 and §5 C3): what the window server composes from a window whose
// view draws premultiplied pixels with the copy blend mode, as the host
// presents today, when the window is non-opaque and when a behind-window
// NSVisualEffectView lies beneath the view. Nothing of GUI.Forms is used:
// the probe measures the platform, so that the host adapter is built on
// recorded behaviour rather than on assumptions.
//
// A solid green backdrop window lies behind a titled window whose content
// is three columns: clear (alpha 0), red at half alpha (premultiplied 100,
// 0, 0, 128) and opaque blue. For each case the probe shows a fresh window,
// lets the run loop compose, and reads:
//
// - the composed screen at each column's centre and in the title bar
//   (CGWindowListCreateImage of the window and what lies below it);
// - the window's own image, with its alpha (the window alone);
// - which window the window server reports under the clear column, and
//   which view receives a mouse click posted there;
// - the clear column again while the backdrop window is key;
// - what AppKit reads back: the window's opaque flag and background alpha,
//   and the effect view's material, blending mode, state and appearance.
//
// It prints one JSON line per case, then one with the settings the adapter
// will read, then a last line: "macos compositor probe finished". The
// verdicts ("per_pixel_alpha", "material_shown") are its reading of the
// colours, within a tolerance of 12 per channel.
//
// An effect view follows its window's active state, and an inactive
// window's material is AppKit's solid inactive colour, so a material is
// measured only in an active window, as a real application has one. The
// probe is an application bundle, launched through LaunchServices (`open`,
// tests/support/run_macos_probe_bundle.sh) for an ordinary activation; it
// falls back to activateIgnoringOtherApps: only when it is still not
// active. Each case records the window's isKeyWindow and NSApp.isActive.
// A material case read while either is false prints no material results,
// only "not_exercised":"window inactive", and the run then ends with the
// line "not exercised: window inactive" and fails. Otherwise it measures
// and never judges: it fails only when it cannot make its windows or open
// its output.
//
// Every image read needs a Screen Recording grant, and without one
// CGWindowListCreateImage asks the user for it in a dialog. The probe checks
// CGPreflightScreenCaptureAccess (which never asks) once, before any read;
// without the grant it makes no image read, so no dialog appears, and each
// case prints "image_reads":"not exercised: screen recording not granted"
// in place of its colours and colour verdicts. The rest of the case (the
// window under the clear column, the click, AppKit's flags) is still read,
// and the run prints "not exercised: image reads (screen recording not
// granted)" before its last line and succeeds: the missing grant is a
// measurement of the host, not a probe failure.
//
// Arguments: `--output <path>` writes the lines to that file instead of
// standard output (LaunchServices gives a launched bundle no terminal).
// `--no-image-reads` makes no image read even with the grant, as without
// it, so that path can be exercised on a host that has the grant; its
// lines then say "image reads turned off" in place of "screen recording not
// granted".
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <dlfcn.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Content size in points, three columns of column_width.
constexpr CGFloat content_width = 240.0;
constexpr CGFloat content_height = 120.0;
constexpr CGFloat column_width = 80.0;
constexpr int channel_tolerance = 12;
// The backdrop's colour, (red, green, blue).
constexpr std::array<int, 3> backdrop_rgb{0, 160, 0};

// What lies beneath the probe's view.
enum class ProbeBase {
    // today: an opaque window and an opaque view.
    none,
    // a non-opaque window with a clear background.
    clear,
    // as clear, with ignoresMouseEvents set to NO explicitly.
    clear_receiving,
    // as clear, with a behind-window effect view as the content view.
    effect,
    // an opaque window with a behind-window effect view.
    effect_opaque_window,
};

// A case changes a shown window from first to then when then differs.
struct ProbeCase final {
    const char* name{};
    ProbeBase first{ProbeBase::none};
    ProbeBase then{ProbeBase::none};
    NSVisualEffectMaterial material{NSVisualEffectMaterialWindowBackground};
    const char* material_name{};
};

constexpr std::size_t case_count = 18U;
const std::array<ProbeCase, case_count> probe_cases{{
    {"none", ProbeBase::none, ProbeBase::none, NSVisualEffectMaterialWindowBackground, ""},
    {"clear", ProbeBase::clear, ProbeBase::clear, NSVisualEffectMaterialWindowBackground, ""},
    {"clear_receiving", ProbeBase::clear_receiving, ProbeBase::clear_receiving,
     NSVisualEffectMaterialWindowBackground, ""},
    {"window_background", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialWindowBackground,
     "windowBackground"},
    {"under_window_background", ProbeBase::effect, ProbeBase::effect,
     NSVisualEffectMaterialUnderWindowBackground, "underWindowBackground"},
    {"under_page_background", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialUnderPageBackground,
     "underPageBackground"},
    {"content_background", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialContentBackground,
     "contentBackground"},
    {"sidebar", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialSidebar, "sidebar"},
    {"titlebar", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialTitlebar, "titlebar"},
    {"header_view", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialHeaderView, "headerView"},
    {"menu", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialMenu, "menu"},
    {"popover", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialPopover, "popover"},
    {"hud_window", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialHUDWindow, "hudWindow"},
    {"sheet", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialSheet, "sheet"},
    {"tool_tip", ProbeBase::effect, ProbeBase::effect, NSVisualEffectMaterialToolTip, "toolTip"},
    {"under_window_background_opaque_window", ProbeBase::effect_opaque_window,
     ProbeBase::effect_opaque_window, NSVisualEffectMaterialUnderWindowBackground, "underWindowBackground"},
    {"clear_then_none", ProbeBase::clear, ProbeBase::none, NSVisualEffectMaterialWindowBackground, ""},
    {"effect_then_clear", ProbeBase::effect, ProbeBase::clear, NSVisualEffectMaterialUnderWindowBackground,
     "underWindowBackground"},
}};

const char* base_name(ProbeBase base) noexcept {
    switch (base) {
    case ProbeBase::none: return "none";
    case ProbeBase::clear: return "clear";
    case ProbeBase::clear_receiving: return "clear_receiving";
    case ProbeBase::effect: return "effect";
    case ProbeBase::effect_opaque_window: return "effect_opaque_window";
    }
    return "unknown";
}

const char* blending_name(NSVisualEffectBlendingMode mode) noexcept {
    if (mode == NSVisualEffectBlendingModeBehindWindow) return "behindWindow";
    if (mode == NSVisualEffectBlendingModeWithinWindow) return "withinWindow";
    return "unknown";
}

const char* state_name(NSVisualEffectState state) noexcept {
    if (state == NSVisualEffectStateFollowsWindowActiveState) return "followsWindowActiveState";
    if (state == NSVisualEffectStateActive) return "active";
    if (state == NSVisualEffectStateInactive) return "inactive";
    return "unknown";
}

// The appearance's name, as a literal that outlives the probe's reading.
const char* appearance_name(NSAppearanceName name) {
    if (name == nil) return "";
    if ([name isEqualToString:NSAppearanceNameAqua]) return "aqua";
    if ([name isEqualToString:NSAppearanceNameDarkAqua]) return "darkAqua";
    if ([name isEqualToString:NSAppearanceNameVibrantLight]) return "vibrantLight";
    if ([name isEqualToString:NSAppearanceNameVibrantDark]) return "vibrantDark";
    if ([name isEqualToString:NSAppearanceNameAccessibilityHighContrastAqua]) return "highContrastAqua";
    if ([name isEqualToString:NSAppearanceNameAccessibilityHighContrastDarkAqua]) return "highContrastDarkAqua";
    return "other";
}

// One read pixel, premultiplied sRGB, and whether it was read at all.
struct Sample final {
    int red{};
    int green{};
    int blue{};
    int alpha{};
    bool read{};
};

bool near_rgb(const Sample& sample, int red, int green, int blue) noexcept {
    const bool within = std::abs(sample.red - red) <= channel_tolerance &&
                      std::abs(sample.green - green) <= channel_tolerance &&
                      std::abs(sample.blue - blue) <= channel_tolerance;
    return sample.read && within;
}

// CGWindowListCreateImage, looked up at run time: the macOS 15 SDK
// deprecates it in favour of ScreenCaptureKit, which needs a screen
// recording grant the CI runner does not have. Its type is the SDK
// declaration's (naming it is what warns); a missing entry point leaves it
// null.
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

WindowListCreateImageFunction window_list_create_image() noexcept {
    static const WindowListCreateImageFunction function = load_window_list_create_image();
    return function;
}

// The pixel at (x, y) points from the image's top-left corner, of an image
// covering width points: the image may be scaled to pixels.
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
    sample.alpha = bytes[3];
    sample.read = true;
    return sample;
}

// The primary screen's height: AppKit's screen coordinates rise from the
// primary screen's bottom, Core Graphics' global ones fall from its top.
CGFloat primary_screen_height() {
    NSArray<NSScreen*>* const screens = NSScreen.screens;
    if (screens.count == 0U) return 0.0;
    const CGFloat height = screens[0].frame.size.height;
    return height;
}

void spin(NSTimeInterval seconds) {
    NSDate* const until = [NSDate dateWithTimeIntervalSinceNow:seconds];
    [[NSRunLoop currentRunLoop] runUntilDate:until];
}

} // namespace

// The probe's content: the three columns, written with the copy blend mode
// as the host writes its frame, and a count of mouse-downs it received.
@interface GFProbeView : NSView
@property(nonatomic) BOOL opaqueView;
@property(nonatomic) unsigned mouseDowns;
@end

@implementation GFProbeView
- (BOOL)isOpaque {
    return self.opaqueView;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}
- (void)mouseDown:(NSEvent*)event {
    (void)event;
    self.mouseDowns = self.mouseDowns + 1U;
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    CGContextSaveGState(context);
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    const CGFloat height = self.bounds.size.height;
    CGContextClearRect(context, CGRectMake(0.0, 0.0, column_width, height));
    // Premultiplied (100, 0, 0, 128): unpremultiplied red 100 / 128.
    CGContextSetRGBFillColor(context, 100.0 / 128.0, 0.0, 0.0, 128.0 / 255.0);
    CGContextFillRect(context, CGRectMake(column_width, 0.0, column_width, height));
    CGContextSetRGBFillColor(context, 0.0, 0.0, 220.0 / 255.0, 1.0);
    CGContextFillRect(context, CGRectMake(column_width * 2.0, 0.0, column_width, height));
    CGContextRestoreGState(context);
}
@end

// The backdrop: solid green, opaque, counting the mouse-downs it received.
@interface GFBackdropView : NSView
@property(nonatomic) unsigned mouseDowns;
@end

@implementation GFBackdropView
- (BOOL)isOpaque {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}
- (void)mouseDown:(NSEvent*)event {
    (void)event;
    self.mouseDowns = self.mouseDowns + 1U;
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    CGContextRef context = NSGraphicsContext.currentContext.CGContext;
    CGContextSetRGBFillColor(context, backdrop_rgb[0] / 255.0, backdrop_rgb[1] / 255.0, backdrop_rgb[2] / 255.0,
                             1.0);
    CGContextFillRect(context, NSRectToCGRect(self.bounds));
}
@end

namespace {

// The probe's windows, owned by the probe for its run (ARC).
struct ProbeWindows final {
    NSWindow* backdrop{};
    GFBackdropView* backdrop_view{};
};

// What one case read.
struct CaseReading final {
    Sample clear{};
    Sample clear_inactive{};
    Sample half{};
    Sample opaque{};
    Sample caption{};
    Sample own_clear{};
    Sample own_half{};
    Sample own_opaque{};
    // False when the probe had no Screen Recording grant and made no image
    // read; the samples and the two read flags are then unset.
    bool images_exercised{};
    bool composite_read{};
    bool own_read{};
    const char* window_at_clear{"none"};
    const char* click_received{"none"};
    BOOL window_opaque{};
    CGFloat background_alpha{};
    BOOL ignores_mouse_events{};
    bool has_effect{};
    NSVisualEffectMaterial material{};
    NSVisualEffectBlendingMode blending{};
    NSVisualEffectState state{};
    const char* appearance{""};
    // The window's and the application's activity when the case was read.
    bool key_window{};
    bool app_active{};
};

// Arranges the window for a base: its opacity, background and content
// view. The probe view is kept and moved between content views.
void arrange(NSWindow* window, GFProbeView* view, ProbeBase base, NSVisualEffectMaterial material) {
    const NSRect bounds = NSMakeRect(0.0, 0.0, content_width, content_height);
    window.contentView = nil;
    [view removeFromSuperview];
    view.frame = bounds;
    view.opaqueView = base == ProbeBase::none ? YES : NO;
    const bool opaque_window = base == ProbeBase::none || base == ProbeBase::effect_opaque_window;
    window.opaque = opaque_window ? YES : NO;
    window.backgroundColor = opaque_window ? NSColor.windowBackgroundColor : NSColor.clearColor;
    if (base == ProbeBase::clear_receiving) window.ignoresMouseEvents = NO;
    const bool effect = base == ProbeBase::effect || base == ProbeBase::effect_opaque_window;
    if (!effect) {
        window.contentView = view;
    } else {
        NSVisualEffectView* const backing = [[NSVisualEffectView alloc] initWithFrame:bounds];
        backing.material = material;
        backing.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        backing.state = NSVisualEffectStateFollowsWindowActiveState;
        window.contentView = backing;
        [backing addSubview:view];
    }
    view.needsDisplay = YES;
    [window display];
}

// The composed screen under and including the window, over its frame, or
// the window's own image when alone.
CGImageRef capture(NSWindow* window, bool alone) {
    const WindowListCreateImageFunction create = window_list_create_image();
    if (create == nullptr) return nullptr;
    const NSRect frame = window.frame;
    const CGFloat top = primary_screen_height() - frame.origin.y - frame.size.height;
    const CGWindowID number = static_cast<CGWindowID>(window.windowNumber);
    if (alone) {
        const CGImageRef own = create(CGRectNull, kCGWindowListOptionIncludingWindow, number,
                                      kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution);
        return own;
    }
    const CGRect rectangle = CGRectMake(frame.origin.x, top, frame.size.width, frame.size.height);
    const CGImageRef composite =
        create(rectangle, kCGWindowListOptionOnScreenBelowWindow | kCGWindowListOptionIncludingWindow, number,
               kCGWindowImageNominalResolution);
    return composite;
}

// A content point from the window frame's top-left corner, in points.
NSPoint content_point(NSWindow* window, CGFloat x, CGFloat y_from_top) {
    const NSRect content = [window contentRectForFrameRect:window.frame];
    const NSRect frame = window.frame;
    const CGFloat content_top = frame.size.height - (content.origin.y - frame.origin.y) - content.size.height;
    const NSPoint point = NSMakePoint(content.origin.x - frame.origin.x + x, content_top + y_from_top);
    return point;
}

void read_columns(NSWindow* window, bool alone, Sample& clear, Sample& half, Sample& opaque, bool& read) {
    const CGImageRef image = capture(window, alone);
    read = image != nullptr;
    if (image == nullptr) return;
    const CGFloat width = window.frame.size.width;
    const CGFloat middle = content_height / 2.0;
    const NSPoint clear_point = content_point(window, column_width / 2.0, middle);
    const NSPoint half_point = content_point(window, column_width * 1.5, middle);
    const NSPoint opaque_point = content_point(window, column_width * 2.5, middle);
    clear = sample_image(image, width, clear_point.x, clear_point.y);
    half = sample_image(image, width, half_point.x, half_point.y);
    opaque = sample_image(image, width, opaque_point.x, opaque_point.y);
    CGImageRelease(image);
}

Sample read_caption(NSWindow* window) {
    Sample caption{};
    const CGImageRef image = capture(window, false);
    if (image == nullptr) return caption;
    const CGFloat width = window.frame.size.width;
    // Right of the title, inside the title bar.
    caption = sample_image(image, width, width - 16.0, 8.0);
    CGImageRelease(image);
    return caption;
}

// The clear column's centre in AppKit's screen coordinates.
NSPoint clear_screen_point(NSWindow* window) {
    const NSRect content = [window contentRectForFrameRect:window.frame];
    const NSPoint point = NSMakePoint(content.origin.x + column_width / 2.0, content.origin.y + content_height / 2.0);
    return point;
}

// Posts a left click at an AppKit screen point through the HID event tap,
// as a user's click arrives: the window server chooses the window.
void post_click(NSPoint point) {
    const CGPoint global = CGPointMake(point.x, primary_screen_height() - point.y);
    CGEventRef const down = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseDown, global, kCGMouseButtonLeft);
    CGEventRef const up = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseUp, global, kCGMouseButtonLeft);
    if (down != nullptr) CGEventPost(kCGHIDEventTap, down);
    if (up != nullptr) CGEventPost(kCGHIDEventTap, up);
    if (down != nullptr) CFRelease(down);
    if (up != nullptr) CFRelease(up);
}

NSWindow* make_probe_window(GFProbeView* view) {
    const NSRect content = NSMakeRect(180.0, 260.0, content_width, content_height);
    const NSWindowStyleMask style =
        NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    NSWindow* const window = [[NSWindow alloc] initWithContentRect:content
                                                         styleMask:style
                                                           backing:NSBackingStoreBuffered
                                                             defer:NO];
    window.releasedWhenClosed = NO;
    window.title = @"T5 compositor probe";
    window.contentView = view;
    return window;
}

// image_reads is whether the probe may read window images:
// CGPreflightScreenCaptureAccess's answer, read once before the first case,
// unless --no-image-reads turned them off.
CaseReading run_case(const ProbeCase& probe, const ProbeWindows& windows, bool image_reads) {
    CaseReading reading{};
    reading.images_exercised = image_reads;
    GFProbeView* const view = [[GFProbeView alloc] initWithFrame:NSMakeRect(0.0, 0.0, content_width, content_height)];
    NSWindow* const window = make_probe_window(view);
    arrange(window, view, probe.first, probe.material);
    [window makeKeyAndOrderFront:nil];
    spin(0.6);
    if (probe.then != probe.first) {
        arrange(window, view, probe.then, probe.material);
        spin(0.6);
    }
    reading.key_window = window.isKeyWindow == YES;
    reading.app_active = NSApp.isActive == YES;
    if (reading.images_exercised) {
        read_columns(window, false, reading.clear, reading.half, reading.opaque, reading.composite_read);
        reading.caption = read_caption(window);
        read_columns(window, true, reading.own_clear, reading.own_half, reading.own_opaque, reading.own_read);
    }
    reading.window_opaque = window.opaque;
    reading.background_alpha = window.backgroundColor.alphaComponent;
    reading.ignores_mouse_events = window.ignoresMouseEvents;
    NSView* const content = window.contentView;
    if ([content isKindOfClass:[NSVisualEffectView class]]) {
        NSVisualEffectView* const effect = static_cast<NSVisualEffectView*>(content);
        reading.has_effect = true;
        reading.material = effect.material;
        reading.blending = effect.blendingMode;
        reading.state = effect.state;
        reading.appearance = appearance_name(effect.effectiveAppearance.name);
    }
    const NSPoint clear_point = clear_screen_point(window);
    const NSInteger under = [NSWindow windowNumberAtPoint:clear_point belowWindowWithWindowNumber:0];
    if (under == window.windowNumber) reading.window_at_clear = "probe";
    if (under == windows.backdrop.windowNumber) reading.window_at_clear = "backdrop";
    // The clear column while the backdrop window is key.
    [windows.backdrop makeKeyWindow];
    spin(0.6);
    Sample half_inactive{};
    Sample opaque_inactive{};
    bool inactive_read = false;
    if (reading.images_exercised) {
        read_columns(window, false, reading.clear_inactive, half_inactive, opaque_inactive, inactive_read);
    }
    [window makeKeyAndOrderFront:nil];
    spin(0.4);
    const unsigned probe_before = view.mouseDowns;
    const unsigned backdrop_before = windows.backdrop_view.mouseDowns;
    post_click(clear_point);
    spin(0.5);
    if (view.mouseDowns != probe_before) reading.click_received = "probe";
    if (windows.backdrop_view.mouseDowns != backdrop_before) reading.click_received = "backdrop";
    [window orderOut:nil];
    [window close];
    spin(0.2);
    return reading;
}

void print_sample(std::FILE* output, const char* name, const Sample& sample, bool with_alpha) {
    if (!sample.read) {
        std::fprintf(output, ",\"%s\":null", name);
        return;
    }
    if (with_alpha) {
        std::fprintf(output, ",\"%s\":[%d,%d,%d,%d]", name, sample.red, sample.green, sample.blue, sample.alpha);
        return;
    }
    std::fprintf(output, ",\"%s\":[%d,%d,%d]", name, sample.red, sample.green, sample.blue);
}

// The image reads of one case and the colour verdicts drawn from them.
void print_images(std::FILE* output, const CaseReading& reading) {
    std::fprintf(output, ",\"composite_read\":%s,\"own_read\":%s", reading.composite_read ? "true" : "false",
                 reading.own_read ? "true" : "false");
    print_sample(output, "clear", reading.clear, false);
    print_sample(output, "clear_inactive", reading.clear_inactive, false);
    print_sample(output, "half", reading.half, false);
    print_sample(output, "opaque", reading.opaque, false);
    print_sample(output, "caption", reading.caption, false);
    print_sample(output, "own_clear", reading.own_clear, true);
    print_sample(output, "own_half", reading.own_half, true);
    print_sample(output, "own_opaque", reading.own_opaque, true);
    const bool backdrop_shown = near_rgb(reading.clear, backdrop_rgb[0], backdrop_rgb[1], backdrop_rgb[2]);
    const bool black = near_rgb(reading.clear, 0, 0, 0);
    const bool material_shown = reading.clear.read && !backdrop_shown && !black && reading.has_effect;
    std::fprintf(output, ",\"per_pixel_alpha\":%s,\"material_shown\":%s", backdrop_shown ? "true" : "false",
                 material_shown ? "true" : "false");
}

// Prints one case. Returns false when it is a material case read in an
// inactive window, which then prints no material results.
// skipped_reason names why images were not read, for a case without them.
bool print_case(std::FILE* output, const ProbeCase& probe, const CaseReading& reading, const char* skipped_reason) {
    const bool active = reading.key_window && reading.app_active;
    std::fprintf(output,
                 "{\"probe\":\"macos_compositor\",\"case\":\"%s\",\"first\":\"%s\",\"then\":\"%s\","
                 "\"requested_material\":\"%s\",\"key_window\":%s,\"app_active\":%s",
                 probe.name, base_name(probe.first), base_name(probe.then), probe.material_name,
                 reading.key_window ? "true" : "false", reading.app_active ? "true" : "false");
    if (reading.has_effect && !active) {
        std::fprintf(output, ",\"not_exercised\":\"window inactive\"}\n");
        return false;
    }
    if (reading.images_exercised) {
        print_images(output, reading);
    } else {
        std::fprintf(output, ",\"image_reads\":\"not exercised: %s\"", skipped_reason);
    }
    std::fprintf(output,
                 ",\"window_at_clear\":\"%s\",\"click_received\":\"%s\",\"window_opaque\":%s,"
                 "\"background_alpha\":%.3f,\"ignores_mouse_events\":%s",
                 reading.window_at_clear, reading.click_received, reading.window_opaque ? "true" : "false",
                 static_cast<double>(reading.background_alpha), reading.ignores_mouse_events ? "true" : "false");
    if (reading.has_effect) {
        std::fprintf(output,
                     ",\"effect\":{\"material\":%ld,\"blending\":\"%s\",\"state\":\"%s\",\"appearance\":\"%s\"}",
                     static_cast<long>(reading.material), blending_name(reading.blending),
                     state_name(reading.state), reading.appearance);
    }
    std::fprintf(output, "}\n");
    return true;
}

// How the probe became the active application.
enum class Activation : std::uint8_t {
    // LaunchServices activated it, or the cooperative request was granted.
    activate,
    // only activateIgnoringOtherApps: did.
    ignoring_other_apps,
    // neither: every material case will report itself not exercised.
    none,
};

const char* activation_name(Activation activation) noexcept {
    switch (activation) {
    case Activation::activate: return "activate";
    case Activation::ignoring_other_apps: return "ignoring_other_apps";
    case Activation::none: return "none";
    }
    return "unknown";
}

// The cooperative request first (macOS 14 grants it only when the active
// application yields, which a launch through LaunchServices arranges);
// activateIgnoringOtherApps: only if the probe is still not active.
Activation activate_probe() {
    [NSApp activate];
    spin(0.5);
    if (NSApp.isActive == YES) return Activation::activate;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    spin(0.5);
    if (NSApp.isActive == YES) return Activation::ignoring_other_apps;
    return Activation::none;
}

void print_settings(std::FILE* output, Activation activation, bool screen_capture_access) {
    NSWorkspace* const workspace = NSWorkspace.sharedWorkspace;
    const NSOperatingSystemVersion version = NSProcessInfo.processInfo.operatingSystemVersion;
    const char* const appearance = appearance_name(NSApp.effectiveAppearance.name);
    std::fprintf(output,
                "{\"probe\":\"macos_compositor\",\"settings\":true,\"os\":\"%ld.%ld.%ld\",\"activation\":\"%s\","
                "\"bundle\":%s,"
                "\"reduce_transparency\":%s,\"increase_contrast\":%s,\"differentiate_without_colour\":%s,"
                "\"appearance\":\"%s\",\"screen_capture_access\":%s,\"accessibility_trusted\":%s,"
                "\"window_list_image\":%s,\"screens\":%lu,\"backing_scale\":%.1f}\n",
                static_cast<long>(version.majorVersion), static_cast<long>(version.minorVersion),
                static_cast<long>(version.patchVersion), activation_name(activation),
                NSBundle.mainBundle.bundleIdentifier != nil ? "true" : "false",
                workspace.accessibilityDisplayShouldReduceTransparency ? "true" : "false",
                workspace.accessibilityDisplayShouldIncreaseContrast ? "true" : "false",
                workspace.accessibilityDisplayShouldDifferentiateWithoutColor ? "true" : "false",
                appearance, screen_capture_access ? "true" : "false",
                AXIsProcessTrusted() ? "true" : "false", window_list_create_image() != nullptr ? "true" : "false",
                static_cast<unsigned long>(NSScreen.screens.count),
                static_cast<double>(NSScreen.mainScreen != nil ? NSScreen.mainScreen.backingScaleFactor : 0.0));
}

} // namespace

int main(int argc, char** argv) {
    // The output: standard output, or the file after --output.
    const char* output_path = nullptr;
    bool image_reads_requested = true;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--output") == 0 && index + 1 < argc) output_path = argv[index + 1];
        if (std::strcmp(argv[index], "--no-image-reads") == 0) image_reads_requested = false;
    }
    std::FILE* output = stdout;
    if (output_path != nullptr) {
        output = std::fopen(output_path, "w");
        if (output == nullptr) {
            std::fprintf(stderr, "the probe opens its output\n");
            return 1;
        }
    }
    int status = 0;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        const Activation activation = activate_probe();
        ProbeWindows windows{};
        const NSRect backdrop_frame = NSMakeRect(80.0, 160.0, 440.0, 360.0);
        windows.backdrop = [[NSWindow alloc] initWithContentRect:backdrop_frame
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        if (windows.backdrop == nil) {
            std::fprintf(stderr, "the probe's backdrop window is made\n");
            if (output != stdout) std::fclose(output);
            return 1;
        }
        windows.backdrop.releasedWhenClosed = NO;
        windows.backdrop_view = [[GFBackdropView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 440.0, 360.0)];
        windows.backdrop.contentView = windows.backdrop_view;
        [windows.backdrop orderFront:nil];
        spin(0.4);
        // Read once, before any CGWindowListCreateImage call: without the
        // grant the probe makes none, so the system shows no dialog.
        const bool screen_capture_access = CGPreflightScreenCaptureAccess();
        const bool image_reads = screen_capture_access && image_reads_requested;
        const char* const skipped_reason =
            image_reads_requested ? "screen recording not granted" : "image reads turned off";
        bool exercised = true;
        for (const ProbeCase& probe : probe_cases) {
            const CaseReading reading = run_case(probe, windows, image_reads);
            if (!print_case(output, probe, reading, skipped_reason)) exercised = false;
            std::fflush(output);
        }
        print_settings(output, activation, screen_capture_access);
        [windows.backdrop orderOut:nil];
        [windows.backdrop close];
        if (exercised) {
            if (!image_reads) std::fprintf(output, "not exercised: image reads (%s)\n", skipped_reason);
            std::fprintf(output, "macos compositor probe finished\n");
        } else {
            std::fprintf(output, "not exercised: window inactive\n");
            std::fprintf(stderr, "not exercised: window inactive\n");
            status = 1;
        }
    }
    if (output != stdout) std::fclose(output);
    return status;
}
