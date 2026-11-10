// The help system's controls (help system study, sections 3 to 8): the
// HelpBook paper pane (links, sections, keyboard stops), the HelpBookViewer
// (contents sync, history, index and its chooser, search, bookmarks, keys),
// the HelpBookSystem (F1 walking up to the window page and home, the window
// card, What's This, the program routes, external links) and print
// pagination.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

constexpr std::string_view program_locale =
    "<locale xml:lang=\"en\">\n"
    "  <controls>\n"
    "    <control name=\"app\" text=\"Sketch\" help-line=\"The drawing window.\">\n"
    "      <help keywords=\"window; drawing; canvas\">\n"
    "        Sketch draws on a canvas. Open [the tools](app.tools) or read [the site](https://example.org).\n"
    "\n"
    "        {collapsible}\n"
    "        ## Details\n"
    "\n"
    "        Hidden until opened.\n"
    "      </help>\n"
    "      <control name=\"tools\" text=\"Tools\" help-line=\"The tool panel.\">\n"
    "        <help keywords=\"tools; drawing; pen\">Pick a tool. See [the pen](app.tools.pen \"popup\").</help>\n"
    "        <control name=\"pen\" text=\"Pen\" help-line=\"Draws freehand lines.\"/>\n"
    "        <control name=\"eraser\" text=\"Eraser\" help-line=\"Removes strokes.\">\n"
    "          <help keywords=\"drawing; eraser\">Erases what it touches.</help>\n"
    "        </control>\n"
    "      </control>\n"
    "      <control name=\"silent\" text=\"Silent\"/>\n"
    "    </control>\n"
    "  </controls>\n"
    "</locale>\n";

struct Program final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Panel> tools;
    std::shared_ptr<Button> pen;
    std::shared_ptr<Button> eraser;
    std::shared_ptr<Button> silent;
    std::shared_ptr<HelpBookViewer> viewer;
    std::unique_ptr<Window> window;
};

Program make_program() {
    Program program;
    program.root = make_control<Panel>(StableId("app"));
    program.tools = make_control<Panel>(StableId("app.tools"));
    program.pen = make_control<Button>(StableId("app.tools.pen"), "Pen");
    program.eraser = make_control<Button>(StableId("app.tools.eraser"), "Eraser");
    program.silent = make_control<Button>(StableId("app.silent"), "Silent");
    (*program.tools).set_requested_bounds({0.0, 0.0, 200.0, 120.0});
    (*program.pen).set_requested_bounds({10.0, 10.0, 80.0, 28.0});
    (*program.eraser).set_requested_bounds({10.0, 50.0, 80.0, 28.0});
    (*program.silent).set_requested_bounds({220.0, 10.0, 80.0, 28.0});
    (*program.tools).add_child(program.pen);
    (*program.tools).add_child(program.eraser);
    (*program.root).add_child(program.tools);
    (*program.root).add_child(program.silent);
    program.viewer = make_control<HelpBookViewer>(StableId("app.help"));
    (*program.viewer).set_requested_bounds({320.0, 0.0, 700.0, 560.0});
    (*program.root).add_child(program.viewer);
    program.window = std::make_unique<Window>(program.root, Size{1040.0, 600.0});
    (*program.window).perform_layout();
    return program;
}

struct RecordLink final {
    std::vector<std::string>* seen{};
    void operator()(HelpLinkActivation& activation) const {
        (*seen).push_back(activation.target);
        activation.handled = true;
    }
};

struct RecordPrint final {
    std::vector<std::string>* seen{};
    void operator()(HelpPrintRequest& request) const { (*seen).push_back(request.page); }
};

void paper_pane() {
    Program program = make_program();
    HelpBookSystem help{};
    require(static_cast<bool>(help.load_built_in(program_locale)), "the program's locale loads");
    help.attach(*program.window);
    const std::shared_ptr<const HelpBookSection> section = help.section();
    require(section != nullptr && help.compile().rejections.empty(), "the helpbook compiles cleanly");
    std::shared_ptr<HelpBook> book = make_control<HelpBook>(StableId("app.paper"));
    (*book).set_requested_bounds({0.0, 0.0, 300.0, 400.0});
    (*program.root).add_child(book);
    require((*book).show_record(*section, "app"), "the book shows a record");
    (*program.window).perform_layout();
    require((*book).page().title == "Sketch" && (*book).page().sections.size() == 1U,
            "the page has its title and one collapsible section");
    require(!(*book).section_open(0U), "the section starts closed");
    (*book).set_section_open(0U, true);
    require((*book).section_open(0U), "it opens");
    require((*book).content_height() > 0.0, "the page lays out");
    require(!(*book).show_record(*section, "no.such.record"), "an unknown record is refused");
    // The listed controls: tools' page lists the pen's help line.
    require((*book).show_record(*section, "app.tools"), "the tools page shows");
    (*program.window).perform_layout();
    require((*book).page().find_anchor("app.tools.pen").has_value(), "the pen is a line on the tools page");
    // Links: Tab moves through the stops; Enter follows.
    std::vector<std::string> followed;
    SubscriptionToken following = (*book).link_activated().subscribe(RecordLink{&followed});
    require((*book).move_focus(true) && (*book).focused_stop().has_value(), "Tab reaches the first link");
    require((*book).activate_link(0U) && followed.size() == 1U && followed[0] == "app.tools.pen",
            "activating a link raises it");
}

