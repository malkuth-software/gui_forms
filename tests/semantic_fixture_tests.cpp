// The semantics behind the Windows UI Automation provider and text tests,
// read headless from Window::semantic_snapshot.
//
// windows_accessibility_uia_provider_tests.cpp and
// windows_accessibility_uia_text_tests.cpp read two small fixtures through
// IUIAutomation on the user's desktop, under a machine-wide lock. Each of
// their checks has two halves: what the semantic tree says and does (owned
// here) and what UI Automation returns for it (owned there). This test builds
// the same fixtures, with the same stable IDs, texts and bounds, in headless
// windows and checks the first half through the snapshot, the semantic
// actions (performed as the Windows adapter performs them: a control's own
// node by its runtime ID, a virtual child through its control) and the text
// queries:
// - the provider fixture: each node's role, name, stable ID, value, range,
//   actions and states; distinct runtime IDs; a renamed control described
//   again; press, set_value, select, expand and collapse, each observed in
//   the model and the snapshot; a removed control's identity refused;
// - the text fixture: which nodes publish text (not a password box); the
//   value, selection and read-only state; the grapheme, word, line,
//   paragraph and document units; one box per line; the selection set
//   through set_semantic_text_selection; a selection or text change shows
//   in the next snapshot with a new generation; a removed text box's
//   identity answers stale_identity.
//
// Execution context: one thread, the windows' UI thread; no host session.

#include "gui_forms/gui_forms.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

// "Café au lait" with the é spelled e and U+0301 COMBINING ACUTE ACCENT:
// bytes 3 to 6 are one grapheme (the UI Automation text test's text).
constexpr std::string_view single_text = "Cafe\xCC\x81 au lait";
constexpr std::string_view multi_text = "First line\nSecond line";

// Counts failures and checks; a failure is printed at once.
struct Checks final {
    int passed{};
    int failed{};
};

void expect(Checks& checks, bool condition, std::string_view what) {
    if (condition) {
        ++checks.passed;
        return;
    }
    ++checks.failed;
    std::cerr << "gui_forms_semantic_fixture_tests: " << what << '\n';
}

// The node with `stable_id` at or below `nodes`, depth first, or null.
const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* found = find_node(node.children, stable_id);
        if (found != nullptr) return found;
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

bool has_action(const SemanticNode& node, SemanticAction action) noexcept {
    for (const SemanticAction published : node.actions) {
        if (published == action) return true;
    }
    return false;
}

bool has_state(const SemanticNode& node, SemanticState state) noexcept {
    const bool has = has_semantic_state(node.states, state);
    return has;
}

// A copy of the node with `stable_id` in a fresh snapshot, or none.
std::optional<SemanticNode> read_node(Window& window, std::string_view stable_id) {
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* found = find_node(snapshot.roots, stable_id);
    if (found == nullptr) return std::nullopt;
    SemanticNode copy = *found;
    return copy;
}

void collect_runtime_ids(const std::vector<SemanticNode>& nodes, std::vector<std::uint64_t>& ids) {
    for (const SemanticNode& node : nodes) {
        ids.push_back(node.runtime_id);
        collect_runtime_ids(node.children, ids);
    }
}

// -- the UI Automation provider fixture -------------------------------------

struct ProviderFixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Button> button;
    std::shared_ptr<CheckBox> check;
    std::shared_ptr<TextBox> text;
    std::shared_ptr<NumericUpDown> number;
    std::shared_ptr<ListBox> list;
    std::shared_ptr<TreeView> tree;
    std::shared_ptr<Button> doomed;
};

