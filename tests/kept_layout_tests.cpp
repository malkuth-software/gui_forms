// Kept layouts (S5). ListView, TreeListView, NavigationPane, ToolStrip and
// Ribbon keep their layout under a key (Control::LayoutKey) and make it again
// only when the key moves. Each scenario below writes every field those
// layouts read, through the controls' own operations, and after each write
// reads the window's semantics (which every layout-derived bound reaches).
// The same scenario is run twice, once with kept layouts and once with every
// layout made afresh (detail::set_layout_keeping(false)); the two runs must
// read the same thing after every step.

#include "gui_forms/gui_forms.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_kept_layout_tests: " << message << '\n';
    std::exit(1);
}

// What the window publishes, without the snapshot's own generation count.
std::string observed(Window& window) {
    window.perform_layout();
    SemanticSnapshot snapshot = window.semantic_snapshot();
    snapshot.generation = 0U;
    const std::string text = snapshot.to_json();
    return text;
}

void compare(const std::vector<std::string>& kept, const std::vector<std::string>& fresh, const char* control) {
    require(kept.size() == fresh.size(), "both runs take the same steps");
    for (std::size_t step = 0; step < kept.size(); ++step) {
        if (kept[step] == fresh[step]) continue;
        std::cerr << control << ": step " << step << " differs\n";
        require(false, "a kept layout reads as a fresh one after every write");
    }
}

// -- ListView ----------------------------------------------------------------

std::vector<ListViewColumn> list_columns(double first) {
    std::vector<ListViewColumn> columns;
    columns.push_back({.stable_id = "name", .text = "Name", .width = first});
    columns.push_back({.stable_id = "size", .text = "Size", .width = 80.0});
    columns.push_back({.stable_id = "kind", .text = "Kind", .width = 100.0});
    return columns;
}

