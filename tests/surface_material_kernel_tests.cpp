// One rasteriser (stage T4, owner 2026-11-07): a material a control paints
// itself (paint_surface_material) is baked by the theme engine's material
// kernel, so it has the pixels the same material has as a part's face. A
// sunken well (rounded, bordered, an inset and an outer shadow) and a
// gradient card, at whole and fractional bounds and at scales 1, 1.5 and 2,
// are drawn three ways into a Skia raster: painted directly, recorded and
// replayed as a window records a control, and baked by the kernel as the
// engine bakes a face and composited at the box less its outset. Every
// pixel of the three is equal. A lone solid square fill stays one fill_rect
// (material_raster.hpp) and is not compared here.
#include "gui_forms/dirty.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "skia_raster.hpp"
#include "../src/core/display/recording_painter/recording_painter.hpp"
#include "../src/core/display/replay/replay_display_chunk.hpp"
#include "../src/core/engine/primitives/material/material_primitive.hpp"
#include "../src/core/engine/primitives/material/material_raster.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr Size canvas_size{160.0, 120.0};
const Color ground = Color::rgba(236, 240, 244);

// A sunken well: rounded, bordered, shaded inside, lit below.
SurfaceMaterial well_material() {
    SurfaceMaterial made{};
    made.fills = {MaterialFillLayer::solid(Color::rgba(238, 243, 247))};
    made.corner_radius = 3.0;
    made.border = MaterialBorder{.color = Color::rgba(132, 148, 163), .width = 1.0};
    MaterialShadow inner{};
    inner.offset = Point{0.0, 1.0};
    inner.blur_radius = 2.0;
    inner.color = Color::rgba(20, 30, 42, 60);
    inner.inset = true;
    MaterialShadow outer{};
    outer.offset = Point{0.0, 1.0};
    outer.blur_radius = 2.0;
    outer.color = Color::rgba(255, 255, 255, 140);
    made.shadows = {inner, outer};
    return made;
}

// A card: a vertical gradient, rounded, with a hairline border.
SurfaceMaterial card_material() {
    SurfaceMaterial made{};
    made.fills = {MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0},
                                            {GradientStop{0.0, Color::rgba(250, 252, 254)},
                                             GradientStop{1.0, Color::rgba(214, 224, 234)}})};
    made.corner_radius = 6.0;
    made.border = MaterialBorder{.color = Color::rgba(112, 130, 156), .width = 1.0};
    return made;
}

// One frame's pixels, BGRA rows of `row_bytes`.
struct Frame final {
    std::vector<std::uint8_t> pixels{};
    std::size_t row_bytes{};
};

enum class Path : std::uint8_t { direct, recorded, kernel };

// Draws `material` over `bounds` at `scale` along `path`; false when the
// raster, the recording or the bake cannot be made.
bool draw(Path path, const SurfaceMaterial& material, Rect bounds, double scale, Frame& frame) {
    render::SkiaRaster raster{};
    if (!raster.resize(canvas_size, scale)) return false;
    DamageRegion damage{};
    damage.add(Rect{0.0, 0.0, canvas_size.width, canvas_size.height});
    raster.begin_frame(damage);
    raster.fill_rect(Rect{0.0, 0.0, canvas_size.width, canvas_size.height}, ground);
    bool drawn = true;
    if (path == Path::direct) {
        paint_surface_material(raster, bounds, material);
    } else if (path == Path::recorded) {
        detail::RecordingPainter recorder(nullptr, DeviceScale{scale});
        paint_surface_material(recorder, bounds, material);
        const std::shared_ptr<const detail::DisplayChunk> chunk =
            recorder.finish(1U, PaintPlane::control, Rect{0.0, 0.0, canvas_size.width, canvas_size.height});
        static_cast<void>(detail::replay_display_chunk(*chunk, raster));
    } else {
        // As the engine bakes a face: the box grown by the primitive's
        // outset, the raster placed at the box less it.
        const double outset = engine_primitives::material_outset;
        const DeviceExtent extent{static_cast<std::uint32_t>(std::ceil((bounds.width + outset * 2.0) * scale)),
                                  static_cast<std::uint32_t>(std::ceil((bounds.height + outset * 2.0) * scale))};
        const std::shared_ptr<PartRaster> baked = PartRaster::create(extent, DeviceScale{scale});
        drawn = baked != nullptr;
        if (drawn) {
            PrimitiveTarget target{};
            target.logical_size = Size{bounds.width, bounds.height};
            target.scale = DeviceScale{scale};
            engine_primitives::bake_material(material, target, (*baked).view(), outset);
            raster.draw_part_raster(baked, Point{bounds.x - outset, bounds.y - outset}, 1.0);
        }
    }
    raster.end_frame();
    frame.row_bytes = raster.row_bytes();
    const std::uint8_t* const first = static_cast<const std::uint8_t*>(raster.pixels());
    frame.pixels.assign(first, first + frame.row_bytes * raster.pixel_height());
    return drawn;
}

// How many bytes of the two frames differ.
std::size_t differing_bytes(const Frame& left, const Frame& right) {
    if (left.pixels.size() != right.pixels.size()) return left.pixels.size() + 1U;
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < left.pixels.size(); ++index) {
        if (left.pixels[index] != right.pixels[index]) ++count;
    }
    return count;
}

