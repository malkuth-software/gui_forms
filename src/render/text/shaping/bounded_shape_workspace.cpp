#include "bounded_shape_workspace.hpp"
#include "text_shape_engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms::render::text {
namespace {

void charge_array(const std::size_t count, const std::size_t element_bytes,
    const std::size_t limit, std::size_t& total) {
    if (total > limit || count > (limit - total) / element_bytes) {
        throw std::length_error("Reusable shaping storage exceeds its limit");
    }
    const std::size_t bytes = count * element_bytes;
    total += bytes;
}

} // namespace

std::size_t BoundedShapeWorkspace::controlled_bytes() const noexcept {
    const std::size_t bytes = sizeof(BoundedShapeWorkspace) + array_bytes_;
    return bytes;
}

void BoundedShapeWorkspace::prepare(const ShapeStorageLimits& limits,
    const std::size_t external_bytes) {
    const ShapeStorageLimits ceiling{};
    if (limits.input_bytes == 0U || limits.input_bytes > ceiling.input_bytes ||
        limits.runs == 0U || limits.runs > ceiling.runs ||
        limits.glyphs == 0U || limits.glyphs > ceiling.glyphs ||
        limits.workspace_bytes > ceiling.workspace_bytes) {
        throw std::length_error("Reusable shaping profile exceeds declared ceilings");
    }
    std::size_t live_bytes = external_bytes;
    const std::size_t current_bytes = controlled_bytes();
    charge_array(current_bytes, 1U, limits.workspace_bytes, live_bytes);
    if (scalar_capacity_ >= limits.input_bytes && staging_.run_capacity >= limits.runs &&
        staging_.glyph_capacity >= limits.glyphs) {
        return;
    }
    // During preparation both caller-owned workspace records and both sets of
    // arrays are live. Reuse does not create the second owner or any arrays.
    charge_array(1U, sizeof(BoundedShapeWorkspace), limits.workspace_bytes, live_bytes);
    const detail::GraphemeStorageRequirement grapheme =
        detail::grapheme_workspace_requirement(limits.input_bytes);
    if (grapheme.status != detail::GraphemeStorageStatus::success ||
        limits.input_bytes > std::numeric_limits<std::size_t>::max() / 2U) {
        throw std::length_error("Reusable shaping capacity arithmetic overflow");
    }
    const std::size_t visual_capacity = limits.input_bytes * 2U;
    std::size_t arrays = 0U;
    const std::size_t remaining = limits.workspace_bytes - live_bytes;
    charge_array(grapheme.peak_bytes, 1U, remaining, arrays);
    charge_array(limits.input_bytes, sizeof(char32_t), remaining, arrays);
    charge_array(limits.input_bytes, sizeof(WorkspaceFontSegment), remaining, arrays);
    charge_array(limits.input_bytes, sizeof(WorkspaceDirectionRun), remaining, arrays);
    charge_array(visual_capacity, sizeof(WorkspaceVisualSegment), remaining, arrays);
    charge_array(limits.runs, sizeof(BoundedFontRun), remaining, arrays);
    charge_array(limits.glyphs, sizeof(ShapedGlyph), remaining, arrays);

    BoundedShapeWorkspace candidate{};
    const detail::GraphemeStorageStatus prepared =
        candidate.graphemes_.prepare(limits.input_bytes, grapheme.peak_bytes);
    if (prepared == detail::GraphemeStorageStatus::resource_failure) throw std::bad_alloc();
    if (prepared != detail::GraphemeStorageStatus::success) {
        throw std::length_error("Reusable grapheme preparation failed");
    }
    candidate.scalars_ = std::make_unique<char32_t[]>(limits.input_bytes);
    candidate.segments_ = std::make_unique<WorkspaceFontSegment[]>(limits.input_bytes);
    candidate.directions_ = std::make_unique<WorkspaceDirectionRun[]>(limits.input_bytes);
    candidate.visual_ = std::make_unique<WorkspaceVisualSegment[]>(visual_capacity);
    candidate.staging_.runs = std::make_unique<BoundedFontRun[]>(limits.runs);
    candidate.staging_.glyphs = std::make_unique<ShapedGlyph[]>(limits.glyphs);
    // No throwing operation follows publication. Old arrays retire as their
    // replacements are adopted, after all allocation/initialization succeeds.
    graphemes_ = std::move(candidate.graphemes_);
    scalars_ = std::move(candidate.scalars_);
    segments_ = std::move(candidate.segments_);
    directions_ = std::move(candidate.directions_);
    visual_ = std::move(candidate.visual_);
    staging_ = std::move(candidate.staging_);
    staging_.run_capacity = limits.runs;
    staging_.glyph_capacity = limits.glyphs;
    scalar_capacity_ = limits.input_bytes;
    visual_capacity_ = visual_capacity;
    array_bytes_ = arrays;
}

void BoundedShapeWorkspace::clear_result() noexcept {
    staging_.run_count = 0U;
    staging_.glyph_count = 0U;
    staging_.width = 0.0;
    staging_.height = 0.0;
    staging_.ascent = 0.0;
    staging_.descent = 0.0;
    staging_.missing_clusters = 0U;
    staging_.missing_primary_face = false;
}

std::unique_ptr<BoundedShapedText> BoundedShapeWorkspace::copy_result(
    const std::size_t output_limit, const std::size_t workspace_peak) const {
    std::size_t bytes = 0U;
    charge_array(1U, sizeof(BoundedShapedText), output_limit, bytes);
    charge_array(staging_.run_count, sizeof(BoundedFontRun), output_limit, bytes);
    charge_array(staging_.glyph_count, sizeof(ShapedGlyph), output_limit, bytes);
    std::unique_ptr<BoundedShapedText> owner = std::make_unique<BoundedShapedText>();
    BoundedShapedText& output = *owner;
    if (staging_.run_count != 0U) {
        output.runs = std::make_unique<BoundedFontRun[]>(staging_.run_count);
        std::copy_n(staging_.runs.get(), staging_.run_count, output.runs.get());
    }
    if (staging_.glyph_count != 0U) {
        output.glyphs = std::make_unique<ShapedGlyph[]>(staging_.glyph_count);
        std::copy_n(staging_.glyphs.get(), staging_.glyph_count, output.glyphs.get());
    }
    output.run_count = staging_.run_count;
    output.glyph_count = staging_.glyph_count;
    output.run_capacity = staging_.run_count;
    output.glyph_capacity = staging_.glyph_count;
    output.width = staging_.width;
    output.height = staging_.height;
    output.ascent = staging_.ascent;
    output.descent = staging_.descent;
    output.missing_clusters = staging_.missing_clusters;
    output.missing_primary_face = staging_.missing_primary_face;
    output.controlled_output_bytes = bytes;
    output.controlled_workspace_peak = workspace_peak;
    return owner;
}

} // namespace gui_forms::render::text
