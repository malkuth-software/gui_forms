#pragma once

#include "support.hpp"

// Gallery-wide tests (catalog_tests.cpp, shell_tests.cpp) build their own
// window. Page tests receive the window (and host, where the former
// single-file test created one) from the sequencing wrappers in main.cpp.
namespace gallery_pages_tests {

void test_catalog_pages_resolve_and_headers_describe_pages();
void test_page_contract_and_rendering();
void test_gallery_semantic_surface();
void test_anatomy_coverage();
void test_schema_export();
void test_every_page_scrolls_to_its_end();
void test_display_commands_unchanged();

void test_ranges_and_progress(gui_forms::Window& window);
void test_containers_and_focus(gui_forms::Window& window);
void test_animation_laboratory(gui_forms::Window& window);
void test_text_and_input(gui_forms::Window& window,
                         gui_forms::host::HeadlessHost& host);
void test_collections_and_overlays(gui_forms::Window& window);
void test_values_and_spinners(gui_forms::Window& window);
void test_values_and_spinners_binding(gui_forms::Window& window);
void test_images_and_drawing(gui_forms::Window& window);
void test_tabs_and_pages(gui_forms::Window& window);
void test_checked_collections(gui_forms::Window& window);
void test_timing_and_tooltips(gui_forms::Window& window);
void test_dates_and_calendar(gui_forms::Window& window);
void test_dialogs_and_host_services(gui_forms::Window& window,
                                    gui_forms::host::HeadlessHostServices& services,
                                    bool& cancel_save);
void test_states_and_diagnostics(gui_forms::Window& window,
                                 gui_forms::host::HeadlessHost& host);
void test_dock_and_anchor(gui_forms::Window& window);
void test_docking_workspace(gui_forms::Window& window);
void test_commands_and_menus(gui_forms::Window& window);
void test_tree_and_object_views(gui_forms::Window& window);
void test_choices_and_sections(gui_forms::Window& window);
void test_value_editors(gui_forms::Window& window);
void test_responsive_layouts(gui_forms::Window& window);
void test_anchored_overlays(gui_forms::Window& window);
void test_live_surfaces(gui_forms::Window& window);
void test_typography_and_inspection(gui_forms::Window& window);
void test_semantic_feedback(gui_forms::Window& window);
void test_notices_and_badges(gui_forms::Window& window);
void test_retained_layout_panels(gui_forms::Window& window);
void test_css_layout_panels(gui_forms::Window& window);
void test_control_spectrum(gui_forms::Window& window);
void test_input_and_rules(gui_forms::Window& window);
void test_theme_settings(gui_forms::Window& window);
void test_spinners_and_rings(gui_forms::Window& window);

} // namespace gallery_pages_tests
