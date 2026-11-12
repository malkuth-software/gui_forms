#include "page_tests.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/check_box_values.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/radio_button.hpp"

namespace gallery_pages_tests {

void test_control_spectrum(gui_forms::Window& window) {
    select_page(window, "control-spectrum");
    const std::shared_ptr<Label> status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.status.text"));
    const std::shared_ptr<CheckBox> classic =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.controls.check.0"));
    const std::shared_ptr<CheckBox> tri =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.controls.check.tri"));
    const std::shared_ptr<RadioButton> classic_radio =
        std::dynamic_pointer_cast<RadioButton>(window.find("gallery.controls.radio.0"));
    const std::shared_ptr<RadioButton> modern_radio =
        std::dynamic_pointer_cast<RadioButton>(window.find("gallery.controls.radio.1"));
    const std::shared_ptr<CheckBox> sound_enabled =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.controls.sound.enabled"));
    require(status && classic && tri && classic_radio && modern_radio && sound_enabled,
            "the control spectrum must publish its stock controls by stable id");

    // Button: pointer, keyboard and mnemonic activation converge on one click,
    // observed by the page owner's bound-index handler.
    click(window, window.find("gallery.controls.button.2"));
    require((*status).text.read().find("Button accent clicked · gallery.controls.button.2") == 0U,
            "a style button click must reach the owner with its bound index");
    require(!(*window.find("gallery.controls.button.disabled")).effectively_enabled(),
            "the disabled button must stay disabled");

    // CheckBox: checked_changed reports pointer and programmatic changes alike.
    require(!(*classic).checked.read(), "the classic check box starts unchecked");
    click(window, classic);
    require((*classic).checked.read() &&
                (*status).text.read().find("Check box 1 checked") == 0U,
            "a check box click must toggle and publish checked_changed");
    (*classic).checked = false;
    require((*status).text.read().find("Check box 1 unchecked") == 0U,
            "a programmatic check change must publish the same state event");
    require((*tri).check_state.read() == CheckState::indeterminate,
            "the three-state box must retain its indeterminate state");
    click(window, tri);
    require((*tri).check_state.read() != CheckState::indeterminate &&
                (*status).text.read().find("Three-state box now") == 0U,
            "a three-state click must advance the cycle and publish check_state_changed");

    // RadioButton: the named group unchecks its mates.
    require(!(*classic_radio).checked.read() && (*modern_radio).checked.read(),
            "the modern radio starts checked");
    click(window, classic_radio);
    require((*classic_radio).checked.read() && !(*modern_radio).checked.read() &&
                (*status).text.read().find("Radio 1 checked") == 0U,
            "checking a radio must uncheck its group mate and report once");

    // Sound cues: the CheckBox is bound to the page's Value; the cue buttons
    // read the Value and report the host result (unsupported without a host).
    require((*sound_enabled).checked.read(), "sound starts enabled");
    click(window, sound_enabled);
    require(!(*sound_enabled).checked.read() &&
                (*status).text.read().find("Sound cues muted") == 0U,
            "toggling the bound check box must publish the Value's changed event");
    click(window, window.find("gallery.controls.sound.1"));
    require((*status).text.read().find("Sound cue success") == 0U,
            "a cue button must name its cue through the bound index");
    click(window, sound_enabled);

    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"check_box\"") != std::string::npos &&
                semantics.find("\"role\":\"radio_button\"") != std::string::npos &&
                semantics.find("\"role\":\"link\"") != std::string::npos,
            "the spectrum must publish check box, radio and link semantics");
}

}  // namespace gallery_pages_tests
