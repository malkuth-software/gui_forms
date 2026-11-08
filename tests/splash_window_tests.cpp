// Splash windows (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S,
// SPLASH_STUDY.md) on the headless reference host. A Splash is a popup-kind
// window that takes no input: the tooltip presenter shows its bubble in one
// kept per window (warm standby), opened hidden on first use and shown,
// moved and hidden after that; a run that cannot open one shows the overlay
// as before. The allocations of a warm show are counted by this executable's
// own operator new (a test-only replacement, never in a library).

#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"
#include "application/headless_application.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool counting_allocations = false;
std::uint64_t counted_allocations = 0U;
} // namespace

void* operator new(std::size_t size) {
    if (counting_allocations) ++counted_allocations;
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}
void* operator new[](std::size_t size) {
    void* allocation = operator new(size);
    return allocation;
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool contains(const std::string& text, const char* fragment) {
    const bool found = text.find(fragment) != std::string::npos;
    return found;
}

struct KeepReady final {
    ApplicationWindowHandle* handle{};
    Window** model{};
    void operator()(Window& window, ApplicationWindowHandle value) const {
        *handle = value;
        *model = &window;
    }
};

// The primary window "main", 800 x 600 at 100,100 on the headless work
// area (0,0 1920 x 1040), with one control that has a tooltip.
struct Desk final {
    ApplicationWindowHandle main{};
    Window* model{};
    std::shared_ptr<Control> anchor{};
    std::unique_ptr<host::HeadlessApplication> application{};

    Desk() {
        std::vector<ApplicationWindow> windows;
        ApplicationWindow primary{};
        primary.stable_id = "main";
        std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
        anchor = make_control<Control>(StableId("anchor"));
        (*anchor).set_requested_bounds(Rect{10.0, 10.0, 80.0, 24.0});
        (*anchor).tool_tip_text = "Saves the document";
        (*root).add_child(anchor);
        primary.model = std::make_unique<Window>(root, Size{800.0, 600.0});
        primary.options.title = "main";
        primary.options.initial_size = {800.0, 600.0};
        primary.options.ready = KeepReady{&main, &model};
        windows.push_back(std::move(primary));
        application = std::make_unique<host::HeadlessApplication>(std::move(windows));
        require(model != nullptr && main.active(), "the main window is ready");
        (*model).perform_layout();
    }
    host::HeadlessWindowing& windowing() { return (*application).windowing(); }
    // The one window of the run other than main, or none.
    std::optional<HostWindowId> splash() {
        const std::vector<HostWindowId> order = windowing().z_order();
        for (const HostWindowId id : order) {
            if (id != main.id()) return id;
        }
        return std::nullopt;
    }
};

void tooltip_shows_in_a_splash_window() {
    Desk desk;
    desk.windowing().clear_trace();
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    require((*desk.model).tool_tip_visible(), "the tooltip shows");
    const std::optional<HostWindowId> splash = desk.splash();
    require(splash.has_value() && desk.windowing().window_count() == 2U, "the tooltip has its own window");
    require(desk.windowing().kind(*splash) == HostWindowKind::popup, "a Splash is a popup-kind window");
    require(desk.windowing().owner(*splash) == desk.main.id(), "the window that shows it owns it");
    const std::string& trace = desk.windowing().trace();
    require(contains(trace, "transient=tool_tip") && contains(trace, "focus=passive"),
            "the host is told it is a tooltip Splash, never focused");
    require(contains(trace, "visible=0"), "it opens hidden (warm standby) and is shown after");
    require(desk.windowing().native_visible(*splash), "it shows");
    const std::optional<Rect> content = desk.windowing().native_content(*splash);
    require(content.has_value(), "it is placed");
    // Below the control: the face at the owner's client origin (100,100)
    // plus the anchor's (10,10) plus (8, 24 + 7), less the shadow's outsets.
    require((*content).x < 118.0 && (*content).y < 141.0 && (*content).x > 80.0 && (*content).y > 100.0,
            "the face sits below the control on the desktop");
    (*desk.model).hide_tool_tip();
    require(!(*desk.model).tool_tip_visible(), "the tooltip hides");
    require(desk.windowing().window_count() == 2U && !desk.windowing().native_visible(*splash),
            "hiding keeps the window, hidden, for the next show");
}

void a_warm_show_reuses_the_window() {
    Desk desk;
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> first = desk.splash();
    (*desk.model).hide_tool_tip();
    // The second show measures the warm path; the third repeats it.
    std::uint64_t warm[2]{};
    for (int round = 0; round < 2; ++round) {
        counted_allocations = 0U;
        counting_allocations = true;
        (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
        counting_allocations = false;
        warm[round] = counted_allocations;
        require((*desk.model).tool_tip_visible(), "the warm tooltip shows");
        require(desk.splash() == first && desk.windowing().window_count() == 2U, "the same window shows it");
        require(desk.windowing().native_visible(*first), "and it is visible");
        (*desk.model).hide_tool_tip();
    }
    std::printf("splash warm tooltip show: %llu and %llu allocations (headless host trace included)\n",
                static_cast<unsigned long long>(warm[0]), static_cast<unsigned long long>(warm[1]));
    require(warm[0] == warm[1], "a warm show allocates the same each time");
}

void falls_back_to_the_overlay() {
    Desk desk;
    desk.windowing().set_splash_windows(false);
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    require((*desk.model).tool_tip_visible(), "the tooltip shows without Splash windows");
    require(desk.windowing().window_count() == 1U, "as an overlay in its window");
    (*desk.model).hide_tool_tip();
}

void closes_with_its_control() {
    Desk desk;
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> splash = desk.splash();
    (*desk.anchor).visible = false;
    (*desk.model).perform_layout();
    require(!(*desk.model).tool_tip_visible(), "the tooltip closes when its control hides");
    require(splash.has_value() && !desk.windowing().native_visible(*splash), "its window hides");
}

void survives_the_host_closing_it() {
    Desk desk;
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> splash = desk.splash();
    require(splash.has_value(), "the Splash window is open");
    require(desk.windowing().destroy_window(*splash).accepted(), "the host may destroy it");
    require(!(*desk.model).tool_tip_visible(), "the tooltip closes with it");
    (*desk.model).show_tool_tip(desk.anchor, std::chrono::milliseconds(0));
    const std::optional<HostWindowId> again = desk.splash();
    require(again.has_value() && *again != *splash, "the next show opens a new Splash window");
    (*desk.model).hide_tool_tip();
}

ApplicationWindow splash_entry(const char* id, HostPopupFocus focus) {
    ApplicationWindow entry{};
    entry.stable_id = id;
    entry.owner_id = "main";
    entry.model = std::make_unique<Window>(make_control<Control>(StableId("splash.root")), Size{40.0, 20.0});
    entry.options.initial_size = {40.0, 20.0};
    entry.options.minimum_size = {1.0, 1.0};
    HostPopupPlacement placement{};
    placement.kind = HostTransientKind::tool_tip;
    placement.focus = focus;
    entry.popup = placement;
    return entry;
}

void admission_rules() {
    Desk desk;
    const ApplicationWindows windows = desk.main.windows();
    const ApplicationWindowOpenResult focused = windows.open_window(splash_entry("main/focused", HostPopupFocus::takes_focus));
    require(focused.status.error == HostServiceError::invalid_argument, "a Splash never takes focus");
    desk.windowing().set_splash_windows(false);
    const ApplicationWindowOpenResult refused = windows.open_window(splash_entry("main/refused", HostPopupFocus::passive));
    require(refused.status.error == HostServiceError::unsupported, "a host without Splash windows refuses one");
    desk.windowing().set_splash_windows(true);
    const ApplicationWindowOpenResult opened = windows.open_window(splash_entry("main/opened", HostPopupFocus::passive));
    require(opened.status.accepted(), "a host with Splash windows opens one");
    require(std::string(host_transient_kind_name(HostTransientKind::splashscreen)) == "splashscreen" &&
                host_transient_is_splash(HostTransientKind::tool_tip) &&
                !host_transient_is_splash(HostTransientKind::popup),
            "the kinds are named and classified");
}
} // namespace

int main() {
    try {
        tooltip_shows_in_a_splash_window();
        a_warm_show_reuses_the_window();
        falls_back_to_the_overlay();
        closes_with_its_control();
        survives_the_host_closing_it();
        admission_rules();
    } catch (const std::exception& error) {
        std::cerr << "splash window test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "splash window tests passed\n";
    return 0;
}
