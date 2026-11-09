// Stage DK phase W0: runtime windows, frames in desktop logical units, the
// screen pointer, the window under a point and ordering, on the headless
// reference host (decisions 6 and 7); modal windows (show_modal).
#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/host.hpp"
#include "application/headless_application.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool contains(const std::string& text, const char* fragment) {
    const bool found = text.find(fragment) != std::string::npos;
    return found;
}

std::unique_ptr<Window> make_model(const char* root_id, Size size = {320.0, 240.0}) {
    const std::shared_ptr<Control> root = make_control<Control>(StableId(std::string(root_id)));
    std::unique_ptr<Window> model = std::make_unique<Window>(root, size);
    return model;
}

ApplicationWindow make_entry(const char* id, const char* owner = "", bool tool = false,
                             Size size = {320.0, 240.0}) {
    ApplicationWindow entry{};
    entry.stable_id = id;
    entry.owner_id = owner;
    entry.tool_window = tool;
    entry.model = make_model("root", size);
    entry.options.title = id;
    entry.options.initial_size = size;
    entry.options.minimum_size = {100.0, 80.0};
    return entry;
}

// Records every callback a window receives, in order.
struct Journal final {
    std::string lines{};
    void add(const std::string& line) {
        lines += line;
        lines += '\n';
    }
};

struct ReadyProbe final {
    Journal* journal{};
    std::string name{};
    ApplicationWindowHandle* keep{};
    void operator()(Window&, ApplicationWindowHandle handle) const {
        (*journal).add("ready " + name);
        if (keep != nullptr) *keep = handle;
    }
};
struct ClosedProbe final {
    Journal* journal{};
    std::string name{};
    void operator()() const { (*journal).add("closed " + name); }
};
struct FrameProbe final {
    Journal* journal{};
    std::string name{};
    std::vector<HostWindowFrameEvent>* events{};
    void operator()(const HostWindowFrameEvent& event) const {
        (*journal).add("frame " + name);
        if (events != nullptr) (*events).push_back(event);
    }
};

void observe(ApplicationWindow& entry, Journal& journal, ApplicationWindowHandle* keep = nullptr,
             std::vector<HostWindowFrameEvent>* frames = nullptr) {
    entry.options.ready = ReadyProbe{&journal, entry.stable_id, keep};
    entry.options.closed = ClosedProbe{&journal, entry.stable_id};
    entry.options.frame_changed = FrameProbe{&journal, entry.stable_id, frames};
}

std::vector<ApplicationWindow> main_and_tool(Journal& journal, ApplicationWindowHandle* main,
                                             ApplicationWindowHandle* tool) {
    std::vector<ApplicationWindow> windows;
    ApplicationWindow primary = make_entry("main", "", false, {800.0, 600.0});
    observe(primary, journal, main);
    windows.push_back(std::move(primary));
    ApplicationWindow palette = make_entry("palette", "main", true, {200.0, 300.0});
    observe(palette, journal, tool);
    windows.push_back(std::move(palette));
    return windows;
}

void test_desktop_mapping() {
    // A scale-2 primary monitor 3840 device pixels wide, and a scale-1
    // monitor to its right.
    std::vector<HostMonitor> device;
    device.push_back({"left", {0.0, 0.0, 3840.0, 2160.0}, {0.0, 0.0, 3840.0, 2080.0}, 2.0, true});
    device.push_back({"right", {3840.0, 0.0, 1920.0, 1080.0}, {3840.0, 0.0, 1920.0, 1040.0}, 1.0, false});
    const HostMonitor left = logical_monitor(device[0]);
    require(left.frame == Rect{0.0, 0.0, 1920.0, 1080.0}, "scaled monitor extent not divided");
    require(left.work_area == Rect{0.0, 0.0, 1920.0, 1040.0}, "scaled work area not divided");
    const HostMonitor right = logical_monitor(device[1]);
    require(right.frame == Rect{3840.0, 0.0, 1920.0, 1080.0}, "unscaled monitor changed");
    const DesktopLogicalPoint inside = desktop_logical_point(device, {1000.0, 500.0});
    require(inside.x == 500.0 && inside.y == 250.0, "device point on a scaled monitor not mapped");
    const DesktopLogicalPoint beside = desktop_logical_point(device, {4000.0, 100.0});
    require(beside.x == 4000.0 && beside.y == 100.0, "device point on an unscaled monitor changed");
    double scale = 0.0;
    const DesktopDevicePoint back = desktop_device_point(device, {500.0, 250.0}, scale);
    require(back.x == 1000.0 && back.y == 500.0 && scale == 2.0, "logical point did not map back");
    // A logical point in the gap between the two logical monitors maps
    // through the nearest one.
    const DesktopDevicePoint gap = desktop_device_point(device, {3000.0, 100.0}, scale);
    require(scale == 1.0 && gap.x == 3000.0, "gap point did not use the nearest monitor");
    require(nearest_device_monitor(device, {-50.0, 10.0}) == 0U, "nearest monitor wrong left of the desktop");
    require(nearest_device_monitor(device, {9000.0, 10.0}) == 1U, "nearest monitor wrong right of the desktop");
    const std::vector<HostMonitor> none;
    require(desktop_logical_point(none, {7.0, 9.0}) == DesktopLogicalPoint{7.0, 9.0}, "empty monitor list changed a point");
}

