#pragma once

#include "gui_forms/binding/property_cell/property_cell.hpp"
#include "gui_forms/types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/chrome/window_chrome.hpp"

#include <string>
#include <utility>

namespace gui_forms {

// Window's chrome properties, in the capability form the controls' properties
// take (property-capabilities study, sections 2, 14 and 15), written by hand:
// Window is not a control, so tools/property_codemod.py does not generate
// them. A write `window.text = "Title";` runs `assign`: Window's prepare_write
// hook (the value's checks, std::invalid_argument changing nothing), the
// compare and store, then the written hook, which sends the property to the
// window's host while one is attached (Window::apply_window_chrome). A read is
// `window.text.read()`.

// The window's title (Form.Text): UTF-8, at most 65536 bytes, no NUL (else std::invalid_argument). Until it is first written the title is the one the window opened with (ApplicationWindowOptions::title).
template <typename Declaring>
struct WindowTextCapability {
    PropertyCell<std::string, WindowTextCapability<Declaring>> text{};

    static void assign(WindowTextCapability& capability, std::string incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.text), incoming);
        if (!capability.text.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.text));
    }
};

// The window's icon (Form.Icon): an image of the window's registry, or none for the platform's default. A PNG is decoded when the icon is sent to the host; an image the registry does not hold is std::invalid_argument.
template <typename Declaring>
struct WindowIconCapability {
    PropertyCell<ImageId, WindowIconCapability<Declaring>> icon{};

    static void assign(WindowIconCapability& capability, ImageId incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.icon), incoming);
        if (!capability.icon.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.icon));
    }
};

// The window's frame (Form.FormBorderStyle): none removes the title bar and border; the fixed styles keep the user from resizing; the tool window styles use the platform's smaller utility frame. An undefined value is std::invalid_argument.
template <typename Declaring>
struct WindowFormBorderStyleCapability {
    PropertyCell<FormBorderStyle, WindowFormBorderStyleCapability<Declaring>> form_border_style{FormBorderStyle::sizable};

    static void assign(WindowFormBorderStyleCapability& capability, FormBorderStyle incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.form_border_style), incoming);
        if (!capability.form_border_style.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.form_border_style));
    }
};

// Whether the title bar offers minimizing (Form.MinimizeBox).
template <typename Declaring>
struct WindowMinimizeBoxCapability {
    PropertyCell<bool, WindowMinimizeBoxCapability<Declaring>> minimize_box{true};

    static void assign(WindowMinimizeBoxCapability& capability, bool incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.minimize_box), incoming);
        if (!capability.minimize_box.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.minimize_box));
    }
};

// Whether the title bar offers maximizing (Form.MaximizeBox).
template <typename Declaring>
struct WindowMaximizeBoxCapability {
    PropertyCell<bool, WindowMaximizeBoxCapability<Declaring>> maximize_box{true};

    static void assign(WindowMaximizeBoxCapability& capability, bool incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.maximize_box), incoming);
        if (!capability.maximize_box.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.maximize_box));
    }
};

// Whether the title bar has its window menu and buttons (Form.ControlBox); without it the minimize and maximize boxes go too.
template <typename Declaring>
struct WindowControlBoxCapability {
    PropertyCell<bool, WindowControlBoxCapability<Declaring>> control_box{true};

    static void assign(WindowControlBoxCapability& capability, bool incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.control_box), incoming);
        if (!capability.control_box.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.control_box));
    }
};

// The state the program asks for (Form.WindowState): normal, minimized or maximized. It is the request, not a report: the user may change the native state afterwards. An undefined value is std::invalid_argument.
template <typename Declaring>
struct WindowWindowStateCapability {
    PropertyCell<FormWindowState, WindowWindowStateCapability<Declaring>> window_state{FormWindowState::normal};

    static void assign(WindowWindowStateCapability& capability, FormWindowState incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.window_state), incoming);
        if (!capability.window_state.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.window_state));
    }
};

// Whether the window stays above the other applications' windows (Form.TopMost).
template <typename Declaring>
struct WindowTopMostCapability {
    PropertyCell<bool, WindowTopMostCapability<Declaring>> top_most{false};

    static void assign(WindowTopMostCapability& capability, bool incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.top_most), incoming);
        if (!capability.top_most.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.top_most));
    }
};

// The whole window's opacity (Form.Opacity), from 0 (invisible) to 1 (opaque); outside that range or not finite is std::invalid_argument.
template <typename Declaring>
struct WindowOpacityCapability {
    PropertyCell<double, WindowOpacityCapability<Declaring>> opacity{1.0};

    static void assign(WindowOpacityCapability& capability, double incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.opacity), incoming);
        if (!capability.opacity.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.opacity));
    }
};

// Where the window is placed when it is first shown (Form.StartPosition); ignored once it has been shown. An undefined value is std::invalid_argument.
template <typename Declaring>
struct WindowStartPositionCapability {
    PropertyCell<FormStartPosition, WindowStartPositionCapability<Declaring>> start_position{FormStartPosition::windows_default_location};

    static void assign(WindowStartPositionCapability& capability, FormStartPosition incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.start_position), incoming);
        if (!capability.start_position.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.start_position));
    }
};

// Whether the window has a taskbar button (Form.ShowInTaskbar). Hosts without a taskbar report it unsupported (HostServices::supported_window_chrome).
template <typename Declaring>
struct WindowShowInTaskbarCapability {
    PropertyCell<bool, WindowShowInTaskbarCapability<Declaring>> show_in_taskbar{true};

    static void assign(WindowShowInTaskbarCapability& capability, bool incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.show_in_taskbar), incoming);
        if (!capability.show_in_taskbar.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.show_in_taskbar));
    }
};

// The client area's least size in logical units (Form.MinimumSize); zero each way keeps the window's own minimum. Each extent finite, 0 to 32768 (else std::invalid_argument).
template <typename Declaring>
struct WindowMinimumSizeCapability {
    PropertyCell<Size, WindowMinimumSizeCapability<Declaring>> minimum_size{};

    static void assign(WindowMinimumSizeCapability& capability, Size incoming) {
        Declaring& window = static_cast<Declaring&>(capability);
        window.prepare_write(std::as_const(capability.minimum_size), incoming);
        if (!capability.minimum_size.replace(std::move(incoming))) return;
        window.written(std::as_const(capability.minimum_size));
    }
};

// Every Window chrome capability, in the order Window takes them.
template <typename Declaring>
struct WindowCapabilities : public WindowTextCapability<Declaring>,
                          public WindowIconCapability<Declaring>,
                          public WindowFormBorderStyleCapability<Declaring>,
                          public WindowMinimizeBoxCapability<Declaring>,
                          public WindowMaximizeBoxCapability<Declaring>,
                          public WindowControlBoxCapability<Declaring>,
                          public WindowWindowStateCapability<Declaring>,
                          public WindowTopMostCapability<Declaring>,
                          public WindowOpacityCapability<Declaring>,
                          public WindowStartPositionCapability<Declaring>,
                          public WindowShowInTaskbarCapability<Declaring>,
                          public WindowMinimumSizeCapability<Declaring> {};

} // namespace gui_forms
