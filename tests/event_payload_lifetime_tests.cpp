#include "gui_forms/gui_forms.hpp"
#include "gui_forms_lab/correspondence_view.hpp"
#include "support/theme_definition_heap.hpp"
#include "support/named_callbacks.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Event payloads that must outlive what their handlers do. Each test
// subscribes a handler that changes the emitting object (replaces its items,
// emits again, sets another theme, selects another object) ahead of a
// handler that records the payload. Before the fixes the payload was a
// reference into storage the first handler freed: the sanitizer tree
// reports a heap-use-after-free or an overflow, and the recorded value is
// whatever the freed memory held.

namespace {

using namespace gui_forms;
namespace callbacks = gui_forms::test_support;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Stable IDs longer than any small-string buffer, so a freed ID's bytes
// live in freed heap memory of their own.
const std::string first_id = "event.payload.lifetime.first.item.identity";
const std::string second_id = "event.payload.lifetime.second.item.identity";
const std::string third_id = "event.payload.lifetime.third.item.identity";

// -- ObjectView ------------------------------------------------------------

std::vector<ObjectViewItem> object_items(const std::vector<std::string>& ids) {
    std::vector<ObjectViewItem> items;
    for (const std::string& id : ids) {
        ObjectViewItem item;
        item.stable_id = id;
        item.name = id;
        items.push_back(item);
    }
    return items;
}

// Replaces the view's items on activation, as a navigating consumer does.
struct ReplaceObjectItems final {
    ObjectView* view{};
    void operator()(const std::string&) const {
        (*view).set_items(object_items({third_id}));
    }
};

void test_object_view_activation_outlives_replaced_items() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("payload.object.root"));
    std::shared_ptr<ObjectView> view = make_control<ObjectView>(StableId("payload.object"));
    (*view).set_requested_bounds({0.0, 0.0, 300.0, 200.0});
    (*view).set_items(object_items({first_id, second_id}));
    (*root).add_child(view);
    Window window(root, {320.0, 220.0});
    window.perform_layout();
    std::vector<std::string> activated;
    SubscriptionToken replace = (*view).item_activated().subscribe(ReplaceObjectItems{view.get()});
    SubscriptionToken record = (*view).item_activated().subscribe(
        callbacks::PushBack<std::vector<std::string>, const std::string&>(activated));
    require(window.request_focus(view), "the object view takes focus");
    (*view).selected_id = first_id;
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}),
            "Enter activates the selected object");
    require(activated.size() == 1U && activated[0] == first_id,
            "a later handler reads the activated ID after an earlier one replaced the items");
}

// -- TreeView --------------------------------------------------------------

std::vector<TreeViewItem> tree_items() {
    std::vector<TreeViewItem> items;
    items.push_back(TreeViewItem{first_id, "First", 0U, true, true, true, {}});
    items.push_back(TreeViewItem{second_id, "Second", 1U, false, false, true, {}});
    return items;
}

struct ReplaceTreeItemsOnSelection final {
    TreeView* view{};
    void operator()(const TreeSelectionChange&) const {
        std::vector<TreeViewItem> items;
        items.push_back(TreeViewItem{third_id, "Third", 0U, false, false, true, {}});
        (*view).set_items(items);
    }
};

struct ReplaceTreeItemsOnExpansion final {
    TreeView* view{};
    void operator()(const TreeExpansionChange&) const {
        std::vector<TreeViewItem> items;
        items.push_back(TreeViewItem{third_id, "Third", 0U, false, false, true, {}});
        (*view).set_items(items);
    }
};

void test_tree_view_activation_outlives_a_selection_handler() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("payload.tree.root"));
    std::shared_ptr<TreeView> view = make_control<TreeView>(StableId("payload.tree"));
    (*view).set_requested_bounds({0.0, 0.0, 300.0, 200.0});
    (*view).set_items(tree_items());
    (*root).add_child(view);
    Window window(root, {320.0, 220.0});
    window.perform_layout();
    std::vector<std::string> activated;
    SubscriptionToken replace = (*view).selection_changed().subscribe(
        ReplaceTreeItemsOnSelection{view.get()});
    SubscriptionToken record = (*view).item_activated().subscribe(
        callbacks::PushBack<std::vector<std::string>, const std::string&>(activated));
    require(window.perform_semantic_action((*view).runtime_id(), second_id, SemanticAction::press),
            "pressing a tree item is performed");
    require(activated.size() == 1U && activated[0] == second_id,
            "the activation names the pressed item after a selection handler replaced the items");
}

