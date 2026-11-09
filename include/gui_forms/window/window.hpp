#pragma once

#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/window/toasts/toasts.hpp"

#include <chrono>
#include <cstdint>
#include <memory>

namespace gui_forms {

// Native popups (gui_forms/window/native_popup/native_popup.hpp).
class NativePopupToken;
struct NativePopupOptions;

class Application;
// Told that a live surface's frame was presented unpaired: older than the
// content tag its control required (Window::require_live_surface_content),
// once per required tag. Called on the window's thread only, through the
// window's dispatcher, never inside the present path that found it; a
// listener the control released first receives nothing.
class LiveSurfacePairingListener {
public:
    virtual ~LiveSurfacePairingListener() = default;
    virtual void live_surface_pairing_missed(std::uint64_t content_tag) = 0;
};

// A top-level window: a SurfaceRoot (its control tree, thread and
// dispatcher) presented through a native host.
//
// A window made on the current application's thread joins that
// application (Application::current; APP_WINDOW_RULES_DESIGN A.2): it
// shares the application's resources unless given its own, starts from its
// presentation defaults and reads its tooltip switch, and leaves it when
// destroyed. With no current application it makes its own resources.
class Window : public SurfaceRoot {
public:
    explicit Window(Control::Ptr root, Size client_size = {});
    // Joins `application` explicitly, for a program that wants the
    // dependency visible: std::logic_error off its thread or once closed.
    Window(Application& application, Control::Ptr root, Size client_size = {});
    /// Uses explicit per-window image quotas. The two-argument constructor
    /// retains the normal defaults; large-document consumers choose a budget.
    Window(Control::Ptr root, Size client_size, ImageRegistryLimits image_limits);
    /// Shares an application's image registry and theme with every other
    /// Window constructed with the same resources (stage DK decision 8): an
    /// ImageId, an ImageList and the theme hold in all of them, so a control
    /// subtree moved between them keeps its images. Null resources are
    /// std::invalid_argument; resources made on another thread are
    /// std::logic_error. The window takes the resources' current theme.
    Window(Control::Ptr root, Size client_size, std::shared_ptr<ApplicationResources> resources);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] CursorMetrics cursor_metrics() const noexcept;
    [[nodiscard]] CursorCapabilities cursor_capabilities() const noexcept;
    [[nodiscard]] CursorLeaseResult begin_cursor_hidden();
    // Synchronous client-DIP placement. Success is a native request, not a
    // fabricated motion/click; OS motion may be delivered or coalesced normally.
    [[nodiscard]] CursorStatus warp_cursor(CursorMetrics metrics, Point target);
    // Host-only: a host calls this after its native window's client area
    // changed size. It does not resize the native window; an application
    // asks for a size with ApplicationWindowHandle::request_client_size.
    void resize(Size client_size);
    void set_scale(double scale);

    // The window outer band (owner, 2026-11-08; gui_forms/window/band/
    // window_band.hpp). declare_band reserves the band on every side at
    // once: the root is measured against the client size less the band and
    // arranged inside it, and the window is laid out again. Declaring again
    // lays it out only when the reserved width changes. std::invalid_argument
    // for options window_band_options_valid refuses, changing nothing. UI
    // thread.
    void declare_band(const WindowBandOptions& options);
    // Removes the band and its reservation; the window is laid out again.
    // Does nothing for an undeclared band. UI thread.
    void undeclare_band();
    // What the band shows: paint only, never a layout; only the four strips
    // repaint. Kept while the band is undeclared, and shown once it is
    // declared. std::invalid_argument for an unknown state, changing
    // nothing. UI thread.
    void set_band_appearance(const WindowBandAppearance& appearance);
    [[nodiscard]] WindowBandSnapshot band_snapshot() const noexcept;
    // Host-only: the native window entered or left full screen. A band
    // declared WindowBandFullScreen::hides gives up its reservation in full
    // screen (a layout each way). UI thread.
    void report_full_screen(bool full_screen);
    // The corners this window asks its host for (owner, 2026-11-08, answer
    // 4): sent at once while a host is attached, and when one attaches. The
    // band follows what the host reports (band_snapshot().corners), not the
    // preference: a host without HostCapability::window_corners reports
    // unknown corners, and the band draws square. std::invalid_argument for
    // a value outside the enumeration, changing nothing. UI thread.
    void set_corner_preference(WindowCornerPreference preference);
    [[nodiscard]] WindowCornerPreference corner_preference() const noexcept {
        return band_.corner_preference;
    }

