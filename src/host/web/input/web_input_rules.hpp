#pragma once
// What the web host computes from DOM input: the physical key of a
// KeyboardEvent.code, the modifiers, the click count of each press (a
// browser's pointerdown carries none that every engine fills), the wheel
// delta and its unit, and which keys ask for a context menu. Plain values in,
// no Emscripten, so the rules are tested on every platform
// (tests/web_desk_tests.cpp).
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>
#include <string_view>

namespace gui_forms::host::web_detail {

// The modifier bits web_host.js sends: shift 1, control 2, alt 4, meta 8
// (KeyboardEvent and PointerEvent shiftKey, ctrlKey, altKey, metaKey). They
// are gui_forms::Modifier's bits; others are dropped.
[[nodiscard]] Modifier web_modifiers(std::uint32_t bits) noexcept;

// The USB HID usage of a KeyboardEvent.code value (UI Events KeyboardEvent
// code values), or 0 for a code the host does not map (the key's text still
// arrives through the input element).
[[nodiscard]] std::uint32_t web_physical_key(std::string_view code) noexcept;

// A DOM mouse button (MouseEvent.button: 0 main, 1 auxiliary, 2 secondary)
// as a pointer button; none for the back and forward buttons and others.
[[nodiscard]] PointerButton web_pointer_button(std::int32_t button) noexcept;

// The double-click distance where the page states none: Windows' 4 pixel
// square, reaching 2 pixels each way from the first press.
inline constexpr double default_web_double_click_distance = 2.0;
// The double-click interval: browsers do not expose the system's, so the
// host uses Windows' default.
inline constexpr double default_web_double_click_milliseconds = 500.0;

// The last press the click count compares the next one against.
struct WebClickHistory final {
    std::uint32_t count{};
    PointerButton button{PointerButton::none};
    // The press's DOM timestamp (milliseconds, performance.now()'s clock).
    double time{};
    Point position{};
};

// The click count of a press of `button` at `time` and canvas position
// `position`, updated into `history`: one more than the last press's (up to
// 3, then 1 again) when it is the same button, within `interval` of the
// last press, and within `distance` of it along each axis; otherwise 1.
[[nodiscard]] std::uint32_t next_web_click_count(WebClickHistory& history, PointerButton button, double time,
                                                 Point position, double interval, double distance) noexcept;

// A WheelEvent's deltaMode.
enum class WebWheelMode : std::uint8_t {
    pixels = 0,
    lines = 1,
    pages = 2,
};

struct WebWheel final {
    Point delta{};
    WheelDeltaUnit unit{WheelDeltaUnit::pixels};
};

// A WheelEvent as GUI.Forms' wheel delta: the DOM's deltas point down and
// right for content moving up and left, GUI.Forms' up and left, so both are
// negated. Pixels stay pixels, lines stay lines, and a page is
// `page_height` pixels. A non-finite delta is 0.
[[nodiscard]] WebWheel web_wheel(double delta_x, double delta_y, WebWheelMode mode, double page_height) noexcept;

// Whether a key press asks for a context menu, as Windows' Shift+F10 and
// the Menu key do (the X11 host's rule): the context-menu key with no
// modifier, or F10 with Shift alone. Asked only after the window left the
// key unhandled.
[[nodiscard]] bool web_requests_keyboard_context(std::uint32_t physical_key, Modifier modifiers) noexcept;

} // namespace gui_forms::host::web_detail
