#include "support.hpp"
#include "gui_forms/controls/label.hpp"

namespace gallery_pages_tests {

// Every registry record that names a Gallery page must resolve to a built
// page, every page must describe itself in its header strip, and the
// navigation must list each page exactly once.
void test_catalog_pages_resolve_and_headers_describe_pages() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    for (const gallery::CatalogEntry& entry : gallery::control_catalog) {
        if (entry.gallery_page.empty()) continue;
        require(gallery::find_gallery_page(entry.gallery_page) != nullptr,
                "every catalog gallery_page must name a declared Gallery page");
        require(window.find(std::string(gallery::surface_id_prefix) +
                            std::string(entry.gallery_page)) != nullptr,
                "every catalog gallery_page must resolve to a built page surface");
    }
    std::size_t pages_with_controls = 0;
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        const std::string surface_id =
            std::string(gallery::surface_id_prefix) + std::string(page.id);
        require(window.find(std::string(gallery::navigation_id_prefix) +
                            std::string(page.id)) != nullptr,
                "every page must have a navigation row");
        const std::shared_ptr<Label> title =
            std::dynamic_pointer_cast<Label>(window.find(surface_id + ".title"));
        require(title != nullptr && (*title).text.read() == page.title,
                "every page header must carry the page title");
        const std::size_t listed = gallery::catalog_entries_on_page(page.id);
        if (listed == 0U) {
            const std::shared_ptr<Label> summary =
                std::dynamic_pointer_cast<Label>(window.find(surface_id + ".summary"));
            require(summary != nullptr && !(*summary).text.read().empty(),
                    "a page no registry record names must state what it demonstrates");
            continue;
        }
        ++pages_with_controls;
        for (std::size_t line = 0; line < listed; ++line) {
            const std::shared_ptr<Label> control_line = std::dynamic_pointer_cast<Label>(
                window.find(surface_id + ".control." + std::to_string(line)));
            require(control_line != nullptr &&
                        (*control_line).text.read().find(" · WinForms: ") != std::string::npos,
                    "every page header must list each demonstrated control as a "
                    "Type · header · WinForms line");
        }
        require(window.find(surface_id + ".control." + std::to_string(listed)) == nullptr,
                "a page header must list exactly the controls assigned to it");
        require(gallery::find_catalog_entry("button") != nullptr,
                "catalog lookup by registry id must work");
    }
    require(pages_with_controls >= 14U,
            "most Gallery pages must demonstrate at least one registry control");
    require(gallery::select_gallery_page(window, "basics") &&
                gallery::selected_gallery_page(window) == "basics" &&
                (*window.find("gallery.surface.basics")).effectively_visible() &&
                !(*window.find("gallery.surface.control-spectrum")).effectively_visible(),
            "select_gallery_page must switch pages by stable id");
    require(!gallery::select_gallery_page(window, "no-such-page"),
            "select_gallery_page must reject unknown ids");
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        require(!gallery::gallery_page_group(page).empty(),
                "every page must derive a navigation group");
        require(!page.lab || gallery::gallery_page_group(page) == gallery::labs_group,
                "lab pages must be grouped under Labs");
    }
}

} // namespace gallery_pages_tests
