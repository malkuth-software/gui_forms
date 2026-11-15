#pragma once
// Relative pointer input (cp34; owner, 2026-11-15: "whatever a game engine
// would actually want"). A control asks its window for the pointer's
// motion as deltas rather than positions, as SDL3's relative mouse mode,
// GLFW's raw motion and disabled cursor, Unity's and Unreal's pointer lock,
// Windows Raw Input, Wayland's relative-pointer and pointer-constraints and
// the browser's Pointer Lock (with unadjustedMovement) give it.
//
// The modes (RelativePointerMode), from the least to the most taking:
//   free: the pointer stays the ordinary pointer, visible, moving and
//     clicking as always; the owner also receives every motion sample as a
//     delta (unaccelerated where the host allows and the request asks), for
//     precision without hiding anything.
//   confined: the pointer is kept inside the owner's bounds (visible unless
//     hide_cursor); buttons and the wheel go to the owner as samples, and
//     no click is derived from them.
//   locked: the pointer is hidden and stays where it was; only deltas
//     arrive. The owner's window keeps a virtual position (where the
//     pointer would be, clamped to the window) for the release regions.
// A host that cannot do what is asked does the nearest thing below it and
// says so in the grant (locked -> confined -> free with deltas derived from
// positions); nothing is refused for a missing capability alone.
//
// The shell is always reachable (owner, 2026-11-15: every PlaySuite game
// can always click the capsule). The window, not the game, declares its
// release regions (SurfaceRoot::set_pointer_release_regions): while the
// pointer (or a locked pointer's virtual position) is over one, a confined
// or locked mode is suspended, the ordinary pointer reappears there and
// clicks it as usual, and the mode resumes when the pointer moves back
// over the owner outside every region. The mode also ends, whatever the
// owner does:
//   - on Escape (the key is taken: it reaches no control);
//   - when focus moves to a control outside the owner (not while
//     suspended: a press on the shell's region may take focus);
//   - when the window is deactivated;
//   - when a modal window, a dialog, a menu or any focus scope opens;
//   - when the owner is disposed, detached, hidden or disabled;
//   - with the press it began under, for a request with until_release;
//   - on release_relative_pointer, or another control's request (which
//     ends this one with superseded first).
// Each end raises the owner's relative_pointer_ended once.
//
// Order (manifests/abi/interaction_order.json, sequence relative_pointer):
// every sample is raised as it arrives, one relative_pointer per host
// sample, never merged per frame, in the host's order, each with the
// host's timestamp; then relative_pointer_ended once. In confined and
// locked modes buttons and the wheel are samples of the owner's, and the
// derived events (click, mouse_click, double_click, the activation) do not
// apply; in free mode the routed mouse events go on as usual beside the
// samples.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms {

// The most release regions a window declares (the shell's capsule and a
// few more); more are refused, never truncated.
inline constexpr std::size_t maximum_pointer_release_regions = 8U;

enum class RelativePointerMode : std::uint8_t {
    free,
    confined,
    locked,
};

// What a request asks for. mode: see above (confined unless asked:
// confinement before a hard lock). hide_cursor: hides the pointer in
// confined mode (locked always hides it; free never does). raw_motion:
// unaccelerated deltas where the host has them (the grant says whether it
// does). until_release: accepted only while a button is held on the owner,
// and ended by that button's release, so the mode never outlasts the
// user's own press.
struct GUI_FORMS_API RelativePointerRequest final {
    RelativePointerMode mode{RelativePointerMode::confined};
    bool hide_cursor{};
    bool raw_motion{};
    bool until_release{};
};

enum class RelativePointerResult : std::uint8_t {
    // Granted as asked.
    granted,
    // Granted below what was asked (the grant's mode, raw and cursor say
    // what the owner has).
    degraded,
    // The owner is not in a window, or is hidden, disabled or disposed.
    detached,
    // The window is not active, or a focus scope (a dialog, a menu) that
    // does not hold the owner is open.
    not_active,
    // until_release without a button held on the owner.
    no_press,
    // A mode outside RelativePointerMode.
    invalid_request,
};

// What a request got. mode: the mode in force. raw: deltas are
// unaccelerated. cursor_hidden: the pointer is hidden. host_relative: the
// host delivers deltas itself; false when the window derives them from
// positions (so they stop at the screen's edge, and are accelerated).
struct GUI_FORMS_API RelativePointerGrant final {
    RelativePointerResult result{RelativePointerResult::detached};
    RelativePointerMode mode{RelativePointerMode::free};
    bool raw{};
    bool cursor_hidden{};
    bool host_relative{};

    [[nodiscard]] bool accepted() const noexcept {
        return result == RelativePointerResult::granted || result == RelativePointerResult::degraded;
    }
};

// What the window's host can do (from its HostCapabilities; a window with
// no host derives deltas from the positions it is handed and can do
// nothing else).
struct GUI_FORMS_API RelativePointerSupport final {
    // The host delivers motion deltas (HostRelativeMotionEvent).
    bool relative_motion{};
    // ...unaccelerated where asked.
    bool raw_motion{};
    // It can hide the pointer and hold it still.
    bool lock{};
    // It can keep the pointer inside a rectangle.
    bool confine{};
    // It can move the pointer (warp_pointer).
    bool warp{};
};

// What one sample is.
enum class RelativePointerSampleKind : std::uint8_t {
    motion,
    button_down,
    button_up,
    wheel,
};

// One sample, raised on the owner (Control::relative_pointer). delta: the
// motion in logical units (unaccelerated counts scaled to logical units
// when `unaccelerated`), sub-unit precision kept; zero for a button or
// wheel sample. position: the pointer in the owner's coordinates (a locked
// pointer's virtual position). button: a button sample's button.
// wheel_delta and wheel_unit: a wheel sample's, as PointerEvent carries
// them. timestamp_nanoseconds: the host's time for the sample
// (PointerEvent::timestamp_nanoseconds). handled: set by a handler; it ends
// nothing, and is read by the C ABI's caller.
struct GUI_FORMS_API RelativePointerEventArgs final {
    RelativePointerSampleKind kind{RelativePointerSampleKind::motion};
    Point delta{};
    Point position{};
    PointerButton button{PointerButton::none};
    Point wheel_delta{};
    WheelDeltaUnit wheel_unit{WheelDeltaUnit::lines};
    Modifier modifiers{Modifier::none};
    RelativePointerMode mode{RelativePointerMode::free};
    std::uint64_t timestamp_nanoseconds{};
    bool unaccelerated{};
    bool handled{};
};

// Why a mode ended.
enum class RelativePointerEnd : std::uint8_t {
    released,
    escape,
    focus_lost,
    deactivated,
    modal,
    detached,
    press_released,
    superseded,
};

// The owner's relative_pointer_ended record. reason: the trigger that
// ended the mode (only the first is reported).
struct GUI_FORMS_API RelativePointerEnded final {
    RelativePointerEnd reason{RelativePointerEnd::released};
};

// The window's relative pointer state and counters (AGENTS.md: structured
// instrumentation). samples counts every sample raised; suspensions the
// times a release region suspended the mode.
struct GUI_FORMS_API RelativePointerSnapshot final {
    bool active{};
    bool suspended{};
    RelativePointerMode mode{RelativePointerMode::free};
    bool raw{};
    bool host_relative{};
    std::uint64_t grants{};
    std::uint64_t samples{};
    std::uint64_t suspensions{};
    std::uint64_t resumptions{};
    std::uint64_t ends{};
    RelativePointerEnd last_end{RelativePointerEnd::released};
};

} // namespace gui_forms
