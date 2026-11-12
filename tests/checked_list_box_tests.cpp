#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/check_box_values.hpp"
#include "gui_forms/controls/checked_list_box.hpp"
#include "gui_forms/controls/list_box_values.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++fills; }
    void stroke_rect(Rect, Color, double) override { ++strokes; }
    void draw_line(Point, Point, Color, double) override { ++lines; }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec, Color) override {
        origins.push_back(origin);
        texts.emplace_back(text);
    }
    void draw_image(ImageId, Rect, double) override {}

    std::uint64_t fills{};
    std::uint64_t strokes{};
    std::uint64_t lines{};
    std::vector<Point> origins;
    std::vector<std::string> texts;
};

Point row_point(const CheckedListBox& list, std::size_t index) {
    const Rect bounds = list.absolute_bounds();
    return {bounds.x + 12.0,
            bounds.y + 2.0 +
                (static_cast<double>(index - list.top_index.read()) + 0.5) *
                    list.item_height.read()};
}

class ObserveItemChecking final {
public:
    ObserveItemChecking(std::string& order,
                        std::shared_ptr<CheckedListBox> list)
        : order_(order), list_(std::move(list)) {}

    void operator()(ItemCheckEvent& event) const {
        order_ += "before:" + std::to_string(event.index) + ":" +
            std::to_string(static_cast<int>(
                (*list_).item_check_state(event.index))) + "\n";
        if (event.index == 1U) {
            event.cancel = true;
        }
        if (event.index == 2U) {
            event.new_state = CheckState::checked;
        }
    }

private:
    std::string& order_;
    std::shared_ptr<CheckedListBox> list_;
};

class ObserveCommittedCheckState final {
public:
    ObserveCommittedCheckState(std::string& order,
                               std::shared_ptr<CheckedListBox> list)
        : order_(order), list_(std::move(list)) {}

    void operator()(std::size_t index, CheckState state) const {
        require((*list_).item_check_state(index) == state,
                "post-check event must observe committed state");
        order_ += "after:" + std::to_string(index) + ":" +
            std::to_string(static_cast<int>(state)) + "\n";
    }

private:
    std::string& order_;
    std::shared_ptr<CheckedListBox> list_;
};

void click_row(Window& window, const CheckedListBox& list, std::size_t index) {
    const Point point = row_point(list, index);
    require(window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, point}) &&
                window.dispatch_pointer(
                    {PointerAction::up, PointerButton::primary, point}),
            "CheckedListBox row click must route through retained input");
}

void test_check_model_events_and_mutation() {
    std::shared_ptr<gui_forms::CheckedListBox> list = make_control<CheckedListBox>(StableId("checks"));
    (*list).add_item("Renderer", CheckState::checked);
    (*list).add_item("Host", CheckState::unchecked);
    (*list).add_item("Accessibility", CheckState::unchecked);
    (*list).add_item("Telemetry", CheckState::indeterminate);
    (*list).set_requested_bounds({0.0, 0.0, 260.0, 150.0});
    Window window(list, {260.0, 150.0});
    std::string order;
    SubscriptionToken checking = (*list).item_checking().subscribe(
        ObserveItemChecking(order, list));
    SubscriptionToken changed = (*list).item_check_state_changed().subscribe(
        ObserveCommittedCheckState(order, list));
    (*list).set_item_checked(1U, true);
    (*list).set_item_check_state(2U, CheckState::indeterminate);
    require(!(*list).item_checked(1U) && (*list).item_checked(2U) &&
                order == "before:1:0\nbefore:2:0\nafter:2:1\n",
            "ItemCheck must be cancellable/modifiable before one committed event");
    require((*list).checked_indices() == std::vector<std::size_t>{0U, 2U},
            "checked indices must remain independent of selection");

    (*list).items.erase(0U, 1U);
    require((*list).items.read().size() == 3U && (*list).item_checked(1U) &&
                (*list).item_check_state(2U) == CheckState::indeterminate,
            "item removal must remap check state with the item collection");
    (*list).items.clear();
    require((*list).items.read().empty() && (*list).checked_indices().empty(),
            "clearing items must atomically clear the check model");
}