void test_initial_windows_are_admitted_in_ownership_order() {
    Journal journal;
    ApplicationWindowHandle main;
    ApplicationWindowHandle tool;
    std::vector<ApplicationWindow> windows;
    // Listed before its owner: the run still admits the owner first.
    ApplicationWindow palette = make_entry("palette", "main", true, {200.0, 300.0});
    observe(palette, journal, &tool);
    windows.push_back(std::move(palette));
    ApplicationWindow primary = make_entry("main", "", false, {800.0, 600.0});
    observe(primary, journal, &main);
    windows.push_back(std::move(primary));
    host::HeadlessApplication application(std::move(windows));
    HostWindowing& windowing = application.windowing();
    require(main.id().value == 1U && tool.id().value == 2U, "identities not assigned in ownership order");
    require(windowing.kind(main.id()) == HostWindowKind::primary, "primary kind lost");
    require(windowing.kind(tool.id()) == HostWindowKind::tool, "tool kind lost");
    require(windowing.owner(tool.id()) == main.id(), "owner lost");
    require(windowing.primary() == main.id(), "primary identity lost");
    require(main.windows().window_count() == 2U, "window count wrong");
    // Frames reported before a window is ready are not published to it.
    require(journal.lines == "ready main\nready palette\n", "initial callback order changed");
    const std::string& trace = application.windowing().trace();
    require(trace.find("create id=1 stable=main") < trace.find("show id=1") &&
            trace.find("show id=1") < trace.find("create id=2 stable=palette"),
            "initial window creation order wrong");
    const HostWindowingSnapshot snapshot = windowing.snapshot();
    require(snapshot.started && snapshot.live_windows == 2U && snapshot.admitted == 2U,
            "windowing snapshot wrong after start");
    require(contains(snapshot.to_json(), "\"live_windows\":2"), "snapshot json lacks live windows");
}

