// TreeListView (stage W): a tree with tabular columns. Its published parts
// and states, expansion over virtualized rows, column layout, resizing and
// moving, sorting within siblings, keyboard traces, selection kept by node
// ID, semantics, the realization bound and refusals.

#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override { texts.emplace_back(text); }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> texts;
};

bool painted(const RecordingPainter& painter, std::string_view text) {
    for (const std::string& seen : painter.texts) {
        if (seen == text) return true;
    }
    return false;
}

struct RecordSelection final {
    std::vector<std::string>* seen{};
    void operator()(const TreeListSelectionChange& change) const {
        (*seen).push_back(change.current_id.value_or(std::string("(none)")));
    }
};

struct RecordExpansion final {
    std::vector<std::string>* seen{};
    void operator()(const TreeListExpansionChange& change) const {
        (*seen).push_back((change.expanded ? "+" : "-") + change.node_id);
    }
};

struct RecordActivation final {
    std::vector<std::string>* seen{};
    void operator()(const TreeListActivation& activation) const { (*seen).push_back(activation.node_id); }
};

struct RecordSort final {
    std::vector<TreeListSortChange>* seen{};
    void operator()(const TreeListSortChange& change) const { (*seen).push_back(change); }
};

struct RecordWidth final {
    std::vector<double>* seen{};
    void operator()(const TreeListColumnWidthChange& change) const { (*seen).push_back(change.width); }
};

struct RecordMove final {
    std::vector<TreeListColumnMove>* seen{};
    void operator()(const TreeListColumnMove& change) const { (*seen).push_back(change); }
};

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

Point to_window(const TreeListView& view, Point local) {
    const Rect absolute = view.absolute_bounds();
    const Point point{absolute.x + local.x, absolute.y + local.y};
    return point;
}

void click(Window& window, Point point, std::uint32_t clicks = 1U) {
    PointerEvent down{PointerAction::down, PointerButton::primary, point};
    down.click_count = clicks;
    static_cast<void>(window.dispatch_pointer(down));
    PointerEvent up{PointerAction::up, PointerButton::primary, point};
    static_cast<void>(window.dispatch_pointer(up));
}

bool key(Window& window, std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, physical};
    event.modifiers = modifiers;
    const bool handled = window.dispatch_key(event);
    return handled;
}

bool type(Window& window, std::string_view text) {
    TextInputEvent typed{};
    typed.text_utf8 = std::string(text);
    const bool handled = window.dispatch_text(typed);
    return handled;
}

std::vector<TreeListColumn> sample_columns() {
    std::vector<TreeListColumn> columns;
    columns.push_back({.stable_id = "name", .text = "Name", .width = 180.0});
    columns.push_back({.stable_id = "kind", .text = "Kind", .width = 90.0});
    columns.push_back({.stable_id = "size", .text = "Size", .width = 70.0, .alignment = TreeListAlignment::right});
    return columns;
}

TreeListNode node(std::string id, std::string text, std::size_t depth, std::string kind, std::string size,
                  bool expanded = false) {
    TreeListNode result;
    result.stable_id = std::move(id);
    result.text = std::move(text);
    result.depth = depth;
    result.cells = {std::move(kind), std::move(size)};
    result.expanded = expanded;
    return result;
}

// Two roots. "src" is expanded, with its folder "lib" expanded; "docs" is
// collapsed. In model order the children of src are not sorted.
std::vector<TreeListNode> sample_nodes() {
    std::vector<TreeListNode> nodes;
    nodes.push_back(node("src", "Sources", 0U, "Folder", "", true));
    nodes.push_back(node("beta", "beta.cpp", 1U, "C++", "12"));
    nodes.push_back(node("lib", "Alpha", 1U, "Folder", "", true));
    nodes.push_back(node("zeta", "zeta.h", 2U, "Header", "3"));
    nodes.push_back(node("eta", "eta.h", 2U, "Header", "5"));
    nodes.push_back(node("delta", "delta.txt", 1U, "Text", "1"));
    nodes.push_back(node("docs", "Docs", 0U, "Folder", ""));
    nodes.push_back(node("manual", "manual.md", 1U, "Markdown", "7"));
    return nodes;
}

std::shared_ptr<TreeListView> sample_view() {
    std::shared_ptr<TreeListView> view = make_control<TreeListView>(StableId("tree"));
    (*view).set_columns(sample_columns());
    (*view).set_nodes(sample_nodes());
    return view;
}

std::vector<std::string> row_ids(const TreeListView& view) {
    std::vector<std::string> ids;
    for (std::size_t row = 0; row < view.row_count(); ++row) ids.emplace_back(view.row_node_id(row));
    return ids;
}

// The returned row borrows from `rows`, so the vector must outlive it:
// realized_rows() returns a new vector, and a reference into that temporary
// dangles at the end of the statement. The deleted overload refuses one.
const TreeListView::Row& row_named(const std::vector<TreeListView::Row>& rows, std::string_view id) {
    for (const TreeListView::Row& row : rows) {
        if (row.stable_id == id) return row;
    }
    throw std::runtime_error("row is not realized");
}
const TreeListView::Row& row_named(std::vector<TreeListView::Row>&& rows, std::string_view id) = delete;

