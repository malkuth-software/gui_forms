#pragma once
#include "gui_forms/types/cursor_image/cursor_image.hpp"

#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/host/cursor_interaction/cursor_interaction.hpp"

#include <chrono>
#include <optional>
#include <thread>

namespace gui_forms {

// Portable host-to-platform service seam. The base class owns thread,
// capability, UTF-8, size, accounting, modal-depth, and shutdown policy so
// adapters cannot silently disagree on those contracts.
class HostServices {
public:
    static constexpr std::size_t maximum_clipboard_text_bytes = 16U * 1024U * 1024U;
    static constexpr std::size_t maximum_dialog_text_bytes = 64U * 1024U;
    static constexpr std::size_t maximum_dialog_filters = 64U;
    static constexpr std::size_t maximum_dialog_extensions = 64U;
    static constexpr std::size_t maximum_dialog_paths = 64U;
    static constexpr std::uint32_t maximum_nested_modal_depth = 8U;
    static constexpr std::size_t maximum_tray_icons = 16U;
    static constexpr std::size_t maximum_tray_icon_images = 8U;
    static constexpr std::uint32_t maximum_tray_icon_side = 256U;
    static constexpr std::size_t maximum_tray_tooltip_bytes = 127U;
    static constexpr std::size_t maximum_tray_title_bytes = 63U;
    static constexpr std::size_t maximum_tray_text_bytes = 255U;
    static constexpr std::size_t maximum_tray_menu_items = 64U;

    explicit HostServices(HostCapabilities capabilities);
    virtual ~HostServices();
    HostServices(const HostServices&) = delete;
    HostServices& operator=(const HostServices&) = delete;

