#pragma once

#include "gui_forms/window.hpp"

#include <memory>
#include <string_view>

namespace gui_forms::gallery {

// Default window size: the larger of the two retired demonstration windows,
// so every migrated page keeps its design scale. Hosts still honour
// `--size`/`capture_size`.
inline constexpr Size gallery_default_size {1280.0, 820.0};
inline constexpr Size gallery_minimum_size {980.0, 680.0};
inline constexpr std::string_view gallery_program_name = "GUI.Forms Gallery";

// Builds the registry-driven control gallery: the form document's pages
// (demo/gallery.form), the pages migrated from the Complete Showcase,
// navigation derived from manifests/controls.json, and the diagnostics
// overlay. The retained controls
// own their models; callers only own the native Window boundary.
[[nodiscard]] std::unique_ptr<Window> make_gallery();

// Selects a page by its stable id (see demo/gallery_pages.hpp), exactly as the
// navigation row "gallery.page.<id>" does. Returns false for an unknown id.
bool select_gallery_page(Window& window, std::string_view page_id);

// The id of the page currently shown, or empty when `window` is not a Gallery.
[[nodiscard]] std::string_view selected_gallery_page(const Window& window);

}  // namespace gui_forms::gallery