void parts_states_and_paint() {
    std::shared_ptr<TreeListView> view = sample_view();
    Window window(view, {420.0, 260.0});
    window.perform_layout();
    const ControlAnatomy& anatomy = (*view).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                anatomy.control_id == "tree_list_view" && anatomy.item_systems.size() == 3U &&
                find_item_system(anatomy, "row").has_value() && find_item_system(anatomy, "cell").has_value() &&
                find_item_system(anatomy, "column").has_value() &&
                find_item_part(anatomy.item_systems[0], "expander").has_value(),
            "TreeListView publishes a valid anatomy with row, cell and column systems");
    const TreeListView::Parts parts = (*view).parts();
    require(parts.well.present && parts.header.present && !parts.vertical_scroll_bar.present,
            "a short tree shows its well and header and no scroll bar");
    require(row_ids(*view) == std::vector<std::string>({"src", "beta", "lib", "zeta", "eta", "delta", "docs"}),
            "the rows are the nodes in model order without those under a collapsed node");
    const std::vector<TreeListView::Row> rows = (*view).realized_rows();
    require(rows.size() == 7U, "every row of a short tree is realized");
    const TreeListView::Row& src = row_named(rows, "src");
    const TreeListView::Row& lib = row_named(rows, "lib");
    const TreeListView::Row& zeta = row_named(rows, "zeta");
    const TreeListView::Row& docs = row_named(rows, "docs");
    require(src.parts.expander.present && src.states.open && !src.states.closed && docs.parts.expander.present &&
                docs.states.closed && !zeta.parts.expander.present && !zeta.states.open && !zeta.states.closed,
            "nodes with children publish an expander and open or closed; leaves publish neither");
    require(lib.parts.text.bounds.x > src.parts.text.bounds.x && zeta.parts.text.bounds.x > lib.parts.text.bounds.x,
            "the tree column indents each level");
    require(row_named(rows, "beta").parts.text.bounds.x == lib.parts.text.bounds.x,
            "a leaf's text lines up with its sibling folder's: the expander slot is kept");
    require(lib.parts.expander.bounds.x + lib.parts.expander.bounds.width <= lib.parts.text.bounds.x &&
                src.parts.row_face.bounds.width > 300.0,
            "the expander sits before the text and the face spans the whole row");
    const std::vector<TreeListView::Cell> cells = (*view).realized_cells();
    require(cells.size() == 14U && cells[0].column == 1U && cells[1].column == 2U &&
                cells[0].stable_id == "src.cell.kind" && cells[3].stable_id == "beta.cell.size",
            "each row realizes one cell per visible column after the tree column");
    const std::vector<TreeListView::Column> columns = (*view).realized_columns();
    require(columns.size() == 3U && columns[1].stable_id == "tree.column.kind" && columns[1].parts.column_label.present &&
                columns[1].parts.column_divider.present && !columns[1].parts.sort_glyph.present,
            "each visible column publishes its header parts; no sort glyph unsorted");
    require((*view).realized_item_count() == 7U + 14U + 3U && (*view).realized_item_system(0U).value == 0U &&
                (*view).realized_item_system(7U).value == 1U && (*view).realized_item_system(21U).value == 2U,
            "the generic path lists rows, then cells, then columns");
    require((*view).realized_item_stable_id(0U) == "src" && (*view).realized_item_stable_id(7U) == "src.cell.kind" &&
                (*view).realized_item_stable_id(23U) == "tree.column.size",
            "realized items name their nodes, cells and columns");

    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 420.0, 260.0}));
    require(painted(painter, "Name") && painted(painter, "Sources") && painted(painter, "zeta.h") &&
                painted(painter, "Header") && painted(painter, "12"),
            "the view paints headers, node text and cells");
    require(!painted(painter, "manual.md") && !painted(painter, "Markdown"),
            "a node under a collapsed node is neither realized nor painted");
}

