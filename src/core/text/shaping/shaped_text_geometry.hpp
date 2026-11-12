#pragma once

#include "gui_forms/text_shaping.hpp"

#include <cstddef>
#include <memory>

// Private renderer-neutral geometry records shared by the optional native
// shaper and retained core. No native font/library handle is stored here.
namespace gui_forms::render::text {

struct ShapedGlyph final {
    GlyphId glyph{};
    Utf8Offset cluster{};
    float x{};
    float y{};
    float advance_x{};
    float advance_y{};
    friend constexpr bool operator==(const ShapedGlyph& left, const ShapedGlyph& right) noexcept {
        const bool equal = left.glyph == right.glyph && left.cluster == right.cluster &&
            left.x == right.x && left.y == right.y && left.advance_x == right.advance_x && left.advance_y == right.advance_y;
        return equal;
    }
};

struct BoundedFontRun final {
    FontFaceId face{};
    Utf8Range source_range{};
    std::size_t glyph_begin{};
    std::size_t glyph_count{};
    // The direction the run was shaped in (its bidi level's parity).
    bool right_to_left{};
};

struct BoundedShapedText final {
    std::unique_ptr<BoundedFontRun[]> runs{};
    std::unique_ptr<ShapedGlyph[]> glyphs{};
    std::size_t run_count{};
    std::size_t glyph_count{};
    std::size_t run_capacity{};
    std::size_t glyph_capacity{};
    std::size_t controlled_output_bytes{};
    std::size_t controlled_workspace_peak{};
    double width{};
    double height{};
    double ascent{};
    double descent{};
    std::size_t missing_clusters{};
    bool missing_primary_face{};
};
} // namespace gui_forms::render::text
