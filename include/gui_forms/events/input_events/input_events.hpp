#pragma once

#include "gui_forms/types.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gui_forms {

enum class EventPhase : std::uint8_t {
    preview,
    target,
    bubble,
};

enum class PointerAction : std::uint8_t {
    move,
    down,
    up,
    wheel,
    enter,
    leave,
};

enum class PointerButton : std::uint8_t {
    none,
    primary,
    secondary,
    middle,
};

// What a wheel event's delta counts. A detented wheel reports lines (one
// per notch, or a fraction of one where the host accelerates), which a
// scrolling control turns into its own step; a precise device (a trackpad,
// a smooth-scrolling mouse) reports logical pixels, which it scrolls
// exactly, fractions included.
enum class WheelDeltaUnit : std::uint8_t {
    lines,
    pixels,
};

enum class KeyAction : std::uint8_t {
    down,
    up,
};

enum class Modifier : std::uint8_t {
    none = 0,
    shift = 1U << 0U,
    control = 1U << 1U,
    alt = 1U << 2U,
    meta = 1U << 3U,
};

[[nodiscard]] constexpr Modifier operator|(Modifier left, Modifier right) noexcept {
    return static_cast<Modifier>(static_cast<std::uint8_t>(left) |
                                 static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr Modifier operator&(Modifier left, Modifier right) noexcept {
    return static_cast<Modifier>(static_cast<std::uint8_t>(left) &
                                 static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool has_modifier(Modifier modifiers,
                                          Modifier requested) noexcept {
    return (modifiers & requested) == requested;
}

struct PointerEvent {
    PointerAction action{PointerAction::move};
    PointerButton button{PointerButton::none};
    Point position{};
    Point wheel_delta{};
    Modifier modifiers{Modifier::none};
    std::uint32_t pointer_id{};
    EventPhase phase{EventPhase::target};
    bool handled{};
    // Host-normalized click cardinality. Move/wheel events report zero or one;
    // a second native press in the platform double-click sequence reports two.
    std::uint32_t click_count{1U};
    // The unit of wheel_delta (a wheel event's only).
    WheelDeltaUnit wheel_unit{WheelDeltaUnit::lines};
};

struct KeyEvent {
    KeyAction action{KeyAction::down};
    std::uint32_t physical_key{};
    Modifier modifiers{Modifier::none};
    EventPhase phase{EventPhase::target};
    bool repeat{};
    bool handled{};
};

// USB HID usage IDs are the normalized physical-key vocabulary at the host
// boundary. Text remains a separate TextInputEvent.
struct PhysicalKey final {
    static constexpr std::uint32_t a = 0x04U;
    static constexpr std::uint32_t b = 0x05U;
    static constexpr std::uint32_t c = 0x06U;
    static constexpr std::uint32_t d = 0x07U;
    static constexpr std::uint32_t e = 0x08U;
    static constexpr std::uint32_t f = 0x09U;
    static constexpr std::uint32_t g = 0x0AU;
    static constexpr std::uint32_t h = 0x0BU;
    static constexpr std::uint32_t i = 0x0CU;
    static constexpr std::uint32_t j = 0x0DU;
    static constexpr std::uint32_t k = 0x0EU;
    static constexpr std::uint32_t l = 0x0FU;
    static constexpr std::uint32_t m = 0x10U;
    static constexpr std::uint32_t n = 0x11U;
    static constexpr std::uint32_t o = 0x12U;
    static constexpr std::uint32_t p = 0x13U;
    static constexpr std::uint32_t q = 0x14U;
    static constexpr std::uint32_t r = 0x15U;
    static constexpr std::uint32_t s = 0x16U;
    static constexpr std::uint32_t t = 0x17U;
    static constexpr std::uint32_t u = 0x18U;
    static constexpr std::uint32_t v = 0x19U;
    static constexpr std::uint32_t w = 0x1AU;
    static constexpr std::uint32_t x = 0x1bU;
    static constexpr std::uint32_t y = 0x1cU;
    static constexpr std::uint32_t z = 0x1dU;
    static constexpr std::uint32_t enter = 0x28U;
    static constexpr std::uint32_t escape = 0x29U;
    static constexpr std::uint32_t backspace = 0x2AU;
    static constexpr std::uint32_t tab = 0x2BU;
    static constexpr std::uint32_t space = 0x2CU;
    static constexpr std::uint32_t minus = 0x2DU;
    static constexpr std::uint32_t equal = 0x2EU;
    static constexpr std::uint32_t slash = 0x38U;
    static constexpr std::uint32_t f1 = 0x3AU;
    static constexpr std::uint32_t f2 = 0x3BU;
    static constexpr std::uint32_t f3 = 0x3CU;
    static constexpr std::uint32_t f4 = 0x3DU;
    static constexpr std::uint32_t f5 = 0x3EU;
    static constexpr std::uint32_t f6 = 0x3FU;
    static constexpr std::uint32_t f7 = 0x40U;
    static constexpr std::uint32_t f8 = 0x41U;
    static constexpr std::uint32_t f9 = 0x42U;
    static constexpr std::uint32_t f10 = 0x43U;
    static constexpr std::uint32_t f11 = 0x44U;
    static constexpr std::uint32_t f12 = 0x45U;
    static constexpr std::uint32_t insert = 0x49U;
    static constexpr std::uint32_t home = 0x4AU;
    static constexpr std::uint32_t page_up = 0x4BU;
    static constexpr std::uint32_t end = 0x4DU;
    static constexpr std::uint32_t page_down = 0x4EU;
    static constexpr std::uint32_t delete_forward = 0x4CU;
    static constexpr std::uint32_t right = 0x4FU;
    static constexpr std::uint32_t left = 0x50U;
    static constexpr std::uint32_t down = 0x51U;
    static constexpr std::uint32_t up = 0x52U;
    static constexpr std::uint32_t keypad_minus = 0x56U;
    static constexpr std::uint32_t keypad_plus = 0x57U;
    // The context-menu key (HID "Keyboard Application"): VK_APPS on Windows,
    // the Menu keysym on X11. Apple keyboards have none; the macOS host maps
    // the virtual key code a PC keyboard's key reports there (0x6E).
    static constexpr std::uint32_t context_menu = 0x65U;
    static constexpr std::uint32_t left_control = 0xE0U;
    static constexpr std::uint32_t right_control = 0xE4U;
};

struct TextInputEvent {
    std::string text_utf8;
    bool composing{};
    std::int32_t replacement_start{-1};
    std::int32_t replacement_length{};
    bool handled{};
};

enum class DragAction : std::uint8_t {
    enter,
    over,
    leave,
    drop,
};

enum class DragEffect : std::uint8_t {
    none = 0,
    copy = 1U << 0U,
    move = 1U << 1U,
    link = 1U << 2U,
};

struct DragLimits final {
    static constexpr std::size_t maximum_items = 16U;
    static constexpr std::size_t maximum_paths = 4096U;
    static constexpr std::size_t maximum_path_bytes = 64U * 1024U;
    static constexpr std::size_t maximum_text_bytes = 16U * 1024U * 1024U;
    static constexpr std::size_t maximum_media_type_bytes = 255U;
    static constexpr std::size_t maximum_total_bytes = 16U * 1024U * 1024U;
};

[[nodiscard]] constexpr DragEffect operator|(DragEffect left,
                                              DragEffect right) noexcept {
    return static_cast<DragEffect>(static_cast<std::uint8_t>(left) |
                                   static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool has_drag_effect(DragEffect available,
                                              DragEffect requested) noexcept {
    return (static_cast<std::uint8_t>(available) &
            static_cast<std::uint8_t>(requested)) ==
           static_cast<std::uint8_t>(requested);
}

struct DragTextData final {
    std::string text_utf8;
};

struct DragFileListData final {
    std::vector<std::string> paths_utf8;
};

struct DragBinaryData final {
    std::string media_type;
    std::vector<std::uint8_t> bytes;
};

using DragDataItem = std::variant<DragTextData, DragFileListData, DragBinaryData>;

struct DragEvent final {
    DragAction action{DragAction::enter};
    std::uint64_t session_id{};
    Point position{};
    Modifier modifiers{Modifier::none};
    DragEffect allowed_effects{DragEffect::none};
    DragEffect accepted_effect{DragEffect::none};
    std::vector<DragDataItem> items;
    EventPhase phase{EventPhase::target};
    bool handled{};
};

struct DragDispatchResult final {
    bool handled{};
    DragEffect accepted_effect{DragEffect::none};
    // Lease-time drag dispatch cannot synchronously enter application code.
    // A deferred result reuses only the last valid effect for the same session;
    // capacity rejection is explicit and never pretends acceptance.
    bool deferred{};
    bool capacity_rejected{};
};

} // namespace gui_forms
