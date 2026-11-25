#pragma once
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/cursor_image/cursor_image.hpp"

#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/host/cursor_interaction/cursor_interaction.hpp"
#include "gui_forms/window/band/window_band.hpp"
#include "gui_forms/window/chrome/window_chrome.hpp"
#include "gui_forms/types/platform_types/platform_types.hpp"

#include <stxlib/optional.hpp>
#include <chrono>
#include <optional>

namespace gui_forms {

// A corner request's outcome (HostServices::apply_window_corners): the
// status, and what the window's corners are afterwards, which the window's
// band follows. A refused or failed request reports unknown corners.
struct GUI_FORMS_API HostWindowCornersResult final {
    HostServiceStatus status{};
    WindowCornerReport report{};
};

// A chrome request's outcome (HostServices::apply_window_chrome): the
// status, and which of the requested properties the platform applied.
struct GUI_FORMS_API HostWindowChromeResult final {
    HostServiceStatus status{};
    WindowChromeFields applied{};
};

// Portable host-to-platform service seam. The base class owns thread,
// capability, UTF-8, size, accounting, modal-depth, and shutdown policy so
// adapters cannot silently disagree on those contracts.
class GUI_FORMS_API HostServices {
public:
    static constexpr std::size_t maximum_clipboard_text_bytes = 16U * 1024U * 1024U;
    static constexpr std::size_t maximum_dialog_text_bytes = 64U * 1024U;
    static constexpr std::size_t maximum_url_bytes = 8U * 1024U;
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
    static constexpr std::size_t maximum_desktop_notification_title_bytes = 255U;
    static constexpr std::size_t maximum_desktop_notification_text_bytes = 1023U;
    // Live notification identities a HostServices keeps; showing one more
    // withdraws the oldest (development bound, not measured capacity).
    static constexpr std::size_t maximum_live_desktop_notifications = 32U;

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
    // Relative pointer mode (capability relative_pointer; the window calls
    // it from SurfaceRoot::begin_relative_pointer and its ends). An active
    // request puts the native pointer in `request.mode`; an inactive one
    // gives the ordinary pointer back (shown, free, absolute moves) and is
    // accepted whenever the pointer is in the mode. Refused, changing
    // nothing: unsupported without relative_pointer, or for a confined or
    // locked mode without pointer_confinement; invalid_argument for a mode
    // outside the enumeration or a confine rectangle that is not finite and
    // positive in a confined mode. Shutdown gives the pointer back.
    [[nodiscard]] HostServiceStatus set_relative_pointer(const HostRelativePointerRequest& request);
    // Moves the pointer to `position`, window logical units (capability
    // pointer_warp). invalid_argument for a position that is not finite.
    [[nodiscard]] HostServiceStatus warp_pointer(Point position);
    // Hands `url` to the platform, which opens it in the user's browser or
    // mail program (capability open_url); it returns once the platform has
    // the address, not when anything opened. Refused, changing nothing:
    // unsupported without open_url; invalid_argument for an address that
    // is not valid UTF-8, is longer than maximum_url_bytes, holds a space
    // or a control character, or does not begin "https://", "http://"
    // (each with something after it) or "mailto:" (with an address after
    // it). No other scheme reaches the platform: a file, a program or a
    // custom handler is never opened this way. UI thread.
    [[nodiscard]] HostServiceStatus open_url(std::string_view url_utf8);
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
    // Starts a drag of `request.items` to other programs (capability
    // typed_drag_source); the program's own windows are destinations too.
    // Call it while handling a pointer press or move with a button held
    // down, as the drag follows that button. Refused, starting nothing:
    // unsupported without typed_drag_source; invalid_argument for items
    // outside the bounds HostDragSourceRequest states or for allowed
    // effects that are none or hold an unknown bit; modal_limit while
    // another drag from these services runs; backend_failure when the
    // platform refuses (no button held, no pointer event to start from).
    // An accepted drag ends exactly once, through drag_source_ended. On
    // Windows (DoDragDrop) and X11 (the XDND source's loop) the drag runs a
    // modal loop and ends before start_drag returns; on macOS it ends later,
    // from the run loop. The platform takes the press's release; on macOS
    // and X11 the window receives it right after the end is reported, so
    // pressed and capture state end as after any press (Windows ends the
    // capture with WM_CAPTURECHANGED). UI thread.
    [[nodiscard]] HostDragSourceResult start_drag(const HostDragSourceRequest& request);
    // Raised on the UI thread once for each accepted start_drag.
    [[nodiscard]] Event<const HostDragSourceEnd&>& drag_source_ended() noexcept {
        return drag_source_ended_;
    }
    // Whether a drag from these services is running.
    [[nodiscard]] bool drag_source_active() const noexcept { return active_drag_source_ != 0U; }
    // Desktop notifications (capability desktop_notifications), shown by
    // the platform's notification service outside the application's
    // windows. show_desktop_notification hands the request to the platform
    // and returns its identity, assigned from 1 and never reused by this
    // HostServices; the platform may still refuse it later, which arrives
    // as a `failed` event. Refused, changing nothing: unsupported without
    // the capability (always in a high-integrity build, which compiles the
    // service out); too_large for a title or text past its maximum;
    // invalid_utf8 for either not valid UTF-8; invalid_argument for empty
    // text, a NUL, or an urgency outside the enumeration; backend_failure
    // when the platform's service could not take it. With
    // maximum_live_desktop_notifications live, the oldest is withdrawn
    // first. UI thread.
    [[nodiscard]] HostDesktopNotificationResult show_desktop_notification(
        const HostDesktopNotificationRequest& request);
    // Takes a live notification back from the platform's list, raising no
    // event. invalid_argument for an identity that is not live.
    [[nodiscard]] HostServiceStatus withdraw_desktop_notification(HostDesktopNotificationId notification_id);
    // Whether the identity is shown and not yet activated, dismissed, failed
    // or withdrawn. Shutdown forgets every identity and leaves the
    // notifications in the platform's list, as platforms keep them.
    [[nodiscard]] bool desktop_notification_live(HostDesktopNotificationId notification_id) const noexcept;
    // Raised on the UI thread, once per live notification, when it is
    // activated, dismissed or fails; the identity is no longer live when
    // the event is raised.
    [[nodiscard]] Event<const HostDesktopNotificationEvent&>& desktop_notification_events() noexcept {
        return desktop_notification_events_;
    }
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
    // Asks the platform to shape the window's corners (capability
    // window_corners) and reports what they are afterwards. Refused as
    // unsupported, with unknown corners, without the capability; refused as
    // invalid_argument for a value outside the enumeration. A platform that
    // cannot comply (it rounds regardless, or cannot round) reports its
    // actual corners with `complied` false.
    [[nodiscard]] HostWindowCornersResult apply_window_corners(WindowCornerPreference preference);
    // Colours the platform's own window border (Windows 11:
    // DWMWA_BORDER_COLOR), or restores the system's colour for none. The
    // automation's in-use sign joins it while a model drives the window
    // (MCP round 1). Refused as unsupported where the platform draws no
    // coloured border of its own (the default), as wrong_thread off the UI
    // thread and after_shutdown once shut down; a refusal leaves the border
    // as it was.
    [[nodiscard]] HostServiceStatus apply_system_border_colour(stxlib::Optional<Color> colour);
    // Applies the named chrome properties of `chrome` to the window
    // (Window's chrome properties): applied lists those the platform
    // honoured; the others leave the native window as it was. Any value
    // passes through as given (Window validates); refused as invalid_argument
    // for a field outside WindowChromeFields, wrong_thread off the UI
    // thread, after_shutdown once shut down. No capability bit: what each
    // host can do is supported_window_chrome.
    [[nodiscard]] HostWindowChromeResult apply_window_chrome(const WindowChrome& chrome, WindowChromeFields fields);
    // The chrome properties this host applies; none unless the adapter says
    // so.
    [[nodiscard]] virtual WindowChromeFields supported_window_chrome() const noexcept;
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
    // Called with a validated request whose capabilities the host
    // advertises. An inactive request must give the pointer back even when
    // it reports a failure. The defaults refuse as unsupported.
    [[nodiscard]] virtual HostServiceStatus set_relative_pointer_impl(const HostRelativePointerRequest& request);
    [[nodiscard]] virtual HostServiceStatus warp_pointer_impl(Point position);
    // A high-integrity build (cmake/HighIntegrity.cmake) has none of the
    // hooks below up to show_dialog_impl, so no adapter compiles them: the
    // public entries refuse as unsupported (the capabilities are not
    // reported). Clipboard text stays with its allowance.
#if !defined(GUI_FORMS_HIGH_INTEGRITY)

