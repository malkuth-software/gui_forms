#include "pages.hpp"

#include "tabs-and-pages_form.hpp"

namespace gui_forms::gallery::pages {

// Owner of the Tabs and Retained Pages page that
// demo/pages/tabs-and-pages.form describes: one interactive TabControl whose
// selection state event drives the status line, plus a matrix of alignment
// and appearance variants, a PageStrip, PageViews and a NavigationPane. The
// Activity page's toggle binds a Value<bool> that the owner observes
// instead of the control. The page makes the tab pages, the page records
// and their content, and the two controls a document cannot make alone.
class TabsPage final : public gui_forms::Component {
public:
    TabsPage(const std::shared_ptr<Surface>& body,
             const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_form_children(*document_.gallery_tabs_document, *body);
        const std::array<std::shared_ptr<ScaledGroupBox>, 7> groups{
            document_.gallery_tabs_primary_group, document_.gallery_tabs_variants,
            document_.gallery_tabs_strip_group,   document_.gallery_tabs_view_group,
            document_.gallery_tabs_documents_group, document_.gallery_tabs_side_group,
            document_.gallery_tabs_navigation_group};
        for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
            (*group).set_style(page_style());
        }
        build_primary();
        build_variants();
        build_strip();
        build_view();
        build_documents();
        build_side_strips();
        build_navigation();
        on((*document_.gallery_tabs_primary).selected_index_changed(), *this, &TabsPage::on_primary_selected);
        on(activity_visible_.changed(), *this, &TabsPage::on_activity_visible_changed);
        on((*strip_).page_invoked(), *this, &TabsPage::on_strip_invoked);
    }

    // The strip borrows this page's set; it is released before the set dies
    // in case the control tree outlives the page owner.
    ~TabsPage() override {
        if (strip_) (*strip_).release_page_set();
    }

private:
    static constexpr std::array<std::string_view, 4> primary_names{
        "Overview", "Activity", "Security", "Diagnostics"};
    static constexpr std::array<std::string_view, 4> primary_ids{
        "overview", "activity", "security", "diagnostics"};
    static constexpr std::array<std::string_view, 4> primary_copy{
        "A retained page owns ordinary controls and remains stable across selection cycles.",
        "Ctrl+Tab and Ctrl+Shift+Tab traverse pages from focused child controls.",
        "Hidden pages leave paint, hit testing, focus traversal, and accessibility.",
        "Removal and direct disposal reconcile the surviving selected page."};

    void build_primary() {
        TabControl& primary = *document_.gallery_tabs_primary;
        primary.set_style(page_style());
        primary.set_item_size({150.0, 30.0});
        for (std::size_t index = 0U; index < primary_names.size(); ++index) {
            std::shared_ptr<TabPage> tab_page = make_control<TabPage>(
                StableId("gallery.tabs.primary." + std::string(primary_ids[index])),
                std::string(primary_names[index]));
            std::shared_ptr<Label> title = label(
                "gallery.tabs.primary.page.title." + std::to_string(index),
                std::string(primary_names[index]) + " workspace", 16.0, 700,
                shell_blue_dark);
            (*title).set_requested_bounds({22.0, 22.0, 420.0, 30.0});
            (*tab_page).add_child(title);
            std::shared_ptr<Label> copy = label(
                "gallery.tabs.primary.page.copy." + std::to_string(index),
                std::string(primary_copy[index]), 11.0, 400, ink);
            (*copy).set_requested_bounds({22.0, 58.0, 660.0, 26.0});
            (*tab_page).add_child(copy);
            (*tab_page).add_child(page_content(index));
            // The first tab names its image by key, the third by index; the
            // Gallery gives the control its image list once the window
            // holds the images.
            if (index == 0U) (*tab_page).set_image_key("folder");
            if (index == 2U) (*tab_page).set_image_index(0);
            primary.add_page(tab_page);
        }
        primary.set_selected_index(1U);
    }

