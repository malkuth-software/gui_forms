#import <AppKit/AppKit.h>

#include "../src/surface/macos/view/macos_scroll_phase.hpp"

#include <cstdio>
#include <cstdlib>

// The macOS host's scroll gesture mapping (cp33): AppKit's contact and
// momentum phases, as a trackpad gesture reports them in order, become the
// portable WheelPhase the window latches by. The latching itself is tested
// headlessly (scrollable_control_tests,
// test_scroll_gesture_latches_and_does_not_chain).

namespace {

using gui_forms::WheelPhase;
using gui_forms::host::macos::wheel_phase_for;

// One AppKit event's phases and the WheelPhase it must map to.
struct PhaseCase final {
    const char* name{};
    NSEventPhase phase{NSEventPhaseNone};
    NSEventPhase momentum{NSEventPhaseNone};
    WheelPhase expected{WheelPhase::none};
};

} // namespace

int main() {
    // A whole trackpad gesture in AppKit's order, then the other phases.
    const PhaseCase cases[] = {
        {"detented wheel", NSEventPhaseNone, NSEventPhaseNone, WheelPhase::none},
        {"fingers down", NSEventPhaseMayBegin, NSEventPhaseNone, WheelPhase::began},
        {"contact began", NSEventPhaseBegan, NSEventPhaseNone, WheelPhase::began},
        {"contact changed", NSEventPhaseChanged, NSEventPhaseNone, WheelPhase::changed},
        {"contact stationary", NSEventPhaseStationary, NSEventPhaseNone, WheelPhase::changed},
        {"contact ended", NSEventPhaseEnded, NSEventPhaseNone, WheelPhase::ended},
        {"momentum began", NSEventPhaseNone, NSEventPhaseBegan, WheelPhase::momentum},
        {"momentum changed", NSEventPhaseNone, NSEventPhaseChanged, WheelPhase::momentum},
        {"momentum ended", NSEventPhaseNone, NSEventPhaseEnded, WheelPhase::momentum_ended},
        {"momentum cancelled", NSEventPhaseNone, NSEventPhaseCancelled, WheelPhase::momentum_ended},
        {"contact cancelled", NSEventPhaseCancelled, NSEventPhaseNone, WheelPhase::cancelled},
    };
    int failures = 0;
    for (const PhaseCase& entry : cases) {
        const WheelPhase mapped = wheel_phase_for(entry.phase, entry.momentum);
        if (mapped != entry.expected) {
            std::printf("FAIL %s: mapped to %d, expected %d\n", entry.name, static_cast<int>(mapped),
                        static_cast<int>(entry.expected));
            ++failures;
        }
    }
    std::printf("gui_forms_macos_scroll_phase_tests: %d failures\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
