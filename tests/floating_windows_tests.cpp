// Stage DK phase W0: the floating-window policy (decisions 1, 10 and 11) on
// the headless reference host: hiding while the application is inactive,
// minimizing with the primary window, following it with a kept offset and
// clamping to the work area, on a platform that does and one that does not
// move owned windows itself.
#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/host.hpp"
#include "application/headless_application.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
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

ApplicationWindow make_entry(const char* id, const char* owner, HostWindowFloating floating,
                             Size size) {
    ApplicationWindow entry{};
    entry.stable_id = id;
    entry.owner_id = owner;
    entry.floating = floating;
    const std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
    entry.model = std::make_unique<Window>(root, size);
    entry.options.title = id;
    entry.options.initial_size = size;
    entry.options.minimum_size = {100.0, 80.0};
    return entry;
}

struct KeepHandle final {
    ApplicationWindowHandle* handle{};
    void operator()(Window&, ApplicationWindowHandle value) const { *handle = value; }
};

// The primary window "main" (800 x 600 at 100,100 on a 1920 x 1040 work
// area), a floating tool "tool" and a floating document "doc", both
// visible, placed beside the main window.
struct Desk final {
    ApplicationWindowHandle main{};
    ApplicationWindowHandle tool{};
    ApplicationWindowHandle doc{};
    std::unique_ptr<host::HeadlessApplication> application{};

    explicit Desk(bool tool_hides_on_close = false) {
        std::vector<ApplicationWindow> windows;
        ApplicationWindow primary = make_entry("main", "", HostWindowFloating::none, {800.0, 600.0});
        primary.options.ready = KeepHandle{&main};
        windows.push_back(std::move(primary));
        ApplicationWindow panel = make_entry("tool", "main", HostWindowFloating::tool, {200.0, 300.0});
        panel.options.ready = KeepHandle{&tool};
        panel.options.hide_on_close = tool_hides_on_close;
        windows.push_back(std::move(panel));
        ApplicationWindow document = make_entry("doc", "main", HostWindowFloating::document, {300.0, 200.0});
        document.options.ready = KeepHandle{&doc};
        windows.push_back(std::move(document));
        application = std::make_unique<host::HeadlessApplication>(std::move(windows));
    }
    host::HeadlessWindowing& windowing() { return (*application).windowing(); }
    Rect content(const ApplicationWindowHandle& handle) {
        const std::optional<Rect> rect = windowing().native_content(handle.id());
        require(rect.has_value(), "window has no native rectangle");
        return *rect;
    }
    bool visible(const ApplicationWindowHandle& handle) { return windowing().native_visible(handle.id()); }
};

void test_kinds_traits_and_clamp() {
    require(host_window_kind(false, false, HostWindowFloating::none, false) == HostWindowKind::primary &&
                host_window_kind(true, false, HostWindowFloating::none, false) == HostWindowKind::standard &&
                host_window_kind(true, true, HostWindowFloating::none, false) == HostWindowKind::tool &&
                host_window_kind(true, false, HostWindowFloating::tool, false) == HostWindowKind::floating_tool &&
                host_window_kind(true, false, HostWindowFloating::document, false) ==
                    HostWindowKind::floating_document,
            "window kinds wrong");
    const HostWindowKindTraits tool = host_window_kind_traits(HostWindowKind::floating_tool);
    const HostWindowKindTraits document = host_window_kind_traits(HostWindowKind::floating_document);
    const HostWindowKindTraits legacy = host_window_kind_traits(HostWindowKind::tool);
    require(tool.floating && tool.hides_when_application_inactive && tool.follows_main_window &&
                tool.joins_main_full_screen,
            "floating tool traits wrong");
    require(document.floating && !document.hides_when_application_inactive &&
                document.follows_main_window && document.joins_main_full_screen,
            "floating document traits wrong");
    require(!legacy.floating && !legacy.hides_when_application_inactive && !legacy.follows_main_window,
            "the existing tool window must keep its own behaviour");
    const Rect work{0.0, 0.0, 1000.0, 700.0};
    require(clamp_to_work_area({900.0, 100.0, 200.0, 100.0}, work) == Rect{800.0, 100.0, 200.0, 100.0},
            "right edge not clamped");
    require(clamp_to_work_area({-50.0, -20.0, 200.0, 100.0}, work) == Rect{0.0, 0.0, 200.0, 100.0},
            "top left not clamped");
    require(clamp_to_work_area({10.0, 650.0, 200.0, 100.0}, work) == Rect{10.0, 600.0, 200.0, 100.0},
            "bottom edge not clamped");
    require(clamp_to_work_area({300.0, 300.0, 1200.0, 100.0}, work) == Rect{0.0, 300.0, 1200.0, 100.0},
            "a wider window not aligned with the left edge");
    require(clamp_to_work_area({10.0, 10.0, 50.0, 50.0}, work) == Rect{10.0, 10.0, 50.0, 50.0},
            "a window inside the work area moved");
}

