#include "gui_forms/controls/help/help_book_viewer/help_book_viewer.hpp"

#include "gui_forms/controls/button_base/button/button.hpp"
#include "gui_forms/controls/button_base/check_box/check_box.hpp"
#include "gui_forms/controls/panel/list_box/list_box.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/panel/tree_view/tree_view.hpp"
#include "gui_forms/controls/scrollable_control/container_control/tab_control/tab_control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/tab_control/tab_page/tab_page.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace gui_forms {
namespace detail {

// One navigation tab's content: controls stacked top to bottom with the
// viewer's margins, each a fixed height except one that fills (height 0).
// A row may carry a companion, which takes the right half of the row
// (the bookmarks' Add and Remove buttons). Hidden rows take no space.
class HelpNavigationColumn final : public Control {
public:
    explicit HelpNavigationColumn(StableId stable_id) : Control(std::move(stable_id)) {}

    void add_row(const Control::Ptr& control, double height, const Control::Ptr& companion = {}) {
        rows_.push_back(Row{control, companion, height});
        add_child(control);
        if (companion) add_child(companion);
    }

    void arrange(Rect final_bounds) override;

private:
    struct Row final {
        Control::Ptr control{};
        Control::Ptr companion{};
        double height{};
    };

    std::vector<Row> rows_{};
};

} // namespace detail

namespace {

constexpr double viewer_margin = 4.0;
constexpr double viewer_gap = 4.0;
constexpr double toolbar_height = 34.0;
constexpr double toolbar_button_height = 26.0;
constexpr double toolbar_button_minimum = 32.0;
constexpr double field_height = 24.0;
constexpr double check_height = 22.0;
constexpr double chooser_height = 120.0;
constexpr double bookmark_button_height = 26.0;
constexpr double minimum_navigation_width = 120.0;
constexpr double maximum_navigation_width = 2000.0;
// The navigation pane never takes more than this share of the viewer.
constexpr double maximum_navigation_share = 0.6;
constexpr double preferred_width = 760.0;
constexpr double preferred_height = 540.0;

// The events HelpBookViewer adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 6> help_book_viewer_interactions{{
    {.name = "navigated", .payload = "std::string", .mutable_payload = false},
    {.name = "external_link", .payload = "HelpLinkActivation", .mutable_payload = true},
    {.name = "popup_requested", .payload = "HelpLinkActivation", .mutable_payload = true},
    {.name = "print_requested", .payload = "HelpPrintRequest", .mutable_payload = true},
    {.name = "bookmarks_changed", .payload = "std::vector<std::string>", .mutable_payload = false},
    {.name = "built_in_language_requested", .payload = "", .mutable_payload = false},
}};

[[nodiscard]] bool same_index_row(const HelpKeywordEntry& first, const HelpKeywordEntry& second) noexcept {
    return first.keyword == second.keyword && first.subkeyword == second.subkeyword;
}

} // namespace

void detail::HelpNavigationColumn::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const double width = std::max(0.0, final_bounds.width - 2.0 * viewer_margin);
    // The fixed rows' height first, so the fill row takes what remains.
    double fixed = 0.0;
    std::size_t shown = 0U;
    for (const Row& row : rows_) {
        if (!(*row.control).visible.read()) continue;
        fixed += row.height;
        ++shown;
    }
    const double gaps = shown > 1U ? static_cast<double>(shown - 1U) * viewer_gap : 0.0;
    const double fill = std::max(0.0, final_bounds.height - 2.0 * viewer_margin - fixed - gaps);
    double top = viewer_margin;
    for (const Row& row : rows_) {
        if (!(*row.control).visible.read()) continue;
        const double height = row.height > 0.0 ? row.height : fill;
        if (row.companion) {
            const double half = std::max(0.0, (width - viewer_gap) / 2.0);
            set_child_layout(row.control, Rect{viewer_margin, top, half, height});
            set_child_layout(row.companion, Rect{viewer_margin + half + viewer_gap, top, half, height});
        } else {
            set_child_layout(row.control, Rect{viewer_margin, top, width, height});
        }
        top += height + viewer_gap;
    }
}

HelpBookViewer::HelpBookViewer(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = false;
    tab_stop = false;
}

// --- Tree ---------------------------------------------------------------------------

