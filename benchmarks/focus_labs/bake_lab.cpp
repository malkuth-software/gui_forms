// Lab 7: what a material bake costs per pixel (bake_material, built
// libraries of D:\gui_forms_h, c3f1824a9, read only), over a ladder of
// materials from a plain opaque rectangle upward, at three part sizes. No
// candidate here: the look's resolved form is private to the bake, so this
// is the baseline FS10 lacked.

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "../src/core/engine/primitives/material/material_primitive.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace gui_forms;

namespace lab {

using Clock = std::chrono::steady_clock;

struct Step final {
    const char* name{};
    SurfaceMaterial material{};
};

static double bake_seconds(const SurfaceMaterial& material, double width, double height, std::size_t& pixels,
                           std::size_t& uniform_rows) {
    PrimitiveTarget target{};
    target.logical_size = Size{width, height};
    target.scale = DeviceScale{1.0};
    target.material = &material;
    const double outset = engine_primitives::material_outset;
    const DeviceExtent extent{static_cast<std::uint32_t>(std::ceil(width + outset * 2.0)),
                              static_cast<std::uint32_t>(std::ceil(height + outset * 2.0))};
    const std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{1.0});
    pixels = static_cast<std::size_t>(extent.width) * extent.height;
    double best = 1.0e30;
    const int repeats = pixels > 400000U ? 3 : 9;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const Clock::time_point start = Clock::now();
        engine_primitives::bake_material(material, target, (*raster).view(), outset);
        const Clock::time_point end = Clock::now();
        best = std::min(best, std::chrono::duration<double>(end - start).count());
    }
    // How much of the result is trivially repetitive: rows whose interior
    // (the box less 16 pixels each side) is one repeated pixel.
    const std::span<const std::uint8_t> bytes = (*raster).pixels();
    const std::size_t row_bytes = (*raster).row_bytes();
    uniform_rows = 0U;
    const std::size_t margin = static_cast<std::size_t>(outset) + 16U;
    if (extent.width > margin * 2U + 2U) {
        for (std::uint32_t row = 0U; row < extent.height; ++row) {
            const std::uint8_t* const first = bytes.data() + row * row_bytes + margin * 4U;
            bool same = true;
            for (std::size_t x = 1U; x < extent.width - margin * 2U && same; ++x) {
                same = std::memcmp(first, first + x * 4U, 4U) == 0;
            }
            if (same) uniform_rows += 1U;
        }
    }
    return best;
}


// The same bytes from less work, through the project's own region bake:
// the two side strips and one middle column are baked; every other pixel of
// a row is the middle column's pixel, copied. Valid for a look that does
// not vary across its interior (the caller's decision, made before the
// loop); checked here against the whole bake.
static void striped(const SurfaceMaterial& material, double width, double height) {
    PrimitiveTarget target{};
    target.logical_size = Size{width, height};
    target.scale = DeviceScale{1.0};
    target.material = &material;
    const double outset = engine_primitives::material_outset;
    const std::uint32_t full_w = static_cast<std::uint32_t>(std::ceil(width + outset * 2.0));
    const std::uint32_t full_h = static_cast<std::uint32_t>(std::ceil(height + outset * 2.0));
    const std::shared_ptr<PartRaster> whole = PartRaster::create(DeviceExtent{full_w, full_h}, DeviceScale{1.0});
    double whole_time = 1.0e30;
    for (int repeat = 0; repeat < 3; ++repeat) {
        const Clock::time_point start = Clock::now();
        engine_primitives::bake_material(material, target, (*whole).view(), outset);
        whole_time = std::min(whole_time, std::chrono::duration<double>(Clock::now() - start).count());
    }
    const std::uint32_t side = static_cast<std::uint32_t>(outset) + 16U;
    const std::shared_ptr<PartRaster> left = PartRaster::create(DeviceExtent{side, full_h}, DeviceScale{1.0});
    const std::shared_ptr<PartRaster> right = PartRaster::create(DeviceExtent{side, full_h}, DeviceScale{1.0});
    const std::shared_ptr<PartRaster> middle = PartRaster::create(DeviceExtent{1U, full_h}, DeviceScale{1.0});
    const std::shared_ptr<PartRaster> joined = PartRaster::create(DeviceExtent{full_w, full_h}, DeviceScale{1.0});
    double strip_time = 1.0e30;
    for (int repeat = 0; repeat < 3; ++repeat) {
        const Clock::time_point start = Clock::now();
        engine_primitives::bake_material_region(material, target, Point{-outset, -outset}, (*left).view());
        engine_primitives::bake_material_region(material, target,
                                                Point{static_cast<double>(full_w - side) - outset, -outset},
                                                (*right).view());
        engine_primitives::bake_material_region(material, target,
                                                Point{static_cast<double>(full_w / 2U) - outset, -outset},
                                                (*middle).view());
        const PartRasterView out = (*joined).view();
        const std::span<const std::uint8_t> l = (*left).pixels();
        const std::span<const std::uint8_t> r = (*right).pixels();
        const std::span<const std::uint8_t> m = (*middle).pixels();
        for (std::uint32_t row = 0U; row < full_h; ++row) {
            std::uint8_t* const to = out.pixels.data() + static_cast<std::size_t>(row) * out.row_bytes;
            std::memcpy(to, l.data() + row * (*left).row_bytes(), side * 4U);
            std::uint32_t pixel;
            std::memcpy(&pixel, m.data() + row * (*middle).row_bytes(), 4U);
            std::uint32_t* const words = reinterpret_cast<std::uint32_t*>(to + side * 4U);
            for (std::uint32_t x = 0U; x < full_w - side * 2U; ++x) words[x] = pixel;
            std::memcpy(to + static_cast<std::size_t>(full_w - side) * 4U, r.data() + row * (*right).row_bytes(), side * 4U);
        }
        strip_time = std::min(strip_time, std::chrono::duration<double>(Clock::now() - start).count());
    }
    std::size_t different = 0U;
    const std::span<const std::uint8_t> a = (*whole).pixels();
    const std::span<const std::uint8_t> b = (*joined).pixels();
    for (std::uint32_t row = 0U; row < full_h; ++row) {
        if (std::memcmp(a.data() + row * (*whole).row_bytes(), b.data() + row * (*joined).row_bytes(), full_w * 4U) != 0) {
            different += 1U;
        }
    }
    std::printf("  %4.0fx%-3.0f whole bake %7.3f ms | strips and one column %6.3f ms x%.1f (%zu of %u rows differ)\n", width,
                height, whole_time * 1.0e3, strip_time * 1.0e3, whole_time / strip_time, different, full_h);
}

} // namespace lab

