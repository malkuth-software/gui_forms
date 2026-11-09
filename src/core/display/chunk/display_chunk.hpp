#pragma once

#include "../command/display_command.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gui_forms::detail {

// The last live surface a chunk draws where it may hide everything recorded
// before it (replay_display_chunk_occluded): at the chunk's top level, or
// inside one save at that level with no translation, rounded clip or paint
// group around it, and before any top-level translation or clip (state a
// replay of the commands beneath it, inside a band's save, would drop).
// Whether it is opaque is the painter's answer at replay
// (Painter::opaque_live_surface_cover).
struct DisplayOccluder final {
    // The first command of what draws the surface: the save that opens its
    // group, or the draw itself at the top level. Commands before it lie
    // beneath the surface.
    std::size_t group_start{};
    // The draw_live_surface command.
    std::size_t command{};
    // The rectangle clips of its group, intersected, when `clipped`.
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
    [[nodiscard]] const std::vector<DisplayCommand>& commands() const noexcept {
        return commands_;
    }
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
