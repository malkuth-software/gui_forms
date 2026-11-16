#pragma once

#include "../chunk/display_chunk.hpp"

#include <array>

namespace gui_forms::detail {

[[nodiscard]] std::uint64_t replay_display_chunk(const DisplayChunk& chunk,
                                                 Painter& painter);
// As replay_display_chunk, for a chunk replayed under `clip` (its own
// coordinates, within the painter's current clip). When the chunk's
// occluder (DisplayChunk::occluder) is a live surface the painter promises
// to cover opaquely, everything before it is replayed only into the parts
// of `clip` it leaves showing, at most four bands, and not at all when it
// covers the clip; then the state open at the surface is restored without
// drawing, and the surface and the rest are replayed. The pixels are those
// of replay_display_chunk. Returns the commands replayed.
[[nodiscard]] std::uint64_t replay_display_chunk_occluded(const DisplayChunk& chunk,
                                                          Painter& painter, Rect clip);

// The occlusion split replay_display_chunk_occluded makes, shared with the
// blend study's walk of the same commands (count_display_blend), so the
// two agree on what was drawn. The rectangle of `clip` the chunk's occluder
// hides, asked of `painter` in its current state, or empty when the chunk
// has no occluder past its first command, the clip is empty or the painter
// promises no cover. Reads only.
[[nodiscard]] Rect display_chunk_hidden_rect(const DisplayChunk& chunk, Painter& painter, Rect clip);
// `clip` less `hidden`: full-width bands above and below it, then the parts
// beside it between them; any may be empty.
[[nodiscard]] std::array<Rect, 4> display_occlusion_bands(Rect clip, Rect hidden) noexcept;
// Whether a command changes only the painter's state (what a replay of the
// saves open at an occluder must reproduce without drawing). Paint groups
// are excluded: none is open at an occluder, and a closed one leaves no
// state behind.
[[nodiscard]] bool display_state_only(DisplayOperation operation) noexcept;

} // namespace gui_forms::detail