void HelpBookViewer::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    hide_button_ = make_control<Button>(StableId(prefix + ".hide"));
    back_button_ = make_control<Button>(StableId(prefix + ".back"));
    forward_button_ = make_control<Button>(StableId(prefix + ".forward"));
    home_button_ = make_control<Button>(StableId(prefix + ".home"));
    print_button_ = make_control<Button>(StableId(prefix + ".print"));
    locate_button_ = make_control<Button>(StableId(prefix + ".locate"));
    language_button_ = make_control<Button>(StableId(prefix + ".built-in-language"));

    tabs_ = make_control<TabControl>(StableId(prefix + ".tabs"));
    contents_page_ = make_control<TabPage>(StableId(prefix + ".contents"));
    index_page_ = make_control<TabPage>(StableId(prefix + ".index"));
    search_page_ = make_control<TabPage>(StableId(prefix + ".search"));
    bookmarks_page_ = make_control<TabPage>(StableId(prefix + ".bookmarks"));

    contents_tree_ = make_control<TreeView>(StableId(prefix + ".contents-tree"));
    index_field_ = make_control<TextBox>(StableId(prefix + ".index-field"));
    index_list_ = make_control<ListBox>(StableId(prefix + ".index-list"));
    index_chooser_ = make_control<ListBox>(StableId(prefix + ".index-chooser"));
    search_field_ = make_control<TextBox>(StableId(prefix + ".search-field"));
    titles_only_box_ = make_control<CheckBox>(StableId(prefix + ".titles-only"));
    search_list_ = make_control<ListBox>(StableId(prefix + ".search-list"));
    bookmarks_list_ = make_control<ListBox>(StableId(prefix + ".bookmarks-list"));
    add_bookmark_button_ = make_control<Button>(StableId(prefix + ".add-bookmark"));
    remove_bookmark_button_ = make_control<Button>(StableId(prefix + ".remove-bookmark"));
    book_ = make_control<HelpBook>(StableId(prefix + ".book"));
    (*book_).set_close_visible(false);
    (*index_chooser_).visible = false;

    std::shared_ptr<detail::HelpNavigationColumn> contents_column =
        make_control<detail::HelpNavigationColumn>(StableId(prefix + ".contents-column"));
    (*contents_column).add_row(contents_tree_, 0.0);
    std::shared_ptr<detail::HelpNavigationColumn> index_column =
        make_control<detail::HelpNavigationColumn>(StableId(prefix + ".index-column"));
    (*index_column).add_row(index_field_, field_height);
    (*index_column).add_row(index_list_, 0.0);
    (*index_column).add_row(index_chooser_, chooser_height);
    std::shared_ptr<detail::HelpNavigationColumn> search_column =
        make_control<detail::HelpNavigationColumn>(StableId(prefix + ".search-column"));
    (*search_column).add_row(search_field_, field_height);
    (*search_column).add_row(titles_only_box_, check_height);
    (*search_column).add_row(search_list_, 0.0);
    std::shared_ptr<detail::HelpNavigationColumn> bookmarks_column =
        make_control<detail::HelpNavigationColumn>(StableId(prefix + ".bookmarks-column"));
    (*bookmarks_column).add_row(bookmarks_list_, 0.0);
    (*bookmarks_column).add_row(add_bookmark_button_, bookmark_button_height, remove_bookmark_button_);
    index_column_ = index_column;

    (*contents_column).dock = DockStyle::fill;
    (*index_column).dock = DockStyle::fill;
    (*search_column).dock = DockStyle::fill;
    (*bookmarks_column).dock = DockStyle::fill;
    (*contents_page_).add_child(contents_column);
    (*index_page_).add_child(index_column);
    (*search_page_).add_child(search_column);
    (*bookmarks_page_).add_child(bookmarks_column);
    (*tabs_).add_page(contents_page_);
    (*tabs_).add_page(index_page_);
    (*tabs_).add_page(search_page_);
    (*tabs_).add_page(bookmarks_page_);

    add_child(hide_button_);
    add_child(back_button_);
    add_child(forward_button_);
    add_child(home_button_);
    add_child(print_button_);
    add_child(locate_button_);
    add_child(language_button_);
    add_child(tabs_);
    add_child(book_);

    on((*hide_button_).clicked(), *this, &HelpBookViewer::on_hide_clicked);
    on((*back_button_).clicked(), *this, &HelpBookViewer::on_back_clicked);
    on((*forward_button_).clicked(), *this, &HelpBookViewer::on_forward_clicked);
    on((*home_button_).clicked(), *this, &HelpBookViewer::on_home_clicked);
    on((*print_button_).clicked(), *this, &HelpBookViewer::on_print_clicked);
    on((*locate_button_).clicked(), *this, &HelpBookViewer::on_locate_clicked);
    on((*language_button_).clicked(), *this, &HelpBookViewer::on_language_clicked);
    on((*add_bookmark_button_).clicked(), *this, &HelpBookViewer::on_add_bookmark_clicked);
    on((*remove_bookmark_button_).clicked(), *this, &HelpBookViewer::on_remove_bookmark_clicked);
    on((*contents_tree_).item_activated(), *this, &HelpBookViewer::on_contents_activated);
    on((*contents_tree_).selection_changed(), *this, &HelpBookViewer::on_contents_selected);
    on((*index_field_).text_changed(), *this, &HelpBookViewer::on_index_text);
    on((*index_list_).item_activated(), *this, &HelpBookViewer::on_index_activated);
    on((*index_chooser_).item_activated(), *this, &HelpBookViewer::on_chooser_activated);
    on((*search_field_).text_changed(), *this, &HelpBookViewer::on_search_text);
    on((*search_field_).committed(), *this, &HelpBookViewer::on_search_committed);
    on((*search_list_).item_activated(), *this, &HelpBookViewer::on_search_activated);
    on((*titles_only_box_).checked_changed(), *this, &HelpBookViewer::on_titles_only);
    on((*bookmarks_list_).item_activated(), *this, &HelpBookViewer::on_bookmark_activated);
    on((*book_).link_activated(), *this, &HelpBookViewer::on_link);
    apply_labels();
}

