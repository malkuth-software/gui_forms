#pragma once

#include "page_context.hpp"

#include <array>
#include <memory>
#include <string_view>

// One builder per Gallery page that is not one of the window form's own
// pages (demo/gallery.form), each defined in
// src/controls/gallery/pages/<page-id>.cpp; `page_builders` below lists
// them by page id, and the shell (src/controls/gallery_pages.cpp) fills each
// page body from it.
namespace gui_forms::gallery::pages {

// control-spectrum
void add_control_spectrum(const std::shared_ptr<Surface>& page,
                          const std::shared_ptr<PageContext>& context);

// ranges-and-progress
void add_ranges(const std::shared_ptr<Surface>& page,
                const std::shared_ptr<PageContext>& context);

// containers-and-focus
void add_containers(const std::shared_ptr<Surface>& page,
                    const std::shared_ptr<PageContext>& context);

// retained-layout-panels
void add_layout_panels(const std::shared_ptr<Surface>& page,
                       const std::shared_ptr<PageContext>& context);

// css-layout-panels
void add_css_layout_panels(const std::shared_ptr<Surface>& page,
                           const std::shared_ptr<PageContext>& context);

// dock-and-anchor
void add_dock_and_anchor(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context);

// docking-workspace
void add_docking_workspace(const std::shared_ptr<Surface>& page,
                           const std::shared_ptr<PageContext>& context);

// animation-laboratory
void add_animation(const std::shared_ptr<Surface>& page,
                   const std::shared_ptr<PageContext>& context);

// states-and-diagnostics
void add_states(const std::shared_ptr<Surface>& page,
                const std::shared_ptr<PageContext>& context);

// text-and-input
void add_text_input(const std::shared_ptr<Surface>& page,
                    const std::shared_ptr<PageContext>& context);

// collections-and-overlays
void add_collections(const std::shared_ptr<Surface>& page,
                     const std::shared_ptr<PageContext>& context);

// values-and-spinners
void add_values(const std::shared_ptr<Surface>& page,
                const std::shared_ptr<PageContext>& context);

// images-and-drawing
void add_images_and_drawing(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context);

// tabs-and-pages
void add_tabs_and_pages(const std::shared_ptr<Surface>& page,
                        const std::shared_ptr<PageContext>& context);

// checked-collections
void add_checked_collections(const std::shared_ptr<Surface>& page,
                             const std::shared_ptr<PageContext>& context);

// timing-and-tooltips
void add_timing_and_tooltips(const std::shared_ptr<Surface>& page,
                             const std::shared_ptr<PageContext>& context);

// dates-and-calendar
void add_dates_and_calendar(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context);

// dialogs-and-host-services
void add_dialogs_and_host_services(
    const std::shared_ptr<Surface>& page,
    const std::shared_ptr<PageContext>& context);

void add_commands_and_menus(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context);
void add_tree_and_object_views(const std::shared_ptr<Surface>& page,
                               const std::shared_ptr<PageContext>& context);
void add_choices_and_sections(const std::shared_ptr<Surface>& page,
                              const std::shared_ptr<PageContext>& context);
void add_value_editors(const std::shared_ptr<Surface>& page,
                       const std::shared_ptr<PageContext>& context);
void add_responsive_layouts(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context);
void add_anchored_overlays(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context);
void add_live_surfaces(const std::shared_ptr<Surface>& page,
                       const std::shared_ptr<PageContext>& context);
void add_typography_and_inspection(const std::shared_ptr<Surface>& page,
                                   const std::shared_ptr<PageContext>& context);
void add_semantic_feedback(const std::shared_ptr<Surface>& page,
                           const std::shared_ptr<PageContext>& context);

// notices-and-badges
void add_notices_and_badges(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context);

// input-and-rules
void add_input_and_rules(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context);

// theme-settings
void add_theme_settings(const std::shared_ptr<Surface>& page,
                        const std::shared_ptr<PageContext>& context);

// Fills the body `page` of one page, reading and extending the shell's
// `context` (the status line, timers, providers, the Gallery's model).
using PageBuilder = void (*)(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context);

struct PageBuilderEntry final {
    std::string_view id{};
    PageBuilder build{};
};

// The page list's builders, by page id. A new page is one GalleryPageSpec
// (demo/gallery_page_specs.hpp, which places it in the navigation) and one
// row here; the shell refuses a page with neither a form page nor a row
// (std::logic_error at construction).
inline constexpr std::array<PageBuilderEntry, 30> page_builders{{
    {"control-spectrum", &add_control_spectrum},
    {"ranges-and-progress", &add_ranges},
    {"containers-and-focus", &add_containers},
    {"animation-laboratory", &add_animation},
    {"states-and-diagnostics", &add_states},
    {"text-and-input", &add_text_input},
    {"collections-and-overlays", &add_collections},
    {"values-and-spinners", &add_values},
    {"images-and-drawing", &add_images_and_drawing},
    {"tabs-and-pages", &add_tabs_and_pages},
    {"checked-collections", &add_checked_collections},
    {"timing-and-tooltips", &add_timing_and_tooltips},
    {"dates-and-calendar", &add_dates_and_calendar},
    {"dialogs-and-host-services", &add_dialogs_and_host_services},
    {"retained-layout-panels", &add_layout_panels},
    {"css-layout-panels", &add_css_layout_panels},
    {"dock-and-anchor", &add_dock_and_anchor},
    {"docking-workspace", &add_docking_workspace},
    {"commands-and-menus", &add_commands_and_menus},
    {"tree-and-object-views", &add_tree_and_object_views},
    {"choices-and-sections", &add_choices_and_sections},
    {"value-editors", &add_value_editors},
    {"responsive-layouts", &add_responsive_layouts},
    {"anchored-overlays", &add_anchored_overlays},
    {"live-surfaces", &add_live_surfaces},
    {"typography-and-inspection", &add_typography_and_inspection},
    {"semantic-feedback", &add_semantic_feedback},
    {"notices-and-badges", &add_notices_and_badges},
    {"input-and-rules", &add_input_and_rules},
    {"theme-settings", &add_theme_settings},
}};

} // namespace gui_forms::gallery::pages