void test_runtime_open_and_destroy() {
    Journal journal;
    ApplicationWindowHandle main;
    ApplicationWindowHandle tool;
    host::HeadlessApplication application(main_and_tool(journal, &main, &tool));
    const ApplicationWindows windows = main.windows();
    require(windows.active(), "run windowing inactive");
    journal.lines.clear();

    ApplicationWindowHandle floating;
    ApplicationWindow entry = make_entry("float.1", "main", true, {240.0, 180.0});
    entry.options.initially_visible = false;
    observe(entry, journal, &floating);
    const ApplicationWindowOpenResult opened = windows.open_window(std::move(entry));
    require(opened.status.accepted(), "runtime open refused");
    require(opened.window.active() && floating.active(), "runtime window not ready on return");
    require(journal.lines == "ready float.1\n", "runtime window callbacks wrong");
    const HostWindowId id = opened.window.id();
    require(id.value == 3U, "runtime identity not the next identity");
    require(!application.windowing().native_visible(id), "hidden runtime window shown");
    require(contains(application.windowing().trace(), "open id=3 kind=tool owner=1 visible=0 runtime=1"),
            "runtime open missing from the trace");
    require(windows.window_count() == 3U, "runtime window not counted");
    require(opened.window.show().accepted() && application.windowing().native_visible(id),
            "runtime window did not show");

    // Destroying runs the closed callback before returning and retires the
    // identity; the handle stops working.
    journal.lines.clear();
    require(windows.destroy_window(opened.window).accepted(), "destroy refused");
    require(journal.lines == "closed float.1\n", "destroy did not run the closed callback");
    require(!opened.window.active() && !application.windowing().live(id), "destroyed window still live");
    require(opened.window.show().error == HostServiceError::after_shutdown,
            "destroyed handle still accepts requests");
    require(windows.destroy_window(opened.window).error == HostServiceError::after_shutdown,
            "second destroy accepted");
    require(windows.window_count() == 2U, "destroyed window still counted");
    application.reclaim();

    // A later window never receives the retired identity, even with the
    // same stable id.
    ApplicationWindow again = make_entry("float.1", "main", true);
    const ApplicationWindowOpenResult reopened = windows.open_window(std::move(again));
    require(reopened.status.accepted() && reopened.window.id().value == 4U,
            "identity reused after destroy");

    // The primary window, and an owner of live windows, are refused.
    require(windows.destroy_window(main).error == HostServiceError::invalid_argument,
            "primary destroyed through the windowing seam");
    ApplicationWindow owner = make_entry("doc.1", "main");
    const ApplicationWindowOpenResult document = windows.open_window(std::move(owner));
    require(document.status.accepted(), "owned standard window refused");
    ApplicationWindow child = make_entry("doc.1.tool", "doc.1", true);
    require(windows.open_window(std::move(child)).status.accepted(), "window owned by a runtime window refused");
    require(windows.destroy_window(document.window).error == HostServiceError::invalid_argument,
            "owner of a live window destroyed");
}

void test_runtime_open_refusals_leave_the_entry() {
    Journal journal;
    ApplicationWindowHandle main;
    host::HeadlessApplication application(main_and_tool(journal, &main, nullptr));
    const ApplicationWindows windows = main.windows();

    ApplicationWindow orphan = make_entry("orphan", "missing", true);
    require(windows.open_window(std::move(orphan)).status.error == HostServiceError::invalid_argument,
            "unknown owner accepted");
    require(orphan.model != nullptr, "refused entry consumed");
    ApplicationWindow second_primary = make_entry("second");
    require(windows.open_window(std::move(second_primary)).status.error ==
                HostServiceError::invalid_argument,
            "second primary accepted at run time");
    require(second_primary.model != nullptr, "refused primary consumed");
    ApplicationWindow duplicate = make_entry("palette", "main", true);
    require(windows.open_window(std::move(duplicate)).status.error == HostServiceError::invalid_argument,
            "duplicate stable id accepted");
    ApplicationWindow empty = make_entry("empty", "main", true);
    empty.model.reset();
    require(windows.open_window(std::move(empty)).status.error == HostServiceError::invalid_argument,
            "entry without a model accepted");

    // The bound: 64 live windows in all.
    std::size_t opened = windows.window_count();
    for (std::size_t index = 0U; opened < HostWindowing::maximum_windows; ++index) {
        ApplicationWindow entry = make_entry(("bulk." + std::to_string(index)).c_str(), "main", true);
        require(windows.open_window(std::move(entry)).status.accepted(), "window below the bound refused");
        opened = windows.window_count();
    }
    ApplicationWindow over = make_entry("over", "main", true);
    require(windows.open_window(std::move(over)).status.error == HostServiceError::too_large,
            "65th live window accepted");
    require(over.model != nullptr, "window over the bound consumed");
    require(windows.window_count() == HostWindowing::maximum_windows, "bound changed the table");
}