void test_tree_view_collapse_outlives_an_expansion_handler() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("payload.collapse.root"));
    std::shared_ptr<TreeView> view = make_control<TreeView>(StableId("payload.collapse"));
    (*view).set_requested_bounds({0.0, 0.0, 300.0, 200.0});
    (*view).set_items(tree_items());
    (*root).add_child(view);
    Window window(root, {320.0, 220.0});
    window.perform_layout();
    (*view).selected_id = second_id;
    SubscriptionToken replace = (*view).expansion_changed().subscribe(
        ReplaceTreeItemsOnExpansion{view.get()});
    bool threw = false;
    try {
        (*view).set_expanded(first_id, false);
    } catch (const std::exception&) {
        threw = true;
    }
    require(!threw && (*view).items().size() == 1U,
            "collapsing over a selected descendant survives a handler that replaced the items");
}

// -- CorrespondenceView ----------------------------------------------------

std::vector<CorrespondenceItem> correspondence_items(const std::vector<std::string>& ids) {
    std::vector<CorrespondenceItem> items;
    for (const std::string& id : ids) {
        CorrespondenceItem item;
        item.stable_id = id;
        item.title = id;
        items.push_back(item);
    }
    return items;
}

// Moves the pressed entry to the front of a shorter list, so the index the
// view looked up before selecting is past the end of the new items.
struct ShortenCorrespondenceOnSelection final {
    CorrespondenceView* view{};
    void operator()(const CorrespondenceSelectionChange& change) const {
        if (change.current_id != third_id || (*view).items().size() == 1U) return;
        (*view).set_items(correspondence_items({third_id}));
    }
};

void test_correspondence_activation_looks_the_item_up_again() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("payload.ledger.root"));
    std::shared_ptr<CorrespondenceView> view =
        make_control<CorrespondenceView>(StableId("payload.ledger"));
    (*view).set_requested_bounds({0.0, 0.0, 400.0, 300.0});
    (*view).set_items(correspondence_items({first_id, second_id, third_id}));
    (*root).add_child(view);
    Window window(root, {420.0, 320.0});
    window.perform_layout();
    std::vector<std::string> activated;
    SubscriptionToken shorten = (*view).selection_changed().subscribe(
        ShortenCorrespondenceOnSelection{view.get()});
    SubscriptionToken record = (*view).item_activated().subscribe(
        callbacks::PushBack<std::vector<std::string>, const std::string&>(activated));
    require(window.perform_semantic_action((*view).runtime_id(), third_id, SemanticAction::press),
            "pressing a ledger entry is performed");
    require(activated.size() == 1U && activated[0] == third_id,
            "the activation reads the entry where the new items hold it");
}

// -- SemanticFeedback ------------------------------------------------------

// Emits a second record from inside the first one's emission, once.
struct RecordKind final {
    std::vector<SemanticFeedbackKind>* kinds{};
    void operator()(const SemanticFeedbackRecord& record) const { (*kinds).push_back(record.kind); }
};

struct EmitAgain final {
    SemanticFeedback* feedback{};
    bool* emitted_again{};
    void operator()(const SemanticFeedbackRecord&) const {
        if (*emitted_again) return;
        *emitted_again = true;
        static_cast<void>((*feedback).emit(SemanticFeedbackKind::option_committed));
    }
};

void test_semantic_feedback_record_outlives_a_nested_emission() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("payload.feedback.root"));
    Window window(root, {100.0, 60.0});
    SemanticFeedback feedback(window);
    bool emitted_again = false;
    std::vector<SemanticFeedbackKind> kinds;
    SubscriptionToken again = feedback.emitted().subscribe(EmitAgain{&feedback, &emitted_again});
    SubscriptionToken record = feedback.emitted().subscribe(RecordKind{&kinds});
    static_cast<void>(feedback.emit(SemanticFeedbackKind::location_changed));
    require(kinds.size() == 2U && kinds[0] == SemanticFeedbackKind::option_committed &&
                kinds[1] == SemanticFeedbackKind::location_changed,
            "the outer emission's later handler reads its own record after a nested emission grew the history");
}

// -- Window theme ----------------------------------------------------------