void viewer() {
    Program program = make_program();
    HelpBookSystem help{};
    static_cast<void>(help.load_built_in(program_locale));
    help.attach(*program.window);
    help.set_viewer(program.viewer);
    (*program.window).perform_layout();
    HelpBookViewer& viewer = *program.viewer;
    require(viewer.current_page() == "app", "the viewer opens on home: the window's page");
    require(viewer.show("app.tools.eraser") && viewer.current_page() == "app.tools.eraser", "a page opens");
    require(std::string((*viewer.contents_tree()).selected_id.read()) == "app.tools.eraser",
            "the contents follow the page");
    require(viewer.show("app.tools") && viewer.back() && viewer.current_page() == "app.tools.eraser" &&
                viewer.forward() && viewer.current_page() == "app.tools",
            "back and forward walk history");
    require(!viewer.show("app.tools.pen"), "a control without a body has no page");
    // The index: "drawing" names three records, so its row opens the chooser.
    require(viewer.index_row_count() > 0U, "the index has rows");
    std::size_t drawing_row = viewer.index_row_count();
    viewer.show_index("drawing");
    require((*viewer.index_field()).text.read() == "drawing", "the index field holds the typed keyword");
    for (std::size_t row = 0U; row < viewer.index_row_count() && drawing_row == viewer.index_row_count(); ++row) {
        require(viewer.open_index_row(row), "an index row opens");
        if (!viewer.chooser_records().empty()) drawing_row = row;
    }
    require(drawing_row < viewer.index_row_count() && viewer.chooser_records().size() >= 2U,
            "a keyword with several pages opens the chooser");
    const std::string chosen = viewer.chooser_records().front();
    require(viewer.open_chooser_entry(0U) && viewer.current_page() == chosen, "the chooser opens its page");
    // Search.
    viewer.show_search("erases");
    require(viewer.search_results().size() == 1U && viewer.search_results().front() == "app.tools.eraser",
            "search finds the eraser");
    require(viewer.open_search_result(0U) && viewer.current_page() == "app.tools.eraser", "a result opens");
    viewer.set_titles_only(true);
    require(viewer.search_results().empty(), "titles only: the word is in no title");
    viewer.set_titles_only(false);
    // Bookmarks.
    require(viewer.add_bookmark() && viewer.bookmarks().size() == 1U && help.bookmarks().size() == 1U,
            "a bookmark is added and reaches the system");
    require(!viewer.add_bookmark(), "the same page is not bookmarked twice");
    // Keys: Alt+N selects Index, Alt+Left goes back, F6 moves to the book.
    // The search tab is showing: the contents tree is focusable once its tab is.
    viewer.select_tab(HelpNavigationTab::contents);
    require((*program.window).request_focus(viewer.contents_tree()), "focus in the contents");
    require((*program.window).dispatch_key({KeyAction::down, PhysicalKey::n, Modifier::alt}) &&
                viewer.selected_tab() == HelpNavigationTab::index,
            "Alt+N selects the index");
    const std::string before_back = viewer.current_page();
    static_cast<void>((*program.window).dispatch_key({KeyAction::down, PhysicalKey::left, Modifier::alt}));
    require(viewer.current_page() != before_back || !viewer.can_go_back(), "Alt+Left goes back");
    static_cast<void>((*program.window).dispatch_key({KeyAction::down, PhysicalKey::f6, Modifier::none}));
    require((*program.window).focused_control() == viewer.book(), "F6 moves to the paper");
    std::vector<std::string> printed;
    SubscriptionToken printing_token = help.print_requested().subscribe(RecordPrint{&printed});
    static_cast<void>((*program.window).dispatch_key({KeyAction::down, PhysicalKey::p, Modifier::control}));
    require(printed.size() == 1U && printed[0] == viewer.current_page(), "Ctrl+P asks to print the page");
    // The navigation pane hides and shows.
    viewer.set_navigation_visible(false);
    (*program.window).perform_layout();
    require(!(*viewer.contents_tree()).effectively_visible(), "Hide hides the tabs");
    viewer.set_navigation_visible(true);
}

