#pragma once
// The bounds every typed drag payload keeps, inbound (a DragEvent at the host
// boundary) and outbound (HostServices::start_drag): defined with the host
// session's other boundary checks, in host_session.cpp.

#include "gui_forms/events/input_events/input_events.hpp"

#include <vector>

namespace gui_forms::detail {

// Whether `effects` holds only copy, move and link (none included).
[[nodiscard]] bool known_drag_effects(DragEffect effects) noexcept;

// Whether every item is within DragLimits: at most maximum_items items, text
// and paths valid UTF-8 without NUL and within their sizes, file lists of
// 1 to maximum_paths non-empty paths, media types non-empty, and the whole
// within maximum_total_bytes. An empty list is valid here; the caller
// decides whether it may be empty.
[[nodiscard]] bool valid_drag_items(const std::vector<DragDataItem>& items) noexcept;

} // namespace gui_forms::detail
