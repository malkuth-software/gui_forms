#include "gui_forms/c_api.h"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/inspection_controls.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/scrolling.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "headless_host.hpp"
#if defined(GF_C_API_HAS_WINDOWS_HOST)
#include "windows_host.hpp"
#endif
#if defined(GF_C_API_HAS_MACOS_HOST)
#include "macos_host.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "registry/registry.hpp"
#include "paint_endpoint/windows_compatibility_paint_endpoint.hpp"

using namespace gui_forms::abi::detail;

namespace {

gf_result api_last_error(gf_error_view* error) noexcept {
    if (error == nullptr) {
        return fail(GF_ERROR_INVALID_ARGUMENT, "last_error requires an output");
    }
    (*error).code = last_error_code;
    (*error).message = {last_error_message.data(), last_error_message.size()};
    return GF_OK;
}

gf_result api_control_create(gf_string_view id, gf_handle* control) noexcept {
    return translate(&Registry::create, &registry(), GF_CONTROL_GENERIC, id, control);
}
gf_result api_control_create_kind(std::uint32_t kind, gf_string_view id,
                                  gf_handle* control) noexcept {
    return translate(&Registry::create, &registry(), kind, id, control);
}
gf_result api_retain(gf_handle handle) noexcept {
    return translate(&Registry::retain, &registry(), handle);
}
gf_result api_release(gf_handle handle) noexcept {
    return translate(&Registry::release, &registry(), handle);
}
gf_result api_dispose(gf_handle handle) noexcept {
    return translate(&Registry::dispose, &registry(), handle);
}
gf_result api_component_state(gf_handle handle, std::uint32_t* state) noexcept {
    return translate(&Registry::component_state, &registry(), handle, state);
}
gf_result api_stable_id(gf_handle handle,
                        char* buffer,
                        std::uint64_t capacity,
                        std::uint64_t* required) noexcept {
    return translate(&Registry::stable_id, &registry(), handle, buffer, capacity, required);
}
gf_result api_set_visible(gf_handle handle, std::uint32_t visible) noexcept {
    return translate(&Registry::set_visible, &registry(), handle, visible);
}
gf_result api_get_visible(gf_handle handle, std::uint32_t* visible) noexcept {
    return translate(&Registry::get_visible, &registry(), handle, visible);
}
gf_result api_set_bounds(gf_handle handle, gf_rect bounds) noexcept {
    return translate(&Registry::set_bounds, &registry(), handle, bounds);
}
gf_result api_get_bounds(gf_handle handle, gf_rect* bounds) noexcept {
    return translate(&Registry::get_bounds, &registry(), handle, bounds);
}
gf_result api_get_control_absolute_bounds(gf_handle handle,
                                          gf_rect* bounds) noexcept {
    return translate(&Registry::get_control_absolute_bounds, &registry(), handle, bounds);
}
gf_result api_add_child(gf_handle parent, gf_handle child) noexcept {
    return translate(&Registry::add_child, &registry(), parent, child);
}
gf_result api_remove_child(gf_handle parent, gf_handle child) noexcept {
    return translate(&Registry::remove_child, &registry(), parent, child);
}
gf_result api_subscribe(gf_handle sender,
                        std::uint32_t event_kind,
                        gf_event_callback callback,
                        void* context,
                        gf_event_token* token) noexcept {
    return translate(&Registry::subscribe, &registry(), sender, event_kind,
                     callback, context, token);
}
gf_result api_disconnect(gf_event_token token) noexcept {
    return translate(&Registry::disconnect, &registry(), token);
}
gf_result api_set_name(gf_handle handle, gf_string_view value) noexcept {
    return translate(&Registry::set_string, &registry(), handle, value, false);
}
gf_result api_get_name(gf_handle handle, char* buffer, std::uint64_t capacity,
                       std::uint64_t* required) noexcept {
    return translate(&Registry::get_string, &registry(), handle, buffer, capacity, required, false);
}
gf_result api_set_text(gf_handle handle, gf_string_view value) noexcept {
    return translate(&Registry::set_string, &registry(), handle, value, true);
}
gf_result api_get_text(gf_handle handle, char* buffer, std::uint64_t capacity,
                       std::uint64_t* required) noexcept {
    return translate(&Registry::get_string, &registry(), handle, buffer, capacity, required, true);
}
gf_result api_set_enabled(gf_handle handle, std::uint32_t enabled) noexcept {
    return translate(&Registry::set_enabled, &registry(), handle, enabled);
}
gf_result api_get_enabled(gf_handle handle, std::uint32_t* enabled) noexcept {
    return translate(&Registry::get_enabled, &registry(), handle, enabled);
}
gf_result api_set_cursor(gf_handle handle, std::uint32_t cursor_kind) noexcept {
    return translate(&Registry::set_cursor, &registry(), handle, cursor_kind);
}
gf_result api_get_cursor(gf_handle handle, std::uint32_t* cursor_kind) noexcept {
    return translate(&Registry::get_cursor, &registry(), handle, cursor_kind);
}
gf_result api_set_auto_scroll_offset(gf_handle handle, gf_point offset) noexcept {
    return translate(&Registry::set_auto_scroll_offset, &registry(), handle, offset);
}
gf_result api_set_auto_scroll(gf_handle handle, std::uint32_t enabled) noexcept {
    return translate(&Registry::set_auto_scroll, &registry(), handle, enabled);
}
gf_result api_set_auto_scroll_margin(gf_handle handle, gf_size margin) noexcept {
    return translate(&Registry::set_auto_scroll_margin, &registry(), handle, margin);
}
gf_result api_set_auto_scroll_min_size(gf_handle handle, gf_size size) noexcept {
    return translate(&Registry::set_auto_scroll_min_size, &registry(), handle, size);
}
gf_result api_set_auto_scroll_position(gf_handle handle,
                                       gf_point position) noexcept {
    return translate(&Registry::set_auto_scroll_position, &registry(), handle, position);
}
gf_result api_get_scroll_state(gf_handle handle, gf_scroll_state* state) noexcept {
    return translate(&Registry::get_scroll_state, &registry(), handle, state);
}
gf_result api_set_scroll_axis_state(gf_handle handle,
                                    std::uint32_t orientation,
                                    gf_scroll_axis_state state) noexcept {
    return translate(&Registry::set_scroll_axis_state, &registry(), handle, orientation, state);
}
gf_result api_scroll_control_into_view(gf_handle handle,
                                       gf_handle child) noexcept {
    return translate(&Registry::scroll_control_into_view, &registry(), handle, child);
}
gf_result api_suspend_layout(gf_handle handle) noexcept {
    return translate(&Registry::suspend_layout, &registry(), handle);
}
gf_result api_resume_layout(gf_handle handle,
                            std::uint32_t perform_layout) noexcept {
    return translate(&Registry::resume_layout, &registry(), handle, perform_layout);
}
gf_result api_perform_control_layout(gf_handle handle) noexcept {
    return translate(&Registry::perform_control_layout, &registry(), handle);
}
gf_result api_get_layout_state(gf_handle handle,
                               gf_layout_state* state) noexcept {
    return translate(&Registry::get_layout_state, &registry(), handle, state);
}
gf_result api_property_grid_set_selected_controls(
    gf_handle property_grid, const gf_handle* controls,
    std::uint64_t count) noexcept {
    return translate(&Registry::property_grid_set_selected_controls, &registry(),
            property_grid, controls, count);
}
gf_result api_property_grid_set_sort(gf_handle property_grid,
                                     std::uint32_t property_sort) noexcept {
    return translate(&Registry::property_grid_set_sort, &registry(), property_grid, property_sort);
}
gf_result api_property_grid_get_sort(gf_handle property_grid,
                                     std::uint32_t* property_sort) noexcept {
    return translate(&Registry::property_grid_get_sort, &registry(), property_grid, property_sort);
}
gf_result api_property_grid_refresh(gf_handle property_grid) noexcept {
    return translate(&Registry::property_grid_refresh, &registry(), property_grid);
}
gf_result api_property_object_define(
    gf_handle property_object,
    const gf_property_descriptor_v1* descriptor,
    const gf_property_callbacks_v1* callbacks) noexcept {
    return translate(&Registry::property_object_define, &registry(),
            property_object, descriptor, callbacks);
}
gf_result api_property_object_notify_changed(
    gf_handle property_object, gf_string_view property_name) noexcept {
    return translate(&Registry::property_object_notify_changed, &registry(),
            property_object, property_name);
}
gf_result api_property_grid_try_set_text(
    gf_handle property_grid, gf_string_view property_name,
    gf_string_view text, std::uint32_t* committed) noexcept {
    return translate(&Registry::property_grid_try_set_text, &registry(),
            property_grid, property_name, text, committed);
}
gf_result api_property_grid_reset_property(
    gf_handle property_grid, gf_string_view property_name,
    std::uint32_t* committed) noexcept {
    return translate(&Registry::property_grid_reset_property, &registry(),
            property_grid, property_name, committed);
}
gf_result api_property_grid_activate_editor(
    gf_handle property_grid, gf_string_view property_name,
    std::uint32_t* activated) noexcept {
    return translate(&Registry::property_grid_activate_editor, &registry(),
            property_grid, property_name, activated);
}
gf_result api_run_window(gf_handle handle, std::uint32_t flags) noexcept {
    return translate(&Registry::run_window, &registry(), handle, flags);
}
gf_result api_last_host_trace(gf_handle handle, char* buffer,
                              std::uint64_t capacity,
                              std::uint64_t* required) noexcept {
    return translate(&Registry::last_host_trace, &registry(), handle, buffer, capacity, required);
}
gf_result api_subscribe_v2(gf_handle sender, std::uint32_t event_kind,
                           gf_event_callback_v2 callback, void* context,
                           gf_event_token* token) noexcept {
    return translate(&Registry::subscribe_v2, &registry(), sender, event_kind, callback, context, token);
}
gf_result api_begin_invoke(gf_handle control, gf_dispatch_callback callback,
                           void* context) noexcept {
    return translate(&Registry::begin_invoke, &registry(), control, callback, context);
}
gf_result api_request_close(gf_handle form) noexcept {
    return translate(&Registry::request_close, &registry(), form);
}
gf_result api_callback_fault_count(gf_handle control,
                                   std::uint64_t* count) noexcept {
    return translate(&Registry::callback_fault_count, &registry(), control, count);
}
gf_result api_set_control_png(gf_handle control, const std::uint8_t* encoded,
                              std::uint64_t encoded_size) noexcept {
    return translate(&Registry::set_control_png, &registry(), control, encoded, encoded_size);
}
gf_result api_set_control_pixels(gf_handle control, const std::uint8_t* pixels,
                                 std::uint32_t width, std::uint32_t height,
                                 std::uint64_t row_bytes,
                                 std::uint32_t pixel_format) noexcept {
    return translate(&Registry::set_control_pixels, &registry(), control, pixels, width, height,
                                             row_bytes, pixel_format);
}
gf_result api_set_child_index(gf_handle parent, gf_handle child,
                              std::uint64_t index) noexcept {
    return translate(&Registry::set_child_index, &registry(), parent, child, index);
}
gf_result api_set_control_colors(gf_handle control, std::uint32_t foreground_argb,
                                 std::uint32_t background_argb) noexcept {
    return translate(&Registry::set_control_colors, &registry(), control, foreground_argb,
                                             background_argb);
}
gf_result api_set_control_text_alignment(gf_handle control,
                                         std::uint32_t content_alignment) noexcept {
    return translate(&Registry::set_control_text_alignment, &registry(), control, content_alignment);
}
gf_result api_set_button_appearance(gf_handle button, std::uint32_t visual_style,
                                    double flat_border_width) noexcept {
    return translate(&Registry::set_button_appearance, &registry(), button, visual_style,
                                                flat_border_width);
}
gf_result api_set_panel_border_style(gf_handle panel,
                                     std::uint32_t border_style) noexcept {
    return translate(&Registry::set_panel_border_style, &registry(), panel, border_style);
}

gf_result api_attach_overlay(gf_handle owner, gf_handle overlay) noexcept {
    return translate(&Registry::attach_overlay, &registry(), owner, overlay);
}

gf_result api_detach_overlay(gf_handle overlay) noexcept {
    return translate(&Registry::detach_overlay, &registry(), overlay);
}
gf_result api_subscribe_pointer(gf_handle sender, gf_pointer_callback callback,
                                void* context, gf_event_token* token) noexcept {
    return translate(&Registry::subscribe_pointer, &registry(), sender, callback, context, token);
}
gf_result api_set_check_state(gf_handle control, std::uint32_t check_state) noexcept {
    return translate(&Registry::set_check_state, &registry(), control, check_state);
}
gf_result api_get_check_state(gf_handle control, std::uint32_t* check_state) noexcept {
    return translate(&Registry::get_check_state, &registry(), control, check_state);
}
gf_result api_subscribe_key(gf_handle sender, gf_key_callback callback,
                            void* context, gf_event_token* token) noexcept {
    return translate(&Registry::subscribe_key, &registry(), sender, callback, context, token);
}
gf_result api_subscribe_key_preview(gf_handle sender, gf_key_callback callback,
                                    void* context,
                                    gf_event_token* token) noexcept {
    return translate(&Registry::subscribe_key_preview, &registry(), sender, callback, context, token);
}
gf_result api_subscribe_text(gf_handle sender, gf_text_callback callback,
                             void* context, gf_event_token* token) noexcept {
    return translate(&Registry::subscribe_text, &registry(), sender, callback, context, token);
}
gf_result api_set_range(gf_handle control, double minimum, double maximum) noexcept {
    return translate(&Registry::set_range, &registry(), control, minimum, maximum);
}
gf_result api_get_range(gf_handle control, double* minimum, double* maximum) noexcept {
    return translate(&Registry::get_range, &registry(), control, minimum, maximum);
}
gf_result api_set_range_value(gf_handle control, double value) noexcept {
    return translate(&Registry::set_range_value, &registry(), control, value);
}
gf_result api_get_range_value(gf_handle control, double* value) noexcept {
    return translate(&Registry::get_range_value, &registry(), control, value);
}
gf_result api_set_pointer_capture(gf_handle control, std::uint32_t captured) noexcept {
    return translate(&Registry::set_pointer_capture, &registry(), control, captured);
}
gf_result api_get_pointer_capture(gf_handle control, std::uint32_t* captured) noexcept {
    return translate(&Registry::get_pointer_capture, &registry(), control, captured);
}
gf_result api_show_path_dialog(gf_handle owner, std::uint32_t kind,
                               gf_string_view title,
                               gf_string_view initial_directory,
                               gf_string_view suggested_name,
                               gf_string_view default_extension,
                               gf_string_view filter,
                               std::uint32_t flags,
                               std::uint32_t* accepted) noexcept {
    return translate(&Registry::show_path_dialog, &registry(), owner, kind, title,
                                           initial_directory, suggested_name,
                                           default_extension, filter, flags,
                                           accepted);
}
gf_result api_last_dialog_path(gf_handle owner, char* buffer,
                               std::uint64_t capacity,
                               std::uint64_t* required) noexcept {
    return translate(&Registry::last_dialog_path, &registry(), owner, buffer, capacity, required);
}
gf_result api_show_tooltip(gf_handle owner, gf_string_view text, double x,
                           double y,
                           std::uint32_t duration_milliseconds) noexcept {
    return translate(&Registry::show_tooltip, &registry(), owner, text, x, y,
                                       duration_milliseconds);
}
gf_result api_hide_tooltip(gf_handle owner) noexcept {
    return translate(&Registry::hide_tooltip, &registry(), owner);
}

gf_result api_set_field_selection(gf_handle control,
                                  std::uint64_t selection_start_utf8,
                                  std::uint64_t selection_length_utf8,
                                  std::uint32_t caret_visible) noexcept {
    return translate(&Registry::set_field_selection, &registry(), control, selection_start_utf8,
                                              selection_length_utf8,
                                              caret_visible);
}
gf_result api_set_field_edit_state(gf_handle control,
                                   std::uint64_t anchor_utf8,
                                   std::uint64_t caret_utf8,
                                   std::uint32_t caret_visible) noexcept {
    return translate(&Registry::set_field_edit_state, &registry(), control, anchor_utf8, caret_utf8,
                                               caret_visible);
}
gf_result api_field_position_from_point(gf_handle control, double local_x,
                                        std::uint64_t* position_utf8) noexcept {
    return translate(&Registry::field_position_from_point, &registry(), control, local_x,
                                                    position_utf8);
}
gf_result api_write_clipboard_text(gf_handle owner, gf_string_view text) noexcept {
    return translate(&Registry::write_clipboard_text, &registry(), owner, text);
}
gf_result api_read_clipboard_text(gf_handle owner, char* buffer,
                                  std::uint64_t capacity,
                                  std::uint64_t* required_size,
                                  std::uint32_t* has_text) noexcept {
    return translate(&Registry::read_clipboard_text, &registry(), owner, buffer, capacity,
                                              required_size, has_text);
}
gf_result api_field_navigate(gf_handle control, std::uint64_t position_utf8,
                             std::int32_t direction,
                             std::uint64_t* result_utf8) noexcept {
    return translate(&Registry::field_navigate, &registry(), control, position_utf8, direction,
                                         result_utf8);
}
gf_result api_field_replace(gf_handle control, std::uint64_t start_utf8,
                            std::uint64_t length_utf8,
                            gf_string_view replacement,
                            gf_field_edit_result* result) noexcept {
    return translate(&Registry::field_replace, &registry(), control, start_utf8, length_utf8,
                                        replacement, result);
}
gf_result api_field_history(gf_handle control, std::int32_t direction,
                            gf_field_edit_result* result) noexcept {
    return translate(&Registry::field_history, &registry(), control, direction, result);
}
gf_result api_field_clear_history(gf_handle control) noexcept {
    return translate(&Registry::field_clear_history, &registry(), control);
}

} // namespace

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_acquire_v1(
    gf_handle control, std::uint32_t width, std::uint32_t height,
    std::uint64_t* endpoint, std::uintptr_t* compatibility_handle) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_acquire,
            control, width, height, endpoint, compatibility_handle);