// A large tree: only the rows in view are realized, painted and described,
// and expansion changes the row count without realizing the rest.
void expansion_with_virtualization() {
    std::vector<TreeListNode> nodes;
    nodes.reserve(101'000U);
    for (std::size_t root = 0; root < 1000U; ++root) {
        nodes.push_back(node("r" + std::to_string(root), "Root " + std::to_string(root), 0U, "Folder", ""));
        for (std::size_t child = 0; child < 100U; ++child) {
            nodes.push_back(node("r" + std::to_string(root) + "c" + std::to_string(child),
                                 "Child " + std::to_string(child), 1U, "File", std::to_string(child)));
        }
    }
    std::shared_ptr<TreeListView> view = make_control<TreeListView>(StableId("big"));
    (*view).set_columns(sample_columns());
    (*view).set_nodes(std::move(nodes));
    Window window(view, {420.0, 260.0});
    window.perform_layout();
    std::vector<std::string> expansions;
    SubscriptionToken expanded = (*view).expansion_changed().subscribe(RecordExpansion{&expansions});
    require((*view).nodes().size() == 101'000U && (*view).row_count() == 1000U,
            "a collapsed tree of 101,000 nodes shows its 1,000 roots as rows");
    const std::size_t realized = (*view).realized_rows().size();
    // The bound: the rows a 260-unit window can show at least partly.
    const double row_height = (*view).realized_rows()[0].parts.row.bounds.height;
    const std::size_t screenful = static_cast<std::size_t>(260.0 / row_height) + 1U;
    require(realized > 0U && realized <= screenful && (*view).parts().vertical_scroll_bar.present,
            "only a screenful of rows is realized, with a scroll bar");
    require((*view).realized_item_count() <= realized * 3U + 3U && (*view).semantic_virtual_children().size() <= realized + 3U,
            "the anatomy and semantics realize the visible rows, cells and headers only");
    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 420.0, 260.0}));
    require(painter.texts.size() <= realized * 3U + 3U, "painting draws text for the visible rows and headers only");

    (*view).set_expanded("r0", true);
    require((*view).row_count() == 1100U && (*view).row_node_id(1U) == "r0c0" && (*view).row_node_id(101U) == "r1" &&
                expansions == std::vector<std::string>({"+r0"}),
            "expanding a root inserts its children as rows and raises expansion_changed");
    require((*view).realized_rows().size() == realized, "expansion does not grow the realized rows");
    (*view).set_top_row(1050U);
    require((*view).top_row() == 1050U && (*view).realized_rows().front().stable_id == "r950",
            "scrolling realizes the rows at the new top only");
    (*view).ensure_visible("r500c42");
    require((*view).is_expanded("r500") && (*view).row_of("r500c42").has_value() &&
                (*view).realized_rows().front().stable_id == "r500c42" && expansions.back() == "+r500",
            "ensure_visible expands the ancestors and scrolls the node into view");
    (*view).set_expanded("r0", false);
    (*view).set_expanded("r500", false);
    require((*view).row_count() == 1000U && !(*view).row_of("r500c42").has_value(),
            "collapsing hides the children again; their nodes stay in the model");
    (*view).expand_subtree("r7");
    require((*view).row_count() == 1100U && expansions.back() == "+r7",
            "expand_subtree expands the node and its expandable descendants");
}

void sort_within_siblings() {
    std::shared_ptr<TreeListView> view = sample_view();
    (*view).set_expanded("docs", true);
    Window window(view, {420.0, 260.0});
    window.perform_layout();
    std::vector<TreeListSortChange> sorts;
    SubscriptionToken sorted = (*view).sort_changed().subscribe(RecordSort{&sorts});
    (*view).selected_id = "zeta";
    const std::optional<std::size_t> zeta_before = (*view).row_of("zeta");

    (*view).set_sort(std::size_t{0U}, TreeListSortOrder::ascending);
    require(row_ids(*view) ==
                std::vector<std::string>({"docs", "manual", "src", "lib", "eta", "zeta", "beta", "delta"}),
            "an ascending sort orders each node's children by text, folding ASCII case, and keeps the tree");
    require(sorts.size() == 1U && sorts[0].column == 0U && sorts[0].order == TreeListSortOrder::ascending &&
                (*view).realized_columns()[0].states.sorted_ascending &&
                (*view).realized_columns()[0].parts.sort_glyph.present,
            "the sort raises sort_changed and shows a glyph and a custom state on its column");
    require((*view).selected_id.read() == std::optional<std::string_view>("zeta") && (*view).row_of("zeta") != zeta_before,
            "the selection stays on its node while its row moves");
    require((*view).nodes()[0].stable_id == "src", "sorting never reorders the model");

    (*view).set_sort(std::size_t{2U}, TreeListSortOrder::descending);
    require(row_ids(*view) ==
                std::vector<std::string>({"src", "beta", "delta", "lib", "eta", "zeta", "docs", "manual"}),
            "a descending sort by a cell column orders siblings by that column's text");
    (*view).set_cell_text("zeta", 2U, "9");
    require((*view).row_of("zeta") < (*view).row_of("eta"),
            "an edit to the sort column sorts the siblings again");
    (*view).set_sort(std::nullopt, TreeListSortOrder::ascending);
    require(row_ids(*view) ==
                std::vector<std::string>({"src", "beta", "lib", "zeta", "eta", "delta", "docs", "manual"}) &&
                !(*view).sort_column().has_value() && (*view).sort_order() == TreeListSortOrder::none,
            "no sort column returns to the model order");

    // The header: a click sorts ascending, a second click descending.
    std::vector<TreeListView::Column> columns = (*view).realized_columns();
    click(window, to_window(*view, centre(columns[1].parts.column_label.bounds)));
    require((*view).sort_column() == 1U && (*view).sort_order() == TreeListSortOrder::ascending,
            "a header click sorts ascending by that column");
    columns = (*view).realized_columns();
    click(window, to_window(*view, centre(columns[1].parts.column_label.bounds)));
    require((*view).sort_column() == 1U && (*view).sort_order() == TreeListSortOrder::descending &&
                sorts.size() == 5U,
            "a second click on the same header sorts descending");
}