    // Called with an address open_url validated, on a host that advertises
    // open_url. The default refuses as unsupported.
    [[nodiscard]] virtual HostServiceStatus open_url_impl(std::string_view url_utf8);
#endif
#if !defined(GUI_FORMS_HIGH_INTEGRITY) || defined(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    [[nodiscard]] virtual HostClipboardTextResult read_clipboard_text_impl() = 0;
    [[nodiscard]] virtual HostServiceStatus write_clipboard_text_impl(
        std::string_view text_utf8) = 0;
#endif
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    [[nodiscard]] virtual HostClipboardImageResult read_clipboard_image_impl();
    [[nodiscard]] virtual HostClipboardFilesResult read_clipboard_files_impl();
    [[nodiscard]] virtual HostAutoCompleteResult query_auto_complete_impl(
        const HostAutoCompleteRequest& request);
    [[nodiscard]] virtual HostServiceStatus write_clipboard_image_impl(HostImageView image);
    // Desktop notification adapters: called with a validated request and
    // the identity the base class assigned, on a host that advertises
    // desktop_notifications. The defaults refuse as unsupported. Withdraw
    // must not fail in a way that leaves the notification in the platform's
    // list where the platform can take it back.
    [[nodiscard]] virtual HostServiceStatus show_desktop_notification_impl(
        HostDesktopNotificationId notification_id, const HostDesktopNotificationRequest& request);
    virtual void withdraw_desktop_notification_impl(HostDesktopNotificationId notification_id) noexcept;
#endif
    [[nodiscard]] virtual HostDialogResult show_dialog_impl(
        const HostDialogRequest& request) = 0;
    [[nodiscard]] virtual HostServiceStatus play_sound_cue_impl(
        const HostSoundCueRequest& request) = 0;
    virtual void shutdown_impl() noexcept {}
    // Starts the platform drag for start_drag, which has validated the
    // request and assigned `session_id`; the adapter reports its end through
    // publish_drag_source_end. The default refuses as unsupported.
    [[nodiscard]] virtual HostServiceStatus start_drag_impl(std::uint64_t session_id,
                                                            const HostDragSourceRequest& request);
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
    // Called with a known preference when the host advertises
    // window_corners. The default refuses as unsupported with unknown
    // corners.
    [[nodiscard]] virtual HostWindowCornersResult apply_window_corners_impl(WindowCornerPreference preference);
    // Called on the UI thread before shutdown. The default refuses as
    // unsupported.
    [[nodiscard]] virtual HostServiceStatus apply_system_border_colour_impl(stxlib::Optional<Color> colour);
    // Called with fields the host supports (supported_window_chrome), at
    // least one. The default applies nothing.
    [[nodiscard]] virtual HostWindowChromeResult apply_window_chrome_impl(const WindowChrome& chrome,
                                                                          WindowChromeFields fields);
    // Adapters report native tray events here, on the UI thread. Events for
    // identities that are not live are dropped.
    void publish_tray_icon_event(const HostTrayIconEvent& event);
    // Adapters report the end of the drag start_drag_impl started here.
    // Dropped off the UI thread and for an identity that is not the running
    // drag's; a performed effect outside the drag's allowed effects is
    // reported as none. Ends the drag before raising drag_source_ended, so
    // a handler may start another.
    void publish_drag_source_end(const HostDragSourceEnd& event);
    // Adapters report what became of a desktop notification here, on the
    // UI thread. The identity stops being live before the event is raised;
    // events for identities that are not live are dropped (a second report,
    // or one for a withdrawn notification).
    void publish_desktop_notification_event(const HostDesktopNotificationEvent& event);

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
    ThreadId ui_thread_;
    HostServicesSnapshot snapshot_;
    Event<const HostModalTransition&> modal_changed_;
    std::vector<std::uint64_t> modal_stack_;
    stxlib::Optional<HostSoundCue> last_sound_cue_;
    std::uint64_t last_sound_timestamp_{};
    // Live tray icon identities, in creation order; identities are never reused.
    std::vector<HostTrayIconId> tray_icons_;
    // The value the next added icon's identity takes.
    std::uint64_t next_tray_icon_id_{1U};
    Event<const HostTrayIconEvent&> tray_icon_events_;
    // Live desktop notification identities, oldest first; never reused.
    std::vector<HostDesktopNotificationId> desktop_notifications_;
    // The value the next shown notification's identity takes.
    std::uint64_t next_desktop_notification_id_{1U};
    Event<const HostDesktopNotificationEvent&> desktop_notification_events_;
    Event<const HostDialogHelpEvent&> dialog_help_requested_;
    // The running drag's identity and allowed effects (zero and none when
    // no drag runs), and the identity the next drag takes.
    std::uint64_t active_drag_source_{};
    DragEffect active_drag_effects_{DragEffect::none};
    std::uint64_t next_drag_source_{1U};
    Event<const HostDragSourceEnd&> drag_source_ended_;
};

} // namespace gui_forms
