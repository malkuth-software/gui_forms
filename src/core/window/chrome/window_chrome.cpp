// Window's chrome properties (gui_forms/window/chrome): validation, the
// request a host receives, and the icon's decode.

#include "gui_forms/window.hpp"

#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "../../image/png_pixels.hpp"

#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

bool form_border_style_defined(FormBorderStyle style) noexcept {
    switch (style) {
    case FormBorderStyle::none:
    case FormBorderStyle::fixed_single:
    case FormBorderStyle::fixed_3d:
    case FormBorderStyle::fixed_dialog:
    case FormBorderStyle::sizable:
    case FormBorderStyle::fixed_tool_window:
    case FormBorderStyle::sizable_tool_window:
        return true;
    }
    return false;
}

bool form_window_state_defined(FormWindowState state) noexcept {
    const bool defined = state == FormWindowState::normal || state == FormWindowState::minimized ||
                         state == FormWindowState::maximized;
    return defined;
}

bool form_start_position_defined(FormStartPosition position) noexcept {
    switch (position) {
    case FormStartPosition::manual:
    case FormStartPosition::center_screen:
    case FormStartPosition::windows_default_location:
    case FormStartPosition::windows_default_bounds:
    case FormStartPosition::center_parent:
        return true;
    }
    return false;
}

bool form_border_style_is_tool(FormBorderStyle style) noexcept {
    const bool tool = style == FormBorderStyle::fixed_tool_window || style == FormBorderStyle::sizable_tool_window;
    return tool;
}

bool form_border_style_is_sizable(FormBorderStyle style) noexcept {
    const bool sizable = style == FormBorderStyle::sizable || style == FormBorderStyle::sizable_tool_window;
    return sizable;
}

namespace {

// Decodes a registry image to premultiplied BGRA8: pixels copied as they
// are, a PNG through the one PNG pixel path. An image that cannot be
// decoded gives an empty icon (the platform's default).
HostImage decode_icon(const ImageResourceView& view) {
    HostImage icon{};
    const std::uint32_t width = view.metadata.width;
    const std::uint32_t height = view.metadata.height;
    if (width == 0U || height == 0U) return icon;
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4U;
    // The registry admitted the image, so its pixel count is within its
    // limits; the product cannot overflow 64 bits for 32-bit extents.
    const std::uint64_t byte_count = row_bytes * height;
    if (byte_count > HostImage::maximum_bytes) return icon;
    if (view.encoding == ImageResourceEncoding::bgra32_premultiplied) {
        if (view.row_bytes < row_bytes || view.encoded.size() < view.row_bytes * (height - 1U) + row_bytes) {
            return icon;
        }
        icon.pixels.resize(static_cast<std::size_t>(byte_count));
        for (std::uint32_t row = 0U; row < height; ++row) {
            const std::byte* source = view.encoded.data() + static_cast<std::size_t>(view.row_bytes * row);
            std::byte* target = icon.pixels.data() + static_cast<std::size_t>(row_bytes * row);
            std::memcpy(target, source, static_cast<std::size_t>(row_bytes));
        }
    } else {
        image::PngPixelRequest request{};
        request.limits = image::png_limits_for_image(width, height, view.encoded.size());
        request.target = {image::PixelOrder::bgra, image::PixelAlpha::premultiplied};
        request.purpose = image::PngPixelPurpose::resource;
        image::PngPixelDecoder decoder{};
        icon.pixels.resize(static_cast<std::size_t>(byte_count));
        const image::PngPixelResult decoded =
            decoder.decode(view.encoded, request, icon.pixels, static_cast<std::size_t>(row_bytes));
        if (decoded.error != image::PngPixelError::none || decoded.width != width || decoded.height != height) {
            const HostImage none{};
            return none;
        }
    }
    icon.width = width;
    icon.height = height;
    icon.row_bytes = row_bytes;
    return icon;
}

bool valid_window_text(const std::string& text) {
    const bool valid = text.size() <= maximum_window_text_bytes && text.find('\0') == std::string::npos &&
                       validate_utf8(text).valid();
    return valid;
}

bool valid_minimum_extent(double extent) noexcept {
    const bool valid = std::isfinite(extent) && extent >= 0.0 && extent <= maximum_window_minimum_extent;
    return valid;
}

} // namespace

WindowChromeFields Window::supported_chrome() const noexcept {
    if (host_services_ == nullptr) {
        const WindowChromeFields none{};
        return none;
    }
    const WindowChromeFields supported = (*host_services_).supported_window_chrome();
    return supported;
}

WindowChrome Window::chrome() const {
    WindowChrome request{};
    request.text = text.read();
    if (icon.read().value != 0U) {
        const std::optional<ImageResourceView> found = image_resources().find(icon.read());
        if (found.has_value()) request.icon = decode_icon(*found);
    }
    request.form_border_style = form_border_style.read();
    request.minimize_box = minimize_box.read();
    request.maximize_box = maximize_box.read();
    request.control_box = control_box.read();
    request.window_state = window_state.read();
    request.top_most = top_most.read();
    request.opacity = opacity.read();
    request.start_position = start_position.read();
    request.show_in_taskbar = show_in_taskbar.read();
    request.minimum_size = minimum_size.read();
    return request;
}

