#pragma once

#include "../chunk/display_chunk.hpp"

namespace gui_forms::detail {

[[nodiscard]] std::uint64_t replay_display_chunk(const DisplayChunk& chunk,
                                                 Painter& painter);
// As replay_display_chunk, for a chunk replayed under `clip` (its own
// coordinates, the painter's current clip). When the chunk's occluder
// (DisplayChunk::occluder) is a live surface the painter promises to cover
// opaquely, the commands beneath it are replayed only into the parts of
// `clip` it leaves showing, at most four bands, and not at all when it
// covers the clip; then the rest of the chunk is replayed whole. The pixels
// are those of replay_display_chunk. Returns the commands replayed.
[[nodiscard]] std::uint64_t replay_display_chunk_occluded(const DisplayChunk& chunk,
                                                          Painter& painter, Rect clip);

} // namespace gui_forms::detail
