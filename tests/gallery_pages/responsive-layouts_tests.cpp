#include "support.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/command_overflow_panel.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/master_detail_view.hpp"
#include "gui_forms/controls/master_detail_view_values.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/responsive_track_panel.hpp"
#include "gui_forms/controls/track_bar.hpp"

namespace gallery_pages_tests {

namespace {

void relayout(gui_forms::Window& window) {
    // Responsive containers re-solve from their parents' committed slots, so
    // a width change settles across two layout passes like the unit tests.
    window.perform_layout();
    window.perform_layout();
}

} // namespace

void test_responsive_layouts(gui_forms::Window& window) {
    select_page(window, "responsive-layouts");
    require(window.find("gallery.responsive-layouts.heading") != nullptr,
            "responsive-layouts page shows its heading");
    const std::shared_ptr<TrackBar> width =
        std::dynamic_pointer_cast<TrackBar>(window.find("gallery.responsive.width"));
    const std::shared_ptr<ResponsiveTrackPanel> tracks =
        std::dynamic_pointer_cast<ResponsiveTrackPanel>(window.find("gallery.responsive.tracks"));
    const std::shared_ptr<CommandOverflowPanel> shelf =
        std::dynamic_pointer_cast<CommandOverflowPanel>(window.find("gallery.responsive.shelf"));
    const std::shared_ptr<MasterDetailView> view =
        std::dynamic_pointer_cast<MasterDetailView>(window.find("gallery.responsive.master-detail"));
    const std::shared_ptr<Panel> scroller =
        std::dynamic_pointer_cast<Panel>(window.find("gallery.responsive.scroll"));
    const std::shared_ptr<Button> more =
        std::dynamic_pointer_cast<Button>(window.find("gallery.responsive.shelf.more"));
    const std::shared_ptr<Button> show_detail =
        std::dynamic_pointer_cast<Button>(window.find("gallery.responsive.master-detail.show-detail"));
    const std::shared_ptr<Button> show_master =
        std::dynamic_pointer_cast<Button>(window.find("gallery.responsive.master-detail.show-master"));
    const Control::Ptr inspector = window.find("gallery.responsive.tracks.inspector");
    const Control::Ptr navigation = window.find("gallery.responsive.tracks.navigation");
    const Control::Ptr arrange = window.find("gallery.responsive.shelf.arrange");
    require(width && tracks && shelf && view && scroller && more && show_detail &&
                show_master && inspector && navigation && arrange,
            "responsive page must expose every specimen by stable id");
    relayout(window);
    require((*width).value.read() == 894.0 &&
                (*tracks).layout_snapshot().resolution.collapse_order.empty() &&
                !(*shelf).layout_snapshot().overflow_actuator_visible &&
                (*more).layout_collapsed() &&
                (*view).effective_display_mode() == MasterDetailDisplayMode::side_by_side &&
                !(*show_detail).enabled.read() && !(*show_master).enabled.read() &&
                (*scroller).hscroll(),
            "the full stage must fit every track and command group, with only the scroller overflowing");

    // A command bar inside a shelf group reports through the page's handler.
    const std::shared_ptr<Label> shelf_status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.responsive.shelf.status"));
    click(window, window.find("gallery.responsive.shelf.selection.bar.item.2"));
    require(shelf_status &&
                (*shelf_status).text.read().find("Selection · Copy invoked") != std::string::npos,
            "a CommandBar item inside an overflow group must invoke through the page owner");

    // Narrowing the bound Value is a state change: the slider follows and the
    // containers collapse by authored priority on the next layout.
    (*width).value = 600.0;
    relayout(window);
    const ResponsiveLayoutSnapshot mid = (*tracks).layout_snapshot();
    const CommandOverflowSnapshot mid_shelf = (*shelf).layout_snapshot();
    require(mid.resolution.collapse_order == std::vector<std::size_t>{2U} &&
                (*inspector).layout_collapsed() && !(*navigation).layout_collapsed() &&
                mid_shelf.overflow_actuator_visible && !mid_shelf.overflow &&
                !mid_shelf.groups[0].collapsed && mid_shelf.groups[1].collapsed &&
                !mid_shelf.groups[2].collapsed &&
                mid_shelf.groups[1].stable_id == "gallery.responsive.shelf.arrange" &&
                (*arrange).layout_collapsed() && !(*more).layout_collapsed() &&
                (*view).effective_display_mode() == MasterDetailDisplayMode::master_only &&
                (*show_detail).enabled.read() && !(*show_master).enabled.read(),
            "a 600 px stage must collapse the lowest-priority track and group and go compact");
    click(window, more);
    require((*shelf_status).text.read().find("gallery.responsive.shelf.arrange") != std::string::npos,
            "the overflow actuator must let the page compose the collapsed group ids");

    // Protecting a track is a bound Value<bool>; the panel then collapses the
    // next authored priority instead of the revealed one.
    click(window, window.find("gallery.responsive.tracks.protect"));
    relayout(window);
    require((*tracks).track_revealed(2U) &&
                (*tracks).layout_snapshot().resolution.collapse_order ==
                    std::vector<std::size_t>{0U} &&
                !(*inspector).layout_collapsed() && (*navigation).layout_collapsed(),
            "a revealed track must stay allocated while the next priority collapses");

    // Compact master/detail navigation runs through two Commands whose
    // enabled state projects onto the buttons.
    const std::shared_ptr<Label> view_status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.responsive.master-detail.status"));
    click(window, show_detail);
    relayout(window);
    require((*view).effective_display_mode() == MasterDetailDisplayMode::detail_only &&
                !(*show_detail).enabled.read() && (*show_master).enabled.read() &&
                (*(*view).detail()).effectively_visible() &&
                !(*(*view).master()).effectively_visible() &&
                view_status && (*view_status).text.read().find("detail only") != std::string::npos,
            "Show detail must switch the compact presentation and re-enable Show master");
    click(window, show_master);
    relayout(window);
    require((*view).effective_display_mode() == MasterDetailDisplayMode::master_only &&
                (*show_detail).enabled.read() && !(*show_master).enabled.read(),
            "Show master must return to the compact master presentation");

    // Scroll paging is a Command too; the page observes the input-only
    // Scroll event rather than polling the viewport.
    const std::shared_ptr<Label> scroll_status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.responsive.scroll.status"));
    click(window, window.find("gallery.responsive.scroll.next"));
    require((*scroller).scroll_position().x == 240.0 && scroll_status &&
                (*scroll_status).text.read().find("Scroll · x 240") != std::string::npos,
            "the scroll command must page the viewport and publish a Scroll event");
    click(window, window.find("gallery.responsive.scroll.previous"));
    require((*scroller).scroll_position().x == 0.0,
            "the reverse scroll command must page back to the origin");

    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("Responsive track specimen") != std::string::npos &&
                semantics.find("Command shelf specimen") != std::string::npos &&
                semantics.find("Folder browser specimen") != std::string::npos &&
                semantics.find("Column scroller specimen") != std::string::npos,
            "every responsive specimen must publish its named group semantics");

    (*width).value = 894.0;
    relayout(window);
    require((*tracks).layout_snapshot().resolution.collapse_order.empty() &&
                !(*shelf).layout_snapshot().overflow_actuator_visible &&
                (*view).effective_display_mode() == MasterDetailDisplayMode::side_by_side &&
                !(*show_detail).enabled.read() && !(*show_master).enabled.read(),
            "restoring the stage width must restore every automatically collapsed role");
}

} // namespace gallery_pages_tests
