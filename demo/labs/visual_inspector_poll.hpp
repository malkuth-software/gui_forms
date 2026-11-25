#pragma once

// The labs' visual inspector polling (owner, 2026-11-01): the inspector view
// and its overlay each poll on their own paint-gated AnimationTimer once a
// second, and repaint only when what they show has changed. A settled lab so
// holds one wake a second, and none while it is hidden or covered.

#include "gui_forms_demo/visual_inspector_view.hpp"

#include <chrono>

namespace gui_forms::demo {

inline constexpr FrameInterval visual_inspector_poll_interval = std::chrono::seconds(1);

inline void start_visual_inspector_poll(VisualInspectorView& view,
                                        VisualInspectorOverlay& overlay) {
    view.start_polling(visual_inspector_poll_interval);
    overlay.start_polling(visual_inspector_poll_interval);
}

}  // namespace gui_forms::demo
