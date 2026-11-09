#include "support.hpp"

namespace gallery_pages_tests {

void test_containers_and_focus(gui_forms::Window& window) {
    select_page(window, "containers-and-focus");
    std::shared_ptr<gui_forms::SplitContainer> split = std::dynamic_pointer_cast<SplitContainer>(
        window.find("gallery.containers.split"));
    std::shared_ptr<gui_forms::ContainerControl> base_container = std::dynamic_pointer_cast<ContainerControl>(
        window.find("gallery.containers.base"));
    std::shared_ptr<gui_forms::UserControl> user_control = std::dynamic_pointer_cast<UserControl>(
        window.find("gallery.containers.user"));
    std::shared_ptr<gui_forms::Label> user_status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.containers.user.status"));
    std::shared_ptr<gui_forms::Button> collapse = std::dynamic_pointer_cast<Button>(
        window.find("gallery.containers.collapse"));
    require(base_container && user_control && user_status && collapse &&
                (*user_control).is_loaded() && (*user_control).is_attached() &&
                (*user_control).attachment_count() == 1U &&
                (*user_status).text.read().find("Loaded fired once") != std::string::npos,
            "gallery must directly dogfood public ContainerControl and UserControl lifecycle");
    click(window, window.find("gallery.containers.base.focus"));
    require((*base_container).active_control() ==
                window.find("gallery.containers.base.focus") &&
                window.semantic_snapshot().to_json().find(
                    "UserControl lifecycle specimen") != std::string::npos,
            "public container focus and named group semantics must work in the gallery");
    // The page owner's named handler transfers focus to the splitter through
    // the public container seam; arrow keys then move the seam.
    click(window, window.find("gallery.containers.focus"));
    require(split && window.focused_control() == (*split).splitter_control(),
            "gallery focus command must route focus to the public splitter control");
    const double distance_before = (*split).splitter_distance.read();
    require(window.dispatch_key({KeyAction::down, PhysicalKey::right}) &&
                (*split).splitter_distance.read() > distance_before,
            "a focused splitter must move its seam from keyboard input");
    click(window, collapse);
    require((*split).first_collapsed() &&
                (*collapse).text.read() == "Restore navigation",
            "gallery collapse command must mutate the public SplitContainer");
    click(window, collapse);
    require(!(*split).first_collapsed() &&
                (*collapse).text.read() == "Collapse navigation",
            "gallery SplitContainer must restore its retained allocation");

    // The docked workspace: three stacks in the tree and Output auto-hidden
    // behind a side tab on the bottom edge, only the shown panels' controls
    // made, and focus in a panel activating it.
    std::shared_ptr<gui_forms::DockSite> site = std::dynamic_pointer_cast<DockSite>(
        window.find("gallery.containers.dock"));
    std::shared_ptr<gui_forms::Label> dock_status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.containers.docking.status"));
    require(site && dock_status && (*site).workspace() != nullptr && (*site).realized_stacks().size() == 4U &&
                (*site).realized_seams().size() == 2U,
            "the gallery's docked workspace realizes four stacks and two seams");
    require((*site).side_panels(DockSide::bottom).size() == 1U &&
                (*site).side_panels(DockSide::bottom)[0].value == "output" &&
                !window.find("gallery.containers.dock.content.output"),
            "Output waits behind a side tab, not yet made");
    require(window.find("gallery.containers.dock.content.classes") &&
                !window.find("gallery.containers.dock.content.toolbox") &&
                !window.find("gallery.containers.dock.content.form.cs"),
            "only the selected panels of shown stacks are made");
    require(window.request_focus(window.find("gallery.containers.dock.field.properties")) &&
                (*dock_status).text.read().find("Active: Properties") != std::string::npos,
            "focus in a panel makes it the active panel");
    require(window.find("gallery.containers.navigator") &&
                window.semantic_snapshot().to_json().find("Active Tool Windows") != std::string::npos,
            "the gallery's navigator publishes its lists");
}

} // namespace gallery_pages_tests
