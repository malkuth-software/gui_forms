#pragma once

// The content boundary (owner decisions 2026-11-13): surfaces whose pixels a
// consumer renders on its own execution context, the sink it publishes
// through, the missed-frame watchdog and the values every hand-over is
// validated against. See docs/user/content-surfaces.md.
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/content/operation/content_operation.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/content/watchdog/content_watchdog.hpp"
#include "gui_forms/controls/content_surface.hpp"
