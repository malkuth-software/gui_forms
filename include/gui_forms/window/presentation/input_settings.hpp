#pragma once
// The input settings a host reports (HostInputSettingsEvent) and the window
// keeps in PresentationSettings. Kept apart so that the host protocol's
// types can name them without the window's presentation types.
#include "gui_forms/export/application_export.hpp"
#include <cstdint>

namespace gui_forms {

// Where a setting the host reports came from.
enum class SettingOrigin : std::uint8_t {
    // GUI.Forms' stated default: the host has no such setting (a desktop
    // session that publishes none), or no host has reported yet.
    default_value,
    // The platform's own setting, read by the host.
    host,
};

// The double-click interval GUI.Forms states where the host has none, and
// the longest one admitted (Windows' own limit). Perceptual durations, in
// milliseconds.
inline constexpr std::uint32_t default_double_click_milliseconds = 400U;
inline constexpr std::uint32_t maximum_double_click_milliseconds = 5000U;

// The pointer settings' Windows-like defaults (owner's mouse-event rule,
// 2026-11-08), used only where the host reports none: Windows' own
// defaults for SM_CXDRAG, SM_CYDRAG, SPI_GETMOUSEHOVERTIME and the hover
// rectangle. Distances in logical pixels, durations perceptual, in
// milliseconds.
inline constexpr double default_drag_size = 4.0;
inline constexpr std::uint32_t default_hover_milliseconds = 400U;
inline constexpr double default_hover_size = 4.0;
// The largest drag or hover rectangle side and hover time a window takes.
inline constexpr double maximum_pointer_distance = 64.0;
inline constexpr std::uint32_t maximum_hover_milliseconds = 10000U;

// The pointer settings controls and the window read (the WinForms
// SystemInformation values), from the host where its platform offers them.
// GUI.Forms recognizes no gestures: controls compare a press's movement
// against the drag rectangle in their MouseMove and start a drag
// themselves.
// - drag_width, drag_height: the rectangle, centred on the press, the
//   pointer must leave before a drag starts (SystemInformation.DragSize;
//   Windows SM_CXDRAG/SM_CYDRAG, X11 XSettings Net/DndDragThreshold; macOS
//   offers none, so its host leaves the default).
// - hover_milliseconds, hover_width, hover_height: how long the pointer
//   must rest within the hover rectangle before a control raises
//   MouseHover (SystemInformation.MouseHoverTime and MouseHoverSize).
// - native_hover: the host reports hover itself (HostHoverEvent, Windows
//   WM_MOUSEHOVER); the window's own hover timer then stays off.
struct GUI_FORMS_API PointerSettings final {
    double drag_width{default_drag_size};
    double drag_height{default_drag_size};
    SettingOrigin drag_origin{SettingOrigin::default_value};
    std::uint32_t hover_milliseconds{default_hover_milliseconds};
    double hover_width{default_hover_size};
    double hover_height{default_hover_size};
    SettingOrigin hover_origin{SettingOrigin::default_value};
    bool native_hover{};

    friend constexpr bool operator==(const PointerSettings& left,
                                     const PointerSettings& right) noexcept {
        return left.drag_width == right.drag_width && left.drag_height == right.drag_height &&
               left.drag_origin == right.drag_origin &&
               left.hover_milliseconds == right.hover_milliseconds &&
               left.hover_width == right.hover_width && left.hover_height == right.hover_height &&
               left.hover_origin == right.hover_origin && left.native_hover == right.native_hover;
    }
};

// Whether every value lies in its range (each rectangle side finite and
// between 0 and maximum_pointer_distance, the hover time between 1 and
// maximum_hover_milliseconds) and every origin is in its enum.
[[nodiscard]] GUI_FORMS_API bool valid_pointer_settings(const PointerSettings& settings) noexcept;

// The caret's blink phase GUI.Forms states where the host has none (the
// Windows default, GetCaretBlinkTime), and the longest phase admitted.
// Perceptual durations, in milliseconds.
inline constexpr std::uint32_t default_caret_blink_milliseconds = 530U;
inline constexpr std::uint32_t maximum_caret_blink_milliseconds = 10000U;

// How a text caret blinks (audit N75): blink_milliseconds is one phase,
// the time the caret stays shown or hidden before it toggles; 0 means the
// caret never blinks and stays shown (the user turned blinking off).
// Sources: Windows GetCaretBlinkTime (INFINITE is 0), X11 XSettings
// Net/CursorBlink and Net/CursorBlinkTime (a whole cycle, halved), macOS
// the NSTextInsertionPointBlinkPeriodOn default where set.
struct GUI_FORMS_API CaretSettings final {
    std::uint32_t blink_milliseconds{default_caret_blink_milliseconds};
    SettingOrigin origin{SettingOrigin::default_value};

    friend constexpr bool operator==(const CaretSettings& left, const CaretSettings& right) noexcept {
        return left.blink_milliseconds == right.blink_milliseconds && left.origin == right.origin;
    }
};

// Whether the phase is at most maximum_caret_blink_milliseconds (0, never
// blinking, included) and the origin is in its enum.
[[nodiscard]] GUI_FORMS_API bool valid_caret_settings(const CaretSettings& settings) noexcept;

} // namespace gui_forms