// --- Section and pages ----------------------------------------------------------------

void HelpBookViewer::set_section(std::shared_ptr<const HelpBookSection> section) {
    require_mutable();
    section_ = std::move(section);
    history_.clear();
    history_position_ = 0U;
    hits_.clear();
    query_ = HelpSearchQuery{};
    last_search_ = HelpSearchStatus::empty_query;
    (*search_list_).items = std::vector<std::string>{};
    close_chooser();
    rebuild_contents();
    rebuild_index();
    refresh_bookmark_list();
    const std::string start = home_page();
    if (start.empty()) {
        (*book_).show_page(HelpPageModel{});
    } else {
        navigate_to(start, {}, true);
    }
    refresh_buttons();
}

void HelpBookViewer::set_home(std::string stable_id) {
    require_mutable();
    home_ = std::move(stable_id);
    refresh_buttons();
}

std::string HelpBookViewer::home_page() const {
    if (!section_) return {};
    const HelpBookSection& section = *section_;
    if (!home_.empty() && section.find(home_).has_value()) return home_;
    for (const HelpContentsNode& node : section.contents.nodes) {
        if (section.find(node.stable_id).has_value()) return node.stable_id;
    }
    if (!section.records.empty()) return section.records.front().stable_id;
    return {};
}

std::string HelpBookViewer::record_title(std::string_view stable_id) const {
    if (!section_) return std::string(stable_id);
    const std::optional<std::size_t> record = (*section_).find(stable_id);
    if (!record.has_value()) return std::string(stable_id);
    const std::string& title = (*section_).records[*record].title;
    return title.empty() ? std::string(stable_id) : title;
}

bool HelpBookViewer::show(std::string_view stable_id, std::string_view anchor) {
    require_mutable();
    if (!section_ || !(*section_).find(stable_id).has_value()) return false;
    navigate_to(stable_id, anchor, true);
    return true;
}

void HelpBookViewer::navigate_to(std::string_view stable_id, std::string_view anchor, bool record_history) {
    const std::string page(stable_id);
    const bool same_page = !history_.empty() && history_[history_position_] == page;
    if (!same_page || !record_history) {
        (*book_).set_highlight({});
        (*book_).show_record(*section_, page, anchor);
    } else if (!anchor.empty()) {
        (*book_).scroll_to_anchor(anchor);
    }
    if (record_history && !same_page) {
        if (!history_.empty()) history_.resize(history_position_ + 1U);
        history_.push_back(page);
        if (history_.size() > maximum_history) history_.erase(history_.begin());
        history_position_ = history_.size() - 1U;
    }
    locate();
    ++navigations_;
    refresh_buttons();
    navigated_.emit(page);
}