    // One ordinary control per retained page, proving hidden pages keep
    // their children while leaving paint, hit testing and semantics.
    [[nodiscard]] Control::Ptr page_content(const std::size_t index) {
        if (index == 0U) {
            std::shared_ptr<Button> action = make_control<Button>(
                StableId("gallery.tabs.primary.action"), "Run page action");
            (*action).set_visual_style(ButtonVisualStyle::accent);
            (*action).set_requested_bounds({22.0, 104.0, 170.0, 36.0});
            return action;
        }
        if (index == 1U) {
            std::shared_ptr<CheckBox> toggle = make_control<CheckBox>(
                StableId("gallery.tabs.primary.activity.toggle"),
                "Keep activity visible");
            (*toggle).set_indicator_style(ChoiceIndicatorStyle::toggle);
            (*toggle).set_requested_bounds({22.0, 104.0, 220.0, 30.0});
            (*toggle).bind(activity_visible_);
            return toggle;
        }
        if (index == 2U) {
            std::shared_ptr<RadioButton> radio = make_control<RadioButton>(
                StableId("gallery.tabs.primary.security.radio"),
                "Professional policy");
            (*radio).set_checked(true);
            (*radio).set_requested_bounds({22.0, 104.0, 220.0, 30.0});
            return radio;
        }
        std::shared_ptr<ProgressBar> value = make_control<ProgressBar>(
            StableId("gallery.tabs.primary.diagnostics.progress"));
        (*value).set_range(0.0, 100.0);
        (*value).set_value(78.0);
        (*value).accessible_name = "Diagnostics completion";
        (*value).set_requested_bounds({22.0, 108.0, 360.0, 24.0});
        return value;
    }

    // The captions, the controls and their accessible names are the
    // document's; the alignment, appearance, item size and pages are made
    // here.
    void build_variants() {
        const std::array<std::shared_ptr<TabControl>, 3> variants{
            document_.gallery_tabs_variant_0, document_.gallery_tabs_variant_1,
            document_.gallery_tabs_variant_2};
        constexpr std::array<std::tuple<TabAlignment, TabAppearance, std::string_view>, 3>
            variants_spec{{
                {TabAlignment::bottom, TabAppearance::buttons, "Bottom · buttons"},
                {TabAlignment::left, TabAppearance::flat_buttons, "Left · flat"},
                {TabAlignment::right, TabAppearance::normal, "Right · normal"},
            }};
        for (std::size_t index = 0U; index < variants_spec.size(); ++index) {
            TabControl& tabs = *variants[index];
            tabs.set_style(page_style());
            tabs.set_alignment(std::get<0>(variants_spec[index]));
            tabs.set_appearance(std::get<1>(variants_spec[index]));
            tabs.set_item_size(index == 0U ? Size{86.0, 28.0} : Size{82.0, 28.0});
            for (std::size_t page_index = 0U; page_index < 3U; ++page_index) {
                std::shared_ptr<TabPage> tab_page = make_control<TabPage>(
                    StableId("gallery.tabs.variant." + std::to_string(index) +
                             ".page." + std::to_string(page_index)),
                    page_index == 0U ? "One" : page_index == 1U ? "Two" : "Three");
                std::shared_ptr<Label> content = label(
                    "gallery.tabs.variant.content." + std::to_string(index) + "." +
                        std::to_string(page_index),
                    "Retained page " + std::to_string(page_index + 1U), 10.0, 600,
                    muted);
                (*content).set_requested_bounds({12.0, 14.0, 150.0, 24.0});
                (*tab_page).add_child(content);
                tabs.add_page(tab_page);
            }
            tabs.set_selected_index(index % 3U);
        }
    }

