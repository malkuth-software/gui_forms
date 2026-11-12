#include "support.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/indicator_lamp.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/scaled_group_box.hpp"

namespace gallery_pages_tests {

// The industrial-surfaces page (skeudoindustrial surfaces, phase 5): its
// finish and glint swatches, the lamps' status line (no bakes are shipped,
// so it says where they are made), the Lamps check box, and the industrial
// theme over the GOST paints, put on the preview group and taken off; the
// preview's indicator lamps (Start and Stop switch Running, the Coolant
// check box its flow lamp, and none is hit) and its danger key.
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

    // The preview's lamps and danger key, in the default theme here.
    const std::shared_ptr<IndicatorLamp> running =
        std::dynamic_pointer_cast<IndicatorLamp>(window.find("gallery.industrial.preview.running"));
    const std::shared_ptr<IndicatorLamp> fault =
        std::dynamic_pointer_cast<IndicatorLamp>(window.find("gallery.industrial.preview.fault"));
    const std::shared_ptr<IndicatorLamp> flow =
        std::dynamic_pointer_cast<IndicatorLamp>(window.find("gallery.industrial.preview.coolant-lamp"));
    require(running != nullptr && fault != nullptr && flow != nullptr, "the preview holds three indicator lamps");
    if (running == nullptr || fault == nullptr || flow == nullptr) return;
    require((*running).state.read() == IndicatorLampState::off && (*fault).state.read() == IndicatorLampState::error &&
                (*flow).state.read() == IndicatorLampState::on,
            "Running starts off, Fault shows an error and Coolant flow is on");
    require(!(*running).hit_test_local(Point{4.0, 4.0}), "a lamp is never hit");
    click(window, window.find("gallery.industrial.preview.start"));
    require((*running).state.read() == IndicatorLampState::on, "Start lights Running");
    click(window, window.find("gallery.industrial.preview.stop"));
    require((*running).state.read() == IndicatorLampState::off, "Stop puts Running out");
    click(window, window.find("gallery.industrial.preview.coolant"));
    require((*flow).state.read() == IndicatorLampState::off, "clearing Coolant puts its flow lamp out");
    click(window, window.find("gallery.industrial.preview.coolant"));
    require((*flow).state.read() == IndicatorLampState::on, "and setting it lights it again");
    const std::shared_ptr<Button> erase =
        std::dynamic_pointer_cast<Button>(window.find("gallery.industrial.preview.erase"));
    require(erase != nullptr && (*erase).danger.read(), "Erase program is a danger key");
}

}  // namespace gallery_pages_tests