void test_frames_and_frame_requests() {
    Journal journal;
    ApplicationWindowHandle main;
    ApplicationWindowHandle tool;
    std::vector<HostWindowFrameEvent> frames;
    std::vector<ApplicationWindow> windows;
    ApplicationWindow primary = make_entry("main", "", false, {800.0, 600.0});
    observe(primary, journal, &main, &frames);
    windows.push_back(std::move(primary));
    host::HeadlessApplication application(std::move(windows));

    const HostWindowFrameResult initial = main.frame();
    require(initial.status.accepted(), "frame refused");
    require(initial.frame.content == Rect{100.0, 100.0, 800.0, 600.0}, "initial content frame wrong");
    require(initial.frame.outer_measured &&
                initial.frame.outer == Rect{99.0, 75.0, 802.0, 626.0},
            "outer frame does not include the decorations");

    frames.clear();
    require(main.request_frame({300.0, 200.0, 640.0, 480.0}).accepted(), "frame request refused");
    require(frames.size() == 1U && frames[0].window == main.id() &&
                frames[0].frame.content == Rect{300.0, 200.0, 640.0, 480.0},
            "frame request not reported once");
    Window& model = *application.model(main.id());
    require(model.client_size() == Size{640.0, 480.0}, "model did not follow the new client size");
    require(main.frame().frame.content == Rect{300.0, 200.0, 640.0, 480.0}, "frame did not follow request");

    const double not_a_number = std::nan("");
    require(main.request_frame({not_a_number, 0.0, 640.0, 480.0}).error == HostServiceError::invalid_argument,
            "non-finite frame accepted");
    require(main.request_frame({0.0, 0.0, 0.5, 480.0}).error == HostServiceError::invalid_argument,
            "zero-width frame accepted");
    require(main.request_frame({0.0, 0.0, 50.0, 480.0}).error == HostServiceError::invalid_argument,
            "frame below the minimum size accepted");
    require(main.request_frame({0.0, 0.0, 40000.0, 480.0}).error == HostServiceError::invalid_argument,
            "frame above the extent bound accepted");
    require(main.request_frame({2000000.0, 0.0, 640.0, 480.0}).error == HostServiceError::invalid_argument,
            "frame far off the desktop accepted");
    require(frames.size() == 1U, "refused request reported a frame");

    // A user move reports through the same event.
    application.simulate_user_frame(main.id(), {10.0, 20.0, 640.0, 480.0});
    require(frames.size() == 2U && frames[1].frame.content.x == 10.0, "user move not reported");
    const HostWindowingSnapshot snapshot = application.windowing().snapshot();
    // The minimum size is the adapter's rule, so that refusal reached the
    // adapter and counts as a request; the other four stopped in the base.
    require(snapshot.frame_requests == 2U && snapshot.rejected_requests == 5U,
            "frame request counters wrong");

    // The monitors in desktop logical units.
    const HostMonitorResult monitors = main.windows().monitors();
    require(monitors.status.accepted() && monitors.monitors.size() == 1U &&
                monitors.monitors[0].work_area == Rect{0.0, 0.0, 1920.0, 1040.0},
            "logical monitors wrong");
}

// The headless application reports its fixed double-click interval to
// every window after attach, as a host value.
void test_headless_input_settings() {
    Journal journal;
    ApplicationWindowHandle main;
    std::vector<HostWindowFrameEvent> frames;
    std::vector<ApplicationWindow> windows;
    ApplicationWindow primary = make_entry("main", "", false, {800.0, 600.0});
    observe(primary, journal, &main, &frames);
    windows.push_back(std::move(primary));
    host::HeadlessApplication application(std::move(windows));
    const Window& model = *application.model(main.id());
    require(model.presentation_settings().double_click_milliseconds == host::headless_double_click_milliseconds &&
                model.presentation_settings().double_click_milliseconds == 500U &&
                model.presentation_settings().double_click_origin == SettingOrigin::host,
            "the headless host reports its fixed double-click interval");
}