void test_three_paths_agree() {
    const std::array<SurfaceMaterial, 2> materials{well_material(), card_material()};
    constexpr std::array<Rect, 3> placements{Rect{18.0, 20.0, 120.0, 48.0}, Rect{18.25, 20.5, 119.5, 47.5},
                                             Rect{17.75, 21.25, 80.25, 60.0}};
    constexpr std::array<double, 3> scales{1.0, 1.5, 2.0};
    for (std::size_t which = 0U; which < materials.size(); ++which) {
        require(detail::material_painting(materials[which]) == detail::MaterialPainting::kernel,
                "the material is one the kernel bakes");
        for (const Rect bounds : placements) {
            for (const double scale : scales) {
                Frame direct{};
                Frame recorded{};
                Frame kernel{};
                const bool drawn = draw(Path::direct, materials[which], bounds, scale, direct) &&
                                   draw(Path::recorded, materials[which], bounds, scale, recorded) &&
                                   draw(Path::kernel, materials[which], bounds, scale, kernel);
                require(drawn, "all three frames draw");
                const std::size_t recorded_differs = differing_bytes(recorded, kernel);
                const std::size_t direct_differs = differing_bytes(direct, kernel);
                if (recorded_differs != 0U || direct_differs != 0U) {
                    std::cerr << "  material " << which << " at (" << bounds.x << ", " << bounds.y << ") scale "
                              << scale << ": recorded " << recorded_differs << ", direct " << direct_differs
                              << " bytes differ\n";
                }
                require(recorded_differs == 0U, "the recorded material is the kernel's face");
                require(direct_differs == 0U, "the material painted directly is the kernel's face");
            }
        }
    }
}

// A sunken radio well (Windows 2000's): a circle whose single edges and
// keylines differ, top and left dark, bottom and right light. Rounded
// single edges of one width are valid and draw as rings split at 45
// degrees: the top and left of each ring take the top and left colours,
// the bottom and right the others. Unequal widths stay invalid.
void test_rounded_single_edges() {
    SurfaceMaterial well{};
    well.fills = {MaterialFillLayer::solid(Color::rgba(255, 255, 255))};
    well.corner_radius = 7.5;
    well.border_edges.top = MaterialBorder{.color = Color::rgba(128, 128, 128), .width = 1.0};
    well.border_edges.left = MaterialBorder{.color = Color::rgba(128, 128, 128), .width = 1.0};
    well.border_edges.bottom = MaterialBorder{.color = Color::rgba(255, 255, 255), .width = 1.0};
    well.border_edges.right = MaterialBorder{.color = Color::rgba(255, 255, 255), .width = 1.0};
    well.keylines = {MaterialKeyline{.edge = MaterialEdge::top, .color = Color::rgba(64, 64, 64), .width = 1.0, .inset = 1.0},
                     MaterialKeyline{.edge = MaterialEdge::left, .color = Color::rgba(64, 64, 64), .width = 1.0, .inset = 1.0},
                     MaterialKeyline{.edge = MaterialEdge::bottom, .color = Color::rgba(212, 208, 200), .width = 1.0, .inset = 1.0},
                     MaterialKeyline{.edge = MaterialEdge::right, .color = Color::rgba(212, 208, 200), .width = 1.0, .inset = 1.0}};
    require(valid_surface_material(well), "rounded single edges of one width are valid");
    SurfaceMaterial uneven = well;
    (*uneven.border_edges.top).width = 2.0;
    require(!valid_surface_material(uneven), "rounded single edges of differing widths are refused");
    const double outset = detail::material_raster_outset(well);
    const std::shared_ptr<const PartRaster> raster =
        detail::bake_material_raster(well, Size{15.0, 15.0}, DeviceScale{1.0}, outset);
    require(raster != nullptr, "the radio well bakes");
    if (raster == nullptr) return;
    const PartRaster& baked = *raster;
    const std::size_t origin = static_cast<std::size_t>(outset);
    const std::size_t row_bytes = baked.row_bytes();
    // The blue byte of the pixel at (x, y) in the box (BGRA): the greys
    // here have equal channels, near enough.
    const std::span<const std::uint8_t> pixels = baked.pixels();
    const std::array<std::array<std::size_t, 2>, 6> probes{{{7U, 1U}, {1U, 7U}, {7U, 13U}, {13U, 7U}, {7U, 7U}, {7U, 0U}}};
    std::array<std::uint8_t, 6> blue{};
    for (std::size_t index = 0U; index < probes.size(); ++index) {
        const std::size_t at = (origin + probes[index][1]) * row_bytes + (origin + probes[index][0]) * 4U;
        blue[index] = pixels[at];
    }
    require(blue[0] < 110U && blue[1] < 110U, "the inner ring's top and left are the dark keylines");
    require(blue[2] > 180U && blue[2] < 230U && blue[3] > 180U && blue[3] < 230U,
            "the inner ring's bottom and right are the light keylines");
    require(blue[4] == 255U, "the well's centre is its white fill");
    require(blue[5] < 200U, "the outer ring's top is the shadow edge");
}

} // namespace

int main() {
    test_three_paths_agree();
    test_rounded_single_edges();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "surface material kernel tests passed\n";
    return 0;
}
