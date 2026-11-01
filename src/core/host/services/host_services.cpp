#include "gui_forms/host.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <new>
#include <sstream>
#include <type_traits>
#include <unordered_set>

namespace gui_forms {
namespace {
bool valid_size(Size size) noexcept {
    return std::isfinite(size.width) && std::isfinite(size.height) &&
           size.width > 0.0 && size.height > 0.0;
}

bool valid_scale(double scale) noexcept {
    return std::isfinite(scale) && scale > 0.0;
}

bool valid_monitor_rectangle(Rect rectangle) noexcept {
    return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
           valid_size({rectangle.width, rectangle.height});
}

bool valid_utf8(std::string_view text) noexcept {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = bytes[index];
        if (lead <= 0x7FU) {
            ++index;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t codepoint = 0;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            length = 2;
            codepoint = lead & 0x1FU;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            length = 3;
            codepoint = lead & 0x0FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            length = 4;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (index + length > text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            const unsigned char continuation = bytes[index + offset];
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (continuation & 0x3FU);
        }
        if ((length == 3 && codepoint < 0x800U) ||
            (length == 4 && codepoint < 0x10000U) ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
            codepoint > 0x10FFFFU) {
            return false;
        }
        index += length;
    }
    return true;
}

bool valid_monitor(const HostMonitor& monitor) noexcept {
    return !monitor.id.empty() && valid_monitor_rectangle(monitor.frame) &&
           valid_monitor_rectangle(monitor.work_area) &&
           valid_scale(monitor.scale);
}

class DragDataValidator final {
public:
    explicit DragDataValidator(std::size_t& total) noexcept : total_(&total) {}

    template <typename DataValue>
    bool operator()(const DataValue& data) const noexcept {
        using Data =
            std::remove_cv_t<std::remove_reference_t<DataValue>>;
        if constexpr (std::is_same_v<Data, DragTextData>) {
            return data.text_utf8.size() <= DragLimits::maximum_text_bytes &&
                   data.text_utf8.find('\0') == std::string::npos &&
                   valid_utf8(data.text_utf8) && add_size(data.text_utf8.size());
        } else if constexpr (std::is_same_v<Data, DragFileListData>) {
            if (data.paths_utf8.empty() ||
                data.paths_utf8.size() > DragLimits::maximum_paths) {
                return false;
            }
            for (const std::string& path : data.paths_utf8) {
                if (path.empty() ||
                    path.size() > DragLimits::maximum_path_bytes ||
                    path.find('\0') != std::string::npos ||
                    !valid_utf8(path) || !add_size(path.size())) {
                    return false;
                }
            }
            return true;
        } else if constexpr (std::is_same_v<Data, DragBinaryData>) {
            return !data.media_type.empty() &&
                   data.media_type.size() <=
                       DragLimits::maximum_media_type_bytes &&
                   data.media_type.find('\0') == std::string::npos &&
                   valid_utf8(data.media_type) && add_size(data.bytes.size());
        } else {
            return false;
        }
    }

private:
    bool add_size(std::size_t value) const noexcept {
        if (value > DragLimits::maximum_total_bytes - *total_) return false;
        *total_ += value;
        return true;
    }

