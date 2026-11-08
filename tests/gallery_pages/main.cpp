#include "page_tests.hpp"
#include "support.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace gallery_pages_tests {
namespace {

// Each wrapper builds the window (and host) exactly as the former
// single-file test function did, then runs that function's page sections
// in their original order.
void test_ranges_containers_and_animation() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    test_ranges_and_progress(window);
    test_containers_and_focus(window);
    test_animation_laboratory(window);
}

void test_text_collections_and_overlay_lifecycle() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    host::HeadlessHost host(window);
    test_text_and_input(window, host);
    test_collections_and_overlays(window);
    test_values_and_spinners(window);
    test_images_and_drawing(window);
    test_tabs_and_pages(window);
    test_checked_collections(window);
}

void test_timing_and_tooltip_runtime() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    test_timing_and_tooltips(window);
}

void test_binding_source_gallery_runtime() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    test_values_and_spinners_binding(window);
}

void test_date_time_picker_gallery() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    test_dates_and_calendar(window);
}

void test_dialogs_and_host_services_gallery() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    host::HeadlessHost host(window);
    host::HeadlessHostServices& services =
        static_cast<host::HeadlessHostServices&>(host.services());
    bool cancel_save = false;
    services.set_dialog_handler(GalleryDialogHandler(cancel_save));
    test_dialogs_and_host_services(window, services, cancel_save);
}

void test_dispatcher_gallery() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    host::HeadlessHost host(window);
    test_states_and_diagnostics(window, host);
}

void test_dock_anchor_gallery_interaction() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    test_dock_and_anchor(window);
    test_docking_workspace(window);
}

void test_registry_pages_gallery() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    test_commands_and_menus(window);
    test_tree_and_object_views(window);
    test_choices_and_sections(window);
    test_value_editors(window);
    test_responsive_layouts(window);
    test_anchored_overlays(window);
    test_live_surfaces(window);
    test_typography_and_inspection(window);
    test_semantic_feedback(window);
    test_notices_and_badges(window);
    test_retained_layout_panels(window);
    test_css_layout_panels(window);
    test_control_spectrum(window);
}

// The input-and-rules and theme-settings pages change the window (its
// rules, its theme and presentation overrides), so they run in a window
// of their own.
void test_input_and_theme_gallery() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    test_input_and_rules(window);
    test_theme_settings(window);
}

} // namespace
} // namespace gallery_pages_tests

using namespace gallery_pages_tests;

int main(int argc, char** argv) {
    try {
        // The display trace runs alone, in a process of its own (its own
        // ctest entry, gui_forms_gallery_display_trace_tests): the
        // typography page shows a RuntimeId, which counts every control the
        // process made before it, so no other test may run first.
        if (argc == 2 && std::string_view(argv[1]) == "--display-trace") {
            test_display_commands_unchanged();
            std::cout << "gui_forms_gallery_pages_tests --display-trace: passed\n";
            return EXIT_SUCCESS;
        }
        test_page_contract_and_rendering();
        test_catalog_pages_resolve_and_headers_describe_pages();
        test_gallery_semantic_surface();
        test_ranges_containers_and_animation();
        test_text_collections_and_overlay_lifecycle();
        test_timing_and_tooltip_runtime();
        test_binding_source_gallery_runtime();
        test_date_time_picker_gallery();
        test_dialogs_and_host_services_gallery();
        test_dispatcher_gallery();
        test_dock_anchor_gallery_interaction();
        test_registry_pages_gallery();
        test_input_and_theme_gallery();
        test_anatomy_coverage();
        test_schema_export();
        std::cout << "gui_forms_gallery_pages_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_gallery_pages_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
