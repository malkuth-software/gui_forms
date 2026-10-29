// A partial repaint draws the pixels a full repaint draws (owner decision D3,
// 2026-10-28). Damage is a clip, and stx_skia's analytic anti-aliasing used to
// chop curved edges and start its walk at the clip's top and bottom, so a
// rounded stroke cut by a damage edge came out a few coverage levels off on the
// rows next to it (tabs-and-pages and dock-and-anchor in the Gallery). Each
// scene here is painted whole, then repainted above and below every device row
// of its height; every repaint must leave every pixel as the whole paint left
// it. Banded paint relies on the same guarantee.
#include "gui_forms/types.hpp"
#include "skia_raster.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using gui_forms::Color;
using gui_forms::DamageRegion;
using gui_forms::GradientSpreadMode;
using gui_forms::GradientStop;
using gui_forms::Point;
using gui_forms::Rect;
using gui_forms::Size;
using gui_forms::render::SkiaRaster;

constexpr Size scene_size{240.0, 120.0};
constexpr std::array<double, 3> scales{1.0, 2.0, 3.0};

// The geometry the Gallery drew off at damage edges: rounded strokes and fills
// with fractional edges, a repeating gradient in a rounded clip and a wide
// diagonal line.
void paint_scene(SkiaRaster& raster) {
    raster.fill_rect({0.0, 0.0, scene_size.width, scene_size.height}, Color::rgba(250, 250, 250));
    raster.stroke_rounded_rect({0.5, 9.5, 189.0, 83.107}, 5.5, Color::rgba(40, 60, 120), 1.0);
    raster.stroke_rounded_rect({20.5, 30.5, 75.0, 31.563}, 2.5, Color::rgba(120, 40, 60), 1.0);
    raster.fill_rounded_rect({18.0, 9.5, 11.0, 11.0}, 5.5, Color::rgba(30, 140, 70));
    raster.fill_rounded_rect({120.25, 40.75, 60.5, 47.375}, 9.0, Color::rgba(200, 120, 20, 180));
    const std::array<GradientStop, 2> stops{{{0.0, Color::rgba(10, 10, 160)}, {1.0, Color::rgba(220, 220, 255)}}};
    raster.save();
    raster.clip_rounded_rect({196.5, 7.25, 36.0, 101.5}, 6.0);
    raster.fill_linear_gradient_spread({196.5, 7.25, 36.0, 101.5}, {0.0, 7.25}, {0.0, 24.5}, stops,
                                       GradientSpreadMode::repeat);
    raster.restore();
    raster.draw_line({4.25, 110.5}, {230.75, 96.125}, Color::rgba(0, 0, 0), 2.0);
}

void paint(SkiaRaster& raster, Rect damage) {
    DamageRegion region;
    region.add(damage);
    raster.begin_frame(region);
    paint_scene(raster);
    raster.end_frame();
}

std::vector<std::uint8_t> snapshot(const SkiaRaster& raster) {
    std::vector<std::uint8_t> bytes(raster.byte_size());
    std::memcpy(bytes.data(), raster.pixels(), bytes.size());
    return bytes;
}

// The number of bytes in which the raster differs from `whole`, and the row of the first.
struct Difference final {
    std::size_t bytes{};
    std::size_t first_row{};
};

Difference compare(const SkiaRaster& raster, const std::vector<std::uint8_t>& whole) {
    const std::uint8_t* const now = static_cast<const std::uint8_t*>(raster.pixels());
    Difference difference{};
    std::size_t index = 0;
    for (index = 0; index < whole.size(); ++index) {
        if (whole[index] == now[index]) continue;
        if (difference.bytes == 0U) difference.first_row = index / raster.row_bytes();
        ++difference.bytes;
    }
    return difference;
}

// Repaints rows [0, row) and then rows [row, height) over the whole paint; both must change nothing.
bool repaint_matches(SkiaRaster& raster, const std::vector<std::uint8_t>& whole, double scale, int row) {
    const double edge = static_cast<double>(row) / scale;
    const std::array<Rect, 2> parts{{{0.0, 0.0, scene_size.width, edge},
                                     {0.0, edge, scene_size.width, scene_size.height - edge}}};
    for (const Rect& part : parts) {
        if (part.empty()) continue;
        paint(raster, part);
        const Difference difference = compare(raster, whole);
        if (difference.bytes != 0U) {
            std::cerr << "scale " << scale << ", damage edge at device row " << row << ": " << difference.bytes
                      << " bytes changed, first on row " << difference.first_row << '\n';
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    std::size_t repaints = 0;
    for (const double scale : scales) {
        SkiaRaster raster;
        if (!raster.resize(scene_size, scale)) {
            std::cerr << "skia raster allocation failed at scale " << scale << '\n';
            return 1;
        }
        paint(raster, {0.0, 0.0, scene_size.width, scene_size.height});
        const std::vector<std::uint8_t> whole = snapshot(raster);
        const int rows = static_cast<int>(raster.pixel_height());
        int row = 0;
        for (row = 1; row < rows; ++row) {
            if (!repaint_matches(raster, whole, scale, row)) return 1;
            ++repaints;
        }
    }
    std::cout << "gui_forms_skia_damage_edge_tests: " << repaints << " repaints, all tests passed\n";
    return 0;
}
