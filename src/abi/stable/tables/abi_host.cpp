// GF_TABLE_HOST: the platform host's capability report, and the cursor,
// clipboard and dialogs of the running window. See gf_abi.h.

#include "abi_tables.hpp"
#include "abi_records.hpp"

#include "../engine/abi_engine.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/application.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity capabilities_slot{GF_TABLE_HOST, 0U};
constexpr SlotIdentity platform_name_slot{GF_TABLE_HOST, 1U};
constexpr SlotIdentity set_cursor_slot{GF_TABLE_HOST, 2U};
constexpr SlotIdentity read_clipboard_text_slot{GF_TABLE_HOST, 3U};
constexpr SlotIdentity write_clipboard_text_slot{GF_TABLE_HOST, 4U};
constexpr SlotIdentity show_dialog_slot{GF_TABLE_HOST, 5U};
constexpr SlotIdentity dialog_path_at_slot{GF_TABLE_HOST, 6U};

// The 1.0 size of gf_host_capabilities: a caller passes at least this much
// and receives the fields that fit (1.1 added transient_surfaces, then
// window_corner_shapes and window_band_maximum_width).
constexpr std::uint32_t host_capabilities_size_1_0 = 16U;
static_assert(sizeof(gf_host_capabilities) == 32U);
static_assert(sizeof(gf_file_filter) == 32U);
static_assert(sizeof(gf_dialog_result) == 32U);
// The 1.0 size of gf_dialog_request: a caller passes at least this much.
// The 1.2 size adds owner; a 1.0-sized record names no owner.
constexpr std::uint32_t dialog_request_size_1_0 = 136U;
constexpr std::uint32_t dialog_request_size_1_2 = 144U;
static_assert(sizeof(gf_dialog_request) == dialog_request_size_1_2);

// One capability in both spellings. The ABI's bits are its own constants;
// this table, not an equality of values, connects them to the host's.
struct CapabilityBit final {
    HostCapability host = HostCapability::none;
    gf_host_capability abi = 0U;
};

constexpr std::array<CapabilityBit, 35> capability_bits{{
    {HostCapability::lifecycle, GF_HOST_CAPABILITY_LIFECYCLE},
    {HostCapability::scale_notifications, GF_HOST_CAPABILITY_SCALE_NOTIFICATIONS},
    {HostCapability::monitor_geometry, GF_HOST_CAPABILITY_MONITOR_GEOMETRY},
    {HostCapability::occlusion, GF_HOST_CAPABILITY_OCCLUSION},
    {HostCapability::scheduled_wake, GF_HOST_CAPABILITY_SCHEDULED_WAKE},
    {HostCapability::pointer_input, GF_HOST_CAPABILITY_POINTER_INPUT},
    {HostCapability::keyboard_input, GF_HOST_CAPABILITY_KEYBOARD_INPUT},
    {HostCapability::text_composition, GF_HOST_CAPABILITY_TEXT_COMPOSITION},
    {HostCapability::pointer_capture, GF_HOST_CAPABILITY_POINTER_CAPTURE},
    {HostCapability::cursor, GF_HOST_CAPABILITY_CURSOR},
    {HostCapability::clipboard, GF_HOST_CAPABILITY_CLIPBOARD},
    {HostCapability::typed_drag_destination, GF_HOST_CAPABILITY_TYPED_DRAG_DESTINATION},
    {HostCapability::dialogs, GF_HOST_CAPABILITY_DIALOGS},
    {HostCapability::menus, GF_HOST_CAPABILITY_MENUS},
    {HostCapability::font_discovery, GF_HOST_CAPABILITY_FONT_DISCOVERY},
    {HostCapability::accessibility, GF_HOST_CAPABILITY_ACCESSIBILITY},
    {HostCapability::typed_drag_source, GF_HOST_CAPABILITY_TYPED_DRAG_SOURCE},
    {HostCapability::sound_cues, GF_HOST_CAPABILITY_SOUND_CUES},
    {HostCapability::clipboard_images, GF_HOST_CAPABILITY_CLIPBOARD_IMAGES},
    {HostCapability::frame_capture, GF_HOST_CAPABILITY_FRAME_CAPTURE},
    {HostCapability::notification_area, GF_HOST_CAPABILITY_NOTIFICATION_AREA},
    {HostCapability::notification_area_messages, GF_HOST_CAPABILITY_NOTIFICATION_AREA_MESSAGES},
    {HostCapability::notification_area_menus, GF_HOST_CAPABILITY_NOTIFICATION_AREA_MENUS},
    {HostCapability::runtime_windows, GF_HOST_CAPABILITY_RUNTIME_WINDOWS},
    {HostCapability::window_frames, GF_HOST_CAPABILITY_WINDOW_FRAMES},
    {HostCapability::screen_pointer, GF_HOST_CAPABILITY_SCREEN_POINTER},
    {HostCapability::window_at_point, GF_HOST_CAPABILITY_WINDOW_AT_POINT},
    {HostCapability::window_ordering, GF_HOST_CAPABILITY_WINDOW_ORDERING},
    {HostCapability::transparent_windows, GF_HOST_CAPABILITY_TRANSPARENT_WINDOWS},
    {HostCapability::window_backdrops, GF_HOST_CAPABILITY_WINDOW_BACKDROPS},
    {HostCapability::compositor_notifications, GF_HOST_CAPABILITY_COMPOSITOR_NOTIFICATIONS},
    {HostCapability::presentation_preferences, GF_HOST_CAPABILITY_PRESENTATION_PREFERENCES},
    {HostCapability::transient_surfaces, GF_HOST_CAPABILITY_TRANSIENT_SURFACES},
    {HostCapability::window_corners, GF_HOST_CAPABILITY_WINDOW_CORNERS},
    {HostCapability::window_state_reports, GF_HOST_CAPABILITY_WINDOW_STATE_REPORTS},
}};

