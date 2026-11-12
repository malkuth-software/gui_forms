#pragma once

#include "../command/display_command.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gui_forms::detail {

// The last live surface a chunk draws where it may hide everything recorded
// before it (replay_display_chunk_occluded). It may lie inside saves (a
// window's frame records each control inside a save, a translation to its
// corner and a clip), provided nothing open around it is a rounded clip or a
// paint group, and it precedes any translation or clip at the chunk's top
// level (state a replay of the commands beneath it, inside a band's save,
// would drop). Whether it is opaque is the painter's answer at replay
// (Painter::opaque_live_surface_cover).
struct DisplayOccluder final {
    // The draw_live_surface command. Every command before it lies beneath
    // it.
    std::size_t command{};
    // The first command of the outermost save open at the draw, or the
    // draw itself at the top level, and how many saves are open there.
    std::size_t group_start{};
    std::size_t open_saves{};
    // The translations in force at the draw, summed: the surface's
    // coordinates are the chunk's moved by this.
    Point offset{};
    // The rectangle clips in force at the draw, intersected, in the
    // chunk's coordinates, when `clipped`.
    bool clipped{};
    Rect clip{};
};

class DisplayChunk final {
public:
    DisplayChunk(std::uint64_t generation, PaintPlane plane,
                 Rect logical_bounds, std::vector<DisplayCommand> commands,
                 std::optional<Rect> opaque_cover = std::nullopt);

    [[nodiscard]] DisplayChunkInfo info() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] PaintPlane plane() const noexcept { return plane_; }
    [[nodiscard]] Rect logical_bounds() const noexcept { return logical_bounds_; }
    [[nodiscard]] const std::vector<DisplayCommand>& commands() const& noexcept {
        return commands_;
    }
    const std::vector<DisplayCommand>& commands() const&& = delete;
    // A rectangle, in the chunk's own coordinates, that replaying it leaves
    // opaque whatever lay beneath (RecordingPainter::mark_opaque_cover), or
    // none.
    [[nodiscard]] std::optional<Rect> opaque_cover() const noexcept { return opaque_cover_; }
    // Found once, when the chunk is made.
    [[nodiscard]] const std::optional<DisplayOccluder>& occluder() const noexcept { return occluder_; }

private:
    std::uint64_t generation_{};
    PaintPlane plane_{PaintPlane::control};
    Rect logical_bounds_{};
    std::vector<DisplayCommand> commands_;
    std::optional<Rect> opaque_cover_;
    std::optional<DisplayOccluder> occluder_;
};

} // namespace gui_forms::detail