int main() {
    std::vector<lab::Step> steps;
    {
        lab::Step step{"solid opaque fill", {}};
        step.material.fills.assign(1U, MaterialFillLayer::solid(Color::rgba(240, 240, 244)));
        steps.push_back(step);
    }
    {
        lab::Step step = steps.back();
        step.name = "+ border, radius 4";
        step.material.border = MaterialBorder{Color::rgba(90, 100, 120), 1.0};
        step.material.corner_radius = 4.0;
        steps.push_back(step);
    }
    {
        lab::Step step = steps.back();
        step.name = "vertical gradient + border, radius 4";
        step.material.fills.assign(
            1U, MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0},
                                          std::vector<GradientStop>{GradientStop{0.0, Color::rgba(250, 250, 252)},
                                                                    GradientStop{1.0, Color::rgba(220, 224, 232)}}));
        steps.push_back(step);
    }
    {
        lab::Step step = steps.back();
        step.name = "+ drop shadow, blur 8";
        MaterialShadow shadow{};
        shadow.offset = Point{0.0, 2.0};
        shadow.blur_radius = 8.0;
        step.material.shadows.push_back(shadow);
        steps.push_back(step);
    }
    {
        lab::Step step = steps.back();
        step.name = "+ inset shadow";
        MaterialShadow inset{};
        inset.offset = Point{0.0, 1.0};
        inset.blur_radius = 3.0;
        inset.inset = true;
        step.material.shadows.push_back(inset);
        steps.push_back(step);
    }
    const double sizes[3][2] = {{120.0, 28.0}, {400.0, 300.0}, {1000.0, 700.0}};
    for (const lab::Step& step : steps) {
        std::printf("%-38s", step.name);
        for (const double* size : sizes) {
            std::size_t pixels = 0U;
            std::size_t uniform = 0U;
            const double seconds = lab::bake_seconds(step.material, size[0], size[1], pixels, uniform);
            std::printf(" | %4.0fx%-3.0f %8.3f ms %6.1f ns/px (%3.0f%% rows uniform inside)", size[0], size[1],
                        seconds * 1.0e3, seconds * 1.0e9 / static_cast<double>(pixels),
                        100.0 * static_cast<double>(uniform) / (size[1] + 16.0));
        }
        std::printf("\n");
    }
    // Without the drop shadow (a part filter the region bake does not draw).
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        if (index == 3U) continue;
        lab::Step step = steps[index];
        if (index == 4U) step.material.shadows.erase(step.material.shadows.begin());
        std::printf("%s%s:", step.name, index == 4U ? " (inset only)" : "");
        std::printf("%c", 10);
        lab::striped(step.material, 120.0, 28.0);
        lab::striped(step.material, 400.0, 300.0);
        lab::striped(step.material, 1000.0, 700.0);
    }
    return 0;
}
