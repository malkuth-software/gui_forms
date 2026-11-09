#include "headless_host_services.hpp"
#include "../../../core/host/image/clipboard_image_wire.hpp"

#include <sstream>

namespace gui_forms::host {
namespace {

struct DialogOutcomeVisitor final {
    template <typename Result>
    [[nodiscard]] HostDialogOutcome operator()(const Result& value) const {
        return value.outcome;
    }
};

} // namespace

HeadlessHostServices::HeadlessHostServices()
    : HostServices(headless_capabilities()),
      monitors_{{"headless.primary",
                 {0.0, 0.0, 1920.0, 1080.0},
                 {0.0, 0.0, 1920.0, 1040.0},
                 1.0,
                 true}} {}

HostMonitorResult HeadlessHostServices::query_monitors_impl() {
    return {{}, monitors_};
}

HostServiceStatus HeadlessHostServices::set_cursor_impl(CursorKind) {
    return {};
}

HostServiceStatus HeadlessHostServices::set_pointer_capture_impl(
    bool, std::uint64_t) {
    return {};
}

HostClipboardTextResult HeadlessHostServices::read_clipboard_text_impl() {
    return {{}, clipboard_text_, clipboard_generation_, clipboard_has_text_};
}

HostServiceStatus HeadlessHostServices::write_clipboard_text_impl(
    std::string_view text_utf8) {
    clipboard_text_.assign(text_utf8);
    clipboard_image_.reset();
    clipboard_has_text_ = true;
    ++clipboard_generation_;
    return {};
}

void HeadlessHostServices::queue_dialog_result(HostDialogResult result) {
    dialog_results_.push_back(std::move(result));
}

void HeadlessHostServices::set_dialog_handler(DialogHandler handler) {
    dialog_handler_ = std::move(handler);
}

HostClipboardImageResult HeadlessHostServices::read_clipboard_image_impl() {
    if (!clipboard_image_.has_value()) return HostClipboardImageResult::nothing_read(clipboard_generation_);
    // A copy: the consumer's image never shares the clipboard's storage.
    return HostClipboardImageResult::image_read(*clipboard_image_, clipboard_generation_);
}

HostServiceStatus HeadlessHostServices::write_clipboard_image_impl(HostImageView image) {
    HostImage owned = gui_forms::detail::copy_host_image(image);
    clipboard_image_ = std::move(owned);
    clipboard_text_.clear();
    clipboard_has_text_ = false;
    ++clipboard_generation_;
    return {};
}

HostDialogResult HeadlessHostServices::show_dialog_impl(
    const HostDialogRequest& request) {
    HostDialogResult result;
    if (dialog_handler_) {
        result = dialog_handler_(request, *this);
    } else if (!dialog_results_.empty()) {
        result = std::move(dialog_results_.front());
        dialog_results_.pop_front();
    } else {
        result.status.error = HostServiceError::backend_failure;
        result.request_id = request.request_id;
    }
    std::ostringstream line;
    line << "dialog=" << host_dialog_kind_name(request.payload)
         << " request=" << request.request_id
         << " depth=" << snapshot().modal_depth
         << " error=" << host_service_error_name(result.status.error);
    if (result.status.accepted()) {
        line << " outcome=" << host_dialog_outcome_name(
            std::visit(DialogOutcomeVisitor{}, result.payload));
        if (const gui_forms::HostMessageDialogResult* message =
                std::get_if<HostMessageDialogResult>(&result.payload)) {
            line << " choice=" << host_dialog_choice_name((*message).choice);
        } else if (const gui_forms::HostPathDialogResult* paths =
                       std::get_if<HostPathDialogResult>(&result.payload)) {
            line << " paths=" << (*paths).paths.size();
        }
    }
    dialog_trace_ += line.str() + '\n';
    return result;
}

HostServiceStatus HeadlessHostServices::play_sound_cue_impl(
    const HostSoundCueRequest& request) {
    std::ostringstream line;
    line << "sound=" << host_sound_cue_name(request.cue)
         << " timestamp=" << request.timestamp_nanoseconds
         << " gain=" << request.gain << '\n';
    sound_trace_ += line.str();
    return {};
}

const HostTrayIconRequest* HeadlessHostServices::tray_icon(HostTrayIconId icon_id) const noexcept {
    for (const TrayIcon& icon : tray_icons_) {
        if (icon.id == icon_id) return &icon.request;
    }
    return nullptr;
}

void HeadlessHostServices::simulate_tray_icon_event(HostTrayIconEvent event) {
    publish_tray_icon_event(event);
}

void HeadlessHostServices::queue_tray_menu_choice(std::string chosen_id) {
    tray_menu_choices_.push_back(std::move(chosen_id));
}

HostServiceStatus HeadlessHostServices::add_tray_icon_impl(HostTrayIconId icon_id,
                                                           const HostTrayIconRequest& request) {
    tray_icons_.push_back({icon_id, request});
    std::ostringstream line;
    line << "tray add id=" << icon_id.value << " images=" << request.images.size()
         << " tooltip=" << request.tooltip << '\n';
    tray_trace_ += line.str();
    return {};
}

HostServiceStatus HeadlessHostServices::update_tray_icon_impl(HostTrayIconId icon_id,
                                                              const HostTrayIconRequest& request) {
    for (TrayIcon& icon : tray_icons_) {
        if (icon.id == icon_id) icon.request = request;
    }
    std::ostringstream line;
    line << "tray update id=" << icon_id.value << " tooltip=" << request.tooltip << '\n';
    tray_trace_ += line.str();
    return {};
}

void HeadlessHostServices::remove_tray_icon_impl(HostTrayIconId icon_id) noexcept {
    for (std::size_t index = 0; index < tray_icons_.size(); ++index) {
        if (tray_icons_[index].id == icon_id) {
            tray_icons_.erase(tray_icons_.begin() + static_cast<std::ptrdiff_t>(index));
            break;
        }
    }
    try {
        tray_trace_ += "tray remove id=" + std::to_string(icon_id.value) + '\n';
    } catch (...) {
    }
}

HostServiceStatus HeadlessHostServices::show_tray_notification_impl(
    HostTrayIconId icon_id, const HostTrayNotificationRequest& request) {
    std::ostringstream line;
    line << "tray notify id=" << icon_id.value << " title=" << request.title << " text=" << request.text << '\n';
    tray_trace_ += line.str();
    return {};
}

HostTrayMenuResult HeadlessHostServices::show_tray_menu_impl(HostTrayIconId icon_id,
                                                             const HostTrayMenuRequest& request) {
    HostTrayMenuResult result;
    std::ostringstream line;
    line << "tray menu id=" << icon_id.value << " items=" << request.items.size() << '\n';
    tray_trace_ += line.str();
    if (tray_menu_choices_.empty()) return result;
    result.chosen_id = std::move(tray_menu_choices_.front());
    tray_menu_choices_.pop_front();
    return result;
}

void HeadlessHostServices::shutdown_impl() noexcept {
    tray_menu_choices_.clear();
    clipboard_text_.clear();
    clipboard_has_text_ = false;
    clipboard_image_.reset();
    dialog_results_.clear();
    sound_trace_.clear();
}
void HeadlessHostServices::set_corner_report(WindowCornerPreference preference, WindowCornerReport report) noexcept {
    const std::size_t index = static_cast<std::size_t>(preference);
    if (index < corner_reports_.size()) corner_reports_[index] = report;
}

HostWindowCornersResult HeadlessHostServices::apply_window_corners_impl(WindowCornerPreference preference) {
    // The base class admitted only a known preference.
    native_corner_preference_ = preference;
    HostWindowCornersResult result{};
    result.report = corner_reports_[static_cast<std::size_t>(preference)];
    return result;
}

HostWindowChromeResult HeadlessHostServices::apply_window_chrome_impl(const WindowChrome& chrome,
                                                                     WindowChromeFields fields) {
    ++chrome_requests_;
    if (fields.contains(WindowChromeField::text)) native_chrome_.text = chrome.text;
    if (fields.contains(WindowChromeField::icon)) native_chrome_.icon = chrome.icon;
    if (fields.contains(WindowChromeField::form_border_style)) native_chrome_.form_border_style = chrome.form_border_style;
    if (fields.contains(WindowChromeField::minimize_box)) native_chrome_.minimize_box = chrome.minimize_box;
    if (fields.contains(WindowChromeField::maximize_box)) native_chrome_.maximize_box = chrome.maximize_box;
    if (fields.contains(WindowChromeField::control_box)) native_chrome_.control_box = chrome.control_box;
    if (fields.contains(WindowChromeField::window_state)) native_chrome_.window_state = chrome.window_state;
    if (fields.contains(WindowChromeField::top_most)) native_chrome_.top_most = chrome.top_most;
    if (fields.contains(WindowChromeField::opacity)) native_chrome_.opacity = chrome.opacity;
    if (fields.contains(WindowChromeField::start_position)) native_chrome_.start_position = chrome.start_position;
    if (fields.contains(WindowChromeField::show_in_taskbar)) native_chrome_.show_in_taskbar = chrome.show_in_taskbar;
    if (fields.contains(WindowChromeField::minimum_size)) native_chrome_.minimum_size = chrome.minimum_size;
    HostWindowChromeResult result{};
    result.applied = fields;
    return result;
}

HostServiceStatus HeadlessHostServices::apply_window_backdrop_impl(WindowBackdrop backdrop) {
    HostServiceStatus status{};
    // Opaque never fails (the HostServices contract).
    if (fail_next_backdrop_ && backdrop != WindowBackdrop::opaque) {
        fail_next_backdrop_ = false;
        status.error = HostServiceError::backend_failure;
    } else {
        native_backdrop_ = backdrop;
    }
    backdrop_trace_ += "backdrop=";
    backdrop_trace_ += window_backdrop_name(backdrop);
    backdrop_trace_ += status.accepted() ? " accepted=1\n" : " accepted=0\n";
    return status;
}

} // namespace gui_forms::host