void system_routes() {
    Program program = make_program();
    HelpBookSystem help{};
    static_cast<void>(help.load_built_in(program_locale));
    help.attach(*program.window);
    help.set_viewer(program.viewer);
    (*program.window).perform_layout();
    // resolve: the eraser has a page; the pen has a line on tools' page;
    // silent has nothing and walks up to the window's page.
    const HelpTarget eraser = help.resolve(*program.eraser);
    require(eraser.page == "app.tools.eraser" && eraser.anchor.empty(), "a control with a body is its own page");
    const HelpTarget pen = help.resolve(*program.pen);
    require(pen.page == "app.tools" && pen.anchor == "app.tools.pen", "a help line is an anchor on the parent page");
    const HelpTarget silent = help.resolve(*program.silent);
    require(silent.page == "app" && !silent.home, "nothing on the way walks up to the window's page");
    // F1 on the focused pen.
    require((*program.window).request_focus(program.pen), "the pen takes focus");
    require((*program.window).dispatch_key({KeyAction::down, PhysicalKey::f1, Modifier::none}) &&
                (*program.viewer).current_page() == "app.tools",
            "F1 shows the focused control's page");
    // The window card: the pen's help line and More.
    require(help.show_card(program.pen) && help.popup_open(), "the card opens");
    const std::shared_ptr<HelpBook> card = help.popup_book();
    require(card != nullptr && (*card).page().links.size() == 1U && (*card).page().links[0].target == "app.tools" &&
                (*card).page().links[0].fragment == "app.tools.pen",
            "More goes to the pen's line on the tools page");
    require((*program.window).dispatch_key({KeyAction::down, PhysicalKey::escape, Modifier::none}) ||
                !help.popup_open(),
            "Escape is taken");
    help.close_popup();
    require(!help.popup_open(), "the popup closes");
    require(help.show_popup("app.tools", program.tools) && help.popup_open(), "a page popup opens");
    help.close_popup();
    // What's This: Shift+F1, then a click on the eraser.
    require((*program.window).dispatch_key({KeyAction::down, PhysicalKey::f1, Modifier::shift}) &&
                help.help_mode_active(),
            "Shift+F1 enters What's This");
    PointerEvent press{};
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = Point{30.0, 60.0};
    static_cast<void>((*program.window).dispatch_pointer(press));
    require(!help.help_mode_active() && help.popup_open(), "the click ends the mode and shows the card");
    require((*help.popup_book()).page().stable_id == "app.tools.eraser", "the card is the eraser's");
    help.close_popup();
    require(help.enter_help_mode(*program.window), "the mode enters again");
    static_cast<void>((*program.window).dispatch_key({KeyAction::down, PhysicalKey::escape, Modifier::none}));
    require(!help.help_mode_active(), "Escape leaves What's This");
    // Program routes.
    HelpRequestEvent request{};
    request.target_stable_id = "app.tools.eraser";
    help.on_help_requested(request);
    require(request.handled && (*program.viewer).current_page() == "app.tools.eraser", "a HelpProvider request routes");
    RibbonToolTipHelpRequest ribbon{};
    ribbon.target_stable_id = "app.silent";
    ribbon.help_topic = "app.tools";
    help.on_ribbon_help(ribbon);
    require((*program.viewer).current_page() == "app.tools", "the ribbon's topic routes");
    // External links go to the program.
    std::vector<std::string> external;
    SubscriptionToken external_token = help.external_link().subscribe(RecordLink{&external});
    require((*program.viewer).show("app"), "the window page shows");
    HelpBook& book = *(*program.viewer).book();
    (*program.window).perform_layout();
    for (std::size_t link = 0U; link < book.page().links.size(); ++link) {
        if (book.page().links[link].kind == HelpLinkKind::external) static_cast<void>(book.activate_link(link));
    }
    require(external.size() == 1U && external[0] == "https://example.org", "an external link is offered");
    const HelpBookSystemSnapshot snapshot = help.snapshot();
    require(snapshot.attached_windows == 1U && snapshot.popups >= 3U && snapshot.help_mode_entries == 2U &&
                snapshot.external_links == 1U,
            "the snapshot counts what happened");
    help.detach(*program.window);
}

void printing() {
    Program program = make_program();
    HelpBookSystem help{};
    static_cast<void>(help.load_built_in(program_locale));
    help.attach(*program.window);
    help.set_viewer(program.viewer);
    (*program.window).perform_layout();
    HelpBook& book = *(*program.viewer).book();
    HelpPrintOptions options{};
    options.program_title = "Sketch";
    HelpPrintPlan plan;
    plan_help_print(book, options, plan);
    require(plan.sheets.size() == 1U && plan.content_height > 0.0, "a short page prints on one sheet");
    options.sheet = Size{816.0, 260.0};
    plan_help_print(book, options, plan);
    require(plan.sheets.size() >= 2U, "a short sheet breaks the page");
    double covered = 0.0;
    for (const HelpPrintSheet& sheet : plan.sheets) {
        require(sheet.top == covered && sheet.height > 0.0, "sheets follow each other without gaps");
        covered += sheet.height;
    }
    require(covered == plan.content_height, "the sheets cover the page");
}

} // namespace

int main() {
    paper_pane();
    viewer();
    system_routes();
    printing();
    std::cout << "help system tests passed\n";
    return 0;
}
