#import <AppKit/AppKit.h>

#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/window/window.hpp"
#include "support/macos_handoff_probe.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

// Window properties written before Application::run, read back from the
// NSWindow AppKit made (Mac handoff 2026-11-09, item 2a; the Win32 twin is
// check_chrome_before_run in application_native_tests.cpp): the title is the
// window text, a fixed dialog has no resizable style and a disabled zoom
// button, top_most is the floating level and center_screen places the frame
// where [NSWindow center] does. The host's report of what it applied is the
// written set less what it does not support, and it reports Icon and
// ShowInTaskbar as unsupported.

namespace {
using namespace gui_forms;

constexpr const char* window_text = "GUI.Forms chrome written before the run";

struct ChromeProbe final {
    handoff_probe::Report report{};
    handoff_probe::Watchdog* watchdog{};
    Window* window{};
    ApplicationWindowHandle handle{};
    NSRect ready_frame{};
};

std::string bits_text(const WindowChromeFields fields) {
    char text[32];
    std::snprintf(text, sizeof text, "0x%03x", fields.bits);
    const std::string described(text);
    return described;
}

// Reads the native window once it is on screen, then closes it.
void read_native_chrome(void* const context) {
    ChromeProbe& probe = *static_cast<ChromeProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(2);
    Window& window = *probe.window;

    const WindowChromeFields written = window.written_chrome();
    const WindowChromeFields applied = window.applied_chrome();
    HostServices* const services = window.host_services();
    report.check("host services attached", services != nullptr, "");
    WindowChromeFields supported{};
    if (services != nullptr) supported = (*services).supported_window_chrome();
    report.check("supported chrome reports Icon unsupported", !supported.contains(WindowChromeField::icon),
                 "supported=" + bits_text(supported));
    report.check("supported chrome reports ShowInTaskbar unsupported",
                 !supported.contains(WindowChromeField::show_in_taskbar), "supported=" + bits_text(supported));
    const bool all_written = written.contains(WindowChromeField::text) &&
                             written.contains(WindowChromeField::form_border_style) &&
                             written.contains(WindowChromeField::maximize_box) &&
                             written.contains(WindowChromeField::top_most) &&
                             written.contains(WindowChromeField::start_position);
    report.check("written chrome holds the five properties", all_written, "written=" + bits_text(written));
    const WindowChromeFields expected{written.bits & supported.bits};
    report.check("applied == written less unsupported", applied == expected,
                 "written=" + bits_text(written) + " applied=" + bits_text(applied) + " expected=" +
                     bits_text(expected));

    NSWindow* const native = handoff_probe::find_window(window_text);
    std::string titles;
    for (NSWindow* each in [NSApp windows]) titles += std::string(" '") + [each.title UTF8String] + "'";
    report.check("NSWindow title is the window text", native != nil, "windows:" + titles);
    if (native != nil) {
        const NSWindowStyleMask mask = native.styleMask;
        char mask_text[48];
        std::snprintf(mask_text, sizeof mask_text, "styleMask=0x%llx",
                      static_cast<unsigned long long>(mask));
        report.check("styleMask has no Resizable", (mask & NSWindowStyleMaskResizable) == 0, mask_text);
        NSButton* const zoom = [native standardWindowButton:NSWindowZoomButton];
        const std::string zoom_state = zoom == nil ? "absent" : (zoom.enabled ? "enabled" : "disabled");
        report.check("zoom button disabled", zoom != nil && !zoom.enabled, "zoom " + zoom_state);
        report.check("level == NSFloatingWindowLevel", native.level == NSFloatingWindowLevel,
                     "level=" + std::to_string(static_cast<long>(native.level)) +
                         " floating=" + std::to_string(static_cast<long>(NSFloatingWindowLevel)));
        const NSRect placed = native.frame;
        [native center];
        const NSRect centred = native.frame;
        [native setFrame:placed display:NO];
        const double dx = std::fabs(placed.origin.x - centred.origin.x);
        const double dy = std::fabs(placed.origin.y - centred.origin.y);
        const NSRect visible = native.screen == nil ? NSZeroRect : native.screen.visibleFrame;
        // Measured 2026-11-09: once the window is visible, [NSWindow center]
        // answers 2 points lower than it did for the same hidden window, so
        // the placement is checked where the start position applies, before
        // the first show (ChromeReady), and here only that showing it kept
        // that frame.
        report.note("after show: center would give " + handoff_probe::describe(centred) +
                    " (dx=" + std::to_string(dx) + " dy=" + std::to_string(dy) + ")");
        const bool kept = NSEqualRects(placed, probe.ready_frame) == YES;
        report.check("first show kept the centred frame", kept,
                     "at ready=" + handoff_probe::describe(probe.ready_frame) + " shown=" +
                         handoff_probe::describe(placed) + " visibleFrame=" + handoff_probe::describe(visible));
        report.check("NSWindow is visible", native.visible == YES, "");
    }
    const HostServiceStatus closed = probe.handle.request_close();
    report.check("close request accepted", closed.accepted(), "");
}

struct ChromeReady final {
    ChromeProbe* probe{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        (*probe).window = &window;
        (*probe).handle = handle;
        (*(*probe).watchdog).reached(1);
        // The frame as the host left it at attach, before the first show,
        // and where [NSWindow center] would put it then; the check itself
        // reads the window once it is on screen.
        NSWindow* const native = handoff_probe::find_window(window_text);
        if (native != nil) {
            const NSRect placed = native.frame;
            [native center];
            const NSRect centred = native.frame;
            [native setFrame:placed display:NO];
            (*probe).ready_frame = placed;
            const bool same = NSEqualRects(placed, centred) == YES;
            (*probe).report.check("frame centred as [NSWindow center] places it (before the first show)", same,
                                  "visible=" + std::to_string(native.visible ? 1 : 0) + " frame=" +
                                      handoff_probe::describe(placed) + " center=" +
                                      handoff_probe::describe(centred));
        }
        handoff_probe::after(0.5, probe, &read_native_chrome);
    }
};

} // namespace

int main() {
    handoff_probe::Watchdog watchdog(45);
    ChromeProbe probe{};
    probe.watchdog = &watchdog;
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    probe.report.check("application created", static_cast<bool>(created), "");
    if (!created) {
        watchdog.finish();
        probe.report.finish();
        return 1;
    }
    const std::shared_ptr<Control> root = make_control<Control>(StableId("chrome.probe.root"));
    std::unique_ptr<Window> window = std::make_unique<Window>(root, gui_forms::Size{360.0, 240.0});
    (*window).text = window_text;
    (*window).form_border_style = FormBorderStyle::fixed_dialog;
    (*window).maximize_box = false;
    (*window).top_most = true;
    (*window).start_position = FormStartPosition::center_screen;
    ApplicationWindowOptions options{};
    options.title = "not the window text";
    options.initial_size = {360.0, 240.0};
    options.ready = ChromeReady{&probe};
    const ApplicationResult result = (*created.application).run(std::move(window), std::move(options));
    probe.report.check("run accepted", result.accepted(),
                       "error=" + std::to_string(static_cast<int>(result.error)));
    probe.report.check("no callback exception", !result.callback_exception, "");
    watchdog.finish();
    probe.report.finish();
    const int status = probe.report.failures() == 0U ? 0 : 1;
    return status;
}
