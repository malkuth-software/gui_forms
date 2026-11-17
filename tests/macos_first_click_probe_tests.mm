#import <AppKit/AppKit.h>

#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/window/window.hpp"
#include "support/macos_handoff_probe.hpp"

#include <memory>
#include <string>
#include <vector>

// Click-through (owner ruling, cp32): the first click on an inactive
// GUI.Forms window both activates it and acts on the control under the
// pointer, as on Windows (GUIFormsView acceptsFirstMouse:).
//
// The owner opens key; an owned window opens ordered front and not key.
// One posted click (a press and a release, real AppKit events) on the owned
// window's button must raise the button's mouse_down, mouse_up and clicked,
// in that order, and leave the owned window key.

namespace {
using namespace gui_forms;

constexpr const char* owner_title = "GUI.Forms first click owner";
constexpr const char* inactive_title = "GUI.Forms first click inactive";
constexpr double window_width = 240.0;
constexpr double window_height = 120.0;
// The button's bounds, top-down client units.
constexpr gui_forms::Rect button_bounds{20.0, 40.0, 120.0, 28.0};

struct FirstClickProbe final {
    handoff_probe::Report report{};
    handoff_probe::Watchdog* watchdog{};
    ApplicationWindowHandle owner_handle{};
    ApplicationWindowHandle inactive_handle{};
    std::shared_ptr<Button> button{};
    SubscriptionToken pressed{};
    SubscriptionToken released{};
    SubscriptionToken clicked{};
    // The raisers in the order they ran: 'd' press, 'u' release, 'c' click.
    std::string order{};
    bool key_before_click{};
};

struct RecordPress final {
    FirstClickProbe* probe{};
    void operator()(const MouseEventArgs&) const { (*probe).order += 'd'; }
};

struct RecordRelease final {
    FirstClickProbe* probe{};
    void operator()(const MouseEventArgs&) const { (*probe).order += 'u'; }
};

struct RecordClick final {
    FirstClickProbe* probe{};
    void operator()(ButtonBase&) const { (*probe).order += 'c'; }
};

void post_click(void* context);
void check_click(void* context);

struct InactiveReady final {
    FirstClickProbe* probe{};
    void operator()(Window&, ApplicationWindowHandle handle) const {
        (*probe).inactive_handle = handle;
        (*(*probe).watchdog).reached(2);
        // After the window is ordered front (it is not made key).
        handoff_probe::after(0.6, probe, &post_click);
    }
};

// The press and release on the button's centre, in window base coordinates
// (bottom-up), on the window that is not key.
void post_click(void* const context) {
    FirstClickProbe& probe = *static_cast<FirstClickProbe*>(context);
    (*probe.watchdog).reached(3);
    NSWindow* const native = handoff_probe::find_window(inactive_title);
    probe.report.check("the owned window is found", native != nil, "");
    if (native == nil) {
        static_cast<void>(probe.owner_handle.request_close());
        return;
    }
    probe.key_before_click = [native isKeyWindow];
    const double height = native.contentView.bounds.size.height;
    const NSPoint point = NSMakePoint(button_bounds.x + button_bounds.width / 2.0,
                                      height - (button_bounds.y + button_bounds.height / 2.0));
    const NSTimeInterval now = [[NSProcessInfo processInfo] systemUptime];
    NSEvent* const down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                             location:point
                                        modifierFlags:0
                                            timestamp:now
                                         windowNumber:native.windowNumber
                                              context:nil
                                          eventNumber:0
                                           clickCount:1
                                             pressure:1.0f];
    NSEvent* const up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                           location:point
                                      modifierFlags:0
                                          timestamp:now + 0.05
                                       windowNumber:native.windowNumber
                                            context:nil
                                        eventNumber:0
                                         clickCount:1
                                           pressure:0.0f];
    if (down != nil) [NSApp postEvent:down atStart:NO];
    if (up != nil) [NSApp postEvent:up atStart:NO];
    handoff_probe::after(0.8, context, &check_click);
}

void check_click(void* const context) {
    FirstClickProbe& probe = *static_cast<FirstClickProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(4);
    NSWindow* const native = handoff_probe::find_window(inactive_title);
    report.check("the owned window was not key before the click", !probe.key_before_click, "");
    report.check("the first click raised press, release and click in order", probe.order == "duc",
                 "order=" + probe.order);
    report.check("the first click made the owned window key", native != nil && [native isKeyWindow], "");
    probe.pressed.disconnect();
    probe.released.disconnect();
    probe.clicked.disconnect();
    const HostServiceStatus closed = probe.owner_handle.request_close();
    report.check("owner close request accepted", closed.accepted(), "");
}

struct OwnerReady final {
    FirstClickProbe* probe{};
    void operator()(Window&, ApplicationWindowHandle handle) const {
        (*probe).owner_handle = handle;
        (*(*probe).watchdog).reached(1);
        const std::shared_ptr<Panel> root = make_control<Panel>(StableId("first.click.inactive"));
        (*root).set_requested_bounds({0.0, 0.0, window_width, window_height});
        (*probe).button = make_control<Button>(StableId("first.click.button"));
        (*(*probe).button).text = "Press";
        (*(*probe).button).set_requested_bounds(button_bounds);
        (*root).add_child((*probe).button);
        (*probe).pressed = (*(*probe).button).mouse_down().subscribe(RecordPress{probe});
        (*probe).released = (*(*probe).button).mouse_up().subscribe(RecordRelease{probe});
        (*probe).clicked = (*(*probe).button).clicked().subscribe(RecordClick{probe});
        ApplicationWindow entry{};
        entry.stable_id = "first.click.inactive";
        entry.owner_id = "first.click.owner";
        entry.model = std::make_unique<Window>(root, gui_forms::Size{window_width, window_height});
        entry.options.title = inactive_title;
        entry.options.initial_size = {window_width, window_height};
        entry.options.ready = InactiveReady{probe};
        const ApplicationWindowOpenResult opened = handle.windows().open_window(std::move(entry));
        (*probe).report.check("the owned window opened", opened.status.accepted(), "");
    }
};

} // namespace

int main() {
    handoff_probe::Watchdog watchdog(60);
    FirstClickProbe probe{};
    probe.watchdog = &watchdog;
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    probe.report.check("application created", static_cast<bool>(created), "");
    if (created) {
        const std::shared_ptr<Panel> root = make_control<Panel>(StableId("first.click.owner"));
        (*root).set_requested_bounds({0.0, 0.0, 320.0, 200.0});
        ApplicationWindow owner{};
        owner.stable_id = "first.click.owner";
        owner.model = std::make_unique<Window>(root, gui_forms::Size{320.0, 200.0});
        owner.options.title = owner_title;
        owner.options.initial_size = {320.0, 200.0};
        owner.options.ready = OwnerReady{&probe};
        std::vector<ApplicationWindow> windows;
        windows.push_back(std::move(owner));
        const ApplicationResult result = (*created.application).run(std::move(windows));
        probe.report.check("run accepted", result.accepted(),
                           "error=" + std::to_string(static_cast<int>(result.error)));
        probe.report.check("no callback exception", !result.callback_exception, "");
    }
    probe.button.reset();
    watchdog.finish();
    probe.report.finish();
    const int status = probe.report.failures() == 0U ? 0 : 1;
    return status;
}