#else
    static_cast<void>(control);
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(endpoint);
    static_cast<void>(compatibility_handle);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_configure_v1(
    std::uint64_t endpoint, std::uint32_t width, std::uint32_t height) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_configure, endpoint, width, height);
#else
    static_cast<void>(endpoint);
    static_cast<void>(width);
    static_cast<void>(height);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_touch_v1(
    std::uint64_t endpoint, std::uint32_t explicit_boundary) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_touch, endpoint, explicit_boundary);
#else
    static_cast<void>(endpoint);
    static_cast<void>(explicit_boundary);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_drain_v1(
    std::uint64_t endpoint) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_drain, endpoint);
#else
    static_cast<void>(endpoint);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_snapshot_v1(
    std::uint64_t endpoint, char* buffer, std::uint64_t capacity,
    std::uint64_t* required_size) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_snapshot,
            endpoint, buffer, capacity, required_size);
#else
    static_cast<void>(endpoint);
    static_cast<void>(buffer);
    static_cast<void>(capacity);
    static_cast<void>(required_size);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_release_v1(
    std::uint64_t endpoint) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_release, endpoint);
#else
    static_cast<void>(endpoint);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_submit_bgra_v1(
    std::uintptr_t compatibility_handle, std::uint32_t width, std::uint32_t height,
    std::uint64_t row_bytes, const void* pixels) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_submit_bgra,
            compatibility_handle, width, height, row_bytes, pixels);