// The provider test's fixture (make_fixture there), without its counting
// root, which measures the provider's tree reuse.
std::unique_ptr<Window> make_provider_fixture(ProviderFixture& fixture) {
    fixture.root = make_control<Panel>(StableId("uia.root"));
    fixture.button = make_control<Button>(StableId("uia.button"), "Press me");
    fixture.check = make_control<CheckBox>(StableId("uia.check"), "Check me");
    fixture.text = make_control<TextBox>(StableId("uia.text"));
    fixture.number = make_control<NumericUpDown>(StableId("uia.number"));
    fixture.list = make_control<ListBox>(StableId("uia.list"));
    fixture.tree = make_control<TreeView>(StableId("uia.tree"));
    fixture.doomed = make_control<Button>(StableId("uia.doomed"), "Removed");
    (*fixture.button).set_requested_bounds({10.0, 10.0, 120.0, 28.0});
    (*fixture.check).set_requested_bounds({10.0, 50.0, 160.0, 24.0});
    (*fixture.text).set_requested_bounds({10.0, 90.0, 200.0, 26.0});
    (*fixture.number).set_requested_bounds({10.0, 130.0, 120.0, 26.0});
    (*fixture.number).set_range(0.0, 10.0);
    (*fixture.list).set_requested_bounds({240.0, 10.0, 160.0, 100.0});
    (*fixture.list).set_items({"Alpha", "Beta", "Gamma"});
    (*fixture.tree).set_requested_bounds({240.0, 120.0, 160.0, 100.0});
    std::vector<TreeViewItem> nodes;
    nodes.push_back(TreeViewItem{"uia.tree.folder", "Folder", 0U, true, false, true, {}});
    nodes.push_back(TreeViewItem{"uia.tree.file", "File", 1U, false, false, true, {}});
    (*fixture.tree).set_items(nodes);
    (*fixture.doomed).set_requested_bounds({10.0, 170.0, 120.0, 28.0});
    (*fixture.root).add_child(fixture.button);
    (*fixture.root).add_child(fixture.check);
    (*fixture.root).add_child(fixture.text);
    (*fixture.root).add_child(fixture.number);
    (*fixture.root).add_child(fixture.list);
    (*fixture.root).add_child(fixture.tree);
    (*fixture.root).add_child(fixture.doomed);
    std::unique_ptr<Window> window = std::make_unique<Window>(fixture.root, Size{420.0, 240.0});
    return window;
}

struct CountClick final {
    int* clicks{};
    void operator()(ButtonBase&) const { ++*clicks; }
};

// A control's own node, by its runtime ID, as the adapter names it.
bool perform_on_control(Window& window, const Control& control, SemanticAction action,
                        std::string_view value = {}) {
    const bool handled = window.perform_semantic_action(control.runtime_id(), std::string_view(), action, value);
    return handled;
}

// A virtual child, through the control that publishes it.
bool perform_on_child(Window& window, const Control& owner, std::string_view child, SemanticAction action) {
    const bool handled = window.perform_semantic_action(owner.runtime_id(), child, action);
    return handled;
}

void check_provider_identity(Checks& checks, Window& window) {
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* button = find_node(snapshot.roots, "uia.button");
    const SemanticNode* check = find_node(snapshot.roots, "uia.check");
    expect(checks, button != nullptr, "the button publishes a node with its stable ID");
    expect(checks, check != nullptr, "the check box publishes a node with its stable ID");
    if (button == nullptr || check == nullptr) return;
    expect(checks, (*button).role == SemanticRole::button && (*button).name == "Press me",
           "the button's role and name");
    std::vector<std::uint64_t> ids;
    collect_runtime_ids(snapshot.roots, ids);
    bool distinct = true;
    for (std::size_t left = 0; left < ids.size(); ++left) {
        if (ids[left] == 0U) distinct = false;
        for (std::size_t right = left + 1U; right < ids.size(); ++right) {
            if (ids[left] == ids[right]) distinct = false;
        }
    }
    expect(checks, distinct && (*button).runtime_id != (*check).runtime_id,
           "every node carries a distinct, nonzero runtime ID");
}

void check_provider_rename(Checks& checks, Window& window, ProviderFixture& fixture) {
    const std::uint64_t before = window.semantic_snapshot().generation;
    (*fixture.button).text = "Renamed";
    const SemanticSnapshot renamed = window.semantic_snapshot();
    const SemanticNode* node = find_node(renamed.roots, "uia.button");
    expect(checks, node != nullptr && (*node).name == "Renamed" && renamed.generation != before,
           "a renamed button is described again, with a new generation");
    (*fixture.button).text = "Press me";
    const std::optional<SemanticNode> restored = read_node(window, "uia.button");
    expect(checks, restored && (*restored).name == "Press me", "the restored name is published");
}