void test_floating_validation() {
    // A floating window must be owned by the primary window and is not a
    // tool window in the host's sense.
    std::vector<ApplicationWindow> windows;
    windows.push_back(make_entry("main", "", HostWindowFloating::none, {800.0, 600.0}));
    windows.push_back(make_entry("doc", "main", HostWindowFloating::none, {300.0, 200.0}));
    windows.push_back(make_entry("panel", "doc", HostWindowFloating::tool, {200.0, 200.0}));
    require(!Application::validate(windows).accepted(), "floating window owned by an owned window accepted");
    windows.pop_back();
    ApplicationWindow tool_flag = make_entry("panel", "main", HostWindowFloating::tool, {200.0, 200.0});
    tool_flag.tool_window = true;
    windows.push_back(std::move(tool_flag));
    require(!Application::validate(windows).accepted(), "floating tool_window accepted");
    windows.pop_back();
    windows.push_back(make_entry("panel", "", HostWindowFloating::tool, {200.0, 200.0}));
    require(!Application::validate(windows).accepted(), "unowned floating window accepted");
    windows.pop_back();
    windows.push_back(make_entry("panel", "main", HostWindowFloating::tool, {200.0, 200.0}));
    require(Application::validate(windows).accepted(), "valid floating window refused");

    Desk desk;
    const ApplicationWindows run = desk.main.windows();
    ApplicationWindow wrong_owner = make_entry("float.x", "doc", HostWindowFloating::document, {200.0, 200.0});
    require(run.open_window(std::move(wrong_owner)).status.error == HostServiceError::invalid_argument,
            "runtime floating window owned by another floating window accepted");
    require(wrong_owner.model != nullptr, "refused floating entry consumed");
    // The bound: 32 floating windows (two exist).
    std::size_t floating = desk.windowing().floating_window_count();
    for (std::size_t index = 0U; floating < HostWindowing::maximum_floating_windows; ++index) {
        ApplicationWindow entry = make_entry(("float." + std::to_string(index)).c_str(), "main",
                                             HostWindowFloating::tool, {200.0, 200.0});
        require(run.open_window(std::move(entry)).status.accepted(), "floating window below the bound refused");
        floating = desk.windowing().floating_window_count();
    }
    ApplicationWindow over = make_entry("float.over", "main", HostWindowFloating::tool, {200.0, 200.0});
    require(run.open_window(std::move(over)).status.error == HostServiceError::too_large,
            "33rd floating window accepted");
    require(over.model != nullptr, "floating window over the bound consumed");
    ApplicationWindow plain = make_entry("plain", "main", HostWindowFloating::none, {200.0, 200.0});
    plain.tool_window = true;
    require(run.open_window(std::move(plain)).status.accepted(), "non-floating window refused at the floating bound");
}

void test_tools_hide_while_the_application_is_inactive() {
    Desk desk;
    require(desk.visible(desk.main) && desk.visible(desk.tool) && desk.visible(desk.doc),
            "floating windows not shown at start");
    desk.windowing().clear_trace();
    (*desk.application).simulate_application_active(false);
    require(!desk.visible(desk.tool), "floating tool visible while the application is inactive");
    require(desk.visible(desk.doc), "floating document hidden while the application is inactive");
    require(desk.visible(desk.main), "primary window hidden while the application is inactive");
    require(desk.windowing().trace() == "application active=0\nhide id=2\n", "deactivation trace wrong");
    (*desk.application).simulate_application_active(false);
    require(desk.windowing().trace() == "application active=0\nhide id=2\napplication active=0\n",
            "a repeated deactivation acted again");
    (*desk.application).simulate_application_active(true);
    require(desk.visible(desk.tool), "floating tool not shown again on activation");
    require(contains(desk.windowing().trace(), "application active=1\nshow id=2\n"), "activation trace wrong");

    // The program's wish decides whether the window is wanted at all.
    require(desk.tool.hide().accepted() && !desk.visible(desk.tool), "program hide refused");
    (*desk.application).simulate_application_active(false);
    (*desk.application).simulate_application_active(true);
    require(!desk.visible(desk.tool), "activation showed a window the program hid");
    (*desk.application).simulate_application_active(false);
    require(desk.tool.show().accepted() && !desk.visible(desk.tool),
            "program show put a floating tool on screen while the application is inactive");
    (*desk.application).simulate_application_active(true);
    require(desk.visible(desk.tool), "wanted floating tool not shown on activation");
    const HostWindowingSnapshot snapshot = desk.windowing().snapshot();
    require(snapshot.application_active && snapshot.policy_hides >= 2U && snapshot.policy_shows >= 2U,
            "policy counters wrong");
}

