#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/material_panel.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "support/named_callbacks.hpp"

#include <cstdlib>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class RichPainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(Point) override {}
    void clip_rect(Rect rect) override {
        ++clips;
        last_clip = rect;
    }
    void clip_rounded_rect(Rect, double radius) override {
        ++rounded_clips;
        last_radius = radius;
    }
    void fill_rect(Rect, Color) override { ++solid_fills; }
    void fill_rounded_rect(Rect, double, Color) override { ++rounded_fills; }
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double radius, Color, double width) override {
        ++rounded_strokes;
        last_radius = radius;
        last_width = width;
    }
    void fill_linear_gradient(Rect, Point start, Point end,
                              std::span<const GradientStop> stops) override {
        ++linear_fills;
        last_start = start;
        last_end = end;
        last_stop_count = stops.size();
    }
    void fill_linear_gradient_spread(
        Rect, Point start, Point end,
        std::span<const GradientStop> stops,
        GradientSpreadMode spread) override {
        ++spread_linear_fills;
        last_start = start;
        last_end = end;
        last_stop_count = stops.size();
        last_spread = spread;
    }
    void fill_radial_gradient(Rect, Point center, Size radii,
                              std::span<const GradientStop> stops) override {
        ++radial_fills;
        last_center = center;
        last_radii = radii;
        last_stop_count = stops.size();
    }
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {
        ++shadows;
    }
    void draw_inset_box_shadow(
        Rect, double, Point, double, double, Color) override {
        ++inset_shadows;
    }
    void draw_line(Point start, Point end, Color color, double width) override {
        lines.push_back({start, end, color, width});
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_image_region(ImageId image, Rect source, Rect destination,
                           double opacity) override {
        image_regions.push_back({image, source, destination, opacity});
    }
    void fill_image_pattern(ImageId image, Size source_pixel_size,
                            Rect destination, Size logical_tile_size,
                            ImagePatternWrap wrap, double opacity) override {
        image_patterns.push_back({image, source_pixel_size, destination,
                                  logical_tile_size, wrap, opacity});
    }

    struct ImageRegion final {
        ImageId image;
        Rect source;
        Rect destination;
        double opacity{};
    };
    struct ImagePattern final {
        ImageId image;
        Size source_pixel_size;
        Rect destination;
        Size logical_tile_size;
        ImagePatternWrap wrap{ImagePatternWrap::tile};
        double opacity{};
    };
    struct Line final {
        Point start;
        Point end;
        Color color;
        double width{};
    };

    unsigned saves{};
    unsigned restores{};
    unsigned clips{};
    unsigned rounded_clips{};
    unsigned solid_fills{};
    unsigned rounded_fills{};
    unsigned rounded_strokes{};
    unsigned linear_fills{};
    unsigned spread_linear_fills{};
    unsigned radial_fills{};
    unsigned shadows{};
    unsigned inset_shadows{};
    double last_radius{};
    double last_width{};
    Point last_start{};
    Point last_end{};
    Point last_center{};
    Size last_radii{};
    Rect last_clip{};
    std::size_t last_stop_count{};
    GradientSpreadMode last_spread{GradientSpreadMode::pad};
    std::vector<ImageRegion> image_regions;
    std::vector<ImagePattern> image_patterns;
    std::vector<Line> lines;
};

class FallbackPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override { ++clips; }
    void fill_rect(Rect, Color) override { ++fills; }
    void stroke_rect(Rect, Color, double) override { ++strokes; }
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    unsigned clips{};
    unsigned fills{};
    unsigned strokes{};
};

SurfaceMaterial specimen_material() {
    const std::vector<GradientStop> opaque{
        {0.0, Color::rgba(20, 50, 120)},
        {0.55, Color::rgba(55, 130, 205)},
        {1.0, Color::rgba(210, 85, 115)}};
    const std::vector<GradientStop> glow{
        {0.0, Color::rgba(255, 255, 255, 150)},
        {1.0, Color::rgba(255, 255, 255, 0)}};
    SurfaceMaterial material;
    material.fills = {
        MaterialFillLayer::linear({0.0, 0.0}, {1.0, 0.0}, opaque),
        MaterialFillLayer::radial({0.5, 0.5}, {0.5, 0.5}, glow)};
    material.shadows = {{{0.0, 3.0}, 6.0, 1.0,
                         Color::rgba(15, 25, 45, 80)}};
    material.border = MaterialBorder{Color::rgba(25, 45, 75), 2.0};
    material.corner_radius = 9.0;
    return material;
}

