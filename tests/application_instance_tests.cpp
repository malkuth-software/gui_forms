// The application instance (APP_WINDOW_RULES_DESIGN Part A): create and its
// refusals, the one current application, close while windows live, windows
// joining it (implicitly, explicitly, with their own image quotas, from
// another thread), the theme compiled at create, the presentation defaults,
// and an application destroyed before its windows.

#include "../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/application.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/gui_forms.hpp"
#include "support/theme_definition_heap.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

    ApplicationConfig too_many{};
    too_many.theme.packages.resize(maximum_theme_packages + 1U);
    const ApplicationCreateResult several = Application::create(std::move(too_many));
    require(!several && several.error == ApplicationCreateError::theme_refused && !several.findings.empty() &&
                Application::current() == nullptr,
            "more packages than the compile takes are refused, making nothing");

    ApplicationConfig broken{};
    broken.theme.packages.push_back(ThemePackage{.manifest = "not a manifest", .files = {}});
    const ApplicationCreateResult refused = Application::create(std::move(broken));
    require(!refused && refused.error == ApplicationCreateError::theme_refused && !refused.findings.empty() &&
                Application::current() == nullptr,
            "a refused theme makes nothing and says why");

    // EP-3: given the effect registry, a package naming an effect it does
    // not hold refuses the theme, with E417 among the findings.
    const std::string sheet = "button { effect: wobble-band }";
    ThemePackage effect_package{};
    effect_package.manifest =
        "{\"format\": \"gui-forms-theme/1\", \"name\": \"fx\", \"version\": \"1.0.0\", \"engine\": "
        "\">=0.3\", \"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": \"" +
        theme_language::package_sha256_hex(theme_language::package_sha256(sheet)) + "\"}}";
    effect_package.files.push_back(ThemePackageFile{"theme.theme.css", sheet});
    ApplicationConfig unknown_effect{};
    unknown_effect.theme.packages.push_back(effect_package);
    unknown_effect.theme.effects = &built_in_effect_registry();
    const ApplicationCreateResult no_effect = Application::create(std::move(unknown_effect));
    bool named = false;
    for (const ThemeCompileDiagnostic& finding : no_effect.findings) {
        if (finding.code == "E417" && finding.detail == "wobble-band") named = true;
    }
    require(!no_effect && no_effect.error == ApplicationCreateError::theme_refused && named &&
                Application::current() == nullptr,
            "an effect the registry does not hold refuses the theme with E417");
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
    require(default_theme_base == ThemeBase::gui_forms && ApplicationConfig{}.theme.base == ThemeBase::gui_forms,
            "the default base is gui-forms (T4 phase 6)");
    ApplicationConfig config{};
    config.theme.base = ThemeBase::windows_professional;
    ApplicationCreateResult created = Application::create(std::move(config));
    require(static_cast<bool>(created) &&
                (*(*created.application).resources()).theme().id() == "windows-professional" &&
                (*(*created.application).resources()).theme_ptr() != default_compiled_theme(),
            "the windows-professional base alone is compiled as the application's theme");
    created.application.reset();
    ApplicationCreateResult plain = Application::create(ApplicationConfig{});
    require(static_cast<bool>(plain) && (*(*plain.application).resources()).theme_ptr() == default_compiled_theme() &&
                (*(*plain.application).resources()).theme().id() == "gui-forms",
            "the default base alone is the process default theme, gui-forms, not compiled again");
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

// Records which window raised presentation_changed, in order.
class PresentationOrder final {
public:
    PresentationOrder(std::vector<int>& order, int window) noexcept : order_(order), window_(window) {}
    void operator()(const PresentationSettings&) const { order_.push_back(window_); }

private:
    std::vector<int>& order_;
    int window_;
};

void test_presentation_and_overrides() {
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    require(static_cast<bool>(created), "the application is made");
    Application& application = *created.application;
    Window first(make_root("instance.first"), Size{200.0, 100.0});
    Window second(make_root("instance.second"), Size{200.0, 100.0}, ImageRegistryLimits{});
    std::vector<int> order;
    SubscriptionToken first_changed = first.presentation_changed().subscribe(PresentationOrder(order, 1));
    SubscriptionToken second_changed = second.presentation_changed().subscribe(PresentationOrder(order, 2));
    require(first.presentation_overrides() == nullptr, "a window without overrides stores none");

    PresentationSettings settings{};
    settings.text_scale = 2.0;
    settings.high_contrast = true;
    application.set_presentation_settings(settings);
    require(first.presentation_settings() == settings && second.presentation_settings() == settings &&
                order == std::vector<int>{1, 2},
            "the application's presentation reaches every joined window in joining order");

    first.set_presentation_overrides(PresentationOverrides{.text_scale = 1.25});
    require(first.presentation_overrides() != nullptr && first.presentation_settings().text_scale == 1.25 &&
                first.presentation_settings().high_contrast && first.presentation_base() == settings,
            "an override wins over its field only");
    settings.text_scale = 3.0;
    settings.high_contrast = false;
    application.set_presentation_settings(settings);
    require(first.presentation_settings().text_scale == 1.25 && !first.presentation_settings().high_contrast &&
                second.presentation_settings().text_scale == 3.0,
            "an application change keeps a window's overrides and reaches its other fields");

    bool refused = false;
    try {
        first.set_presentation_overrides(PresentationOverrides{.text_scale = 9.0});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && first.presentation_settings().text_scale == 1.25, "an invalid override changes nothing");
    first.set_presentation_overrides(PresentationOverrides{});
    require(first.presentation_overrides() == nullptr && first.presentation_settings().text_scale == 3.0,
            "an empty record releases the overrides");

    bool refused_application = false;
    PresentationSettings invalid = settings;
    invalid.text_scale = 0.0;
    try {
        application.set_presentation_settings(invalid);
    } catch (const std::invalid_argument&) {
        refused_application = true;
    }
    require(refused_application && application.presentation_settings() == settings,
            "an invalid application presentation changes nothing");

    const std::shared_ptr<const Theme> theme = gui_forms_theme();
    application.set_theme(theme);
    require(first.theme_ptr() == theme && second.theme_ptr() == theme,
            "the application's theme reaches shared and own resources");
}

