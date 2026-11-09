#pragma once

#include "../capabilities/headless_capabilities.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace gui_forms::host {

class HeadlessHostServices final : public HostServices {
public:
    using DialogHandler = std::function<HostDialogResult(
        const HostDialogRequest&, HeadlessHostServices&)>;

    HeadlessHostServices();
    ~HeadlessHostServices() override { shutdown(); }

    void queue_dialog_result(HostDialogResult result);
    void set_dialog_handler(DialogHandler handler);
    [[nodiscard]] const std::string& dialog_trace() const noexcept {
        return dialog_trace_;
    }
    [[nodiscard]] const std::string& sound_trace() const noexcept {
        return sound_trace_;
    }
    // Tray icons, kept as requested so tests can read what an application
    // asked to show, and driven by simulated user actions.
    [[nodiscard]] const HostTrayIconRequest* tray_icon(HostTrayIconId icon_id) const noexcept;
    [[nodiscard]] const std::string& tray_trace() const noexcept { return tray_trace_; }
    // Raises a tray event as a user action would; dropped for icons that are
    // not live.
    void simulate_tray_icon_event(HostTrayIconEvent event);
    // The choice the next show_tray_menu returns; empty dismisses the menu.
    void queue_tray_menu_choice(std::string chosen_id);
    // The simulated native window's backdrop: what apply_window_backdrop
    // last applied (opaque until then). Every application, accepted or
    // failed, appends "backdrop=<name> accepted=<0|1>" and a newline to
    // backdrop_trace().
    [[nodiscard]] WindowBackdrop native_backdrop() const noexcept { return native_backdrop_; }
    [[nodiscard]] const std::string& backdrop_trace() const noexcept { return backdrop_trace_; }
    // The next non-opaque application fails with backend_failure and leaves
    // the native backdrop unchanged, as a native call that failed would.
    void fail_next_backdrop() noexcept { fail_next_backdrop_ = true; }
    // The simulated platform's corners: apply_window_corners records the
    // preference (native_corner_preference, `system` until then) and
    // answers with the report set for it here (unknown corners until one
    // is set), as a platform that rounds, squares or cannot comply would.
    void set_corner_report(WindowCornerPreference preference, WindowCornerReport report) noexcept;
    [[nodiscard]] WindowCornerPreference native_corner_preference() const noexcept {
        return native_corner_preference_;
    }

protected:
    [[nodiscard]] HostMonitorResult query_monitors_impl() override;
    [[nodiscard]] HostServiceStatus set_cursor_impl(CursorKind cursor) override;
    [[nodiscard]] HostServiceStatus set_pointer_capture_impl(
        bool captured, std::uint64_t pointer_id) override;
    [[nodiscard]] HostClipboardTextResult read_clipboard_text_impl() override;
    [[nodiscard]] HostServiceStatus write_clipboard_text_impl(
        std::string_view text_utf8) override;
    [[nodiscard]] HostClipboardImageResult read_clipboard_image_impl() override;
    [[nodiscard]] HostServiceStatus write_clipboard_image_impl(HostImageView image) override;
    [[nodiscard]] HostDialogResult show_dialog_impl(
        const HostDialogRequest& request) override;
    [[nodiscard]] HostServiceStatus play_sound_cue_impl(
        const HostSoundCueRequest& request) override;
    void shutdown_impl() noexcept override;
    [[nodiscard]] HostServiceStatus add_tray_icon_impl(
        HostTrayIconId icon_id, const HostTrayIconRequest& request) override;
    [[nodiscard]] HostServiceStatus update_tray_icon_impl(
        HostTrayIconId icon_id, const HostTrayIconRequest& request) override;
    void remove_tray_icon_impl(HostTrayIconId icon_id) noexcept override;
    [[nodiscard]] HostServiceStatus show_tray_notification_impl(
        HostTrayIconId icon_id, const HostTrayNotificationRequest& request) override;
    [[nodiscard]] HostTrayMenuResult show_tray_menu_impl(
        HostTrayIconId icon_id, const HostTrayMenuRequest& request) override;
    [[nodiscard]] HostServiceStatus apply_window_backdrop_impl(WindowBackdrop backdrop) override;
    [[nodiscard]] HostWindowCornersResult apply_window_corners_impl(WindowCornerPreference preference) override;

private:
    struct TrayIcon final {
        HostTrayIconId id{};
        HostTrayIconRequest request;
    };
    std::vector<HostMonitor> monitors_;
    // The clipboard's image; absent while it holds none.
    std::optional<HostImage> clipboard_image_;
    std::string clipboard_text_;
    std::uint64_t clipboard_generation_{};
    bool clipboard_has_text_{};
    std::deque<HostDialogResult> dialog_results_;
    DialogHandler dialog_handler_;
    std::string dialog_trace_;
    std::string sound_trace_;
    std::vector<TrayIcon> tray_icons_;
    std::deque<std::string> tray_menu_choices_;
    std::string tray_trace_;
    WindowBackdrop native_backdrop_{WindowBackdrop::opaque};
    std::string backdrop_trace_;
    bool fail_next_backdrop_{};
    WindowCornerPreference native_corner_preference_{WindowCornerPreference::system};
    // Indexed by WindowCornerPreference.
    std::array<WindowCornerReport, 3> corner_reports_{};
};

} // namespace gui_forms::host
