#pragma once

#include "../../../core/text/shaping/shaped_text_geometry.hpp"
#include "../../../core/text/unicode/unicode_grapheme.hpp"
#include "../textlayout/text_analysis.hpp"
#include "gui_forms/allocation/counted_allocator/counted_allocator.hpp"

#include <stxlib/glyph_shape.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gui_forms::render::text {

class HarfBuzzFontEngine;
struct ShapeStorageLimits;

// One shape call's working storage for STXlib's shaper: a run's characters
// and, for each, its cluster (the byte offset of its first byte in the
// shaped text), counted under AllocationSubsystem::text_layout, and the
// glyphs the shaper returns (STXlib's storage). Reused from run to run. A
// reusable or bounded workspace holds one, reserved for its capacity, and lends it to
// each call by moving it (ScratchLoan), which allocates nothing.
struct ShapeScratch final {
    using CountedScalars =
        std::vector<std::uint32_t, gui_forms::detail::CountedAllocator<std::uint32_t, AllocationSubsystem::text_layout>>;
    CountedScalars characters{};
    CountedScalars clusters{};
    stxlib::Vector<stxlib::glyphs::ShapedGlyph> glyphs{};
};

// Face indices refer only to the current engine's stable registration table.
// They are overwritten before use on each call; no native face borrow persists.
struct WorkspaceFontSegment final {
    std::size_t face_index{};
    Utf8Range range{};
};
struct WorkspaceDirectionRun final {
    Utf8Range range{};
    bool rtl{};
};
struct WorkspaceVisualSegment final {
    std::size_t face_index{};
    Utf8Range range{};
    bool rtl{};
};

// Private, serial-executor scratch. Prepare before repeated shaping. No native
// owners, input borrows, callbacks or retained output aliases are kept here.
// Nonmovable so the executor's scratch identity cannot migrate during a call.
class BoundedShapeWorkspace final {
public:
    BoundedShapeWorkspace() = default;
    ~BoundedShapeWorkspace() = default;
    BoundedShapeWorkspace(const BoundedShapeWorkspace&) = delete;
    BoundedShapeWorkspace& operator=(const BoundedShapeWorkspace&) = delete;
    BoundedShapeWorkspace(BoundedShapeWorkspace&&) = delete;
    BoundedShapeWorkspace& operator=(BoundedShapeWorkspace&&) = delete;
    // Includes this owner and its controlled arrays, not the engine/call records.
    [[nodiscard]] std::size_t controlled_bytes() const noexcept;
private:
    friend class HarfBuzzFontEngine;
    void prepare(const ShapeStorageLimits& limits, std::size_t external_bytes);
    void clear_result() noexcept;
    // Copies only completed active values. No workspace array is transferred.
    [[nodiscard]] std::unique_ptr<BoundedShapedText> copy_result(
        std::size_t output_limit, std::size_t workspace_peak) const;

    detail::GraphemeWorkspace graphemes_{};
    std::unique_ptr<char32_t[]> scalars_{};
    std::unique_ptr<WorkspaceFontSegment[]> segments_{};
    std::unique_ptr<WorkspaceDirectionRun[]> directions_{};
    std::unique_ptr<WorkspaceVisualSegment[]> visual_{};
    BoundedShapedText staging_{};
    // The shaping scratch and the bidi analysis, kept across calls and lent
    // to each (as ReusableShapeWorkspace's): both grow to the largest text
    // shaped so far on the first calls and are then reused, so a prepared
    // shaping allocates only its result. Neither is reserved by prepare nor
    // counted in controlled_bytes (the scratch's glyphs are STXlib's storage;
    // the analysis counts its own bytes under text_layout).
    TextAnalysis analysis_{};
    ShapeScratch scratch_{};
    std::size_t scalar_capacity_{};
    std::size_t visual_capacity_{};
    std::size_t array_bytes_{};
};

} // namespace gui_forms::render::text
