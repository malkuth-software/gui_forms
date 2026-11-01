#pragma once

#include "page_context.hpp"

#include <memory>

// One builder per Gallery page migrated from the retired Complete Showcase,
// each defined in src/controls/gallery/pages/<page-id>.cpp. The shell's
// `populate_page_body` (src/controls/gallery_pages.cpp) dispatches on the
// page id named above each declaration.
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

// collections-and-popups
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
void add_anchored_popups(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context);
void add_live_surfaces(const std::shared_ptr<Surface>& page,
                       const std::shared_ptr<PageContext>& context);
void add_typography_and_inspection(const std::shared_ptr<Surface>& page,
                                   const std::shared_ptr<PageContext>& context);
void add_semantic_feedback(const std::shared_ptr<Surface>& page,
                           const std::shared_ptr<PageContext>& context);

} // namespace gui_forms::gallery::pages