    // A PageStrip over a set the page owns: more content-sized tabs than
    // fit, so the scroll and list buttons show.
    void build_strip() {
        for (std::size_t index = 0U; index < strip_titles.size(); ++index) {
            PageRecord record{};
            record.id = PageId{"strip." + std::to_string(index)};
            record.title = std::string(strip_titles[index]);
            static_cast<void>(strip_pages_.insert(record, PagePlacement::end_of_group()));
        }
        strip_ = make_control<PageStrip>(StableId("gallery.tabs.strip"), strip_pages_);
        (*strip_).accessible_name = "Section tabs";
        insert_before(*document_.gallery_tabs_strip_group, strip_, {20.0, 40.0, 620.0, 28.0},
                      *document_.gallery_tabs_strip_status);
    }

    // A PageView beside the TabControl: the same retained pages in the strip
    // preset, two of them created only when first selected.
    void build_view() {
        on((*document_.gallery_tabs_view).content_requested(), *this, &TabsPage::on_view_content_requested);
        std::shared_ptr<Label> summary = label(
            "gallery.tabs.view.summary", "Ctrl+PageDown and Ctrl+Tab move between pages from inside one.",
            11.0, 400, ink);
        static_cast<void>((*document_.gallery_tabs_view).add_page(view_record("summary", "&Summary"), summary));
        std::shared_ptr<Panel> details = make_control<Panel>(StableId("gallery.tabs.view.details"));
        std::shared_ptr<TextBox> name = make_control<TextBox>(StableId("gallery.tabs.view.details.name"));
        (*name).accessible_name = "Name";
        (*name).set_requested_bounds({16.0, 16.0, 240.0, 26.0});
        (*details).add_child(name);
        std::shared_ptr<TextBox> notes = make_control<TextBox>(StableId("gallery.tabs.view.details.notes"));
        (*notes).accessible_name = "Notes";
        (*notes).set_requested_bounds({16.0, 52.0, 240.0, 26.0});
        (*details).add_child(notes);
        static_cast<void>((*document_.gallery_tabs_view).add_page(view_record("details", "&Details"), details));
        static_cast<void>((*document_.gallery_tabs_view).add_page(view_record("report", "&Report"), Control::Ptr{}));
        static_cast<void>((*document_.gallery_tabs_view).add_page(view_record("log", "&Log"), Control::Ptr{}));
    }

    // A PageView in the document preset: closable documents with close
    // buttons on the selected and hot tabs, one pinned page and one
    // modified, shrinking and then overflowing into the count-and-arrow
    // list. The demo gives the strip an image list so the pinned page
    // collapses to its icon; without one it keeps its label.
    void build_documents() {
        PageViewOptions options{};
        options.preset = PageViewPreset::document;
        options.pinning_enabled = true;
        documents_ = make_control<PageView>(StableId("gallery.tabs.documents"), options);
        (*documents_).accessible_name = "Open documents";
        PageItemWidths widths{};
        widths.minimum = 80.0;
        (*(*documents_).strip()).set_item_widths(widths);
        for (std::size_t index = 0U; index < document_titles.size(); ++index) {
            PageRecord record{};
            record.id = PageId{"doc." + std::to_string(index)};
            record.title = std::string(document_titles[index]);
            record.closable = true;
            record.pinned = index == 0U;
            record.modified = index == 2U;
            record.image_key = "folder";
            std::shared_ptr<Label> body = label(
                "gallery.tabs.documents.body." + std::to_string(index),
                std::string(document_titles[index]) + " · middle-click or Ctrl+F4 closes it", 11.0, 400, ink);
            static_cast<void>((*documents_).add_page(record, body));
        }
        static_cast<void>((*documents_).pages().select(PageId{"doc.1"}, PageSelectionCause::programmatic));
        insert_before(*document_.gallery_tabs_documents_group, documents_, {20.0, 36.0, 600.0, 146.0},
                      *document_.gallery_tabs_documents_status);
        on((*(*documents_).strip()).page_close_requested(), *this, &TabsPage::on_document_close_requested);
        on((*(*documents_).strip()).page_context_menu_requested(), *this, &TabsPage::on_document_context_menu);
        on((*(*documents_).strip()).page_double_clicked(), *this, &TabsPage::on_document_double_clicked);
    }