void HelpBookViewer::locate() {
    const std::string page = current_page();
    if (!section_ || page.empty()) return;
    // find answers help_no_parent for a page with no node: nothing to locate.
    const std::uint32_t node = (*section_).contents.find(page);
    if (node == help_no_parent || node >= (*section_).contents.nodes.size()) return;
    // Open the books above the page, then select it, without navigating.
    syncing_contents_ = true;
    std::uint32_t parent = (*section_).contents.nodes[node].parent;
    std::size_t steps = 0U;
    while (parent != help_no_parent && steps < (*section_).contents.nodes.size()) {
        (*contents_tree_).set_expanded((*section_).contents.nodes[parent].stable_id, true);
        parent = (*section_).contents.nodes[parent].parent;
        ++steps;
    }
    (*contents_tree_).selected_id = page;
    syncing_contents_ = false;
}

std::string HelpBookViewer::current_page() const {
    if (history_.empty()) return {};
    return history_[history_position_];
}

bool HelpBookViewer::back() {
    require_mutable();
    if (!can_go_back()) return false;
    --history_position_;
    navigate_to(history_[history_position_], {}, false);
    return true;
}

bool HelpBookViewer::forward() {
    require_mutable();
    if (!can_go_forward()) return false;
    ++history_position_;
    navigate_to(history_[history_position_], {}, false);
    return true;
}

bool HelpBookViewer::home() {
    const std::string start = home_page();
    if (start.empty()) return false;
    return show(start);
}

// --- Tabs -------------------------------------------------------------------------------

void HelpBookViewer::select_tab(HelpNavigationTab tab) {
    require_mutable();
    if (!navigation_visible_) set_navigation_visible(true);
    (*tabs_).selected_index = std::optional<std::size_t>(static_cast<std::size_t>(tab));
}

HelpNavigationTab HelpBookViewer::selected_tab() const noexcept {
    const std::optional<std::size_t> selected = (*tabs_).selected_index.read();
    if (!selected.has_value() || *selected > static_cast<std::size_t>(HelpNavigationTab::bookmarks)) {
        return HelpNavigationTab::contents;
    }
    return static_cast<HelpNavigationTab>(*selected);
}

void HelpBookViewer::show_contents() {
    select_tab(HelpNavigationTab::contents);
    locate();
    focus_child(contents_tree_);
}

void HelpBookViewer::show_index(std::string_view keyword) {
    select_tab(HelpNavigationTab::index);
    const std::string typed(keyword);
    (*index_field_).text = typed;
    on_index_text(typed);
    focus_child(index_field_);
}

void HelpBookViewer::show_search(std::string_view text) {
    select_tab(HelpNavigationTab::search);
    (*search_field_).text = std::string(text);
    run_search();
    focus_child(search_field_);
}

// --- Contents -----------------------------------------------------------------------------

void HelpBookViewer::rebuild_contents() {
    std::vector<TreeViewItem> items;
    if (section_) {
        const std::vector<HelpContentsNode>& nodes = (*section_).contents.nodes;
        items.reserve(nodes.size());
        for (std::size_t index = 0U; index < nodes.size(); ++index) {
            const HelpContentsNode& node = nodes[index];
            TreeViewItem item{};
            item.stable_id = node.stable_id;
            item.text = node.title.empty() ? node.stable_id : node.title;
            item.depth = node.depth;
            item.expandable = index + 1U < nodes.size() && nodes[index + 1U].parent == index;
            item.expanded = item.expandable && node.depth == 0U;
            items.push_back(std::move(item));
        }
    }
    syncing_contents_ = true;
    (*contents_tree_).set_items(std::move(items));
    syncing_contents_ = false;
}

void HelpBookViewer::on_contents_activated(const std::string& stable_id) {
    if (syncing_contents_) return;
    show(stable_id);
}

void HelpBookViewer::on_contents_selected(const TreeSelectionChange& change) {
    if (syncing_contents_ || change.current_id.empty()) return;
    show(change.current_id);
}

// --- Index ----------------------------------------------------------------------------------

