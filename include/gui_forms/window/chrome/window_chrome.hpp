#pragma once

// A window's chrome: the properties Windows Forms puts on Form for the
// native window around the client area (Text, Icon, FormBorderStyle, the
// three boxes, WindowState, TopMost, Opacity, StartPosition, ShowInTaskbar,
// MinimumSize). Window holds them as properties (window_capabilities.hpp)
// and sends them to its host through HostServices::apply_window_chrome;
// each host applies what its platform can and reports which properties it
// honours (HostServices::supported_window_chrome).

#include "gui_forms/types.hpp"
#include "gui_forms/host/image/host_image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace gui_forms {

// Windows Forms' FormBorderStyle, with its values.
enum class FormBorderStyle : std::uint8_t {
    none = 0,
    fixed_single = 1,
    fixed_3d = 2,
    fixed_dialog = 3,
    sizable = 4,
    fixed_tool_window = 5,
    sizable_tool_window = 6,
};

// Windows Forms' FormWindowState, with its values.
enum class FormWindowState : std::uint8_t {
    normal = 0,
    minimized = 1,
    maximized = 2,
};

// Windows Forms' FormStartPosition, with its values. It places the window
// when it is first shown and is ignored after.
enum class FormStartPosition : std::uint8_t {
    manual = 0,
    center_screen = 1,
    windows_default_location = 2,
    windows_default_bounds = 3,
    center_parent = 4,
};

// One chrome property, as a bit of WindowChromeFields.
enum class WindowChromeField : std::uint32_t {
    text = 1U,
    icon = 2U,
    form_border_style = 4U,
    minimize_box = 8U,
    maximize_box = 16U,
    control_box = 32U,
    window_state = 64U,
    top_most = 128U,
    opacity = 256U,
    start_position = 512U,
    show_in_taskbar = 1024U,
    minimum_size = 2048U,
};

// A set of chrome properties.
struct WindowChromeFields final {
    static constexpr std::uint32_t all_bits = 4095U;
    std::uint32_t bits{};

    [[nodiscard]] bool contains(WindowChromeField field) const noexcept {
        const bool present = (bits & static_cast<std::uint32_t>(field)) != 0U;
        return present;
    }
    void add(WindowChromeField field) noexcept { bits = bits | static_cast<std::uint32_t>(field); }
    [[nodiscard]] bool empty() const noexcept { return bits == 0U; }
    friend bool operator==(WindowChromeFields left, WindowChromeFields right) noexcept {
        return left.bits == right.bits;
    }
};

// What a window asks its host for. text is UTF-8; icon is premultiplied
// BGRA8, empty (zero width) for the platform's default; opacity is in
// [0, 1]; minimum_size is the client area's least size in logical units,
// zero for the window's own minimum. Only the fields a request names
// (apply_window_chrome) are read.
struct WindowChrome final {
    std::string text{};
    HostImage icon{};
    FormBorderStyle form_border_style{FormBorderStyle::sizable};
    bool minimize_box{true};
    bool maximize_box{true};
    bool control_box{true};
    FormWindowState window_state{FormWindowState::normal};
    bool top_most{};
    double opacity{1.0};
    FormStartPosition start_position{FormStartPosition::windows_default_location};
    bool show_in_taskbar{true};
    Size minimum_size{};
};

// The longest window text, in UTF-8 bytes (as ApplicationWindowHandle::
// set_title), and the largest minimum size each way.
inline constexpr std::size_t maximum_window_text_bytes = 65536U;
inline constexpr double maximum_window_minimum_extent = 32768.0;

// Whether each value is one the enumeration defines.
[[nodiscard]] bool form_border_style_defined(FormBorderStyle style) noexcept;
[[nodiscard]] bool form_window_state_defined(FormWindowState state) noexcept;
[[nodiscard]] bool form_start_position_defined(FormStartPosition position) noexcept;
// Whether a border style is one of the two tool window styles.
[[nodiscard]] bool form_border_style_is_tool(FormBorderStyle style) noexcept;
// Whether a border style lets the user resize the window.
[[nodiscard]] bool form_border_style_is_sizable(FormBorderStyle style) noexcept;

} // namespace gui_forms
