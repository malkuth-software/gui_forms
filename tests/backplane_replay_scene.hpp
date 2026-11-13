#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/part_table/engine_part_table.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "support/theme_definition_heap.hpp"
#include "support/rgba_order.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The backplane replay through a host's CPU raster (stage T5, phase C6;
// notes:planning/stages/T5-compositor/PLAN.md §4.3 and §5 C6): the window's
// renderer replays the backplane plane, text included, into the image glass
// samples. windows_backplane_replay_tests runs it through the Windows DIB
// painter and backplane_replay_identity_tests through the Skia raster,
// against the same table, so the glass over replayed text has the same
// bytes on every host (ADR-004's text identity, carried through the
// replay).
//
// A backplane-plane control paints a light page, a dark band and two text
// runs in Carlito (the content face). A control-plane control lays one
// glass region over most of it, with a light blur. The hash is the glass
// part raster's rows (premultiplied RGBA), at each scale.
namespace gui_forms::backplane_replay_scene {

inline constexpr std::array<double, 3> scales{1.0, 1.5, 2.0};
inline constexpr Size window_size{240.0, 100.0};
inline constexpr Rect glass_rect{10.0, 10.0, 220.0, 80.0};

// Expected FNV-1a hashes of the glass raster's rows, [scale], from
// SkiaRaster, the one CPU painter. First recorded through the Windows DIB
// painter (Windows x64, 2026-10-26) and checked against SkiaRaster on macOS
// and Linux; re-recorded through SkiaRaster on Windows x64 (clang 22.1.8),
// 2026-11-02 (SK5), unchanged.
inline constexpr std::array<std::uint64_t, 3> expected{
    0x14e91c188a71dda6ULL,
    0xe31497238996ea16ULL,
    0xfd9daabeb75fa8eeULL,
};

// The window's own paint: keeps the part rasters it is given, draws
// nothing else.
class RasterWatch final : public Painter {
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
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        rasters.push_back(raster);
    }
    std::vector<std::shared_ptr<const PartRaster>> rasters;
};

// The backplane: a light page, a dark band over its lower half, and a text
// run on each.
class Page final : public Control {
public:
    explicit Page(StableId stable_id) : Control(std::move(stable_id)) { set_paint_plane(PaintPlane::backplane); }
    void on_paint(Painter& painter, Rect bounds) override {
        painter.fill_rect(bounds, Color::rgba(250, 248, 240, 255));
        painter.fill_rect(Rect{bounds.x, bounds.y + 50.0, bounds.width, 50.0}, Color::rgba(40, 70, 110, 255));
        const FontSpec font{.role = FontRole::content, .size = 15.0};
        painter.draw_text_utf8(Point{bounds.x + 16.0, bounds.y + 32.0}, "Hamburgefonstiv glass", font,
                               Color::rgba(20, 20, 30, 255));
        painter.draw_text_utf8(Point{bounds.x + 16.0, bounds.y + 80.0}, "quick brown fox jumps", font,
                               Color::rgba(245, 245, 250, 255));
    }
};

// The control plane: one glass region at glass_rect.
class Pane final : public Control {
public:
    explicit Pane(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::region;
        part.family = ControlVisualRole::card;
        part.bounds = glass_rect;
        part.corner_radius = 0.0;
        part.key = PartKey{"glass", {}, {}};
        paint_part(painter, *this, part);
    }
};

// The professional theme with a glass rule for regions, lightly blurred so
// the text under it stays legible in the hash.
inline std::shared_ptr<const Theme> glass_theme() {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "backplane-replay-scene";
    ThemeRule rule{};
    rule.label = "glass";
    rule.selector.role = PartRole::region;
    rule.declarations.push_back(RuleDeclaration::primitive("glass"));
    rule.declarations.push_back(RuleDeclaration::number_value("blur", 1.0));
    definition.rules.rules.push_back(rule);
    std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

// One window of the scene; owns its controls. The renderer it is given is
// borrowed and must outlive it.
struct SceneWindow final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Page> page;
    std::shared_ptr<Pane> pane;
    std::unique_ptr<Window> window;
    RasterWatch watch{};
};

// Makes the window at `scale` with `renderer` as its framebuffer painter,
// lays it out and paints it once.
inline std::unique_ptr<SceneWindow> make_scene(Painter& renderer, double scale) {
    std::unique_ptr<SceneWindow> held = std::make_unique<SceneWindow>();
    SceneWindow& scene = *held;
    scene.root = make_control<Panel>(StableId("root"));
    scene.page = make_control<Page>(StableId("page"));
    (*scene.page).set_requested_bounds(Rect{0.0, 0.0, window_size.width, window_size.height});
    scene.pane = make_control<Pane>(StableId("pane"));
    (*scene.pane).set_requested_bounds(Rect{0.0, 0.0, window_size.width, window_size.height});
    (*scene.root).add_child(scene.page);
    (*scene.root).add_child(scene.pane);
    scene.window = std::make_unique<Window>(scene.root, window_size);
    Window& window = *scene.window;
    window.set_theme(glass_theme());
    window.set_scale(scale);
    window.set_framebuffer_painter(&renderer);
    window.perform_layout();
    window.paint(scene.watch);
    static_cast<void>(window.take_damage());
    return held;
}

// Backplane damage, then the repaint that replays the backplane again.
inline void replay_again(SceneWindow& scene) {
    (*scene.page).invalidate(Dirty::paint);
    Window& window = *scene.window;
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(scene.watch, damage.bounds());
}

// The last part raster of the glass's extent at `scale` (glass_rect's
// sides are whole device pixels at every scale of the table).
inline std::shared_ptr<const PartRaster> last_glass(const SceneWindow& scene, double scale) {
    const std::uint32_t width = static_cast<std::uint32_t>(glass_rect.width * scale);
    const std::uint32_t height = static_cast<std::uint32_t>(glass_rect.height * scale);
    for (std::size_t index = scene.watch.rasters.size(); index > 0U; --index) {
        const std::shared_ptr<const PartRaster>& raster = scene.watch.rasters[index - 1U];
        const DeviceExtent extent = (*raster).extent();
        if (extent.width == width && extent.height == height) return raster;
    }
    return nullptr;
}

// FNV-1a over the raster's rows of width * 4 bytes.
inline std::uint64_t glass_hash(const PartRaster& raster) {
    std::uint64_t value = 1469598103934665603ULL;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    const std::size_t width_bytes = static_cast<std::size_t>(raster.extent().width) * 4U;
    for (std::uint32_t row = 0U; row < raster.extent().height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * raster.row_bytes();
        for (const std::uint8_t byte : gui_forms::test_support::rgba_order(pixels.subspan(start, width_bytes))) {
            value ^= byte;
            value *= 1099511628211ULL;
        }
    }
    return value;
}

// What one scale's scene read.
struct SceneReading final {
    bool glass_found{};
    std::uint64_t hash{};
    std::uint64_t replays{};
    std::uint64_t skipped_commands{};
    bool metrics_replay{};
};

inline SceneReading read_scene(const SceneWindow& scene, double scale) {
    SceneReading reading{};
    const std::shared_ptr<const PartRaster> glass = last_glass(scene, scale);
    reading.glass_found = glass != nullptr;
    if (glass != nullptr) reading.hash = glass_hash(*glass);
    const RasterCacheStatistics statistics = (*scene.window).engine_parts().cache_statistics();
    reading.replays = statistics.backplane_replays;
    reading.skipped_commands = statistics.backplane_skipped_commands;
    reading.metrics_replay = (*scene.window).metrics().snapshot().backplane_replay;
    return reading;
}

} // namespace gui_forms::backplane_replay_scene
