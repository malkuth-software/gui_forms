#pragma once

// A control's border state (Control::set_active_border), private to the
// core: control.cpp destroys it with the control, and
// control_active_border.cpp runs it.

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control/active_border/active_border.hpp"
#include "gui_forms/control/control/control.hpp"

#include <memory>
#include <stxlib/optional.hpp>

namespace gui_forms {

// Allocated while a border is set; the control owns it.
struct Control::ActiveBorderState final {
    ActiveBorder border{};
    // The pose the last paint resolved; empty until the first paint after
    // a change.
    stxlib::Optional<ActiveBorderPose> pose{};
    // Started while the pose moves, stopped when it rests.
    AnimationTimer timer{};
    // When the motion started (lit), and the time the last tick reached:
    // the paint shows the colour at shown - start.
    FrameTime start{};
    FrameTime shown{};
};

// The timer's target: observes the control, so a destroyed control's tick
// does nothing.
struct Control::ActiveBorderTick final {
    std::weak_ptr<Control> control;
    AnimationTickResult operator()(FrameTime now) const;
};

} // namespace gui_forms
