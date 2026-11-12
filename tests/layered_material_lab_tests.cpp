#include "layered_material_lab.hpp"

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/typography/types/typography_types.hpp"
#include "gui_forms/visual_inspection/types/visual_inspection_types.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
namespace lab = gui_forms::layered_material_lab;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

class EstimatedMetrics final : public TextMetricsProvider {
public:
    ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view text, FontSpec font) override {
        return estimate_text_layout_utf8(text, font);
    }
};

std::size_t operation_count(const VisualControlInspection& control,
                            VisualPaintOperation operation) {
    std::size_t result{};
    for (const VisualPaintOperationSnapshot& candidate :
         control.display_operations) {
        if (candidate.operation == operation) ++result;
    }
    return result;
}

// Whether the control's material was baked: one part raster and none of
// the layers' fills, shadows or lines.
bool baked(const VisualControlInspection& control) {
    const bool one_raster = operation_count(control, VisualPaintOperation::draw_part_raster) == 1U;
    const bool no_layers = operation_count(control, VisualPaintOperation::fill_linear_gradient_spread) == 0U &&
                           operation_count(control, VisualPaintOperation::fill_radial_gradient) == 0U &&
                           operation_count(control, VisualPaintOperation::draw_box_shadow) == 0U &&
                           operation_count(control, VisualPaintOperation::draw_line) == 0U;
    return one_raster && no_layers;
}

std::vector<const VisualPaintOperationSnapshot*> operations(
    const VisualControlInspection& control, VisualPaintOperation operation) {
    std::vector<const VisualPaintOperationSnapshot*> result;
    for (const VisualPaintOperationSnapshot& candidate :
         control.display_operations) {
        if (candidate.operation == operation) result.push_back(&candidate);
    }
    return result;
}

void test_reference_recipes_are_exact_and_bounded() {
    const SurfaceMaterial watercolor = lab::watercolor_fresco_material();
    const SurfaceMaterial office = lab::office_pearl_material();
    const SurfaceMaterial graphite = lab::workshop_graphite_material();
    require(valid_surface_material(watercolor) &&
                watercolor.fills.size() == 4U &&
                watercolor.shadows.size() == 2U &&
                watercolor.keylines.size() == 2U &&
                watercolor.keylines[0].edge == MaterialEdge::bottom &&
                watercolor.keylines[0].inset == 0.0 &&
                watercolor.keylines[1].edge == MaterialEdge::bottom &&
                watercolor.keylines[1].inset == 2.0,
            "Watercolor recipe must retain three glows, dark outer edge, and inset lower specular keyline");
    require(valid_surface_material(office) && office.fills.size() == 2U &&
                office.fills[0].spread == GradientSpreadMode::pad &&
                office.fills[1].spread == GradientSpreadMode::reflect &&
                office.shadows.size() == 2U &&
                office.shadows[0].inset == false &&
                office.shadows[1].inset == true,
            "Office Pearl must retain pad/reflect paint and distinct inset/outset depth");
    require(valid_surface_material(graphite) &&
                graphite.fills.size() == 2U &&
                graphite.fills[1].spread == GradientSpreadMode::repeat &&
                graphite.fills[1].coordinate_space ==
                    MaterialCoordinateSpace::logical &&
                graphite.keylines.size() == 4U,
            "Workshop Graphite must use one bounded logical-period texture and physical edge keylines");
    require(lab::over_budget_recipe_is_rejected(),
            "the lab must visibly and deterministically reject an over-budget material");

    const ControlStateRecipes recipes = lab::office_pearl_state_recipes();
    require(valid_control_state_recipes(recipes) &&
                recipes.resolve(ControlSurfaceState::hot).material !=
                    recipes.resolve(ControlSurfaceState::normal).material &&
                recipes.resolve(ControlSurfaceState::pressed).material !=
                    recipes.resolve(ControlSurfaceState::hot).material &&
                recipes.resolve(ControlSurfaceState::disabled).text !=
                    recipes.resolve(ControlSurfaceState::normal).text,
            "normal/hot/pressed/disabled states must resolve to distinct retained recipes");
}

