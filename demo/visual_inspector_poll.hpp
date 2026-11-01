#pragma once

// The labs' visual inspector polling: one UI timer that asks the inspector
// view and its overlay to repaint when what they show has changed. An active
// surface would repaint both on every tick, and the overlay spans the window,
// so it repainted the whole window four times a second with nothing changed.

#include "gui_forms/controls/visual_inspector_view/visual_inspector_view.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <memory>

namespace gui_forms::demo {

inline constexpr FrameInterval visual_inspector_poll_interval = std::chrono::milliseconds(250);

// The timer's named target. Observes both controls; a destroyed one is skipped.
struct VisualInspectorPoll final {
    std::weak_ptr<VisualInspectorView> view;
    std::weak_ptr<VisualInspectorOverlay> overlay;

    void operator()(FrameTime) const {
        const std::shared_ptr<VisualInspectorView> live_view = view.lock();
        if (live_view) static_cast<void>((*live_view).refresh_if_changed());
        const std::shared_ptr<VisualInspectorOverlay> live_overlay = overlay.lock();
        if (live_overlay) static_cast<void>((*live_overlay).refresh_if_changed());
    }
};

// Schedules the poll on `window`, owned by `owner` (the lab's root).
[[nodiscard]] inline FrameRequestToken schedule_visual_inspector_poll(
    Window& window, Component& owner, const std::shared_ptr<VisualInspectorView>& view,
    const std::shared_ptr<VisualInspectorOverlay>& overlay) {
    const FrameTime first = FrameClock::now() + visual_inspector_poll_interval;
    FrameRequestToken token = window.schedule_ui_timer(
        owner, visual_inspector_poll_interval, first, VisualInspectorPoll{view, overlay});
    return token;
}

}  // namespace gui_forms::demo
