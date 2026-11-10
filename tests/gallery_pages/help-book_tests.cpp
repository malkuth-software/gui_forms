#include "support.hpp"

namespace gallery_pages_tests {

// The help-book page (help system H1 to H8): the embedded viewer shows the
// Gallery's declared topics, opens a topic by link and keeps history, and
// the paper beside it shows the popups topic.
void test_help_book(gui_forms::Window& window) {
    select_page(window, "help-book");
    require(window.find("gallery.help-book.heading") != nullptr, "help-book page shows its heading");
    const std::shared_ptr<HelpBookViewer> viewer =
        std::dynamic_pointer_cast<HelpBookViewer>(window.find("gallery.help-book.viewer"));
    require(viewer != nullptr && (*viewer).section() != nullptr, "the viewer has a section");
    require((*viewer).current_page() == "gallery.help.welcome", "the viewer opens on the first topic");
    require((*viewer).show("gallery.help.search"), "a declared topic opens");
    window.perform_layout();
    require((*viewer).can_go_back() && (*viewer).back() && (*viewer).current_page() == "gallery.help.welcome",
            "history goes back");
    // The topic's body says to put words in quotes.
    (*viewer).show_search("quotes");
    require(!(*viewer).search_results().empty() && (*viewer).search_results().front() == "gallery.help.search",
            "search finds the searching topic");
    const std::shared_ptr<HelpBook> paper =
        std::dynamic_pointer_cast<HelpBook>(window.find("gallery.help-book.paper"));
    require(paper != nullptr && (*paper).page().stable_id == "gallery.help.popups", "the paper shows the popups topic");
}

} // namespace gallery_pages_tests