void check_provider_press(Checks& checks, Window& window, ProviderFixture& fixture) {
    const std::optional<SemanticNode> button = read_node(window, "uia.button");
    if (!button) return;
    expect(checks, has_action(*button, SemanticAction::press), "the button publishes press");
    expect(checks,
           !has_state(*button, SemanticState::checkable) && !has_state(*button, SemanticState::checked) &&
               !has_state(*button, SemanticState::mixed),
           "a plain button cannot be checked (no toggle)");
    int clicks = 0;
    SubscriptionToken clicked = (*fixture.button).clicked().subscribe(CountClick{&clicks});
    expect(checks, perform_on_control(window, *fixture.button, SemanticAction::press) && clicks == 1,
           "press clicks the button once");
}

void check_provider_toggle(Checks& checks, Window& window, ProviderFixture& fixture) {
    const std::optional<SemanticNode> check = read_node(window, "uia.check");
    if (!check) return;
    expect(checks, (*check).role == SemanticRole::check_box && has_action(*check, SemanticAction::press),
           "the check box's role, and press");
    expect(checks, !has_state(*check, SemanticState::checked) && !(*fixture.check).checked(),
           "the check box starts unchecked");
    expect(checks, perform_on_control(window, *fixture.check, SemanticAction::press) && (*fixture.check).checked(),
           "press checks the check box");
    const std::optional<SemanticNode> after = read_node(window, "uia.check");
    expect(checks, after && has_state(*after, SemanticState::checked), "the snapshot publishes it checked");
}

void check_provider_value(Checks& checks, Window& window, ProviderFixture& fixture) {
    const std::optional<SemanticNode> text = read_node(window, "uia.text");
    if (!text) return;
    expect(checks, (*text).role == SemanticRole::text_box, "the text box's role");
    expect(checks,
           perform_on_control(window, *fixture.text, SemanticAction::set_value, "Hello, UIA") &&
               (*fixture.text).text() == "Hello, UIA",
           "set_value sets the text box's text");
    const std::optional<SemanticNode> after = read_node(window, "uia.text");
    expect(checks, after && (*after).value == "Hello, UIA", "the node's value is the text");
}

void check_provider_range(Checks& checks, Window& window, ProviderFixture& fixture) {
    const std::optional<SemanticNode> number = read_node(window, "uia.number");
    if (!number) return;
    expect(checks,
           (*number).numeric_value.has_value() && (*number).minimum_value == 0.0 && (*number).maximum_value == 10.0,
           "the numeric field publishes its value and range");
    expect(checks,
           perform_on_control(window, *fixture.number, SemanticAction::set_value, "7") &&
               (*fixture.number).value() == 7.0,
           "set_value 7 sets the numeric field");
    const std::optional<SemanticNode> seven = read_node(window, "uia.number");
    expect(checks, seven && (*seven).numeric_value == 7.0, "the node's numeric value is 7");
    // The Windows adapter refuses a value past the maximum before it reaches
    // the control (the provider test checks that); the control itself never
    // holds one.
    static_cast<void>(perform_on_control(window, *fixture.number, SemanticAction::set_value, "11"));
    expect(checks, (*fixture.number).value() <= 10.0, "a value past the maximum is never held");
}

void check_provider_selection(Checks& checks, Window& window, ProviderFixture& fixture) {
    const std::optional<SemanticNode> list = read_node(window, "uia.list");
    if (!list || (*list).children.size() < 2U) {
        expect(checks, false, "the list publishes at least two items");
        return;
    }
    const SemanticNode second = (*list).children[1];
    expect(checks, second.role == SemanticRole::list_item && second.name == "Beta" && !second.stable_id.empty(),
           "a list item's role, name and stable ID");
    expect(checks, has_action(second, SemanticAction::select), "a list item publishes select");
    expect(checks,
           perform_on_child(window, *fixture.list, second.stable_id, SemanticAction::select) &&
               (*fixture.list).selected_index() == 1U,
           "select selects the item");
    const std::optional<SemanticNode> after = read_node(window, "uia.list");
    if (!after) return;
    std::size_t selected = 0;
    bool second_selected = false;
    for (std::size_t index = 0; index < (*after).children.size(); ++index) {
        if (!has_state((*after).children[index], SemanticState::selected)) continue;
        ++selected;
        if (index == 1U) second_selected = true;
    }
    expect(checks, second_selected, "the selected item publishes selected");
    expect(checks, selected == 1U, "the list holds the one selected item");
}