void test_hide_on_close_is_remembered() {
    Desk desk(true);
    (*desk.application).simulate_user_close(desk.tool.id());
    require(desk.tool.active() && !desk.visible(desk.tool), "hide-on-close floating tool not hidden");
    (*desk.application).simulate_application_active(false);
    (*desk.application).simulate_application_active(true);
    require(!desk.visible(desk.tool), "activation brought back a window the user closed");
    require(desk.tool.show().accepted() && desk.visible(desk.tool), "closed floating tool cannot be shown again");
}

void test_minimized_with_the_primary_window() {
    Desk desk;
    desk.windowing().clear_trace();
    (*desk.application).simulate_minimized(desk.main.id(), true);
    require(!desk.visible(desk.tool) && !desk.visible(desk.doc), "floating windows not minimized with the primary");
    require(desk.windowing().trace() == "minimize id=1\nhide id=2\nhide id=3\n", "minimize trace wrong");
    (*desk.application).simulate_minimized(desk.main.id(), false);
    require(desk.visible(desk.tool) && desk.visible(desk.doc), "floating windows not restored with the primary");
    // A floating window minimized on its own changes nothing else.
    (*desk.application).simulate_minimized(desk.doc.id(), true);
    require(desk.visible(desk.tool), "minimizing a floating window hid another");
}

void test_follow_keeps_the_offset(bool native_follow) {
    Desk desk;
    desk.windowing().set_native_follow(native_follow);
    const Rect main_start = desk.content(desk.main);
    const Rect tool_start = desk.content(desk.tool);
    const double offset_x = tool_start.x - main_start.x;
    const double offset_y = tool_start.y - main_start.y;
    // Move the main window: the tool follows by the same distance.
    (*desk.application).simulate_user_frame(desk.main.id(), {150.0, 130.0, 800.0, 600.0});
    Rect tool_now = desk.content(desk.tool);
    require(tool_now.x == 150.0 + offset_x && tool_now.y == 130.0 + offset_y,
            "floating tool did not keep its offset");
    require(tool_now.width == tool_start.width && tool_now.height == tool_start.height,
            "following changed the floating window's size");
    // The user moves the tool: that is its new offset.
    (*desk.application).simulate_user_frame(desk.tool.id(), {400.0, 500.0, 200.0, 300.0});
    (*desk.application).simulate_user_frame(desk.main.id(), {170.0, 140.0, 800.0, 600.0});
    tool_now = desk.content(desk.tool);
    require(tool_now.x == 420.0 && tool_now.y == 510.0, "floating tool did not follow with its new offset");
    // Resizing the main window from its bottom right moves nothing.
    (*desk.application).simulate_user_frame(desk.main.id(), {170.0, 140.0, 700.0, 500.0});
    require(desk.content(desk.tool).x == 420.0, "a resize that kept the origin moved the floating tool");
}

