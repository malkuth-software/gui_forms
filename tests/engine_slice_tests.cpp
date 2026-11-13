// Slicing (T2 plan, phase E7r; owner direction of 2026-10-22): a part of a
// slicing primitive is assembled from a shared reference bake, its corners
// and edges copied and its centre repeated, and the assembly has the bytes a
// whole bake would; an axis is kept whole where a gradient runs along it,
// where the box does not start and end on device pixels, or where the part
// is no longer than its reference, and each such fallback is counted; a
// radial gradient keeps the whole part; twenty buttons of one material and
// state, all of different widths, take one bake; and a theme change drops
// the references.

#include "gui_forms/control.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/surface_material/presets/material_presets.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"

#include "../src/core/engine/primitives/material/material_primitive.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_slice_tests: " << message << '\n';
    std::exit(1);
}

const EnginePrimitive& material_primitive() {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find("material");
    require(primitive != nullptr, "the built-in registry has the material primitive");
    return *primitive;
}

PrimitiveParameterBlock button_recipe() {
    const PrimitiveDescriptor& descriptor = material_primitive().descriptor();
    PrimitiveParameterBlock parameters = default_parameters(descriptor);
    require(set_choice_parameter(parameters, descriptor, "recipe", "button") == ParameterAssignment::assigned,
            "button is a recipe role");
    return parameters;
}

// The destination the engine sizes for a material part (raster_extent):
// the logical size grown by the outsets of 8, times the scale, rounded up.
DeviceExtent material_extent(Size logical, double scale) {
    const DeviceExtent extent{static_cast<std::uint32_t>(std::ceil((logical.width + 8.0 + 8.0) * scale)),
                              static_cast<std::uint32_t>(std::ceil((logical.height + 8.0 + 8.0) * scale))};
    return extent;
}

// The button recipe drawn with `shape`, one of part_shape_names (E9.6).
PartRasterRequest request_for(const SurfaceMaterial& material, Size logical, double scale,
                              std::string_view shape = "rounded_rectangle") {
    PartRasterRequest request{};
    request.primitive = &material_primitive();
    request.parameters = button_recipe();
    const PrimitiveDescriptor& descriptor = material_primitive().descriptor();
    require(set_choice_parameter(request.parameters, descriptor, "shape", shape) == ParameterAssignment::assigned,
            "a shape is a choice");
    request.target.logical_size = logical;
    request.target.scale = DeviceScale{scale};
    request.target.material = &material;
    request.extent = material_extent(logical, scale);
    return request;
}

// A whole bake, as if the part did not slice.
std::shared_ptr<PartRaster> whole_bake(const PartRasterRequest& request) {
    std::shared_ptr<PartRaster> raster = PartRaster::create(request.extent, request.target.scale);
    require(raster != nullptr, "the whole raster is made");
    const PartShapeKind shape = material_primitive().shape(request.parameters).kind;
    engine_primitives::bake_material(*request.target.material, request.target, (*raster).view(),
                                     engine_primitives::material_outset, shape);
    return raster;
}

bool same_pixels(const PartRaster& left, const PartRaster& right) {
    if (!(left.extent() == right.extent())) return false;
    const std::span<const std::uint8_t> a = left.pixels();
    const std::span<const std::uint8_t> b = right.pixels();
    const bool same = a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
    return same;
}

bool slices_across(const PrimitiveSlicePlan& plan) {
    const bool sliced = plan.across.outcome == SliceAxisOutcome::sliced;
    return sliced;
}

bool slices_down(const PrimitiveSlicePlan& plan) {
    const bool sliced = plan.down.outcome == SliceAxisOutcome::sliced;
    return sliced;
}