    std::size_t* total_{};
};

bool valid_monitor_set(const std::vector<HostMonitor>& monitors) {
    if (monitors.empty() || monitors.size() > 64U) {
        return false;
    }
    std::unordered_set<std::string> identifiers;
    std::size_t primary_count = 0;
    for (const HostMonitor& monitor : monitors) {
        if (!valid_monitor(monitor) || !identifiers.insert(monitor.id).second) {
            return false;
        }
        primary_count += monitor.primary ? 1U : 0U;
    }
    return primary_count == 1U;
}

bool valid_drag_event(const DragEvent& event) noexcept {
    constexpr std::uint8_t known_effects =
        static_cast<std::uint8_t>(DragEffect::copy) |
        static_cast<std::uint8_t>(DragEffect::move) |
        static_cast<std::uint8_t>(DragEffect::link);
    const std::uint8_t effects = static_cast<std::uint8_t>(event.allowed_effects);
    if (event.session_id == 0 || !std::isfinite(event.position.x) ||
        !std::isfinite(event.position.y) || (effects & ~known_effects) != 0 ||
        event.accepted_effect != DragEffect::none ||
        event.items.size() > DragLimits::maximum_items) {
        return false;
    }
    if (event.action != DragAction::leave &&
        (event.items.empty() || event.allowed_effects == DragEffect::none)) {
        return false;
    }
    std::size_t total = 0;
    const DragDataValidator validator(total);
    for (const DragDataItem& item : event.items) {
        const bool valid = std::visit(validator, item);
        if (!valid) {
            return false;
        }
    }
    return true;
}

bool valid_dialog_string(std::string_view text) noexcept {
    return text.size() <= HostServices::maximum_dialog_text_bytes &&
           text.find('\0') == std::string_view::npos && valid_utf8(text);
}

bool valid_dialog_filter(const HostFileDialogFilter& filter) noexcept {
    if (filter.extensions.empty() ||
        filter.extensions.size() > HostServices::maximum_dialog_extensions ||
        !valid_dialog_string(filter.label)) {
        return false;
    }
    for (const std::string& extension : filter.extensions) {
        if (extension.empty() || !valid_dialog_string(extension)) return false;
    }
    return true;
}

bool choice_allowed(HostMessageButtons buttons, HostDialogChoice choice) noexcept {
    switch (buttons) {
    case HostMessageButtons::ok:
        return choice == HostDialogChoice::ok;
    case HostMessageButtons::ok_cancel:
        return choice == HostDialogChoice::ok || choice == HostDialogChoice::cancel;
    case HostMessageButtons::yes_no:
        return choice == HostDialogChoice::yes || choice == HostDialogChoice::no;
    case HostMessageButtons::yes_no_cancel:
        return choice == HostDialogChoice::yes || choice == HostDialogChoice::no ||
               choice == HostDialogChoice::cancel;
    case HostMessageButtons::retry_cancel:
        return choice == HostDialogChoice::retry || choice == HostDialogChoice::cancel;
    }
    return false;
}

// A file dialog's starting filter names one of its filters, or is zero
// when it has none.
bool valid_initial_filter(std::size_t initial_filter,
                          std::size_t filter_count) noexcept {
    const bool valid = filter_count == 0U ? initial_filter == 0U
                                          : initial_filter < filter_count;
    return valid;
}

struct DialogRequestValidator final {
    template <typename PayloadValue>
    bool operator()(const PayloadValue& payload) const noexcept {
        using Payload =
            std::remove_cv_t<std::remove_reference_t<PayloadValue>>;
        if constexpr (std::is_same_v<Payload, HostMessageDialogRequest>) {
            return valid_dialog_string(payload.title) &&
                   valid_dialog_string(payload.message) &&
                   choice_allowed(payload.buttons, payload.default_choice);
        } else if constexpr (
            std::is_same_v<Payload, HostOpenFileDialogRequest>) {
            return valid_dialog_string(payload.title) &&
                   valid_dialog_string(payload.initial_directory) &&
                   valid_dialog_string(payload.suggested_name) &&
                   payload.filters.size() <=
                       HostServices::maximum_dialog_filters &&
                   valid_initial_filter(payload.initial_filter,
                                        payload.filters.size()) &&
                   std::all_of(payload.filters.begin(), payload.filters.end(),
                               &valid_dialog_filter);
        } else if constexpr (
            std::is_same_v<Payload, HostSaveFileDialogRequest>) {
            return valid_dialog_string(payload.title) &&
                   valid_dialog_string(payload.initial_directory) &&
                   valid_dialog_string(payload.suggested_name) &&
                   valid_dialog_string(payload.default_extension) &&
                   payload.filters.size() <=
                       HostServices::maximum_dialog_filters &&
                   valid_initial_filter(payload.initial_filter,
                                        payload.filters.size()) &&
                   std::all_of(payload.filters.begin(), payload.filters.end(),
                               &valid_dialog_filter);
        } else if constexpr (
            std::is_same_v<Payload, HostFolderDialogRequest>) {
            return valid_dialog_string(payload.title) &&
                   valid_dialog_string(payload.initial_directory);
        } else if constexpr (
            std::is_same_v<Payload, HostColorDialogRequest>) {
            return valid_dialog_string(payload.title);
        } else {
            return false;
        }
    }
};

struct DialogOutcomeVisitor final {
    template <typename ResultValue>
    HostDialogOutcome operator()(const ResultValue& value) const noexcept {
        return value.outcome;
    }
};

struct DialogResultValidator final {
    const HostDialogResult* result{};