#else
    static_cast<void>(compatibility_handle);
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(row_bytes);
    static_cast<void>(pixels);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_get_dc_v1(
    std::uintptr_t compatibility_handle, std::uintptr_t* device_context) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_get_dc, compatibility_handle, device_context);
#else
    static_cast<void>(compatibility_handle);
    static_cast<void>(device_context);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_release_dc_v1(
    std::uintptr_t compatibility_handle, std::uintptr_t device_context) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_release_dc, compatibility_handle, device_context);
#else
    static_cast<void>(compatibility_handle);
    static_cast<void>(device_context);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_publish_dc_v1(
    std::uintptr_t device_context) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_publish_dc, device_context);
#else
    static_cast<void>(device_context);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_begin_write_v1(
    std::uintptr_t device_context, std::uint64_t* write_lease) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_begin_write,
            device_context, write_lease);
#else
    static_cast<void>(device_context);
    static_cast<void>(write_lease);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_end_write_v1(
    std::uint64_t write_lease, std::uint32_t publish) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
    return translate(&api_windows_paint_endpoint_end_write, write_lease, publish);
#else
    static_cast<void>(write_lease);
    static_cast<void>(publish);
    return fail(GF_ERROR_UNSUPPORTED_VERSION,
                "Windows paint endpoints require the Win32 host");