std::vector<ListViewItem> list_items(std::size_t count, std::size_t groups) {
    std::vector<ListViewItem> items;
    for (std::size_t index = 0; index < count; ++index) {
        ListViewItem item;
        item.stable_id = "item-" + std::to_string(index);
        item.text = "Item " + std::to_string(count - index);
        item.sub_items = {std::to_string(index) + " KB", "Text"};
        if (groups != 0U) item.group = "group-" + std::to_string(index % groups);
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<std::string> list_view_run(bool keeping) {
    detail::set_layout_keeping(keeping);
    std::vector<std::string> seen;
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("list"));
    (*view).set_columns(list_columns(140.0));
    (*view).set_items(list_items(40U, 0U));
    Window window(view, {360.0, 200.0});
    seen.push_back(observed(window));
    (*view).set_top_line(7U);
    seen.push_back(observed(window));
    (*view).scroll_lines(3);
    seen.push_back(observed(window));
    (*view).set_horizontal_offset(60.0);
    seen.push_back(observed(window));
    (*view).set_column_width(0U, 220.0);
    seen.push_back(observed(window));
    (*view).set_columns(list_columns(90.0));
    seen.push_back(observed(window));
    (*view).sort_items(0U, ListViewSortOrder::ascending);
    seen.push_back(observed(window));
    std::vector<ListViewGroup> groups;
    groups.push_back({.stable_id = "group-0", .header = "Even"});
    groups.push_back({.stable_id = "group-1", .header = "Odd"});
    (*view).set_groups(std::move(groups));
    (*view).set_items(list_items(30U, 2U));
    seen.push_back(observed(window));
    (*view).set_top_line(4U);
    seen.push_back(observed(window));
    (*view).view = ListViewView::large_icon;
    seen.push_back(observed(window));
    (*view).view = ListViewView::list;
    seen.push_back(observed(window));
    (*view).set_items(list_items(5U, 0U));
    seen.push_back(observed(window));
    window.resize({240.0, 160.0});
    seen.push_back(observed(window));
    detail::set_layout_keeping(true);
    return seen;
}

// -- TreeListView ------------------------------------------------------------

std::vector<TreeListColumn> tree_columns(double first) {
    std::vector<TreeListColumn> columns;
    columns.push_back({.stable_id = "name", .text = "Name", .width = first});
    columns.push_back({.stable_id = "kind", .text = "Kind", .width = 90.0});
    return columns;
}

std::vector<TreeListNode> tree_nodes(std::size_t roots) {
    std::vector<TreeListNode> nodes;
    for (std::size_t root = 0; root < roots; ++root) {
        TreeListNode parent;
        parent.stable_id = "root-" + std::to_string(root);
        parent.text = "Folder " + std::to_string(root);
        parent.cells = {"Folder"};
        parent.expanded = root % 2U == 0U;
        nodes.push_back(std::move(parent));
        for (std::size_t child = 0; child < 3U; ++child) {
            TreeListNode leaf;
            leaf.stable_id = "leaf-" + std::to_string(root) + "-" + std::to_string(child);
            leaf.text = "File " + std::to_string(child);
            leaf.depth = 1U;
            leaf.cells = {"File"};
            nodes.push_back(std::move(leaf));
        }
    }
    return nodes;
}

std::vector<std::string> tree_list_view_run(bool keeping) {
    detail::set_layout_keeping(keeping);
    std::vector<std::string> seen;
    std::shared_ptr<TreeListView> view = make_control<TreeListView>(StableId("tree"));
    (*view).set_columns(tree_columns(180.0));
    (*view).set_nodes(tree_nodes(12U));
    Window window(view, {320.0, 180.0});
    seen.push_back(observed(window));
    (*view).set_top_row(5U);
    seen.push_back(observed(window));
    (*view).set_expanded("root-1", true);
    seen.push_back(observed(window));
    (*view).set_expanded("root-0", false);
    seen.push_back(observed(window));
    (*view).set_horizontal_offset(40.0);
    seen.push_back(observed(window));
    (*view).set_column_width(0U, 260.0);
    seen.push_back(observed(window));
    (*view).set_columns(tree_columns(120.0));
    seen.push_back(observed(window));
    (*view).set_nodes(tree_nodes(3U));
    seen.push_back(observed(window));
    window.resize({200.0, 120.0});
    seen.push_back(observed(window));
    detail::set_layout_keeping(true);
    return seen;
}

// -- NavigationPane ----------------------------------------------------------

std::vector<std::string> navigation_pane_run(bool keeping) {
    detail::set_layout_keeping(keeping);
    std::vector<std::string> seen;
    std::shared_ptr<NavigationPane> pane = make_control<NavigationPane>(StableId("nav"));
    PageSet& pages = (*pane).pages();
    const char* const ids[] = {"mail", "calendar", "contacts", "tasks", "notes", "folders"};
    for (const char* id : ids) {
        const PageRecord record{.id = PageId{std::string(id)}, .title = std::string(id)};
        require(pages.insert(record, PagePlacement::end_of_group()) == PageEditStatus::applied, "a page enters the set");
    }
    Window window(pane, {220.0, 420.0});
    seen.push_back(observed(window));
    (*pane).button_count = 2U;
    seen.push_back(observed(window));
    require((*pane).set_page_hidden(PageId{"contacts"}, true), "a page hides");
    seen.push_back(observed(window));
    (*pane).collapsed = true;
    seen.push_back(observed(window));
    (*pane).collapsed = false;
    seen.push_back(observed(window));
    const PageRecord extra{.id = PageId{"journal"}, .title = "journal"};
    require(pages.insert(extra, PagePlacement::end_of_group()) == PageEditStatus::applied, "a page joins");
    seen.push_back(observed(window));
    NavigationPaneState state{};
    state.collapsed = false;
    state.button_count = 4U;
    static_cast<void>((*pane).apply_state(state));
    seen.push_back(observed(window));
    require((*pane).set_page_hidden(PageId{"contacts"}, false), "a page shows again");
    seen.push_back(observed(window));
    window.resize({220.0, 260.0});
    seen.push_back(observed(window));
    detail::set_layout_keeping(true);
    return seen;
}

// -- ToolStrip ---------------------------------------------------------------

std::vector<ToolStripItemSpec> strip_items(std::size_t count) {
    std::vector<ToolStripItemSpec> items;
    for (std::size_t index = 0; index < count; ++index) {
        items.push_back({.stable_id = "item-" + std::to_string(index), .text = "Command " + std::to_string(index)});
    }
    return items;
}

std::vector<std::string> tool_strip_run(bool keeping) {
    detail::set_layout_keeping(keeping);
    std::vector<std::string> seen;
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("strip"));
    (*strip).set_items(strip_items(4U));
    Window window(strip, {420.0, 60.0});
    seen.push_back(observed(window));
    (*strip).set_item_text("item-1", "A much longer command label");
    seen.push_back(observed(window));
    (*strip).set_items(strip_items(9U));
    seen.push_back(observed(window));
    window.resize({260.0, 60.0});
    seen.push_back(observed(window));
    (*strip).set_item_text("item-0", "X");
    seen.push_back(observed(window));
    detail::set_layout_keeping(true);
    return seen;
}

