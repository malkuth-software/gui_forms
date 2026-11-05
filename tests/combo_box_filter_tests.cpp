// The searchable, filtering ComboBox (stage W, matrix row 56): in the
// drop_down style, typing opens the list on the items that match by prefix
// or substring; the rows keep their items' identities (selection, keys,
// pointer and semantic rows); the matched text is a published `match` part;
// auto-complete stands aside while a filter is set. Also the typed prefix a
// TextBox's suggestion list highlights.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct TraceDropDown final {
    std::vector<std::string>* trace{};
    void operator()(bool open) const { (*trace).push_back(open ? "drop" : "fold"); }
};
struct TraceIndex final {
    std::vector<std::string>* trace{};
    void operator()(std::optional<std::size_t> index) const {
        (*trace).push_back(index ? "index " + std::to_string(*index) : std::string("index none"));
    }
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ComboBox> combo;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* id, ComboBoxFilter filter) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.combo = make_control<ComboBox>(StableId(std::string(id)));
    (*fixture.combo).set_requested_bounds({10.0, 10.0, 220.0, 30.0});
    (*fixture.combo).set_items({"Calgary", "California", "Cambridge", "Dover", "Cape Town", "Arcadia"});
    (*fixture.combo).set_drop_down_style(ComboBoxStyle::drop_down);
    (*fixture.combo).set_drop_down_filter(filter);
    (*fixture.root).add_child(fixture.combo);
    fixture.window = std::make_unique<Window>(fixture.root, Size{320.0, 320.0});
    (*fixture.window).perform_layout();
    require((*fixture.window).request_focus((*fixture.combo).editor()), "the editor takes the focus");
    return fixture;
}

bool type(Window& window, const char* text) {
    TextInputEvent input{};
    input.text_utf8 = text;
    const bool handled = window.dispatch_text(input);
    return handled;
}

bool press(Window& window, std::uint32_t key, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, key, modifiers};
    const bool handled = window.dispatch_key(event);
    return handled;
}

bool rows_are(const ComboBox& combo, std::vector<std::size_t> expected) {
    const std::span<const std::size_t> rows = combo.drop_down_items();
    if (rows.size() != expected.size()) return false;
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        if (rows[index] != expected[index]) return false;
    }
    return true;
}

// The named part of a realized row, read through the generic item path.
PartGeometry item_part(const Control& list, std::size_t item, std::string_view name) {
    const ItemSystemDeclaration& system = list.anatomy().item_systems.front();
    std::vector<PartGeometry> geometry(system.parts.size());
    list.fill_realized_item_geometry(item, geometry);
    for (std::size_t index = 0U; index < system.parts.size(); ++index) {
        if (system.parts[index].name == name) return geometry[index];
    }
    return {};
}

void prefix_filter_narrows_and_widens() {
    Fixture fixture = make_fixture("filter.prefix", ComboBoxFilter::prefix);
    Window& window = *fixture.window;
    ComboBox& combo = *fixture.combo;
    std::vector<std::string> trace;
    SubscriptionToken drops = combo.drop_down_changed().subscribe(TraceDropDown{&trace});
    require(combo.drop_down_filter() == ComboBoxFilter::prefix && combo.drop_down_items().empty(),
            "a closed list shows no rows");
    require(type(window, "ca") && combo.dropped_down() && rows_are(combo, {0U, 1U, 2U, 4U}),
            "typing opens the list on the items that start with the text, ignoring case");
    require(trace.size() == 1U && trace[0] == "drop" && window.focus_scope_depth() == 0U &&
                window.focused_control() == combo.editor(),
            "the filtered list opens once and the editor keeps the focus");
    const Control::Ptr list = window.find("filter.prefix.overlay.list");
    require(list != nullptr && (*list).anatomy().control_id == "drop_down_list",
            "the rows are a drop-down list");
    require((*list).realized_item_stable_id(3U) == "filter.prefix.overlay.list.item.4",
            "a row's identity is its item's, not its position");
    window.perform_layout();
    const PartGeometry match = item_part(*list, 0U, "match");
    const PartGeometry label = item_part(*list, 0U, "label");
    require(match.present && match.states.contains(AnatomyState::hot) &&
                match.bounds.x >= label.bounds.x && match.bounds.width > 0.0 &&
                match.bounds.width < label.bounds.width,
            "the matched text is a hot match part inside the row's label");
    require(type(window, "m") && rows_are(combo, {2U}) && combo.dropped_down(), "more text narrows the rows");
    require(press(window, PhysicalKey::backspace) && rows_are(combo, {0U, 1U, 2U, 4U}),
            "deleting widens them again");
    require(type(window, "x") && !combo.dropped_down() && combo.drop_down_items().empty() &&
                trace.size() == 2U && trace[1] == "fold",
            "text that matches nothing closes the list");
    require(press(window, PhysicalKey::backspace) && combo.dropped_down() && rows_are(combo, {0U, 1U, 2U, 4U}),
            "text that matches again reopens it");
    require(press(window, PhysicalKey::escape) && !combo.dropped_down() && combo.text() == "ca",
            "Escape closes the list and keeps the text");
    require(press(window, PhysicalKey::f4) && combo.dropped_down() && combo.drop_down_items().size() == 6U,
            "F4 opens the list on every item");
    const Control::Ptr full = window.find("filter.prefix.overlay.list");
    window.perform_layout();
    require(!item_part(*full, 0U, "match").present, "an unfiltered list highlights nothing");
    require(press(window, PhysicalKey::escape), "closed");
}

