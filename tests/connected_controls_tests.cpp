#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(const double left, const double right) {
    return std::abs(left - right) < 0.0001;
}

SurfaceMaterial specimen_material() {
    SurfaceMaterial result;
    result.fills = {MaterialFillLayer::solid(Color::rgba(240, 245, 250))};
    result.shadows = {
        {{0.0, 2.0}, 3.0, 0.0, Color::rgba(10, 20, 30, 60), false},
    };
    result.border = MaterialBorder{Color::rgba(61, 94, 121), 1.0};
    result.corner_radius = 5.0;
    return result;
}

class GeometryPainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(Point) override {}
    void clip_rect(const Rect rect) override { clips.push_back(rect); }
    void clip_rounded_rect(const Rect rect, double) override {
        rounded_clips.push_back(rect);
    }
    void fill_rect(const Rect rect, Color) override { fills.push_back(rect); }
    void fill_rounded_rect(const Rect rect, double, Color) override {
        rounded_fills.push_back(rect);
    }
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(const Rect rect, double, Color, double) override {
        rounded_strokes.push_back(rect);
    }
    void draw_box_shadow(const Rect rect, double, Point, double, double,
                         Color) override {
        shadows.push_back(rect);
    }
    void draw_line(const Point from, const Point to, Color,
                   double width) override {
        lines.push_back({from, to, width});
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    struct Line final { Point from; Point to; double width; };
    std::size_t saves{};
    std::size_t restores{};
    std::vector<Rect> clips;
    std::vector<Rect> rounded_clips;
    std::vector<Rect> fills;
    std::vector<Rect> rounded_fills;
    std::vector<Rect> rounded_strokes;
    std::vector<Rect> shadows;
    std::vector<Line> lines;
};

void test_topology_and_resolved_geometry() {
    require(!valid_connected_control_topology(
                {ConnectedControlAxis::horizontal, 0U, 1U}) &&
                !valid_connected_control_topology(
                    {ConnectedControlAxis::vertical, 2U, 2U}) &&
                valid_connected_control_topology(
                    {ConnectedControlAxis::horizontal, 1U, 3U}),
            "connected topology must require count >= 2 and index < count");
    require(connected_control_segment(std::nullopt) ==
                ConnectedControlSegment::standalone &&
                connected_control_segment(ConnectedControlTopology{
                    ConnectedControlAxis::horizontal, 0U, 3U}) ==
                    ConnectedControlSegment::leading &&
                connected_control_segment(ConnectedControlTopology{
                    ConnectedControlAxis::horizontal, 1U, 3U}) ==
                    ConnectedControlSegment::middle &&
                connected_control_segment(ConnectedControlTopology{
                    ConnectedControlAxis::horizontal, 2U, 3U}) ==
                    ConnectedControlSegment::trailing,
            "topology positions must resolve deterministically");

    const SurfaceMaterial material = specimen_material();
    const Rect bounds{0.0, 0.0, 100.0, 30.0};
    const ConnectedControlVisualGeometry leading =
        resolve_connected_control_visual_geometry(
            bounds, material,
            ConnectedControlTopology{ConnectedControlAxis::horizontal, 0U, 3U});
    const ConnectedControlVisualGeometry middle =
        resolve_connected_control_visual_geometry(
            bounds, material,
            ConnectedControlTopology{ConnectedControlAxis::horizontal, 1U, 3U});
    const ConnectedControlVisualGeometry trailing =
        resolve_connected_control_visual_geometry(
            bounds, material,
            ConnectedControlTopology{ConnectedControlAxis::horizontal, 2U, 3U});
    require(leading.paint_bounds == Rect{0.0, 0.0, 105.0, 30.0} &&
                !leading.leading_seam &&
                middle.paint_bounds == Rect{-5.0, 0.0, 110.0, 30.0} &&
                middle.leading_seam && near((*middle.leading_seam).from.x, 0.5) &&
                trailing.paint_bounds == Rect{-5.0, 0.0, 105.0, 30.0} &&
                trailing.leading_seam,
            "joined geometry must remove interior corners and assign one leading seam");

    const Insets middle_outsets = connected_surface_visual_outsets(
        material,
        ConnectedControlTopology{ConnectedControlAxis::horizontal, 1U, 3U});
    require(middle_outsets.left == 0.0 && middle_outsets.right == 0.0 &&
                middle_outsets.top > 0.0 && middle_outsets.bottom > 0.0,
            "interior members must not claim clipped joined-edge shadow damage");

    GeometryPainter painter;
    paint_connected_surface_material(
        painter, bounds, material,
        ConnectedControlTopology{ConnectedControlAxis::horizontal, 1U, 3U});
    require(painter.saves == 2U && painter.restores == 2U &&
                painter.clips.size() == 1U && painter.clips.front() == bounds &&
                painter.rounded_clips.size() == 1U &&
                painter.rounded_clips.front() == middle.paint_bounds &&
                !painter.rounded_fills.empty() &&
                painter.rounded_fills.front() == middle.paint_bounds &&
                painter.lines.size() == 1U &&
                near(painter.lines.front().from.x, 0.5) &&
                near(painter.lines.front().to.x, 0.5),
            "connected material replay must clip one expanded surface and draw one seam");
}