// The corner shapes a host with window_corners may give: square on request
// and rounded by default (Windows 11). A window's corners at a moment
// follow its state (Window::band_snapshot().corners).
gf_window_corner_shape abi_window_corner_shapes(HostCapability available) noexcept {
    if (!has_capability(available, HostCapability::window_corners)) return 0U;
    const gf_window_corner_shape shapes = GF_WINDOW_CORNER_SHAPE_SQUARE | GF_WINDOW_CORNER_SHAPE_ROUNDED;
    return shapes;
}

// The kinds and drawing of the host's transient surfaces (1.1): every kind
// with transient_surfaces, translucent, and also opaque where the desktop
// may decide so at run time.
gf_transient_surface_kind abi_transient_surfaces(HostCapability available) noexcept {
    if (!has_capability(available, HostCapability::transient_surfaces)) return 0U;
    gf_transient_surface_kind kinds = GF_TRANSIENT_SURFACE_TOOLTIP | GF_TRANSIENT_SURFACE_SPLASHSCREEN |
                                      GF_TRANSIENT_SURFACE_POPUP | GF_TRANSIENT_SURFACE_TRANSLUCENT;
    if (has_capability(available, HostCapability::opaque_transient_surfaces)) {
        kinds = kinds | GF_TRANSIENT_SURFACE_OPAQUE;
    }
    return kinds;
}

gf_host_capability abi_capabilities(HostCapability available) noexcept {
    gf_host_capability bits = 0U;
    for (const CapabilityBit& bit : capability_bits) {
        if (has_capability(available, bit.host)) {
            bits = bits | bit.abi;
        }
    }
    return bits;
}