void test_pointer_keyboard_and_disabled_policy() {
    std::shared_ptr<gui_forms::CheckedListBox> list = make_control<CheckedListBox>(StableId("checks.input"));
    (*list).items = {"First", "Second", "Third"};
    (*list).set_requested_bounds({0.0, 0.0, 240.0, 120.0});
    Window window(list, {240.0, 120.0});
    window.perform_layout();

    click_row(window, *list, 0U);
    require((*list).selected_index() == 0U && !(*list).item_checked(0U),
            "default first click must select without changing check state");
    click_row(window, *list, 0U);
    require((*list).item_checked(0U),
            "default second click on the selected item must toggle its check");
    (*list).check_on_click = true;
    click_row(window, *list, 1U);
    require((*list).selected_index() == 1U && (*list).item_checked(1U),
            "CheckOnClick must select and toggle on the first click");

    KeyEvent space{KeyAction::down, PhysicalKey::space};
    require(window.dispatch_key(space) && !(*list).item_checked(1U),
            "Space must toggle the active checked-list item");
    (*list).enabled = false;
    const Point third = row_point(*list, 2U);
    require(!window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, third}) &&
                !(*list).item_checked(2U),
            "disabled CheckedListBox must reject pointer check mutation");
}

void test_paint_and_semantic_actions() {
    std::shared_ptr<gui_forms::CheckedListBox> list = make_control<CheckedListBox>(StableId("checks.semantic"));
    (*list).accessible_name = "Feature checks";
    (*list).add_item("Checked", CheckState::checked);
    (*list).add_item("Mixed", CheckState::indeterminate);
    (*list).add_item("Empty", CheckState::unchecked);
    (*list).indicator_size = 20.0;
    (*list).set_requested_bounds({0.0, 0.0, 240.0, 120.0});
    Window window(list, {240.0, 120.0});
    window.perform_layout();
    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 240.0, 120.0});
    require(painter.texts.size() == 3U && painter.strokes >= 3U &&
                painter.lines >= 2U && painter.origins.front().x == 34.0,
            "CheckedListBox must paint check adornments and shifted row text");
    bool invalid_size{};
    try {
        (*list).indicator_size = 40.0;
    } catch (const std::out_of_range&) {
        invalid_size = true;
    }
    require(invalid_size && (*list).indicator_size.read() == 20.0,
            "CheckedListBox indicator size must reject invalid customization atomically");

    const std::string json = window.semantic_snapshot().to_json();
    require(json.find("\"role\":\"check_list_item\"") != std::string::npos &&
                json.find("\"value\":\"checked\"") != std::string::npos &&
                json.find("\"value\":\"indeterminate\"") != std::string::npos,
            "checked rows must publish stable checkable virtual semantics");
    require(window.perform_semantic_action("checks.semantic.item.2",
                                           SemanticAction::press) &&
                (*list).item_checked(2U) && (*list).selected_index() == 2U,
            "semantic press must select and toggle through public behavior");
}

} // namespace

void test_property_capabilities() {
    std::shared_ptr<gui_forms::CheckedListBox> list = make_control<CheckedListBox>(StableId("checks.caps"));
    require(!(*list).check_on_click.read() && (*list).indicator_size.read() == 14.0,
            "check_on_click starts off and the indicator at 14");
    (*list).check_on_click = true;
    (*list).indicator_size = 8.0;
    require((*list).check_on_click.read() && (*list).indicator_size.read() == 8.0,
            "both cells take a write within their bounds");
    bool refused{};
    try {
        (*list).indicator_size = 7.5;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*list).indicator_size.read() == 8.0, "an indicator under 8 is refused");
    (*list).item_height = 30.0;
    (*list).selection_mode = ListSelectionMode::multiple_extended;
    require((*list).item_height.read() == 30.0 &&
                (*list).selection_mode.read() == ListSelectionMode::multiple_extended,
            "ListBox's cells keep working through the derived list");
}

int main() {
    try {
        test_check_model_events_and_mutation();
        test_pointer_keyboard_and_disabled_policy();
        test_paint_and_semantic_actions();
        test_property_capabilities();
        std::cout << "gui_forms_checked_list_box_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_checked_list_box_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
