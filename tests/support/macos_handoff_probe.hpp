#pragma once

// Shared pieces of the macOS native readback probes (the Windows
// coordinator's Mac handoff of 2026-11-09, item 2): a report of check lines,
// a watchdog, delayed main-queue steps, native window lookup, synthesized key
// events and a PNG of a view's raster. Objective-C++ only; AppKit types stay
// in the probe programs, which are macOS-only tests.
//
// The probes run through cmake/run_macos_activated_test.cmake (LaunchServices,
// so their windows can become key). It passes a run only when standard output
// ends with "failures=0".

#import <AppKit/AppKit.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

namespace handoff_probe {

// The check lines of one run, printed as they are made: "PASS name: detail"
// or "FAIL name: detail", then "failures=N" from finish(). Main thread only.
class Report final {
public:
    void check(const char* const name, const bool passed, const std::string& detail) {
        if (!passed) ++failures_;
        const char* const verdict = passed ? "PASS" : "FAIL";
        std::printf("%s %s: %s\n", verdict, name, detail.c_str());
        std::fflush(stdout);
    }
    void note(const std::string& line) {
        std::printf("NOTE %s\n", line.c_str());
        std::fflush(stdout);
    }
    [[nodiscard]] std::size_t failures() const noexcept { return failures_; }
    void finish() {
        std::printf("failures=%zu\n", failures_);
        std::fflush(stdout);
    }

private:
    std::size_t failures_{};
};

// The watchdog thread's body: waits until `done` or the limit, and on the
// limit prints the step it stopped at and ends the process, so a run loop
// that never reaches the next step fails instead of hanging the launcher.
class WatchdogWait final {
public:
    WatchdogWait(const std::atomic<bool>& done, const std::atomic<int>& step, const int seconds) noexcept
        : done_(done), step_(step), seconds_(seconds) {}

    void operator()() const {
        const int ticks = seconds_ * 10;
        for (int tick = 0; tick < ticks; ++tick) {
            if (done_.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (done_.load()) return;
        std::printf("FAIL watchdog: no progress after %d s, at step %d\nfailures=1\n", seconds_, step_.load());
        std::fflush(stdout);
        _exit(3);
    }

private:
    const std::atomic<bool>& done_;
    const std::atomic<int>& step_;
    int seconds_;
};

// Owns the watchdog thread; finish() stops and joins it. Declared before
// everything the probe's steps use, so it is destroyed last.
class Watchdog final {
public:
    explicit Watchdog(const int seconds) : thread_(WatchdogWait(done_, step_, seconds)) {}
    ~Watchdog() { finish(); }
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    void reached(const int step) noexcept { step_.store(step); }
    void finish() {
        done_.store(true);
        if (thread_.joinable()) thread_.join();
    }

private:
    std::atomic<bool> done_{false};
    std::atomic<int> step_{0};
    std::thread thread_;
};

// Runs function(context) on the main queue after `seconds`. The context is
// borrowed: the probe's state outlives the run that executes the step.
inline void after(const double seconds, void* const context, const dispatch_function_t function) {
    const std::int64_t nanoseconds = static_cast<std::int64_t>(seconds * 1.0e9);
    const dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, nanoseconds);
    dispatch_after_f(when, dispatch_get_main_queue(), context, function);
}

// The application's native window whose title is `title`, or nil.
inline NSWindow* find_window(const char* const title) {
    NSString* const wanted = [NSString stringWithUTF8String:title];
    for (NSWindow* window in [NSApp windows]) {
        if ([window.title isEqualToString:wanted]) return window;
    }
    return nil;
}

// The GUI.Forms raster view in `view`'s tree (class GUIFormsView), or nil.
inline NSView* find_raster_view(NSView* const view) {
    if (view == nil) return nil;
    if ([NSStringFromClass([view class]) isEqualToString:@"GUIFormsView"]) return view;
    for (NSView* child in view.subviews) {
        NSView* const found = find_raster_view(child);
        if (found != nil) return found;
    }
    return nil;
}

inline std::string describe(const NSRect rect) {
    char text[128];
    std::snprintf(text, sizeof text, "{%.1f, %.1f, %.1f x %.1f}", rect.origin.x, rect.origin.y,
                  rect.size.width, rect.size.height);
    const std::string described(text);
    return described;
}

inline std::string describe_object(id const object) {
    if (object == nil) return std::string("nil");
    NSString* const name = NSStringFromClass([object class]);
    const std::string described([name UTF8String]);
    return described;
}

// A key press for `window`, posted through NSApp as the window server's
// events are (no Accessibility permission: the event stays in process).
inline void send_key(NSWindow* const window, NSString* const characters, const unsigned short key_code,
                     const NSEventModifierFlags modifiers) {
    const NSTimeInterval now = [[NSProcessInfo processInfo] systemUptime];
    NSEvent* const down = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                           location:NSZeroPoint
                                      modifierFlags:modifiers
                                          timestamp:now
                                       windowNumber:window.windowNumber
                                            context:nil
                                         characters:characters
                        charactersIgnoringModifiers:characters
                                          isARepeat:NO
                                            keyCode:key_code];
    NSEvent* const up = [NSEvent keyEventWithType:NSEventTypeKeyUp
                                         location:NSZeroPoint
                                    modifierFlags:modifiers
                                        timestamp:now
                                     windowNumber:window.windowNumber
                                          context:nil
                                       characters:characters
                      charactersIgnoringModifiers:characters
                                        isARepeat:NO
                                          keyCode:key_code];
    [NSApp sendEvent:down];
    [NSApp sendEvent:up];
}

// Writes what AppKit draws for `view` (its cached display: the host's
// raster) to a PNG at `path`. False when the bitmap or the file fails.
inline bool write_view_png(NSView* const view, const std::string& path) {
    bool written = false;
    @autoreleasepool {
        NSBitmapImageRep* const bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        if (bitmap != nil) {
            [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
            NSData* const png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            NSString* const file = [NSString stringWithUTF8String:path.c_str()];
            written = png != nil && [png writeToFile:file atomically:YES];
        }
    }
    return written;
}

// The directory beside the running bundle, for files a probe leaves behind.
inline std::string beside_bundle(const char* const name) {
    NSString* const parent = [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent];
    NSString* const directory = [parent stringByAppendingPathComponent:[NSString stringWithUTF8String:name]];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    const std::string path([directory UTF8String]);
    return path;
}

} // namespace handoff_probe
