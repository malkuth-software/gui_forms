// Owner decision L-Q8 on the Skia raster: each box edge rounds to the
// nearest device pixel, so neighbours share an edge with no seam and no
// overlap (support/edge_snapping_scene.hpp).
#include "gui_forms/types.hpp"
#include "skia_raster.hpp"
#include "support/edge_snapping_scene.hpp"

#include <cstdint>
#include <iostream>

int main() {
    for (const double scale : edge_snapping::scales) {
        gui_forms::render::SkiaRaster raster;
        if (!raster.resize(edge_snapping::scene_size, scale)) {
            std::cerr << "skia raster allocation failed at scale " << scale << '\n';
            return 1;
        }
        gui_forms::DamageRegion damage;
        damage.add({0.0, 0.0, edge_snapping::scene_size.width, edge_snapping::scene_size.height});
        raster.begin_frame(damage);
        edge_snapping::paint_scene(raster);
        raster.end_frame();
        edge_snapping::PixelImage image{};
        image.width = static_cast<int>(raster.pixel_width());
        image.height = static_cast<int>(raster.pixel_height());
        image.row_bytes = raster.row_bytes();
        image.bytes = static_cast<const std::uint8_t*>(raster.pixels());
        if (image.bytes == nullptr || !edge_snapping::check_scene(image, scale)) return 1;
    }
    std::cout << "gui_forms_skia_edge_snapping_tests: all tests passed\n";
    return 0;
}
