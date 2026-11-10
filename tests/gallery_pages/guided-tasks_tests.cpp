#include "support.hpp"

namespace gallery_pages_tests {

// The guided-tasks page (roadmap row 2.5): the wizard's gate and steps, the
// lookups, and the buttons that open the callout, the modal layer and the
// About box.
void test_guided_tasks(gui_forms::Window& window) {
    select_page(window, "guided-tasks");
    require(window.find("gallery.guided-tasks.heading") != nullptr, "guided-tasks page shows its heading");
    const std::shared_ptr<Wizard> wizard = std::dynamic_pointer_cast<Wizard>(window.find("gallery.guided.wizard"));
    require(wizard && (*wizard).page_count() == 3U, "the wizard has three pages");
    require(!(*(*wizard).next_button()).enabled.read(), "Next waits for the terms");
    click(window, window.find("gallery.guided.wizard.agree"));
    window.perform_layout();
    require((*(*wizard).next_button()).enabled.read(), "accepting the terms enables Next");
    click(window, (*wizard).next_button());
    window.perform_layout();
    const std::shared_ptr<StepIndicator> steps =
        std::dynamic_pointer_cast<StepIndicator>(window.find("gallery.guided.steps"));
    require((*wizard).current_page.read() == 1U && steps && (*steps).current_step.read() == 1U,
            "Next moves the wizard and the step indicator");
    const std::shared_ptr<LookupComboBox> country =
        std::dynamic_pointer_cast<LookupComboBox>(window.find("gallery.guided.wizard.country"));
    require(country && (*country).shown_text() == "New Zealand", "the country lookup shows its display column");

    click(window, window.find("gallery.guided.callout-target"));
    window.perform_layout();
    const std::shared_ptr<Callout> shown = std::dynamic_pointer_cast<Callout>(window.find("gallery.guided.callout"));
    require(shown && (*shown).is_shown(), "the button shows the callout");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && !(*shown).is_shown(),
            "Escape closes the callout");
    window.perform_layout();

    click(window, window.find("gallery.guided.open-modal"));
    window.perform_layout();
    require(window.find("gallery.guided.modal.ok") != nullptr, "the modal layer shows its content");
    click(window, window.find("gallery.guided.modal.ok"));
    window.perform_layout();
    require(window.find("gallery.guided.modal.ok") == nullptr, "OK closes the modal layer");

    click(window, window.find("gallery.guided.about"));
    window.perform_layout();
    require(window.find("gallery.guided.about-dialog") != nullptr, "the About box shows");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}), "Escape closes the About box");
    window.perform_layout();
    require(window.find("gallery.guided.about-dialog") == nullptr, "the About box closed");
}

}  // namespace gallery_pages_tests