// The status of a host service's refusal.
gf_status service_status(HostServiceStatus status, SlotIdentity slot) noexcept {
    switch (status.error) {
    case HostServiceError::none:
        return GF_STATUS_OK;
    case HostServiceError::unsupported:
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot, "the host does not offer this service");
    case HostServiceError::wrong_thread:
        return fail(GF_STATUS_WRONG_THREAD, slot, "the host refused a call from this thread");
    case HostServiceError::invalid_argument:
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the host refused the request");
    case HostServiceError::invalid_utf8:
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the host refused text that is not UTF-8");
    case HostServiceError::too_large:
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "the request exceeds the host's bounds");
    case HostServiceError::modal_limit:
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "too many modal dialogs are open");
    case HostServiceError::backend_failure:
        return fail(GF_STATUS_INTERNAL, slot, "the host failed");
    case HostServiceError::after_shutdown:
        return fail(GF_STATUS_INVALID_STATE, slot, "the host has shut down");
    }
    return fail(GF_STATUS_INTERNAL, slot, "the host returned an unknown result");
}

// The running window's services, once the UI thread is entered.
gf_status running_services(SlotIdentity slot, HostServices*& services) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Window* window = engine().running_window();
    HostServices* found = window != nullptr ? (*window).host_services() : nullptr;
    if (found == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, slot, "no window is running");
    }
    services = found;
    return GF_STATUS_OK;
}

// The services of the window a dialog request names as its owner (N32):
// the run's window for zero or the run's root, an owned window for its
// root. Once the UI thread is entered.
gf_status owner_services(gf_object owner, SlotIdentity slot, HostServices*& services) {
    if (owner == 0U) {
        const gf_status running = running_services(slot, services);
        return running;
    }
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    gf_object run_root = 0U;
    ApplicationWindowHandle running;
    const gf_status run = registry().current_run(slot, run_root, running);
    if (run != GF_STATUS_OK) {
        return run;
    }
    Window* window = engine().window_over(owner, run_root);
    HostServices* found = window != nullptr ? (*window).host_services() : nullptr;
    if (found == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, slot, "the owner is not the root of an open window");
    }
    services = found;
    return GF_STATUS_OK;
}

gf_status GF_CALL host_capabilities(gf_host_capabilities* record) noexcept {
    try {
        const ShadowFrameScope shadow_frame(capabilities_slot.table_id, capabilities_slot.slot_index);
        if (record == nullptr || (*record).struct_size < host_capabilities_size_1_0) {
            return fail(GF_STATUS_INVALID_ARGUMENT, capabilities_slot,
                        "the output record is null or smaller than its 1.0 size");
        }
        const HostCapabilities report = Application::capabilities();
        gf_host_capabilities complete{};
        complete.struct_size = sizeof(gf_host_capabilities);
        complete.protocol_version = report.protocol_version;
        complete.available = abi_capabilities(report.available);
        complete.transient_surfaces = abi_transient_surfaces(report.available);
        complete.window_corner_shapes = abi_window_corner_shapes(report.available);
        complete.window_band_maximum_width = static_cast<std::uint32_t>(maximum_band_width);
#if defined(GF_ABI_HAS_WINDOWS_PAINT_ENDPOINT)
        // A capability of this library, not of the host: its Windows build
        // has the paint endpoint table.
        complete.available = complete.available | GF_HOST_CAPABILITY_WINDOWS_PAINT_ENDPOINT;
#endif
        // The fields that fit the caller's record: a 1.0 record ends before
        // transient_surfaces, an early 1.1 record before window_corner_shapes.
        const std::size_t declared = (*record).struct_size;
        const std::size_t writable = declared < sizeof(gf_host_capabilities) ? declared : sizeof(gf_host_capabilities);
        const std::size_t skipped = sizeof(std::uint32_t);
        std::memcpy(reinterpret_cast<unsigned char*>(record) + skipped,
                    reinterpret_cast<const unsigned char*>(&complete) + skipped, writable - skipped);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(capabilities_slot);
    }
}

gf_status GF_CALL host_platform_name(char* buffer, std::uint64_t capacity, std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(platform_name_slot.table_id, platform_name_slot.slot_index);
        const HostCapabilities report = Application::capabilities();
        const gf_status written = write_text(report.platform, buffer, capacity, required, platform_name_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(platform_name_slot);
    }
}

