// The web host's computed input (web_input_rules.hpp).
#include "web_input_rules.hpp"

#include <array>
#include <cmath>

namespace gui_forms::host::web_detail {
namespace {

struct CodeUsage final {
    std::string_view code{};
    std::uint32_t usage{};
};

// KeyboardEvent.code values and their USB HID usages (HID Usage Tables,
// Keyboard/Keypad page 0x07). Searched in order; the table is small.
constexpr std::array<CodeUsage, 114> code_usages{{
    {"KeyA", 0x04U}, {"KeyB", 0x05U}, {"KeyC", 0x06U}, {"KeyD", 0x07U}, {"KeyE", 0x08U},
    {"KeyF", 0x09U}, {"KeyG", 0x0AU}, {"KeyH", 0x0BU}, {"KeyI", 0x0CU}, {"KeyJ", 0x0DU},
    {"KeyK", 0x0EU}, {"KeyL", 0x0FU}, {"KeyM", 0x10U}, {"KeyN", 0x11U}, {"KeyO", 0x12U},
    {"KeyP", 0x13U}, {"KeyQ", 0x14U}, {"KeyR", 0x15U}, {"KeyS", 0x16U}, {"KeyT", 0x17U},
    {"KeyU", 0x18U}, {"KeyV", 0x19U}, {"KeyW", 0x1AU}, {"KeyX", 0x1BU}, {"KeyY", 0x1CU},
    {"KeyZ", 0x1DU},
    {"Digit1", 0x1EU}, {"Digit2", 0x1FU}, {"Digit3", 0x20U}, {"Digit4", 0x21U}, {"Digit5", 0x22U},
    {"Digit6", 0x23U}, {"Digit7", 0x24U}, {"Digit8", 0x25U}, {"Digit9", 0x26U}, {"Digit0", 0x27U},
    {"Enter", 0x28U}, {"Escape", 0x29U}, {"Backspace", 0x2AU}, {"Tab", 0x2BU}, {"Space", 0x2CU},
    {"Minus", 0x2DU}, {"Equal", 0x2EU}, {"BracketLeft", 0x2FU}, {"BracketRight", 0x30U},
    {"Backslash", 0x31U}, {"IntlHash", 0x32U}, {"Semicolon", 0x33U}, {"Quote", 0x34U},
    {"Backquote", 0x35U}, {"Comma", 0x36U}, {"Period", 0x37U}, {"Slash", 0x38U},
    {"CapsLock", 0x39U},
    {"F1", 0x3AU}, {"F2", 0x3BU}, {"F3", 0x3CU}, {"F4", 0x3DU}, {"F5", 0x3EU}, {"F6", 0x3FU},
    {"F7", 0x40U}, {"F8", 0x41U}, {"F9", 0x42U}, {"F10", 0x43U}, {"F11", 0x44U}, {"F12", 0x45U},
    {"PrintScreen", 0x46U}, {"ScrollLock", 0x47U}, {"Pause", 0x48U}, {"Insert", 0x49U},
    {"Home", 0x4AU}, {"PageUp", 0x4BU}, {"Delete", 0x4CU}, {"End", 0x4DU}, {"PageDown", 0x4EU},
    {"ArrowRight", 0x4FU}, {"ArrowLeft", 0x50U}, {"ArrowDown", 0x51U}, {"ArrowUp", 0x52U},
    {"NumLock", 0x53U}, {"NumpadDivide", 0x54U}, {"NumpadMultiply", 0x55U},
    {"NumpadSubtract", 0x56U}, {"NumpadAdd", 0x57U}, {"NumpadEnter", 0x58U},
    {"Numpad1", 0x59U}, {"Numpad2", 0x5AU}, {"Numpad3", 0x5BU}, {"Numpad4", 0x5CU},
    {"Numpad5", 0x5DU}, {"Numpad6", 0x5EU}, {"Numpad7", 0x5FU}, {"Numpad8", 0x60U},
    {"Numpad9", 0x61U}, {"Numpad0", 0x62U}, {"NumpadDecimal", 0x63U},
    {"IntlBackslash", 0x64U}, {"ContextMenu", 0x65U}, {"NumpadEqual", 0x67U},
    {"F13", 0x68U}, {"F14", 0x69U}, {"F15", 0x6AU},
    {"IntlRo", 0x87U}, {"IntlYen", 0x89U},
    {"ControlLeft", 0xE0U}, {"ShiftLeft", 0xE1U}, {"AltLeft", 0xE2U}, {"MetaLeft", 0xE3U},
    {"ControlRight", 0xE4U}, {"ShiftRight", 0xE5U}, {"AltRight", 0xE6U}, {"MetaRight", 0xE7U},
    // Older engines' names for the meta keys.
    {"OSLeft", 0xE3U}, {"OSRight", 0xE7U},
}};

double finite_or_zero(double value) noexcept {
    const double result = std::isfinite(value) ? value : 0.0;
    return result;
}

} // namespace

Modifier web_modifiers(std::uint32_t bits) noexcept {
    const std::uint32_t known = bits & 0x0FU;
    const Modifier modifiers = static_cast<Modifier>(static_cast<std::uint8_t>(known));
    return modifiers;
}

std::uint32_t web_physical_key(std::string_view code) noexcept {
    for (std::size_t index = 0U; index < code_usages.size(); ++index) {
        if (code_usages[index].code == code) return code_usages[index].usage;
    }
    return 0U;
}

PointerButton web_pointer_button(std::int32_t button) noexcept {
    switch (button) {
    case 0:
        return PointerButton::primary;
    case 1:
        return PointerButton::middle;
    case 2:
        return PointerButton::secondary;
    default:
        return PointerButton::none;
    }
}

std::uint32_t next_web_click_count(WebClickHistory& history, PointerButton button, double time,
                                   Point position, double interval, double distance) noexcept {
    const double elapsed = time - history.time;
    const bool same_button = history.count != 0U && history.button == button;
    const bool soon = std::isfinite(elapsed) && elapsed >= 0.0 && elapsed <= interval;
    const bool near = std::abs(position.x - history.position.x) <= distance &&
                      std::abs(position.y - history.position.y) <= distance;
    std::uint32_t count = 1U;
    if (same_button && soon && near && history.count < 3U) count = history.count + 1U;
    history.count = count;
    history.button = button;
    history.time = time;
    history.position = position;
    return count;
}

WebWheel web_wheel(double delta_x, double delta_y, WebWheelMode mode, double page_height) noexcept {
    WebWheel wheel{};
    const double x = -finite_or_zero(delta_x);
    const double y = -finite_or_zero(delta_y);
    switch (mode) {
    case WebWheelMode::lines:
        wheel.unit = WheelDeltaUnit::lines;
        wheel.delta = Point{x, y};
        break;
    case WebWheelMode::pages: {
        const double page = std::isfinite(page_height) && page_height > 0.0 ? page_height : 0.0;
        wheel.unit = WheelDeltaUnit::pixels;
        wheel.delta = Point{x * page, y * page};
        break;
    }
    case WebWheelMode::pixels:
    default:
        wheel.unit = WheelDeltaUnit::pixels;
        wheel.delta = Point{x, y};
        break;
    }
    return wheel;
}

bool web_requests_keyboard_context(std::uint32_t physical_key, Modifier modifiers) noexcept {
    if (physical_key == PhysicalKey::context_menu) return modifiers == Modifier::none;
    const bool shift_f10 = physical_key == PhysicalKey::f10 && modifiers == Modifier::shift;
    return shift_f10;
}

} // namespace gui_forms::host::web_detail
