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
#include "gui_forms/gui_forms.hpp"
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

} // namespace

int main() {
    test_three_paths_agree();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "surface material kernel tests passed\n";
    return 0;
}