void test_pointer_window_at_and_ordering() {
    Journal journal;
    ApplicationWindowHandle main;
    ApplicationWindowHandle tool;
    host::HeadlessApplication application(main_and_tool(journal, &main, &tool));
    const ApplicationWindows windows = main.windows();

    application.windowing().set_pointer({123.0, 456.0});
    const HostScreenPointResult pointer = windows.pointer_position();
    require(pointer.status.accepted() && pointer.position == DesktopLogicalPoint{123.0, 456.0}, "pointer position wrong");

    // Over the main window's client area and its title bar.
    ApplicationWindowAtResult hit = windows.window_at({150.0, 150.0});
    require(hit.status.accepted() && hit.found && hit.window.id() == main.id(), "client area not hit");
    hit = windows.window_at({150.0, 80.0});
    require(hit.found && hit.window.id() == main.id(), "title bar not hit");
    hit = windows.window_at({5.0, 5.0});
    require(hit.status.accepted() && !hit.found, "desktop reported a window");

    // Overlap: put the tool window over the main window, then raise main;
    // the owned tool window stays above its owner.
    require(tool.request_frame({120.0, 120.0, 200.0, 300.0}).accepted(), "tool frame refused");
    hit = windows.window_at({150.0, 150.0});
    require(hit.found && hit.window.id() == tool.id(), "front window not hit");
    require(main.bring_to_front().accepted(), "bring to front refused");
    const std::vector<HostWindowId> order = application.windowing().z_order();
    require(order.size() == 2U && order[0] == tool.id() && order[1] == main.id(),
            "owned window fell below its owner");
    // A third unowned tool window can be raised above both.
    ApplicationWindow loose = make_entry("loose", "", true);
    const ApplicationWindowOpenResult opened = windows.open_window(std::move(loose));
    require(opened.status.accepted(), "unowned tool window refused");
    require(opened.window.request_frame({110.0, 110.0, 200.0, 200.0}).accepted(), "loose frame refused");
    require(main.bring_to_front().accepted(), "second raise refused");
    hit = windows.window_at({115.0, 115.0});
    require(hit.found && hit.window.id() == main.id(), "raised main window not hit");
    require(application.windowing().z_order()[0] == tool.id(), "owned window not kept above owner");

    // Hidden windows and foreign windows.
    require(tool.hide().accepted(), "hide refused");
    hit = windows.window_at({150.0, 150.0});
    require(hit.found && hit.window.id() == main.id(), "hidden window hit");
    application.windowing().add_foreign_window({140.0, 140.0, 50.0, 50.0});
    hit = windows.window_at({150.0, 150.0});
    require(hit.status.accepted() && !hit.found, "point under another application's window hit");
    require(windows.window_at({std::nan(""), 0.0}).status.error == HostServiceError::invalid_argument,
            "non-finite point accepted");
    require(contains(application.windowing().trace(), "raise id=1"), "raise missing from the trace");
}

struct WorkerRequest final {
    ApplicationWindowHandle handle{};
    ApplicationWindows windows{};
    HostServiceStatus* frame{};
    HostServiceStatus* pointer{};
    void operator()() const {
        *frame = handle.request_frame({0.0, 0.0, 300.0, 300.0});
        *pointer = windows.pointer_position().status;
    }
};

void test_threads_shutdown_and_capabilities() {
    Journal journal;
    ApplicationWindowHandle main;
    ApplicationWindows windows;
    {
        host::HeadlessApplication application(main_and_tool(journal, &main, nullptr));
        windows = main.windows();
        HostServiceStatus frame{};
        HostServiceStatus pointer{};
        std::thread worker(WorkerRequest{main, windows, &frame, &pointer});
        worker.join();
        require(frame.error == HostServiceError::wrong_thread, "worker frame request accepted");
        require(pointer.error == HostServiceError::wrong_thread, "worker pointer query accepted");
        const HostCapabilities capabilities = windows.capabilities();
        require(capabilities.supports(HostCapability::runtime_windows) &&
                    capabilities.supports(HostCapability::window_frames) &&
                    capabilities.supports(HostCapability::screen_pointer) &&
                    capabilities.supports(HostCapability::window_at_point) &&
                    capabilities.supports(HostCapability::window_ordering),
                "headless windowing capabilities missing");
        const std::string json = capabilities.to_json();
        require(contains(json, "\"runtime_windows\"") && contains(json, "\"window_ordering\""),
                "capability names missing from json");
    }
    require(!windows.active() && !main.active(), "handles outlived the run");
    require(windows.pointer_position().status.error == HostServiceError::after_shutdown,
            "windowing reachable after the run");
    require(main.frame().status.error == HostServiceError::after_shutdown, "frame after the run accepted");
    ApplicationWindow late = make_entry("late", "main", true);
    require(windows.open_window(std::move(late)).status.error == HostServiceError::after_shutdown,
            "open after the run accepted");

    // A host without the windowing capabilities refuses each request as
    // unsupported, one by one.
    const HostCapabilities limited{HostCapabilities::current_protocol_version, "limited",
                                   HostCapability::lifecycle | HostCapability::monitor_geometry};
    host::HeadlessApplication application(main_and_tool(journal, &main, nullptr), limited);
    const ApplicationWindows refusing = main.windows();
    ApplicationWindow entry = make_entry("refused", "main", true);
    require(refusing.open_window(std::move(entry)).status.error == HostServiceError::unsupported,
            "runtime window opened without the capability");
    require(entry.model != nullptr, "unsupported open consumed the entry");
    require(main.frame().status.error == HostServiceError::unsupported, "frame without the capability");
    require(main.request_frame({0.0, 0.0, 300.0, 300.0}).error == HostServiceError::unsupported,
            "frame request without the capability");
    require(refusing.pointer_position().status.error == HostServiceError::unsupported,
            "pointer without the capability");
    require(refusing.window_at({1.0, 1.0}).status.error == HostServiceError::unsupported,
            "window at point without the capability");
    require(main.bring_to_front().error == HostServiceError::unsupported, "ordering without the capability");
    require(refusing.monitors().status.accepted(), "monitor geometry refused although advertised");
}

