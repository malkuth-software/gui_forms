#include "windows_key_translation.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

void test_us_slash_translation() {
    static_assert(PhysicalKey::slash == 0x38U);
    const std::uint32_t usage = host::detail::physical_key_from_virtual_key(VK_OEM_2);
    require(usage == PhysicalKey::slash, "US slash OEM virtual key maps to named HID usage");
    // The other US punctuation and the keypad keys have their own usages.
    struct Mapping final { WPARAM native{}; std::uint32_t expected{}; };
    const std::array<Mapping, 4> others{{
        {VK_OEM_1, 0x33U}, {VK_OEM_3, 0x35U}, {VK_DIVIDE, 0x54U}, {VK_ATTN, 0U}}};
    for (std::size_t index = 0; index < others.size(); ++index) {
        const std::uint32_t other = host::detail::physical_key_from_virtual_key(others[index].native);
        require(other == others[index].expected, "punctuation and keypad keys map to their usages");
    }
}

// The full keyboard games and Windows Forms KeyDown read (0.5): modifiers
// with their sides, the keypad, the lock keys and F13 to F24; the side of
// a generic modifier and the keypad Enter come from the message's scan
// code and extended bit.
void test_full_keyboard() {
    struct Mapping final { WPARAM native{}; std::uint32_t expected{}; };
    const std::array<Mapping, 16> keys{{
        {VK_LSHIFT, PhysicalKey::left_shift}, {VK_RSHIFT, PhysicalKey::right_shift},
        {VK_LMENU, PhysicalKey::left_alt}, {VK_RMENU, PhysicalKey::right_alt},
        {VK_LWIN, PhysicalKey::left_meta}, {VK_RWIN, PhysicalKey::right_meta},
        {VK_CAPITAL, 0x39U}, {VK_NUMLOCK, 0x53U}, {VK_NUMPAD0, 0x62U}, {VK_NUMPAD1, 0x59U},
        {VK_NUMPAD9, 0x61U}, {VK_ADD, PhysicalKey::keypad_plus}, {VK_DECIMAL, 0x63U},
        {VK_F13, 0x68U}, {VK_F24, 0x73U}, {VK_OEM_PLUS, PhysicalKey::equal}}};
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const std::uint32_t usage = host::detail::physical_key_from_virtual_key(keys[index].native);
        require(usage == keys[index].expected, "full keyboard mapping");
    }
    const LPARAM left_shift = static_cast<LPARAM>(0x2AU) << 16;
    const LPARAM right_shift = static_cast<LPARAM>(0x36U) << 16;
    const LPARAM extended = static_cast<LPARAM>(1) << 24;
    const LPARAM control_scan = static_cast<LPARAM>(0x1DU) << 16;
    const LPARAM alt_scan = static_cast<LPARAM>(0x38U) << 16;
    const LPARAM enter_scan = static_cast<LPARAM>(0x1CU) << 16;
    require(host::detail::physical_key_from_key_message(VK_SHIFT, left_shift) == PhysicalKey::left_shift &&
                host::detail::physical_key_from_key_message(VK_SHIFT, right_shift) == PhysicalKey::right_shift,
            "VK_SHIFT takes its side from the scan code");
    require(host::detail::physical_key_from_key_message(VK_CONTROL, control_scan) == PhysicalKey::left_control &&
                host::detail::physical_key_from_key_message(VK_CONTROL, control_scan | extended) ==
                    PhysicalKey::right_control,
            "VK_CONTROL takes its side from the extended bit");
    require(host::detail::physical_key_from_key_message(VK_MENU, alt_scan) == PhysicalKey::left_alt &&
                host::detail::physical_key_from_key_message(VK_MENU, alt_scan | extended) == PhysicalKey::right_alt,
            "VK_MENU takes its side from the extended bit");
    require(host::detail::physical_key_from_key_message(VK_RETURN, enter_scan) == PhysicalKey::enter &&
                host::detail::physical_key_from_key_message(VK_RETURN, enter_scan | extended) == 0x58U,
            "the keypad Enter is the extended Return");
    require(host::detail::physical_key_from_key_message('W', static_cast<LPARAM>(0x11U) << 16) == PhysicalKey::w,
            "other keys map as their virtual key");
    require(host::detail::scan_code_from_key_message(right_shift) == 0x36U &&
                host::detail::scan_code_from_key_message(control_scan | extended) == 0xE01DU,
            "the scan code carries 0xE000 for an extended key");
}