    // Records one live-surface frame the host copied to the screen. Live
    // frames carry no paint receipt; this only counts their cost.
    void notify_live_presented(std::uint64_t duration_nanoseconds, PresentedArea presented);
    // Called by a host when it attaches this window (stage DP). Throws
    // std::invalid_argument when the declared contract version is older than
    // paint_contract_version, or claims version 2 or later without partial
    // present: such a host fails attachment rather than presenting in a
    // degraded mode.
    void declare_presentation(PresentationDeclaration declaration);
    // The availability record: the host's declaration and the live lease.
    [[nodiscard]] PresentationAvailability presentation_availability() const noexcept;

    [[nodiscard]] bool queue_live_surface_presentation(
        const Control::Ptr& control, std::shared_ptr<LiveSurface> surface);
    // Content pairing (stage S2, design note section 6): the surface
    // registered for `control` must next show a frame published with a
    // content tag of at least `content_tag`. Until one is published, the next
    // present tick keeps the previous frame on screen (a pairing wait); the
    // tick after presents the newest frame unpaired. Tags only grow: a lower
    // one is ignored. False when `control` has no registration.
    bool require_live_surface_content(const Control::Ptr& control,
                                      std::uint64_t content_tag);
    // Where the notice of an unpaired presentation of the surface
    // registered for `control` goes, held weakly; replaces any earlier
    // listener (an empty one: none). A new surface for the control drops
    // it. False when `control` has no registration. UI thread.
    bool set_live_surface_pairing_listener(const Control::Ptr& control,
                                           std::weak_ptr<LiveSurfacePairingListener> listener);
    // Returns surfaces with a new generation or a changed placement
    // (destination, clip or crossing overlays). Failed native batches request
    // unchanged generations too, with current visibility and overlay clips;
    // callers must not retry stale placements.
    // Optional publication_changed reports newly observed producer generations,
    // independently of composition-only updates or hidden control sampling.
    [[nodiscard]] std::vector<LiveSurfacePresentation>
        take_live_surface_presentations(bool include_unchanged = false,
                                        bool* publication_changed = nullptr);
    // A registration is retained while its control remains attached. Native
    // display clocks use this to stay armed without polling the control tree or
    // requiring one UI callback per producer publication.
    [[nodiscard]] bool has_live_surface_presentations() const noexcept {
        return !live_surface_registrations_.empty();
    }

    // Native host idle handshake. Install/arm on the UI thread. The handler
    // may run on a producer thread and must only schedule lifetime-safe UI work.
    // One publication wakes each armed idle period; arm also checks generations
    // to cover a publication racing the host's final empty presentation drain.
    void set_live_surface_idle_wake_handler(std::function<void()> wake);
    void set_live_surface_idle_waiting(bool waiting);

    // Opens `content`, a live detached root, in a native popup window owned
    // by this window (owner, 2026-11-04; native_popup.hpp), at `bounds` in
    // this window's client area (logical units; each extent 1 to 32768),
    // kept inside the work area of its monitor. Only a true root window has
    // this capability: a sub-surface is not a Window (owner, 2026-11-05).
    // `owner` is a live, visible control of this window; the popup closes
    // when it is gone. When the run cannot open a popup window (no run, a
    // host without popup windows, a host refusal; the token's refusal()
    // names which) the content opens as an overlay at `bounds` instead, or
    // nothing opens (NativePopupOptions::fallback). std::logic_error for an
    // unavailable owner or content, std::invalid_argument for bounds out of
    // range or an invalid backdrop chain, changing nothing. UI thread.
    [[nodiscard]] NativePopupToken open_native_popup(const Control::Ptr& owner, const Control::Ptr& content,
                                                     Rect bounds, const NativePopupOptions& options);

