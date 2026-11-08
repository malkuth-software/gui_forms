#include "support.hpp"

namespace gallery_pages_tests {

void test_semantic_feedback(gui_forms::Window& window) {
    select_page(window, "semantic-feedback");
    require(window.find("gallery.semantic-feedback.heading") != nullptr,
            "semantic-feedback page shows its heading");
    const std::shared_ptr<gui_forms::CommandBar> bar =
        std::dynamic_pointer_cast<CommandBar>(window.find("gallery.semantic.commands"));
    const std::shared_ptr<gui_forms::Label> latest =
        std::dynamic_pointer_cast<Label>(window.find("gallery.semantic.latest"));
    const std::shared_ptr<gui_forms::TextBox> trace =
        std::dynamic_pointer_cast<TextBox>(window.find("gallery.semantic.trace"));
    const std::shared_ptr<gui_forms::Label> accounting =
        std::dynamic_pointer_cast<Label>(window.find("gallery.semantic.accounting"));
    const std::shared_ptr<gui_forms::CheckBox> sound =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.semantic.sound"));
    require(bar && latest && trace && accounting && sound && (*bar).items().size() == 7U,
            "semantic-feedback page must retain its CommandBar, records and policy");
    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"toolbar\"") != std::string::npos,
            "the transition CommandBar must publish toolbar semantics");
    require((*trace).text.read().empty() &&
                (*accounting).text().find("Retained 0 of 6") != std::string::npos,
            "no record may exist before a transition is announced");

    // Each bar item borrows a page-owned Command; invoking one emits a record.
    click(window, window.find("gallery.semantic.commands.item.4"));
    require((*latest).text().find("feedback=operation_completed") != std::string::npos &&
                (*latest).text().find("cue=operation_complete") != std::string::npos &&
                (*trace).text.read().find("feedback=operation_completed sequence=1") !=
                    std::string::npos,
            "announcing a transition must publish its record and host cue");

    // The accent button binds the same completion Command as the bar item.
    const std::shared_ptr<gui_forms::Button> complete =
        std::dynamic_pointer_cast<Button>(window.find("gallery.semantic.complete"));
    require(complete && (*complete).command_connected(),
            "the completion button must share the bar item's Command");
    click(window, complete);
    require((*trace).text.read().find("sequence=2") != std::string::npos &&
                (*accounting).text().find("Retained 2 of 6") != std::string::npos,
            "the shared Command must emit the second record");

    // The sound Value drives the window's presentation settings and the
    // record reports that sound was off for the transition.
    require(window.presentation_settings().sound_enabled,
            "sound must start enabled");
    click(window, sound);
    require(!window.presentation_settings().sound_enabled,
            "unchecking the bound Value must disable window sound cues");
    click(window, window.find("gallery.semantic.commands.item.5"));
    require((*latest).text().find("feedback=operation_failed") != std::string::npos &&
                (*latest).text().find("sound off") != std::string::npos &&
                (*trace).text.read().find("sound=off") != std::string::npos,
            "a transition announced with sound off must record sound=off");
    click(window, sound);
    require(window.presentation_settings().sound_enabled,
            "re-checking the bound Value must restore window sound cues");

    // Bounded history: six retained records, older ones counted as dropped.
    for (int repeat = 0; repeat < 5; ++repeat) {
        click(window, window.find("gallery.semantic.commands.item.0"));
    }
    require((*accounting).text().find("Retained 6 of 6") != std::string::npos &&
                (*accounting).text().find("dropped 2") != std::string::npos &&
                (*trace).text.read().find("sequence=1 ") == std::string::npos &&
                (*trace).text.read().find("feedback=location_changed sequence=8") !=
                    std::string::npos,
            "the history bound must evict the oldest records and count them");

    click(window, window.find("gallery.semantic.clear"));
    require((*trace).text.read().empty() &&
                (*accounting).text().find("Retained 0 of 6") != std::string::npos &&
                (*accounting).text().find("dropped 0") != std::string::npos &&
                (*latest).text().find("cleared") != std::string::npos,
            "the clear Command must empty the history and its eviction count");
}

}  // namespace gallery_pages_tests