    template <typename RequestValue>
    bool operator()(const RequestValue& request) const noexcept {
        using Request =
            std::remove_cv_t<std::remove_reference_t<RequestValue>>;
        if constexpr (std::is_same_v<Request, HostMessageDialogRequest>) {
            const HostMessageDialogResult* value =
                std::get_if<HostMessageDialogResult>(&(*result).payload);
            if (value == nullptr) return false;
            return (*value).outcome == HostDialogOutcome::accepted ?
                choice_allowed(request.buttons, (*value).choice) :
                (*value).choice == HostDialogChoice::none ||
                    (*value).choice == HostDialogChoice::cancel;
        } else if constexpr (
            std::is_same_v<Request, HostOpenFileDialogRequest> ||
            std::is_same_v<Request, HostSaveFileDialogRequest> ||
            std::is_same_v<Request, HostFolderDialogRequest>) {
            const HostPathDialogResult* value =
                std::get_if<HostPathDialogResult>(&(*result).payload);
            if (value == nullptr ||
                (*value).paths.size() > HostServices::maximum_dialog_paths) {
                return false;
            }
            if ((*value).outcome == HostDialogOutcome::cancelled) {
                return (*value).paths.empty() && !(*value).chosen_filter;
            }
            bool multiple = false;
            if constexpr (
                std::is_same_v<Request, HostOpenFileDialogRequest>) {
                multiple = request.allow_multiple;
            }
            // A chosen filter names one of the request's filters; a folder
            // dialog has none.
            if constexpr (
                std::is_same_v<Request, HostFolderDialogRequest>) {
                if ((*value).chosen_filter) return false;
            } else {
                if ((*value).chosen_filter &&
                    *(*value).chosen_filter >= request.filters.size()) {
                    return false;
                }
            }
            if ((*value).paths.empty() ||
                (!multiple && (*value).paths.size() != 1U)) {
                return false;
            }
            return std::all_of((*value).paths.begin(), (*value).paths.end(),
                               &valid_dialog_string);
        } else if constexpr (
            std::is_same_v<Request, HostColorDialogRequest>) {
            return std::holds_alternative<HostColorDialogResult>(
                (*result).payload);
        } else {
            return false;
        }
    }
};

bool valid_dialog_request(const HostDialogRequest& request) noexcept {
    if (request.request_id == 0 || !valid_dialog_string(request.owner_id)) {
        return false;
    }
    return std::visit(DialogRequestValidator{}, request.payload);
}

HostDialogOutcome dialog_outcome(const HostDialogResultPayload& payload) noexcept {
    return std::visit(DialogOutcomeVisitor{}, payload);
}

bool valid_dialog_result(const HostDialogRequest& request,
                         const HostDialogResult& result) noexcept {
    if (!result.status.accepted() || result.request_id != request.request_id) {
        return false;
    }
    return std::visit(DialogResultValidator{&result}, request.payload);
}

} // namespace
HostServices::HostServices(HostCapabilities capabilities)
    : ui_thread_(std::this_thread::get_id()) {
    snapshot_.capabilities = std::move(capabilities);
}

HostServiceStatus HostServices::apply_window_backdrop(WindowBackdrop backdrop) {
    HostCapability needed = HostCapability::none;
    switch (backdrop) {
    case WindowBackdrop::opaque:
        needed = HostCapability::none;
        break;
    case WindowBackdrop::transparent:
        needed = HostCapability::transparent_windows;
        break;
    case WindowBackdrop::window_material:
    case WindowBackdrop::transient_material:
        needed = HostCapability::window_backdrops;
        break;
    default: {
        // A value outside the enumeration names nothing to apply.
        HostServiceStatus refused = reject(HostServiceError::invalid_argument);
        return refused;
    }
    }
    HostServiceStatus status = validate_request(needed);
    if (!status.accepted()) return status;
    status = apply_window_backdrop_impl(backdrop);
    return status;
}

HostServiceStatus HostServices::apply_window_backdrop_impl(WindowBackdrop backdrop) {
    HostServiceStatus status{};
    if (backdrop != WindowBackdrop::opaque) status.error = HostServiceError::unsupported;
    return status;
}

HostServiceStatus HostServices::validate_request(HostCapability capability) noexcept {
    HostServiceStatus status;
    if (std::this_thread::get_id() != ui_thread_) {
        status.error = HostServiceError::wrong_thread;
        return status;
    } else if (snapshot_.shutdown) {
        status.error = HostServiceError::after_shutdown;
    } else if (!snapshot_.capabilities.supports(capability)) {
        status.error = HostServiceError::unsupported;
    }
    if (!status.accepted()) {
        ++snapshot_.rejected_requests;
    }
    return status;
}

HostMonitorResult HostServices::query_monitors() {
    HostMonitorResult result;
    result.status = validate_request(HostCapability::monitor_geometry);
    if (!result.status.accepted()) {
        return result;
    }
    ++snapshot_.monitor_queries;
    result = query_monitors_impl();
    if (result.status.accepted() && !valid_monitor_set(result.monitors)) {
        result.status.error = HostServiceError::backend_failure;
        result.monitors.clear();
    }
    if (!result.status.accepted()) {
        ++snapshot_.rejected_requests;
    }
    return result;
}

HostServiceStatus HostServices::set_cursor(CursorKind cursor) {
    HostServiceStatus status = validate_request(HostCapability::cursor);
    if (!status.accepted()) {
        return status;
    }
    status = set_cursor_impl(cursor);
    if (status.accepted()) {
        if (!cursor_applied_ || active_cursor_images_ || snapshot_.cursor != cursor)
            ++snapshot_.cursor_updates;
        cursor_applied_ = true;
        active_cursor_images_.reset();
        snapshot_.cursor = cursor;
    } else {
        ++snapshot_.rejected_requests;
    }
    return status;
}

HostServiceStatus HostServices::set_custom_cursor_impl(const CursorImagesPtr&, const CursorImage&) {
    return {HostServiceError::unsupported};
}

HostServiceStatus HostServices::set_custom_cursor(CursorImagesPtr images,
                                                 double scale, CursorKind fallback) {
    HostServiceStatus status = validate_request(HostCapability::cursor);
    if (!status.accepted()) return status;
    if (!images) return set_cursor(fallback);
    if (!std::isfinite(scale) || scale <= 0) scale = 1;
    if (prepared_cursor_images_ != images || prepared_request_scale_ != scale) {
        try {
            prepared_cursor_ = (*images).rasterize(scale);
            prepared_cursor_images_ = images;
            prepared_request_scale_ = scale;
        } catch (const std::exception&) { return set_cursor(fallback); }
    }
    const CursorImage& selected = prepared_cursor_;
    if (failed_cursor_images_ == images && failed_cursor_scale_ == selected.scale)
        return set_cursor(fallback);
    try { status = set_custom_cursor_impl(images, selected); }
    catch (const std::exception&) { status = {HostServiceError::backend_failure}; }
    if (!status.accepted()) {
        failed_cursor_images_ = images;
        failed_cursor_scale_ = selected.scale;
        return set_cursor(fallback);
    }
    if (active_cursor_images_ != images || active_cursor_scale_ != selected.scale)
        ++snapshot_.cursor_updates;
    active_cursor_images_ = images;
    active_cursor_scale_ = selected.scale;
    cursor_applied_ = true;
    snapshot_.cursor = fallback;
    return status;
}

HostServiceStatus HostServices::set_pointer_capture(bool captured,
                                                    std::uint64_t pointer_id) {
    HostServiceStatus status = validate_request(HostCapability::pointer_capture);
    if (!status.accepted()) {
        return status;
    }
    if (captured && pointer_id == 0) {
        status.error = HostServiceError::invalid_argument;
        ++snapshot_.rejected_requests;
        return status;
    }
    if (snapshot_.pointer_captured == captured &&
        (!captured || snapshot_.captured_pointer_id == pointer_id)) {
        return status;
    }
    status = set_pointer_capture_impl(captured, captured ? pointer_id : 0);
    if (status.accepted()) {
        snapshot_.pointer_captured = captured;
        snapshot_.captured_pointer_id = captured ? pointer_id : 0;
        ++snapshot_.pointer_capture_updates;
    } else {
        ++snapshot_.rejected_requests;
    }
    return status;
}

HostClipboardTextResult HostServices::read_clipboard_text() {
    HostClipboardTextResult result;
    result.status = validate_request(HostCapability::clipboard);
    if (!result.status.accepted()) {
        return result;
    }
    ++snapshot_.clipboard_reads;
    result = read_clipboard_text_impl();
    if (result.status.accepted() && result.has_text &&
        (result.text_utf8.size() > maximum_clipboard_text_bytes ||
         !valid_utf8(result.text_utf8))) {
        result.status.error = result.text_utf8.size() > maximum_clipboard_text_bytes
                                  ? HostServiceError::too_large
                                  : HostServiceError::invalid_utf8;
        result.text_utf8.clear();
        result.has_text = false;
    }
    if (result.status.accepted()) {
        snapshot_.clipboard_generation = result.generation;
    } else {
        ++snapshot_.rejected_requests;
    }
    return result;
}

HostServiceStatus HostServices::write_clipboard_text(std::string_view text_utf8) {
    HostServiceStatus status = validate_request(HostCapability::clipboard);
    if (!status.accepted()) {
        return status;
    }
    if (text_utf8.size() > maximum_clipboard_text_bytes) {
        status.error = HostServiceError::too_large;
    } else if (!valid_utf8(text_utf8)) {
        status.error = HostServiceError::invalid_utf8;
    }
    if (!status.accepted()) {
        ++snapshot_.rejected_requests;
        return status;
    }
    status = write_clipboard_text_impl(text_utf8);
    if (status.accepted()) {
        ++snapshot_.clipboard_writes;
        ++snapshot_.clipboard_generation;
    } else {
        ++snapshot_.rejected_requests;
    }
    return status;
}

HostClipboardImageResult HostServices::read_clipboard_image() {
    const HostServiceStatus request = validate_request(HostCapability::clipboard_images);
    if (!request.accepted()) return HostClipboardImageResult::failed(request.error);
    ++snapshot_.clipboard_reads;
    // A backend's failure, an image that fails validation, or exhausted
    // storage: the consumer gets the error and never the pixels.
    HostServiceError error = HostServiceError::none;
    try {
        HostClipboardImageResult read = read_clipboard_image_impl();
        error = read.status().error;
        const HostImage* const image = read.image();
        if (error == HostServiceError::none && image != nullptr) {
            const HostImageError invalid = validate_host_image((*image).view());
            if (invalid != HostImageError::none) {
                error = invalid == HostImageError::too_large
                    ? HostServiceError::too_large : HostServiceError::backend_failure;
            }
        }
        if (error == HostServiceError::none) {
            snapshot_.clipboard_generation = read.generation();
            return read;
        }
    } catch (const std::bad_alloc&) {
        error = HostServiceError::backend_failure;
    }
    ++snapshot_.rejected_requests;
    return HostClipboardImageResult::failed(error);
}

HostClipboardFilesResult HostServices::read_clipboard_files() {
    HostClipboardFilesResult result;
    result.status = validate_request(HostCapability::clipboard);
    if (!result.status.accepted()) return result;
    ++snapshot_.clipboard_reads;
    try {
        result = read_clipboard_files_impl();
        if (result.status.accepted()) {
            std::size_t total = 0;
            if (result.paths_utf8.size() > maximum_dialog_paths) {
                result.status.error = HostServiceError::too_large;
            }
            for (const std::string& path : result.paths_utf8) {
                if (path.size() > maximum_dialog_text_bytes ||
                    path.size() > maximum_clipboard_text_bytes - total) {
                    result.status.error = HostServiceError::too_large;
                    break;
                }
                if (path.empty() || path.find('\0') != std::string::npos || !valid_utf8(path)) {
                    result.status.error = HostServiceError::invalid_utf8;
                    break;
                }
                total += path.size();
            }
        }
    } catch (const std::bad_alloc&) {
        result.status.error = HostServiceError::backend_failure;
    }
    if (!result.status.accepted()) {
        result.paths_utf8.clear();
        ++snapshot_.rejected_requests;
    } else {
        snapshot_.clipboard_generation = result.generation;
    }
    return result;
}

HostClipboardFilesResult HostServices::read_clipboard_files_impl() {
    return {};
}

namespace {
// Whether an adapter's accepted candidates keep the contract: at most
// `maximum` of them, each non-empty UTF-8 without NUL.
bool valid_auto_complete_candidates(const std::vector<std::string>& candidates, std::size_t maximum) {
    if (candidates.size() > maximum) return false;
    for (const std::string& candidate : candidates) {
        const bool valid = !candidate.empty() && candidate.find('\0') == std::string::npos &&
            valid_utf8(candidate);
        if (!valid) return false;
    }
    return true;
}
} // namespace

HostAutoCompleteResult HostServices::query_auto_complete(const HostAutoCompleteRequest& request) {
    HostAutoCompleteResult result;
    // No capability flag names these sources yet; the adapter's own answer
    // (unsupported by default) is the capability report.
    if (std::this_thread::get_id() != ui_thread_) {
        result.status.error = HostServiceError::wrong_thread;
    } else if (snapshot_.shutdown) {
        result.status.error = HostServiceError::after_shutdown;
    } else if (!valid_utf8(request.prefix_utf8)) {
        result.status.error = HostServiceError::invalid_utf8;
    }
    if (!result.status.accepted()) {
        ++snapshot_.rejected_requests;
        return result;
    }
    try {
        result = query_auto_complete_impl(request);
        if (result.status.accepted() &&
            !valid_auto_complete_candidates(result.candidates_utf8, request.maximum_candidates)) {
            result.status.error = HostServiceError::backend_failure;
        }
    } catch (const std::bad_alloc&) {
        result.status.error = HostServiceError::backend_failure;
    }
    if (!result.status.accepted()) {
        result.candidates_utf8.clear();
        ++snapshot_.rejected_requests;
    }
    return result;
}

HostAutoCompleteResult HostServices::query_auto_complete_impl(const HostAutoCompleteRequest&) {
    HostAutoCompleteResult result;
    result.status.error = HostServiceError::unsupported;
    return result;
}

HostServiceStatus HostServices::write_clipboard_image(HostImageView image) {
    HostServiceStatus status = validate_request(HostCapability::clipboard_images);
    if (!status.accepted()) return status;
    const HostImageError error = validate_host_image(image);
    if (error != HostImageError::none) {
        ++snapshot_.rejected_requests;
        return {error == HostImageError::too_large
            ? HostServiceError::too_large : HostServiceError::invalid_argument};
    }
    try {
        status = write_clipboard_image_impl(image);
    } catch (const std::bad_alloc&) {
        status.error = HostServiceError::backend_failure;
    }
    if (status.accepted()) {
        ++snapshot_.clipboard_writes;
        ++snapshot_.clipboard_generation;
    } else {
        ++snapshot_.rejected_requests;
    }
    return status;
}

HostClipboardImageResult HostServices::read_clipboard_image_impl() {
    return HostClipboardImageResult::failed(HostServiceError::unsupported);
}

HostServiceStatus HostServices::write_clipboard_image_impl(HostImageView) {
    return {HostServiceError::unsupported};
}

HostDialogResult HostServices::show_dialog(const HostDialogRequest& request) {
    HostDialogResult result;
    result.request_id = request.request_id;
    result.status = validate_request(HostCapability::dialogs);
    if (!result.status.accepted()) {
        return result;
    }
    if (!valid_dialog_request(request) ||
        std::find(modal_stack_.begin(), modal_stack_.end(), request.request_id) !=
            modal_stack_.end()) {
        result.status.error = HostServiceError::invalid_argument;
        ++snapshot_.rejected_requests;
        return result;
    }
    if (modal_stack_.size() >= maximum_nested_modal_depth) {
        result.status.error = HostServiceError::modal_limit;
        ++snapshot_.rejected_requests;
        return result;
    }

    ++snapshot_.dialog_requests;
    revoke_cursor_interaction(CursorError::denied);
    modal_stack_.push_back(request.request_id);
    snapshot_.modal_depth = static_cast<std::uint32_t>(modal_stack_.size());
    snapshot_.maximum_modal_depth =
        std::max(snapshot_.maximum_modal_depth, snapshot_.modal_depth);
    try {
        modal_changed_.emit(HostModalTransition{request.request_id,
                                                snapshot_.modal_depth, true});
        if (!snapshot_.shutdown) {
            result = show_dialog_impl(request);
        }
    } catch (...) {
        result = {{HostServiceError::backend_failure}, request.request_id,
                  HostMessageDialogResult{}};
    }

    modal_stack_.pop_back();
    snapshot_.modal_depth = static_cast<std::uint32_t>(modal_stack_.size());
    try {
        modal_changed_.emit(HostModalTransition{request.request_id,
                                                snapshot_.modal_depth, false});
    } catch (...) {
        result = {{HostServiceError::backend_failure}, request.request_id,
                  HostMessageDialogResult{}};
    }

    if (snapshot_.shutdown) {
        result.status.error = HostServiceError::after_shutdown;
        result.request_id = request.request_id;
    } else if (result.status.accepted() && !valid_dialog_result(request, result)) {
        result.status.error = HostServiceError::backend_failure;
        result.request_id = request.request_id;
    }
    if (result.status.accepted()) {
        ++snapshot_.dialog_completions;
        snapshot_.dialog_cancellations +=
            dialog_outcome(result.payload) == HostDialogOutcome::cancelled ? 1U : 0U;
    } else {
        ++snapshot_.rejected_requests;
    }
    return result;
}

HostServiceStatus HostServices::play_sound_cue(
    const HostSoundCueRequest& request) {
    HostServiceStatus status = validate_request(HostCapability::sound_cues);
    if (!status.accepted()) return status;
    ++snapshot_.sound_requests;
    if (!std::isfinite(request.gain) || request.gain < 0.0 ||
        request.gain > 1.0 || request.timestamp_nanoseconds == 0U ||
        (last_sound_timestamp_ != 0U &&
         request.timestamp_nanoseconds < last_sound_timestamp_)) {
        status.error = HostServiceError::invalid_argument;
        ++snapshot_.rejected_requests;
        return status;
    }
    const bool coalesced = last_sound_cue_ == request.cue &&
        last_sound_timestamp_ != 0U &&
        snapshot_.sound_coalescing_window_nanoseconds != 0U &&
        request.timestamp_nanoseconds - last_sound_timestamp_ <=
            snapshot_.sound_coalescing_window_nanoseconds;
    last_sound_cue_ = request.cue;
    last_sound_timestamp_ = request.timestamp_nanoseconds;
    if (request.gain == 0.0) {
        ++snapshot_.sound_muted;
        return status;
    }
    if (coalesced) {
        ++snapshot_.sound_coalesced;
        return status;
    }
    try {
        status = play_sound_cue_impl(request);
    } catch (...) {
        status.error = HostServiceError::backend_failure;
    }
    if (status.accepted()) ++snapshot_.sound_playbacks;
    else ++snapshot_.rejected_requests;
    return status;
}

HostServiceStatus HostServices::set_sound_cue_coalescing_window(
    std::chrono::nanoseconds window) {
    HostServiceStatus status = validate_request(HostCapability::sound_cues);
    if (!status.accepted()) return status;
    constexpr std::chrono::seconds maximum = std::chrono::seconds(5);
    if (window < std::chrono::nanoseconds::zero() || window > maximum) {
        status.error = HostServiceError::invalid_argument;
        ++snapshot_.rejected_requests;
        return status;
    }
    snapshot_.sound_coalescing_window_nanoseconds =
        static_cast<std::uint64_t>(window.count());
    last_sound_cue_.reset();
    last_sound_timestamp_ = 0U;
    return status;
}

// -- notification-area icons -------------------------------------------------

HostServiceStatus HostServices::reject(HostServiceError error) noexcept {
    ++snapshot_.rejected_requests;
    const HostServiceStatus status{error};
    return status;
}

HostServiceStatus HostServices::validate_tray_icon_request(const HostTrayIconRequest& request) const noexcept {
    if (request.images.empty() || request.images.size() > maximum_tray_icon_images) {
        return {HostServiceError::invalid_argument};
    }
    for (const HostImage& image : request.images) {
        if (validate_host_image(image.view()) != HostImageError::none) return {HostServiceError::invalid_argument};
        if (image.width > maximum_tray_icon_side || image.height > maximum_tray_icon_side) {
            return {HostServiceError::too_large};
        }
    }
    if (request.tooltip.size() > maximum_tray_tooltip_bytes) return {HostServiceError::too_large};
    if (!valid_utf8(request.tooltip)) return {HostServiceError::invalid_utf8};
    return {};
}

bool HostServices::tray_icon_live(HostTrayIconId icon_id) const noexcept {
    const bool live = std::find(tray_icons_.begin(), tray_icons_.end(), icon_id) != tray_icons_.end();
    return live;
}

HostTrayIconResult HostServices::add_tray_icon(const HostTrayIconRequest& request) {
    HostTrayIconResult result;
    result.status = validate_request(HostCapability::notification_area);
    if (!result.status.accepted()) return result;
    const HostServiceStatus validated = validate_tray_icon_request(request);
    if (!validated.accepted()) {
        result.status = reject(validated.error);
        return result;
    }
    if (tray_icons_.size() >= maximum_tray_icons) {
        result.status = reject(HostServiceError::too_large);
        return result;
    }
    const HostTrayIconId icon_id{next_tray_icon_id_};
    HostServiceStatus added{HostServiceError::backend_failure};
    try {
        added = add_tray_icon_impl(icon_id, request);
    } catch (const std::exception&) {
        added = {HostServiceError::backend_failure};
    }
    if (!added.accepted()) {
        result.status = reject(added.error);
        return result;
    }
    ++next_tray_icon_id_;
    tray_icons_.push_back(icon_id);
    snapshot_.tray_icons = tray_icons_.size();
    result.icon_id = icon_id;
    return result;
}

HostServiceStatus HostServices::update_tray_icon(HostTrayIconId icon_id, const HostTrayIconRequest& request) {
    const HostServiceStatus status = validate_request(HostCapability::notification_area);
    if (!status.accepted()) return status;
    if (!tray_icon_live(icon_id)) return reject(HostServiceError::invalid_argument);
    const HostServiceStatus validated = validate_tray_icon_request(request);
    if (!validated.accepted()) return reject(validated.error);
    HostServiceStatus updated{HostServiceError::backend_failure};
    try {
        updated = update_tray_icon_impl(icon_id, request);
    } catch (const std::exception&) {
        updated = {HostServiceError::backend_failure};
    }
    if (!updated.accepted()) return reject(updated.error);
    ++snapshot_.tray_icon_updates;
    return updated;
}

HostServiceStatus HostServices::remove_tray_icon(HostTrayIconId icon_id) {
    const HostServiceStatus status = validate_request(HostCapability::notification_area);
    if (!status.accepted()) return status;
    const std::vector<HostTrayIconId>::iterator found = std::find(tray_icons_.begin(), tray_icons_.end(), icon_id);
    if (found == tray_icons_.end()) return reject(HostServiceError::invalid_argument);
    tray_icons_.erase(found);
    snapshot_.tray_icons = tray_icons_.size();
    remove_tray_icon_impl(icon_id);
    return status;
}

HostServiceStatus HostServices::show_tray_notification(HostTrayIconId icon_id,
                                                       const HostTrayNotificationRequest& request) {
    const HostServiceStatus status = validate_request(HostCapability::notification_area_messages);
    if (!status.accepted()) return status;
    if (!tray_icon_live(icon_id)) return reject(HostServiceError::invalid_argument);
    if (request.title.size() > maximum_tray_title_bytes || request.text.size() > maximum_tray_text_bytes) {
        return reject(HostServiceError::too_large);
    }
    if (!valid_utf8(request.title) || !valid_utf8(request.text)) return reject(HostServiceError::invalid_utf8);
    if (request.text.empty()) return reject(HostServiceError::invalid_argument);
    HostServiceStatus shown{HostServiceError::backend_failure};
    try {
        shown = show_tray_notification_impl(icon_id, request);
    } catch (const std::exception&) {
        shown = {HostServiceError::backend_failure};
    }
    if (!shown.accepted()) return reject(shown.error);
    ++snapshot_.tray_notifications;
    return shown;
}

HostTrayMenuResult HostServices::show_tray_menu(HostTrayIconId icon_id, const HostTrayMenuRequest& request) {
    HostTrayMenuResult result;
    result.status = validate_request(HostCapability::notification_area_menus);
    if (!result.status.accepted()) return result;
    if (!tray_icon_live(icon_id) || request.items.empty()) {
        result.status = reject(HostServiceError::invalid_argument);
        return result;
    }
    if (request.items.size() > maximum_tray_menu_items) {
        result.status = reject(HostServiceError::too_large);
        return result;
    }
    for (const HostTrayMenuItem& item : request.items) {
        if (!valid_utf8(item.id) || !valid_utf8(item.text)) {
            result.status = reject(HostServiceError::invalid_utf8);
            return result;
        }
        if (!item.separator && item.id.empty()) {
            result.status = reject(HostServiceError::invalid_argument);
            return result;
        }
    }
    try {
        result = show_tray_menu_impl(icon_id, request);
    } catch (const std::exception&) {
        result = {};
        result.status = {HostServiceError::backend_failure};
    }
    if (!result.status.accepted()) {
        ++snapshot_.rejected_requests;
        result.chosen_id.clear();
        return result;
    }
    ++snapshot_.tray_menus;
    return result;
}

void HostServices::publish_tray_icon_event(const HostTrayIconEvent& event) {
    if (snapshot_.shutdown || std::this_thread::get_id() != ui_thread_ || !tray_icon_live(event.icon_id)) return;
    ++snapshot_.tray_events;
    tray_icon_events_.emit(event);
}

HostServiceStatus HostServices::add_tray_icon_impl(HostTrayIconId, const HostTrayIconRequest&) {
    return {HostServiceError::unsupported};
}

HostServiceStatus HostServices::update_tray_icon_impl(HostTrayIconId, const HostTrayIconRequest&) {
    return {HostServiceError::unsupported};
}

void HostServices::remove_tray_icon_impl(HostTrayIconId) noexcept {}

HostServiceStatus HostServices::show_tray_notification_impl(HostTrayIconId, const HostTrayNotificationRequest&) {
    return {HostServiceError::unsupported};
}

HostTrayMenuResult HostServices::show_tray_menu_impl(HostTrayIconId, const HostTrayMenuRequest&) {
    HostTrayMenuResult result;
    result.status = {HostServiceError::unsupported};
    return result;
}

void HostServices::shutdown() noexcept {
    if (snapshot_.shutdown || std::this_thread::get_id() != ui_thread_) {
        return;
    }
    revoke_cursor_interaction(CursorError::closing);
    // No icon outlives the services that showed it.
    for (const HostTrayIconId icon_id : tray_icons_) remove_tray_icon_impl(icon_id);
    tray_icons_.clear();
    snapshot_.tray_icons = 0U;
    snapshot_.shutdown = true;
    shutdown_impl();
    active_cursor_images_.reset();
    failed_cursor_images_.reset();
    prepared_cursor_images_.reset();
    prepared_cursor_ = {};
}

HostServicesSnapshot HostServices::snapshot() const {
    return snapshot_;
}

} // namespace gui_forms
