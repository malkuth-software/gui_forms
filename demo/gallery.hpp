#pragma once

#include "gui_forms/application.hpp"
#include "gui_forms/window.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace gui_forms::gallery {

// Default window size: the larger of the two retired demonstration windows,
// so every migrated page keeps its design scale. Hosts still honour
// `--size`/`capture_size`.
inline constexpr Size gallery_default_size {1280.0, 820.0};
inline constexpr Size gallery_minimum_size {980.0, 680.0};
inline constexpr std::string_view gallery_program_name = "GUI.Forms Gallery";

// The Gallery program's application (APP_WINDOW_RULES_DESIGN A.5): its
// project theme (demo/themes/gallery) compiled once onto the base. The
// package is embedded and checked at build time, so a refusal or any finding
// is a defect of the build: std::logic_error, naming the first. Each
// gallery_main makes it before anything else.
// `base` is the theme the project sheet is compiled onto (the --theme-base
// option; gallery_theme_base).
[[nodiscard]] std::unique_ptr<Application> create_gallery_application(ThemeBase base = default_theme_base);

// The base a --theme-base id names: "gui-forms" or "windows-professional"; empty
// is the default base. Empty optional for any other id.
[[nodiscard]] std::optional<ThemeBase> gallery_theme_base(std::string_view id) noexcept;

// Builds the control gallery: the window generated from demo/gallery.form
// (GalleryWindow, bound to its GalleryModel), its shell completed from the
// page list (navigation derived from manifests/controls.json; the form's own
// pages and the page builders), and the diagnostics overlay. The window
// owns the model and the program state through its root, so callers own
// only the Window. It joins the current application
// (create_gallery_application) and takes its theme; with none (tests) it
// compiles the Gallery theme into its own resources.
[[nodiscard]] std::unique_ptr<Window> make_gallery();

// Selects a page by its stable id (see demo/gallery_pages.hpp), exactly as the
// navigation row "gallery.page.<id>" does. Returns false for an unknown id.
bool select_gallery_page(Window& window, std::string_view page_id);

// The id of the page currently shown, or empty when `window` is not a Gallery.
[[nodiscard]] std::string_view selected_gallery_page(const Window& window);

// Capture staging (the Gallery's --reveal and --open-menu options), applied
// after select_gallery_page and before the window is shown, so the first
// captured frame shows them.
//
// reveal_gallery_control scrolls the shown page so the control with
// `stable_id` sits at the top of its viewport (eight units below it).
// Returns false when no control of the shown page has that id or the page
// does not scroll that far; the page then stays where the scroll left it.
bool reveal_gallery_control(Window& window, std::string_view stable_id);

// open_gallery_menu opens a menu of the control with `stable_id`: a
// MenuStrip's menu at index `item` ("0" when empty), or a ToolStrip's or a
// Ribbon's drop-down named `item`. Returns false when there is no such
// control, it is of another kind, or the menu did not open.
bool open_gallery_menu(Window& window, std::string_view stable_id, std::string_view item);

}  // namespace gui_forms::gallery
