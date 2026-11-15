#pragma once

#include "../harfbuzz/harfbuzz_font_engine.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gui_forms::render::text {

// ADR-004 text rasterization: FreeType light auto-hinting, grayscale, no stem
// darkening, glyphs placed in integer 26.6 device units. Any change to this
// profile increments the version so retained coverage is never reused.
inline constexpr std::uint32_t glyph_run_profile_version = 1U;

// A run's device origin. x keeps its subpixel phase in 1/64 pixel; the
// baseline y is snapped to a whole device pixel so light hinting's vertical
// alignment lands on the grid.
struct GlyphRunOrigin final {
    std::int64_t pixel_x{};
    std::int64_t pixel_y{};
    std::int32_t phase_x{};
};

// Device origin for a run whose logical origin maps to (device_x, device_y).
[[nodiscard]] GlyphRunOrigin glyph_run_origin(double device_x, double device_y) noexcept;

// Grayscale coverage, 0..255, row-major with stride == width. (left, top) is
// relative to the origin's (pixel_x, pixel_y).
struct GlyphRunCoverage final {
    std::vector<std::uint8_t> pixels;
    std::uint32_t width{};
    std::uint32_t height{};
    std::int32_t left{};
    std::int32_t top{};
    // Glyphs whose face the engine could not provide; they draw nothing.
    std::uint32_t missing_glyphs{};
    // Glyphs left out because they lie wholly outside the run's window; 0
    // when the coverage holds the whole run.
    std::uint32_t outside_glyphs{};
    // The shown glyphs' coverage would have been wider or taller than
    // glyph_run_maximum_extent: nothing is drawn (pixels is empty) and the
    // caller counts the run. Never an exception, so painting goes on.
    bool oversized{};
};

// Coverage wider or taller than this, in device pixels, is not produced.
inline constexpr std::int64_t glyph_run_maximum_extent = 16384;

// The device pixels a run can show, [left, right) x [top, bottom), relative
// to the coverage's origin pixel (GlyphRunOrigin's pixel_x and pixel_y, or
// floor of the transformed run's device origin). A glyph wholly outside is
// neither rendered nor kept, so the coverage is bounded by what the surface
// shows rather than by the length of the text.
struct GlyphRunWindow final {
    std::int64_t left{};
    std::int64_t top{};
    std::int64_t right{};
    std::int64_t bottom{};
};

// Synthesized styles for faces a family lacks (the C drawing API). Bold
// thickens outlines by a size-dependent amount; oblique shears by 0.25.
struct GlyphRunSynthesis final {
    bool embolden{};
    bool oblique{};
};

// Linear map from logical (y down) to device pixels (y down):
// device = [xx xy; yx yy] * logical. Used for rotated or skewed text.
struct GlyphRunTransform final {
    double xx{1.0};
    double xy{0.0};
    double yx{0.0};
    double yy{1.0};
};

// Rasterizes HarfBuzz-shaped runs. It opens its own FreeType faces, at the
// device size, from the engine's registered font bytes; the engine's shaping
// faces are never resized. Not thread-safe: one rasterizer per executor.
class GlyphRunRasterizer final {
public:
    explicit GlyphRunRasterizer(const HarfBuzzFontEngine& engine);
    ~GlyphRunRasterizer();
    GlyphRunRasterizer(const GlyphRunRasterizer&) = delete;
    GlyphRunRasterizer& operator=(const GlyphRunRasterizer&) = delete;

    // shaped holds logical positions relative to the run origin (y down).
    // scale is device pixels per logical unit; phase_x is the origin's
    // subpixel phase. Overlapping glyphs combine as c + p * (255 - c) / 255.
    // window, when given, limits the glyphs rendered to those that can show.
    // FreeType failures throw; the coverage is then not produced. A run too
    // large to cover is reported as GlyphRunCoverage::oversized, not thrown.
    [[nodiscard]] GlyphRunCoverage rasterize(const ShapedText& shaped, FontSpec font, double scale,
                                             std::int32_t phase_x, GlyphRunSynthesis synthesis = {},
                                             std::optional<GlyphRunWindow> window = std::nullopt);

    // General affine placement: the run origin maps to (device_x, device_y)
    // and glyph positions through transform. Hinting is off (it assumes
    // axis alignment). Coverage is relative to (floor(device_x),
    // floor(device_y)), returned in origin.
    [[nodiscard]] GlyphRunCoverage rasterize_transformed(const ShapedText& shaped, FontSpec font,
                                                         GlyphRunTransform transform, double device_x,
                                                         double device_y, GlyphRunSynthesis synthesis,
                                                         GlyphRunOrigin& origin,
                                                         std::optional<GlyphRunWindow> window = std::nullopt);

    class Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace gui_forms::render::text