constexpr std::array<CursorKind, 10> cursor_kinds{
    CursorKind::arrow,
    CursorKind::text,
    CursorKind::hand,
    CursorKind::crosshair,
    CursorKind::resize_horizontal,
    CursorKind::resize_vertical,
    CursorKind::wait,
    CursorKind::forbidden,
    CursorKind::resize_diagonal_down,
    CursorKind::resize_diagonal_up,
};
static_assert(cursor_kinds.size() == GF_POINTER_CURSOR_RESIZE_DIAGONAL_UP + 1U);

gf_status GF_CALL host_set_cursor(gf_pointer_cursor cursor) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_cursor_slot.table_id, set_cursor_slot.slot_index);
        if (cursor >= cursor_kinds.size()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_cursor_slot, "unknown cursor");
        }
        HostServices* services = nullptr;
        const gf_status found = running_services(set_cursor_slot, services);
        if (found != GF_STATUS_OK) {
            return found;
        }
        const HostServiceStatus status = (*services).set_cursor(cursor_kinds[cursor]);
        return service_status(status, set_cursor_slot);
    } catch (...) {
        return fail_with_current_exception(set_cursor_slot);
    }
}

gf_status GF_CALL host_read_clipboard_text(char* buffer, std::uint64_t capacity, std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(read_clipboard_text_slot.table_id, read_clipboard_text_slot.slot_index);
        if (required == nullptr || (buffer == nullptr && capacity != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, read_clipboard_text_slot,
                        "a text output needs a size output, and a buffer unless its capacity is zero");
        }
        HostServices* services = nullptr;
        const gf_status found = running_services(read_clipboard_text_slot, services);
        if (found != GF_STATUS_OK) {
            return found;
        }
        const HostClipboardTextResult result = (*services).read_clipboard_text();
        const gf_status status = service_status(result.status, read_clipboard_text_slot);
        if (status != GF_STATUS_OK) {
            return status;
        }
        if (!result.has_text) {
            return fail(GF_STATUS_NOT_FOUND, read_clipboard_text_slot, "the clipboard holds no text");
        }
        const gf_status written = write_text(result.text_utf8, buffer, capacity, required, read_clipboard_text_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(read_clipboard_text_slot);
    }
}

gf_status GF_CALL host_write_clipboard_text(gf_utf8_view text) noexcept {
    try {
        const ShadowFrameScope shadow_frame(write_clipboard_text_slot.table_id, write_clipboard_text_slot.slot_index);
        std::string_view checked;
        const gf_status read = read_text(text, write_clipboard_text_slot, checked);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (checked.size() > HostServices::maximum_clipboard_text_bytes) {
            return fail(GF_STATUS_LIMIT_EXCEEDED, write_clipboard_text_slot, "clipboard text is at most 16 MiB");
        }
        HostServices* services = nullptr;
        const gf_status found = running_services(write_clipboard_text_slot, services);
        if (found != GF_STATUS_OK) {
            return found;
        }
        const HostServiceStatus status = (*services).write_clipboard_text(checked);
        return service_status(status, write_clipboard_text_slot);
    } catch (...) {
        return fail_with_current_exception(write_clipboard_text_slot);
    }
}

// The flags each kind reads.
constexpr gf_dialog_flags open_file_flags = GF_DIALOG_FLAGS_ALLOW_MULTIPLE;
constexpr gf_dialog_flags save_file_flags = GF_DIALOG_FLAGS_CONFIRM_OVERWRITE;
constexpr gf_dialog_flags color_flags = GF_DIALOG_FLAGS_ALLOW_ALPHA;

// Copies one optional text of a request.
gf_status read_request_text(gf_utf8_view view, SlotIdentity slot, std::string& text) {
    std::string_view checked;
    const gf_status read = read_text(view, slot, checked);
    if (read != GF_STATUS_OK) {
        return read;
    }
    text = std::string(checked);
    return GF_STATUS_OK;
}

