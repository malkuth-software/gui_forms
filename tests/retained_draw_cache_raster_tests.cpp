#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "skia_raster.hpp"
#include "../src/core/display/chunk/display_chunk.hpp"
#include "../src/core/display/recording_painter/recording_painter.hpp"
#include "../src/core/display/replay/replay_display_chunk.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

// Renderer data kept with a retained command must not change a pixel: each
// cached replay is compared byte for byte with the direct draw it replaces.
namespace {
using namespace gui_forms;

void require(const bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct ShadowCase final {
    Rect rect{};
    double radius{};
    Point offset{};
    double blur{};
    double spread{};
    Color color{};
};

// Varied opaque and translucent destination pixels beneath the shadow.
void paint_background(render::SkiaRaster& raster, const Size size) {
    raster.fill_rect({0.0, 0.0, size.width, size.height}, Color::rgba(236, 240, 228, 255));
    for (int stripe = 0; stripe < 12; ++stripe) {
        const double x = static_cast<double>(stripe) * size.width / 12.0;
        const std::uint8_t level = static_cast<std::uint8_t>(30 + stripe * 18);
        raster.fill_rect({x, 0.0, size.width / 24.0, size.height}, Color::rgba(level, 90, 200 - level / 2, 255));
    }
}

[[nodiscard]] std::vector<std::byte> snapshot(const render::SkiaRaster& raster, const std::size_t height) {
    const std::byte* const bytes = static_cast<const std::byte*>(raster.pixels());
    require(bytes != nullptr, "raster readable");
    return std::vector<std::byte>(bytes, bytes + raster.row_bytes() * height);
}

enum class ShadowDraw : std::uint8_t { direct, retained, inset };

[[nodiscard]] std::vector<std::byte> draw_shadow(const ShadowCase& shadow, const double scale, const Point origin,
                                                 const ShadowDraw mode,
                                                 const std::shared_ptr<RetainedDrawCache>& cache) {
    const Size size{180.0, 90.0};
    render::SkiaRaster raster{};
    require(raster.resize(size, scale), "raster allocated");
    DamageRegion damage{};
    damage.add({0.0, 0.0, size.width, size.height});
    raster.begin_frame(damage);
    paint_background(raster, size);
    raster.save();
    raster.translate(origin);
    if (mode == ShadowDraw::direct) {
        raster.draw_box_shadow(shadow.rect, shadow.radius, shadow.offset, shadow.blur, shadow.spread, shadow.color);
    } else if (mode == ShadowDraw::inset) {
        raster.draw_inset_box_shadow(shadow.rect, shadow.radius, shadow.offset, shadow.blur, shadow.spread,
                                     shadow.color);
    } else {
        raster.draw_retained_box_shadow(shadow.rect, shadow.radius, shadow.offset, shadow.blur, shadow.spread,
                                        shadow.color, cache);
    }
    raster.restore();
    raster.end_frame();
    const std::size_t height = static_cast<std::size_t>(raster.pixel_height());
    return snapshot(raster, height);
}

void shadow_masks_match_direct_blur() {
    const std::vector<ShadowCase> shadows{
        {{20.0, 20.0, 120.0, 24.0}, 12.0, {0.0, 4.0}, 12.0, 0.0, Color::rgba(0, 0, 0, 120)},
        {{30.0, 18.0, 40.0, 30.0}, 3.0, {0.0, 2.0}, 2.0, 0.0, Color::rgba(10, 18, 40, 70)},
        {{60.0, 25.0, 50.0, 30.0}, 7.0, {0.0, 0.0}, 9.0, 3.0, Color::rgba(255, 220, 103, 100)},
        {{25.0, 30.0, 90.0, 20.0}, 0.0, {3.0, -2.0}, 5.0, -2.0, Color::rgba(40, 0, 10, 255)}};
    const std::vector<double> scales{1.0, 2.0, 1.5};
    const std::vector<Point> origins{{0.0, 0.0}, {0.25, 0.5}, {3.3, 1.7}};
    for (const ShadowCase& shadow : shadows) {
        for (const double scale : scales) {
            const std::shared_ptr<RetainedDrawCache> cache = std::make_shared<RetainedDrawCache>();
            for (const Point origin : origins) {
                const std::vector<std::byte> direct = draw_shadow(shadow, scale, origin, ShadowDraw::direct, cache);
                // First replay builds the mask; the second reuses it.
                const std::vector<std::byte> built = draw_shadow(shadow, scale, origin, ShadowDraw::retained, cache);
                const std::vector<std::byte> reused = draw_shadow(shadow, scale, origin, ShadowDraw::retained, cache);
                require(built == direct, "cached shadow mask matches the direct blur");
                require(reused == direct, "reused shadow mask matches the direct blur");
            }
            // Square corners draw as Skia's kept nine patches, never a mask.
            const bool square = !(shadow.radius + shadow.spread > 0.0);
            require(!square || !(*cache).memo, "a square-cornered shadow keeps no coverage mask");
        }
    }
}

// A blur radius the mask filter refuses (past float range, infinite or NaN) draws no
// shadow on any path, never a sharp one; a radius whose mask is too large to keep draws
// directly. Each refused case must leave the background exactly as it was.
void refused_blurs_draw_no_shadow() {
    const Color shadow_color = Color::rgba(0, 0, 0, 200);
    const ShadowCase invisible{{20.0, 20.0, 120.0, 24.0}, 6.0, {0.0, 4.0}, 4.0, 0.0, Color::rgba(0, 0, 0, 0)};
    const std::vector<double> refused{1.0e39, std::numeric_limits<double>::infinity(),
                                      std::numeric_limits<double>::quiet_NaN()};
    for (const double scale : {1.0, 2.0}) {
        const std::shared_ptr<RetainedDrawCache> unused = std::make_shared<RetainedDrawCache>();
        const std::vector<std::byte> background =
            draw_shadow(invisible, scale, {0.0, 0.0}, ShadowDraw::direct, unused);
        for (const double blur : refused) {
            const ShadowCase shadow{{20.0, 20.0, 120.0, 24.0}, 6.0, {0.0, 4.0}, blur, 0.0, shadow_color};
            const std::shared_ptr<RetainedDrawCache> cache = std::make_shared<RetainedDrawCache>();
            require(draw_shadow(shadow, scale, {0.0, 0.0}, ShadowDraw::direct, cache) == background,
                    "a refused blur draws no direct shadow");
            require(draw_shadow(shadow, scale, {0.0, 0.0}, ShadowDraw::retained, cache) == background,
                    "a refused blur draws no retained shadow");
            require(draw_shadow(shadow, scale, {0.0, 0.0}, ShadowDraw::inset, cache) == background,
                    "a refused blur draws no inset shadow");
            require(!(*cache).memo, "a refused blur keeps no shadow mask");
        }
        // Past the kept extent the retained path draws directly, so the two agree.
        const ShadowCase wide{{20.0, 20.0, 120.0, 24.0}, 6.0, {0.0, 4.0}, 12000.0, 0.0, shadow_color};
        const std::shared_ptr<RetainedDrawCache> cache = std::make_shared<RetainedDrawCache>();
        const std::vector<std::byte> direct = draw_shadow(wide, scale, {0.0, 0.0}, ShadowDraw::direct, cache);
        require(draw_shadow(wide, scale, {0.0, 0.0}, ShadowDraw::retained, cache) == direct,
                "a shadow too wide to keep draws as the direct blur");
        require(!(*cache).memo, "a shadow too wide to keep keeps no mask");
    }
}

[[nodiscard]] std::vector<std::byte> draw_label(render::SkiaRaster& raster, const std::string& text, const bool retained,
                                                const std::shared_ptr<RetainedDrawCache>& cache) {
    const Size size{220.0, 40.0};
    DamageRegion damage{};
    damage.add({0.0, 0.0, size.width, size.height});
    raster.begin_frame(damage);
    paint_background(raster, size);
    const FontSpec font{.role = FontRole::control, .size = 15.0};
    if (retained) {
        raster.draw_retained_text_utf8({6.25, 26.5}, text, font, Color::rgba(20, 20, 30, 255), cache);
    } else {
        raster.draw_text_utf8({6.25, 26.5}, text, font, Color::rgba(20, 20, 30, 255));
    }
    raster.end_frame();
    const std::size_t height = static_cast<std::size_t>(raster.pixel_height());
    return snapshot(raster, height);
}

void shaped_text_matches_direct_shaping() {
    render::SkiaRaster raster{};
    require(raster.resize({220.0, 40.0}, 2.0), "text raster allocated");
    require(raster.register_typeface_file(FontRole::control, 400, false, GUI_FORMS_TEST_CONTROL_FONT),
            "control font registered");
    const std::string label = "Help \xe2\x80\x94 Pen the Sheep \xf0\x9f\x90\x91";
    const std::shared_ptr<RetainedDrawCache> cache = std::make_shared<RetainedDrawCache>();
    const std::vector<std::byte> direct = draw_label(raster, label, false, cache);
    require(draw_label(raster, label, true, cache) == direct, "shaped text matches direct shaping");
    require(static_cast<bool>((*cache).memo), "shaping kept with the retained command");
    require(draw_label(raster, label, true, cache) == direct, "reused shaping matches direct shaping");

    // A newly registered fallback changes shaping of the missing emoji.
    require(raster.register_fallback_typeface_file(400, false, GUI_FORMS_TEST_FALLBACK_FONT),
            "fallback font registered");
    const std::vector<std::byte> fallback = draw_label(raster, label, false, cache);
    require(draw_label(raster, label, true, cache) == fallback, "registration invalidates kept shaping");

    // A memo from another raster is never trusted.
    render::SkiaRaster other{};
    require(other.resize({220.0, 40.0}, 2.0), "second raster allocated");
    require(other.register_typeface_file(FontRole::control, 400, false, GUI_FORMS_TEST_CONTROL_FONT),
            "second raster font registered");
    const std::vector<std::byte> other_direct = draw_label(other, label, false, cache);
    require(draw_label(other, label, true, cache) == other_direct, "another raster reshapes");
}

void recording_shares_cache_across_replays() {
    detail::RecordingPainter recorder{};
    recorder.draw_text_utf8({1.0, 2.0}, "Help", {}, Color::rgba(0, 0, 0, 255));
    recorder.draw_box_shadow({0.0, 0.0, 10.0, 10.0}, 2.0, {0.0, 1.0}, 4.0, 0.0, Color::rgba(0, 0, 0, 90));
    const std::shared_ptr<const detail::DisplayChunk> chunk =
        recorder.finish(1U, PaintPlane::control, {0.0, 0.0, 20.0, 20.0});
    detail::RecordingPainter transaction{};
    static_cast<void>(detail::replay_display_chunk(*chunk, transaction));
    const std::shared_ptr<const detail::DisplayChunk> copy =
        transaction.finish(2U, PaintPlane::control, {0.0, 0.0, 20.0, 20.0});
    require((*chunk).commands().size() == 2U && (*copy).commands().size() == 2U, "commands replayed");
    for (std::size_t index = 0; index < 2U; ++index) {
        require((*chunk).commands()[index].retained_cache != nullptr, "recorded command owns a cache");
        require((*chunk).commands()[index].retained_cache == (*copy).commands()[index].retained_cache,
                "a transaction copy shares the recording's cache");
    }
    detail::RecordingPainter rebuilt{};
    rebuilt.draw_text_utf8({1.0, 2.0}, "Help", {}, Color::rgba(0, 0, 0, 255));
    const std::shared_ptr<const detail::DisplayChunk> fresh =
        rebuilt.finish(3U, PaintPlane::control, {0.0, 0.0, 20.0, 20.0});
    require((*fresh).commands()[0].retained_cache != (*chunk).commands()[0].retained_cache,
            "a rebuilt recording starts a new cache");
}

} // namespace

int main() {
    shadow_masks_match_direct_blur();
    refused_blurs_draw_no_shadow();
    shaped_text_matches_direct_shaping();
    recording_shares_cache_across_replays();
    return 0;
}
