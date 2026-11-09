// The Windows host reports the system's double-click interval
// (HostInputSettingsEvent) into the window's PresentationSettings: after
// attach, GetDoubleClickTime as a host value; on WM_SETTINGCHANGE it reads
// the interval again and reports only a change, so a broadcast that leaves
// it alone changes nothing. The test never changes the system setting.
#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"

#include <windows.h>

#include "support/windows_own_window.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace gui_forms;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Counts the window's presentation_changed notifications.
struct ChangeCounter final {
    unsigned* count{};
    void operator()(const PresentationSettings&) const { *count = *count + 1U; }
};

struct Ready final {
    bool* checked{};
    void operator()(gui_forms::Window& window, ApplicationWindowHandle handle) const {
        HWND hwnd = test_support::own_gui_forms_window(L"GUI.Forms input settings fixture");
        require(hwnd != nullptr, "the native window exists");
        const std::uint32_t system = static_cast<std::uint32_t>(GetDoubleClickTime());
        const PresentationSettings& reported = window.presentation_settings();
        require(reported.double_click_milliseconds == system, "the window holds GetDoubleClickTime");
        require(reported.double_click_origin == SettingOrigin::host, "the interval is reported as a host value");
        unsigned changes = 0U;
        const std::shared_ptr<Component> owner = std::make_shared<Component>();
        const SubscriptionToken token = window.presentation_changed().subscribe(*owner, ChangeCounter{&changes});
        static_cast<void>(SendMessageW(hwnd, WM_SETTINGCHANGE, SPI_SETDOUBLECLICKTIME, 0));
        require(changes == 0U && window.presentation_settings().double_click_milliseconds == system,
                "a settings broadcast that leaves the interval alone changes nothing");
        *checked = true;
        require(handle.request_close().accepted(), "the fixture closes");
    }
};

} // namespace

int main() {
    // The program's application (APP_WINDOW_RULES_DESIGN A.2): windows made
    // from here join it, and it runs them.
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) return 1;
    Application& application = *created.application;
    try {
        std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
        bool checked = false;
        ApplicationWindowOptions options;
        options.title = "GUI.Forms input settings fixture";
        options.ready = Ready{&checked};
        const ApplicationResult result =
            application.run(std::make_unique<gui_forms::Window>(root, Size{200, 120}), options);
        if (result.callback_exception) std::rethrow_exception(result.callback_exception);
        require(result.accepted(), "the native fixture completed");
        require(checked, "the ready callback ran its checks");
        std::cout << "windows input settings tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