void test_lab_trace_inspection_and_complexity() {
    std::unique_ptr<Window> window = lab::make_layered_material_lab();
    NullPainter painter;
    static_cast<void>((*window).paint(
        painter, {0.0, 0.0, 1400.0, 760.0}));
    const VisualInspectionSnapshot snapshot =
        (*window).visual_inspection_snapshot();
    const VisualControlInspection* watercolor =
        snapshot.find("layered-material.watercolor");
    const VisualControlInspection* office =
        snapshot.find("layered-material.office");
    const VisualControlInspection* graphite =
        snapshot.find("layered-material.graphite");
    const VisualControlInspection* studio =
        snapshot.find("layered-material.studio");
    const VisualControlInspection* live =
        snapshot.find("layered-material.live-state");
    require(watercolor && office && graphite && studio && live,
            "material lab must retain every named reference specimen");
    // A material within the bake budget is one raster from the material
    // kernel (one rasteriser, stage T4); past it, its layers.
    require((*watercolor).authored_material &&
                (*(*watercolor).authored_material).keylines.size() == 2U &&
                (baked(*watercolor) ||
                 (operation_count(*watercolor,
                                  VisualPaintOperation::fill_radial_gradient) == 3U &&
                  operation_count(*watercolor,
                                  VisualPaintOperation::draw_line) == 2U)),
            "Watercolor inspection must expose authored layers and committed keylines");
    const std::vector<const VisualPaintOperationSnapshot*> watercolor_lines =
        operations(*watercolor, VisualPaintOperation::draw_line);
    require(baked(*watercolor) || (watercolor_lines.size() == 2U &&
                (*watercolor_lines[0]).color == Color::rgba(23, 45, 105) &&
                (*watercolor_lines[0]).first == Point{0.0, 165.5} &&
                (*watercolor_lines[0]).second == Point{380.0, 165.5} &&
                (*watercolor_lines[1]).color ==
                    Color::rgba(215, 242, 255, 196) &&
                (*watercolor_lines[1]).first == Point{2.0, 163.5} &&
                (*watercolor_lines[1]).second == Point{378.0, 163.5} &&
                (*watercolor_lines[0]).command_index <
                    (*watercolor_lines[1]).command_index),
            "committed command inspection must preserve outer-before-inset authored keyline order and distinct geometry");
    require((*office).authored_material &&
                (baked(*office) ||
                 (operation_count(*office,
                      VisualPaintOperation::fill_linear_gradient_spread) == 2U &&
                  operation_count(*office,
                      VisualPaintOperation::draw_box_shadow) == 1U &&
                  operation_count(*office,
                      VisualPaintOperation::draw_inset_box_shadow) == 1U)),
            "Office inspection must expose both gradient spreads and both shadow directions");
    require((*graphite).authored_material &&
                (baked(*graphite) ||
                 (operation_count(*graphite,
                      VisualPaintOperation::fill_linear_gradient_spread) == 2U &&
                  operation_count(*graphite,
                      VisualPaintOperation::draw_line) == 4U)),
            "Graphite inspection must expose its texture and edge-specific chassis keylines");
    require((*studio).authored_material &&
                (*(*studio).authored_material).fills.front().image_mode ==
                    MaterialImageMode::nine_patch &&
                operation_count(*studio,
                    VisualPaintOperation::draw_image_region) == 9U,
            "Studio inspection must expose the cap-inset recipe as exactly nine retained image regions");

    for (const VisualControlInspection* specimen :
         {watercolor, office, graphite, studio}) {
        require((*specimen).display_chunk_current &&
                    !(*specimen).display_operations_truncated &&
                    (*specimen).total_display_operations <= 24U,
                "each material specimen must commit a current bounded display chunk");
    }

    const std::string json = snapshot.to_json();
    const std::size_t outer = json.find(
        "\"edge\":\"bottom\",\"color\":[23,45,105,255],\"width\":1,\"inset\":0");
    const std::size_t inset = json.find(
        "\"edge\":\"bottom\",\"color\":[215,242,255,196],\"width\":1,\"inset\":2");
    require(outer != std::string::npos && inset != std::string::npos &&
                outer < inset,
            "visual-inspection JSON must preserve authored outer-before-inset keyline order");

    PointerEvent move;
    move.action = PointerAction::move;
    move.position = {120.0, 580.0};
    move.pointer_id = 5U;
    static_cast<void>((*window).dispatch_pointer(move));
    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = move.position;
    down.pointer_id = move.pointer_id;
    require((*window).dispatch_pointer(down),
            "live Office Pearl specimen must accept pointer press");
    const VisualInspectionSnapshot pressed_snapshot =
        (*window).visual_inspection_snapshot();
    const VisualControlInspection* pressed = pressed_snapshot.find(
        "layered-material.live-state");
    require(pressed && (*pressed).state.hovered && (*pressed).state.pressed &&
                (*pressed).state.pointer_captured,
            "live state must be truthfully inspectable while pressed and captured");
    PointerEvent up = down;
    up.action = PointerAction::up;
    static_cast<void>((*window).dispatch_pointer(up));

    require((*window).perform_semantic_action(
                "layered-material.cycle-scale", SemanticAction::press) &&
                (*window).presentation_settings().text_scale == 1.25,
            "text-scale profile control must enter the same semantic command path");
    require((*window).perform_semantic_action(
                "layered-material.toggle-contrast", SemanticAction::press) &&
                (*window).presentation_settings().high_contrast,
            "high-contrast profile control must enter the same semantic command path");
    (*window).resize({1200.0, 680.0});
    (*window).perform_layout();
    const VisualInspectionSnapshot resized =
        (*window).visual_inspection_snapshot();
    const VisualControlInspection* resized_inspector =
        resized.find("layered-material.inspector");
    require(resized_inspector &&
                (*resized_inspector).layout.arranged_bounds.x == 822.0 &&
                (*resized_inspector).layout.arranged_bounds.width == 350.0 &&
                (*resized_inspector).layout.arranged_bounds.height == 550.0,
            "native minimum resize must preserve specimen/inspector separation without overlap");
}