void substring_filter() {
    Fixture fixture = make_fixture("filter.substring", ComboBoxFilter::substring);
    Window& window = *fixture.window;
    ComboBox& combo = *fixture.combo;
    require(type(window, "ca") && rows_are(combo, {0U, 1U, 2U, 4U, 5U}),
            "a substring filter also shows items that contain the text");
    const Control::Ptr list = window.find("filter.substring.overlay.list");
    window.perform_layout();
    const PartGeometry first = item_part(*list, 0U, "match");
    const PartGeometry last = item_part(*list, 4U, "match");
    require(first.present && last.present && last.bounds.x > first.bounds.x,
            "the match is where the text occurs: inside \"Arcadia\", at its start in \"Calgary\"");
    require(type(window, "di") && rows_are(combo, {5U}), "the rows narrow to the one containing the text");
}

void keyboard_and_identity() {
    Fixture fixture = make_fixture("filter.keys", ComboBoxFilter::prefix);
    Window& window = *fixture.window;
    ComboBox& combo = *fixture.combo;
    std::vector<std::string> trace;
    SubscriptionToken indices = combo.selected_index_changed().subscribe(TraceIndex{&trace});
    require(type(window, "ca") && rows_are(combo, {0U, 1U, 2U, 4U}), "filtered");
    require(press(window, PhysicalKey::down) && combo.selected_index() == 0U && combo.text() == "Calgary" &&
                rows_are(combo, {0U, 1U, 2U, 4U}),
            "Down chooses the first row; showing its text does not filter again");
    require(press(window, PhysicalKey::down) && press(window, PhysicalKey::down) && press(window, PhysicalKey::down) &&
                combo.selected_index() == 4U && combo.text() == "Cape Town",
            "Down moves through the filtered rows only, skipping Dover");
    require(press(window, PhysicalKey::down) && combo.selected_index() == 4U, "the last row stays chosen");
    require(press(window, PhysicalKey::up) && combo.selected_index() == 2U, "Up moves back one row");
    const Control::Ptr list = window.find("filter.keys.overlay.list");
    require((*std::static_pointer_cast<ListBox>(list)).selected_index() == 2U,
            "the list highlights the chosen item's row");
    window.perform_layout();
    require(!item_part(*list, 2U, "match").present && item_part(*list, 1U, "match").present,
            "the selected row shows no separate match");

    // The items change under the open list: the rows and the selection
    // follow the item, not its old position.
    combo.set_items({"Arcadia", "Cambridge", "Calgary", "Cape Town"});
    require(combo.selected_index() == 1U && combo.text() == "Cambridge" && rows_are(combo, {1U, 2U, 3U}) &&
                (*std::static_pointer_cast<ListBox>(window.find("filter.keys.overlay.list"))).selected_index() == 0U,
            "new items keep the filter and the chosen item's row");
    require(press(window, PhysicalKey::enter) && !combo.dropped_down() && combo.selected_index() == 1U,
            "Enter keeps the chosen item and closes the list");
    require(trace.back() == "index 1", "the selection is reported by item index");
}

