#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/text_shaping/types/text_shaping_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

// Shaped glyphs as output, in caller storage, and the reusable shaping
// workspace that fills them (paragraph layout's two follow-ups,
// include/gui_forms/text/paragraph_layout/). Nothing here names a text
// engine's or renderer's own type: glyph and face identities are the
// engine's numbers, meaningful only to the engine that shaped them, and the
// workspace is an opaque object its provider creates and reads.

// Which engine shaped a set of glyphs, and in which state of its face table:
// a face identity means nothing to another engine, and a glyph id nothing to
// another face. `engine` is a process-unique number the engine was given
// when it was created (never zero); `generation` changes whenever a face is
// registered with it, since registration can change which face shapes a
// cluster. A painter draws glyph runs only when their source is its own
// engine's current one, and otherwise draws their text (Painter::
// draw_glyph_runs). The zero source is no engine's.
struct GUI_FORMS_API GlyphSource final {
    std::uint64_t engine{};
    std::uint64_t generation{};
    [[nodiscard]] constexpr bool valid() const noexcept { return engine != 0U; }
    friend constexpr bool operator==(const GlyphSource& left, const GlyphSource& right) noexcept {
        return left.engine == right.engine && left.generation == right.generation;
    }
};

// One glyph as the engine placed it: its id in its run's face, the byte
// offset of the first byte of its cluster (in the text the offsets of its
// run index), and its pen position in logical pixels from the run sequence's
// origin on the baseline (x right, y down), offsets and letter spacing
// included. These are the positions Painter::draw_text_utf8 draws the same
// text at.
struct GUI_FORMS_API PositionedGlyph final {
    GlyphId glyph{};
    Utf8Offset cluster{};
    float x{};
    float y{};
    friend constexpr bool operator==(const PositionedGlyph& left, const PositionedGlyph& right) noexcept {
        return left.glyph == right.glyph && left.cluster == right.cluster && left.x == right.x && left.y == right.y;
    }
};

// One run of glyphs shaped in one face and one direction, in visual order
// within its sequence: the bytes it shapes, its face (the engine's
// identity), its direction, and its glyphs, glyph_count elements from
// first_glyph of the glyph array its sequence is stored with.
struct GUI_FORMS_API GlyphRunRecord final {
    Utf8Range source_range{};
    FontFaceId face{};
    TextDirection direction{TextDirection::left_to_right};
    std::size_t first_glyph{};
    std::size_t glyph_count{};
    friend constexpr bool operator==(const GlyphRunRecord& left, const GlyphRunRecord& right) noexcept {
        return left.source_range == right.source_range && left.face == right.face &&
               left.direction == right.direction && left.first_glyph == right.first_glyph &&
               left.glyph_count == right.glyph_count;
    }
};

// The runs and glyphs one shaping produced.
struct GUI_FORMS_API ShapedGlyphCounts final {
    std::size_t runs{};
    std::size_t glyphs{};
    friend constexpr bool operator==(const ShapedGlyphCounts& left, const ShapedGlyphCounts& right) noexcept {
        return left.runs == right.runs && left.glyphs == right.glyphs;
    }
};

// One line's glyph runs as a painter draws them (Painter::draw_glyph_runs),
// borrowed for the call:
// - source: the engine that shaped them;
// - runs: the line's runs in visual order; each names its glyphs in `glyphs`
//   by index;
// - text: the line's text, which a painter that cannot draw the runs draws
//   with draw_text_utf8 instead;
// - text_offset: where `text` begins in the offsets the runs' ranges and the
//   glyphs' clusters use (the paragraph's, for a paragraph's line).
struct GUI_FORMS_API GlyphRunsView final {
    GlyphSource source{};
    std::span<const GlyphRunRecord> runs{};
    std::span<const PositionedGlyph> glyphs{};
    std::string_view text{};
    Utf8Offset text_offset{};
};

// A text metrics provider's reusable shaping state
// (TextMetricsProvider::create_shaping_workspace): the provider creates it,
// the caller owns it (ParagraphBuffers holds one), and the provider shapes
// into it (TextMetricsProvider::shape_in_workspace_utf8) without allocating
// once it holds the text's size. It keeps the last shaping until the next
// one, which the reads below answer from. Use it with the provider that
// created it, on one thread at a time; a provider refuses a workspace of
// another (invalid_request).
class GUI_FORMS_API ShapingWorkspace {
public:
    virtual ~ShapingWorkspace() = default;
    ShapingWorkspace(const ShapingWorkspace&) = delete;
    ShapingWorkspace& operator=(const ShapingWorkspace&) = delete;
    ShapingWorkspace(ShapingWorkspace&&) = delete;
    ShapingWorkspace& operator=(ShapingWorkspace&&) = delete;

    // The longest text, in bytes, one shaping takes without allocating.
    [[nodiscard]] virtual std::size_t capacity_bytes() const noexcept = 0;
    // Grows to shape texts of `text_bytes` bytes (never shrinks). It may
    // allocate, and throws std::bad_alloc or std::length_error with the
    // workspace unchanged when it cannot; it forgets the last shaping.
    virtual void reserve(std::size_t text_bytes) = 0;
    // The last shaping's runs and glyphs; zero when there is none (none yet,
    // or the last one was refused).
    [[nodiscard]] virtual ShapedGlyphCounts shaped_counts() const noexcept = 0;
    // The engine and face table of the last shaping; the zero source when
    // there is none.
    [[nodiscard]] virtual GlyphSource glyph_source() const noexcept = 0;
    // Fills `advances` with the last shaping's cluster advances, under
    // TextMetricsProvider::fill_cluster_advances_utf8's contract; its size
    // must equal the shaped text's. False, writing nothing, when it does not
    // or there is no shaping.
    [[nodiscard]] virtual bool fill_cluster_advances(std::span<double> advances) const noexcept = 0;
    // Copies the last shaping's runs into `runs` and its glyphs into
    // `glyphs` (at least shaped_counts() of each; the rest is left as it
    // was), with `text_offset` added to every range and cluster and
    // `first_glyph` to every run's first_glyph, so that a run's glyphs are
    // found at its first_glyph in the array `glyphs` begins at element
    // first_glyph of. False, writing nothing, when either span is too short
    // or there is no shaping.
    [[nodiscard]] virtual bool copy_glyphs(Utf8Offset text_offset, std::size_t first_glyph,
                                           std::span<GlyphRunRecord> runs,
                                           std::span<PositionedGlyph> glyphs) const noexcept = 0;

protected:
    ShapingWorkspace() = default;
};

} // namespace gui_forms
