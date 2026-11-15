#pragma once
// What the web host computes from DOM input to post the window manager's
// records (surface_input.hpp): the physical key of a KeyboardEvent.code,
// the modifiers, the button, the action, and the wheel's delta and unit.
// Plain values in, no Emscripten, so the rules are tested on every platform
// (tests/web_host_rules_tests.cpp).
#include "../../surface/input/surface_input.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>
#include <optional>
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

// A PointerEvent's action as web_host.js numbers it (0 move, 1 down, 2 up,
// 3 enter, 4 leave, 5 cancel, SurfacePointerAction's values); none for any
// other number.
[[nodiscard]] std::optional<surface_detail::SurfacePointerAction> web_pointer_action(std::int32_t action) noexcept;

// A WheelEvent's deltas in GUI.Forms' sign: the DOM's point down and right
// for content moving up and left, GUI.Forms' up and left, so both are
// negated. A non-finite delta is 0.
[[nodiscard]] Point web_wheel_delta(double delta_x, double delta_y) noexcept;

// A WheelEvent's deltaMode (0 pixels, 1 lines, 2 pages); pixels for any
// other number.
[[nodiscard]] surface_detail::SurfaceWheelUnit web_wheel_unit(std::int32_t mode) noexcept;

// A dialog answer's outcome as web_host.js numbers it (0 cancelled,
// 1 accepted, 2 failed); failed for any other number.
[[nodiscard]] surface_detail::SurfaceDialogOutcome web_dialog_outcome(std::int32_t outcome) noexcept;

} // namespace gui_forms::host::web_detail
