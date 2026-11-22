#pragma once

// Stage MCP G6's inspect sections (study 18.1), registered with the
// automation's tool registry: `summary` (the window in one paragraph),
// `layout_reasons` (why the target control has its place and size, with
// VA's overflow records; the core's `layout` gives the bounds) and
// `resources` (images and counted allocations). The
// text comes from src/core/model_inspection. Event propagation is the
// core's own `events` section (track M2).

#include "../tools/automation_tools.hpp"

namespace gui_forms::automation {

// Adds the three sections; a section already registered under one of the
// names is left as it is. Returns how many were added.
std::size_t register_model_inspect_sections(ToolRegistry& tools);

} // namespace gui_forms::automation
