#include "page_tests.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace gallery_pages_tests {

namespace {

// The lowest bottom edge, in window coordinates, of the shown direct
// children of a page body: the page's last control.
double lowest_child_bottom(const Control& body) {
    double lowest = 0.0;
    for (const Control::Ptr& child : body.children()) {
        if (!(*child).effectively_visible()) continue;
        const Rect bounds = (*child).absolute_bounds();
        lowest = std::max(lowest, bounds.y + bounds.height);
    }
    return lowest;
}

} // namespace

// Every Gallery page, first shown in a window shorter than the Gallery's
// design (as on a laptop screen), keeps its viewport inside the page area
// and scrolls its last control into view. A page's viewport, hidden at the
// first layout, took its anchor distances when first shown, against the
// shorter page area, and overhung it by the difference: the last controls
// of the live-surfaces and timing pages could not be reached.
void test_every_page_scrolls_to_its_end() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    window.resize({1280.0, 640.0});
    window.perform_layout();
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        const std::string id(page.id);
        select_page(window, page.id);
        window.perform_layout();
        const std::shared_ptr<ScrollableControl> viewport = std::dynamic_pointer_cast<ScrollableControl>(
            window.find(std::string(gallery::surface_id_prefix) + id + ".viewport"));
        const Control::Ptr body = window.find(std::string(gallery::body_id_prefix) + id);
        const std::string missing = "every Gallery page has a viewport and a body: " + id;
        require(viewport != nullptr && body != nullptr && (*viewport).parent() != nullptr, missing.c_str());
        const Rect area = (*(*viewport).parent()).absolute_bounds();
        const Rect port = (*viewport).absolute_bounds();
        const std::string overhang = "the page's viewport stays inside the page area: " + id;
        require(port.y + port.height <= area.y + area.height + 0.5, overhang.c_str());
        static_cast<void>((*viewport).scroll_to({0.0, 1.0e6}));
        window.perform_layout();
        const double visible_bottom = port.y + (*viewport).viewport_rectangle().height;
        const double last = lowest_child_bottom(*body);
        const std::string unreachable = "the page scrolls its last control into view: " + id +
                                        " (last control ends at " + std::to_string(last) + ", the view at " +
                                        std::to_string(visible_bottom) + ")";
        require(last <= visible_bottom + 0.5, unreachable.c_str());
        static_cast<void>((*viewport).scroll_to({0.0, 0.0}));
    }
}

} // namespace gallery_pages_tests