std::shared_ptr<const Theme> theme_named(const char* id) {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = id;
    std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

// Sets a second theme from inside the first one's emission, once.
struct SetAnotherTheme final {
    Window* window{};
    bool* replaced{};
    void operator()(const Theme&) const {
        if (*replaced) return;
        *replaced = true;
        (*window).set_theme(theme_named("payload-second"));
    }
};

struct RecordThemeId final {
    std::vector<std::string>* ids{};
    void operator()(const Theme& theme) const {
        (*ids).push_back(std::string(theme.id()));
    }
};

void test_theme_outlives_a_nested_theme_change() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("payload.theme.root"));
    Window window(root, {100.0, 60.0});
    bool replaced = false;
    std::vector<std::string> ids;
    SubscriptionToken again = window.theme_changed().subscribe(SetAnotherTheme{&window, &replaced});
    SubscriptionToken record = window.theme_changed().subscribe(RecordThemeId{&ids});
    // Only the window and its resources hold the first theme, so the
    // nested change releases it unless the emission holds it.
    window.set_theme(theme_named("payload-first"));
    require(ids.size() == 2U && ids[0] == "payload-second" && ids[1] == "payload-first",
            "the outer emission's later handler reads its own theme after a nested change replaced it");
    require(window.theme().id() == "payload-second", "the nested theme is the window's");
}

// -- PropertyGrid ----------------------------------------------------------

// Selects another object on a failed edit, which resets the grid's last
// error.
struct SelectAnotherObject final {
    PropertyGrid* grid{};
    Control::Ptr other;
    void operator()(const PropertyGridEditError&) const {
        if ((*grid).selected_object() == other) return;
        (*grid).set_selected_object(other);
    }
};

void test_property_grid_error_outlives_a_new_selection() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("payload.grid.root"));
    std::shared_ptr<CheckBox> target = make_control<CheckBox>(StableId("payload.grid.target"), "Target");
    std::shared_ptr<CheckBox> other = make_control<CheckBox>(StableId("payload.grid.other"), "Other");
    std::shared_ptr<PropertyGrid> grid = make_control<PropertyGrid>(StableId("payload.grid"));
    (*target).set_requested_bounds({0.0, 0.0, 100.0, 24.0});
    (*other).set_requested_bounds({0.0, 30.0, 100.0, 24.0});
    (*grid).set_requested_bounds({120.0, 0.0, 300.0, 300.0});
    (*root).add_child(target);
    (*root).add_child(other);
    (*root).add_child(grid);
    (*grid).set_selected_object(target);
    Window window(root, {440.0, 320.0});
    window.perform_layout();
    std::vector<PropertyGridEditError> failures;
    SubscriptionToken select = (*grid).edit_failed().subscribe(SelectAnotherObject{grid.get(), other});
    SubscriptionToken record = (*grid).edit_failed().subscribe(
        callbacks::PushBack<std::vector<PropertyGridEditError>, const PropertyGridEditError&>(failures));
    require(!(*grid).try_set_property_value("TabIndex", BindingValue{std::string("-1")}),
            "an invalid tab index is refused");
    require(failures.size() == 1U && failures[0].property_name == "tab_index" &&
                !failures[0].message.empty(),
            "a later handler reads the failure after an earlier one selected another object");
    require((*grid).selected_object() == other && !(*grid).last_error().has_value(),
            "the new selection cleared the grid's last error");
}

// The tests by name, so one can run alone (each case stops the sanitizer
// tree at its first report).
struct NamedTest final {
    std::string_view name;
    void (*run)();
};

const std::array<NamedTest, 7> tests{{
    {"object_view", test_object_view_activation_outlives_replaced_items},
    {"tree_view_activation", test_tree_view_activation_outlives_a_selection_handler},
    {"tree_view_collapse", test_tree_view_collapse_outlives_an_expansion_handler},
    {"correspondence", test_correspondence_activation_looks_the_item_up_again},
    {"semantic_feedback", test_semantic_feedback_record_outlives_a_nested_emission},
    {"theme", test_theme_outlives_a_nested_theme_change},
    {"property_grid", test_property_grid_error_outlives_a_new_selection},
}};

} // namespace

// With no argument every test runs; with one, only the test of that name.
int main(int argc, char** argv) {
    try {
        const std::string_view only = argc > 1 ? std::string_view(argv[1]) : std::string_view();
        std::size_t ran = 0U;
        for (const NamedTest& test : tests) {
            if (!only.empty() && test.name != only) continue;
            test.run();
            ++ran;
        }
        if (ran == 0U) throw std::runtime_error("no test has that name");
        std::cout << "gui_forms_event_payload_lifetime_tests: " << ran << " tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_event_payload_lifetime_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