void column_layout_resize_and_move() {
    std::shared_ptr<TreeListView> view = sample_view();
    Window window(view, {420.0, 260.0});
    window.perform_layout();
    std::vector<double> widths;
    SubscriptionToken resized = (*view).column_width_changed().subscribe(RecordWidth{&widths});
    std::vector<TreeListColumnMove> moves;
    SubscriptionToken moved = (*view).column_moved().subscribe(RecordMove{&moves});

    std::vector<TreeListView::Column> columns = (*view).realized_columns();
    require(columns[0].parts.column_header.bounds.x < columns[1].parts.column_header.bounds.x &&
                columns[1].parts.column_header.bounds.x < columns[2].parts.column_header.bounds.x,
            "columns are laid out left to right in display order");
    const Point start = to_window(*view, centre(columns[0].parts.column_grip.bounds));
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, start}) &&
                (*view).realized_columns()[0].states.captured,
            "the divider takes the press and its column is captured");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::primary, {start.x + 30.0, start.y}}));
    require((*view).columns()[0].width == 210.0 && widths.empty(), "dragging the divider resizes live");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, {start.x + 30.0, start.y}}));
    require(widths.size() == 1U && widths[0] == 210.0 && !(*view).sort_column().has_value(),
            "releasing the divider reports the width once and does not sort");
    const std::vector<TreeListView::Row> rows = (*view).realized_rows();
    require(rows[0].parts.text.bounds.x + rows[0].parts.text.bounds.width <= 2.0 + 210.0 + 0.001,
            "the tree column's text stays inside the resized column");

    // Dragging the Size header left of Kind moves it.
    columns = (*view).realized_columns();
    const Point size_header = to_window(*view, centre(columns[2].parts.column_label.bounds));
    const Point kind_left = to_window(*view, {columns[1].parts.column_header.bounds.x + 4.0, size_header.y -
                                                  (*view).absolute_bounds().y});
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, size_header}),
            "a header takes the press");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::primary, kind_left}));
    require((*view).realized_columns()[2].states.captured && (*view).realized_columns()[2].states.pressed,
            "a header dragged past the threshold is pressed and captured");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, kind_left}));
    require((*view).column_order()[1] == 2U && (*view).column_order()[2] == 1U && moves.size() == 1U &&
                moves[0].column == 2U && moves[0].display_position == 1U && !(*view).sort_column().has_value(),
            "dropping a dragged header moves the column, raises column_moved, and does not sort");
    columns = (*view).realized_columns();
    require(columns[1].index == 2U && columns[1].display_position == 1U &&
                columns[1].parts.column_header.bounds.x < columns[2].parts.column_header.bounds.x,
            "the realized columns follow the display order");
    const std::vector<TreeListView::Cell> cells = (*view).realized_cells();
    require(cells[0].column == 2U && cells[1].column == 1U && cells[0].parts.cell.bounds.x < cells[1].parts.cell.bounds.x,
            "cells follow their columns to the new positions");

    // The tree column cannot be dragged; a drop left of it lands at 1.
    columns = (*view).realized_columns();
    const Point tree_header = to_window(*view, centre(columns[0].parts.column_label.bounds));
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, tree_header}));
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::primary,
                                               {tree_header.x + 120.0, tree_header.y}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                               {tree_header.x + 120.0, tree_header.y}}));
    require((*view).column_order()[0] == 0U && moves.size() == 1U, "the tree column stays first");
    (*view).move_column(1U, 1U);
    require((*view).column_order()[1] == 1U && moves.size() == 2U, "move_column moves a column and reports it");
    bool threw = false;
    try {
        (*view).move_column(0U, 2U);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "moving the tree column is refused");
    (*view).set_column_width(2U, 60.0);
    require(widths.size() == 2U && (*view).columns()[2].width == 60.0, "set_column_width reports the change");
    (*view).set_horizontal_offset(80.0);
    require((*view).horizontal_offset() == 0.0, "the sideways offset is clamped: the columns fit");
}