void test_generated_pointer_count_construction_is_owned_and_bounded() {
    GradientStop stops[]{
        {0.0, Color::rgba(12, 31, 48)},
        {0.55, Color::rgba(33, 89, 122)},
        {1.0, Color::rgba(82, 155, 178)}};
    MaterialFillLayer fills[]{
        MaterialFillLayer::solid(Color::rgba(12, 31, 48)),
        MaterialFillLayer::linear({0.0, 0.0}, {1.0, 1.0}, stops, 3U)};
    MaterialShadow shadows[]{
        {{0.0, 3.0}, 7.0, 1.0, Color::rgba(7, 15, 22, 96)}};
    const MaterialBorder border{Color::rgba(55, 115, 145), 1.0};

    const SurfaceMaterial material = SurfaceMaterial::from_parts(
        fills, 2U, shadows, 1U, &border, 8.0);
    stops[1].offset = 0.75;
    fills[0].color = Color::rgba(255, 0, 0);
    shadows[0].blur_radius = 19.0;

    require(material.fills.size() == 2U &&
                material.fills[0].color == Color::rgba(12, 31, 48) &&
                material.fills[1].stops.size() == 3U &&
                material.fills[1].stops[1].offset == 0.55 &&
                material.shadows.size() == 1U &&
                material.shadows[0].blur_radius == 7.0 &&
                material.border == border && material.corner_radius == 8.0,
            "pointer/count construction must copy generated static data into retained ownership");

    bool rejected{};
    try {
        static_cast<void>(MaterialFillLayer::linear(
            {0.0, 0.0}, {1.0, 0.0}, nullptr, 2U));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "nonzero generated gradient count must reject a null pointer");

    rejected = false;
    try {
        static_cast<void>(SurfaceMaterial::from_parts(
            nullptr, 1U, nullptr, 0U, nullptr, 0.0));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "nonzero generated fill count must reject a null pointer");
}

void test_css_angle_gradient_resolves_against_rectangular_bounds() {
    const GradientStop stops[]{
        {0.0, Color::rgba(10, 20, 30)},
        {1.0, Color::rgba(80, 90, 100)}};
    SurfaceMaterial material;
    material.fills = {
        MaterialFillLayer::linear_css_angle(145.0, stops, 2U)};
    RichPainter painter;
    paint_surface_material(painter, {20.0, 30.0, 200.0, 100.0}, material);

    require(painter.spread_linear_fills == 1U &&
                std::abs(painter.last_start.x - 63.6086916466358) < 0.000001 &&
                std::abs(painter.last_start.y - -0.5351346224371) < 0.000001 &&
                std::abs(painter.last_end.x - 176.391308353364) < 0.000001 &&
                std::abs(painter.last_end.y - 160.535134622437) < 0.000001,
            "CSS-angle gradients must resolve from the current rectangular bounds");

    material.fills = {
        MaterialFillLayer::linear_css_angle(180.0, stops, 2U)};
    paint_surface_material(painter, {20.0, 30.0, 200.0, 100.0}, material);
    require(painter.last_start == Point{120.0, 30.0} &&
                painter.last_end == Point{120.0, 130.0},
            "cardinal CSS angles must retain exact endpoints");
}

void test_independent_edge_borders_are_owned_and_inset() {
    MaterialFillLayer fill =
        MaterialFillLayer::solid(Color::rgba(240, 244, 248));
    const MaterialBorder top{Color::rgba(10, 20, 30), 2.0};
    const MaterialBorder right{Color::rgba(40, 50, 60), 4.0};
    const MaterialBorderEdges edges = MaterialBorderEdges::from_parts(
        &top, &right, nullptr, nullptr);
    const SurfaceMaterial material = SurfaceMaterial::from_parts(
        &fill, 1U, nullptr, 0U, nullptr, &edges, 0.0);
    require(valid_surface_material(material),
            "independent square edge borders must form a valid material");

    RichPainter painter;
    paint_surface_material(painter, {20.0, 30.0, 100.0, 60.0}, material);
    require(painter.lines.size() == 2U &&
                painter.lines[0].start == Point{20.0, 31.0} &&
                painter.lines[0].end == Point{120.0, 31.0} &&
                painter.lines[0].width == 2.0 &&
                painter.lines[1].start == Point{118.0, 30.0} &&
                painter.lines[1].end == Point{118.0, 90.0} &&
                painter.lines[1].width == 4.0,
            "edge borders must paint half-width inside their owned bounds");

    SurfaceMaterial invalid = material;
    invalid.corner_radius = 5.0;
    require(!valid_surface_material(invalid),
            "edge borders with rounded joins must wait for a proven corner primitive");
}

