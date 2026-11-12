// Stage B7 property capabilities: the item views (TreeView, ObjectView;
// ListView and TreeListView have their own tests). Each test writes the
// properties through their cells or views, checks that a refusal leaves the
// value unchanged, and checks the events a write raises.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/object_view.hpp"
#include "gui_forms/controls/object_view_values.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/tree_view.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/image_list/image_list/image_list.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_item_view_capabilities_tests: " << message << '\n';
    std::exit(1);
}

struct RecordTreeSelection final {
    std::vector<std::string>* currents{};
    void operator()(const TreeSelectionChange& change) const {
        (*currents).push_back(change.current_id);
    }
};

void test_tree_view() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("caps.tree.root"));
    std::shared_ptr<TreeView> tree = make_control<TreeView>(StableId("caps.tree"));
    (*tree).set_requested_bounds({0.0, 0.0, 300.0, 200.0});
    (*root).add_child(tree);
    Window window(root, {320.0, 220.0});
    std::vector<TreeViewItem> items;
    items.push_back(TreeViewItem{"root", "Root", 0U, true, true, true, {}});
    items.push_back(TreeViewItem{"leaf", "Leaf", 1U, false, false, true, {}});
    (*tree).set_items(items);
    std::vector<std::string> currents;
    const SubscriptionToken token = (*tree).selection_changed().subscribe(RecordTreeSelection{&currents});
    (*tree).selected_id = std::string("leaf");
    (*tree).selected_id = std::string("leaf");
    require((*tree).selected_id.read() == "leaf" && (*tree).active_id() == "leaf" &&
                currents.size() == 1U && currents[0] == "leaf",
            "a selection write activates the item and raises selection_changed once");
    bool refused = false;
    try {
        (*tree).selected_id = std::string("missing");
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*tree).selected_id.read() == "leaf", "a selection outside the model is refused");
    (*tree).set_expanded("root", false);
    require((*tree).selected_id.read() == "root" && currents.size() == 2U,
            "collapsing the selection's ancestor selects the ancestor through the view");
    static_cast<void>(token);

    refused = false;
    try {
        (*tree).item_height = 13.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*tree).item_height.read() == 24.0, "a row height under 14 is refused");
    refused = false;
    try {
        (*tree).indentation = 97.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*tree).indentation.read() == 18.0, "an indentation over 96 is refused");
    (*tree).show_expanders = false;
    require(!(*tree).show_expanders.read(), "show_expanders stores");
    refused = false;
    try {
        (*tree).font = FontSpec{FontRole::content, -1.0, 400, false};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*tree).font.read() == FontSpec{FontRole::content, 12.0, 400, false},
            "an invalid font is refused");
    const std::shared_ptr<ImageList> images = std::make_shared<ImageList>(window, Size{16.0, 16.0});
    (*tree).image_list = images;
    require((*tree).image_list.read() == images, "the image list stores");
    (*tree).image_list = std::shared_ptr<ImageList>{};
    require(!(*tree).image_list.read(), "the image list clears");
}

struct CountObjectSelection final {
    int* count{};
    void operator()(const ObjectSelectionChange&) const { ++*count; }
};

ObjectViewItem object_item(const char* stable_id, const char* name) {
    ObjectViewItem item;
    item.stable_id = stable_id;
    item.name = name;
    return item;
}

void test_object_view() {
    std::shared_ptr<ObjectView> view = make_control<ObjectView>(StableId("caps.objects"));
    std::vector<ObjectViewItem> items;
    items.push_back(object_item("a", "Alpha"));
    items.push_back(object_item("b", "Beta"));
    (*view).set_items(items);
    int changes = 0;
    const SubscriptionToken token = (*view).selection_changed().subscribe(CountObjectSelection{&changes});
    (*view).set_selected_ids({"a", "b"}, "b");
    require((*view).selected_ids().size() == 2U && (*view).selected_id.read() == "b" && changes == 1,
            "set_selected_ids selects both with a primary");
    (*view).selection_mode = ObjectSelectionMode::single;
    require((*view).selected_ids().size() == 1U && (*view).selected_id.read() == "b" && changes == 2,
            "single selection keeps only the primary item");
    (*view).selected_id = std::string("a");
    require((*view).selected_id.read() == "a" && (*view).selected_ids().size() == 1U && changes == 3,
            "a selected_id write selects that item alone");
    (*view).selected_id = std::string{};
    require((*view).selected_id.read().empty() && (*view).selected_ids().empty() && changes == 4,
            "an empty write clears the selection");
    bool refused = false;
    try {
        (*view).selected_id = std::string("missing");
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && changes == 4, "a selection outside the model is refused");
    static_cast<void>(token);

    refused = false;
    try {
        (*view).view_mode = static_cast<ObjectViewMode>(7);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).view_mode.read() == ObjectViewMode::icons, "an undeclared mode is refused");
    (*view).view_mode = ObjectViewMode::details;
    require((*view).view_mode.read() == ObjectViewMode::details, "the mode stores");
    refused = false;
    try {
        (*view).icon_cell_size = Size{85.0, 100.0};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).icon_cell_size.read() == Size{112.0, 91.0}, "an icon cell under 86 wide is refused");
    refused = false;
    try {
        (*view).details_row_height = 19.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).details_row_height.read() == 28.0, "a details row under 20 is refused");
    (*view).show_secondary_text = false;
    require(!(*view).show_secondary_text.read(), "show_secondary_text stores");

    ObjectDetailsColumn name_column;
    name_column.id = ObjectColumnId{"name"};
    name_column.label = "Name";
    for (ObjectViewItem& item : items) {
        item.cells.push_back(ObjectDetailsCell{ObjectColumnId{"name"}, item.name});
    }
    (*view).set_details_model({name_column}, items);
    (*view).details_sort = ObjectDetailsSort{ObjectColumnId{"name"}, ObjectSortDirection::descending};
    require((*view).details_sort.read().column == ObjectColumnId{"name"} &&
                (*view).details_sort.read().direction == ObjectSortDirection::descending,
            "the sort indicator stores");
    refused = false;
    try {
        (*view).details_sort = ObjectDetailsSort{ObjectColumnId{"size"}, ObjectSortDirection::ascending};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).details_sort.read().column == ObjectColumnId{"name"}, "an unknown sort column is refused");
}

} // namespace

int main() {
    test_tree_view();
    test_object_view();
    std::cout << "gui_forms_item_view_capabilities_tests: all tests passed\n";
    return 0;
}