void HelpBookViewer::rebuild_index() {
    index_rows_.clear();
    std::vector<std::string> texts;
    if (section_) {
        const std::vector<HelpKeywordEntry>& keywords = (*section_).keywords;
        std::size_t first = 0U;
        while (first < keywords.size()) {
            std::size_t end = first + 1U;
            while (end < keywords.size() && same_index_row(keywords[first], keywords[end])) ++end;
            const HelpKeywordEntry& entry = keywords[first];
            if (!entry.subkeyword.empty()) {
                // A subkeyword whose keyword has no row of its own gets a
                // heading row that opens nothing.
                const bool keyword_shown = first > 0U && keywords[first - 1U].keyword == entry.keyword;
                if (!keyword_shown) {
                    index_rows_.push_back(IndexRow{first, 0U});
                    texts.push_back(entry.keyword);
                }
                index_rows_.push_back(IndexRow{first, end - first});
                texts.push_back("    " + entry.subkeyword);
            } else {
                index_rows_.push_back(IndexRow{first, end - first});
                texts.push_back(entry.keyword);
            }
            first = end;
        }
    }
    (*index_list_).items = std::move(texts);
}

void HelpBookViewer::on_index_text(const std::string& text) {
    close_chooser();
    if (!section_ || index_rows_.empty()) return;
    std::string folded;
    fold_help_text(text, folded);
    const std::size_t position = (*section_).keyword_position(folded);
    std::size_t row = index_rows_.size() - 1U;
    for (std::size_t index = 0U; index < index_rows_.size(); ++index) {
        if (index_rows_[index].first + std::max<std::size_t>(index_rows_[index].count, 1U) > position) {
            row = index;
            break;
        }
    }
    (*index_list_).select_index(row);
}

bool HelpBookViewer::open_index_row(std::size_t row) {
    require_mutable();
    if (!section_ || row >= index_rows_.size()) return false;
    const IndexRow index_row = index_rows_[row];
    if (index_row.count == 0U) return true;
    std::vector<std::uint32_t> records;
    for (std::size_t entry = index_row.first; entry < index_row.first + index_row.count; ++entry) {
        const std::uint32_t record = (*section_).keywords[entry].record;
        if (std::find(records.begin(), records.end(), record) == records.end()) records.push_back(record);
    }
    if (records.size() == 1U) {
        close_chooser();
        return show((*section_).records[records.front()].stable_id);
    }
    // Several topics: the chooser lists them (CHM's Topics Found).
    chooser_ = std::move(records);
    std::vector<std::string> titles;
    titles.reserve(chooser_.size());
    for (const std::uint32_t record : chooser_) titles.push_back(record_title((*section_).records[record].stable_id));
    (*index_chooser_).items = std::move(titles);
    (*index_chooser_).visible = true;
    (*index_column_).invalidate(Dirty::layout);
    (*index_chooser_).select_index(0U);
    focus_child(index_chooser_);
    return true;
}

void HelpBookViewer::close_chooser() {
    chooser_.clear();
    if (!index_chooser_ || !(*index_chooser_).visible.read()) return;
    (*index_chooser_).items = std::vector<std::string>{};
    (*index_chooser_).visible = false;
    (*index_column_).invalidate(Dirty::layout);
}

std::vector<std::string> HelpBookViewer::chooser_records() const {
    std::vector<std::string> ids;
    if (!section_) return ids;
    ids.reserve(chooser_.size());
    for (const std::uint32_t record : chooser_) ids.push_back((*section_).records[record].stable_id);
    return ids;
}

bool HelpBookViewer::open_chooser_entry(std::size_t entry) {
    require_mutable();
    if (!section_ || entry >= chooser_.size()) return false;
    const std::string target = (*section_).records[chooser_[entry]].stable_id;
    return show(target);
}

void HelpBookViewer::on_index_activated(std::size_t row) { open_index_row(row); }

void HelpBookViewer::on_chooser_activated(std::size_t entry) { open_chooser_entry(entry); }

// --- Search -----------------------------------------------------------------------------------

void HelpBookViewer::run_search() {
    hits_.clear();
    parse_help_query((*search_field_).text.read(), query_);
    if (section_) {
        HelpSearchOptions options{};
        options.titles_only = titles_only_;
        last_search_ = search_help(*section_, query_, options, hits_);
    } else {
        last_search_ = HelpSearchStatus::empty_query;
    }
    ++searches_;
    std::vector<std::string> titles;
    titles.reserve(hits_.size());
    for (const HelpSearchHit& hit : hits_) titles.push_back(record_title((*section_).records[hit.record].stable_id));
    (*search_list_).items = std::move(titles);
}

std::vector<std::string> HelpBookViewer::search_results() const {
    std::vector<std::string> ids;
    if (!section_) return ids;
    ids.reserve(hits_.size());
    for (const HelpSearchHit& hit : hits_) ids.push_back((*section_).records[hit.record].stable_id);
    return ids;
}

