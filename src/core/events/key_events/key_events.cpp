#include "gui_forms/events/key_events/key_events.hpp"

#include <array>
#include <cstddef>

namespace gui_forms {

namespace {

// One run of consecutive HID usages whose Keys codes are consecutive too.
struct KeyCodeRun final {
    std::uint32_t first_usage;
    std::uint32_t last_usage;
    std::uint32_t first_code;
};

// The runs, in usage order (HID Usage Tables, Keyboard/Keypad page 0x07;
// Keys values from System.Windows.Forms.Keys).
constexpr std::array<KeyCodeRun, 36> key_code_runs{{
    {0x04U, 0x1DU, 0x41U},  // A to Z
    {0x1EU, 0x26U, 0x31U},  // 1 to 9
    {0x27U, 0x27U, 0x30U},  // 0
    {0x28U, 0x28U, 0x0DU},  // Enter
    {0x29U, 0x29U, 0x1BU},  // Escape
    {0x2AU, 0x2AU, 0x08U},  // Backspace
    {0x2BU, 0x2BU, 0x09U},  // Tab
    {0x2CU, 0x2CU, 0x20U},  // Space
    {0x2DU, 0x2DU, 0xBDU},  // - (OemMinus)
    {0x2EU, 0x2EU, 0xBBU},  // = (Oemplus)
    {0x2FU, 0x2FU, 0xDBU},  // [ (OemOpenBrackets)
    {0x30U, 0x30U, 0xDDU},  // ] (OemCloseBrackets)
    {0x31U, 0x32U, 0xDCU},  // backslash and the non-US hash (OemPipe)
    {0x33U, 0x33U, 0xBAU},  // ; (OemSemicolon)
    {0x34U, 0x34U, 0xDEU},  // ' (OemQuotes)
    {0x35U, 0x35U, 0xC0U},  // ` (Oemtilde)
    {0x36U, 0x36U, 0xBCU},  // , (Oemcomma)
    {0x37U, 0x37U, 0xBEU},  // . (OemPeriod)
    {0x38U, 0x38U, 0xBFU},  // / (OemQuestion)
    {0x39U, 0x39U, 0x14U},  // Caps Lock
    {0x3AU, 0x45U, 0x70U},  // F1 to F12
    {0x46U, 0x46U, 0x2CU},  // Print Screen
    {0x47U, 0x47U, 0x91U},  // Scroll Lock
    {0x48U, 0x48U, 0x13U},  // Pause
    {0x49U, 0x49U, 0x2DU},  // Insert
    {0x4AU, 0x4AU, 0x24U},  // Home
    {0x4BU, 0x4BU, 0x21U},  // Page Up
    {0x4CU, 0x4CU, 0x2EU},  // Delete
    {0x4DU, 0x4DU, 0x23U},  // End
    {0x4EU, 0x4EU, 0x22U},  // Page Down
    {0x4FU, 0x4FU, 0x27U},  // Right
    {0x50U, 0x50U, 0x25U},  // Left
    {0x51U, 0x51U, 0x28U},  // Down
    {0x52U, 0x52U, 0x26U},  // Up
    {0x53U, 0x53U, 0x90U},  // Num Lock
    {0x59U, 0x61U, 0x61U},  // keypad 1 to 9
}};

// Single usages outside the runs.
struct KeyCodePair final {
    std::uint32_t usage;
    std::uint32_t code;
};

constexpr std::array<KeyCodePair, 18> key_code_pairs{{
    {0x54U, 0x6FU},  // keypad / (Divide)
    {0x55U, 0x6AU},  // keypad * (Multiply)
    {0x56U, 0x6DU},  // keypad - (Subtract)
    {0x57U, 0x6BU},  // keypad + (Add)
    {0x58U, 0x0DU},  // keypad Enter
    {0x62U, 0x60U},  // keypad 0
    {0x63U, 0x6EU},  // keypad . (Decimal)
    {0x64U, 0xE2U},  // the non-US backslash (OemBackslash)
    {0x65U, 0x5DU},  // the context-menu key (Apps)
    {0x67U, 0xBBU},  // keypad = (as Oemplus)
    {0xE0U, 0x11U},  // left Control (ControlKey)
    {0xE1U, 0x10U},  // left Shift (ShiftKey)
    {0xE2U, 0x12U},  // left Alt (Menu)
    {0xE3U, 0x5BU},  // left Windows / Command (LWin)
    {0xE4U, 0x11U},  // right Control
    {0xE5U, 0x10U},  // right Shift
    {0xE6U, 0x12U},  // right Alt
    {0xE7U, 0x5CU},  // right Windows / Command (RWin)
}};

} // namespace

std::uint32_t key_code_from_physical_key(std::uint32_t physical_key) noexcept {
    // F13 to F24 (usages 0x68 to 0x73) follow F12 in Keys (0x7C to 0x87).
    if (physical_key >= 0x68U && physical_key <= 0x73U) {
        const std::uint32_t code = 0x7CU + (physical_key - 0x68U);
        return code;
    }
    for (const KeyCodeRun& run : key_code_runs) {
        if (physical_key >= run.first_usage && physical_key <= run.last_usage) {
            const std::uint32_t code = run.first_code + (physical_key - run.first_usage);
            return code;
        }
    }
    for (const KeyCodePair& pair : key_code_pairs) {
        if (pair.usage == physical_key) return pair.code;
    }
    return 0U;
}

std::uint32_t single_key_char(const std::string& text) noexcept {
    if (text.empty() || text.size() > 4U) return 0U;
    const unsigned char lead = static_cast<unsigned char>(text[0]);
    std::size_t length = 0U;
    std::uint32_t scalar = 0U;
    if (lead < 0x80U) {
        length = 1U;
        scalar = lead;
    } else if ((lead & 0xE0U) == 0xC0U) {
        length = 2U;
        scalar = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3U;
        scalar = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4U;
        scalar = lead & 0x07U;
    } else {
        return 0U;
    }
    if (text.size() != length) return 0U;
    for (std::size_t index = 1U; index < length; ++index) {
        const unsigned char continuation = static_cast<unsigned char>(text[index]);
        if ((continuation & 0xC0U) != 0x80U) return 0U;
        scalar = (scalar << 6U) | (continuation & 0x3FU);
    }
    // Overlong forms, surrogates and values past Unicode are not scalars.
    const std::array<std::uint32_t, 5> least{{0U, 0U, 0x80U, 0x800U, 0x10000U}};
    if (scalar < least[length] || scalar > 0x10FFFFU || (scalar >= 0xD800U && scalar <= 0xDFFFU)) return 0U;
    return scalar;
}

} // namespace gui_forms