void test_lab_copy_fits_at_150_percent_text_scale() {
    std::unique_ptr<Window> window = lab::make_layered_material_lab();
    EstimatedMetrics metrics;
    (*window).set_text_metrics_provider(&metrics);
    require((*window).perform_semantic_action(
                "layered-material.cycle-scale", SemanticAction::press) &&
                (*window).perform_semantic_action(
                    "layered-material.cycle-scale", SemanticAction::press) &&
                (*window).presentation_settings().text_scale == 1.5,
            "material lab must enter its 150 percent dogfood profile deterministically");
    NullPainter painter;
    static_cast<void>((*window).paint(
        painter, {0.0, 0.0, 1400.0, 760.0}));
    const VisualInspectionSnapshot snapshot =
        (*window).visual_inspection_snapshot();
    const std::vector<std::string_view> copy_ids{
        "layered-material.header.title",
        "layered-material.header.subtitle",
        "layered-material.watercolor.title",
        "layered-material.watercolor.detail",
        "layered-material.office.title",
        "layered-material.office.detail",
        "layered-material.graphite.title",
        "layered-material.graphite.detail",
        "layered-material.studio.title",
        "layered-material.studio.detail",
        "layered-material.states.title",
        "layered-material.state.0.label",
        "layered-material.state.1.label",
        "layered-material.state.2.label",
        "layered-material.state.3.label",
        "layered-material.state.4.label",
        "layered-material.live-state",
        "layered-material.live-disabled",
        "layered-material.rejection",
        "layered-material.target.0",
        "layered-material.target.1",
        "layered-material.target.2",
        "layered-material.target.3",
        "layered-material.target.4",
        "layered-material.target.5",
        "layered-material.toggle-contrast",
        "layered-material.cycle-scale",
    };
    constexpr double tolerance = 0.5;
    for (const std::string_view id : copy_ids) {
        const VisualControlInspection* control = snapshot.find(id);
        require(control != nullptr,
                "every scale-profile copy control must remain inspectable");
        bool found_text = false;
        for (const VisualPaintOperationSnapshot& operation :
             (*control).display_operations) {
            if (operation.operation != VisualPaintOperation::draw_text) continue;
            require(operation.resolved_text.has_value(),
                    "scale-profile text must carry deterministic resolved metrics");
            const ResolvedTextLayout& resolved = *operation.resolved_text;
            const Rect client = (*control).layout.client_rectangle;
            if (!(operation.first.x >= -tolerance &&
                  operation.first.x + resolved.logical_size.width <=
                      client.width + tolerance &&
                  operation.first.y - resolved.ascent >= -tolerance &&
                  operation.first.y + resolved.descent <=
                      client.height + tolerance)) {
                throw std::runtime_error(
                    "material-lab authored copy exceeds its control at 150 percent: " +
                    std::string(id) + " text=" +
                    std::to_string(resolved.logical_size.width) + "x" +
                    std::to_string(resolved.logical_size.height) + " client=" +
                    std::to_string(client.width) + "x" +
                    std::to_string(client.height) + " origin=" +
                    std::to_string(operation.first.x) + "," +
                    std::to_string(operation.first.y) + " ascent=" +
                    std::to_string(resolved.ascent) + " descent=" +
                    std::to_string(resolved.descent));
            }
            found_text = true;
        }
        require(found_text,
                "every scale-profile copy control must commit its text operation");
    }
}

} // namespace

int main() {
    try {
        test_reference_recipes_are_exact_and_bounded();
        test_lab_trace_inspection_and_complexity();
        test_lab_copy_fits_at_150_percent_text_scale();
        std::cout << "gui_forms_layered_material_lab_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_layered_material_lab_tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