void check_provider_expand(Checks& checks, Window& window, ProviderFixture& fixture) {
    const std::optional<SemanticNode> folder = read_node(window, "uia.tree.folder");
    if (!folder) {
        expect(checks, false, "the tree's folder publishes a node");
        return;
    }
    expect(checks, has_action(*folder, SemanticAction::expand), "the folder publishes expand");
    expect(checks, !has_state(*folder, SemanticState::expanded) && !(*fixture.tree).expanded("uia.tree.folder"),
           "the folder starts collapsed");
    expect(checks,
           perform_on_child(window, *fixture.tree, "uia.tree.folder", SemanticAction::expand) &&
               (*fixture.tree).expanded("uia.tree.folder"),
           "expand expands the folder");
    const std::optional<SemanticNode> expanded = read_node(window, "uia.tree.folder");
    expect(checks, expanded && has_state(*expanded, SemanticState::expanded) &&
                       has_action(*expanded, SemanticAction::collapse),
           "the expanded folder publishes expanded, and collapse");
    expect(checks,
           perform_on_child(window, *fixture.tree, "uia.tree.folder", SemanticAction::collapse) &&
               !(*fixture.tree).expanded("uia.tree.folder"),
           "collapse collapses it again");
}

void check_provider_stale(Checks& checks, Window& window, ProviderFixture& fixture) {
    const std::optional<SemanticNode> doomed = read_node(window, "uia.doomed");
    expect(checks, doomed && has_action(*doomed, SemanticAction::press), "the button to remove publishes press");
    const RuntimeId identity = (*fixture.doomed).runtime_id();
    static_cast<void>((*fixture.root).remove_child(identity));
    expect(checks, (*fixture.doomed).attached_window() == nullptr, "the button left the window");
    expect(checks, !read_node(window, "uia.doomed"), "a removed control publishes no node");
    expect(checks, !window.perform_semantic_action(identity, std::string_view(), SemanticAction::press),
           "an action on a removed control's runtime ID is refused");
    expect(checks, !window.perform_semantic_action("uia.doomed", SemanticAction::press),
           "an action on a removed control's stable ID is refused");
}

void run_provider_fixture(Checks& checks) {
    ProviderFixture fixture;
    std::unique_ptr<Window> window = make_provider_fixture(fixture);
    (*window).perform_layout();
    check_provider_identity(checks, *window);
    check_provider_rename(checks, *window, fixture);
    check_provider_press(checks, *window, fixture);
    check_provider_toggle(checks, *window, fixture);
    check_provider_value(checks, *window, fixture);
    check_provider_range(checks, *window, fixture);
    check_provider_selection(checks, *window, fixture);
    check_provider_expand(checks, *window, fixture);
    check_provider_stale(checks, *window, fixture);
}

// -- the UI Automation text fixture -----------------------------------------

struct TextFixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<TextBox> single;
    std::shared_ptr<TextBox> multi;
    std::shared_ptr<TextBox> password;
    std::shared_ptr<TextBox> doomed;
};

// The text test's fixture (make_fixture there).
std::unique_ptr<Window> make_text_fixture(TextFixture& fixture) {
    fixture.root = make_control<Panel>(StableId("uiatext.root"));
    fixture.single = make_control<TextBox>(StableId("uiatext.single"));
    fixture.multi = make_control<TextBox>(StableId("uiatext.multi"));
    fixture.password = make_control<TextBox>(StableId("uiatext.password"));
    fixture.doomed = make_control<TextBox>(StableId("uiatext.doomed"));
    (*fixture.single).set_requested_bounds({10.0, 10.0, 260.0, 26.0});
    (*fixture.single).set_text(std::string(single_text));
    (*fixture.multi).set_requested_bounds({10.0, 50.0, 260.0, 80.0});
    (*fixture.multi).set_multiline(true);
    (*fixture.multi).set_text(std::string(multi_text));
    (*fixture.password).set_requested_bounds({10.0, 140.0, 260.0, 26.0});
    (*fixture.password).set_use_system_password_character(true);
    (*fixture.password).set_text("secret");
    (*fixture.doomed).set_requested_bounds({10.0, 180.0, 260.0, 26.0});
    (*fixture.doomed).set_text("Removed");
    (*fixture.root).add_child(fixture.single);
    (*fixture.root).add_child(fixture.multi);
    (*fixture.root).add_child(fixture.password);
    (*fixture.root).add_child(fixture.doomed);
    std::unique_ptr<Window> window = std::make_unique<Window>(fixture.root, Size{300.0, 220.0});
    return window;
}

