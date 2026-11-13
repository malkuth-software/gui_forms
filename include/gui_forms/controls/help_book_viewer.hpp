#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/help_book.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/help/help_search/help_search.hpp"
#include "gui_forms/help/helpbook_section/helpbook_section.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Button;
class ButtonBase;
class CheckBox;
class ListBox;
class TabControl;
class TabPage;
class TextBox;
class TreeView;
struct ListSelectionChange;
struct TabSelectionChange;
struct TreeSelectionChange;

// The words HelpBookViewer shows: the help system's own text, translated
// with the program's. `built_in_language` is the untranslatable way back
// (translation design): the name of the program's built-in language, which
// the program supplies in that language; empty hides the link.
struct GUI_FORMS_API HelpBookViewerLabels final {
    GUI_FORMS_LOCALIZABLE(text) std::string contents{"Contents"};
    GUI_FORMS_LOCALIZABLE(text) std::string index{"Index"};
    GUI_FORMS_LOCALIZABLE(text) std::string search{"Search"};
    GUI_FORMS_LOCALIZABLE(text) std::string bookmarks{"Bookmarks"};
    GUI_FORMS_LOCALIZABLE(text) std::string hide{"Hide"};
    GUI_FORMS_LOCALIZABLE(text) std::string show{"Show"};
    GUI_FORMS_LOCALIZABLE(text) std::string back{"Back"};
    GUI_FORMS_LOCALIZABLE(text) std::string forward{"Forward"};
    GUI_FORMS_LOCALIZABLE(text) std::string home{"Home"};
    GUI_FORMS_LOCALIZABLE(text) std::string print{"Print"};
    GUI_FORMS_LOCALIZABLE(text) std::string locate{"Show in contents"};
    GUI_FORMS_LOCALIZABLE(text) std::string add{"Add"};
    GUI_FORMS_LOCALIZABLE(text) std::string remove{"Remove"};
    GUI_FORMS_LOCALIZABLE(text) std::string titles_only{"Search titles only"};
    GUI_FORMS_LOCALIZABLE(placeholder) std::string index_placeholder{"Type a keyword"};
    GUI_FORMS_LOCALIZABLE(placeholder) std::string search_placeholder{"Type words to find"};
    std::string built_in_language{};
};

// The navigation tabs, in CHM's order (help system study, section 2).
enum class HelpNavigationTab : std::uint8_t { contents, index, search, bookmarks };

// Printing was asked for: the page shown (`page`), or the contents book
// under it when `book` is set. The help system's handler prints.
struct GUI_FORMS_API HelpPrintRequest final {
    std::string page{};
    bool book{};
    bool handled{};
};

struct GUI_FORMS_API HelpBookViewerSnapshot final {
    std::size_t history_size{};
    std::size_t history_position{};
    std::size_t navigations{};
    std::size_t searches{};
    HelpSearchStatus last_search{HelpSearchStatus::empty_query};
    std::size_t last_hit_count{};
};