// The filters of a file dialog, checked whole, within the host's bounds.
gf_status read_filters(const gf_dialog_request& request, SlotIdentity slot,
                       std::vector<HostFileDialogFilter>& filters) {
    if (request.filters == nullptr && request.filter_count != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a dialog with filters needs its filter array");
    }
    if (request.filter_count > HostServices::maximum_dialog_filters) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a dialog has at most 64 filters");
    }
    const std::span<const gf_file_filter> given(request.filters, static_cast<std::size_t>(request.filter_count));
    std::vector<HostFileDialogFilter> made;
    made.reserve(given.size());
    for (const gf_file_filter& filter : given) {
        if (filter.extensions == nullptr && filter.extension_count != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a filter with extensions needs its extension array");
        }
        if (filter.extension_count > HostServices::maximum_dialog_extensions) {
            return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a filter has at most 64 extensions");
        }
        HostFileDialogFilter entry;
        const gf_status label = read_request_text(filter.label, slot, entry.label);
        if (label != GF_STATUS_OK) {
            return label;
        }
        const std::span<const gf_utf8_view> extensions(filter.extensions,
                                                       static_cast<std::size_t>(filter.extension_count));
        entry.extensions.reserve(extensions.size());
        for (const gf_utf8_view extension : extensions) {
            std::string copied;
            const gf_status read = read_request_text(extension, slot, copied);
            if (read != GF_STATUS_OK) {
                return read;
            }
            entry.extensions.push_back(std::move(copied));
        }
        made.push_back(std::move(entry));
    }
    if (!made.empty() && request.initial_filter >= made.size()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "initial_filter names no filter");
    }
    if (made.empty() && request.initial_filter != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "initial_filter is zero when there are no filters");
    }
    filters = std::move(made);
    return GF_STATUS_OK;
}

bool view_empty(gf_utf8_view view) noexcept {
    return view.size == 0U;
}

// Whether the fields a kind does not read are empty or zero.
bool unread_fields_empty(const gf_dialog_request& request) noexcept {
    const bool message_fields = request.kind == GF_DIALOG_KIND_MESSAGE;
    const bool file_fields = request.kind == GF_DIALOG_KIND_OPEN_FILE || request.kind == GF_DIALOG_KIND_SAVE_FILE;
    const bool directory_field = file_fields || request.kind == GF_DIALOG_KIND_FOLDER;
    const bool colour_fields = request.kind == GF_DIALOG_KIND_COLOR;
    if (!message_fields && (!view_empty(request.message) || request.buttons != 0U || request.icon != 0U ||
                            request.default_choice != 0U)) {
        return false;
    }
    if (!directory_field && !view_empty(request.initial_directory)) {
        return false;
    }
    if (!file_fields && (!view_empty(request.suggested_name) || request.filter_count != 0U ||
                         request.initial_filter != 0U)) {
        return false;
    }
    if (request.kind != GF_DIALOG_KIND_SAVE_FILE && !view_empty(request.default_extension)) {
        return false;
    }
    if (!colour_fields && request.initial_rgba != 0U) {
        return false;
    }
    return true;
}

gf_dialog_flags flags_read_by(gf_dialog_kind kind) noexcept {
    if (kind == GF_DIALOG_KIND_OPEN_FILE) return open_file_flags;
    if (kind == GF_DIALOG_KIND_SAVE_FILE) return save_file_flags;
    if (kind == GF_DIALOG_KIND_COLOR) return color_flags;
    return GF_DIALOG_FLAGS_NONE;
}

constexpr std::array<HostMessageButtons, 5> message_buttons{
    HostMessageButtons::ok, HostMessageButtons::ok_cancel, HostMessageButtons::yes_no,
    HostMessageButtons::yes_no_cancel, HostMessageButtons::retry_cancel,
};
constexpr std::array<HostMessageIcon, 5> message_icons{
    HostMessageIcon::none, HostMessageIcon::information, HostMessageIcon::warning,
    HostMessageIcon::error, HostMessageIcon::question,
};
constexpr std::array<HostDialogChoice, 6> dialog_choices{
    HostDialogChoice::none, HostDialogChoice::ok, HostDialogChoice::cancel,
    HostDialogChoice::yes, HostDialogChoice::no, HostDialogChoice::retry,
};