// A material with every layer that slices: offset outer and inset shadows,
// a rounded border or else square single edges (a material has one or the
// other, and single edges only on square corners), keylines on all four
// edges and a solid fill.
SurfaceMaterial layered_material(bool single_edges) {
    SurfaceMaterial material{};
    material.corner_radius = 3.0;
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::solid(Color{200, 210, 220, 255}));
    MaterialShadow outer{};
    outer.color = Color{0, 0, 0, 90};
    outer.offset = Point{1.0, 2.0};
    outer.blur_radius = 4.0;
    outer.spread = 1.0;
    material.shadows.push_back(outer);
    MaterialShadow inset{};
    inset.inset = true;
    inset.color = Color{255, 255, 255, 120};
    inset.offset = Point{-1.0, 1.0};
    inset.blur_radius = 2.0;
    material.shadows.push_back(inset);
    // A glow over the outer shadow (E9-C) slices as an outer shadow does.
    MaterialGlow glow{};
    glow.color = Color{255, 220, 120, 110};
    glow.blur_radius = 2.0;
    glow.spread = 1.0;
    material.glows.push_back(glow);
    // And an over glow, light added within the face.
    MaterialGlow over{};
    over.color = Color{255, 255, 255, 60};
    over.blur_radius = 1.5;
    over.spread = 0.0;
    over.placement = MaterialGlowPlacement::over;
    material.glows.push_back(over);
    if (single_edges) {
        // Single edges are square-cornered.
        material.corner_radius = 0.0;
        material.border_edges.top = MaterialBorder{Color{255, 255, 255, 160}, 1.0};
        material.border_edges.bottom = MaterialBorder{Color{0, 0, 0, 120}, 2.0};
        material.border_edges.left = MaterialBorder{Color{0, 0, 0, 120}, 1.5};
    } else {
        material.border = MaterialBorder{Color{40, 60, 90, 255}, 1.0};
    }
    const MaterialEdge edges[] = {MaterialEdge::top, MaterialEdge::right, MaterialEdge::bottom, MaterialEdge::left};
    for (const MaterialEdge edge : edges) {
        MaterialKeyline keyline{};
        keyline.edge = edge;
        keyline.color = Color{255, 0, 0, 100};
        keyline.inset = 2.0;
        keyline.width = 1.0;
        material.keylines.push_back(keyline);
    }
    require(valid_surface_material(material), "the layered material is valid");
    return material;
}

// -- the assembly has the whole bake's bytes ------------------------------------------

struct SliceCount final {
    std::size_t across{};
    std::size_t down{};
    std::size_t parts{};
};

void compare_assembly(const SurfaceMaterial& material, Size logical, double scale, SliceCount& count,
                      std::string_view shape = "rounded_rectangle") {
    const PartRasterRequest request = request_for(material, logical, scale, shape);
    if (!material_primitive().accepts(request.parameters, request.target)) {
        std::cerr << shape << ' ' << logical.width << " x " << logical.height << " at " << scale << '\n';
        require(false, "the material accepts the part");
    }
    const PrimitiveSlicePlan plan =
        material_primitive().slice_plan(request.parameters, request.target, request.extent);
    require(plan.slices, "the material slices");
    EnginePartTable table{};
    const PartRasterResult assembled = table.raster_without_record(request, 1U);
    require(assembled.raster != nullptr, "the part is drawn");
    const std::shared_ptr<PartRaster> whole = whole_bake(request);
    if (!same_pixels(*assembled.raster, *whole)) {
        std::cerr << "size " << logical.width << " x " << logical.height << " at " << scale << '\n';
        require(false, "the assembled part has the whole bake's bytes");
    }
    if (slices_across(plan)) count.across = count.across + 1U;
    if (slices_down(plan)) count.down = count.down + 1U;
    count.parts = count.parts + 1U;
}