bool HelpBookViewer::open_search_result(std::size_t result) {
    require_mutable();
    if (!section_ || result >= hits_.size()) return false;
    const std::string target = (*section_).records[hits_[result].record].stable_id;
    if (!show(target)) return false;
    // Mark every word the query asked for, phrases included.
    std::vector<std::string> words = query_.words;
    for (const std::vector<std::string>& phrase : query_.phrases) words.insert(words.end(), phrase.begin(), phrase.end());
    (*book_).set_highlight(std::move(words));
    return true;
}

void HelpBookViewer::set_titles_only(bool titles_only) {
    require_mutable();
    if (titles_only_ == titles_only) return;
    titles_only_ = titles_only;
    if ((*titles_only_box_).checked.read() != titles_only) (*titles_only_box_).checked = titles_only;
    run_search();
}

void HelpBookViewer::on_search_text(const std::string& text) {
    static_cast<void>(text);
    run_search();
}

void HelpBookViewer::on_search_committed(const std::string& text) {
    static_cast<void>(text);
    run_search();
    if (!hits_.empty()) (*search_list_).select_index(0U);
}

void HelpBookViewer::on_search_activated(std::size_t result) { open_search_result(result); }

void HelpBookViewer::on_titles_only(bool titles_only) { set_titles_only(titles_only); }

// --- Bookmarks ----------------------------------------------------------------------------------

void HelpBookViewer::set_bookmarks(std::vector<std::string> stable_ids) {
    require_mutable();
    bookmarks_ = std::move(stable_ids);
    refresh_bookmark_list();
    refresh_buttons();
}

void HelpBookViewer::refresh_bookmark_list() {
    std::vector<std::string> titles;
    titles.reserve(bookmarks_.size());
    for (const std::string& bookmark : bookmarks_) titles.push_back(record_title(bookmark));
    (*bookmarks_list_).items = std::move(titles);
}

bool HelpBookViewer::add_bookmark() {
    require_mutable();
    const std::string page = current_page();
    if (page.empty() || std::find(bookmarks_.begin(), bookmarks_.end(), page) != bookmarks_.end()) return false;
    bookmarks_.push_back(page);
    refresh_bookmark_list();
    refresh_buttons();
    bookmarks_changed_.emit(bookmarks_);
    return true;
}

bool HelpBookViewer::remove_selected_bookmark() {
    require_mutable();
    const std::optional<std::size_t> selected = (*bookmarks_list_).selected_index();
    if (!selected.has_value() || *selected >= bookmarks_.size()) return false;
    bookmarks_.erase(bookmarks_.begin() + static_cast<std::ptrdiff_t>(*selected));
    refresh_bookmark_list();
    refresh_buttons();
    bookmarks_changed_.emit(bookmarks_);
    return true;
}

void HelpBookViewer::on_bookmark_activated(std::size_t entry) {
    if (entry >= bookmarks_.size()) return;
    const std::string target = bookmarks_[entry];
    show(target);
}

void HelpBookViewer::on_add_bookmark_clicked(ButtonBase& button) {
    static_cast<void>(button);
    add_bookmark();
}

void HelpBookViewer::on_remove_bookmark_clicked(ButtonBase& button) {
    static_cast<void>(button);
    remove_selected_bookmark();
}

// --- Toolbar --------------------------------------------------------------------------------------

void HelpBookViewer::on_back_clicked(ButtonBase& button) {
    static_cast<void>(button);
    back();
}

void HelpBookViewer::on_forward_clicked(ButtonBase& button) {
    static_cast<void>(button);
    forward();
}

void HelpBookViewer::on_home_clicked(ButtonBase& button) {
    static_cast<void>(button);
    home();
}

void HelpBookViewer::on_print_clicked(ButtonBase& button) {
    static_cast<void>(button);
    HelpPrintRequest request{};
    request.page = current_page();
    if (request.page.empty()) return;
    print_requested_.emit(request);
}

void HelpBookViewer::on_hide_clicked(ButtonBase& button) {
    static_cast<void>(button);
    set_navigation_visible(!navigation_visible_);
}

void HelpBookViewer::on_locate_clicked(ButtonBase& button) {
    static_cast<void>(button);
    show_contents();
}

void HelpBookViewer::on_language_clicked(ButtonBase& button) {
    static_cast<void>(button);
    built_in_language_requested_.emit();
}