void Window::apply_window_chrome(WindowChromeFields fields) {
    const WindowChromeFields wanted{fields.bits & written_chrome_.bits};
    if (host_services_ == nullptr || wanted.empty()) return;
    const WindowChrome request = chrome();
    const HostWindowChromeResult result = (*host_services_).apply_window_chrome(request, wanted);
    // The fields asked for now leave the applied set, then those the host
    // applied join it.
    applied_chrome_.bits = (applied_chrome_.bits & ~wanted.bits) | result.applied.bits;
}

// The hooks: each validates (std::invalid_argument, changing nothing) or,
// after a store, records the write and sends the property to the host.

void Window::prepare_write(const decltype(text)&, std::string& incoming) {
    require_root_thread("window text");
    if (!valid_window_text(incoming)) {
        throw std::invalid_argument("GUI.Forms window text must be UTF-8 without NUL, at most 65536 bytes");
    }
}
void Window::written(const decltype(text)&) {
    written_chrome_.add(WindowChromeField::text);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::text)});
}

void Window::prepare_write(const decltype(icon)&, ImageId& incoming) {
    require_root_thread("window icon");
    if (incoming.value != 0U && !image_resources().find(incoming).has_value()) {
        throw std::invalid_argument("GUI.Forms window icon is not an image of the window's registry");
    }
}
void Window::written(const decltype(icon)&) {
    written_chrome_.add(WindowChromeField::icon);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::icon)});
}

void Window::prepare_write(const decltype(form_border_style)&, FormBorderStyle& incoming) {
    require_root_thread("window border style");
    if (!form_border_style_defined(incoming)) {
        throw std::invalid_argument("GUI.Forms window border style is not a FormBorderStyle");
    }
}
void Window::written(const decltype(form_border_style)&) {
    written_chrome_.add(WindowChromeField::form_border_style);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::form_border_style)});
}

void Window::prepare_write(const decltype(minimize_box)&, bool&) {
    require_root_thread("window minimize box");
}
void Window::written(const decltype(minimize_box)&) {
    written_chrome_.add(WindowChromeField::minimize_box);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::minimize_box)});
}

void Window::prepare_write(const decltype(maximize_box)&, bool&) {
    require_root_thread("window maximize box");
}
void Window::written(const decltype(maximize_box)&) {
    written_chrome_.add(WindowChromeField::maximize_box);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::maximize_box)});
}

void Window::prepare_write(const decltype(control_box)&, bool&) {
    require_root_thread("window control box");
}
void Window::written(const decltype(control_box)&) {
    written_chrome_.add(WindowChromeField::control_box);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::control_box)});
}

void Window::prepare_write(const decltype(window_state)&, FormWindowState& incoming) {
    require_root_thread("window state");
    if (!form_window_state_defined(incoming)) {
        throw std::invalid_argument("GUI.Forms window state is not a FormWindowState");
    }
}
void Window::written(const decltype(window_state)&) {
    written_chrome_.add(WindowChromeField::window_state);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::window_state)});
}

void Window::prepare_write(const decltype(top_most)&, bool&) {
    require_root_thread("window top-most");
}
void Window::written(const decltype(top_most)&) {
    written_chrome_.add(WindowChromeField::top_most);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::top_most)});
}

void Window::prepare_write(const decltype(opacity)&, double& incoming) {
    require_root_thread("window opacity");
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > 1.0) {
        throw std::invalid_argument("GUI.Forms window opacity must be finite, from 0 to 1");
    }
}
void Window::written(const decltype(opacity)&) {
    written_chrome_.add(WindowChromeField::opacity);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::opacity)});
}

void Window::prepare_write(const decltype(start_position)&, FormStartPosition& incoming) {
    require_root_thread("window start position");
    if (!form_start_position_defined(incoming)) {
        throw std::invalid_argument("GUI.Forms window start position is not a FormStartPosition");
    }
}
void Window::written(const decltype(start_position)&) {
    written_chrome_.add(WindowChromeField::start_position);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::start_position)});
}

void Window::prepare_write(const decltype(show_in_taskbar)&, bool&) {
    require_root_thread("window taskbar button");
}
void Window::written(const decltype(show_in_taskbar)&) {
    written_chrome_.add(WindowChromeField::show_in_taskbar);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::show_in_taskbar)});
}

void Window::prepare_write(const decltype(minimum_size)&, Size& incoming) {
    require_root_thread("window minimum size");
    if (!valid_minimum_extent(incoming.width) || !valid_minimum_extent(incoming.height)) {
        throw std::invalid_argument("GUI.Forms window minimum size must be finite, 0 to 32768 each way");
    }
}
void Window::written(const decltype(minimum_size)&) {
    written_chrome_.add(WindowChromeField::minimum_size);
    apply_window_chrome(WindowChromeFields{static_cast<std::uint32_t>(WindowChromeField::minimum_size)});
}

} // namespace gui_forms