    // The document strip shows no menu of its own: the page reports the
    // request (a docking workspace shows its position menu here).
    void on_document_context_menu(PageContextMenuRequest& request) {
        (*document_.gallery_tabs_documents_status).set_text("Context menu asked for " + document_title(request.page));
    }

    void on_document_double_clicked(const PageDoubleClick& event) {
        (*document_.gallery_tabs_documents_status).set_text("Double-clicked " + document_title(event.page) + " (docking floats it)");
    }

    [[nodiscard]] std::string document_title(const PageId& page) const {
        const std::optional<PagePosition> found = (*documents_).pages().find(page);
        if (!found) return page.value;
        const std::string title = (*documents_).pages().record_at(*found).title;
        return title;
    }

    // Two PageViews with side strips (owner decision DK-42): on the left
    // edge the labels read from bottom to top, on the right edge from top to
    // bottom; Up and Down move between the tabs.
    void build_side_strips() {
        const std::array<std::shared_ptr<PageView>, 2> views{document_.gallery_tabs_side_left,
                                                             document_.gallery_tabs_side_right};
        const std::array<PageStripPlacement, 2> edges{PageStripPlacement::left, PageStripPlacement::right};
        const std::array<std::string_view, 2> names{"left", "right"};
        for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
            const std::string prefix = "gallery.tabs.side." + std::string(names[edge]);
            PageView& view = *views[edge];
            (*view.strip()).set_placement(edges[edge]);
            for (std::size_t index = 0U; index < side_titles.size(); ++index) {
                PageRecord record{};
                record.id = PageId{std::string(names[edge]) + "." + std::to_string(index)};
                record.title = std::string(side_titles[index]);
                std::shared_ptr<Label> body = label(
                    prefix + ".body." + std::to_string(index),
                    std::string(side_titles[index]) + " · " + std::string(names[edge]) + " edge", 11.0, 400, ink);
                static_cast<void>(view.add_page(record, body));
            }
        }
    }

    // The view removes the page after this handler, which leaves the
    // request unhandled.
    void on_document_close_requested(PageCloseRequest& request) {
        const std::optional<PagePosition> found = (*documents_).pages().find(request.page);
        if (!found) return;
        const std::string title = (*documents_).pages().record_at(*found).title;
        std::string_view cause = "close button";
        if (request.cause == PageCloseCause::middle_button) cause = "middle click";
        if (request.cause == PageCloseCause::keyboard) cause = "Ctrl+F4";
        if (request.cause == PageCloseCause::semantic) cause = "accessibility";
        (*document_.gallery_tabs_documents_status).set_text("Closed " + title + " · " + std::string(cause));
    }

    [[nodiscard]] static PageRecord view_record(std::string_view id, std::string_view title) {
        PageRecord record{};
        record.id = PageId{"view." + std::string(id)};
        record.title = std::string(title);
        return record;
    }

    // Late content (owner decision DK-16): made the first time its page is
    // selected, and never asked for again.
    void on_view_content_requested(const PageContentRequest& request) {
        std::shared_ptr<Label> made = label(
            "gallery.tabs.view.made." + request.page.value,
            "Created when " + request.page.value + " was first selected.", 11.0, 400, ink);
        static_cast<void>((*document_.gallery_tabs_view).set_page_content(request.page, made));
        (*document_.gallery_tabs_view_status).set_text("Created " + request.page.value + " on its first selection");
    }

    // A NavigationPane: six pages with content, four large buttons, the
    // overflow icons and the configure menu; collapsing it shows the bar and
    // the overlay. Its state is shown as the program would save it.
    void build_navigation() {
        (*document_.gallery_tabs_navigation).set_expanded_width(240.0);
        PageSet& pages = (*document_.gallery_tabs_navigation).pages();
        constexpr std::array<std::string_view, 6> ids{"mail", "calendar", "contacts", "tasks", "notes", "folders"};
        constexpr std::array<std::string_view, 6> titles{"Mail", "Calendar", "Contacts", "Tasks", "Notes", "Folders"};
        for (std::size_t index = 0U; index < ids.size(); ++index) {
            PageRecord record{};
            record.id = PageId{std::string(ids[index])};
            record.title = std::string(titles[index]);
            static_cast<void>(pages.insert(record, PagePlacement::end_of_group()));
            std::shared_ptr<Label> content = label(
                "gallery.tabs.navigation.content." + std::string(ids[index]),
                std::string(titles[index]) + " · the selected page's control", 11.0, 400, ink);
            std::unique_ptr<PageContent> held = std::make_unique<PageControlContent>(content);
            static_cast<void>(pages.set_content(record.id, held));
        }
        static_cast<void>((*document_.gallery_tabs_navigation).set_page_hidden(PageId{"notes"}, true));
        on((*document_.gallery_tabs_navigation).state_changed(), *this, &TabsPage::on_navigation_state);
        on((*document_.gallery_tabs_navigation).options_requested(), *this, &TabsPage::on_navigation_options);
    }

    void on_navigation_state(const NavigationPaneState& state) {
        std::string text = state.collapsed ? "Collapsed" : "Expanded";
        text += " · " + std::to_string(state.button_count) + " large buttons · " +
                std::to_string(state.hidden_pages.size()) + " hidden";
        (*document_.gallery_tabs_navigation_status).set_text(text);
    }

    void on_navigation_options(const NavigationPaneOptionsRequest&) {
        (*document_.gallery_tabs_navigation_status).set_text("Options requested · the program shows its own dialog");
    }

    void on_strip_invoked(const PageInvocation& invocation) {
        const std::optional<PagePosition> found = strip_pages_.find(invocation.page);
        if (!found) return;
        (*document_.gallery_tabs_strip_status).set_text("Strip · " + strip_pages_.record_at(*found).title + " selected");
    }

    // State event: fires for pointer, keyboard, semantic and programmatic
    // selection alike, so the status line never needs a second route.
    void on_primary_selected(const TabSelectionChange&) {
        const std::shared_ptr<TabPage> selected = (*document_.gallery_tabs_primary).selected_tab();
        if (!selected) return;
        (*document_.gallery_tabs_status).set_text((*selected).text() +
                            " selected · focus and semantics reconciled");
    }

    void on_activity_visible_changed(const bool visible) {
        (*document_.gallery_tabs_status).set_text(visible
            ? "Activity · keep visible on · bound Value<bool> changed"
            : "Activity · keep visible off · bound Value<bool> changed");
    }

    static constexpr std::array<std::string_view, 10> document_titles{
        "README.md",        "page_view.cpp",  "page_strip.cpp", "page_set.cpp",   "page_view.hpp",
        "page_strip.hpp",   "CMakeLists.txt", "PLAN.md",        "README.rst",     "tests.cpp"};

    static constexpr std::array<std::string_view, 4> side_titles{"Explorer", "Search", "Outline", "Output"};

    static constexpr std::array<std::string_view, 14> strip_titles{
        "Overview", "Activity", "Security", "Diagnostics", "Network", "Storage", "Accounts",
        "Notifications", "Display", "Sound", "Keyboard", "Accessibility", "Updates", "About"};

    // Declared before the strip that borrows it.
    PageSet strip_pages_{};
    std::shared_ptr<PageStrip> strip_;
    std::shared_ptr<PageView> documents_;
    Value<bool> activity_visible_{true};
    // Declared after the Value its toggle binds, so it is released first.
    TabsDocument<TabsPage> document_{};
};

void add_tabs_and_pages(const std::shared_ptr<Surface>& page,
                        const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<TabsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