void HelpBookViewer::refresh_buttons() {
    if (!back_button_) return;
    const bool has_page = !current_page().empty();
    (*back_button_).enabled = can_go_back();
    (*forward_button_).enabled = can_go_forward();
    (*home_button_).enabled = !home_page().empty();
    (*print_button_).enabled = has_page;
    (*locate_button_).enabled = has_page;
    (*add_bookmark_button_).enabled = has_page;
    (*remove_bookmark_button_).enabled = !bookmarks_.empty();
    (*hide_button_).text = navigation_visible_ ? labels_.hide : labels_.show;
    (*language_button_).visible = !labels_.built_in_language.empty();
}

void HelpBookViewer::apply_labels() {
    if (!back_button_) return;
    (*back_button_).text = labels_.back;
    (*forward_button_).text = labels_.forward;
    (*home_button_).text = labels_.home;
    (*print_button_).text = labels_.print;
    (*locate_button_).text = labels_.locate;
    (*language_button_).text = labels_.built_in_language;
    (*contents_page_).text = labels_.contents;
    (*index_page_).text = labels_.index;
    (*search_page_).text = labels_.search;
    (*bookmarks_page_).text = labels_.bookmarks;
    (*add_bookmark_button_).text = labels_.add;
    (*remove_bookmark_button_).text = labels_.remove;
    (*titles_only_box_).text = labels_.titles_only;
    (*index_field_).placeholder_text = labels_.index_placeholder;
    (*search_field_).placeholder_text = labels_.search_placeholder;
    (*contents_tree_).accessible_name = labels_.contents;
    (*index_list_).accessible_name = labels_.index;
    (*search_list_).accessible_name = labels_.search;
    (*bookmarks_list_).accessible_name = labels_.bookmarks;
    refresh_buttons();
    invalidate(Dirty::layout);
}

void HelpBookViewer::set_labels(HelpBookViewerLabels labels) {
    require_mutable();
    labels_ = std::move(labels);
    apply_labels();
}

void HelpBookViewer::set_images(std::vector<HelpBookImage> images) {
    require_mutable();
    (*book_).set_images(std::move(images));
}

void HelpBookViewer::set_navigation_visible(bool visible) {
    require_mutable();
    if (navigation_visible_ == visible) return;
    navigation_visible_ = visible;
    (*tabs_).visible = visible;
    refresh_buttons();
    invalidate(Dirty::layout);
}

void HelpBookViewer::set_navigation_width(double width) {
    require_mutable();
    const double clamped = std::clamp(width, minimum_navigation_width, maximum_navigation_width);
    if (clamped == navigation_width_) return;
    navigation_width_ = clamped;
    invalidate(Dirty::layout);
}

// --- Links ----------------------------------------------------------------------------------------

void HelpBookViewer::on_link(HelpLinkActivation& activation) {
    switch (activation.kind) {
    case HelpLinkKind::topic:
        if (activation.target.empty() || activation.target == current_page()) {
            activation.handled = (*book_).scroll_to_anchor(activation.fragment);
            return;
        }
        (*book_).mark_visited(activation.target);
        activation.handled = show(activation.target, activation.fragment);
        return;
    case HelpLinkKind::popup:
        (*book_).mark_visited(activation.target);
        popup_requested_.emit(activation);
        return;
    case HelpLinkKind::external:
        external_link_.emit(activation);
        return;
    case HelpLinkKind::image:
        return;
    }
}

// --- Focus and keys -------------------------------------------------------------------------------

void HelpBookViewer::focus_child(const Control::Ptr& child) {
    SurfaceRoot* const root = surface_root();
    if (root == nullptr || !child) return;
    (*root).request_focus(child);
}

bool HelpBookViewer::book_holds_focus() const {
    SurfaceRoot* const root = surface_root();
    if (root == nullptr) return false;
    Control::Ptr control = (*root).focused_control();
    // The focused control's ancestors, bounded by the tree's depth.
    std::size_t steps = 0U;
    while (control && steps < 256U) {
        if (control.get() == book_.get()) return true;
        control = (*control).parent();
        ++steps;
    }
    return false;
}

