#pragma once

#include "../../../core/text/shaping/shaped_text_geometry.hpp"
#include "../../../core/text/unicode/unicode_grapheme.hpp"
#include "bounded_shape_workspace.hpp"
#include "gui_forms/allocation/counted_allocator/counted_allocator.hpp"
#include "gui_forms/typography/shaping_workspace/shaping_workspace.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

struct hb_buffer_t;
// stx_text_allocation.h's arena, which only the engine's sources include.
struct StxSheenBidiArena;

namespace gui_forms::render::text {

class HarfBuzzFontEngine;

// One shaping's runs and glyphs, in the workspace's arrays: the record the
// engine's bounded shaping fills (BoundedShapedText's members, over arrays
// the workspace owns). run_capacity and glyph_capacity are the arrays'
// sizes; run_count and glyph_count the live prefix.
struct StagedShaping final {
    BoundedFontRun* runs{};
    ShapedGlyph* glyphs{};
    std::size_t run_count{};
    std::size_t glyph_count{};
    std::size_t run_capacity{};
    std::size_t glyph_capacity{};
    double width{};
    double height{};
    double ascent{};
    double descent{};
    std::size_t missing_clusters{};
    bool missing_primary_face{};
};

// The bundled engine's reusable shaping state (gui_forms::ShapingWorkspace),
// which HarfBuzzFontEngine::create_reusable_workspace creates for one engine
// and HarfBuzzFontEngine::shape_in_workspace shapes into. Reserved for texts
// of capacity_bytes() bytes, it holds every array one shaping uses:
// - the grapheme boundaries, decoded scalars, font segments, bidi runs and
//   visual segments (the bounded shaping's scratch: capacity, capacity,
//   capacity, capacity and twice capacity elements);
// - the staged runs (twice capacity: at most one a visual segment) and
//   glyphs (four a byte, plus 64);
// - the HarfBuzz buffer, kept across calls (allocated to twice capacity
//   glyphs at reserve) and lent to each shape call;
// - the SheenBidi arena (40 bytes a byte of text, plus 16 KiB), installed
//   for each call's bidi analysis. SheenBidi 2.9 asks for about 34 bytes a
//   byte at most (its types, levels, links and flags, and a run of 24 bytes
//   a byte in the worst case), with fixed-size queue blocks; a request the
//   arena cannot hold goes to the C heap and is counted there
//   (stx_text_sheenbidi_heap_allocations).
// The arrays are counted under AllocationSubsystem::text_layout; the
// grapheme workspace's own arrays and the HarfBuzz buffer are not (the
// buffer is HarfBuzz's, counted by stx_text_harfbuzz_allocations).
// Single-threaded: one shaping at a time, on the thread that installs the
// arena.
class ReusableShapeWorkspace final : public ShapingWorkspace {
public:
    explicit ReusableShapeWorkspace(std::uint64_t engine_identity);
    ~ReusableShapeWorkspace() override;

    [[nodiscard]] std::size_t capacity_bytes() const noexcept override;
    void reserve(std::size_t text_bytes) override;
    [[nodiscard]] ShapedGlyphCounts shaped_counts() const noexcept override;
    [[nodiscard]] GlyphSource glyph_source() const noexcept override;
    [[nodiscard]] bool fill_cluster_advances(std::span<double> advances) const noexcept override;
    [[nodiscard]] bool copy_glyphs(Utf8Offset text_offset, std::size_t first_glyph, std::span<GlyphRunRecord> runs,
                                   std::span<PositionedGlyph> glyphs) const noexcept override;

private:
    friend class HarfBuzzFontEngine;

    template <typename Value>
    using CountedArray = std::vector<Value, gui_forms::detail::CountedAllocator<Value, AllocationSubsystem::text_layout>>;
    // One aligned unit of the SheenBidi arena (STX_SHEENBIDI_ARENA_ALIGNMENT
    // bytes; reusable_shape_workspace.cpp checks the two agree).
    struct alignas(16) ArenaUnit final {
        unsigned char bytes[16];
    };

    // Forgets the last shaping: the reads answer none until the next.
    void forget_shaping() noexcept;
    // Records a completed shaping of `text_bytes` bytes by the engine in the
    // face table `generation`.
    void keep_shaping(std::size_t text_bytes, std::uint64_t generation) noexcept;

    std::uint64_t engine_{};
    std::size_t capacity_{};
    detail::GraphemeWorkspace graphemes_{};
    CountedArray<char32_t> scalars_{};
    CountedArray<WorkspaceFontSegment> segments_{};
    CountedArray<WorkspaceDirectionRun> directions_{};
    CountedArray<WorkspaceVisualSegment> visual_{};
    CountedArray<BoundedFontRun> runs_{};
    CountedArray<ShapedGlyph> glyphs_{};
    CountedArray<ArenaUnit> arena_units_{};
    // The arena over arena_units_, created with the workspace.
    std::unique_ptr<StxSheenBidiArena> arena_;
    std::unique_ptr<hb_buffer_t, void (*)(hb_buffer_t*)> buffer_;
    // Pointers into runs_ and glyphs_, refreshed at reserve.
    StagedShaping staged_{};
    // The last shaping: whether there is one, its text's size and the face
    // table generation it was shaped in.
    bool shaped_{};
    std::size_t shaped_bytes_{};
    std::uint64_t shaped_generation_{};
};

} // namespace gui_forms::render::text