struct PaintOperationMatches final {
    VisualPaintOperation operation;

    bool operator()(const VisualPaintOperationSnapshot& candidate) const {
        return candidate.operation == operation;
    }
};

struct InspectedOperationCounter final {
    Window& window;

    std::size_t operator()(const std::string_view id,
                           const VisualPaintOperation operation) const {
        const VisualInspectionSnapshot snapshot =
            window.visual_inspection_snapshot();
        const VisualControlInspection* control = snapshot.find(id);
        require(control != nullptr, "state trace target must remain inspectable");
        return static_cast<std::size_t>(std::count_if(
            (*control).display_operations.begin(),
            (*control).display_operations.end(),
            PaintOperationMatches{operation}));
    }
};

void test_atomic_grouping_hit_cracks_focus_and_semantics() {
    const std::shared_ptr<Panel> root = make_control<Panel>(
        StableId("connected.test.root"));
    const std::shared_ptr<Button> first = make_control<Button>(
        StableId("connected.test.first"), "First");
    const std::shared_ptr<CheckBox> middle = make_control<CheckBox>(
        StableId("connected.test.middle"), "Middle");
    const std::shared_ptr<DropDownButton> last = make_control<DropDownButton>(
        StableId("connected.test.last"), "Last", DropDownButtonMode::split);
    (*middle).appearance = CheckBoxAppearance::button;
    (*middle).checked = true;
    (*first).set_requested_bounds({0.0, 0.0, 100.0, 36.0});
    (*middle).set_requested_bounds({100.0, 0.0, 100.0, 36.0});
    (*last).set_requested_bounds({200.0, 0.0, 100.0, 36.0});
    (*first).tab_index = 0U;
    (*middle).tab_index = 1U;
    (*last).tab_index = 2U;
    (*root).add_child(first);
    (*root).add_child(middle);
    (*root).add_child(last);
    const std::array<std::shared_ptr<ButtonBase>, 3U> group{
        first, middle, last};
    connect_button_group(group, ConnectedControlAxis::horizontal);
    require((*first).connection_topology() ==
                ConnectedControlTopology{ConnectedControlAxis::horizontal, 0U, 3U} &&
                (*middle).connection_topology() ==
                ConnectedControlTopology{ConnectedControlAxis::horizontal, 1U, 3U} &&
                (*last).connection_topology() ==
                ConnectedControlTopology{ConnectedControlAxis::horizontal, 2U, 3U},
            "atomic helper must assign the explicit supplied order");

    const std::optional<ConnectedControlTopology> before =
        (*first).connection_topology();
    bool rejected_axis{};
    try {
        connect_button_group(group, ConnectedControlAxis::vertical);
    } catch (const std::invalid_argument&) {
        rejected_axis = true;
    }
    require(rejected_axis && (*first).connection_topology() == before &&
                (*(*middle).connection_topology()).axis ==
                    ConnectedControlAxis::horizontal &&
                (*(*last).connection_topology()).axis ==
                    ConnectedControlAxis::horizontal,
            "mixed-axis rejection must mutate no group member");

    const std::array<std::shared_ptr<ButtonBase>, 2U> duplicate{first, first};
    bool rejected_duplicate{};
    try {
        connect_button_group(duplicate, ConnectedControlAxis::horizontal);
    } catch (const std::invalid_argument&) {
        rejected_duplicate = true;
    }
    require(rejected_duplicate && (*first).connection_topology() == before,
            "duplicate-member rejection must be atomic");

    const std::shared_ptr<Panel> other_parent = make_control<Panel>(
        StableId("connected.test.other-parent"));
    const std::shared_ptr<Button> outsider = make_control<Button>(
        StableId("connected.test.outsider"), "Outsider");
    (*other_parent).add_child(outsider);
    const std::array<std::shared_ptr<ButtonBase>, 2U> mixed_parent{
        first, outsider};
    bool rejected_parent{};
    try {
        connect_button_group(mixed_parent, ConnectedControlAxis::horizontal);
    } catch (const std::invalid_argument&) {
        rejected_parent = true;
    }
    require(rejected_parent && (*first).connection_topology() == before &&
                !(*outsider).connection_topology(),
            "mixed-parent rejection must mutate no group member");

    Window window(root, {300.0, 80.0});
    window.perform_layout();
    require(window.hit_test({99.999, 18.0}) == first &&
                window.hit_test({100.0, 18.0}) == middle &&
                window.hit_test({199.999, 18.0}) == middle &&
                window.hit_test({200.0, 18.0}) == last,
            "joined visual seams must leave neither overlapping nor dead hit cracks");
    require(window.request_focus(first) && window.focused_control() == first &&
                window.move_focus(true) && window.focused_control() == middle &&
                window.move_focus(true) && window.focused_control() == last,
            "joined controls must retain independent keyboard focus stops");
    const SemanticDescriptor first_semantics = (*first).semantic_descriptor();
    const SemanticDescriptor middle_semantics = (*middle).semantic_descriptor();
    const SemanticDescriptor last_semantics = (*last).semantic_descriptor();
    require(first_semantics.role == SemanticRole::button &&
                middle_semantics.role == SemanticRole::check_box &&
                has_semantic_state(middle_semantics.states, SemanticState::checked) &&
                last_semantics.role == SemanticRole::split_button && last_semantics.has_popup &&
                std::find(last_semantics.actions.begin(), last_semantics.actions.end(),
                          SemanticAction::show_menu) != last_semantics.actions.end(),
            "connection must preserve separate roles, checked state, and disclosure actions");

    bool rejected_invalid{};
    try {
        (*first).set_connection_topology(
            ConnectedControlTopology{ConnectedControlAxis::horizontal, 2U, 2U});
    } catch (const std::invalid_argument&) {
        rejected_invalid = true;
    }
    require(rejected_invalid && (*first).connection_topology() == before,
            "invalid direct topology must be rejected before mutation");

    GeometryPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 300.0, 80.0}));
    const InspectedOperationCounter operation_count{window};
    require(operation_count("connected.test.first",
                            VisualPaintOperation::draw_line) == 0U &&
                operation_count("connected.test.middle",
                                VisualPaintOperation::draw_line) == 1U,
            "normal/checked state must retain zero leading seams and exactly one interior seam");

    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, {150.0, 18.0}}));
    require((*middle).hovered_visual(),
            "hover must remain local to one joined segment");
    static_cast<void>(window.paint(painter, window.take_damage().bounds()));
    require(operation_count("connected.test.middle",
                            VisualPaintOperation::draw_line) == 1U,
            "hover state must not duplicate its physical seam");
    require(window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary,
                 {150.0, 18.0}}) &&
                (*middle).pressed_visual(),
            "press must remain local to one joined segment");
    static_cast<void>(window.paint(painter, window.take_damage().bounds()));
    require(operation_count("connected.test.middle",
                            VisualPaintOperation::draw_line) == 1U,
            "pressed state must not duplicate its physical seam");
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::up, PointerButton::primary, {150.0, 18.0}}));
    (*middle).enabled = false;
    static_cast<void>(window.paint(painter, window.take_damage().bounds()));
    require(operation_count("connected.test.middle",
                            VisualPaintOperation::draw_line) == 1U,
            "disabled interior state must retain one shared seam");
    (*first).default_button = true;
    static_cast<void>(window.request_focus(first));
    static_cast<void>(window.paint(painter, window.take_damage().bounds()));
    require(operation_count("connected.test.first",
                            VisualPaintOperation::draw_line) == 0U &&
                operation_count("connected.test.first",
                                VisualPaintOperation::stroke_rounded_rect) >= 1U,
            "default/focus cues must remain per control without inventing a leading seam");
}

} // namespace

int main() {
    try {
        test_topology_and_resolved_geometry();
        test_atomic_grouping_hit_cracks_focus_and_semantics();
        std::cout << "gui_forms_connected_controls_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_connected_controls_tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
