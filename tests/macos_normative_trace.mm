#import <AppKit/AppKit.h>

#include "gui_forms/platform/macos_host.hpp"
#include "support/normative_trace.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// The normative trace on the macOS host (release-1.0 condition 2;
// tests/support/normative_trace.hpp): the scene in a real AppKit window,
// each step injected as NSEvents posted to the process's own queue (no
// Accessibility grant), and the trace written to the path given with
// --output, or, by default (LaunchServices passes no arguments, see
// cmake/run_macos_activated_test.cmake), to normative/macos.trace beside
// the bundle. tools/normative_trace.py compare checks it against the
// headless reference at the window's backing scale. The last output line is
// "failures=0" when the trace was written.
//
// Native events per step: a move is a mouse-moved event; a click a left
// mouse down and up; the wheel one line-unit scroll event (a detented
// wheel, no precise deltas), sent to the window (see post_wheel_line); a
// key its key down and up with AppKit's virtual key code, characters
// included for a typed key, so the text arrives through the view's own
// text input path.

namespace {

constexpr const char* window_title = "GUI.Forms normative trace";

NSWindow* scene_window() {
    NSString* const title = [NSString stringWithUTF8String:window_title];
    for (NSWindow* candidate in [NSApp windows]) {
        if ([[candidate title] isEqualToString:title]) return candidate;
    }
    return nil;
}

// AppKit's virtual key code for the HID usages the script uses; 0xFFFF for
// any other.
unsigned short mac_key_code(const std::uint32_t physical_key) {
    if (physical_key == gui_forms::PhysicalKey::tab) return 48U;
    if (physical_key == gui_forms::PhysicalKey::a) return 0U;
    return 0xFFFFU;
}

// The point in window coordinates for a point in the scene's (flipped)
// view coordinates.
NSPoint window_point(NSWindow* const window, const gui_forms::Point position) {
    NSView* const view = [window contentView];
    const NSPoint local = NSMakePoint(position.x, position.y);
    if ([view isFlipped]) return [view convertPoint:local toView:nil];
    const NSPoint flipped = NSMakePoint(position.x, NSHeight([view bounds]) - position.y);
    return [view convertPoint:flipped toView:nil];
}

void post_mouse(NSWindow* const window, const NSEventType type, const NSPoint location) {
    NSEvent* const event = [NSEvent mouseEventWithType:type
                                              location:location
                                         modifierFlags:0
                                             timestamp:[[NSProcessInfo processInfo] systemUptime]
                                          windowNumber:[window windowNumber]
                                               context:nil
                                           eventNumber:0
                                            clickCount:1
                                              pressure:type == NSEventTypeLeftMouseDown ? 1.0F : 0.0F];
    if (event != nil) [NSApp postEvent:event atStart:NO];
}

void post_key(NSWindow* const window, const NSEventType type, const unsigned short key_code, NSString* const text) {
    NSEvent* const event = [NSEvent keyEventWithType:type
                                            location:NSZeroPoint
                                       modifierFlags:0
                                           timestamp:[[NSProcessInfo processInfo] systemUptime]
                                        windowNumber:[window windowNumber]
                                             context:nil
                                          characters:text
                         charactersIgnoringModifiers:text
                                           isARepeat:NO
                                             keyCode:key_code];
    if (event != nil) [NSApp postEvent:event atStart:NO];
}

// One line down from a detented wheel at `location` (window
// coordinates): a line-unit scroll event made through Core Graphics
// (NSEvent has no public scroll-event constructor), sent to the window.
//
// The clicks and keys are NSEvents made for the window (windowNumber and
// locationInWindow), posted to the own queue. A Core Graphics event cannot
// be made for a window: eventWithCGEvent: leaves it on window 0 even with
// the window-under-pointer fields set (seen on the M4 and the macos-15
// runner), NSApplication then drops it from the queue, and its
// locationInWindow is the global point in AppKit's screen space. So the
// global point is chosen to equal `location`: Core Graphics puts the
// origin at the top left of the primary display, AppKit at its bottom
// left, hence the flipped y. The event then reads `location` as its
// locationInWindow, and is delivered through the window's own dispatch,
// [window sendEvent:], which hit-tests the content view at that point and
// calls the view's scrollWheel:, synchronously inside this step. A made
// event not at `location` is reported on stderr, so the comparison's
// failure names it.
void post_wheel_line(NSWindow* const window, const NSPoint location) {
    CGEventRef const scroll = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1U, -1);
    if (scroll == nullptr) {
        std::fprintf(stderr, "macos normative trace: no scroll event could be made\n");
        return;
    }
    const CGFloat primary_height = NSMaxY([[[NSScreen screens] firstObject] frame]);
    CGEventSetLocation(scroll, CGPointMake(location.x, primary_height - location.y));
    NSEvent* const event = [NSEvent eventWithCGEvent:scroll];
    CFRelease(scroll);
    if (event == nil) {
        std::fprintf(stderr, "macos normative trace: the scroll event has no NSEvent form\n");
        return;
    }
    const NSPoint placed = [event locationInWindow];
    const bool at_location = std::fabs(placed.x - location.x) < 0.5 && std::fabs(placed.y - location.y) < 0.5;
    if (!at_location) {
        std::fprintf(stderr,
                     "macos normative trace: the scroll event is at (%.1f, %.1f) on window %ld, "
                     "not at (%.1f, %.1f) for window %ld\n",
                     static_cast<double>(placed.x), static_cast<double>(placed.y),
                     static_cast<long>([event windowNumber]), static_cast<double>(location.x),
                     static_cast<double>(location.y), static_cast<long>([window windowNumber]));
    }
    [window sendEvent:event];
}