void test_primary_close_ends_the_run() {
    Journal journal;
    ApplicationWindowHandle main;
    ApplicationWindowHandle tool;
    host::HeadlessApplication application(main_and_tool(journal, &main, &tool));
    journal.lines.clear();
    application.simulate_user_close(main.id());
    require(journal.lines == "closed palette\nclosed main\n", "owned window did not close before its owner");
    require(application.windowing().window_count() == 0U, "windows left after the primary closed");
}
// run_modal's nested loop on the headless host: what a turn does.
enum class ModalTurnAction : std::uint8_t {
    // Sets the modal model's dialog result to ok on the second turn.
    set_ok,
    // The user closes the modal window on the first turn.
    user_close,
    // The run quits on the first turn.
    quit,
    // The first turn shows a second modal window over the first, whose own
    // first turn closes it; the second turn closes the first.
    nest,
};

struct ModalTurnScript final {
    host::HeadlessApplication* application{};
    Window* model{};
    ModalTurnAction action{ModalTurnAction::set_ok};
    // Observations, owned by the test.
    int* turns{};
    bool* main_blocked{};
    std::string* trace{};
    bool operator()(HostWindowId window) const;
};

// The inner window's turns in the nest script.
struct InnerModalTurn final {
    host::HeadlessApplication* application{};
    std::string* trace{};
    bool operator()(HostWindowId window) const {
        *trace += "inner turn depth=" + std::to_string((*application).windowing().modal_depth()) + "\n";
        (*application).simulate_user_close(window);
        return true;
    }
};

bool ModalTurnScript::operator()(HostWindowId window) const {
    ++(*turns);
    host::HeadlessApplication& app = *application;
    const HostWindowId main_id = app.id("main");
    *main_blocked = app.windowing().input_blocked(main_id);
    // The user cannot close the window under the modal one.
    app.simulate_user_close(main_id);
    switch (action) {
    case ModalTurnAction::set_ok:
        if (*turns == 2) (*model).set_dialog_result(DialogResult::ok);
        return true;
    case ModalTurnAction::user_close:
        app.simulate_user_close(window);
        return true;
    case ModalTurnAction::quit:
        return false;
    case ModalTurnAction::nest:
        if (*turns == 1) {
            app.windowing().set_modal_turn(InnerModalTurn{application, trace});
            ApplicationWindow inner = make_entry("modal.inner", "modal");
            const ApplicationModalResult shown = app.handle("main").windows().show_modal(std::move(inner));
            *trace += "inner result=" + std::to_string(static_cast<int>(shown.dialog_result)) + "\n";
            // Back to this script for the outer window's next turn.
            app.windowing().set_modal_turn(*this);
            return true;
        }
        app.simulate_user_close(window);
        return true;
    }
    return true;
}

