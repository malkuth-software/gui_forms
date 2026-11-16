// The window manager's services (surface_manager_internal.hpp): the surface
// as the one monitor, the cursor, pointer capture, the clipboard copy and
// the dialogs (surface_dialogs.cpp).
#include "surface_manager_internal.hpp"

#include <string>
#include <variant>

namespace gui_forms::host::surface_detail {

Services::Services(NativeWindow& window) : HostServices(window.runtime.host.capabilities()), window_(window) {}

Services::~Services() { shutdown(); }

// The surface in device pixels, as a desktop host reports its monitors
// (HostDisplayEvent's units).
HostMonitorResult Services::query_monitors_impl() {
    HostMonitorResult result;
    const Runtime& runtime = window_.runtime;
    result.monitors.resize(1U);
    HostMonitor& monitor = result.monitors.front();
    monitor.id.assign(runtime.host.monitor_id());
    monitor.frame = Rect{0.0, 0.0, runtime.extent.width * runtime.scale, runtime.extent.height * runtime.scale};
    monitor.work_area = monitor.frame;
    monitor.scale = runtime.scale;
    monitor.primary = true;
    return result;
}

// One cursor for the surface: the window under the pointer sets it, so a
// window elsewhere does not change it.
HostServiceStatus Services::set_cursor_impl(CursorKind cursor) {
    window_.current_cursor = cursor;
    Runtime& runtime = window_.runtime;
    const bool under_pointer = runtime.pointer_window == window_.window_id || runtime.pressed_window == window_.window_id;
    if (under_pointer) runtime.show_cursor(cursor);
    return {};
}

HostServiceStatus Services::apply_window_backdrop_impl(WindowBackdrop backdrop) {
    switch (backdrop) {
    case WindowBackdrop::opaque:
        window_.present_alpha = false;
        break;
    case WindowBackdrop::transparent:
        window_.present_alpha = true;
        break;
    // The materials are host_unsupported here (Runtime's backdrop support), so the service refuses them.
    default:
        return {HostServiceError::backend_failure};
    }
    window_.raster_resized = true;
    window_.runtime.scene_changed();
    return {};
}

// The host keeps the pointer's events coming outside the surface; the
// manager's own routing already keeps a press's window until its release
// (Runtime::pressed_window).
HostServiceStatus Services::set_pointer_capture_impl(bool captured, std::uint64_t pointer_id) {
    static_cast<void>(pointer_id);
    window_.runtime.host.pointer_capture(captured);
    return {};
}

#if !defined(GUI_FORMS_HIGH_INTEGRITY) || defined(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
HostClipboardTextResult Services::read_clipboard_text_impl() {
    HostClipboardTextResult result;
    const Runtime& runtime = window_.runtime;
    result.text_utf8 = runtime.clipboard_text;
    result.generation = runtime.clipboard_generation;
    result.has_text = !runtime.clipboard_text.empty();
    return result;
}

// The host may refuse the write (a browser wants focus and a recent
// gesture); the manager's copy is kept either way, so a paste within the
// run reads what was copied.
HostServiceStatus Services::write_clipboard_text_impl(std::string_view text) {
    Runtime& runtime = window_.runtime;
    runtime.clipboard_text.assign(text);
    ++runtime.clipboard_generation;
    runtime.host.write_clipboard(runtime.clipboard_text);
    return {};
}
#endif

HostDialogResult Services::show_dialog_impl(const HostDialogRequest& request) {
    const HostMessageDialogRequest* message = std::get_if<HostMessageDialogRequest>(&request.payload);
    if (message != nullptr) {
        const HostDialogResult result = message_dialog(window_, request, *message);
        return result;
    }
    const HostDialogResult result = host_dialog(window_, request);
    return result;
}

HostServiceStatus Services::play_sound_cue_impl(const HostSoundCueRequest& request) {
    static_cast<void>(request);
    return {HostServiceError::unsupported};
}

} // namespace gui_forms::host::surface_detail