// NormativeInjector's target: the step's native events. Context unused.
void inject_step(void* const context, const gui_forms::tests::NormativeStep& step) {
    static_cast<void>(context);
    NSWindow* const window = scene_window();
    if (window == nil) return;
    switch (step.kind) {
    case gui_forms::tests::NormativeStepKind::pointer_move:
        post_mouse(window, NSEventTypeMouseMoved, window_point(window, step.position));
        break;
    case gui_forms::tests::NormativeStepKind::click: {
        const NSPoint location = window_point(window, step.position);
        post_mouse(window, NSEventTypeLeftMouseDown, location);
        post_mouse(window, NSEventTypeLeftMouseUp, location);
        break;
    }
    case gui_forms::tests::NormativeStepKind::wheel_down:
        post_wheel_line(window, window_point(window, step.position));
        break;
    case gui_forms::tests::NormativeStepKind::key:
    case gui_forms::tests::NormativeStepKind::typed: {
        NSString* const text = step.kind == gui_forms::tests::NormativeStepKind::typed
                                   ? [NSString stringWithUTF8String:step.text]
                                   : (step.physical_key == gui_forms::PhysicalKey::tab ? @"\t" : @"");
        const unsigned short key_code = mac_key_code(step.physical_key);
        post_key(window, NSEventTypeKeyDown, key_code, text);
        post_key(window, NSEventTypeKeyUp, key_code, text);
        break;
    }
    default:
        break;
    }
}

// The run's state, owned by main for the whole run; the driver is made in
// host_ready, once the window and its close request exist.
struct RunState final {
    gui_forms::tests::NormativeScene* scene{};
    gui_forms::Window* window{};
    std::string output{};
    std::unique_ptr<gui_forms::tests::NormativeDriver> driver{};
};

using VoidCallback = std::function<void()>;
using ShowDialogCallback = std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)>;
using ReadClipboardCallback = std::function<gui_forms::HostClipboardTextResult()>;
using WriteClipboardCallback = std::function<gui_forms::HostServiceStatus(std::string_view)>;

class StartDriver final {
public:
    explicit StartDriver(RunState& state) noexcept : state_(state) {}

    void operator()(VoidCallback, VoidCallback request_close, ShowDialogCallback, ReadClipboardCallback,
                    WriteClipboardCallback) const {
        NSWindow* const window = scene_window();
        const double scale = window == nil ? [[NSScreen mainScreen] backingScaleFactor] : [window backingScaleFactor];
        (*state_.scene).begin(scale);
        gui_forms::tests::NormativeInjector injector{};
        injector.inject = &inject_step;
        state_.driver = std::make_unique<gui_forms::tests::NormativeDriver>(
            *state_.scene, *state_.window, injector, state_.output, std::move(request_close));
        (*state_.driver).start();
    }

private:
    RunState& state_;
};

} // namespace

int main(int argc, char** argv) {
    RunState state{};
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == "--output") state.output = argv[index + 1];
    }
    if (state.output.empty()) {
        NSString* const beside = [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent];
        NSString* const directory = [beside stringByAppendingPathComponent:@"normative"];
        [[NSFileManager defaultManager] createDirectoryAtPath:directory
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        state.output = std::string([[directory stringByAppendingPathComponent:@"macos.trace"] UTF8String]);
    }
    // Declared before the window it records, which the host destroys
    // before run_macos returns.
    gui_forms::tests::NormativeScene scene;
    std::unique_ptr<gui_forms::Window> window = scene.make_window();
    state.scene = &scene;
    state.window = window.get();
    gui_forms::host::MacHostOptions options;
    options.title = window_title;
    options.initial_size = gui_forms::tests::normative_window_size;
    options.print_metrics_on_close = false;
    options.host_ready = StartDriver(state);
    const int result = gui_forms::host::run_macos(std::move(window), std::move(options));
    const bool written = state.driver != nullptr && (*state.driver).written();
    state.driver.reset();
    if (!written) {
        std::printf("macos normative trace: the trace was not written (host result %d)\nfailures=1\n", result);
        return 1;
    }
    std::printf("macos normative trace written to %s\nfailures=%d\n", state.output.c_str(), result == 0 ? 0 : 1);
    return result == 0 ? 0 : 1;
}