#endif
}

extern "C" GF_C_API_EXPORT gf_result gf_get_api_v0(std::uint32_t requested_version,
                                                    gf_api_v0* table) {
    if (table == nullptr || (*table).struct_size < sizeof(std::uint32_t) * 2U) {
        return fail(GF_ERROR_INVALID_ARGUMENT,
                    "gf_get_api_v0 requires a size-prefixed output table");
    }
    if (requested_version != GF_ABI_VERSION_0_1 &&
        requested_version != GF_ABI_VERSION_0_2 &&
        requested_version != GF_ABI_VERSION_0_3 &&
        requested_version != GF_ABI_VERSION_0_4 &&
        requested_version != GF_ABI_VERSION_0_5 &&
        requested_version != GF_ABI_VERSION_0_6 &&
        requested_version != GF_ABI_VERSION_0_7 &&
        requested_version != GF_ABI_VERSION_0_8 &&
        requested_version != GF_ABI_VERSION_0_9 &&
        requested_version != GF_ABI_VERSION_0_10 &&
        requested_version != GF_ABI_VERSION_0_11 &&
        requested_version != GF_ABI_VERSION_0_12 &&
        requested_version != GF_ABI_VERSION_0_13 &&
        requested_version != GF_ABI_VERSION_0_14 &&
        requested_version != GF_ABI_VERSION_0_15 &&
        requested_version != GF_ABI_VERSION_0_16 &&
        requested_version != GF_ABI_VERSION_0_17 &&
        requested_version != GF_ABI_VERSION_0_18 &&
        requested_version != GF_ABI_VERSION_0_19 &&
        requested_version != GF_ABI_VERSION_0_20 &&
        requested_version != GF_ABI_VERSION_0_21 &&
        requested_version != GF_ABI_VERSION_0_22 &&
        requested_version != GF_ABI_VERSION_0_23 &&
        requested_version != GF_ABI_VERSION_0_24 &&
        requested_version != GF_ABI_VERSION_0_25 &&
        requested_version != GF_ABI_VERSION_0_26) {
        return fail(GF_ERROR_UNSUPPORTED_VERSION,
                    "requested GUI.Forms experimental ABI version is unsupported");
    }
    const std::uint32_t caller_size = (*table).struct_size;
    const gf_api_v0 implementation{
        sizeof(gf_api_v0),
        requested_version,
        &api_last_error,
        &api_control_create,
        &api_retain,
        &api_release,
        &api_dispose,
        &api_component_state,
        &api_stable_id,
        &api_set_visible,
        &api_get_visible,
        &api_set_bounds,
        &api_get_bounds,
        &api_add_child,
        &api_remove_child,
        &api_subscribe,
        &api_disconnect,
        &api_control_create_kind,
        &api_set_name,
        &api_get_name,
        &api_set_text,
        &api_get_text,
        &api_set_enabled,
        &api_get_enabled,
        &api_run_window,
        &api_last_host_trace,
        &api_subscribe_v2,
        &api_begin_invoke,
        &api_request_close,
        &api_callback_fault_count,
        &api_set_control_png,
        &api_set_child_index,
        &api_set_control_colors,
        &api_subscribe_pointer,
        &api_set_check_state,
        &api_get_check_state,
        &api_subscribe_key,
        &api_subscribe_text,
        &api_set_range,
        &api_get_range,
        &api_set_range_value,
        &api_get_range_value,
        &api_set_pointer_capture,
        &api_get_pointer_capture,
        &api_show_path_dialog,
        &api_last_dialog_path,
        &api_show_tooltip,
        &api_hide_tooltip,
        &api_set_field_selection,
        &api_set_field_edit_state,
        &api_field_position_from_point,
        &api_write_clipboard_text,
        &api_read_clipboard_text,
        &api_field_navigate,
        &api_field_replace,
        &api_field_history,
        &api_field_clear_history,
        &api_set_control_pixels,
        &api_get_control_absolute_bounds,
        &api_subscribe_key_preview,
        &api_set_cursor,
        &api_get_cursor,
        &api_set_auto_scroll_offset,
        &api_set_auto_scroll,
        &api_set_auto_scroll_margin,
        &api_set_auto_scroll_min_size,
        &api_set_auto_scroll_position,
        &api_get_scroll_state,
        &api_set_scroll_axis_state,
        &api_scroll_control_into_view,
        &api_suspend_layout,
        &api_resume_layout,
        &api_perform_control_layout,
        &api_get_layout_state,
        &api_property_grid_set_selected_controls,
        &api_property_grid_set_sort,
        &api_property_grid_get_sort,
        &api_property_grid_refresh,
        &api_property_object_define,
        &api_property_object_notify_changed,
        &api_property_grid_try_set_text,
        &api_property_grid_reset_property,
        &api_property_grid_activate_editor,
        &api_set_control_text_alignment,
        &api_set_button_appearance,
        &api_set_panel_border_style,
        &api_attach_overlay,
        &api_detach_overlay,
    };
    const std::size_t copy_size = std::min<std::size_t>(caller_size, sizeof(implementation));
    std::memcpy(table, &implementation, copy_size);
    return GF_OK;
}
