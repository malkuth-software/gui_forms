// A C++ GUI.Forms program with no theme code of its own (it installs the
// stock controls' effect registry while it sets up, as any program using
// them does, so a requested sheet's effect names are checked), started by CTest
// with --theme <name|built-in|path.css> or GUI_FORMS_THEME_SHEET: it makes
// its application and one window, and checks what the request did. Its
// expectations come from the environment, so its command line holds only
// what a user would type:
//   GF_EXPECT_THEME_REQUEST  none, built_in, applied, ignored_by_policy,
//                            unknown_name, refused, unreadable or malformed;
//   GF_TEST_POLICY           refused or replaces, chosen before the window
//                            (layered when unset).
// Exit 0 when the request did what was expected.
#include "gui_forms/application/application.hpp"
#include "gui_forms/application/user_themes/user_themes.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "gui_forms_theme_request_app: " << message << '\n';
        ++failures;
    }
}

std::string_view environment(const char* name) {
    const char* const value = std::getenv(name);
    const std::string_view text = value == nullptr ? std::string_view{} : std::string_view(value);
    return text;
}

std::string_view outcome_name(ThemeRequestOutcome outcome) {
    switch (outcome) {
    case ThemeRequestOutcome::none: return "none";
    case ThemeRequestOutcome::built_in: return "built_in";
    case ThemeRequestOutcome::applied: return "applied";
    case ThemeRequestOutcome::ignored_by_policy: return "ignored_by_policy";
    case ThemeRequestOutcome::unknown_name: return "unknown_name";
    case ThemeRequestOutcome::refused: return "refused";
    case ThemeRequestOutcome::unreadable: return "unreadable";
    case ThemeRequestOutcome::malformed: return "malformed";
    }
    return "unknown";
}

} // namespace

int main() {
    const std::string_view expected = environment("GF_EXPECT_THEME_REQUEST");
    const std::string_view policy = environment("GF_TEST_POLICY");
    require(!expected.empty(), "GF_EXPECT_THEME_REQUEST is not set");
    install_built_in_effect_registry();
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    require(static_cast<bool>(created), "the application is made");
    if (!created) return 1;
    const std::unique_ptr<Application> application = std::move(created.application);
    ApplicationResources& resources = *(*application).resources();
    if (policy == "refused") resources.set_user_theme_policy(UserThemePolicy::refused);
    if (policy == "replaces") resources.set_user_theme_policy(UserThemePolicy::replaces_program_sheet);
    require(resources.theme_request_held() == (expected != "none"), "the request is held until the first window");
    {
        const Window window(make_control<Control>(StableId(std::string("theme-request-app.root"))), Size{160.0, 90.0});
        const ThemeRequestReport* const report = resources.theme_request_report();
        const ThemeRequestOutcome outcome = report == nullptr ? ThemeRequestOutcome::none : (*report).outcome;
        if (outcome_name(outcome) != expected) {
            std::cerr << "gui_forms_theme_request_app: the request was " << outcome_name(outcome) << ", expected "
                      << expected << '\n';
            ++failures;
        }
        const bool applied = outcome == ThemeRequestOutcome::applied;
        require(applied == (resources.user_theme() != nullptr), "a user theme is selected exactly when applied");
        if (applied && policy == "replaces") {
            require(resources.theme_ptr() == resources.user_theme_ptr(), "a replacing theme is the windows' theme");
        }
        // The program's own arguments, without the user-theme options.
        const std::vector<std::string> arguments = process_arguments();
        std::vector<std::string_view> views{};
        for (const std::string& argument : arguments) views.push_back(argument);
        for (const std::string_view kept : program_arguments(views)) {
            require(kept != "--theme", "program_arguments passes over --theme");
        }
    }
    require((*application).close().accepted(), "the application closes");
    if (failures != 0) return 1;
    std::cout << "gui_forms_theme_request_app: " << expected << '\n';
    return 0;
}