    [[nodiscard]] HostMonitorResult query_monitors();
    [[nodiscard]] CursorCapabilities cursor_capabilities() const noexcept;
    [[nodiscard]] CursorLeaseResult begin_cursor_hidden(Window& window);
    [[nodiscard]] CursorStatus warp_cursor(Window& window, CursorMetrics metrics, Point target);
    // Host lifecycle hooks: revoke before focus/capture loss, detach or close.
    // Cleanup failures remain visible on outstanding leases and this snapshot.
    void revoke_cursor_interaction(CursorError cause = CursorError::revoked) noexcept;
    [[nodiscard]] CursorLeaseSnapshot cursor_interaction_snapshot() const noexcept;
    [[nodiscard]] bool cursor_hidden() const noexcept;
    [[nodiscard]] HostServiceStatus set_cursor(CursorKind cursor);
    // Native failure/unsupported images immediately install fallback. Success
    // then means a usable pointer; it does not promise custom-image support.
    [[nodiscard]] HostServiceStatus set_custom_cursor(CursorImagesPtr images,
        double device_scale, CursorKind fallback = CursorKind::arrow);
    [[nodiscard]] HostServiceStatus set_pointer_capture(
        bool captured, std::uint64_t pointer_id = 1);
    [[nodiscard]] HostClipboardTextResult read_clipboard_text();
    [[nodiscard]] HostServiceStatus write_clipboard_text(
        std::string_view text_utf8);
    /// Reads an owned premultiplied BGRA8 image on the creating UI thread.
    /// Successful absence is distinguished by an absent image.
    [[nodiscard]] HostClipboardImageResult read_clipboard_image();
    /// Reads local file references without opening or decoding their contents.
    [[nodiscard]] HostClipboardFilesResult read_clipboard_files();
    // Completion candidates from a host-owned source (TextBox and ComboBox
    // auto-complete). No host adapter supplies any yet: the base
    // implementation reports unsupported. Accepted results are bounded to
    // the request's maximum and to valid, non-empty UTF-8 strings without
    // NUL; anything else is reported as backend_failure with no candidates.
    [[nodiscard]] HostAutoCompleteResult query_auto_complete(
        const HostAutoCompleteRequest& request);
    /// Copies an image to the native clipboard before returning. Does not retain
    /// the borrowed view. Replaces clipboard contents on successful publication.
    [[nodiscard]] HostServiceStatus write_clipboard_image(HostImageView image);
    [[nodiscard]] HostDialogResult show_dialog(
        const HostDialogRequest& request);
    [[nodiscard]] HostServiceStatus play_sound_cue(
        const HostSoundCueRequest& request);
    [[nodiscard]] std::chrono::nanoseconds
    sound_cue_coalescing_window() const noexcept {
        return std::chrono::nanoseconds(
            snapshot_.sound_coalescing_window_nanoseconds);
    }
    // A zero interval disables burst coalescing. The upper bound prevents a
    // theme or consumer setting from suppressing a semantic cue indefinitely.
    [[nodiscard]] HostServiceStatus set_sound_cue_coalescing_window(
        std::chrono::nanoseconds window);
    // Notification-area icons (capability notification_area). The base class
    // assigns identities, validates images and text, bounds the icon count
    // and removes every live icon at shutdown, so adapters agree on those
    // rules. An unknown or removed identity is an invalid_argument.
    [[nodiscard]] HostTrayIconResult add_tray_icon(const HostTrayIconRequest& request);
    [[nodiscard]] HostServiceStatus update_tray_icon(HostTrayIconId icon_id,
                                                     const HostTrayIconRequest& request);
    [[nodiscard]] HostServiceStatus remove_tray_icon(HostTrayIconId icon_id);
    // Capability notification_area_messages.
    [[nodiscard]] HostServiceStatus show_tray_notification(
        HostTrayIconId icon_id, const HostTrayNotificationRequest& request);
    // Capability notification_area_menus. Shows a native menu at the icon and
    // returns when it closes, as show_dialog does.
    [[nodiscard]] HostTrayMenuResult show_tray_menu(HostTrayIconId icon_id,
                                                    const HostTrayMenuRequest& request);
    // Raised on the UI thread for each user action on a live tray icon.
    [[nodiscard]] Event<const HostTrayIconEvent&>& tray_icon_events() noexcept {
        return tray_icon_events_;
    }
    [[nodiscard]] bool tray_icon_live(HostTrayIconId icon_id) const noexcept;
    // Raised on the UI thread when the user asks for help in an open message
    // dialog (its Help button, or F1 where the host's dialog takes it), with
    // the dialog's request identity; the dialog stays open. Handlers run
    // inside the host's modal loop: they may open other windows but cannot
    // close the dialog.
    [[nodiscard]] Event<const HostDialogHelpEvent&>& dialog_help_requested() noexcept {
        return dialog_help_requested_;
    }
    // Adapters report a help request in an open dialog here. Dropped (false)
    // off the UI thread, after shutdown, and for a request that is not an
    // open dialog's.
    bool publish_dialog_help(const HostDialogHelpEvent& event);
    // Applies a window's backdrop grant to the native window (stage T5).
    // The window calls it only when its granted backdrop changes. `opaque`
    // needs no capability, and an adapter must not fail it; `transparent`
    // needs transparent_windows, and the two materials window_backdrops. A
    // refused or failed request leaves the native window as it was.
    [[nodiscard]] HostServiceStatus apply_window_backdrop(WindowBackdrop backdrop);
    void shutdown() noexcept;