// Copy-on-write overrides and lazy key tables (APP_WINDOW_RULES_DESIGN
// K.1): a window stores no override and makes no key table until it
// overrides something; a joined window reads its application's table by
// reference; a window-local rebinding copies it and leaves the application
// and its other windows alone; clearing the last override releases the
// record; a theme override is the window's alone.
void test_window_overrides() {
    {
        Window alone(make_root("overrides.alone"), Size{200.0, 100.0});
        require(alone.overrides() == nullptr && alone.key_bindings() == nullptr && alone.key_map() == pc_key_map(),
                "a window alone stores no override and reads the PC convention");
        const KeyBinding rebound{{PhysicalKey::w, Modifier::meta}, {}};
        alone.rebind_key_command(KeyCommand::close_document, rebound);
        require(alone.overrides() != nullptr && alone.key_bindings() != nullptr &&
                    alone.key_map().binding(KeyCommand::close_document) == rebound,
                "a rebinding makes the window's own table");
        alone.clear_key_bindings();
        require(alone.overrides() == nullptr && alone.key_map() == pc_key_map(),
                "clearing the table releases the record");
        bool refused = false;
        try {
            alone.rebind_key_command(static_cast<KeyCommand>(255U), rebound);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        require(refused && alone.overrides() == nullptr, "a refused rebinding stores nothing");
    }

    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    require(static_cast<bool>(created), "overrides: the application is made");
    if (!created) return;
    Application& application = *created.application;
    {
        Window first(make_root("overrides.first"), Size{200.0, 100.0});
        Window second(make_root("overrides.second"), Size{200.0, 100.0});
        require(first.overrides() == nullptr && first.key_bindings() == application.key_bindings(),
                "a joined window reads its application's table, storing nothing");
        first.set_key_bindings(application.key_bindings());
        require(first.overrides() == nullptr, "sharing the table it reads stores nothing");
        require(first.share_key_bindings() == application.key_bindings() && first.overrides() == nullptr,
                "sharing a joined window's table hands out the application's");

        const KeyBinding rebound{{PhysicalKey::w, Modifier::meta}, {}};
        first.rebind_key_command(KeyCommand::close_document, rebound);
        require(first.overrides() != nullptr && first.key_map().binding(KeyCommand::close_document) == rebound,
                "a window-local rebinding copies the table");
        require((*application.key_bindings()).map() == pc_key_map() && second.key_map() == pc_key_map(),
                "the application's table and its other windows are unchanged");

        first.set_presentation_overrides(PresentationOverrides{.text_scale = 1.5});
        first.clear_key_bindings();
        require(first.overrides() != nullptr && first.key_bindings() == application.key_bindings(),
                "the record stays while another field is overridden");
        first.set_presentation_overrides(PresentationOverrides{});
        require(first.overrides() == nullptr, "clearing the last override releases the record");

        const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
        (*definition).id = "overrides.window-only";
        const std::shared_ptr<const Theme> own = Theme::create(*definition);
        const std::shared_ptr<const Theme> shared = first.theme_ptr();
        first.set_theme_override(own);
        require(first.overrides() != nullptr && first.theme_ptr() == own && second.theme_ptr() == shared,
                "a theme override is the window's alone");
        application.set_theme(gui_forms_theme());
        require(first.theme_ptr() == own && second.theme_ptr() == gui_forms_theme(),
                "the application's theme does not replace a window's override");
        first.set_theme_override(nullptr);
        require(first.overrides() == nullptr && first.theme_ptr() == gui_forms_theme(),
                "cleared, the window follows its resources' theme again");
    }
    const ApplicationResult closed = application.close();
    require(closed.accepted(), "overrides: the application closes");
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
        test_presentation_and_overrides();
        test_window_overrides();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "application instance tests passed\n";
    return EXIT_SUCCESS;
}
