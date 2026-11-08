// The application instance (APP_WINDOW_RULES_DESIGN Part A): create and its
// refusals, the one current application, close while windows live, windows
// joining it (implicitly, explicitly, with their own image quotas, from
// another thread), the theme compiled at create, the presentation defaults,
// and an application destroyed before its windows.

#include "gui_forms/application.hpp"
#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Control::Ptr make_root(const std::string& id) {
    Control::Ptr root = make_control<Panel>(StableId(id));
    return root;
}

void test_no_application() {
    require(Application::current() == nullptr, "no application is current at first");
    Window window(make_root("instance.none"), Size{200.0, 100.0});
    require(window.application() == nullptr && window.resources() != nullptr,
            "a window without an application makes its own resources");
}

void test_create_and_refusals() {
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    require(static_cast<bool>(created) && created.error == ApplicationCreateError::none &&
                Application::current() == created.application.get(),
            "create makes the current application");
    const ApplicationCreateResult second = Application::create(ApplicationConfig{});
    require(!second && second.error == ApplicationCreateError::already_exists &&
                Application::current() == created.application.get(),
            "a second application is refused, changing nothing");
    created.application.reset();
    require(Application::current() == nullptr, "destroying the application ends its time as current");

    ApplicationConfig two{};
    two.theme.packages.resize(2U);
    const ApplicationCreateResult several = Application::create(std::move(two));
    require(!several && several.error == ApplicationCreateError::invalid_argument &&
                Application::current() == nullptr,
            "several packages are refused until the multi-package compile");

    ApplicationConfig broken{};
    broken.theme.packages.push_back(ThemePackage{.manifest = "not a manifest", .files = {}});
    const ApplicationCreateResult refused = Application::create(std::move(broken));
    require(!refused && refused.error == ApplicationCreateError::theme_refused && !refused.findings.empty() &&
                Application::current() == nullptr,
            "a refused theme makes nothing and says why");
}

void test_windows_join_and_close() {
    ApplicationConfig config{};
    PresentationSettings presentation{};
    presentation.text_scale = 1.5;
    config.presentation = presentation;
    ApplicationCreateResult created = Application::create(std::move(config));
    require(static_cast<bool>(created), "the application is made");
    Application& application = *created.application;
    {
        Window implicit(make_root("instance.implicit"), Size{200.0, 100.0});
        Window explicit_window(application, make_root("instance.explicit"), Size{200.0, 100.0});
        Window limited(make_root("instance.limited"), Size{200.0, 100.0}, ImageRegistryLimits{});
        require(implicit.application() == &application && explicit_window.application() == &application &&
                    limited.application() == &application && application.window_count() == 3U,
                "windows made while the application is current join it");
        require(implicit.resources() == application.resources() &&
                    explicit_window.resources() == application.resources(),
                "joined windows share the application's resources");
        require(limited.resources() != application.resources() &&
                    limited.theme_ptr() == (*application.resources()).program_theme_ptr(),
                "a window with its own quotas has its own resources and the application's theme");
        require(implicit.presentation_settings().text_scale == 1.5,
                "a joined window starts from the application's presentation");
        const ApplicationResult refused = application.close();
        require(refused.error == ApplicationError::windows_alive && !application.closed() &&
                    Application::current() == &application,
                "close is refused while windows live, changing nothing");
    }
    require(application.window_count() == 0U, "destroyed windows leave the application");
    require(application.close().accepted() && application.closed() && Application::current() == nullptr,
            "close ends the application's time as current");
    require(application.close().accepted(), "closing again does nothing");
    Window after(make_root("instance.after"), Size{200.0, 100.0});
    require(after.application() == nullptr, "a window made after close joins nothing");
    bool refused_explicit = false;
    try {
        Window late(application, make_root("instance.late"), Size{200.0, 100.0});
    } catch (const std::logic_error&) {
        refused_explicit = true;
    }
    require(refused_explicit, "an explicit window refuses a closed application");
    ApplicationCreateResult next = Application::create(ApplicationConfig{});
    require(static_cast<bool>(next), "another application may be made after close");
}

void test_base_theme() {
    ApplicationConfig config{};
    config.theme.base = ThemeCompileBase::gui_forms;
    ApplicationCreateResult created = Application::create(std::move(config));
    require(static_cast<bool>(created) && (*(*created.application).resources()).theme_ptr() == gui_forms_theme(),
            "the gui-forms base alone is the built gui-forms theme, compiled once");
}

// Makes a window on another thread and records whether it joined.
class ForeignWindow final {
public:
    explicit ForeignWindow(bool& joined) noexcept : joined_(joined) {}
    void operator()() const {
        Window window(make_root("instance.foreign"), Size{120.0, 80.0});
        joined_ = window.application() != nullptr;
    }

private:
    bool& joined_;
};

void test_other_thread_does_not_join() {
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    require(static_cast<bool>(created), "the application is made");
    bool joined = true;
    std::thread other(ForeignWindow{joined});
    other.join();
    require(!joined && (*created.application).window_count() == 0U,
            "a window made on another thread does not join the application");
}

void test_application_destroyed_first() {
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    require(static_cast<bool>(created), "the application is made");
    const std::shared_ptr<ApplicationResources> shared = (*created.application).resources();
    Window window(make_root("instance.outlives"), Size{200.0, 100.0});
    created.application.reset();
    require(window.application() == nullptr && window.resources() == shared && Application::current() == nullptr,
            "a window outliving its application keeps its resources and forgets the application");
    window.perform_layout();
}

} // namespace

int main() {
    try {
        test_no_application();
        test_create_and_refusals();
        test_windows_join_and_close();
        test_base_theme();
        test_other_thread_does_not_join();
        test_application_destroyed_first();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "application instance tests passed\n";
    return EXIT_SUCCESS;
}