    [[nodiscard]] HostServicesSnapshot snapshot() const;
    [[nodiscard]] Event<const HostModalTransition&>& modal_changed() noexcept {
        return modal_changed_;
    }

protected:
    [[nodiscard]] virtual CursorCapabilities cursor_capabilities_impl() const noexcept;
    [[nodiscard]] virtual CursorStatus cursor_authority_impl(const Window& window, bool require_pointer) const noexcept;
    [[nodiscard]] virtual CursorStatus hide_cursor_impl() noexcept;
    [[nodiscard]] virtual CursorStatus restore_cursor_impl() noexcept;
    [[nodiscard]] virtual CursorStatus warp_cursor_impl(int client_x, int client_y) noexcept;
    [[nodiscard]] virtual HostMonitorResult query_monitors_impl() = 0;
    [[nodiscard]] virtual HostServiceStatus set_cursor_impl(CursorKind cursor) = 0;
    [[nodiscard]] virtual HostServiceStatus set_custom_cursor_impl(
        const CursorImagesPtr& images, const CursorImage& image);
    [[nodiscard]] virtual HostServiceStatus set_pointer_capture_impl(
        bool captured, std::uint64_t pointer_id) = 0;
    [[nodiscard]] virtual HostClipboardTextResult read_clipboard_text_impl() = 0;
    [[nodiscard]] virtual HostServiceStatus write_clipboard_text_impl(
        std::string_view text_utf8) = 0;
    [[nodiscard]] virtual HostClipboardImageResult read_clipboard_image_impl();
    [[nodiscard]] virtual HostClipboardFilesResult read_clipboard_files_impl();
    [[nodiscard]] virtual HostAutoCompleteResult query_auto_complete_impl(
        const HostAutoCompleteRequest& request);
    [[nodiscard]] virtual HostServiceStatus write_clipboard_image_impl(HostImageView image);
    [[nodiscard]] virtual HostDialogResult show_dialog_impl(
        const HostDialogRequest& request) = 0;
    [[nodiscard]] virtual HostServiceStatus play_sound_cue_impl(
        const HostSoundCueRequest& request) = 0;
    virtual void shutdown_impl() noexcept {}
    // Tray adapters: called with a validated request and an identity the base
    // class assigned. The defaults refuse as unsupported.
    [[nodiscard]] virtual HostServiceStatus add_tray_icon_impl(
        HostTrayIconId icon_id, const HostTrayIconRequest& request);
    [[nodiscard]] virtual HostServiceStatus update_tray_icon_impl(
        HostTrayIconId icon_id, const HostTrayIconRequest& request);
    // Must not fail in a way that leaves the icon shown; called at shutdown.
    virtual void remove_tray_icon_impl(HostTrayIconId icon_id) noexcept;
    [[nodiscard]] virtual HostServiceStatus show_tray_notification_impl(
        HostTrayIconId icon_id, const HostTrayNotificationRequest& request);
    [[nodiscard]] virtual HostTrayMenuResult show_tray_menu_impl(
        HostTrayIconId icon_id, const HostTrayMenuRequest& request);
    // Called with a backdrop whose capability the host advertises. The
    // default accepts `opaque`, which needs nothing native, and refuses the
    // rest as unsupported.
    [[nodiscard]] virtual HostServiceStatus apply_window_backdrop_impl(WindowBackdrop backdrop);
    // Adapters report native tray events here, on the UI thread. Events for
    // identities that are not live are dropped.
    void publish_tray_icon_event(const HostTrayIconEvent& event);

private:
    friend class CursorHiddenLease;
    [[nodiscard]] CursorStatus release_cursor_lease(CursorLeaseState& state) noexcept;
    [[nodiscard]] CursorStatus validate_cursor_request(const Window& window) const noexcept;
    std::shared_ptr<CursorLeaseState> cursor_lease_{};
    [[nodiscard]] HostServiceStatus validate_request(
        HostCapability capability) noexcept;
    [[nodiscard]] HostServiceStatus validate_tray_icon_request(
        const HostTrayIconRequest& request) const noexcept;
    [[nodiscard]] HostServiceStatus reject(HostServiceError error) noexcept;

    CursorImagesPtr prepared_cursor_images_;
    CursorImage prepared_cursor_;
    double prepared_request_scale_{};
    CursorImagesPtr active_cursor_images_;
    CursorImagesPtr failed_cursor_images_;
    double active_cursor_scale_{};
    double failed_cursor_scale_{};
    bool cursor_applied_{};
    std::thread::id ui_thread_;
    HostServicesSnapshot snapshot_;
    Event<const HostModalTransition&> modal_changed_;
    std::vector<std::uint64_t> modal_stack_;
    std::optional<HostSoundCue> last_sound_cue_;
    std::uint64_t last_sound_timestamp_{};
    // Live tray icon identities, in creation order; identities are never reused.
    std::vector<HostTrayIconId> tray_icons_;
    // The value the next added icon's identity takes.
    std::uint64_t next_tray_icon_id_{1U};
    Event<const HostTrayIconEvent&> tray_icon_events_;
    Event<const HostDialogHelpEvent&> dialog_help_requested_;
};

} // namespace gui_forms
