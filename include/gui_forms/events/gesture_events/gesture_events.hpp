#pragma once
// Touchpad gestures (owner, 2026-11-22: "we support gestures like pinch. it
// will be needed on every laptop in the world"). One event family for the
// gestures a precision touchpad performs, as each platform reports them
// (the platform table is notes:dev/GESTURES.md):
//   magnify: two fingers pinching or spreading (macOS magnifyWithEvent:,
//     X11 XI 2.4 pinch, Windows DirectManipulation's zoom). Some platforms
//     report the fingers' turn and travel in the same event: rotation and
//     translation then carry them, so a canvas can zoom, turn and pan from
//     one gesture.
//   rotate: two fingers turning, where the platform reports it apart from
//     the pinch (macOS rotateWithEvent:).
//   smart_magnify: a two-finger double tap (macOS smartMagnifyWithEvent:):
//     one event, phase ended, the gesture's position its focal point; the
//     control decides what it toggles (zoom to fit, back to 100%).
//   swipe: a multi-finger swipe, translation per event, with the system's
//     momentum after the fingers leave where the platform gives it.
// Scrolling stays the wheel event (PointerAction::wheel, WheelPhase), which
// already carries a precise device's phases and momentum.
//
// Every event carries the change since the previous event of its gesture as
// one transform: scale (multiplied: 1 changes nothing), rotation_degrees
// (counter-clockwise, added) and translation (logical units, added), about
// the focal point `position`. Fields a platform does not report hold their
// identity values (1, 0, 0), so every field is meaningful for every kind.
//
// A gesture is began, changed..., then ended or cancelled, followed for a
// swipe by momentum... and momentum_ended where the platform reports it (as
// WheelPhase). The window keeps every event of a gesture on the control it
// began over (gesture latching, as scroll latching), unless a control holds
// the pointer capture, which takes it. A smart_magnify is a gesture of its
// own and goes to the control under its position.
//
// Dispatch, as the wheel: the target's on_gesture runs first (target
// phase), then each ancestor's (bubble phase), nearest first, until one sets
// handled. A gesture nobody handles is dropped: it is never turned into
// Ctrl+wheel or anything else (a platform that synthesizes Ctrl+wheel from
// a pinch by itself, as Windows does for a window without
// DirectManipulation, delivers wheel events, not gestures).

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types.hpp"

#include <cmath>
#include <cstdint>

namespace gui_forms {

enum class GestureKind : std::uint8_t {
    magnify,
    rotate,
    smart_magnify,
    swipe,
};

// Where an event stands in its gesture: the fingers' contact (began,
// changed..., then ended, or cancelled when the platform or the window
// abandons it), then, for a gesture the platform continues after the
// fingers leave, the system's momentum (momentum..., momentum_ended).
enum class GesturePhase : std::uint8_t {
    began,
    changed,
    ended,
    cancelled,
    momentum,
    momentum_ended,
};

// One gesture event as the host reports it and the window routes it.
// position is the focal point in window logical units; scale,
// rotation_degrees and translation are the change since the previous event
// of the gesture (header comment). phase (EventPhase) and handled are the
// route's, as PointerEvent's. timestamp_nanoseconds is FrameClock
// nanoseconds, as PointerEvent's; zero is stamped at dispatch.
struct GUI_FORMS_API GestureEvent final {
    GestureKind kind{GestureKind::magnify};
    GesturePhase gesture_phase{GesturePhase::changed};
    Point position{};
    double scale{1.0};
    double rotation_degrees{};
    Point translation{};
    Modifier modifiers{Modifier::none};
    EventPhase phase{EventPhase::target};
    bool handled{};
    std::uint64_t timestamp_nanoseconds{};
};

// One gesture event as a control's on_gesture and its gesture subscribers
// see it: the GestureEvent's fields, the focal point in the control's own
// coordinates (location) and the window's (window_position). A raiser that
// sets handled ends the route at that control: no further ancestor sees
// the event (the subscribers still run when the raiser calls its base).
struct GUI_FORMS_API GestureEventArgs final {
    GestureKind kind{GestureKind::magnify};
    GesturePhase gesture_phase{GesturePhase::changed};
    Point location{};
    Point window_position{};
    double scale{1.0};
    double rotation_degrees{};
    Point translation{};
    Modifier modifiers{Modifier::none};
    EventPhase phase{EventPhase::target};
    bool handled{};
    std::uint64_t timestamp_nanoseconds{};
};

// The GestureEventArgs `event` carries on a control whose absolute bounds
// start at `control_origin` (window coordinates). The caller copies handled
// back.
[[nodiscard]] inline GestureEventArgs gesture_event_arguments(const GestureEvent& event,
                                                              Point control_origin) noexcept {
    GestureEventArgs arguments{};
    arguments.kind = event.kind;
    arguments.gesture_phase = event.gesture_phase;
    arguments.location = Point{event.position.x - control_origin.x, event.position.y - control_origin.y};
    arguments.window_position = event.position;
    arguments.scale = event.scale;
    arguments.rotation_degrees = event.rotation_degrees;
    arguments.translation = event.translation;
    arguments.modifiers = event.modifiers;
    arguments.phase = event.phase;
    arguments.handled = event.handled;
    arguments.timestamp_nanoseconds = event.timestamp_nanoseconds;
    return arguments;
}

// Whether `event` is one a host may report: a known kind and phase, a
// finite focal point, rotation and translation, and a finite scale above
// zero. The session refuses any other with invalid_payload.
[[nodiscard]] inline bool valid_gesture_event(const GestureEvent& event) noexcept {
    const bool kind = event.kind == GestureKind::magnify || event.kind == GestureKind::rotate ||
                      event.kind == GestureKind::smart_magnify || event.kind == GestureKind::swipe;
    const bool phase = event.gesture_phase == GesturePhase::began ||
                       event.gesture_phase == GesturePhase::changed ||
                       event.gesture_phase == GesturePhase::ended ||
                       event.gesture_phase == GesturePhase::cancelled ||
                       event.gesture_phase == GesturePhase::momentum ||
                       event.gesture_phase == GesturePhase::momentum_ended;
    const bool position = std::isfinite(event.position.x) && std::isfinite(event.position.y);
    const bool scale = std::isfinite(event.scale) && event.scale > 0.0;
    const bool rotation = std::isfinite(event.rotation_degrees);
    const bool translation = std::isfinite(event.translation.x) && std::isfinite(event.translation.y);
    const bool valid = kind && phase && position && scale && rotation && translation;
    return valid;
}

// Whether `phase` certainly ends its gesture: the window releases the
// gesture's latch after it. ended does not, as momentum may follow it; the
// next began replaces the latch.
[[nodiscard]] constexpr bool gesture_phase_final(GesturePhase phase) noexcept {
    const bool final_phase = phase == GesturePhase::cancelled || phase == GesturePhase::momentum_ended;
    return final_phase;
}

} // namespace gui_forms