void test_ordered_outer_and_inset_keylines_are_bounded() {
    SurfaceMaterial material;
    material.keylines = {
        {MaterialEdge::bottom, Color::rgba(20, 30, 40), 1.0, 0.0},
        {MaterialEdge::bottom, Color::rgba(230, 240, 250), 1.0, 2.0},
        {MaterialEdge::top, Color::rgba(120, 140, 160), 2.0, 3.0},
    };
    require(valid_surface_material(material),
            "ordered outer and inset keylines must form a valid material");

    RichPainter painter;
    paint_surface_material(painter, {10.0, 20.0, 100.0, 40.0}, material);
    require(painter.lines.size() == 3U &&
                painter.lines[0].start == Point{10.0, 59.5} &&
                painter.lines[0].end == Point{110.0, 59.5} &&
                painter.lines[0].color == Color::rgba(20, 30, 40) &&
                painter.lines[1].start == Point{12.0, 57.5} &&
                painter.lines[1].end == Point{108.0, 57.5} &&
                painter.lines[1].color == Color::rgba(230, 240, 250) &&
                painter.lines[2].start == Point{13.0, 24.0} &&
                painter.lines[2].end == Point{107.0, 24.0},
            "keyline replay must preserve authored edge order and distinguish outer from inset geometry");

    std::shared_ptr<MaterialPanel> panel =
        make_control<MaterialPanel>(StableId("material.keyline.atomic"));
    (*panel).material = material;
    SurfaceMaterial over_budget = material;
    over_budget.keylines.resize(SurfaceMaterial::maximum_keylines + 1U);
    bool rejected = false;
    try {
        (*panel).material = over_budget;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*panel).material.read() == material,
            "over-budget keylines must reject atomically without replacing the retained material");

    const MaterialFillLayer fill =
        MaterialFillLayer::solid(Color::rgba(255, 255, 255));
    rejected = false;
    try {
        static_cast<void>(SurfaceMaterial::from_parts(
            &fill, 1U, nullptr, 0U, nullptr, nullptr,
            over_budget.keylines.data(), over_budget.keylines.size(), 0.0));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "generated pointer/count material construction must reject a ninth keyline");
}

void test_material_validation_is_atomic() {
    std::shared_ptr<gui_forms::MaterialPanel> panel = make_control<MaterialPanel>(StableId("material.atomic"));
    const SurfaceMaterial valid = specimen_material();
    unsigned changes{};
    SubscriptionToken token = (*panel).material_changed().subscribe(
        test_support::IncrementCounter<unsigned,
                                       const SurfaceMaterial&>(changes));
    (*panel).material = valid;
    (*panel).material = valid;
    require(changes == 1U, "identical material assignment must be silent");

    SurfaceMaterial invalid = valid;
    invalid.fills[0].stops[1].offset = 0.0;
    bool rejected{};
    try {
        (*panel).material = std::move(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*panel).material.read() == valid && changes == 1U,
            "invalid material must be rejected without partial mutation");

    invalid = valid;
    invalid.fills.front().spread =
        static_cast<GradientSpreadMode>(0xffU);
    rejected = false;
    try {
        (*panel).material = std::move(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*panel).material.read() == valid && changes == 1U,
            "unknown gradient spread must be rejected atomically");
}

