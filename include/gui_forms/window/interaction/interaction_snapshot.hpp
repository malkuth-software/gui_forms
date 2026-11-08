#pragma once
// A copy of one window's interaction state (APP_WINDOW_RULES_DESIGN C.2),
// for tests and diagnostics: SurfaceRoot::interaction_snapshot.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/window/interaction/interaction_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gui_forms {

// The most physical keys a window tracks as held at once. A key pressed
// while this many are held is not tracked (InteractionSnapshot::
// untracked_key_presses counts them); its release is ignored.
inline constexpr std::size_t maximum_held_keys = 16U;

// One held physical key (a USB HID usage, PhysicalKey) and when it went
// down, in the window's input time (InputTimeSource).
struct HeldKey final {
    std::uint32_t physical_key{};
    FrameTime pressed_at{};

    friend bool operator==(const HeldKey& left, const HeldKey& right) noexcept {
        return left.physical_key == right.physical_key && left.pressed_at == right.pressed_at;
    }
};

// Where the gesture in progress stands.
// - none: no gesture runs.
// - pressed: a button is down and nothing else has happened.
// - held: the primary press raised hold.
// - dragging: the primary press raised drag_start.
enum class GesturePhase : std::uint8_t {
    none,
    pressed,
    held,
    dragging,
};

// The state at the moment of the copy. The four controls are read from the
// window's own registries (focus, capture, press and hover), not kept a
// second time; a zero RuntimeId means none. The pointer fields hold the
// last pointer event the window dispatched; `pointer_inside` turns false on
// the pointer's leave. A window that has dispatched no input reports
// empty input fields.
struct InteractionSnapshot final {
    std::vector<HeldKey> held_keys{};  // in press order
    std::uint32_t untracked_key_presses{};
    Modifier modifiers{Modifier::none};
    Point pointer_position{};
    bool pointer_inside{};
    bool primary_down{};
    bool secondary_down{};
    bool middle_down{};
    RuntimeId focused{};
    RuntimeId captured{};
    RuntimeId pressed{};
    RuntimeId hovered{};
    GesturePhase gesture{GesturePhase::none};
    PointerButton gesture_button{PointerButton::none};
    RuntimeId gesture_control{};
    Point gesture_origin{};
    // 2 while the press in progress is the second of a possible
    // double-click, 1 for another primary press, 0 without one.
    std::uint32_t gesture_click_count{};
    // Interactions raised over the window's life, and gestures cancelled by
    // capture or activation loss.
    std::uint64_t interactions_raised{};
    std::uint64_t gestures_cancelled{};
};

} // namespace gui_forms