void assembly_equals_whole_bake() {
    const std::shared_ptr<const Theme> theme = Theme::create(*test_support::professional_definition());
    const ControlSurfaceState states[] = {ControlSurfaceState::normal, ControlSurfaceState::hot,
                                          ControlSurfaceState::pressed};
    const double scales[] = {1.0, 1.25, 1.5, 2.0};
    const double widths[] = {24.0, 40.0, 76.0, 120.0, 204.0, 332.0};
    const double heights[] = {20.0, 24.0, 40.0, 64.0};
    SliceCount button{};
    for (const ControlSurfaceState state : states) {
        ControlVisualContext context{};
        context.surface = state;
        const SurfaceMaterial& material = (*theme).resolve(ControlVisualRole::button, context).material;
        for (const double scale : scales) {
            for (const double width : widths) {
                for (const double height : heights) compare_assembly(material, Size{width, height}, scale, button);
            }
        }
    }
    require(button.across > 0U, "some button parts slice across");
    SliceCount layered{};
    for (const bool single_edges : {false, true}) {
        const SurfaceMaterial material = layered_material(single_edges);
        for (const double scale : scales) {
            for (const double width : widths) {
                for (const double height : heights) compare_assembly(material, Size{width, height}, scale, layered);
            }
        }
    }
    require(layered.across > 0U && layered.down > 0U, "the layered material slices on both axes");
    // The gloss preset's vertical gradient slices across only.
    SliceCount gloss{};
    const SurfaceMaterial glossy = gloss_material(GlossColours{}, 4.0);
    for (const double scale : scales) {
        for (const double width : widths) compare_assembly(glossy, Size{width, 24.0}, scale, gloss);
    }
    require(gloss.across > 0U && gloss.down == 0U, "the gloss slices across only");
    // Four different corners (E9.6) slice as one radius does, by the
    // largest; so does a pill, along its long axis.
    SliceCount cornered{};
    SurfaceMaterial corners = layered_material(false);
    corners.corner_radii = MaterialCornerRadii{6.0, 0.0, 3.0, 1.5};
    SliceCount pill{};
    // The layered material less its over glow (its last glow), so that a
    // pill 40 high stays within the bake budget at every scale.
    SurfaceMaterial plain = layered_material(false);
    require(!plain.glows.empty() && plain.glows.back().placement == MaterialGlowPlacement::over,
            "the layered material's last glow is its over glow");
    plain.glows.pop_back();
    // A uniform radius past half the shorter side reaches only as far as
    // that half, as a pill does.
    SliceCount round_ended{};
    SurfaceMaterial generous = layered_material(false);
    generous.corner_radius = 32.0;
    for (const double scale : scales) {
        for (const double width : widths) {
            for (const double height : heights) {
                compare_assembly(corners, Size{width, height}, scale, cornered);
                // A tall pill's round ends are all corner pixels, and at
                // 64 high it exceeds the bake budget.
                if (height <= 40.0) compare_assembly(plain, Size{width, height}, scale, pill, "pill");
                // At 40 high its unclamped radius still marks every pixel as a
                // corner pixel of its shadows (outer_coverage), which the
                // bake effort counts: a dearer bake than the pill's.
                if (height <= 24.0) compare_assembly(generous, Size{width, height}, scale, round_ended);
            }
        }
    }
    require(cornered.across > 0U && cornered.down > 0U, "four corners slice on both axes");
    require(pill.across > 0U, "a pill slices along its length");
    // With its over glow the layered material is dearer: a pill of it 80
    // high at scale 2 is past the bake budget and refused. At 64 high it
    // is refused too, with or without its over glow.
    const SurfaceMaterial glowing = layered_material(false);
    const PartRasterRequest heavy = request_for(glowing, Size{76.0, 80.0}, 2.0, "pill");
    require(!material_primitive().accepts(heavy.parameters, heavy.target),
            "the heavier material's pill past the budget is refused");
    const PartRasterRequest heavy_short = request_for(glowing, Size{76.0, 64.0}, 2.0, "pill");
    const PartRasterRequest plain_tall = request_for(plain, Size{76.0, 64.0}, 2.0, "pill");
    require(!material_primitive().accepts(heavy_short.parameters, heavy_short.target) &&
                !material_primitive().accepts(plain_tall.parameters, plain_tall.target),
            "a pill 64 high at scale 2 is past the budget, with or without the over glow");
    // The re-check after the quiet-machine re-measure (E9.6 G1): a 120 x 64
    // part at scale 1.5, a pill or a radius of 32, is within the bake
    // budget (README, "Bake effort").
    const PartRasterRequest long_pill = request_for(plain, Size{120.0, 64.0}, 1.5, "pill");
    const PartRasterRequest long_round = request_for(generous, Size{120.0, 64.0}, 1.5);
    const PartRasterRequest glowing_pill = request_for(glowing, Size{120.0, 64.0}, 1.5, "pill");
    require(material_primitive().accepts(long_pill.parameters, long_pill.target) &&
                material_primitive().accepts(long_round.parameters, long_round.target) &&
                material_primitive().accepts(glowing_pill.parameters, glowing_pill.target),
            "a 120 x 64 part at scale 1.5 is within the bake budget");
    require(round_ended.across > 0U, "a radius past half the height slices along the length");
    // The case that was refused before the clamp: radius 32, 32 high, at 2.
    SliceCount large{};
    compare_assembly(generous, Size{120.0, 32.0}, 2.0, large);
    require(large.across == 1U, "a radius-32 part 32 high at scale 2 slices and is within the bake budget");
    // The part filters (E9.6) carry every change further in by their
    // reach, so the corners grow by it: sliced, the part still has the
    // whole bake's bytes.
    SliceCount filtered{};
    SurfaceMaterial filters = layered_material(false);
    filters.colour_filter = MaterialColourMatrix::sepia(0.6);
    filters.drop_shadow = MaterialDropShadow{Point{1.0, 2.0}, 2.0, Color{0, 0, 0, 90}};
    filters.filter_blur = 1.0;
    for (const double scale : scales) {
        for (const double width : widths) {
            for (const double height : heights) {
                if (height <= 40.0) compare_assembly(filters, Size{width, height}, scale, filtered);
            }
        }
    }
    require(filtered.across > 0U && filtered.down > 0U, "a filtered part slices on both axes");
}

// -- the fallbacks -----------------------------------------------------------------