void test_retained_replay_preserves_material_operations() {
    std::shared_ptr<gui_forms::MaterialPanel> panel = make_control<MaterialPanel>(StableId("material.replay"));
    (*panel).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    (*panel).material = specimen_material();
    Window window(panel, {200.0, 100.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());

    RichPainter painter;
    window.paint(painter, {0.0, 0.0, 200.0, 100.0});
    require(painter.solid_fills == 1U && painter.shadows == 1U &&
                painter.rounded_clips == 1U &&
                painter.spread_linear_fills == 1U &&
                painter.linear_fills == 0U && painter.radial_fills == 1U &&
                painter.rounded_strokes == 2U && painter.last_width == 2.0 &&
                painter.saves >= 1U &&
                painter.restores == painter.saves,
            "window backplane and display chunks must retain every material operation");
    require(painter.last_start == Point{0.0, 0.0} &&
                painter.last_end == Point{200.0, 0.0} &&
                painter.last_center == Point{100.0, 50.0} &&
                painter.last_radii == Size{100.0, 50.0} &&
                painter.last_stop_count == 2U,
            "normalized material geometry must resolve against arranged bounds");
    require(painter.last_spread == GradientSpreadMode::pad,
            "retained material must preserve its explicit gradient spread");

    const std::optional<DisplayChunkInfo> before = (*panel).display_chunk_info();
    (*panel).material = SurfaceMaterial{};
    const DamageRegion damage = window.take_damage();
    require(before && !damage.empty(),
            "material mutation must invalidate its retained display chunk");
    window.paint(painter, damage.bounds());
    require((*(*panel).display_chunk_info()).generation > (*before).generation,
            "material mutation must rebuild rather than reuse stale pixels");
}

void test_repeating_material_survives_record_and_replay() {
    std::shared_ptr<gui_forms::MaterialPanel> panel = make_control<MaterialPanel>(StableId("material.repeat"));
    (*panel).set_requested_bounds({0.0, 0.0, 96.0, 48.0});
    SurfaceMaterial texture;
    texture.fills = {MaterialFillLayer::repeating_linear(
        {0.0, 0.0}, {8.0, 8.0},
        {{0.0, Color::rgba(20, 30, 40)},
         {0.48, Color::rgba(20, 30, 40)},
         {0.52, Color::rgba(65, 80, 92)},
         {1.0, Color::rgba(65, 80, 92)}})};
    (*panel).material = texture;
    Window window(panel, {96.0, 48.0});
    window.perform_layout();

    RichPainter painter;
    window.paint(painter, {0.0, 0.0, 96.0, 48.0});
    require(painter.spread_linear_fills == 1U &&
                painter.last_spread == GradientSpreadMode::repeat &&
                painter.last_start == Point{0.0, 0.0} &&
                painter.last_end == Point{8.0, 8.0} &&
                painter.last_stop_count == 4U,
            "repeat mode and logical period must survive the retained display list");
}

void test_image_materials_retain_crop_tile_and_nine_patch() {
    std::shared_ptr<gui_forms::MaterialPanel> panel = make_control<MaterialPanel>(StableId("material.images"));
    (*panel).set_requested_bounds({0.0, 0.0, 30.0, 20.0});
    Window window(panel, {30.0, 20.0});
    const std::vector<std::byte> patch_pixels(12U * 12U * 4U,
                                               std::byte{0xff});
    const ImageLoadResult patch = window.load_bgra32_premultiplied(
        12U, 12U, 48U, patch_pixels);
    require(static_cast<bool>(patch), "nine-patch fixture must load");
    SurfaceMaterial material;
    material.fills = {MaterialFillLayer::nine_patch(
        patch.image, {12.0, 12.0}, {3.0, 3.0, 3.0, 3.0}, 1.5, 0.75)};
    (*panel).material = material;
    window.perform_layout();

    RichPainter painter;
    window.paint(painter, {0.0, 0.0, 30.0, 20.0});
    require(painter.image_regions.size() == 9U,
            "nine-patch material must retain exactly nine source crops");
    require(painter.image_regions.front().image == patch.image &&
                painter.image_regions.front().source == Rect{0.0, 0.0, 3.0, 3.0} &&
                painter.image_regions.front().destination == Rect{0.0, 0.0, 2.0, 2.0} &&
                painter.image_regions.front().opacity == 0.75 &&
                painter.image_regions[4].source == Rect{3.0, 3.0, 6.0, 6.0} &&
                painter.image_regions[4].destination == Rect{2.0, 2.0, 26.0, 16.0},
            "nine-patch must preserve source corners and stretch only the center bands");

    const std::vector<std::byte> tile_pixels(4U * 4U * 4U,
                                              std::byte{0xff});
    const ImageLoadResult tile = window.load_bgra32_premultiplied(
        4U, 4U, 16U, tile_pixels);
    require(static_cast<bool>(tile), "tile fixture must load");
    material.fills = {MaterialFillLayer::tiled_image(
        tile.image, {4.0, 4.0}, 1.0)};
    (*panel).set_requested_bounds({0.0, 0.0, 10.0, 6.0});
    (*panel).material = material;
    window.resize({10.0, 6.0});
    window.perform_layout();
    painter.image_regions.clear();
    painter.image_patterns.clear();
    window.paint(painter, {0.0, 0.0, 10.0, 6.0});
    require(painter.image_regions.empty() && painter.image_patterns.size() == 1U &&
                painter.image_patterns.front().image == tile.image &&
                painter.image_patterns.front().source_pixel_size == Size{4.0, 4.0} &&
                painter.image_patterns.front().destination == Rect{0.0, 0.0, 10.0, 6.0} &&
                painter.image_patterns.front().logical_tile_size == Size{4.0, 4.0} &&
                painter.image_patterns.front().wrap == ImagePatternWrap::tile,
            "tiled image material must retain one exact-period pattern command");

    // A positioned tiling (E9.6): one tile's corner at (6 * 0.5 + 1, 2 * 1)
    // = (4, 2), so the pattern starts a whole tile back from there, at
    // (0, -2), clipped to the box.
    SurfaceMaterial positioned = material;
    positioned.fills.front().image_anchor = Point{0.5, 1.0};
    positioned.fills.front().image_offset = Point{1.0, 0.0};
    (*panel).material = positioned;
    window.perform_layout();
    painter.image_patterns.clear();
    const unsigned clips_before = painter.clips;
    window.paint(painter, {0.0, 0.0, 10.0, 6.0});
    require(painter.image_patterns.size() == 1U &&
                painter.image_patterns.front().destination == Rect{0.0, -2.0, 10.0, 8.0} &&
                painter.image_patterns.front().logical_tile_size == Size{4.0, 4.0},
            "a positioned tiling starts whole tiles back from its tile");
    require(painter.clips > clips_before && painter.last_clip == Rect{0.0, 0.0, 10.0, 6.0},
            "a positioned tiling is clipped to its box");
    (*panel).material = material;

    // A finished material (E9.6) draws as the material it describes on the
    // built-in path too: its four stops, and its shine.
    SurfaceMaterial finished_face;
    finished_face.fills = {MaterialFillLayer::solid(Color::rgba(100, 140, 200))};
    finished_face.finish = 1.0;
    (*panel).material = finished_face;
    window.perform_layout();
    RichPainter finish_painter;
    window.paint(finish_painter, {0.0, 0.0, 10.0, 6.0});
    require(finish_painter.linear_fills + finish_painter.spread_linear_fills == 1U &&
                finish_painter.last_stop_count == 4U,
            "a finished face draws its four-stop gradient");
    (*panel).material = material;

    SurfaceMaterial invalid_position = material;
    invalid_position.fills.front().image_anchor = Point{1.5, 0.0};
    bool position_rejected{};
    try {
        (*panel).material = std::move(invalid_position);
    } catch (const std::invalid_argument&) {
        position_rejected = true;
    }
    require(position_rejected && (*panel).material.read() == material,
            "an anchor outside [0, 1] is rejected atomically");
    invalid_position = material;
    invalid_position.fills.front().image_offset = Point{0.0, 5000.0};
    position_rejected = false;
    try {
        (*panel).material = std::move(invalid_position);
    } catch (const std::invalid_argument&) {
        position_rejected = true;
    }
    require(position_rejected && (*panel).material.read() == material,
            "an offset past 4096 units is rejected atomically");

    SurfaceMaterial invalid = material;
    invalid.fills.front().image_slice = {3.0, 0.0, 3.0, 0.0};
    bool rejected{};
    try {
        (*panel).material = std::move(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*panel).material.read() == material,
            "non-nine-patch slice metadata must be rejected atomically");

    invalid = material;
    invalid.fills.front() = MaterialFillLayer::stretched_image(
        ImageId{UINT64_C(0xffffffffffffffff)}, {4.0, 4.0});
    rejected = false;
    try {
        (*panel).material = std::move(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*panel).material.read() == material,
            "an attached MaterialPanel must reject missing or stale-size image IDs");
}

void test_minimal_painter_fallbacks_are_bounded() {
    FallbackPainter painter;
    const std::vector<GradientStop> stops{
        {0.0, Color::rgba(0, 0, 0)},
        {1.0, Color::rgba(255, 255, 255)}};
    painter.clip_rounded_rect({0.0, 0.0, 100.0, 40.0}, 8.0);
    painter.fill_rounded_rect({0.0, 0.0, 100.0, 40.0}, 8.0,
                              Color::rgba(30, 40, 50));
    painter.fill_linear_gradient({0.0, 0.0, 100.0, 40.0}, {0.0, 0.0},
                                 {100.0, 0.0}, stops);
    painter.fill_linear_gradient_spread(
        {0.0, 0.0, 100.0, 40.0}, {0.0, 0.0}, {8.0, 8.0}, stops,
        GradientSpreadMode::repeat);
    painter.fill_radial_gradient({0.0, 0.0, 100.0, 40.0}, {50.0, 20.0},
                                 {50.0, 20.0}, stops);
    painter.draw_box_shadow({5.0, 5.0, 90.0, 30.0}, 6.0, {0.0, 2.0},
                            4.0, 0.0, Color::rgba(0, 0, 0, 80));
    require(painter.clips == 2U && painter.fills > 50U &&
                painter.fills < 1000U && painter.strokes > 0U &&
                painter.strokes < 100U,
            "minimal-host material fallbacks must be visible and bounded");
}

void test_shadow_outsets_participate_in_damage() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("material.damage.root"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 100.0});
    std::shared_ptr<gui_forms::MaterialPanel> panel = make_control<MaterialPanel>(StableId("material.damage.panel"));
    (*panel).set_requested_bounds({30.0, 25.0, 40.0, 30.0});
    SurfaceMaterial shadowed;
    shadowed.shadows = {{{4.0, 5.0}, 3.0, 1.0,
                         Color::rgba(0, 0, 0, 100)}};
    (*panel).material = shadowed;
    (*root).add_child(panel);
    Window window(root, {120.0, 100.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());
    RichPainter painter;
    window.paint(painter, {0.0, 0.0, 120.0, 100.0});

    // Spread 1 and 3.5 deviations of a blur radius of 3 (5.25), less and
    // plus the offset (4, 5).
    const Insets outsets = (*panel).visual_outsets();
    require(outsets == Insets{2.25, 1.25, 10.25, 11.25},
            "material shadow must publish conservative visual outsets");
    require(painter.shadows == 1U && painter.last_clip.x <= -2.25 &&
                painter.last_clip.y <= -1.25 &&
                painter.last_clip.width >= 52.5 &&
                painter.last_clip.height >= 42.5,
            "retained replay must not clip a decoration to arranged bounds");
    (*panel).material = SurfaceMaterial{};
    const Rect damage = window.take_damage().bounds();
    require(damage.x <= 27.75 && damage.y <= 23.75 &&
                damage.x + damage.width >= 80.25 &&
                damage.y + damage.height >= 66.25,
            "shrinking a decoration must damage its former visual extent");
}

