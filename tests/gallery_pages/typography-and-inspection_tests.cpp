#include "support.hpp"

#include "gui_forms/controls/typography_specimen/typography_specimen.hpp"
#include "gui_forms/controls/visual_inspector_view/visual_inspector_view.hpp"

namespace gallery_pages_tests {

void test_typography_and_inspection(gui_forms::Window& window) {
    select_page(window, "typography-and-inspection");
    require(window.find("gallery.typography-and-inspection.heading") != nullptr,
            "typography-and-inspection page shows its heading");
    const std::shared_ptr<gui_forms::TypographySpecimen> heading =
        std::dynamic_pointer_cast<TypographySpecimen>(
            window.find("gallery.typography.specimen.0"));
    const std::shared_ptr<gui_forms::TypographySpecimen> wrapped =
        std::dynamic_pointer_cast<TypographySpecimen>(
            window.find("gallery.typography.specimen.3"));
    const std::shared_ptr<gui_forms::VisualInspectorView> inspector =
        std::dynamic_pointer_cast<VisualInspectorView>(
            window.find("gallery.typography.inspector"));
    const std::shared_ptr<gui_forms::VisualInspectorOverlay> overlay =
        std::dynamic_pointer_cast<VisualInspectorOverlay>(
            window.find("gallery.typography.inspector.overlay"));
    const std::shared_ptr<gui_forms::CheckBox> overlay_box =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.typography.overlay"));
    const std::shared_ptr<gui_forms::Button> overlay_toggle =
        std::dynamic_pointer_cast<Button>(window.find("gallery.typography.overlay.toggle"));
    const std::shared_ptr<gui_forms::Label> readout =
        std::dynamic_pointer_cast<Label>(window.find("gallery.typography.readout"));
    require(heading && wrapped && inspector && overlay && overlay_box && overlay_toggle &&
                readout,
            "typography page must retain its specimens, inspector, overlay and policy");
    require((*heading).text() == "CONTROL FACE · GALLERY HEADING" &&
                (*heading).font().weight == 700 && (*heading).diagnostics_visible() &&
                (*wrapped).wrap_text(),
            "specimens must carry their authored runs, fonts and wrapping");
    require((*inspector).target_stable_id() == "gallery.typography.specimen.0" &&
                (*overlay).target_stable_id() == "gallery.typography.specimen.0" &&
                !(*overlay).visible.read(),
            "the inspector and overlay must start on the heading specimen, overlay hidden");
    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"static_text\"") != std::string::npos &&
                semantics.find("\"role\":\"radio_group\"") != std::string::npos &&
                semantics.find("gallery.typography.specimen.0") != std::string::npos,
            "specimens, the target ChoiceGroup and the inspector must publish semantics");

    // One checkable Command is shared by the CheckBox and the Button: the
    // check box moves the command's checked state, the button flips it.
    require((*overlay_box).command_connected() && (*overlay_toggle).command_connected(),
            "overlay controls must bind the page's checkable Command");
    click(window, overlay_box);
    require((*overlay).visible.read() && (*overlay_box).checked(),
            "checking the overlay box must show the overlay through the Command");
    click(window, overlay_toggle);
    require(!(*overlay).visible.read() && !(*overlay_box).checked(),
            "the shared Command must project the button's flip back to the check box");
    click(window, overlay_toggle);
    require((*overlay).visible.read() && (*overlay_box).checked(),
            "a second flip must show the overlay and re-check the box");

    // The ChoiceGroup binds the target Value; selecting an item retargets
    // both the view and the overlay.
    click(window, window.find("gallery.typography.target.item.2"));
    require((*inspector).target_stable_id() == "gallery.typography.specimen.2" &&
                (*overlay).target_stable_id() == "gallery.typography.specimen.2" &&
                (*readout).text.read().find("gallery.typography.specimen.2") != std::string::npos,
            "choosing a target must retarget the inspector view and overlay");
    const std::shared_ptr<gui_forms::ChoiceGroup> target =
        std::dynamic_pointer_cast<ChoiceGroup>(window.find("gallery.typography.target"));
    require(target && (*target).selected_index() == 2,
            "the ChoiceGroup must report the bound selection");

    // Bound Values: the diagnostics check box and the size TrackBar change
    // specimen state through state events, with no input event required.
    click(window, window.find("gallery.typography.diagnostics"));
    require(!(*heading).diagnostics_visible() && !(*wrapped).diagnostics_visible(),
            "the diagnostics Value must hide every specimen's adornments");
    const std::shared_ptr<gui_forms::TrackBar> size =
        std::dynamic_pointer_cast<TrackBar>(window.find("gallery.typography.size"));
    require(size && (*size).value() == 14.0, "the size TrackBar must start at the Value");
    (*size).set_value(22.0);
    require((*wrapped).font().size == 22.0,
            "a size state change must rewrite the wrapped specimen's FontSpec");
}

}  // namespace gallery_pages_tests
