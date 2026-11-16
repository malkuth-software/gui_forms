#pragma once

#include "gui_forms/events/input_events/input_events.hpp"

#include <windows.h>

namespace gui_forms::host::detail {

// Preserve the current virtual-key-derived vocabulary. VK_OEM_2 is slash and
// VK_OEM_MINUS is hyphen on the tested US layout; this is not a
// layout-independent character shortcut translator or a scan-code-based
// physical-position normalization.
[[nodiscard]] inline std::uint32_t physical_key_from_virtual_key(WPARAM key) noexcept {
    if (key >= 'A' && key <= 'Z') {
        const WPARAM relative = key - 'A';
        const std::uint32_t index = static_cast<std::uint32_t>(relative);
        const std::uint32_t usage = 0x04U + index;
        return usage;
    }
    if (key >= '1' && key <= '9') {
        const WPARAM relative = key - '1';
        const std::uint32_t index = static_cast<std::uint32_t>(relative);
        const std::uint32_t usage = 0x1EU + index;
        return usage;
    }
    if (key == '0') { return 0x27U; }
    switch (key) {
    case VK_RETURN: return PhysicalKey::enter;
    case VK_ESCAPE: return PhysicalKey::escape;
    case VK_BACK: return PhysicalKey::backspace;
    case VK_TAB: return PhysicalKey::tab;
    case VK_SPACE: return PhysicalKey::space;
    case VK_OEM_2: return PhysicalKey::slash;
    case VK_INSERT: return PhysicalKey::insert;
    case VK_HOME: return PhysicalKey::home;
    case VK_PRIOR: return PhysicalKey::page_up;
    case VK_END: return PhysicalKey::end;
    case VK_NEXT: return PhysicalKey::page_down;
    case VK_DELETE: return PhysicalKey::delete_forward;
    // The docking keys (owner decision DK-30): F6 regions, Ctrl+F6, Alt+F7,
    // Shift+F10, Ctrl+Shift+F12, Alt+Hyphen, the context-menu key, and the
    // Ctrl release that commits the Ctrl+Tab navigator.
    case VK_OEM_MINUS: return PhysicalKey::minus;
    case VK_F1: return PhysicalKey::f1;
    case VK_F2: return PhysicalKey::f2;
    case VK_F3: return PhysicalKey::f3;
    case VK_F4: return PhysicalKey::f4;
    case VK_F5: return PhysicalKey::f5;
    case VK_F6: return PhysicalKey::f6;
    case VK_F7: return PhysicalKey::f7;
    case VK_F8: return PhysicalKey::f8;
    case VK_F9: return PhysicalKey::f9;
    case VK_F10: return PhysicalKey::f10;
    case VK_F11: return PhysicalKey::f11;
    case VK_F12: return PhysicalKey::f12;
    case VK_APPS: return PhysicalKey::context_menu;
    // WM_KEYDOWN reports the generic VK_CONTROL without its side.
    case VK_CONTROL: return PhysicalKey::left_control;
    case VK_LCONTROL: return PhysicalKey::left_control;
    case VK_RCONTROL: return PhysicalKey::right_control;
    case VK_RIGHT: return PhysicalKey::right;
    case VK_LEFT: return PhysicalKey::left;
    case VK_DOWN: return PhysicalKey::down;
    case VK_UP: return PhysicalKey::up;
    // The rest of a full keyboard (key input for games and Windows Forms
    // KeyDown, 0.5): modifiers with their sides, punctuation on the US
    // layout, the keypad, the lock keys and F13 to F24.
    case VK_SHIFT: return PhysicalKey::left_shift;
    case VK_LSHIFT: return PhysicalKey::left_shift;
    case VK_RSHIFT: return PhysicalKey::right_shift;
    case VK_MENU: return PhysicalKey::left_alt;
    case VK_LMENU: return PhysicalKey::left_alt;
    case VK_RMENU: return PhysicalKey::right_alt;
    case VK_LWIN: return PhysicalKey::left_meta;
    case VK_RWIN: return PhysicalKey::right_meta;
    case VK_CAPITAL: return 0x39U;
    case VK_SNAPSHOT: return 0x46U;
    case VK_SCROLL: return 0x47U;
    case VK_PAUSE: return 0x48U;
    case VK_NUMLOCK: return 0x53U;
    case VK_OEM_PLUS: return PhysicalKey::equal;
    case VK_OEM_4: return 0x2FU;
    case VK_OEM_6: return 0x30U;
    case VK_OEM_5: return 0x31U;
    case VK_OEM_1: return 0x33U;
    case VK_OEM_7: return 0x34U;
    case VK_OEM_3: return 0x35U;
    case VK_OEM_COMMA: return 0x36U;
    case VK_OEM_PERIOD: return 0x37U;
    case VK_OEM_102: return 0x64U;
    case VK_DIVIDE: return 0x54U;
    case VK_MULTIPLY: return 0x55U;
    case VK_SUBTRACT: return PhysicalKey::keypad_minus;
    case VK_ADD: return PhysicalKey::keypad_plus;
    case VK_DECIMAL: return 0x63U;
    case VK_NUMPAD0: return 0x62U;
    default: break;
    }
    if (key >= VK_NUMPAD1 && key <= VK_NUMPAD9) {
        const WPARAM relative = key - VK_NUMPAD1;
        const std::uint32_t usage = 0x59U + static_cast<std::uint32_t>(relative);
        return usage;
    }
    if (key >= VK_F13 && key <= VK_F24) {
        const WPARAM relative = key - VK_F13;
        const std::uint32_t usage = 0x68U + static_cast<std::uint32_t>(relative);
        return usage;
    }
    return 0;
}

// The key of one WM_KEYDOWN/WM_KEYUP (or WM_SYS*) message: the virtual key
// as above, with the side of a generic modifier and the keypad Enter told
// from the message's scan code and extended-key bit (bit 24 of `lparam`),
// which the side-less VK_SHIFT, VK_CONTROL, VK_MENU and VK_RETURN do not
// carry. The right Shift's scan code is 0x36 on every PC keyboard.
[[nodiscard]] inline std::uint32_t physical_key_from_key_message(WPARAM key, LPARAM lparam) noexcept {
    const bool extended = (lparam & (1LL << 24)) != 0;
    const LPARAM scan = (lparam >> 16) & 0xFF;
    switch (key) {
    case VK_SHIFT: return scan == 0x36 ? PhysicalKey::right_shift : PhysicalKey::left_shift;
    case VK_CONTROL: return extended ? PhysicalKey::right_control : PhysicalKey::left_control;
    case VK_MENU: return extended ? PhysicalKey::right_alt : PhysicalKey::left_alt;
    case VK_RETURN: return extended ? 0x58U : PhysicalKey::enter;
    default: break;
    }
    const std::uint32_t usage = physical_key_from_virtual_key(key);
    return usage;
}

// The message's scan code, 0xE000 added for an extended key
// (KeyEvent::scan_code).
[[nodiscard]] inline std::uint32_t scan_code_from_key_message(LPARAM lparam) noexcept {
    const std::uint32_t scan = static_cast<std::uint32_t>((lparam >> 16) & 0xFF);
    const bool extended = (lparam & (1LL << 24)) != 0;
    const std::uint32_t code = extended ? (scan | 0xE000U) : scan;
    return code;
}

} // namespace gui_forms::host::detail
