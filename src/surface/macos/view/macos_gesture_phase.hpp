#pragma once

// A macOS touchpad gesture event as the portable GestureEvent names it
// (macos_view_input.mm's magnifyWithEvent:, rotateWithEvent: and
// smartMagnifyWithEvent:; tested by macos_scroll_phase_tests). AppKit types
// stay inside src/surface/macos.

#import <AppKit/AppKit.h>

#include "gui_forms/events/gesture_events/gesture_events.hpp"

#include <cmath>

namespace gui_forms::host::macos {

// A magnify or rotate event's place in its gesture. AppKit sends began,
// changed... and ended or cancelled; may-begin and stationary (not sent for
// these gestures, but defined for NSEvent.phase) continue one.
inline gui_forms::GesturePhase gesture_phase_for(const NSEventPhase phase) noexcept {
    if ((phase & NSEventPhaseBegan) != 0) return gui_forms::GesturePhase::began;
    if ((phase & NSEventPhaseEnded) != 0) return gui_forms::GesturePhase::ended;
    if ((phase & NSEventPhaseCancelled) != 0) return gui_forms::GesturePhase::cancelled;
    return gui_forms::GesturePhase::changed;
}

// NSEvent.magnification is the change to add to the scale (0.1 grows it by
// a tenth); the portable scale multiplies, so it is 1 + magnification. A
// value at or below -1 (or not finite), which no trackpad reports, would
// make the scale zero or negative: it is taken as no change.
inline double magnify_scale_for(const double magnification) noexcept {
    if (!std::isfinite(magnification) || magnification <= -1.0) return 1.0;
    const double scale = 1.0 + magnification;
    return scale;
}

} // namespace gui_forms::host::macos