void fallbacks() {
    const SurfaceMaterial material = layered_material(false);
    const EnginePrimitive& primitive = material_primitive();
    // At 1.25, 75 units is 93.75 device pixels: the far edge is not on a
    // pixel boundary, so the axis is baked at its length; 76 is 95.
    PartRasterRequest request = request_for(material, Size{75.0, 76.0}, 1.25);
    PrimitiveSlicePlan plan = primitive.slice_plan(request.parameters, request.target, request.extent);
    require(plan.across.outcome == SliceAxisOutcome::fractional, "a fractional far edge keeps the axis whole");
    require(slices_down(plan), "the other axis still slices");
    require(plan.reference_size.width == 75.0, "the reference keeps the part's width");
    // A part no longer than its corners is baked whole.
    request = request_for(material, Size{6.0, 6.0}, 1.0);
    plan = primitive.slice_plan(request.parameters, request.target, request.extent);
    require(plan.across.outcome == SliceAxisOutcome::too_small && plan.down.outcome == SliceAxisOutcome::too_small,
            "a small part is baked whole");
    // A vertical gradient varies down the part, not across it.
    SurfaceMaterial vertical = material;
    const std::vector<GradientStop> stops{GradientStop{0.0, Color{255, 255, 255, 255}},
                                          GradientStop{1.0, Color{0, 0, 0, 255}}};
    vertical.fills.clear();
    vertical.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0}, stops));
    request = request_for(vertical, Size{120.0, 40.0}, 1.0);
    plan = primitive.slice_plan(request.parameters, request.target, request.extent);
    require(slices_across(plan) && plan.down.outcome == SliceAxisOutcome::varies,
            "a vertical gradient slices across only");
    // A radial gradient keeps the whole part, counted.
    SurfaceMaterial radial = material;
    radial.fills.clear();
    radial.fills.push_back(MaterialFillLayer::radial(Point{0.5, 0.5}, Size{0.5, 0.5}, stops));
    request = request_for(radial, Size{120.0, 40.0}, 1.0);
    plan = primitive.slice_plan(request.parameters, request.target, request.extent);
    require(plan.across.outcome == SliceAxisOutcome::varies && plan.down.outcome == SliceAxisOutcome::varies,
            "a radial gradient varies along both axes");
    EnginePartTable table{};
    const PartRasterResult drawn = table.raster_without_record(request, 1U);
    require(drawn.raster != nullptr, "the radial part is drawn whole");
    const RasterCacheStatistics statistics = table.cache_statistics();
    require(statistics.slice_whole_fallbacks == 1U && statistics.slice_axis_fallbacks == 2U,
            "the whole-part fallback and both axes are counted");
    require(statistics.bakes == 1U && statistics.slice_bakes == 0U, "the whole part is one bake, no reference");
}

// -- twenty buttons, one bake ---------------------------------------------------------

// A painter that counts the part rasters composited, and draws nothing.
class RasterCount final : public Painter {
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
    void draw_part_raster(const std::shared_ptr<const PartRaster>&, Point, double) override {
        rasters = rasters + 1U;
    }
    std::size_t rasters{};
};

class Pad final : public Control {
public:
    Pad(StableId stable_id, double width) : Control(std::move(stable_id)), width_(width) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{0.0, 0.0, width_, 24.0};
        part.key = PartKey{"face", {}, {}};
        paint_part(painter, *this, part);
    }

private:
    double width_{};
};

ThemeRule sliced_rule() {
    ThemeRule rule{};
    rule.label = "sliced";
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.classes.push_back("sliced");
    rule.selector.part = "face";
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    return rule;
}

void twenty_buttons_one_bake() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    for (std::size_t index = 0U; index < 20U; ++index) {
        const double width = 40.0 + 7.0 * static_cast<double>(index);
        const std::shared_ptr<Pad> pad =
            make_control<Pad>(StableId("pad" + std::to_string(index)), width);
        (*pad).add_class("sliced");
        (*pad).set_requested_bounds(Rect{20.0, 20.0 + 40.0 * static_cast<double>(index), width, 24.0});
        (*root).add_child(pad);
    }
    Window window(root, Size{400.0, 860.0});
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "engine-slice-test";
    definition.rules.rules.push_back(sliced_rule());
    window.set_theme(Theme::create(definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the slicing theme compiles cleanly");
    window.perform_layout();
    RasterCount painter{};
    window.paint(painter);
    require(painter.rasters == 20U, "every button is drawn by the material");
    RasterCacheStatistics statistics = window.engine_parts().cache_statistics();
    require(statistics.bakes == 1U && statistics.slice_bakes == 1U, "twenty widths take one bake");
    require(statistics.slice_hits == 19U, "the other nineteen are assembled from the held reference");
    require(statistics.shared_rasters == 1U, "one reference is held");
    // A theme change drops the reference; the next paint bakes one again.
    window.set_theme(Theme::create(definition));
    statistics = window.engine_parts().cache_statistics();
    require(statistics.shared_rasters == 0U, "a theme change drops the references");
}

} // namespace

int main() {
    assembly_equals_whole_bake();
    fallbacks();
    twenty_buttons_one_bake();
    return 0;
}