    // Warm Popup windows (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S;
    // menus are Popups). Slot `slot` (below maximum_popup_windows; a menu
    // uses its depth) is one passive popup-kind host window, opened hidden
    // on the slot's first show and kept, hidden between shows, for this
    // window's life. show_popup_window shows `content` (a live detached
    // root) as its whole client at `bounds` (this window's client
    // coordinates, kept inside the monitor's work area), replacing content
    // already shown there; move_popup_window moves it; hide_popup_window
    // hides it and takes the content out. A Popup never takes the focus:
    // keys reach this window, and the control that opened the popup forwards
    // them to popup_window_model(slot), the model while the slot shows (else
    // null). False when the run cannot (no run, a host without popup
    // windows, a host refusal): the caller opens an overlay instead.
    // popup_window_closed() is raised with the slot when the host closes a
    // slot that shows. std::out_of_range for a slot out of range. UI thread.
    static constexpr std::size_t maximum_popup_windows = 8U;
    [[nodiscard]] bool popup_windows_available() const;
    // Whether this window's popup windows are translucent now: false on a
    // run whose host draws them opaque (X11 without a compositing manager),
    // where content shown in them casts no shadow and does not fade.
    [[nodiscard]] bool popup_windows_translucent() const;
    [[nodiscard]] bool show_popup_window(std::size_t slot, const Control::Ptr& content, Rect bounds);
    [[nodiscard]] bool move_popup_window(std::size_t slot, Rect bounds);
    void hide_popup_window(std::size_t slot) noexcept;
    [[nodiscard]] Window* popup_window_model(std::size_t slot) const noexcept;
    [[nodiscard]] Event<std::size_t>& popup_window_closed();
    // The slot drop-downs and pickers lift their lists and panels into
    // (PopupLift; owner, 2026-11-08: Part S scope for 0.5). Menus use the
    // slots below it, one per depth.
    static constexpr std::size_t picker_popup_window_slot = maximum_popup_windows - 1U;
    // While `slot` is set, every key and text event this window receives is
    // offered first to `popup_window_model(*slot)`, and routed here only when
    // that leaves it unhandled; none ends the forwarding. The control whose
    // Popup has the keyboard sets it (PopupLift does) and clears it when the
    // Popup hides. std::out_of_range for a slot out of range. UI thread.
    void forward_input_to_popup_window(std::optional<std::size_t> slot);
    // The work area of the monitor under `client_point` (this window's
    // client coordinates), in client coordinates: the region a Popup window
    // may cover, which reaches past the client area onto the desktop. None
    // when the run cannot say (no run, no frame, no monitors). Reads the
    // monitor table into storage the window keeps. UI thread.
    [[nodiscard]] std::optional<Rect> popup_work_area(Point client_point);