bool boundaries_are(const std::vector<Utf8Offset>& boundaries, std::initializer_list<std::size_t> wanted) {
    if (boundaries.size() != wanted.size()) return false;
    std::size_t index = 0;
    for (const std::size_t offset : wanted) {
        if (boundaries[index].value() != offset) return false;
        ++index;
    }
    return true;
}

void check_text_offered(Checks& checks, Window& window, TextFixture& fixture) {
    const std::optional<SemanticNode> single = read_node(window, "uiatext.single");
    expect(checks,
           single && (*single).role == SemanticRole::text_box && (*single).text_selection.has_value() &&
               !has_state(*single, SemanticState::protected_content),
           "a text box publishes a text selection (its text can be read)");
    const std::optional<SemanticNode> password = read_node(window, "uiatext.password");
    expect(checks,
           password && has_state(*password, SemanticState::protected_content) &&
               !(*password).text_selection.has_value(),
           "a password box is protected and publishes no text selection");
    SemanticTextLayout layout;
    expect(checks,
           window.semantic_text_layout((*fixture.password).runtime_id(), layout) == SemanticTextResult::no_text,
           "a password box answers no_text to a layout query");
}

void check_text_document(Checks& checks, Window& window) {
    const std::optional<SemanticNode> single = read_node(window, "uiatext.single");
    if (!single) return;
    expect(checks, (*single).value == single_text, "the node's value is the text box's text");
    expect(checks, !has_state(*single, SemanticState::read_only), "an editable box is not read-only");
}

void check_text_units(Checks& checks, Window& window, TextFixture& fixture) {
    SemanticTextLayout layout;
    std::vector<Utf8Offset> boundaries;
    expect(checks, window.semantic_text_layout((*fixture.single).runtime_id(), layout) == SemanticTextResult::done,
           "the single-line box answers a layout query");
    fill_semantic_text_boundaries(layout, single_text, SemanticTextUnit::character, boundaries);
    expect(checks, boundaries_are(boundaries, {0U, 1U, 2U, 3U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U}),
           "a combining sequence is one character (bytes 3 to 6)");
    fill_semantic_text_boundaries(layout, single_text, SemanticTextUnit::word, boundaries);
    expect(checks, boundaries_are(boundaries, {0U, 7U, 10U, 14U}),
           "words run to the next word start: \"Cafe\xCC\x81 \", \"au \", \"lait\"");
    fill_semantic_text_boundaries(layout, single_text, SemanticTextUnit::document, boundaries);
    expect(checks, boundaries_are(boundaries, {0U, 14U}), "the document unit is the whole text");
    expect(checks, layout.graphemes.size() > 1U && layout.graphemes[1].bounds.width > 0.0 &&
                       layout.graphemes[1].bounds.height > 0.0,
           "a character has a box with area");

    const std::string multi_value((*fixture.multi).text());
    expect(checks, window.semantic_text_layout((*fixture.multi).runtime_id(), layout) == SemanticTextResult::done,
           "the multiline box answers a layout query");
    expect(checks, multi_value == multi_text, "the multiline box holds its text");
    fill_semantic_text_boundaries(layout, multi_value, SemanticTextUnit::line, boundaries);
    expect(checks, boundaries_are(boundaries, {0U, 11U, 22U}), "the first line runs through its break");
    fill_semantic_text_boundaries(layout, multi_value, SemanticTextUnit::paragraph, boundaries);
    expect(checks, boundaries_are(boundaries, {0U, 11U, 22U}), "a paragraph holds its line feed");
    expect(checks,
           layout.lines.size() == 2U && layout.lines[0].bounds.width > 0.0 && layout.lines[0].bounds.height > 0.0 &&
               layout.lines[1].bounds.y > layout.lines[0].bounds.y,
           "the multiline box has one box per line, the second below the first");
}

