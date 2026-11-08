#include "page_tests.hpp"

namespace gallery_pages_tests {
namespace {

// Counts the state event (every change) separately from the input event
// (user activation only) on the page's ChoiceGroups.
class ChoiceObserver final : public gui_forms::Component {
public:
    void changed(int) { ++changes; }
    void activated(int) { ++activations; }
    int changes{};
    int activations{};
};

} // namespace

void test_choices_and_sections(gui_forms::Window& window) {
    select_page(window, "choices-and-sections");
    require(window.find("gallery.choices-and-sections.heading") != nullptr,
            "choices-and-sections page shows its heading");
    const std::shared_ptr<gui_forms::ChoiceGroup> ring =
        std::dynamic_pointer_cast<ChoiceGroup>(window.find("gallery.choices.ring"));
    const std::shared_ptr<gui_forms::ChoiceGroup> chips =
        std::dynamic_pointer_cast<ChoiceGroup>(window.find("gallery.choices.ring-chips"));
    const std::shared_ptr<gui_forms::Label> ring_status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.choices.ring-status"));
    const std::shared_ptr<gui_forms::ExpandableSections> sections =
        std::dynamic_pointer_cast<ExpandableSections>(window.find("gallery.choices.sections"));
    const std::shared_ptr<gui_forms::GroupBox> state =
        std::dynamic_pointer_cast<GroupBox>(window.find("gallery.choices.state"));
    const std::shared_ptr<gui_forms::CheckBox> notes_toggle =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.choices.state.notes"));
    const std::shared_ptr<gui_forms::CheckBox> checklist_toggle =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.choices.state.checklist"));
    const std::shared_ptr<gui_forms::Card> card =
        std::dynamic_pointer_cast<Card>(window.find("gallery.choices.card"));
    const std::shared_ptr<gui_forms::ReviewCard> review =
        std::dynamic_pointer_cast<ReviewCard>(window.find("gallery.choices.review"));
    const std::shared_ptr<gui_forms::Label> card_body =
        std::dynamic_pointer_cast<Label>(window.find("gallery.choices.card.body"));
    require(ring && chips && ring_status && sections && state && notes_toggle &&
                checklist_toggle && card && review && card_body,
            "choices-and-sections page must retain its public choice, section, group and card controls");
    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"radio_group\"") != std::string::npos &&
                semantics.find("\"role\":\"radio_button\"") != std::string::npos &&
                semantics.find("gallery.choices.state") != std::string::npos &&
                semantics.find("gallery.choices.sections.header.1") != std::string::npos,
            "choices-and-sections page must publish radio group, group box and disclosure semantics");

    // One Value<int> behind two ChoiceGroups: a click on the labelled row is
    // one state change and one activation; the glyph row follows the model.
    ChoiceObserver observer;
    on((*ring).changed(), observer, &ChoiceObserver::changed);
    on((*ring).activated(), observer, &ChoiceObserver::activated);
    require((*ring).selected_index() == 0 && (*chips).selected_index() == 0 &&
                (*review).record.read().disposition == ReviewDisposition::pending,
            "both choice views must start from the shared ring Value");
    click(window, window.find("gallery.choices.ring.item.20"));
    require((*ring).selected_index() == 1 && (*chips).selected_index() == 1 &&
                observer.changes == 1 && observer.activations == 1,
            "a user click must change the shared Value once and activate once");
    // The labelled row is the segmented presentation (W-4); the glyph row
    // keeps the buttons.
    window.perform_layout();
    require((*ring).presentation.read() == ChoiceGroupPresentation::segmented &&
                (*ring).parts().selection.present &&
                (*ring).parts().selection.bounds == (*ring).item(1).committed_arranged_bounds() &&
                (*chips).presentation.read() == ChoiceGroupPresentation::buttons && !(*chips).parts().track.present,
            "the segmented row draws its indicator behind the chosen segment");
    require((*review).record.read().title == "Review: Staged" &&
                (*review).record.read().disposition == ReviewDisposition::accepted &&
                (*card_body).text().find("Staged") != std::string::npos &&
                (*ring_status).text().find("user activations: 1") != std::string::npos,
            "the ReviewCard and Card must project the chosen ring");
    // A programmatic change through the other view is a state change only.
    (*chips).set_selected_index(2);
    require((*ring).selected_index() == 2 && observer.changes == 2 &&
                observer.activations == 1 &&
                (*review).record.read().disposition == ReviewDisposition::warning,
            "a setter on the glyph view must move the labelled view without an activation");
    // Arrow keys select and focus the neighbouring choice.
    require(window.request_focus((*ring).item(2).shared_from_this()) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::left}) &&
                (*ring).selected_index() == 1 && (*chips).selected_index() == 1 &&
                observer.changes == 3,
            "Left must select the previous choice through the shared Value");

    // Three Value<bool>s: section headers and the GroupBox toggles share them.
    require((*sections).expanded(0) && !(*sections).expanded(1) &&
                (*checklist_toggle).checked() && !(*notes_toggle).checked(),
            "section expansion and the bound toggles must start from the same models");
    click(window, window.find("gallery.choices.sections.header.2"));
    require((*sections).expanded(1) && (*notes_toggle).checked() &&
                (*std::dynamic_pointer_cast<Label>(window.find("gallery.choices.sections-status")))
                        .text().find("Notes open") != std::string::npos,
            "clicking a section header must open it and check the bound toggle");
    click(window, notes_toggle);
    require(!(*sections).expanded(1) && !(*notes_toggle).checked(),
            "unchecking the bound toggle must collapse the section through the Value");
    click(window, checklist_toggle);
    require(!(*sections).expanded(0) && !(*checklist_toggle).checked() &&
                !(*window.find("gallery.choices.sections.checklist")).visible.read(),
            "collapsing through the toggle must hide the retained section body");

    // The interactive Card selects on activation and reports through its footer.
    require(!(*card).selected.read() && window.perform_semantic_action(
                "gallery.choices.card", SemanticAction::press) && (*card).selected.read() &&
                (*std::dynamic_pointer_cast<Label>(window.find("gallery.choices.card.footer")))
                        .text().find("Selected") != std::string::npos,
            "activating the Card must set its retained selection and report it");

    // The switches (W-4) turn over on a click and report through the status line.
    const std::shared_ptr<ToggleSwitch> deploy =
        std::dynamic_pointer_cast<ToggleSwitch>(window.find("gallery.choices.switches.auto-deploy"));
    const std::shared_ptr<ToggleSwitch> notify =
        std::dynamic_pointer_cast<ToggleSwitch>(window.find("gallery.choices.switches.notify"));
    const std::shared_ptr<Label> switch_status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.choices.switches.status"));
    require(deploy && notify && switch_status && (*deploy).checked.read() && !(*notify).checked.read() &&
                (*deploy).state_text() == "On",
            "the switches start as the form sets them");
    window.perform_layout();
    click(window, notify);
    require((*notify).checked.read() &&
                (*switch_status).text() == "Automatic deployment is on · reviewers are notified",
            "clicking a switch turns it on and the page reports it");
    require(window.perform_semantic_action("gallery.choices.switches.auto-deploy", SemanticAction::press) &&
                !(*deploy).checked.read() && (*deploy).state_text() == "Off" &&
                (*switch_status).text().starts_with("Automatic deployment is off"),
            "the press action turns the other switch off");
}

}  // namespace gallery_pages_tests
