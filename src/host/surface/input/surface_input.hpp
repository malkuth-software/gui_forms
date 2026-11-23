#pragma once
// The single-surface window manager's input (owner decision, 2026-11-15):
// what a host posts, already in GUI.Forms' portable values, and the queue
// that carries it to the manager's thread. A host turns its own events
// (DOM events in a browser, STXlib's input records on the appliance) into
// these records; the manager does the rest (surface_manager_internal.hpp).
// Plain C++, tested on every platform (tests/surface_desk_tests.cpp).
//
// Threads: post and wake are called on any thread (the host's event thread,
// the engine's workers, a model's wake handler); take is called only on the
// manager's thread. Each critical section is a few moves long, so a host's
// thread that may not block for long (a browser's main thread spins on a
// futex) holds the lock only briefly.

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <stxlib/threads.hpp>
#include <vector>

namespace gui_forms::host::surface_detail {

enum class SurfaceInputKind : std::uint8_t {
    // A pointer event at `position` (surface logical units): `action`,
    // `button`, `modifiers`, `time` in milliseconds on the host's clock.
    pointer,
    // A wheel turn at `position`: `wheel_delta` in `wheel_unit`, positive
    // up and left (GUI.Forms' sign).
    wheel,
    // A key: `down`, `repeat`, `physical_key` (a USB HID usage), `scan_code`
    // (the host's own code, or 0), `modifiers`.
    key,
    // Committed text.
    text,
    // A composition's marked text; empty cancels it.
    composition,
    // The host's clipboard text as it last saw it (a paste, a copy).
    clipboard,
    // The surface's logical size (`extent`) and device scale (`scale`).
    resize,
    // The surface gained (`down` true) or lost the keyboard.
    focus,
    // The surface was hidden (`down` true) or shown again.
    visibility,
    // The last present was completed: composing may resume (a host whose
    // present returned SurfacePresentResult::pending).
    presented,
    // The surface is on screen again (`down` true) after another surface
    // held it, and every pixel is composed again (STXlib's
    // surface_activated); `down` false reports the deactivation.
    activation,
    // A display frame while the manager asked for them (live surfaces).
    frame,
    // A host dialog answered: `request` the manager's ticket, `outcome`,
    // `paths` the files written, `text` a chosen value.
    dialog,
    // Files dropped at `position`, already in the program's file system.
    drop,
    // A font group arrived (`text` its name).
    fonts,
    // The host is going away: every window closes.
    close,
};

enum class SurfacePointerAction : std::uint8_t {
    move = 0,
    down = 1,
    up = 2,
    enter = 3,
    leave = 4,
    cancel = 5,
};

enum class SurfaceWheelUnit : std::uint8_t {
    pixels = 0,
    lines = 1,
    // A page is the window's client height in pixels.
    pages = 2,
};

enum class SurfaceDialogOutcome : std::uint8_t {
    cancelled = 0,
    accepted = 1,
    failed = 2,
};

struct SurfaceInputRecord final {
    SurfaceInputKind kind{SurfaceInputKind::frame};
    SurfacePointerAction action{SurfacePointerAction::move};
    PointerButton button{PointerButton::none};
    Modifier modifiers{Modifier::none};
    bool down{};
    bool repeat{};
    std::uint32_t physical_key{};
    std::uint32_t scan_code{};
    Point position{};
    Point wheel_delta{};
    SurfaceWheelUnit wheel_unit{SurfaceWheelUnit::lines};
    Size extent{};
    double scale{1.0};
    double time{};
    std::uint64_t request{};
    SurfaceDialogOutcome outcome{SurfaceDialogOutcome::cancelled};
    std::string text{};
    std::vector<std::string> paths{};
};

class SurfaceInputQueue final {
public:
    // Pointer moves and wheel turns beyond this many are dropped: a host
    // that floods the queue loses motion rather than memory.
    static constexpr std::size_t maximum_records = 4096U;
    // Every other record (presses and releases, keys, presented, dialog
    // answers, close, activation) is admitted up to this larger bound, so a
    // busy manager thread does not lose a record whose loss would leave
    // state stuck (A-gf-host-02-F14); the queue stays bounded.
    static constexpr std::size_t maximum_control_records = 4U * maximum_records;

    // Appends `record` and wakes the manager's thread. Returns false, keeping
    // nothing, when the queue is full for its kind.
    bool post(SurfaceInputRecord record);
    // Wakes the manager's thread without a record (a model's wake handler).
    void wake() noexcept;
    // Moves every record into `taken` (appended, its storage reused) and
    // clears the wake. When there is neither a record nor a wake, first
    // waits until one arrives or `deadline` passes; no deadline waits without
    // limit, and a deadline in the past does not wait. Returns whether a
    // record or a wake was taken.
    bool take(std::deque<SurfaceInputRecord>& taken, std::optional<std::chrono::steady_clock::time_point> deadline);
    // Records dropped because the queue was full.
    [[nodiscard]] std::uint64_t dropped() const;

private:
    // take's wait predicate, under the mutex: a record or a wake.
    [[nodiscard]] static bool record_or_wake(const void* context) noexcept;

    mutable stxlib::Mutex mutex_;
    stxlib::ConditionVariable ready_;
    std::deque<SurfaceInputRecord> records_;
    bool woken_{};
    std::uint64_t dropped_{};
};

// The double-click rule where the host states none: Windows' defaults, a 4
// pixel square (2 each way) and 500 ms.
inline constexpr double default_double_click_distance = 2.0;
inline constexpr double default_double_click_milliseconds = 500.0;

// The last press the click count compares the next one against.
struct SurfaceClickHistory final {
    std::uint32_t count{};
    PointerButton button{PointerButton::none};
    // The press's time in milliseconds on the host's clock.
    double time{};
    Point position{};
};

// The click count of a press of `button` at `time` and position `position`,
// updated into `history`: one more than the last press's (up to 3, then 1
// again) when it is the same button, within `interval` of the last press,
// and within `distance` of it along each axis; otherwise 1.
[[nodiscard]] std::uint32_t next_surface_click_count(SurfaceClickHistory& history, PointerButton button, double time,
                                                     Point position, double interval, double distance) noexcept;

struct SurfaceWheel final {
    Point delta{};
    WheelDeltaUnit unit{WheelDeltaUnit::pixels};
};

// A wheel record as GUI.Forms' wheel delta: pixels and lines stay as they
// are, a page is `page_height` pixels. A non-finite delta is 0.
[[nodiscard]] SurfaceWheel surface_wheel(Point delta, SurfaceWheelUnit unit, double page_height) noexcept;

// Whether a key press asks for a context menu, as Windows' Shift+F10 and
// the Menu key do (the X11 host's rule): the context-menu key with no
// modifier, or F10 with Shift alone. Asked only after the window left the
// key unhandled.
[[nodiscard]] bool surface_requests_keyboard_context(std::uint32_t physical_key, Modifier modifiers) noexcept;

} // namespace gui_forms::host::surface_detail
