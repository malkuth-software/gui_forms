// Stage B7 property capabilities: the item views (TreeView, ObjectView;
// ListView and TreeListView have their own tests). Each test writes the
// properties through their cells or views, checks that a refusal leaves the
// value unchanged, and checks the events a write raises.

#include "gui_forms/gui_forms.hpp"

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

} // namespace

int main() {
    test_tree_view();
    std::cout << "gui_forms_item_view_capabilities_tests: all tests passed\n";
    return 0;
}