void check_text_selection(Checks& checks, Window& window, TextFixture& fixture) {
    (*fixture.single).select(Utf8Offset{1U}, Utf8Offset{6U});
    const std::optional<SemanticNode> selected = read_node(window, "uiatext.single");
    expect(checks,
           selected && (*selected).text_selection &&
               (*(*selected).text_selection).anchor == Utf8Offset{1U} &&
               (*(*selected).text_selection).caret == Utf8Offset{6U},
           "the node publishes the model's selection and caret");
    const SemanticTextResult moved =
        window.set_semantic_text_selection((*fixture.single).runtime_id(), {Utf8Offset{7U}, Utf8Offset{10U}});
    const TextSelection model = (*fixture.single).selection();
    expect(checks, moved == SemanticTextResult::done && model.anchor.value() == 7U && model.caret.value() == 10U,
           "a host's selection sets the model's, anchored at its start");
    const std::optional<SemanticNode> after = read_node(window, "uiatext.single");
    expect(checks,
           after && (*after).text_selection && (*(*after).text_selection).anchor == Utf8Offset{7U} &&
               (*(*after).text_selection).caret == Utf8Offset{10U},
           "the snapshot follows the host's selection");
}

void check_text_changes(Checks& checks, Window& window, TextFixture& fixture) {
    const std::uint64_t before = window.semantic_snapshot().generation;
    (*fixture.single).select(Utf8Offset{0U}, Utf8Offset{2U});
    const SemanticSnapshot selected = window.semantic_snapshot();
    const SemanticNode* node = find_node(selected.roots, "uiatext.single");
    expect(checks,
           node != nullptr && (*node).text_selection && (*(*node).text_selection).caret == Utf8Offset{2U} &&
               selected.generation != before,
           "a selection change shows in the next snapshot, with a new generation");
    (*fixture.single).set_text("Changed");
    const SemanticSnapshot changed = window.semantic_snapshot();
    const SemanticNode* text = find_node(changed.roots, "uiatext.single");
    expect(checks,
           text != nullptr && (*text).value == "Changed" && changed.generation != selected.generation,
           "a text change shows in the next snapshot, with a new generation");
}

void check_text_stale(Checks& checks, Window& window, TextFixture& fixture) {
    const RuntimeId identity = (*fixture.doomed).runtime_id();
    SemanticTextLayout layout;
    expect(checks, window.semantic_text_layout(identity, layout) == SemanticTextResult::done,
           "the text box to remove answers a layout query");
    static_cast<void>((*fixture.root).remove_child(identity));
    expect(checks, (*fixture.doomed).attached_window() == nullptr, "the text box left the window");
    expect(checks, window.semantic_text_layout(identity, layout) == SemanticTextResult::stale_identity,
           "a removed text box's runtime ID answers stale_identity");
}

void run_text_fixture(Checks& checks) {
    TextFixture fixture;
    std::unique_ptr<Window> window = make_text_fixture(fixture);
    (*window).perform_layout();
    check_text_offered(checks, *window, fixture);
    check_text_document(checks, *window);
    check_text_units(checks, *window, fixture);
    check_text_selection(checks, *window, fixture);
    check_text_changes(checks, *window, fixture);
    check_text_stale(checks, *window, fixture);
}

} // namespace

int main() {
    try {
        Checks checks;
        run_provider_fixture(checks);
        run_text_fixture(checks);
        if (checks.failed != 0) {
            std::cerr << "gui_forms_semantic_fixture_tests: " << checks.failed << " of "
                      << checks.passed + checks.failed << " checks failed\n";
            return EXIT_FAILURE;
        }
        std::cout << "gui_forms_semantic_fixture_tests: " << checks.passed << " checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_semantic_fixture_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
