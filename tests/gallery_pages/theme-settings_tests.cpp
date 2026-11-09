#include "page_tests.hpp"

#include <string>

namespace gallery_pages_tests {

// The theme-settings page applies its choices to the running Gallery: a
// theme for every window, for this window alone (the window's override,
// leaving the resources' theme), or for the preview (a control override);
// dark is offered only for a theme with a dark variant; the scheme radios
// write this window's presentation overrides; Restore returns everything.
void test_theme_settings(gui_forms::Window& window) {
    select_page(window, "theme-settings");
    const std::shared_ptr<ListBox> themes =
        std::dynamic_pointer_cast<ListBox>(window.find("gallery.theme-settings.themes"));
    const std::shared_ptr<RadioButton> light =
        std::dynamic_pointer_cast<RadioButton>(window.find("gallery.theme-settings.scheme.light"));
    const std::shared_ptr<RadioButton> dark =
        std::dynamic_pointer_cast<RadioButton>(window.find("gallery.theme-settings.scheme.dark"));
    const Control::Ptr high_contrast = window.find("gallery.theme-settings.scheme.high-contrast");
    const Control::Ptr this_window = window.find("gallery.theme-settings.scope.this-window");
    const Control::Ptr preview_scope = window.find("gallery.theme-settings.scope.preview");
    const Control::Ptr preview = window.find("gallery.theme-settings.preview");
    const Control::Ptr restore = window.find("gallery.theme-settings.restore");
    require(themes && light && dark && high_contrast && this_window && preview_scope && preview && restore,
            "the theme-settings page publishes its choices by stable id");
    require((*themes).items.read().size() == 7U, "the list offers the seven themes");
    const std::string original((*window.resources()).program_theme().id());

    // Every window: the resources' theme changes.
    (*themes).select_index(2U);
    require(window.theme().id() == "classic" && (*window.resources()).program_theme().id() == "classic",
            "classic applies to every window sharing the resources");
    require(!(*dark).effectively_enabled(), "a light-only theme offers no dark scheme");
    (*themes).select_index(6U);
    require(window.theme().id() == "macos-style", "macos-style applies to every window");
    require((*dark).effectively_enabled(), "macos-style has a dark variant, so dark is offered");
    click(window, dark);
    const PresentationOverrides* overrides = window.presentation_overrides();
    require(overrides != nullptr && (*overrides).colour_scheme == ColourScheme::dark,
            "the dark radio writes this window's colour-scheme override");
    click(window, light);
    overrides = window.presentation_overrides();
    require(overrides != nullptr && (*overrides).colour_scheme == ColourScheme::light,
            "the light radio writes the light override");

    // This window alone: the window's override; the resources keep theirs.
    click(window, this_window);
    (*themes).select_index(2U);
    require(window.theme().id() == "classic", "the window override shows classic in this window");
    require((*window.resources()).program_theme().id() == "macos-style",
            "a window override leaves the resources' theme as it was");

    // The preview alone: a control override, nothing else changes.
    click(window, preview_scope);
    (*themes).select_index(4U);
    const std::shared_ptr<const Theme> preview_theme = (*preview).theme_override();
    require(preview_theme && (*preview_theme).id() == "xp-style", "the preview shows xp-style alone");
    require(window.theme().id() == "classic", "the preview scope leaves the window override");

    click(window, high_contrast);
    overrides = window.presentation_overrides();
    require(overrides != nullptr && (*overrides).high_contrast == std::optional<bool>(true),
            "the high-contrast radio writes the window's high-contrast override");

    // Restore: the Gallery's own theme everywhere, no override left.
    click(window, restore);
    require((*window.resources()).program_theme().id() == original && window.theme().id() == original,
            "Restore returns every window to the theme the Gallery started with");
    require(!(*preview).theme_override(), "Restore clears the preview's override");
    overrides = window.presentation_overrides();
    require(overrides == nullptr || (!(*overrides).colour_scheme && !(*overrides).high_contrast),
            "Restore clears the scheme overrides the page set");
    require((*light).checked.read(), "Restore checks Light again");
}

} // namespace gallery_pages_tests