    // The application this window joined, or null (none was current, or it
    // was destroyed first).
    [[nodiscard]] Application* application() const noexcept { return application_; }
    // Toasts (W-4; coordinator ruling: one presenter per window, on the
    // tooltip presenter's pattern, so that both move to lightweight Splash
    // windows in Part S). Only a true root window shows toasts, as only it
    // opens native popups.
    //
    // show_toast places `content`, a live control subtree with no parent
    // and in no window (the stock content is Toast), in an overlay owned by
    // `owner` (passive, as a tooltip: a disabled owner may show one),
    // centred above the bottom of the client area at the size it
    // measures, for `duration` (zero: until dismissed or hidden). A toast
    // that shows is replaced (dismissed `replaced`). The content takes no
    // focus from the window. When the toast closes, the content leaves the
    // overlay, detached again, so the caller may show it again. Returns the
    // toast's ID: 1 for the first, growing by one. std::invalid_argument for
    // an owner that is not a live, visible control of this window, null or
    // disposed content, content with a parent or in a window, or a negative
    // duration, changing nothing. UI thread.
    std::uint64_t show_toast(const Control::Ptr& owner, const Control::Ptr& content,
                             std::chrono::milliseconds duration = default_toast_duration);
    // Closes the toast that shows when `content` is its content, for
    // `reason` (the content's own close mark or action); false, changing
    // nothing, otherwise. UI thread.
    bool dismiss_toast(const Control& content, ToastDismissReason reason);
    // Closes the toast that shows (dismissed `hidden`). UI thread.
    void hide_toast();
    [[nodiscard]] bool toast_visible() const noexcept;
    // The content of the toast that shows, or null.
    [[nodiscard]] Control::Ptr toast_content() const noexcept;
    // The ID of the toast that shows, or 0.
    [[nodiscard]] std::uint64_t toast_id() const noexcept;
    // Raised after a toast closed, last in the operation that closed it.
    [[nodiscard]] Event<const ToastDismissal&>& toast_dismissed() noexcept { return toast_dismissed_; }

private:
    friend class SurfaceRoot;
    friend class HostSession;
    friend class Application;
    Window(Control::Ptr root, Size client_size, std::shared_ptr<ApplicationResources> resources,
           Application* application);
    // The application joined and the neighbours in its window list
    // (Application::join); observers, cleared when either side goes.
    Application* application_{};
    Window* previous_in_application_{};
    Window* next_in_application_{};
    // Raises toast_dismissed.
    friend class detail::ToastPresenter;
    void raise_toast_dismissed(const ToastDismissal& dismissal);
    // The window's one toast presenter; made by the constructor and shut
    // down first in the destructor (it holds an overlay and a wake).
    std::unique_ptr<detail::ToastPresenter> toasts_;
    Event<const ToastDismissal&> toast_dismissed_{};
    // Native popups opened by this window, numbering their stable ids.
    std::uint64_t native_popup_serial_{};
    // The warm Popup windows, made on first use (popup_standby()). Shared
    // so that its deleter is bound where it is made.
    std::shared_ptr<detail::PopupStandby> popup_standby_{};
    [[nodiscard]] detail::PopupStandby& popup_standby();
    // Sends the corner preference to the attached host, if any, and takes
    // its report; changed corners repaint the band's strips. Called by
    // set_corner_preference and by HostSession when it attaches.
    void apply_corner_preference();
    // Called by HostSession: seeds the window's bindings with the host's
    // defaults unless they are seeded already.
    void install_host_key_map(const KeyMap& defaults);
    void initialize_cursor_identity() noexcept;
    void invalidate_cursor_interaction(CursorError cause) noexcept;
    std::uint64_t cursor_window_id_{};
    std::uint64_t cursor_metrics_generation_{1};
    struct LiveSurfaceRegistration final {
        Control::WeakPtr control;
        std::shared_ptr<LiveSurface> surface;
        std::uint64_t sampled_epoch{};
        std::uint64_t sampled_generation{};
        // Placement of the last presentation: destination, visible clip
        // before overlay subtraction, and the overlays crossing that clip.
        bool placed{};
        Rect placed_destination{};
        Rect placed_clip{};
        std::vector<Rect> placed_overlays;
        LiveSurfaceWakeConnection idle_wake{};
        std::uint64_t observed_epoch{};
        std::uint64_t observed_generation{};
        // Pairing: the least content tag the next presented frame must
        // carry, and whether the present tick before already waited for it.
        std::uint64_t required_content_tag{};
        bool pairing_waited{};
        // Who hears of unpaired presentations, and the newest required tag
        // already told (one notice per required tag).
        std::weak_ptr<LiveSurfacePairingListener> pairing_listener{};
        std::uint64_t pairing_missed_tag{};
    };
    using LiveSurfaceRegistrationMap =
        std::unordered_map<std::uint64_t, LiveSurfaceRegistration>;
    void collect_visible_overlay_rectangles(
        const Control::Ptr& control, std::vector<Rect>& rectangles) const;
    [[nodiscard]] static std::vector<Rect> subtract_rectangle(
        Rect source, Rect cover);
    LiveSurfaceRegistrationMap live_surface_registrations_;
    std::shared_ptr<detail::LiveSurfaceIdleWake> live_surface_idle_wake_{};
    PresentationDeclaration presentation_declaration_{};
};

} // namespace gui_forms
