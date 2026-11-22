#pragma once

// What `inspect` adds for a model that cannot see the screen (stage MCP
// G6; study section 18.1): plain text, bounded, opt-in per call.
//
// - **state summary**: one paragraph about a window, made from its semantic
//   snapshot: what it is, what has focus, what is selected, checked,
//   expanded, busy, invalid or disabled, and what waits on the user (a
//   modal node, an enabled default action). A protected node's value
//   (a password) is never written: only that it is filled or empty.
// - **layout reasons**: why a control has its place and size: what it
//   asked for and what it was given, its own layout properties, the
//   properties its parent gives it (cell, spans, flex), the constraints
//   holding it (at its minimum or maximum size), the window's overflow
//   records (stage VA: OverflowRecord, its cause, extents and answer) for
//   the control and its parent, and whether it lies outside its parent.
// - **resources**: the window's image registry and the process's counted
//   allocations by subsystem.
//
// Event propagation (the route an input took) needs a recorder in the
// surface root's routing and is not here yet (track M3's next step).
//
// Internal: track M2's inspect tool calls these; nothing public changes.

#include "gui_forms/allocation/statistics/allocation_statistics.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"

#include <cstddef>
#include <string>

namespace gui_forms::model_inspection {

// The longest summary written; the rest is cut at a word with "...".
inline constexpr std::size_t default_summary_bytes = 1200U;
// The most nodes a summary or a list in it names.
inline constexpr std::size_t maximum_summary_items = 8U;

// The paragraph for `snapshot` (Window::semantic_snapshot), at most
// `maximum_bytes` (at least 64) long. Visits at most the snapshot's
// node_count nodes, iteratively.
[[nodiscard]] std::string window_state_summary(const SemanticSnapshot& snapshot,
                                               std::size_t maximum_bytes = default_summary_bytes);

// The reasons for `control`'s layout, one per line, as of its last
// arrangement. UI thread.
[[nodiscard]] std::string layout_reasons(const Control& control);

// The image registry's snapshot (Window::image_resource_snapshot) and the
// counted allocations (allocation_statistics), as a few lines.
[[nodiscard]] std::string resource_summary(const ImageRegistrySnapshot& images,
                                           const AllocationStatistics& allocations);

} // namespace gui_forms::model_inspection