void keyboard_traces() {
    std::shared_ptr<TreeListView> view = sample_view();
    Window window(view, {420.0, 260.0});
    window.perform_layout();
    std::vector<std::string> selections;
    SubscriptionToken selected = (*view).selection_changed().subscribe(RecordSelection{&selections});
    std::vector<std::string> expansions;
    SubscriptionToken expanded = (*view).expansion_changed().subscribe(RecordExpansion{&expansions});
    std::vector<std::string> activations;
    SubscriptionToken activated = (*view).item_activated().subscribe(RecordActivation{&activations});
    require(window.request_focus(view) && (*view).current_id() == std::optional<std::string_view>("src"),
            "focus puts the current node on the first row");

    require(key(window, PhysicalKey::down) && key(window, PhysicalKey::down) &&
                (*view).selected_id.read() == std::optional<std::string_view>("lib"),
            "Down moves the selection one row at a time");
    require(key(window, PhysicalKey::left) && !(*view).is_expanded("lib") && expansions == std::vector<std::string>({"-lib"}),
            "Left collapses an expanded node");
    require(key(window, PhysicalKey::left) && (*view).selected_id.read() == std::optional<std::string_view>("src"),
            "Left on a collapsed node goes to its parent");
    require(key(window, PhysicalKey::down) && key(window, PhysicalKey::down) && key(window, PhysicalKey::right) &&
                (*view).is_expanded("lib") && (*view).selected_id.read() == std::optional<std::string_view>("lib"),
            "Right expands a collapsed node and stays on it");
    require(key(window, PhysicalKey::right) && (*view).selected_id.read() == std::optional<std::string_view>("zeta"),
            "Right on an expanded node goes to its first child");
    require(key(window, PhysicalKey::end) && (*view).selected_id.read() == std::optional<std::string_view>("docs") &&
                key(window, PhysicalKey::home) && (*view).selected_id.read() == std::optional<std::string_view>("src"),
            "End and Home go to the last and first rows");
    require(key(window, PhysicalKey::page_down) && (*view).selected_id.read() == std::optional<std::string_view>("docs"),
            "Page Down moves a page, stopping at the last row");
    require(key(window, PhysicalKey::keypad_plus) && (*view).is_expanded("docs") &&
                key(window, PhysicalKey::keypad_minus) && !(*view).is_expanded("docs"),
            "keypad plus and minus expand and collapse");
    require(type(window, "*") && (*view).is_expanded("docs") && expansions.back() == "+docs",
            "the asterisk expands the subtree");
    require(key(window, PhysicalKey::enter) && activations == std::vector<std::string>({"docs"}),
            "Enter activates the current node");
    require(type(window, "z") && (*view).selected_id.read() == std::optional<std::string_view>("zeta"),
            "typing selects the next row whose text starts with it");

    // Disabled rows are skipped by the arrows and refused by the pointer.
    std::vector<TreeListNode> nodes = sample_nodes();
    nodes[1].enabled = false;
    (*view).set_nodes(std::move(nodes));
    (*view).selected_id = "src";
    require(key(window, PhysicalKey::down) && (*view).selected_id.read() == std::optional<std::string_view>("lib"),
            "Down skips a disabled row");
    const std::vector<TreeListView::Row> disabled_rows = (*view).realized_rows();
    const TreeListView::Row& beta = row_named(disabled_rows, "beta");
    require(beta.states.disabled, "a disabled node's row publishes disabled");
    click(window, to_window(*view, centre(beta.parts.text.bounds)));
    require((*view).selected_id.read() == std::optional<std::string_view>("lib"), "a click on a disabled row selects nothing");

    // The pointer: a click on the expander toggles without selecting; a
    // double click activates.
    const std::vector<TreeListView::Row> expander_rows = (*view).realized_rows();
    const TreeListView::Row& lib = row_named(expander_rows, "lib");
    click(window, to_window(*view, centre(lib.parts.expander.bounds)));
    require(!(*view).is_expanded("lib") && (*view).selected_id.read() == std::optional<std::string_view>("lib"),
            "a click on the expander collapses the node");
    const std::vector<TreeListView::Row> collapsed_rows = (*view).realized_rows();
    const TreeListView::Row& delta = row_named(collapsed_rows, "delta");
    click(window, to_window(*view, centre(delta.parts.text.bounds)));
    click(window, to_window(*view, centre(delta.parts.text.bounds)), 2U);
    require((*view).selected_id.read() == std::optional<std::string_view>("delta") && activations.back() == "delta",
            "a click selects a row and a double click activates it");
    require(selections.front() == "beta", "every selection change is reported");
}

void selection_kept_by_id() {
    std::shared_ptr<TreeListView> view = sample_view();
    Window window(view, {420.0, 260.0});
    window.perform_layout();
    std::vector<std::string> selections;
    SubscriptionToken selected = (*view).selection_changed().subscribe(RecordSelection{&selections});
    std::vector<std::string> expansions;
    SubscriptionToken expanded = (*view).expansion_changed().subscribe(RecordExpansion{&expansions});

    (*view).selected_id = "manual";
    require((*view).is_expanded("docs") && (*view).row_of("manual").has_value() &&
                expansions == std::vector<std::string>({"+docs"}) && selections == std::vector<std::string>({"manual"}),
            "selecting a hidden node expands its ancestors first, then reports the selection");
    (*view).selected_id = "zeta";
    (*view).set_expanded("src", false);
    require((*view).selected_id.read() == std::optional<std::string_view>("src") && expansions.back() == "-src" &&
                selections.back() == "src",
            "a collapse that hides the selected node moves the selection to the collapsed node");
    (*view).set_expanded("src", true);
    require((*view).selected_id.read() == std::optional<std::string_view>("src"), "expanding keeps the selection");

    // Replacement: the same IDs in a new order keep the selection.
    (*view).selected_id = "delta";
    const std::size_t changes = selections.size();
    std::vector<TreeListNode> reordered = sample_nodes();
    std::swap(reordered[1], reordered[5]);
    (*view).set_nodes(std::move(reordered));
    require((*view).selected_id.read() == std::optional<std::string_view>("delta") && selections.size() == changes &&
                (*view).row_of("delta") == 1U,
            "set_nodes keeps the selection on its ID while its row moves, without a change report");
    (*view).set_cell_text("delta", 0U, "Delta renamed");
    require((*view).selected_id.read() == std::optional<std::string_view>("delta") &&
                (*view).nodes()[1].text == "Delta renamed",
            "an edit keeps the selection");
    std::vector<TreeListNode> without = sample_nodes();
    without.erase(without.begin() + 5);
    (*view).set_nodes(std::move(without));
    require(!(*view).selected_id.read().has_value() && selections.back() == "(none)",
            "removing the selected node clears the selection and reports it");
    (*view).selected_id = "eta";
    (*view).selected_id = std::nullopt;
    require(!(*view).selected_id.read().has_value() && selections.back() == "(none)", "the program can clear the selection");
}