gf_dialog_choice abi_choice(HostDialogChoice choice) noexcept {
    for (std::size_t index = 0U; index < dialog_choices.size(); ++index) {
        if (dialog_choices[index] == choice) {
            return static_cast<gf_dialog_choice>(index);
        }
    }
    return GF_DIALOG_CHOICE_NONE;
}

// The host request for a caller's request, checked whole before anything
// is shown.
gf_status read_dialog_request(const gf_dialog_request* request, SlotIdentity slot, HostDialogRequest& made) {
    if (request == nullptr || (*request).struct_size < dialog_request_size_1_0 || (*request).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a dialog request needs a record of at least the 1.0 size with reserved zero");
    }
    const gf_dialog_request& given = *request;
    if (given.kind > GF_DIALOG_KIND_COLOR) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "unknown dialog kind");
    }
    if ((given.flags & ~flags_read_by(given.kind)) != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a flag this kind of dialog does not read");
    }
    if (!unread_fields_empty(given)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a field this kind of dialog does not read is set");
    }
    std::string title;
    const gf_status titled = read_request_text(given.title, slot, title);
    if (titled != GF_STATUS_OK) {
        return titled;
    }
    std::string directory;
    const gf_status directed = read_request_text(given.initial_directory, slot, directory);
    if (directed != GF_STATUS_OK) {
        return directed;
    }
    std::string suggested;
    const gf_status suggested_read = read_request_text(given.suggested_name, slot, suggested);
    if (suggested_read != GF_STATUS_OK) {
        return suggested_read;
    }
    HostDialogRequest built;
    if (given.kind == GF_DIALOG_KIND_MESSAGE) {
        if (given.buttons >= message_buttons.size() || given.icon >= message_icons.size() ||
            given.default_choice >= dialog_choices.size()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "unknown buttons, icon or default choice");
        }
        HostMessageDialogRequest message;
        message.title = std::move(title);
        const gf_status text = read_request_text(given.message, slot, message.message);
        if (text != GF_STATUS_OK) {
            return text;
        }
        message.buttons = message_buttons[given.buttons];
        message.icon = message_icons[given.icon];
        message.default_choice = dialog_choices[given.default_choice];
        built.payload = std::move(message);
    } else if (given.kind == GF_DIALOG_KIND_OPEN_FILE) {
        HostOpenFileDialogRequest open;
        const gf_status filtered = read_filters(given, slot, open.filters);
        if (filtered != GF_STATUS_OK) {
            return filtered;
        }
        open.title = std::move(title);
        open.initial_directory = std::move(directory);
        open.suggested_name = std::move(suggested);
        open.allow_multiple = (given.flags & GF_DIALOG_FLAGS_ALLOW_MULTIPLE) != 0U;
        open.initial_filter = static_cast<std::size_t>(given.initial_filter);
        built.payload = std::move(open);
    } else if (given.kind == GF_DIALOG_KIND_SAVE_FILE) {
        HostSaveFileDialogRequest save;
        const gf_status filtered = read_filters(given, slot, save.filters);
        if (filtered != GF_STATUS_OK) {
            return filtered;
        }
        const gf_status extension = read_request_text(given.default_extension, slot, save.default_extension);
        if (extension != GF_STATUS_OK) {
            return extension;
        }
        save.title = std::move(title);
        save.initial_directory = std::move(directory);
        save.suggested_name = std::move(suggested);
        save.confirm_overwrite = (given.flags & GF_DIALOG_FLAGS_CONFIRM_OVERWRITE) != 0U;
        save.initial_filter = static_cast<std::size_t>(given.initial_filter);
        built.payload = std::move(save);
    } else if (given.kind == GF_DIALOG_KIND_FOLDER) {
        HostFolderDialogRequest folder;
        folder.title = std::move(title);
        folder.initial_directory = std::move(directory);
        built.payload = std::move(folder);
    } else {
        HostColorDialogRequest colour;
        colour.title = std::move(title);
        colour.initial_rgba = given.initial_rgba;
        colour.allow_alpha = (given.flags & GF_DIALOG_FLAGS_ALLOW_ALPHA) != 0U;
        built.payload = std::move(colour);
    }
    made = std::move(built);
    return GF_STATUS_OK;
}