// -- Ribbon ------------------------------------------------------------------

std::vector<RibbonTabSpec> ribbon_tabs(const std::shared_ptr<Command>& paste, bool wide) {
    std::vector<RibbonTabSpec> tabs;
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard"};
    clipboard.commands.push_back({.stable_id = "paste", .command = paste, .image = ImageId{5}});
    clipboard.commands.push_back({.stable_id = "cut", .size = RibbonCommandSize::small, .text = "Cut"});
    home.groups.push_back(std::move(clipboard));
    RibbonGroupSpec font{.stable_id = "font", .caption = wide ? "Font and paragraph" : "Font"};
    font.commands.push_back({.stable_id = "strike", .kind = RibbonCommandKind::check_box, .text = "Strike"});
    home.groups.push_back(std::move(font));
    RibbonTabSpec insert{.stable_id = "insert", .text = "&Insert"};
    RibbonGroupSpec tables{.stable_id = "tables", .caption = "Tables"};
    tables.commands.push_back({.stable_id = "find", .kind = RibbonCommandKind::text_box, .text = "Find"});
    insert.groups.push_back(std::move(tables));
    tabs.push_back(std::move(home));
    tabs.push_back(std::move(insert));
    return tabs;
}

std::vector<std::string> ribbon_run(bool keeping) {
    detail::set_layout_keeping(keeping);
    std::vector<std::string> seen;
    const std::shared_ptr<Command> paste = std::make_shared<Command>("paste", "Paste");
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(ribbon_tabs(paste, false));
    Window window(ribbon, {700.0, 300.0});
    seen.push_back(observed(window));
    const std::vector<Ribbon::Tab> tabs = (*ribbon).realized_tabs();
    require(tabs.size() == 2U, "both tabs are shown");
    const Rect second = tabs[1].parts.tab.bounds;
    const Point inside{second.x + second.width / 2.0, second.y + second.height / 2.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, inside}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, inside}));
    seen.push_back(observed(window));
    (*ribbon).set_tabs(ribbon_tabs(paste, true));
    seen.push_back(observed(window));
    window.resize({260.0, 300.0});
    seen.push_back(observed(window));
    window.resize({700.0, 90.0});
    seen.push_back(observed(window));
    window.resize({700.0, 300.0});
    seen.push_back(observed(window));
    detail::set_layout_keeping(true);
    return seen;
}

} // namespace

int main() {
    compare(list_view_run(true), list_view_run(false), "ListView");
    compare(tree_list_view_run(true), tree_list_view_run(false), "TreeListView");
    compare(navigation_pane_run(true), navigation_pane_run(false), "NavigationPane");
    compare(tool_strip_run(true), tool_strip_run(false), "ToolStrip");
    compare(ribbon_run(true), ribbon_run(false), "Ribbon");
    std::cout << "gui_forms_kept_layout_tests: passed\n";
    return 0;
}