void semantics() {
    std::shared_ptr<TreeListView> view = sample_view();
    (*view).accessible_name = "Project";
    Window window(view, {420.0, 260.0});
    window.perform_layout();
    const SemanticDescriptor descriptor = (*view).semantic_descriptor();
    require(descriptor.role == SemanticRole::tree_grid && descriptor.name == "Project" &&
                descriptor.row_count == 7U && descriptor.column_count == 3U,
            "the view is a named tree grid sized by its shown rows and its columns");
    std::vector<SemanticNode> children = (*view).semantic_virtual_children();
    require(children.size() == 3U + 7U && children[0].role == SemanticRole::column_header &&
                children[0].name == "Name" && children[0].column_index == 1U && children[2].column_index == 3U &&
                children[3].role == SemanticRole::tree_item && children[3].name == "Sources" &&
                children[3].row_index == 1U,
            "column headers carry their column and rows are tree items carrying their row");
    const SemanticNode& lib = children[5];
    require(lib.stable_id == "lib" && lib.description.empty() && lib.level == 2U && lib.row_index == 3U &&
                lib.position_in_set == 2U && lib.set_size == 3U &&
                has_semantic_state(lib.states, SemanticState::expanded),
            "a row publishes its level, its row, its place among its siblings and its expansion");
    require(lib.children.size() == 3U && lib.children[0].role == SemanticRole::cell &&
                lib.children[0].stable_id == "lib.cell.name" && lib.children[1].name == "Kind" &&
                lib.children[1].value == "Folder" && lib.children[1].row_index == 3U &&
                lib.children[1].column_index == 2U && lib.children[2].value.empty() && lib.children[2].actions.empty(),
            "a row holds one cell per column, each carrying its row and column");
    require(children[9].stable_id == "docs" && !has_semantic_state(children[9].states, SemanticState::expanded) &&
                children[9].actions.back() == SemanticAction::expand,
            "a collapsed node offers expand");
    require((*view).on_semantic_child_action("docs", SemanticAction::expand, {}) && (*view).is_expanded("docs"),
            "the expand action expands");
    require((*view).on_semantic_child_action("eta", SemanticAction::select, {}) &&
                (*view).selected_id.read() == std::optional<std::string_view>("eta") &&
                (*view).semantic_descriptor().value == "eta.h",
            "the select action selects a node, and the list's value names it");
    std::vector<std::string> activations;
    SubscriptionToken activated = (*view).item_activated().subscribe(RecordActivation{&activations});
    require((*view).on_semantic_child_action("zeta", SemanticAction::press, {}) &&
                activations == std::vector<std::string>({"zeta"}),
            "the press action activates");
    require((*view).on_semantic_child_action("tree.column.size", SemanticAction::press, {}) &&
                (*view).sort_column() == 2U,
            "pressing a header node sorts by its column");
    children = (*view).semantic_virtual_children();
    require(children[2].value == "ascending", "a sorted header publishes its order as its value");
    require(!(*view).on_semantic_child_action("missing", SemanticAction::select, {}) &&
                !(*view).on_semantic_child_action("zeta", SemanticAction::expand, {}),
            "an unknown node and an expand on a leaf are refused");
    (*view).set_expanded("src", false);
    require(!(*view).on_semantic_child_action("zeta", SemanticAction::select, {}),
            "a hidden node refuses actions");
}

void refusals() {
    std::shared_ptr<TreeListView> view = make_control<TreeListView>(StableId("refusals"));
    (*view).set_columns(sample_columns());
    bool threw = false;
    try {
        std::vector<TreeListNode> nodes = sample_nodes();
        nodes[2].stable_id = nodes[0].stable_id;
        (*view).set_nodes(std::move(nodes));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*view).nodes().empty(), "duplicate node IDs are refused and leave the model unchanged");
    threw = false;
    try {
        std::vector<TreeListNode> nodes = sample_nodes();
        nodes[3].depth = 4U;
        (*view).set_nodes(std::move(nodes));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a depth that skips a level is refused");
    threw = false;
    try {
        std::vector<TreeListNode> nodes = sample_nodes();
        nodes[0].depth = 1U;
        (*view).set_nodes(std::move(nodes));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a first node below the top level is refused");
    (*view).set_nodes(sample_nodes());
    threw = false;
    try {
        (*view).selected_id = "missing";
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw && !(*view).selected_id.read().has_value(), "selecting an unknown node is refused");
    threw = false;
    try {
        (*view).set_sort(std::size_t{5U}, TreeListSortOrder::ascending);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "sorting by a missing column is refused");
    threw = false;
    try {
        std::vector<TreeListColumn> columns = sample_columns();
        columns[1].width = 5.0;
        (*view).set_columns(std::move(columns));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*view).columns().size() == 3U, "a column narrower than the minimum is refused");
    threw = false;
    try {
        (*view).set_cell_text("src", 7U, "x");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "editing a missing column is refused");
    threw = false;
    try {
        (*view).indentation = 2.0;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "an indentation outside the bounds is refused");
    std::shared_ptr<TreeListView> bare = make_control<TreeListView>(StableId("bare"));
    (*bare).set_nodes(sample_nodes());
    Window window(bare, {300.0, 200.0});
    window.perform_layout();
    require((*bare).realized_item_count() == 0U && !(*bare).parts().header.present,
            "without columns the view shows no rows and no header");
}

} // namespace

