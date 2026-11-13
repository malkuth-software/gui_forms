#pragma once

// A macOS scroll event's place in its gesture, as the portable WheelPhase
// names it (macos_view_input.mm's scrollWheel:; tested by
// macos_scroll_phase_tests). AppKit types stay inside src/host/macos.

#import <AppKit/AppKit.h>

#include "gui_forms/events/input_events/input_events.hpp"

namespace gui_forms::host::macos {

// The momentum phase when AppKit reports one, else the contact phase.
// May-begin (fingers down, not yet moving) starts a gesture, so it latches
// and stops the last gesture's momentum as AppKit's own scroll views do;
// stationary continues one. A detented wheel reports neither phase: none.
inline gui_forms::WheelPhase wheel_phase_for(const NSEventPhase phase, const NSEventPhase momentum) noexcept {
    if (momentum != NSEventPhaseNone) {
        if ((momentum & (NSEventPhaseEnded | NSEventPhaseCancelled)) != 0) return gui_forms::WheelPhase::momentum_ended;
        return gui_forms::WheelPhase::momentum;
    }
    if ((phase & (NSEventPhaseBegan | NSEventPhaseMayBegin)) != 0) return gui_forms::WheelPhase::began;
    if ((phase & (NSEventPhaseChanged | NSEventPhaseStationary)) != 0) return gui_forms::WheelPhase::changed;
    if ((phase & NSEventPhaseEnded) != 0) return gui_forms::WheelPhase::ended;
    if ((phase & NSEventPhaseCancelled) != 0) return gui_forms::WheelPhase::cancelled;
    return gui_forms::WheelPhase::none;
}

} // namespace gui_forms::host::macos