void test_inset_shadow_is_retained_inside_owner_geometry() {
    std::shared_ptr<gui_forms::MaterialPanel> panel =
        make_control<MaterialPanel>(StableId("material.inset"));
    (*panel).set_requested_bounds({0.0, 0.0, 120.0, 44.0});
    SurfaceMaterial material;
    material.shadows = {
        {{0.0, 1.0}, 0.0, 0.0, Color::rgba(255, 255, 255, 48), true}};
    (*panel).material = material;
    Window window(panel, {120.0, 44.0});
    window.perform_layout();
    RichPainter painter;
    window.paint(painter, {0.0, 0.0, 120.0, 44.0});

    require(painter.shadows == 0U && painter.inset_shadows == 1U,
            "retained replay must preserve inset shadow identity");
    require((*panel).visual_outsets() == Insets{},
            "inset shadows must not expand owner damage geometry");
}

} // namespace

int main() {
    try {
        test_generated_pointer_count_construction_is_owned_and_bounded();
        test_css_angle_gradient_resolves_against_rectangular_bounds();
        test_independent_edge_borders_are_owned_and_inset();
        test_ordered_outer_and_inset_keylines_are_bounded();
        test_material_validation_is_atomic();
        test_retained_replay_preserves_material_operations();
        test_repeating_material_survives_record_and_replay();
        test_image_materials_retain_crop_tile_and_nine_patch();
        test_minimal_painter_fallbacks_are_bounded();
        test_shadow_outsets_participate_in_damage();
        test_inset_shadow_is_retained_inside_owner_geometry();
        std::cout << "gui_forms_material_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_material_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
