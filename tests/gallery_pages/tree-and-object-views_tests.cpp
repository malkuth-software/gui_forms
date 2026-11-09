#include "page_tests.hpp"

namespace gallery_pages_tests {
namespace {

// Virtual rows and segments are semantic children of one control; their
// pointer targets come from the published semantic geometry.
Point virtual_child_center(const Control& control, std::string_view stable_id) {
    for (const SemanticNode& node : control.semantic_virtual_children()) {
        if (node.stable_id == stable_id) {
            return {node.bounds.x + node.bounds.width * 0.5,
                    node.bounds.y + node.bounds.height * 0.5};
        }
    }
    throw std::runtime_error("virtual child is not published: " + std::string(stable_id));
}

void click_at(Window& window, const Point point, const char* message) {
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}),
            message);
}

} // namespace

void test_tree_and_object_views(gui_forms::Window& window) {
    select_page(window, "tree-and-object-views");
    require(window.find("gallery.tree-and-object-views.heading") != nullptr,
            "tree-and-object-views page shows its heading");
    window.perform_layout();
    const std::shared_ptr<TreeView> tree =
        std::dynamic_pointer_cast<TreeView>(window.find("gallery.tree.folders"));
    const std::shared_ptr<BreadcrumbTrail> trail =
        std::dynamic_pointer_cast<BreadcrumbTrail>(window.find("gallery.tree.trail"));
    const std::shared_ptr<ObjectView> objects =
        std::dynamic_pointer_cast<ObjectView>(window.find("gallery.tree.objects"));
    const std::shared_ptr<CorrespondenceView> ledger =
        std::dynamic_pointer_cast<CorrespondenceView>(window.find("gallery.tree.ledger"));
    const std::shared_ptr<Label> status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.tree.status"));
    require(tree && trail && objects && ledger && status,
            "the page must publish all four views and its status by stable id");

    // One fixture, projected: the initial folder is src/controls.
    require((*tree).selected_id.read() == "controls" && (*trail).active_id.read() == "controls" &&
                (*trail).segments().size() == 3U && (*objects).items().size() == 2U &&
                (*ledger).items().size() == 2U && (*ledger).pinned_id.read() == "button.cpp" &&
                (*objects).view_mode.read() == ObjectViewMode::details,
            "the initial folder must be projected into the trail, the objects and the ledger");

    // TreeView: a pointer press on the fifth visible row (docs) selects it and
    // every other view follows through the tree's selection_changed.
    const Rect tree_bounds = (*tree).absolute_bounds();
    click_at(window, {tree_bounds.x + 40.0, tree_bounds.y + 2.0 + 4.5 * (*tree).item_height.read()},
             "the TreeView must accept a pointer press on a visible row");
    require((*tree).selected_id.read() == "docs" && (*trail).active_id.read() == "docs" &&
                (*trail).segments().size() == 2U && (*objects).items().size() == 2U &&
                (*objects).items()[0].stable_id == "atlas.md" &&
                (*status).text.read().find("Folder Portsmouth Gallery / docs selected") == 0U,
            "selecting a tree row must re-project the trail, the objects and the status");

    // BreadcrumbTrail: activating the root segment selects the root folder.
    click_at(window, virtual_child_center(*trail, "root"),
             "a breadcrumb segment must accept a pointer press");
    require((*tree).selected_id.read() == "root" && (*trail).segments().size() == 1U &&
                (*objects).items().size() == 4U && (*ledger).items().size() == 8U &&
                (*status).text.read().find("Breadcrumb root activated") == 0U,
            "a breadcrumb segment must drive the tree selection and the dependent views");

    // ObjectView: select the src folder row, then Enter activates it, which
    // drills the tree into that folder.
    click_at(window, virtual_child_center(*objects, "src"),
             "an ObjectView row must accept a pointer press");
    require((*objects).selected_id.read() == "src" && window.focused_control() == objects,
            "a pointer press must select the object row and focus the view");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*tree).selected_id.read() == "src" && (*objects).items().size() == 2U &&
                (*objects).items()[0].stable_id == "controls" && (*trail).segments().size() == 2U,
            "Enter on a folder object must select that folder in the tree");

    // CorrespondenceView: the ledger lists the files under src with the first
    // one pinned. A single click selects the row on press and moves the pin
    // to it on release, so the final status is the pin report with the path.
    window.perform_layout();
    require((*ledger).items().size() == 3U && (*ledger).pinned_id.read() == "button.cpp",
            "the ledger must list the files under src with the first entry pinned");
    const std::optional<Rect> row = (*ledger).item_bounds("window.cpp");
    require(row.has_value(), "the ledger row must be realized");
    const Point ledger_point =
        (*ledger).point_to_window({(*row).x + 120.0, (*row).y + (*row).height * 0.5});
    require(window.hit_test(ledger_point) == ledger,
            "the computed ledger row point must hit the CorrespondenceView");
    click_at(window, ledger_point, "a ledger row must accept a pointer press");
    require((*ledger).selected_id.read() == "window.cpp" && (*ledger).pinned_id.read() == "window.cpp" &&
                (*ledger).expanded("window.cpp") &&
                (*status).text.read().find("Pinned entry: window.cpp · Portsmouth Gallery / src / core / window.cpp") == 0U,
            "clicking a ledger row must select it, pin it and report its fixture path");

    // ChoiceGroup bound to the page's Value<int> switches the ObjectView mode.
    click(window, window.find("gallery.tree.view-mode.item.0"));
    require((*objects).view_mode.read() == ObjectViewMode::icons,
            "the Icons choice must switch the ObjectView through the bound Value");
    click(window, window.find("gallery.tree.view-mode.item.1"));
    require((*objects).view_mode.read() == ObjectViewMode::details,
            "the Details choice must switch the ObjectView back");

    // Keyboard on the tree: Left collapses the selected, expanded src folder.
    require(window.request_focus(tree) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::left}) &&
                !(*tree).expanded("src") &&
                (*status).text.read().find("Folder src collapsed") == 0U,
            "Left on an expanded tree row must collapse it and publish expansion_changed");

    // TreeListView: the whole fixture with columns; pressing the Name header
    // sorts every folder's children and the page reports it.
    const std::shared_ptr<TreeListView> tree_list =
        std::dynamic_pointer_cast<TreeListView>(window.find("gallery.tree.tree-list"));
    require(tree_list && (*tree_list).nodes().size() == 15U && (*tree_list).row_count() == 9U &&
                (*tree_list).row_node_id(0U) == "root",
            "the TreeListView must show the fixture with root, src and controls expanded");
    require((*tree_list).on_semantic_child_action("gallery.tree.tree-list.column.name", SemanticAction::press, {}) &&
                (*tree_list).sort_column() == 0U && (*tree_list).row_node_id(1U) == "assets" &&
                (*status).text.read().find("TreeListView siblings sorted by Name ascending") == 0U,
            "pressing a TreeListView header must sort the siblings and report it");

    // DataGridView's image column: the Gallery's fixtures install the test
    // card as the preview of the one PNG file; the other files have none.
    const std::shared_ptr<DataGridView> grid =
        std::dynamic_pointer_cast<DataGridView>(window.find("gallery.tree.grid"));
    require(grid != nullptr, "the page must hold the project grid");
    const std::optional<std::size_t> preview = (*grid).column_index("preview");
    const std::optional<DataGridViewRowPosition> logo = (*grid).row_position(DataGridViewRowId{"logo.png"});
    const std::optional<DataGridViewRowPosition> source = (*grid).row_position(DataGridViewRowId{"button.cpp"});
    require(preview && logo && source && (*grid).columns()[*preview].kind == DataGridViewColumnKind::image,
            "the grid must have an image column and the fixture's files");
    const BindingValue logo_value = (*grid).cell_value(*logo, *preview);
    const ImageId* const logo_image = std::get_if<ImageId>(&logo_value);
    require(logo_image != nullptr && (*logo_image).value != 0U &&
                (*grid).cell_text(*logo, *preview) == "Image preview" &&
                (*grid).cell_text(*source, *preview).empty(),
            "the PNG file's preview cell must hold the installed image, and a source file's none");

    // The link column: every file's link shows the column's text, and
    // pressing it follows the link, which is then visited.
    const std::optional<std::size_t> history = (*grid).column_index("history");
    require(history && (*grid).columns()[*history].kind == DataGridViewColumnKind::link &&
                (*grid).cell_text(*source, *history) == "Changes" && !(*grid).link_visited(*source, *history),
            "the grid must have a link column whose empty cells show its text");
    require((*grid).on_semantic_child_action("button.cpp.history", SemanticAction::press, {}) &&
                (*status).text.read().find("History of button.cpp") == 0U &&
                (*grid).link_visited(*(*grid).row_position(DataGridViewRowId{"button.cpp"}), *history),
            "pressing a link cell must report the link and leave it visited");

    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("gallery.tree.folders") != std::string::npos &&
                semantics.find("\"role\":\"list_item\"") != std::string::npos &&
                semantics.find("gallery.tree.ledger") != std::string::npos &&
                semantics.find("gallery.tree.trail") != std::string::npos &&
                semantics.find("\"role\":\"radio_group\"") != std::string::npos,
            "the views must publish their virtual-row and choice semantics");
}

}  // namespace gallery_pages_tests
