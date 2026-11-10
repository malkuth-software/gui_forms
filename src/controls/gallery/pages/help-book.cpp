#include "pages.hpp"

#include "help-book_form.hpp"

namespace gui_forms::gallery::pages {
namespace {

// The page's help: stand-alone topics, declared where they are written
// (help system study, section 1). They join every helpbook compiled in
// the Gallery.
const HelpTopic welcome_topic(
    StableIdLiteral("gallery.help.welcome"), "Welcome to help",
    "Help in GUI.Forms is written on the program's own records: every control's stable ID can carry a "
    "**help line** and a help body, and stand-alone topics like this one fill in the rest.\n\n"
    "{callout=tip}\n"
    "Use the *Index* tab to find a topic by keyword, or *Search* to find words anywhere.\n\n"
    "- [The viewer](gallery.help.viewer) shows contents, index, search and bookmarks.\n"
    "- [Popups](gallery.help.popups \"popup\") show a page on paper beside the control.\n"
    "- [Searching](gallery.help.search) explains the query words.\n",
    HelpPlace::top(), "Where help starts.", "help; start; welcome");

const HelpTopic viewer_topic(
    StableIdLiteral("gallery.help.viewer"), "The help viewer",
    "The viewer has three panes: the toolbar, the navigation tabs and the paper.\n\n"
    "## Keys\n\n"
    "Alt+C, Alt+N, Alt+S and Alt+I choose a tab; F6 moves between the tabs and the paper; "
    "Alt+Left and Alt+Right go back and forward.\n\n"
    "{collapsible}\n"
    "## Bookmarks\n\n"
    "The *Bookmarks* tab keeps the pages you add, by their stable IDs.\n\n"
    "{callout=note}\n"
    "Printing lays the page out for paper, with every section open.\n",
    HelpPlace::after(StableIdLiteral("gallery.help.welcome")), "Contents, index, search and bookmarks.",
    "viewer; keys, keyboard; bookmarks; printing");

const HelpTopic search_topic(
    StableIdLiteral("gallery.help.search"), "Searching help",
    "Type words: a page must hold all of them, and the last word may be the start of a word. "
    "Put words in quotes to find them together.\n\n"
    "```\nclipboard \"paste special\"\n```\n\n"
    "{callout=caution}\n"
    "Search compares letters without case; it does not find other forms of a word.\n\n"
    "More at [the GUI.Forms site](https://example.org/gui-forms).\n",
    HelpPlace::under(StableIdLiteral("gallery.help.viewer")), "Words, prefixes and phrases.",
    "search; words; phrases");

const HelpTopic popups_topic(
    StableIdLiteral("gallery.help.popups"), "Paper popups",
    "A popup shows one page on paper beside the control it explains. Escape or a click elsewhere "
    "closes it; a topic link in it opens the page in the viewer.\n\n"
    "Shift+F1 enters **What's This** mode: the next click shows the clicked control's help line, "
    "with *More...* for its page.\n",
    HelpPlace::after(StableIdLiteral("gallery.help.viewer")), "Help on paper beside a control.",
    "popup; what's this; paper");

} // namespace

// Owns the help-book page: a HelpBookSystem whose viewer is embedded in the
// page, and a popup-style HelpBook showing one topic beside it.
class HelpBookPage final : public gui_forms::Component {
public:
    explicit HelpBookPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void on_external_link(HelpLinkActivation& activation);
    void on_popup_link(HelpLinkActivation& activation);

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    HelpBookPageDocument<HelpBookPage> document_{};
    HelpBookSystem help_{};
    std::shared_ptr<HelpBookViewer> viewer_{};
    std::shared_ptr<HelpBook> paper_{};
};

HelpBookPage::HelpBookPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    viewer_ = make_control<HelpBookViewer>(StableId("gallery.help-book.viewer"));
    (*viewer_).dock = DockStyle::fill;
    (*document_.gallery_help_book_viewer_host).add_child(viewer_);
    (*viewer_).set_navigation_width(220.0);
    help_.set_viewer(viewer_);
    on(help_.external_link(), *this, &HelpBookPage::on_external_link);
    paper_ = make_control<HelpBook>(StableId("gallery.help-book.paper"));
    (*paper_).dock = DockStyle::fill;
    (*paper_).set_close_visible(true);
    (*document_.gallery_help_book_popup_host).add_child(paper_);
    const std::shared_ptr<const HelpBookSection> section = help_.section();
    if (section) static_cast<void>((*paper_).show_record(*section, "gallery.help.popups"));
    on((*paper_).link_activated(), *this, &HelpBookPage::on_popup_link);
}

void HelpBookPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body. The copy of the child list keeps the
    // iteration valid while the root shrinks.
    Control& root = *document_.gallery_help_book_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
}

void HelpBookPage::on_external_link(HelpLinkActivation& activation) {
    (*document_.gallery_help_book_status).text = "The program was offered " + activation.target;
    activation.handled = true;
}

void HelpBookPage::on_popup_link(HelpLinkActivation& activation) {
    if (activation.kind != HelpLinkKind::topic) return;
    activation.handled = help_.show(activation.target, activation.fragment);
}

void add_help_book(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<HelpBookPage>(page));
}

}  // namespace gui_forms::gallery::pages
