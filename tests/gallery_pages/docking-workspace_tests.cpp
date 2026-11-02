#include "support.hpp"

namespace gallery_pages_tests {

namespace {

bool placed(const gui_forms::DockSite& site, std::string_view panel) {
    const gui_forms::DockWorkspace* const workspace = site.workspace();
    if (workspace == nullptr) return false;
    const gui_forms::DockPlacementKind placement =
        (*workspace).layout().locate(gui_forms::DockPanelId(std::string(panel))).placement;
    const bool result = placement == gui_forms::DockPlacementKind::docked;
    return result;
}

} // namespace

// The docking page (stage DK phase D8): a real DockWorkspace on a DockSite,
// the closing prompt, Close All, saving and restoring the layout and named
// layouts, through the page's own controls.
void test_docking_workspace(gui_forms::Window& window) {
    select_page(window, "docking-workspace");
    window.perform_layout();
    const std::shared_ptr<gui_forms::DockSite> site =
        std::dynamic_pointer_cast<DockSite>(window.find("gallery.docking.site"));
    const std::shared_ptr<gui_forms::CheckBox> keep =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.docking.keep"));
    const std::shared_ptr<gui_forms::Label> closing =
        std::dynamic_pointer_cast<Label>(window.find("gallery.docking.closing"));
    require(site && keep && closing && (*site).workspace() != nullptr && placed(*site, "form.cs") &&
                placed(*site, "program.cs") && placed(*site, "readme.md") && placed(*site, "solution") &&
                placed(*site, "properties") && placed(*site, "output"),
            "the docking page shows a real workspace with its tool panels and three documents");
    const gui_forms::DockWorkspace& workspace = *(*site).workspace();
    require(workspace.layout().locate(DockPanelId("toolbox")).placement == DockPlacementKind::auto_hidden,
            "the Toolbox is pinned out to an edge");

    // Kept: Close All asks and the documents stay.
    click(window, keep);
    click(window, window.find("gallery.docking.close-all"));
    require((*keep).checked() && placed(*site, "form.cs") && placed(*site, "readme.md") &&
                (*closing).text().find("kept") != std::string::npos,
            "with Keep checked, Close All asks about the first document and closes nothing");

    // Saved, closed, restored.
    click(window, window.find("gallery.docking.save"));
    click(window, keep);
    click(window, window.find("gallery.docking.close-all"));
    require(!placed(*site, "form.cs") && !placed(*site, "program.cs") && !placed(*site, "readme.md") &&
                placed(*site, "solution"),
            "without Keep, Close All closes every document and no tool panel");
    click(window, window.find("gallery.docking.restore"));
    window.perform_layout();
    require(placed(*site, "form.cs") && placed(*site, "readme.md") &&
                (*closing).text().find("Layout restored") != std::string::npos,
            "Restore brings the saved layout back");

    // A named layout, saved with the name field's text and applied from the
    // list.
    click(window, window.find("gallery.docking.save-named"));
    const std::shared_ptr<gui_forms::ListBox> named =
        std::dynamic_pointer_cast<ListBox>(window.find("gallery.docking.named"));
    require(named && workspace.named_layouts().size() == 1U && workspace.named_layouts()[0].name == "Wide",
            "Save as keeps the layout under the typed name");
    click(window, window.find("gallery.docking.close-all"));
    (*named).select_index(0U);
    click(window, window.find("gallery.docking.apply-named"));
    window.perform_layout();
    require(placed(*site, "program.cs") && (*closing).text().find("Layout restored") != std::string::npos,
            "applying the named layout brings the documents back");
}

} // namespace gallery_pages_tests
