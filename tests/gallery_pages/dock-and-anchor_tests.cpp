#include "support.hpp"

namespace gallery_pages_tests {

void test_dock_and_anchor(gui_forms::Window& window) {
    select_page(window, "dock-and-anchor");
    const std::shared_ptr<gui_forms::Panel> dock_canvas = std::dynamic_pointer_cast<Panel>(
        window.find("gallery.dock.canvas"));
    const std::shared_ptr<gui_forms::Button> left = std::dynamic_pointer_cast<Button>(
        window.find("gallery.dock.left"));
    const std::shared_ptr<gui_forms::Button> fill = std::dynamic_pointer_cast<Button>(
        window.find("gallery.dock.fill"));
    const std::shared_ptr<gui_forms::Button> top_first = std::dynamic_pointer_cast<Button>(
        window.find("gallery.dock.top.first"));
    const std::shared_ptr<gui_forms::Button> top_second = std::dynamic_pointer_cast<Button>(
        window.find("gallery.dock.top.second"));
    require(dock_canvas && left && fill && top_first && top_second &&
                (*left).dock.read() == DockStyle::left &&
                (*fill).dock.read() == DockStyle::fill,
            "dock page must use public Dock properties on stock controls");
    const std::shared_ptr<gui_forms::Button> toggle_left = std::dynamic_pointer_cast<Button>(
        window.find("gallery.dock.toggle-left"));
    const Rect fill_before = (*fill).arranged_bounds();
    click(window, toggle_left);
    window.perform_layout();
    require(!(*left).visible.read() &&
                (*fill).arranged_bounds().width > fill_before.width + 70.0 &&
                (*toggle_left).text() == "Restore left edge",
            "dock visibility command must release the hidden edge into Fill");
    click(window, toggle_left);
    window.perform_layout();
    require((*left).visible.read() && (*fill).arranged_bounds() == fill_before &&
                (*toggle_left).text() == "Hide left edge",
            "dock visibility restore must recover exact retained geometry");

    const double first_y = (*top_first).arranged_bounds().y;
    const double second_y = (*top_second).arranged_bounds().y;
    require(first_y < second_y,
            "Top A must initially be topmost in the dock z order");
    click(window, window.find("gallery.dock.swap-top"));
    window.perform_layout();
    require((*top_second).arranged_bounds().y == first_y &&
                (*top_first).arranged_bounds().y == second_y,
            "public SetChildIndex must swap the two dock edge allocations");

    const std::shared_ptr<gui_forms::Button> fixed = std::dynamic_pointer_cast<Button>(
        window.find("gallery.anchor.fixed"));
    const std::shared_ptr<gui_forms::Button> stretch = std::dynamic_pointer_cast<Button>(
        window.find("gallery.anchor.stretch"));
    const std::shared_ptr<gui_forms::Button> centered = std::dynamic_pointer_cast<Button>(
        window.find("gallery.anchor.centered"));
    const std::shared_ptr<gui_forms::Button> bottom_right = std::dynamic_pointer_cast<Button>(
        window.find("gallery.anchor.bottom-right"));
    require(fixed && stretch && centered && bottom_right &&
                (*stretch).anchor.read() == (AnchorStyles::left | AnchorStyles::right |
                                      AnchorStyles::top) &&
                (*centered).anchor.read() == AnchorStyles::none,
            "anchor page must retain public compound Anchor configurations");
    const Rect fixed_before = (*fixed).arranged_bounds();
    const Rect stretch_before = (*stretch).arranged_bounds();
    const Rect centered_before = (*centered).arranged_bounds();
    const Rect right_before = (*bottom_right).arranged_bounds();
    const Rect stretch_authored = (*stretch).requested_bounds();
    click(window, window.find("gallery.anchor.resize"));
    window.perform_layout();
    require((*fixed).arranged_bounds() == fixed_before &&
                (*stretch).arranged_bounds().width > stretch_before.width + 120.0 &&
                (*centered).arranged_bounds().x > centered_before.x + 60.0 &&
                (*bottom_right).arranged_bounds().x > right_before.x + 120.0 &&
                (*bottom_right).arranged_bounds().y > right_before.y + 15.0 &&
                (*stretch).requested_bounds() == stretch_authored,
            "anchor specimen resize must stretch, center, and edge-shift without rewriting authored bounds");
    click(window, window.find("gallery.anchor.resize"));
    window.perform_layout();
    require((*stretch).arranged_bounds() == stretch_before &&
                (*centered).arranged_bounds() == centered_before &&
                (*bottom_right).arranged_bounds() == right_before &&
                window.metrics_snapshot().bounded_pass_limit_hits == 0U,
            "anchor restore must be exact and bounded without competing layout owners");
    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("Dock layout specimen") != std::string::npos &&
                semantics.find("Compound anchor specimen") != std::string::npos,
            "dock and anchor specimens must publish named semantic groups");
}

} // namespace gallery_pages_tests