void pointer_and_semantic_rows() {
    Fixture fixture = make_fixture("filter.rows", ComboBoxFilter::prefix);
    Window& window = *fixture.window;
    ComboBox& combo = *fixture.combo;
    require(type(window, "ca") && combo.dropped_down(), "filtered");
    window.perform_layout();
    const Rect list = (*window.find("filter.rows.overlay.list")).absolute_bounds();
    const Point fourth{list.x + 20.0, list.y + 2.0 + 3.0 * 26.0 + 13.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, fourth}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, fourth}) &&
                !combo.dropped_down() && combo.selected_index() == 4U && combo.text() == "Cape Town",
            "clicking the fourth filtered row chooses its item");
    (*combo.editor()).select_all();
    require(type(window, "cam") && rows_are(combo, {2U}), "filtered again");
    require(window.perform_semantic_action("filter.rows.overlay.list.item.2", SemanticAction::press) &&
                !combo.dropped_down() && combo.selected_index() == 2U && combo.text() == "Cambridge",
            "a semantic press on a row addresses it by its item's identity");
}

void filter_replaces_auto_complete() {
    Fixture fixture = make_fixture("filter.complete", ComboBoxFilter::prefix);
    Window& window = *fixture.window;
    ComboBox& combo = *fixture.combo;
    combo.set_auto_complete_source(AutoCompleteSource::list_items);
    combo.set_auto_complete_mode(AutoCompleteMode::suggest_append);
    require((*combo.editor()).auto_complete_mode() == AutoCompleteMode::none,
            "while a filter is set the editor does not auto-complete");
    require(type(window, "c") && combo.text() == "c" && !(*combo.editor()).auto_complete_open() &&
                combo.dropped_down(),
            "the filtered list shows the matches; nothing is appended");
    combo.set_drop_down_filter(ComboBoxFilter::none);
    require(!combo.dropped_down() && (*combo.editor()).auto_complete_mode() == AutoCompleteMode::suggest_append,
            "clearing the filter closes the list and restores auto-complete");
    require(type(window, "a") && combo.text() == "calgary", "auto-complete acts again");

    combo.set_drop_down_style(ComboBoxStyle::drop_down_list);
    combo.set_drop_down_filter(ComboBoxFilter::substring);
    require(combo.drop_down_filter() == ComboBoxFilter::substring && !combo.editor(),
            "the filter is kept in the drop_down_list style");
    combo.set_dropped_down(true);
    require(combo.drop_down_items().size() == 6U, "a drop-down list always shows every item");
    combo.set_dropped_down(false);
}

void suggestions_highlight_the_typed_prefix() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("suggest.root"));
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("suggest.box"));
    (*box).set_requested_bounds({10.0, 10.0, 200.0, 28.0});
    (*root).add_child(box);
    (*(*box).auto_complete_custom_source()).add_range({"Calgary", "Cambridge"});
    (*box).set_auto_complete_source(AutoCompleteSource::custom_source);
    (*box).set_auto_complete_mode(AutoCompleteMode::suggest);
    Window window(root, Size{320.0, 300.0});
    window.perform_layout();
    require(window.request_focus(box) && type(window, "cal") && (*box).auto_complete_open(), "suggesting");
    const Control::Ptr list = window.find("suggest.box.auto-complete.list");
    window.perform_layout();
    const PartGeometry match = item_part(*list, 0U, "match");
    const PartGeometry label = item_part(*list, 0U, "label");
    require(match.present && match.bounds.x == label.bounds.x && match.bounds.width < label.bounds.width,
            "a suggestion highlights the typed prefix");
}

} // namespace

int main() {
    prefix_filter_narrows_and_widens();
    substring_filter();
    keyboard_and_identity();
    pointer_and_semantic_rows();
    filter_replaces_auto_complete();
    suggestions_highlight_the_typed_prefix();
    std::cout << "combo box filter tests passed\n";
    return 0;
}