// HelpBookViewer: the three-pane help viewer (help system study, sections 2
// and 6): a toolbar (Hide/Show, Back, Forward, Home, Print, Show in
// contents, and the untranslatable way back), the navigation tabs
// (Contents, Index, Search, Bookmarks) and a HelpBook. It can be placed in
// any window or dock panel; HelpBookWindow presents it in a window of its
// own. It shows one compiled language's helpbook section, never opens a
// file or a network resource, and passes external links on.
//
// Keyboard: Alt+C, Alt+N, Alt+S and Alt+I select Contents, Index, Search
// and Bookmarks; F6 moves between the navigation pane and the book;
// Alt+Left and Alt+Right move in history and Alt+Home goes home; Ctrl+P
// prints.
class GUI_FORMS_API HelpBookViewer final : public Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    // The most pages history keeps; the oldest goes first.
    static constexpr std::size_t maximum_history = 256U;

    explicit HelpBookViewer(StableId stable_id);
    void initialize_control_tree();

    // Shows `section` (shared, read-only): fills the contents and the index
    // and shows the home page. Null shows nothing.
    void set_section(std::shared_ptr<const HelpBookSection> section);
    [[nodiscard]] const std::shared_ptr<const HelpBookSection>& section() const noexcept { return section_; }
    // The home page's stable ID; empty takes the contents' first page.
    void set_home(std::string stable_id);
    [[nodiscard]] std::string home_page() const;

    // Opens record `stable_id` at `anchor`, adding it to history and
    // selecting it in the contents (sync). Returns false, changing nothing,
    // for a record the section does not hold.
    bool show(std::string_view stable_id, std::string_view anchor = {});
    void show_contents();
    // Selects the Index tab with `keyword` typed.
    void show_index(std::string_view keyword);
    // Selects the Search tab and searches for `text`.
    void show_search(std::string_view text);
    void select_tab(HelpNavigationTab tab);
    [[nodiscard]] HelpNavigationTab selected_tab() const noexcept;

    bool back();
    bool forward();
    bool home();
    [[nodiscard]] bool can_go_back() const noexcept { return history_position_ > 0U; }
    [[nodiscard]] bool can_go_forward() const noexcept { return history_position_ + 1U < history_.size(); }
    [[nodiscard]] std::string current_page() const;
    // Selects the shown page in the contents (CHM's Locate).
    void locate();

    void set_navigation_visible(bool visible);
    [[nodiscard]] bool navigation_visible() const noexcept { return navigation_visible_; }
    void set_navigation_width(double width);
    [[nodiscard]] double navigation_width() const noexcept { return navigation_width_; }
    void set_labels(HelpBookViewerLabels labels);
    [[nodiscard]] const HelpBookViewerLabels& labels() const noexcept { return labels_; }
    void set_images(std::vector<HelpBookImage> images);

    // Bookmarks are stable IDs; the application keeps them in its settings
    // (bookmarks_changed says when).
    void set_bookmarks(std::vector<std::string> stable_ids);
    [[nodiscard]] const std::vector<std::string>& bookmarks() const& noexcept { return bookmarks_; }
    const std::vector<std::string>& bookmarks() const&& = delete;
    bool add_bookmark();
    bool remove_selected_bookmark();

    // The index tab's rows: one per keyword and subkeyword, in index order.
    [[nodiscard]] std::size_t index_row_count() const noexcept { return index_rows_.size(); }
    // Opens index row `row`: its record, or the chooser when it names
    // several records. Returns false when the row is out of range.
    bool open_index_row(std::size_t row);
    // The records the chooser offers (stable IDs), empty when it is closed.
    [[nodiscard]] std::vector<std::string> chooser_records() const;
    bool open_chooser_entry(std::size_t entry);
    // The last search's hits as stable IDs, best first.
    [[nodiscard]] std::vector<std::string> search_results() const;
    bool open_search_result(std::size_t result);
    void set_titles_only(bool titles_only);

    [[nodiscard]] std::shared_ptr<HelpBook> book() const noexcept { return book_; }
    [[nodiscard]] std::shared_ptr<TreeView> contents_tree() const noexcept { return contents_tree_; }
    [[nodiscard]] std::shared_ptr<TextBox> index_field() const noexcept { return index_field_; }
    [[nodiscard]] std::shared_ptr<TextBox> search_field() const noexcept { return search_field_; }
    [[nodiscard]] HelpBookViewerSnapshot snapshot() const noexcept;

    // A page was shown (its stable ID).
    [[nodiscard]] Event<const std::string&>& navigated() noexcept { return navigated_; }
    // An external link was followed: the application decides (ruling 6).
    [[nodiscard]] Event<HelpLinkActivation&>& external_link() noexcept { return external_link_; }
    // A popup link was followed: the help system shows the paper popup.
    [[nodiscard]] Event<HelpLinkActivation&>& popup_requested() noexcept { return popup_requested_; }
    [[nodiscard]] Event<HelpPrintRequest&>& print_requested() noexcept { return print_requested_; }
    [[nodiscard]] Event<const std::vector<std::string>&>& bookmarks_changed() noexcept { return bookmarks_changed_; }
    // The untranslatable way back was chosen: the help system switches help
    // to the built-in language and gives the viewer the new section.
    [[nodiscard]] Event<>& built_in_language_requested() noexcept { return built_in_language_requested_; }

    void arrange(Rect final_bounds) override;
    [[nodiscard]] Size measure(Size available) override;
    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    // One row of the index: the keyword entries it opens
    // (HelpBookSection::keywords from first, count of them).
    struct IndexRow final {
        std::size_t first{};
        std::size_t count{};
    };

    void rebuild_contents();
    void rebuild_index();
    void refresh_bookmark_list();
    void close_chooser();
    void focus_child(const Control::Ptr& child);
    [[nodiscard]] bool book_holds_focus() const;
    [[nodiscard]] std::string record_title(std::string_view stable_id) const;
    void refresh_buttons();
    void apply_labels();
    void navigate_to(std::string_view stable_id, std::string_view anchor, bool record_history);
    void run_search();
    void on_link(HelpLinkActivation& activation);
    void on_back_clicked(ButtonBase& button);
    void on_forward_clicked(ButtonBase& button);
    void on_home_clicked(ButtonBase& button);
    void on_print_clicked(ButtonBase& button);
    void on_hide_clicked(ButtonBase& button);
    void on_locate_clicked(ButtonBase& button);
    void on_language_clicked(ButtonBase& button);
    void on_add_bookmark_clicked(ButtonBase& button);
    void on_remove_bookmark_clicked(ButtonBase& button);
    void on_contents_activated(const std::string& stable_id);
    void on_contents_selected(const TreeSelectionChange& change);
    void on_index_text(const std::string& text);
    void on_index_activated(std::size_t row);
    void on_chooser_activated(std::size_t entry);
    void on_search_text(const std::string& text);
    void on_search_committed(const std::string& text);
    void on_search_activated(std::size_t result);
    void on_titles_only(bool titles_only);
    void on_bookmark_activated(std::size_t entry);

    std::shared_ptr<const HelpBookSection> section_{};
    std::string home_{};
    HelpBookViewerLabels labels_{};
    std::vector<std::string> history_{};
    std::size_t history_position_{};
    std::vector<std::string> bookmarks_{};
    std::vector<IndexRow> index_rows_{};
    std::vector<std::uint32_t> chooser_{};
    std::vector<HelpSearchHit> hits_{};
    HelpSearchQuery query_{};
    bool titles_only_{};
    bool navigation_visible_{true};
    bool syncing_contents_{};
    double navigation_width_{260.0};
    std::size_t navigations_{};
    std::size_t searches_{};
    HelpSearchStatus last_search_{HelpSearchStatus::empty_query};

    std::shared_ptr<Button> hide_button_;
    std::shared_ptr<Button> back_button_;
    std::shared_ptr<Button> forward_button_;
    std::shared_ptr<Button> home_button_;
    std::shared_ptr<Button> print_button_;
    std::shared_ptr<Button> locate_button_;
    std::shared_ptr<Button> language_button_;
    std::shared_ptr<TabControl> tabs_;
    std::shared_ptr<TabPage> contents_page_;
    std::shared_ptr<TabPage> index_page_;
    std::shared_ptr<TabPage> search_page_;
    std::shared_ptr<TabPage> bookmarks_page_;
    std::shared_ptr<TreeView> contents_tree_;
    std::shared_ptr<TextBox> index_field_;
    std::shared_ptr<ListBox> index_list_;
    std::shared_ptr<ListBox> index_chooser_;
    // The index tab's column (detail::HelpNavigationColumn), laid out again
    // when the chooser opens or closes.
    Control::Ptr index_column_;
    std::shared_ptr<TextBox> search_field_;
    std::shared_ptr<CheckBox> titles_only_box_;
    std::shared_ptr<ListBox> search_list_;
    std::shared_ptr<ListBox> bookmarks_list_;
    std::shared_ptr<Button> add_bookmark_button_;
    std::shared_ptr<Button> remove_bookmark_button_;
    std::shared_ptr<HelpBook> book_;

    Event<const std::string&> navigated_;
    Event<HelpLinkActivation&> external_link_;
    Event<HelpLinkActivation&> popup_requested_;
    Event<HelpPrintRequest&> print_requested_;
    Event<const std::vector<std::string>&> bookmarks_changed_;
    Event<> built_in_language_requested_;
};

} // namespace gui_forms
