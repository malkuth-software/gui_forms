// Cut-off item text revealed in place (owner decision, 2026-10-28): a
// ListView row in the details and list views, or a TreeView node, whose
// text its cell cuts off shows the whole text over the row while the
// pointer is on it. It is not a tooltip: it needs nothing set and ignores
// the tooltip switches.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "cut-off text tests failed: " << message << '\n';
    std::exit(1);
}

// Paints nothing: the window records each control's display operations.
class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

const SemanticNode* find_named(const std::vector<SemanticNode>& nodes, std::string_view name) {
    for (const SemanticNode& node : nodes) {
        if (node.name == name) return &node;
        if (const SemanticNode* found = find_named(node.children, name)) return found;
    }
    return nullptr;
}
const SemanticNode* find_named(std::vector<SemanticNode>&& nodes, std::string_view name) = delete;

void hover(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
}

// Where the item named `name` is, in window coordinates.
Rect item_bounds(Window& window, std::string_view name) {
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* node = find_named(snapshot.roots, name);
    require(node != nullptr, "the item is published");
    return (*node).bounds;
}

// What the reveal of `owner` shows: the text it painted and its bounds, or
// nothing when none shows.
struct Revealed final {
    bool shown{};
    std::string text{};
    Rect bounds{};
};

Revealed revealed(Window& window, std::string_view owner) {
    window.perform_layout();
    NullPainter painter;
    const Size client = window.client_size();
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, client.width, client.height}));
    VisualInspectionOptions options{};
    options.include_text = true;
    const VisualInspectionSnapshot snapshot = window.visual_inspection_snapshot(options);
    const std::string face_id = std::string(owner) + ".reveal.face";
    Revealed result{};
    for (const VisualControlInspection& control : snapshot.controls) {
        if (control.stable_id != face_id || !control.popup_tree) continue;
        result.shown = true;
        result.bounds = control.layout.absolute_bounds;
        for (const VisualPaintOperationSnapshot& operation : control.display_operations) {
            if (operation.operation == VisualPaintOperation::draw_text) result.text = operation.text;
        }
    }
    return result;
}

constexpr std::string_view long_name = "A remarkably long report name, quarterly.txt";

void test_list_view_details_and_list() {
    Control::Ptr root = make_control<Control>(StableId("reveal.root"));
    (*root).set_requested_bounds({0.0, 0.0, 520.0, 260.0});
    std::shared_ptr<ListView> list = make_control<ListView>(StableId("reveal.list"));
    (*list).set_requested_bounds({20.0, 20.0, 300.0, 200.0});
    (*list).set_view(ListViewView::details);
    (*list).set_columns({{.stable_id = "name", .text = "Name", .width = 100.0},
                         {.stable_id = "size", .text = "Size", .width = 80.0}});
    (*list).set_items({{.stable_id = "long", .text = std::string(long_name), .sub_items = {"12 KB"}},
                       {.stable_id = "short", .text = "Short", .sub_items = {"1 KB"}}});
    (*root).add_child(list);
    Window window(root, {520.0, 260.0});
    // The reveal is not a tooltip: the switches leave it alone.
    window.set_tool_tips_shown(false);

    const Rect long_row = item_bounds(window, long_name);
    hover(window, {long_row.x + 30.0, long_row.y + long_row.height * 0.5});
    const Revealed shown = revealed(window, "reveal.list");
    require(shown.shown && shown.text == long_name, "a cut-off name shows whole over its row");
    require(shown.bounds.width > 100.0 && shown.bounds.y == long_row.y && shown.bounds.height == long_row.height,
            "the reveal is wider than the column, at the row's own position");
    require(!window.tool_tip_visible() && window.hit_test({long_row.x + 150.0, long_row.y + 4.0}) == list,
            "the reveal is no tooltip and the pointer passes through it");

    hover(window, {long_row.x + 140.0, long_row.y + long_row.height * 0.5});
    require(!revealed(window, "reveal.list").shown, "a sub-item that fits reveals nothing");
    const Rect short_row = item_bounds(window, "Short");
    hover(window, {short_row.x + 30.0, short_row.y + short_row.height * 0.5});
    require(!revealed(window, "reveal.list").shown, "a name that fits reveals nothing");

    hover(window, {long_row.x + 30.0, long_row.y + long_row.height * 0.5});
    require(revealed(window, "reveal.list").shown, "back on the cut-off name, it shows again");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                               {long_row.x + 30.0, long_row.y + long_row.height * 0.5}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                               {long_row.x + 30.0, long_row.y + long_row.height * 0.5}}));
    require((*list).selected_count() == 1U, "the press reaches the row under the reveal");
    hover(window, {long_row.x + 31.0, long_row.y + long_row.height * 0.5});
    require(revealed(window, "reveal.list").shown, "the selected row reveals again on the next move");
    hover(window, {480.0, 240.0});
    require(!revealed(window, "reveal.list").shown, "leaving the view conceals it");

    // The list view: the item's own text cell.
    (*list).set_view(ListViewView::list);
    const Rect list_item = item_bounds(window, long_name);
    hover(window, {list_item.x + 30.0, list_item.y + list_item.height * 0.5});
    const Revealed in_list = revealed(window, "reveal.list");
    require(in_list.shown && in_list.text == long_name, "the list view reveals a cut-off item too");

    // Other views and owner-drawn content reveal nothing.
    (*list).set_view(ListViewView::large_icon);
    const Rect icon_item = item_bounds(window, long_name);
    hover(window, {icon_item.x + icon_item.width * 0.5, icon_item.y + icon_item.height - 6.0});
    require(!revealed(window, "reveal.list").shown, "the icon views reveal nothing");
}

void test_tree_view() {
    Control::Ptr root = make_control<Control>(StableId("reveal.tree-root"));
    (*root).set_requested_bounds({0.0, 0.0, 420.0, 240.0});
    std::shared_ptr<TreeView> tree = make_control<TreeView>(StableId("reveal.tree"));
    (*tree).set_requested_bounds({20.0, 20.0, 160.0, 180.0});
    (*tree).set_items({{.stable_id = "root", .text = "Root", .depth = 0U, .expandable = true, .expanded = true},
                       {.stable_id = "deep", .text = std::string(long_name), .depth = 1U}});
    (*root).add_child(tree);
    Window window(root, {420.0, 240.0});
    window.set_tool_tips_shown(false);

    const Rect deep_row = item_bounds(window, long_name);
    hover(window, {deep_row.x + 60.0, deep_row.y + deep_row.height * 0.5});
    const Revealed shown = revealed(window, "reveal.tree");
    require(shown.shown && shown.text == long_name, "a cut-off node shows whole over its row");
    require(shown.bounds.x + shown.bounds.width > 180.0, "the node's reveal runs past the view's edge");
    const Rect top_row = item_bounds(window, "Root");
    hover(window, {top_row.x + 40.0, top_row.y + top_row.height * 0.5});
    require(!revealed(window, "reveal.tree").shown, "a node that fits reveals nothing");
    hover(window, {deep_row.x + 60.0, deep_row.y + deep_row.height * 0.5});
    require(revealed(window, "reveal.tree").shown, "back on the cut-off node, it shows again");
    (*tree).set_items({{.stable_id = "other", .text = "Other"}});
    require(!revealed(window, "reveal.tree").shown, "new items conceal it");
}

} // namespace

int main() {
    test_list_view_details_and_list();
    test_tree_view();
    std::cout << "Cut-off text: ListView details and list views, TreeView, outside the tooltip switches passed\n";
    return 0;
}