void HelpBookViewer::on_key_preview(KeyEvent& event) {
    Control::on_key_preview(event);
    if (event.handled || event.action != KeyAction::down) return;
    const std::uint32_t key = event.physical_key;
    if (event.modifiers == Modifier::alt) {
        if (key == PhysicalKey::c) {
            show_contents();
        } else if (key == PhysicalKey::n) {
            select_tab(HelpNavigationTab::index);
            focus_child(index_field_);
        } else if (key == PhysicalKey::s) {
            select_tab(HelpNavigationTab::search);
            focus_child(search_field_);
        } else if (key == PhysicalKey::i) {
            select_tab(HelpNavigationTab::bookmarks);
            focus_child(bookmarks_list_);
        } else if (key == PhysicalKey::left) {
            back();
        } else if (key == PhysicalKey::right) {
            forward();
        } else if (key == PhysicalKey::home) {
            home();
        } else {
            return;
        }
        event.handled = true;
        return;
    }
    if (event.modifiers == Modifier::control && key == PhysicalKey::p) {
        HelpPrintRequest request{};
        request.page = current_page();
        if (!request.page.empty()) print_requested_.emit(request);
        event.handled = true;
        return;
    }
    if (event.modifiers == Modifier::none && key == PhysicalKey::f6) {
        if (book_holds_focus() && navigation_visible_) {
            switch (selected_tab()) {
            case HelpNavigationTab::contents:
                focus_child(contents_tree_);
                break;
            case HelpNavigationTab::index:
                focus_child(index_field_);
                break;
            case HelpNavigationTab::search:
                focus_child(search_field_);
                break;
            case HelpNavigationTab::bookmarks:
                focus_child(bookmarks_list_);
                break;
            }
        } else {
            focus_child(book_);
        }
        event.handled = true;
    }
}

// --- Layout -----------------------------------------------------------------------------------------

Size HelpBookViewer::measure(Size available) {
    const double width = std::isfinite(available.width) ? std::min(available.width, preferred_width) : preferred_width;
    const double height =
        std::isfinite(available.height) ? std::min(available.height, preferred_height) : preferred_height;
    return Size{width, height};
}

void HelpBookViewer::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    if (!book_) return;
    const double unbounded = std::numeric_limits<double>::infinity();
    // The toolbar: the commands from the left, the way back at the right.
    const std::array<Control::Ptr, 6> commands{hide_button_, back_button_, forward_button_,
                                               home_button_, print_button_, locate_button_};
    const double button_top = (toolbar_height - toolbar_button_height) / 2.0;
    double left = viewer_margin;
    for (const Control::Ptr& command : commands) {
        const Size wanted = (*command).measure(Size{unbounded, toolbar_button_height});
        const double width = std::max(wanted.width, toolbar_button_minimum);
        set_child_layout(command, Rect{left, button_top, width, toolbar_button_height});
        left += width + viewer_gap;
    }
    if ((*language_button_).visible.read()) {
        const Size wanted = (*language_button_).measure(Size{unbounded, toolbar_button_height});
        const double width = std::max(wanted.width, toolbar_button_minimum);
        const double right = std::max(left, final_bounds.width - viewer_margin - width);
        set_child_layout(language_button_, Rect{right, button_top, width, toolbar_button_height});
    }
    // The navigation pane and the book share the rest.
    const double top = toolbar_height;
    const double height = std::max(0.0, final_bounds.height - top - viewer_margin);
    double book_left = viewer_margin;
    if (navigation_visible_) {
        const double share = std::max(0.0, final_bounds.width * maximum_navigation_share);
        const double width = std::min(navigation_width_, share);
        set_child_layout(tabs_, Rect{viewer_margin, top, width, height});
        book_left = viewer_margin + width + viewer_gap;
    }
    const double book_width = std::max(0.0, final_bounds.width - book_left - viewer_margin);
    set_child_layout(book_, Rect{book_left, top, book_width, height});
}

// --- Description ----------------------------------------------------------------------------------------

HelpBookViewerSnapshot HelpBookViewer::snapshot() const noexcept {
    HelpBookViewerSnapshot snapshot{};
    snapshot.history_size = history_.size();
    snapshot.history_position = history_position_;
    snapshot.navigations = navigations_;
    snapshot.searches = searches_;
    snapshot.last_search = last_search_;
    snapshot.last_hit_count = hits_.size();
    return snapshot;
}

SemanticDescriptor HelpBookViewer::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::pane;
    descriptor.name = accessible_name.read().empty() ? std::string("Help") : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = true;
    return descriptor;
}

void HelpBookViewer::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), help_book_viewer_interactions.begin(), help_book_viewer_interactions.end());
}

} // namespace gui_forms