void test_existing_translation_preserved() {
    for (WPARAM native = 'A'; native <= 'Z'; ++native) {
        const std::uint32_t usage = host::detail::physical_key_from_virtual_key(native);
        const WPARAM relative = native - 'A';
        const std::uint32_t index = static_cast<std::uint32_t>(relative);
        const std::uint32_t expected = index + 0x04U;
        require(usage == expected, "letter mapping preserved");
    }
    for (WPARAM native = '1'; native <= '9'; ++native) {
        const std::uint32_t usage = host::detail::physical_key_from_virtual_key(native);
        const WPARAM relative = native - '1';
        const std::uint32_t index = static_cast<std::uint32_t>(relative);
        const std::uint32_t expected = index + 0x1EU;
        require(usage == expected, "digit mapping preserved");
    }
    struct Mapping final { WPARAM native{}; std::uint32_t expected{}; };
    const std::array<Mapping, 19> existing{{
        {'0', 0x27U}, {VK_RETURN, PhysicalKey::enter}, {VK_ESCAPE, PhysicalKey::escape},
        {VK_BACK, PhysicalKey::backspace}, {VK_TAB, PhysicalKey::tab}, {VK_SPACE, PhysicalKey::space},
        {VK_HOME, PhysicalKey::home}, {VK_PRIOR, PhysicalKey::page_up}, {VK_END, PhysicalKey::end},
        {VK_NEXT, PhysicalKey::page_down}, {VK_DELETE, PhysicalKey::delete_forward},
        {VK_F1, PhysicalKey::f1}, {VK_F2, PhysicalKey::f2}, {VK_F4, PhysicalKey::f4},
        {VK_RIGHT, PhysicalKey::right}, {VK_LEFT, PhysicalKey::left},
        {VK_DOWN, PhysicalKey::down}, {VK_UP, PhysicalKey::up}, {0xffffU, 0}}};
    for (std::size_t index = 0; index < existing.size(); ++index) {
        const Mapping& mapping = existing[index];
        const std::uint32_t usage = host::detail::physical_key_from_virtual_key(mapping.native);
        require(usage == mapping.expected, "existing special key mapping preserved");
    }
}

// The keys docking binds (owner decision DK-30).
void test_docking_keys() {
    struct Mapping final { WPARAM native{}; std::uint32_t expected{}; };
    const std::array<Mapping, 15> docking{{
        {VK_OEM_MINUS, PhysicalKey::minus}, {VK_F3, PhysicalKey::f3}, {VK_F5, PhysicalKey::f5},
        {VK_F6, PhysicalKey::f6}, {VK_F7, PhysicalKey::f7}, {VK_F8, PhysicalKey::f8},
        {VK_F9, PhysicalKey::f9}, {VK_F10, PhysicalKey::f10}, {VK_F11, PhysicalKey::f11},
        {VK_F12, PhysicalKey::f12}, {VK_APPS, PhysicalKey::context_menu},
        {VK_CONTROL, PhysicalKey::left_control}, {VK_LCONTROL, PhysicalKey::left_control},
        {VK_RCONTROL, PhysicalKey::right_control}, {VK_SUBTRACT, PhysicalKey::keypad_minus}}};
    for (std::size_t index = 0; index < docking.size(); ++index) {
        const Mapping& mapping = docking[index];
        const std::uint32_t usage = host::detail::physical_key_from_virtual_key(mapping.native);
        require(usage == mapping.expected, "docking key mapping");
    }
    static_assert(PhysicalKey::context_menu == 0x65U, "the context-menu key is HID Keyboard Application");
}

} // namespace

int main() {
    try {
        test_us_slash_translation();
        test_existing_translation_preserved();
        test_docking_keys();
        test_full_keyboard();
        std::cout << "Windows virtual-key translation fixtures passed; native layout/input delivery not exercised\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
