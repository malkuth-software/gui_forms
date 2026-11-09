#pragma once

#include "gallery/context/gallery_context.hpp"
#include "gui_forms/gui_forms.hpp"

#include <memory>
#include <string_view>

namespace gui_forms::gallery {

// Completes the Gallery window's shell (demo/gallery.form) from the page
// list: navigation rows grouped by catalog category in gallery.sidebar, and
// one retained surface per page in gallery.content, whose body holds the
// form's own page group (moved out of the gallery.form-pages boundary,
// which is then removed) or what the page's builder adds (pages::
// page_builders). The shell's page context, stored in the root's Tag, owns
// `compiled`; the navigation and the live-motion switch are window rules.
// Call once, after compiled->attach(window). Throws std::logic_error for a
// page with neither a form page nor a builder, or a refused rule.
void build_gallery_shell(GalleryWindow& window, std::shared_ptr<GalleryContext> compiled);

// Attaches the window-owned components (UI Timer, ToolTip, providers, binding
// source) that pages declared while building. Idempotent per window.
void initialize_gallery_runtime(Window& window);

} // namespace gui_forms::gallery
