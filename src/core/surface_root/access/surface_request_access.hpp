#pragma once

// The authority to answer and present a root's program requests (B10):
// tooltip and alert answers, the tooltip visibility change, and a
// sub-surface's program tooltip and tooltip items reaching its parent root.
//
// Holders: the tooltip routes (ToolTipPresenter, SubSurfaceToolTipRequester),
// the alert routes (WindowAlerts, SubSurfaceAlertRequester) and
// SubSurfaceHost.
//
// Stale use: the routes are owned by the root (SurfaceRoot::tool_tips_ and
// alerts_) and shut down first in the derived root's destructor, so a route
// never reaches a destroyed root. SubSurfaceHost reaches its root through
// attached_surface_root(), null once it is detached, and then does nothing.
// Answers are posted through the root's dispatcher to observed targets; a
// target released by then receives nothing. The functions are static and
// keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace gui_forms::detail {

class SurfaceRequestAccess final {
public:
    SurfaceRequestAccess() = delete;

    // Posts `outcome` to `answer` through `root`'s dispatcher.
    static void deliver_request_answer(SurfaceRoot& root, const std::weak_ptr<SurfaceRequestAnswer>& answer,
                                       const SurfaceRequestOutcome& outcome) {
        root.deliver_request_answer(answer, outcome);
    }
    // Answers tooltip request `id` with `result` when it is still current.
    static void answer_tool_tip_request(SurfaceRoot& root, std::uint64_t id, SurfaceRequestResult result) {
        root.answer_tool_tip_request(id, result);
    }
    // A tooltip visibility change, in its fixed order: the current request
    // is answered first, then tool_tip_visibility_changed is raised.
    static void publish_tool_tip_change(SurfaceRoot& root, const ToolTipEvent& change) {
        root.tool_tip_request_changed(change);
        root.tool_tip_visibility_changed_.emit(change);
    }
    // A sub-surface's program tooltip, shown for `control`'s part `item_id`
    // (SurfaceRoot::show_requested_tool_tip).
    static void show_requested_tool_tip(SurfaceRoot& root, const Control::Ptr& control, std::string item_id,
                                        std::string text, Rect anchor,
                                        std::optional<std::chrono::milliseconds> duration) {
        root.show_requested_tool_tip(control, std::move(item_id), std::move(text), anchor, duration);
    }
    // `control`'s part tooltips changed outside a layout.
    static void tool_tip_items_changed(SurfaceRoot& root, Control& control) { root.tool_tip_items_changed(control); }
};

} // namespace gui_forms::detail