// A synthetic tree for the virtual source: roots, each with `children`
// leaves, every root expanded at first. Root records can be removed and
// inserted in bulk by the application (then splice_rows is called).
class SyntheticTreeSource final : public TreeListRowSource {
public:
    struct Root final {
        std::size_t number{};
        bool expanded{true};
    };

    SyntheticTreeSource(std::size_t roots, std::size_t children) : children_(children) {
        roots_.reserve(roots);
        for (std::size_t root = 0; root < roots; ++root) roots_.push_back({root, true});
        next_number_ = roots;
        recount();
    }

    [[nodiscard]] std::size_t row_count() const override { return starts_.back(); }

    void retrieve_row(std::size_t row, TreeListNode& node) const override {
        ++retrievals;
        const std::size_t root = root_of(row);
        const std::size_t offset = row - starts_[root];
        const std::size_t number = roots_[root].number;
        node.cells.clear();
        node.image_key.clear();
        node.enabled = true;
        if (offset == 0U) {
            node.stable_id = "root-" + std::to_string(number);
            node.text = "Root " + std::to_string(number);
            node.depth = 0U;
            node.expandable = true;
            node.expanded = roots_[root].expanded;
        } else {
            node.stable_id = "leaf-" + std::to_string(number) + "-" + std::to_string(offset - 1U);
            node.text = "Leaf " + std::to_string(offset - 1U);
            node.depth = 1U;
            node.expandable = false;
            node.expanded = false;
        }
    }

    [[nodiscard]] TreeListRowSplice set_row_expanded(std::size_t row, bool expanded) override {
        const std::size_t root = root_of(row);
        roots_[root].expanded = expanded;
        recount();
        TreeListRowSplice splice{row + 1U, expanded ? 0U : children_, expanded ? children_ : 0U};
        return splice;
    }

    // Every seventh leaf is tall (a two-line row); the rest keep the view's
    // estimate.
    [[nodiscard]] double row_height(std::size_t row, double estimated) const override {
        ++measurements;
        const std::size_t offset = row - starts_[root_of(row)];
        if (offset != 0U && (offset - 1U) % 7U == 0U) return tall_height;
        return estimated;
    }

    [[nodiscard]] std::optional<std::size_t> find_row(std::string_view node_id) const override {
        if (!node_id.starts_with("root-")) return std::nullopt;
        const std::string digits(node_id.substr(5U));
        const std::size_t number = static_cast<std::size_t>(std::stoull(digits));
        for (std::size_t root = 0; root < roots_.size(); ++root) {
            if (roots_[root].number == number) return starts_[root];
        }
        return std::nullopt;
    }

    // Removes `count` roots from `first`: returns the rows they held.
    std::size_t remove_roots(std::size_t first, std::size_t count) {
        const std::size_t rows = starts_[first + count] - starts_[first];
        roots_.erase(roots_.begin() + static_cast<std::ptrdiff_t>(first),
                     roots_.begin() + static_cast<std::ptrdiff_t>(first + count));
        recount();
        return rows;
    }

    // Inserts `count` new expanded roots before root `first`: returns rows.
    std::size_t insert_roots(std::size_t first, std::size_t count) {
        std::vector<Root> added{};
        for (std::size_t root = 0; root < count; ++root) added.push_back({next_number_ + root, true});
        next_number_ += count;
        roots_.insert(roots_.begin() + static_cast<std::ptrdiff_t>(first), added.begin(), added.end());
        recount();
        const std::size_t rows = count * (children_ + 1U);
        return rows;
    }

    static constexpr double tall_height = 40.0;
    mutable std::size_t retrievals{};
    mutable std::size_t measurements{};

private:
    void recount() {
        starts_.assign(roots_.size() + 1U, 0U);
        for (std::size_t root = 0; root < roots_.size(); ++root) {
            const std::size_t rows = roots_[root].expanded ? children_ + 1U : 1U;
            starts_[root + 1U] = starts_[root] + rows;
        }
    }

    [[nodiscard]] std::size_t root_of(std::size_t row) const {
        const std::vector<std::size_t>::const_iterator after = std::upper_bound(starts_.begin(), starts_.end(), row);
        const std::size_t root = static_cast<std::size_t>(after - starts_.begin()) - 1U;
        return root;
    }

    std::size_t children_{};
    std::size_t next_number_{};
    std::vector<Root> roots_{};
    std::vector<std::size_t> starts_{};
};