void test_follow_clamps_and_remembers(bool native_follow) {
    Desk desk;
    desk.windowing().set_native_follow(native_follow);
    // Tool beside the main window's right edge: content x = 100 + 800 + 2 +
    // 16 = 918.
    const Rect tool_start = desk.content(desk.tool);
    require(tool_start.x == 918.0 && tool_start.y == 100.0, "floating tool not placed beside the main window");
    // Move the main window right by 900: the tool would leave the 1920-wide
    // work area; it stops with its outer right edge at 1920.
    (*desk.application).simulate_user_frame(desk.main.id(), {1000.0, 100.0, 800.0, 600.0});
    const Rect clamped = desk.content(desk.tool);
    const double outer_right = clamped.x + clamped.width + host::HeadlessWindowing::border_width;
    require(outer_right == 1920.0 && clamped.y == 100.0, "floating tool not clamped to the work area");
    // Down past the task bar: the outer bottom stops at 1040.
    (*desk.application).simulate_user_frame(desk.main.id(), {1000.0, 900.0, 800.0, 600.0});
    const Rect low = desk.content(desk.tool);
    require(low.y + low.height + host::HeadlessWindowing::border_width == 1040.0, "bottom edge not clamped");
    // Back: the original offset returns, not the clamped one.
    (*desk.application).simulate_user_frame(desk.main.id(), {100.0, 100.0, 800.0, 600.0});
    const Rect back = desk.content(desk.tool);
    require(back.x == 918.0 && back.y == 100.0, "clamping changed the remembered offset");
    const HostWindowingSnapshot snapshot = desk.windowing().snapshot();
    require(snapshot.clamped_follow_moves >= 2U && snapshot.follow_moves >= 3U, "follow counters wrong");
}

void test_follow_onto_another_screen() {
    Desk desk;
    std::vector<HostMonitor> monitors;
    monitors.push_back({"left", {0.0, 0.0, 1920.0, 1080.0}, {0.0, 0.0, 1920.0, 1040.0}, 1.0, true});
    monitors.push_back({"right", {1920.0, 0.0, 1280.0, 1024.0}, {1920.0, 0.0, 1280.0, 984.0}, 1.0, false});
    desk.windowing().set_monitors(monitors);
    // The main window moves onto the right monitor; the tool lands there too
    // and is clamped to that monitor's work area, not the left one's.
    (*desk.application).simulate_user_frame(desk.main.id(), {2200.0, 100.0, 800.0, 600.0});
    const Rect tool = desk.content(desk.tool);
    const double outer_right = tool.x + tool.width + host::HeadlessWindowing::border_width;
    require(outer_right == 3200.0, "floating tool not clamped to the screen it landed on");
}

void test_late_reports_keep_the_offset() {
    Desk desk;
    desk.windowing().set_deferred_frame_reports(true);
    // Two quick moves whose follow requests are answered late: the stale
    // answers must not be taken for user moves.
    (*desk.application).simulate_user_frame(desk.main.id(), {1000.0, 100.0, 800.0, 600.0});
    (*desk.application).simulate_user_frame(desk.main.id(), {600.0, 100.0, 800.0, 600.0});
    desk.windowing().flush_frame_reports();
    desk.windowing().set_deferred_frame_reports(false);
    (*desk.application).simulate_user_frame(desk.main.id(), {100.0, 100.0, 800.0, 600.0});
    const Rect back = desk.content(desk.tool);
    require(back.x == 918.0 && back.y == 100.0, "late reports changed the remembered offset");
}

void test_runtime_floating_window_is_created_hidden() {
    Desk desk;
    (*desk.application).simulate_application_active(false);
    desk.windowing().clear_trace();
    ApplicationWindowHandle torn{};
    ApplicationWindow entry = make_entry("torn", "main", HostWindowFloating::tool, {250.0, 250.0});
    entry.options.ready = KeepHandle{&torn};
    const ApplicationWindowOpenResult opened = desk.main.windows().open_window(std::move(entry));
    require(opened.status.accepted(), "runtime floating tool refused");
    require(!desk.visible(torn), "runtime floating tool shown while the application is inactive");
    require(!contains(desk.windowing().trace(), "show id=4"), "runtime floating tool shown at creation");
    (*desk.application).simulate_application_active(true);
    require(desk.visible(torn), "runtime floating tool not shown on activation");
    require(contains(desk.windowing().trace(), "open id=4 kind=floating_tool owner=1"),
            "runtime floating kind missing from the trace");
}
} // namespace

// The closing handler's view of each request: the reasons it was asked
// with, and whether it refuses.
struct RecordClosing final {
    std::vector<HostCloseReason>* reasons{};
    const bool* refuse{};
    void operator()(HostCloseRequest& request) const {
        (*reasons).push_back(request.reason);
        if (*refuse) request.cancel = true;
    }
};

struct CountClosed final {
    int* count{};
    void operator()() const { ++(*count); }
};