void test_modal_windows() {
    Journal journal;
    ApplicationWindowHandle main;
    ApplicationWindowHandle tool;
    host::HeadlessApplication application(main_and_tool(journal, &main, &tool));
    const ApplicationWindows windows = main.windows();

    // Without a nested loop the host refuses, leaving nothing open.
    require(!application.windowing().supports_modal_windows(), "headless modal loop on by default");
    ApplicationWindow refused = make_entry("modal", "main");
    require(windows.show_modal(std::move(refused)).status.error == HostServiceError::unsupported,
            "show_modal without a nested loop accepted");
    require(windows.window_count() == 2U, "refused modal window opened");

    int turns = 0;
    bool main_blocked = false;
    std::string trace;
    ModalTurnScript script{&application, nullptr, ModalTurnAction::set_ok, &turns, &main_blocked, &trace};

    // A dialog result set during the loop closes the window and is the
    // result; the window under it refused input meanwhile.
    ApplicationWindow first = make_entry("modal", "main");
    script.model = first.model.get();
    application.windowing().set_modal_turn(script);
    journal.lines.clear();
    observe(first, journal);
    const ApplicationModalResult ok = windows.show_modal(std::move(first));
    require(ok.status.accepted(), "show_modal refused");
    require(ok.dialog_result == DialogResult::ok, "dialog result not reported");
    require(turns == 2, "nested loop did not stop when the window closed");
    require(main_blocked, "the owner took input during the modal loop");
    require(!application.windowing().input_blocked(main.id()), "the owner stayed blocked after the loop");
    require(main.active(), "the user closed the owner during the modal loop");
    require(journal.lines == "ready modal\nclosed modal\n", "modal callbacks wrong");
    require(contains(application.windowing().trace(), "run_modal "), "nested loop missing from the trace");
    require(application.windowing().modal_depth() == 0U, "modal depth not restored");
    application.reclaim();

    // The close box gives cancel.
    turns = 0;
    ApplicationWindow second = make_entry("modal", "main");
    script.model = second.model.get();
    script.action = ModalTurnAction::user_close;
    application.windowing().set_modal_turn(script);
    const ApplicationModalResult closed = windows.show_modal(std::move(second));
    require(closed.status.accepted() && closed.dialog_result == DialogResult::cancel,
            "a modal window closed by the user did not give cancel");
    require(turns == 1, "close box: wrong number of turns");
    application.reclaim();

    // A quit ends the loop with the window still open.
    turns = 0;
    ApplicationWindow third = make_entry("modal", "main");
    script.model = third.model.get();
    script.action = ModalTurnAction::quit;
    application.windowing().set_modal_turn(script);
    const ApplicationModalResult quit = windows.show_modal(std::move(third));
    require(quit.status.accepted() && quit.dialog_result == DialogResult::cancel, "quit: wrong result");
    const HostWindowId left_open = application.id("modal");
    require(application.windowing().live(left_open), "quit closed the modal window");
    require(!application.windowing().input_blocked(main.id()), "quit left the owner blocked");
    require(windows.destroy_window(application.handle("modal")).accepted(), "quit window not destroyable");
    application.reclaim();

    // A modal window over a modal window.
    turns = 0;
    trace.clear();
    ApplicationWindow fourth = make_entry("modal", "main");
    script.model = fourth.model.get();
    script.action = ModalTurnAction::nest;
    application.windowing().set_modal_turn(script);
    const ApplicationModalResult nested = windows.show_modal(std::move(fourth));
    require(nested.status.accepted() && nested.dialog_result == DialogResult::cancel, "nest: wrong result");
    require(trace == "inner turn depth=2\ninner result=2\n", "nested modal loop wrong");
    application.reclaim();

    // Refusals: no owner, a popup, a floating window.
    ApplicationWindow unowned = make_entry("modal", "");
    unowned.tool_window = true;
    require(windows.show_modal(std::move(unowned)).status.error == HostServiceError::invalid_argument,
            "unowned modal window accepted");
    ApplicationWindow popup = make_entry("modal", "main");
    popup.popup = HostPopupPlacement{};
    require(windows.show_modal(std::move(popup)).status.error == HostServiceError::invalid_argument,
            "modal popup accepted");
    require(windows.window_count() == 2U, "a refused modal window opened");
}
} // namespace

int main() {
    try {
        test_desktop_mapping();
        test_initial_windows_are_admitted_in_ownership_order();
        test_runtime_open_and_destroy();
        test_runtime_open_refusals_leave_the_entry();
        test_frames_and_frame_requests();
        test_headless_input_settings();
        test_pointer_window_at_and_ordering();
        test_threads_shutdown_and_capabilities();
        test_primary_close_ends_the_run();
        test_modal_windows();
        std::cout << "gui_forms_runtime_windows_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_runtime_windows_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