// Audit N94, owner ruling D10 b: a million-row virtual tree on the height
// index; jump anywhere, re-measure lazily, expand and collapse as splices,
// bulk insert and delete through splice_rows.
void virtual_million_rows() {
    std::shared_ptr<SyntheticTreeSource> source = std::make_shared<SyntheticTreeSource>(1000U, 999U);
    std::shared_ptr<TreeListView> view = make_control<TreeListView>(StableId("virtual-tree"));
    (*view).set_columns(sample_columns());
    (*view).set_row_source(source);
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    require((*view).virtual_mode() && (*view).row_count() == 1'000'000U && (*view).nodes().empty(),
            "a virtual tree takes its rows from the source");
    std::vector<TreeListView::Row> rows = (*view).realized_rows();
    require(!rows.empty() && rows.size() < 40U && rows[0].stable_id == "root-0",
            "a virtual tree realizes a screenful");
    require((*source).measurements < 60U, "only the shown rows are measured");
    // Leaf 0 of every root is tall: the row at index 1.
    require(rows.size() > 2U && rows[1].parts.row.bounds.height == SyntheticTreeSource::tall_height &&
                rows[2].parts.row.bounds.height < SyntheticTreeSource::tall_height,
            "a measured row keeps its own height");
    require(rows[2].parts.row.bounds.y == rows[1].parts.row.bounds.y + SyntheticTreeSource::tall_height,
            "the next row starts below the tall one");

    // Jump anywhere.
    (*view).set_top_row(654'321U);
    rows = (*view).realized_rows();
    require(rows[0].row == 654'321U && rows[0].stable_id == (*view).row_node_id(654'321U),
            "a jump shows the row asked for at the top");
    require((*source).measurements < 120U, "a jump measures only the new screenful");
    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 400.0, 300.0}));
    require(!painter.texts.empty(), "the jumped-to rows paint");

    // Select a leaf under root 500, then collapse root 500: the selection
    // moves to the collapsed root and the rows splice.
    std::vector<std::string> selections;
    std::vector<std::string> expansions;
    SubscriptionToken selected = (*view).selection_changed().subscribe(RecordSelection{&selections});
    SubscriptionToken expanded = (*view).expansion_changed().subscribe(RecordExpansion{&expansions});
    const std::size_t root_row = 500U * 1000U;
    (*view).set_top_row(root_row);
    window.perform_layout();
    (*view).selected_id = std::string("leaf-500-3");
    require((*view).selected_id.read() == std::optional<std::string>("leaf-500-3"), "a shown leaf is selected by ID");
    (*view).set_expanded("root-500", false);
    require((*view).row_count() == 1'000'000U - 999U && expansions.size() == 1U && expansions[0] == "-root-500" &&
                (*view).selected_id.read() == std::optional<std::string>("root-500") &&
                (*view).row_node_id(root_row + 1U) == "root-501",
            "a collapse splices the rows and lifts the selection to the collapsed node");
    (*view).set_expanded("root-500", true);
    require((*view).row_count() == 1'000'000U && (*view).row_node_id(root_row + 1U) == "leaf-500-0" &&
                (*view).selected_id.read() == std::optional<std::string>("root-500"),
            "an expansion splices the rows back");

    // Bulk delete 300 roots (300,000 rows) and insert 250 roots (250,000).
    const std::size_t removed = (*source).remove_roots(0U, 300U);
    (*view).splice_rows(0U, removed, 0U);
    require((*view).row_count() == 700'000U && (*view).row_node_id(0U) == "root-300" &&
                (*view).selected_id.read() == std::optional<std::string>("root-500") &&
                (*view).row_of("root-500") == 200'000U,
            "a bulk removal shifts the selection with its row");
    const std::size_t inserted = (*source).insert_roots(0U, 250U);
    (*view).splice_rows(0U, 0U, inserted);
    require((*view).row_count() == 950'000U && (*view).row_node_id(0U) == "root-1000" &&
                (*view).row_of("root-500") == 450'000U,
            "a bulk insertion shifts the selection with its row");
    window.perform_layout();
    rows = (*view).realized_rows();
    require(!rows.empty() && rows[0].row <= 450'000U, "the view keeps showing the selected row's place");

    // A splice the source does not match is refused and changes nothing.
    bool threw = false;
    try {
        (*view).splice_rows(0U, 1U, 0U);
    } catch (const std::logic_error&) {
        threw = true;
    }
    require(threw && (*view).row_count() == 950'000U, "a mismatched splice is refused");

    // The keyboard reaches the end.
    require(window.request_focus(view) && key(window, PhysicalKey::end) &&
                (*view).current_id() == std::optional<std::string_view>((*view).row_node_id(949'999U)),
            "End reaches the last virtual row");
    window.perform_layout();
    rows = (*view).realized_rows();
    require(rows.back().row == 949'999U, "the view scrolls to the last row");
    bool refused = false;
    try {
        (*view).set_nodes({});
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused, "a virtual tree refuses nodes");
    (*view).set_row_source(nullptr);
    require(!(*view).virtual_mode() && (*view).row_count() == 0U, "a null source returns to an empty node model");
    static_cast<void>(selected);
    static_cast<void>(expanded);
}

int main() {
    parts_states_and_paint();
    expansion_with_virtualization();
    sort_within_siblings();
    column_layout_resize_and_move();
    keyboard_traces();
    selection_kept_by_id();
    semantics();
    refusals();
    virtual_million_rows();
    std::cout << "TreeListView: parts, expansion with virtualization, sorting within siblings, column layout, "
                 "keyboard, selection by ID, semantics and refusals passed\n";
    return 0;
}