// A session end (log out, restart, shut down) reaches the closing handler as
// HostCloseReason::session_end, not as the user's close; a refusal keeps
// every window open, and an accepted one closes every window with that
// reason, newest first, including a hide-on-close tool.
void test_session_end_is_its_own_close_reason() {
    std::vector<HostCloseReason> reasons;
    bool refuse = true;
    int closed = 0;
    ApplicationWindowHandle main{};
    ApplicationWindowHandle tool{};
    std::vector<ApplicationWindow> windows;
    ApplicationWindow primary = make_entry("main", "", HostWindowFloating::none, {800.0, 600.0});
    primary.options.ready = KeepHandle{&main};
    primary.options.closing = RecordClosing{&reasons, &refuse};
    primary.options.closed = CountClosed{&closed};
    // Observing: the application owns the model, which outlives its close
    // until the application reclaims it.
    const Window* main_model = primary.model.get();
    windows.push_back(std::move(primary));
    ApplicationWindow panel = make_entry("tool", "main", HostWindowFloating::tool, {200.0, 300.0});
    panel.options.ready = KeepHandle{&tool};
    panel.options.hide_on_close = true;
    const Window* tool_model = panel.model.get();
    windows.push_back(std::move(panel));
    host::HeadlessApplication application(std::move(windows));

    require(!application.simulate_session_end(), "a refused session end reported as accepted");
    require(reasons.size() == 1U && reasons[0] == HostCloseReason::session_end,
            "the closing handler was not asked with the session-end reason");
    require(main.active() && tool.active() && closed == 0, "a refused session end closed a window");
    require(!(*main_model).host_close_reason().has_value(), "an open window reports a close reason");

    application.simulate_user_close(main.id());
    require(reasons.size() == 2U && reasons[1] == HostCloseReason::user,
            "the close button must still report the user's close");

    refuse = false;
    require(application.simulate_session_end(), "an accepted session end reported as refused");
    require(!main.active() && !tool.active() && closed == 1, "an accepted session end left a window open");
    require((*main_model).host_close_reason() == HostCloseReason::session_end &&
                (*tool_model).host_close_reason() == HostCloseReason::session_end,
            "the closed windows do not report the session-end reason");
}

// Every window's closing handler is asked at a session end, not only the
// run-ending window's (audit P2): a secondary window that refuses, say for
// unsaved work, cancels it and every window stays open. This is the
// contract the Windows host meets through WM_QUERYENDSESSION, which
// Windows sends to each top-level window, and the macOS host's quit path.
void test_session_end_asks_every_window() {
    std::vector<HostCloseReason> main_reasons;
    std::vector<HostCloseReason> tool_reasons;
    const bool accept = false;
    const bool refuse = true;
    ApplicationWindowHandle main{};
    ApplicationWindowHandle tool{};
    std::vector<ApplicationWindow> windows;
    ApplicationWindow primary = make_entry("main", "", HostWindowFloating::none, {800.0, 600.0});
    primary.options.ready = KeepHandle{&main};
    primary.options.closing = RecordClosing{&main_reasons, &accept};
    windows.push_back(std::move(primary));
    ApplicationWindow panel = make_entry("tool", "main", HostWindowFloating::tool, {200.0, 300.0});
    panel.options.ready = KeepHandle{&tool};
    panel.options.closing = RecordClosing{&tool_reasons, &refuse};
    windows.push_back(std::move(panel));
    host::HeadlessApplication application(std::move(windows));

    require(!application.simulate_session_end(), "a secondary window's refusal did not cancel the session end");
    require(tool_reasons.size() == 1U && tool_reasons[0] == HostCloseReason::session_end,
            "the secondary window's closing handler was not asked");
    require(main.active() && tool.active(), "a cancelled session end closed a window");
}

int main() {
    try {
        test_kinds_traits_and_clamp();
        test_session_end_asks_every_window();
        test_floating_validation();
        test_tools_hide_while_the_application_is_inactive();
        test_hide_on_close_is_remembered();
        test_minimized_with_the_primary_window();
        test_follow_keeps_the_offset(false);
        test_follow_keeps_the_offset(true);
        test_follow_clamps_and_remembers(false);
        test_follow_clamps_and_remembers(true);
        test_follow_onto_another_screen();
        test_late_reports_keep_the_offset();
        test_runtime_floating_window_is_created_hidden();
        test_session_end_is_its_own_close_reason();
        std::cout << "gui_forms_floating_windows_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_floating_windows_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
