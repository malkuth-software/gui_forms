#pragma once
// One window's input state (APP_WINDOW_RULES_DESIGN C.2): what is held and
// where the pointer is, copied for tests and diagnostics by
// SurfaceRoot::interaction_snapshot.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"

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
struct GUI_FORMS_API HeldKey final {
    std::uint32_t physical_key{};
    FrameTime pressed_at{};

    friend bool operator==(const HeldKey& left, const HeldKey& right) noexcept {
        return left.physical_key == right.physical_key && left.pressed_at == right.pressed_at;
    }
};

// The state at the moment of the copy. The four controls are read from the
// window's own registries (focus, capture, press and hover), not kept a
// second time; a zero RuntimeId means none. The pointer fields hold the
// last pointer event the window dispatched; `pointer_inside` turns false on
// the pointer's leave, and `last_click_count` is the click count the host
// reported for the last press. A window that has dispatched no input
// reports empty input fields.
struct GUI_FORMS_API InteractionSnapshot final {
    std::vector<HeldKey> held_keys{};  // in press order
    std::uint32_t untracked_key_presses{};
    Modifier modifiers{Modifier::none};
    Point pointer_position{};
    bool pointer_inside{};
    bool primary_down{};
    bool secondary_down{};
    bool middle_down{};
    std::uint32_t last_click_count{};
    RuntimeId focused{};
    RuntimeId captured{};
    RuntimeId pressed{};
    RuntimeId hovered{};
    // Whether MouseHover was raised on the hovered control since the pointer
    // entered it, and whether the window's hover timer is armed.
    bool hover_raised{};
    bool hover_pending{};
};

// Where the window's input state reads the time of an input event (a held
// key's press time, the start of a hover). Steady-clock time unless a
// source is installed: tests install one so that times are theirs. Its
// times must be in the same domain as the times the window is polled with
// (SurfaceRoot::poll_frame_schedule), which wakes a pending hover.
class GUI_FORMS_API InputTimeSource {
public:
    virtual ~InputTimeSource() = default;
    [[nodiscard]] virtual FrameTime now() const noexcept = 0;
};

} // namespace gui_forms
