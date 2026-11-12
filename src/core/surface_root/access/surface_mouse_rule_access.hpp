#pragma once

// The authority to offer a window's rules a MouseDown or MouseUp: Control's
// base on_mouse_down and on_mouse_up offer the event before raising it to
// the subscribers (the owner's Q-C5: rules before subscribers), so that a
// consuming rule keeps the event from the subscribers and not from the
// control's own handling in an override.
//
// Holder: Control (its two raisers).
//
// Stale use: the raiser reads its window when it runs and offers nothing
// without one. A rule's action may close the window; the offer holds the
// rule set for its own duration (SurfaceRoot::offer_mouse_rules) and the
// raiser touches the window no further. The function is static and keeps
// nothing.

#include "gui_forms/surface_root/surface_root.hpp"

namespace gui_forms::detail {

class SurfaceMouseRuleAccess final {
public:
    SurfaceMouseRuleAccess() = delete;

    // Whether a rule consumed `event` on `control`.
    static bool offer(SurfaceRoot& root, MouseRuleEvent event, Control& control, const MouseEventArgs& arguments) {
        const bool consumed = root.offer_mouse_rules(event, control, &arguments);
        return consumed;
    }
};

} // namespace gui_forms::detail