gf_dialog_outcome abi_outcome(HostDialogOutcome outcome) noexcept {
    const gf_dialog_outcome result =
        outcome == HostDialogOutcome::accepted ? GF_DIALOG_OUTCOME_ACCEPTED : GF_DIALOG_OUTCOME_CANCELLED;
    return result;
}

gf_status GF_CALL host_show_dialog(const gf_dialog_request* request, gf_dialog_result* result) noexcept {
    try {
        const ShadowFrameScope shadow_frame(show_dialog_slot.table_id, show_dialog_slot.slot_index);
        const gf_status checked = check_record(result, show_dialog_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        HostDialogRequest made;
        const gf_status read = read_dialog_request(request, show_dialog_slot, made);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_object owner = (*request).struct_size >= dialog_request_size_1_2 ? (*request).owner : 0U;
        HostServices* services = nullptr;
        const gf_status found = owner_services(owner, show_dialog_slot, services);
        if (found != GF_STATUS_OK) {
            return found;
        }
        made.request_id = engine().next_dialog_request();
        // Modal: the window's events and posted callbacks run until it closes.
        const HostDialogResult shown = (*services).show_dialog(made);
        const gf_status status = service_status(shown.status, show_dialog_slot);
        if (status != GF_STATUS_OK) {
            return status;
        }
        gf_dialog_result complete{};
        complete.struct_size = sizeof(gf_dialog_result);
        complete.chosen_filter = GF_DIALOG_NO_FILTER;
        std::vector<std::string> paths;
        if (std::holds_alternative<HostMessageDialogResult>(shown.payload)) {
            const HostMessageDialogResult& message = std::get<HostMessageDialogResult>(shown.payload);
            complete.outcome = abi_outcome(message.outcome);
            complete.choice = abi_choice(message.choice);
        } else if (std::holds_alternative<HostPathDialogResult>(shown.payload)) {
            const HostPathDialogResult& chosen = std::get<HostPathDialogResult>(shown.payload);
            complete.outcome = abi_outcome(chosen.outcome);
            paths = chosen.paths;
            complete.path_count = paths.size();
            if (chosen.chosen_filter.has_value()) {
                complete.chosen_filter = *chosen.chosen_filter;
            }
        } else {
            const HostColorDialogResult& colour = std::get<HostColorDialogResult>(shown.payload);
            complete.outcome = abi_outcome(colour.outcome);
            complete.rgba = colour.rgba;
        }
        engine().dialog_paths() = std::move(paths);
        write_record(complete, result);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(show_dialog_slot);
    }
}

gf_status GF_CALL host_dialog_path_at(std::uint64_t index, char* buffer, std::uint64_t capacity,
                                      std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(dialog_path_at_slot.table_id, dialog_path_at_slot.slot_index);
        const gf_status entered = registry().enter_ui(dialog_path_at_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::vector<std::string>& paths = engine().dialog_paths();
        if (index >= paths.size()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, dialog_path_at_slot, "the last dialog chose no path at that index");
        }
        const gf_status written =
            write_text(paths[static_cast<std::size_t>(index)], buffer, capacity, required, dialog_path_at_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(dialog_path_at_slot);
    }
}

constexpr gf_host_table table{
    .struct_size = sizeof(gf_host_table),
    .abi_minor = GF_ABI_MINOR,
    .capabilities = &host_capabilities,
    .platform_name = &host_platform_name,
    .set_cursor = &host_set_cursor,
    .read_clipboard_text = &host_read_clipboard_text,
    .write_clipboard_text = &host_write_clipboard_text,
    .show_dialog = &host_show_dialog,
    .dialog_path_at = &host_dialog_path_at,
};

} // namespace

const gf_host_table& host_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
