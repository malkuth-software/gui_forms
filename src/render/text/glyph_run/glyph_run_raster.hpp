#pragma once

#include "../harfbuzz/harfbuzz_font_engine.hpp"

#include <stxlib/glyph_face.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gui_forms::render::text {

// ADR-004 text rasterization, drawn by STXlib's glyph face
// (stxlib/glyph_face.hpp): light auto-hinting along y, grey coverage, glyphs
// placed in integer 26.6 device units. STXlib changes face_profile_version
// whenever its output for the same input would change; retained coverage
// keeps the version it was made under (GlyphRunCoverage::profile_version)
// and is made again when it differs.
inline constexpr std::uint32_t face_profile_version = stxlib::glyphs::face_profile_version;

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
    // The face_profile_version the coverage was made under.
    std::uint32_t profile_version{};
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
// thickens outlines by a size-dependent amount (STXlib's DrawStyle::thicken,
// its own integer rule); oblique shears by 0.25 (the DrawStyle matrix, which
// STXlib takes only unhinted, so oblique text is drawn unhinted).
struct GlyphRunSynthesis final {
    bool thicken{};
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

// Rasterizes HarfBuzz-shaped runs through STXlib's glyph face. For each face
// the engine registered it opens one face of its own, on first use, over the
// program's typeface of the same bytes (program_typefaces.hpp: the
// registry's for a bundled or application face), so the glyphs read for one
// executor serve every other; the engine's shaping faces are never resized.
// A face over an unloaded application family's typeface is rejected and
// draws nothing. Its faces' bytes are counted under AllocationSubsystem::text_layout.
// Not thread-safe: one rasterizer per executor.
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
    // A glyph the face cannot give is counted (missing_glyphs) and draws
    // nothing. A bundled face that will not open, or a run STXlib refuses
    // (its storage exhausted), throws std::runtime_error; the coverage is
    // then not produced. An application family's face quarantines the family
    // instead and its glyphs count as missing. A run too large to cover is
    // reported as GlyphRunCoverage::oversized, not thrown.
    [[nodiscard]] GlyphRunCoverage rasterize(const ShapedText& shaped, FontSpec font, double scale,
                                             std::int32_t phase_x, GlyphRunSynthesis synthesis = {},
                                             std::optional<GlyphRunWindow> window = std::nullopt);

    // General affine placement: the run origin maps to (device_x, device_y)
    // and glyph positions through transform. A transform that is an even
    // scale and a turn (xx = yy and xy = -yx, without synthetic oblique) is
    // drawn as rasterize_turned draws it, its turn rounded to 16.16 entries.
    // Any other is the unhinted outline (Fit::none) through the matrix. A transform entry of 32768 or more in
    // magnitude, beyond a 16.16 matrix, is refused with invalid_argument. Coverage is relative to (floor(device_x),
    // floor(device_y)), returned in origin.
    [[nodiscard]] GlyphRunCoverage rasterize_transformed(const ShapedText& shaped, FontSpec font,
                                                         GlyphRunTransform transform, double device_x,
                                                         double device_y, GlyphRunSynthesis synthesis,
                                                         GlyphRunOrigin& origin,
                                                         std::optional<GlyphRunWindow> window = std::nullopt);

    // Turned text as STXlib draws it: the run turned anticlockwise as seen
    // (on the y-down surface) by `degrees` about its origin, which lands at
    // (device_x, device_y), at `scale` device pixels per logical unit. The
    // turn is STXlib's turn_matrix of the angle in 16.16
    // (detail::text_turn_entries, the turn rotated_text_bounds uses). The
    // glyphs are fitted (Fit::light): at a quarter turn each is the upright
    // mask turned pixel for pixel; at other angles, up to STXlib's lattice
    // size (20 pixels), the upright mask read through its CONV surface; above
    // it, the fitted outline turned. STXlib turns the places too
    // (DrawStyle::turn_places), each rounded once to a 64th of a pixel.
    // Synthetic oblique is not a turn and is drawn by the outline as
    // rasterize_transformed draws it. Not used at 0 degrees, where callers
    // draw upright text with rasterize. Coverage is relative to
    // (floor(device_x), floor(device_y)), returned in origin;
    // invalid_argument for a non-finite angle, scale or origin.
    [[nodiscard]] GlyphRunCoverage rasterize_turned(const ShapedText& shaped, FontSpec font, double degrees,
                                                    double scale, double device_x, double device_y,
                                                    GlyphRunSynthesis synthesis, GlyphRunOrigin& origin,
                                                    std::optional<GlyphRunWindow> window = std::nullopt);

    class Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace gui_forms::render::text
