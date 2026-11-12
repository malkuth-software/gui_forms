#include "support.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/scaled_group_box.hpp"

namespace gallery_pages_tests {

// The industrial-surfaces page (skeudoindustrial surfaces, phase 5): its
// finish and glint swatches, the lamps' status line (no bakes are shipped,
// so it says where they are made), the Lamps check box, and the industrial
// theme over the GOST paints, put on the preview group and taken off.
// Compositing the lamps is covered by gui_forms_engine_light_planes_tests,
// and the industrial package's compile by gui_forms_theme_gost_palette_tests.
void test_industrial_surfaces(gui_forms::Window& window) {
    select_page(window, "industrial-surfaces");
    require(window.find("gallery.industrial-surfaces.heading") != nullptr,
            "industrial-surfaces page shows its heading");
    require(window.find("gallery.industrial.finish.hammertone") != nullptr &&
                window.find("gallery.industrial.finish.cloth-weave") != nullptr &&
                window.find("gallery.industrial.glint.painted-sand") != nullptr,
            "the page shows the finish and glint swatches");

    const std::shared_ptr<Label> bakes = std::dynamic_pointer_cast<Label>(window.find("gallery.industrial.bakes"));
    require(bakes != nullptr && !(*bakes).text.read().empty(), "the lamps' status line says what is shown");
    const bool baked = window.find("gallery.industrial.lamp.dialog_ok") != nullptr;
    require(baked || (*bakes).text.read().find("No light-plane bakes") != std::string::npos,
            "without bakes the status line says where they are made");

    const std::shared_ptr<CheckBox> lamps = std::dynamic_pointer_cast<CheckBox>(window.find("gallery.industrial.lamps"));
    require(lamps != nullptr && !(*lamps).checked.read(), "the lamps start out");
    click(window, lamps);
    window.perform_layout();
    require((*lamps).checked.read(), "the Lamps check box switches them on");
    click(window, lamps);
    window.perform_layout();
    require(!(*lamps).checked.read(), "and off");

    const std::shared_ptr<ScaledGroupBox> preview =
        std::dynamic_pointer_cast<ScaledGroupBox>(window.find("gallery.industrial.preview"));
    const std::shared_ptr<Label> state = std::dynamic_pointer_cast<Label>(window.find("gallery.industrial.preview.state"));
    require(preview != nullptr && state != nullptr && (*preview).theme_override() == nullptr,
            "the preview starts in the Gallery's theme");
    click(window, window.find("gallery.industrial.theme"));
    window.perform_layout();
    if ((*preview).theme_override() == nullptr) std::cerr << "  industrial theme: " << (*state).text.read() << '\n';
    require((*preview).theme_override() != nullptr, "the industrial theme compiles over the GOST paints and shows");
    click(window, window.find("gallery.industrial.theme"));
    window.perform_layout();
    require((*preview).theme_override() == nullptr, "clearing it restores the Gallery's theme");
}

}  // namespace gallery_pages_tests
