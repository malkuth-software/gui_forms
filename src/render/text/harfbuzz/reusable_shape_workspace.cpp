#include "reusable_shape_workspace.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace gui_forms::render::text {
namespace {

// Staged glyphs: four a byte, plus 64.
constexpr std::size_t glyphs_per_text_byte = 4U;
constexpr std::size_t glyph_margin = 64U;

[[nodiscard]] std::size_t checked_product(const std::size_t count, const std::size_t factor) {
    if (factor != 0U && count > std::numeric_limits<std::size_t>::max() / factor) {
        throw std::length_error("Shaping workspace size arithmetic overflow");
    }
    const std::size_t product = count * factor;
    return product;
}

[[nodiscard]] std::size_t checked_sum(const std::size_t left, const std::size_t right) {
    if (left > std::numeric_limits<std::size_t>::max() - right) {
        throw std::length_error("Shaping workspace size arithmetic overflow");
    }
    const std::size_t sum = left + right;
    return sum;
}

} // namespace

ReusableShapeWorkspace::ReusableShapeWorkspace(const std::uint64_t engine_identity)
    : engine_(engine_identity) {}

ReusableShapeWorkspace::~ReusableShapeWorkspace() = default;

std::size_t ReusableShapeWorkspace::capacity_bytes() const noexcept {
    return capacity_;
}

void ReusableShapeWorkspace::reserve(const std::size_t text_bytes) {
    if (text_bytes <= capacity_) return;
    // Sizes first, then every replacement, then publication: a failure
    // leaves the workspace as it was.
    const std::size_t visual_capacity = checked_product(text_bytes, 2U);
    const std::size_t glyph_capacity = checked_sum(checked_product(text_bytes, glyphs_per_text_byte), glyph_margin);
    const std::size_t scratch_glyphs = checked_product(text_bytes, 2U);
    const detail::GraphemeStorageRequirement grapheme = detail::grapheme_workspace_requirement(text_bytes);
    if (grapheme.status != detail::GraphemeStorageStatus::success) {
        throw std::length_error("Shaping workspace grapheme capacity arithmetic overflow");
    }
    detail::GraphemeWorkspace graphemes{};
    const detail::GraphemeStorageStatus prepared = graphemes.prepare(text_bytes, grapheme.peak_bytes);
    if (prepared == detail::GraphemeStorageStatus::resource_failure) throw std::bad_alloc();
    if (prepared != detail::GraphemeStorageStatus::success) {
        throw std::length_error("Shaping workspace grapheme preparation failed");
    }
    CountedArray<char32_t> scalars(text_bytes);
    CountedArray<WorkspaceFontSegment> segments(text_bytes);
    CountedArray<WorkspaceDirectionRun> directions(text_bytes);
    CountedArray<WorkspaceVisualSegment> visual(visual_capacity);
    CountedArray<BoundedFontRun> runs(visual_capacity);
    CountedArray<ShapedGlyph> glyphs(glyph_capacity);
    ShapeScratch scratch{};
    scratch.characters.reserve(text_bytes);
    scratch.clusters.reserve(text_bytes);
    const stxlib::OperationStatus glyph_room = scratch.glyphs.reserve(scratch_glyphs);
    if (glyph_room.failed()) throw std::bad_alloc();
    // Nothing below throws.
    graphemes_ = std::move(graphemes);
    scalars_ = std::move(scalars);
    segments_ = std::move(segments);
    directions_ = std::move(directions);
    visual_ = std::move(visual);
    runs_ = std::move(runs);
    glyphs_ = std::move(glyphs);
    scratch_ = std::move(scratch);
    staged_ = StagedShaping{};
    staged_.runs = runs_.data();
    staged_.glyphs = glyphs_.data();
    staged_.run_capacity = runs_.size();
    staged_.glyph_capacity = glyphs_.size();
    capacity_ = text_bytes;
    forget_shaping();
}

ShapedGlyphCounts ReusableShapeWorkspace::shaped_counts() const noexcept {
    ShapedGlyphCounts counts{};
    if (!shaped_) return counts;
    counts.runs = staged_.run_count;
    counts.glyphs = staged_.glyph_count;
    return counts;
}

GlyphSource ReusableShapeWorkspace::glyph_source() const noexcept {
    GlyphSource source{};
    if (!shaped_) return source;
    source.engine = engine_;
    source.generation = shaped_generation_;
    return source;
}

bool ReusableShapeWorkspace::fill_cluster_advances(const std::span<double> advances) const noexcept {
    if (!shaped_ || advances.size() != shaped_bytes_) return false;
    std::fill(advances.begin(), advances.end(), 0.0);
    // A glyph's cluster is the byte offset of its cluster's first byte in
    // the shaped text, in either direction; the order does not matter to a
    // sum (HarfBuzzFontEngine::fill_cluster_advances_utf8 reads shape()'s
    // glyphs the same way).
    const std::size_t glyph_count = staged_.glyph_count;
    for (std::size_t index = 0U; index < glyph_count; ++index) {
        const ShapedGlyph& glyph = staged_.glyphs[index];
        const std::size_t cluster = glyph.cluster.value();
        if (cluster >= shaped_bytes_) continue;
        advances[cluster] += static_cast<double>(glyph.advance_x);
    }
    return true;
}

bool ReusableShapeWorkspace::copy_glyphs(const Utf8Offset text_offset, const std::size_t first_glyph,
                                         const std::span<GlyphRunRecord> runs,
                                         const std::span<PositionedGlyph> glyphs) const noexcept {
    if (!shaped_ || runs.size() < staged_.run_count || glyphs.size() < staged_.glyph_count) return false;
    const std::size_t offset = text_offset.value();
    const std::size_t run_count = staged_.run_count;
    for (std::size_t index = 0U; index < run_count; ++index) {
        const BoundedFontRun& staged = staged_.runs[index];
        GlyphRunRecord record{};
        record.source_range = Utf8Range{Utf8Offset(staged.source_range.start.value() + offset),
                                        Utf8Offset(staged.source_range.end.value() + offset)};
        record.face = staged.face;
        record.direction = staged.right_to_left ? TextDirection::right_to_left : TextDirection::left_to_right;
        record.first_glyph = first_glyph + staged.glyph_begin;
        record.glyph_count = staged.glyph_count;
        runs[index] = record;
    }
    const std::size_t glyph_count = staged_.glyph_count;
    for (std::size_t index = 0U; index < glyph_count; ++index) {
        const ShapedGlyph& staged = staged_.glyphs[index];
        PositionedGlyph glyph{};
        glyph.glyph = staged.glyph;
        glyph.cluster = Utf8Offset(staged.cluster.value() + offset);
        glyph.x = staged.x;
        glyph.y = staged.y;
        glyphs[index] = glyph;
    }
    return true;
}

void ReusableShapeWorkspace::forget_shaping() noexcept {
    staged_.run_count = 0U;
    staged_.glyph_count = 0U;
    staged_.width = 0.0;
    staged_.height = 0.0;
    staged_.ascent = 0.0;
    staged_.descent = 0.0;
    staged_.missing_clusters = 0U;
    staged_.missing_primary_face = false;
    shaped_ = false;
    shaped_bytes_ = 0U;
    shaped_generation_ = 0U;
}

void ReusableShapeWorkspace::keep_shaping(const std::size_t text_bytes, const std::uint64_t generation) noexcept {
    shaped_ = true;
    shaped_bytes_ = text_bytes;
    shaped_generation_ = generation;
}

} // namespace gui_forms::render::text
