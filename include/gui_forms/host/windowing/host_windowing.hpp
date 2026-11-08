#pragma once

#include "gui_forms/event.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/host/windowing/desktop_coordinates.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gui_forms {

// Identity of one window within one application run. Identities are
// assigned from 1 in admission order and never reused within a run, so a
// destroyed window's identity cannot address a later window.
struct HostWindowId final {
    std::uint64_t value{};

    [[nodiscard]] bool valid() const noexcept { return value != 0U; }
    friend bool operator==(HostWindowId left, HostWindowId right) noexcept {
        return left.value == right.value;
    }
    friend bool operator!=(HostWindowId left, HostWindowId right) noexcept {
        return left.value != right.value;
    }
};

// What a window is to the run. The kind is fixed when the window is
// admitted.
enum class HostWindowKind : std::uint8_t {
    // The run's one main window: no owner, never destroyed through the
    // windowing seam (it closes, and the run ends).
    primary,
    // An owned window with ordinary decorations (ApplicationWindow with an
    // owner_id and no tool_window flag).
    standard,
    // ApplicationWindow::tool_window: the host's utility style, with each
    // host's existing behaviour (see docs/user/hosts-and-capabilities.md).
    tool,
    // A floating tool panel (stage DK decision 1): the floating policy below,
    // including hiding while the application is inactive.
    floating_tool,
    // A document torn out into its own window: the floating policy, but it
    // stays visible while the application is inactive.
    floating_document,
    // A native popup (owner, 2026-11-04): an undecorated window owned by
    // the window that opened it, with per-pixel alpha, placed by the
    // program, without a taskbar button, above its owner. Only a host
    // that opens popup windows admits it
    // (HostWindowing::supports_popup_windows).
    popup,
};

[[nodiscard]] const char* host_window_kind_name(HostWindowKind kind) noexcept;

// What the run does with a window of each kind (stage DK decisions 1, 10 and
// 11). One portable rule on every host; the adapters only carry it out.
struct HostWindowKindTraits final {
    // Owned by the primary window, without a taskbar button, minimized with
    // it, and above it only, never above other applications' windows. At
    // most HostWindowing::maximum_floating_windows are live at once.
    bool floating{};
    // Hidden while the application is inactive and shown again when it is
    // activated (the Mac palette convention). The program's own show and
    // hide still decide whether the window is wanted at all.
    bool hides_when_application_inactive{};
    // Moved with the primary window, keeping its offset from it, and clamped
    // to the work area of the screen it lands on.
    bool follows_main_window{};
    // Joins the primary window's full-screen space where a host has such
    // spaces (macOS); nothing elsewhere.
    bool joins_main_full_screen{};
    // A native popup: no decorations, no taskbar button, above its owner,
    // placed where the program puts it and never moved by the floating
    // policy. Whether it is activated is declared per popup
    // (HostPopupFocus).
    bool popup{};
};

[[nodiscard]] HostWindowKindTraits host_window_kind_traits(HostWindowKind kind) noexcept;

// Whether a window entry asks for a floating window (stage DK decision 1).
// A floating window is owned by the primary window and is not a tool
// window in the host's sense (ApplicationWindow::tool_window stays false).
enum class HostWindowFloating : std::uint8_t {
    none,
    // A floating tool panel: HostWindowKind::floating_tool.
    tool,
    // A document torn out into its own window: HostWindowKind::floating_document.
    document,
};

// How a native popup takes part in activation and keyboard focus, declared
// per popup; there is no global rule (owner, 2026-11-04).
enum class HostPopupFocus : std::uint8_t {
    // Never activated: the owner stays the active window and keeps keyboard
    // focus, and the core routes keys to the popup.
    passive,
    // Activated and given keyboard focus when shown; the owner's focus is
    // restored when it closes.
    takes_focus,
};

[[nodiscard]] const char* host_popup_focus_name(HostPopupFocus focus) noexcept;

// What a native popup window is for (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN
// Part S). One lightweight transient window serves two kinds that differ only
// in input: a Popup takes pointer input in its own surface, a Splash takes
// none (click-through, never key, never activated).
enum class HostTransientKind : std::uint8_t {
    // A Popup: today's native popup, its focus declared by HostPopupFocus.
    popup,
    // A Splash tooltip: no input, above the program's popups and menus.
    tool_tip,
    // A Splash splashscreen: no input, above the program's windows.
    splashscreen,
};

[[nodiscard]] const char* host_transient_kind_name(HostTransientKind kind) noexcept;
// Whether `kind` is a Splash (takes no input): tool_tip and splashscreen.
[[nodiscard]] bool host_transient_is_splash(HostTransientKind kind) noexcept;

// Where a native popup window opens and how it takes focus (owner,
// 2026-11-04): carried by its window entry (ApplicationWindow::popup and
// each adapter's native entry). Programs open popups through
// Window::open_native_popup, which fills this in.
struct HostPopupPlacement final {
    // A Splash kind takes no input, so its focus is always passive; an entry
    // declaring takes_focus with a Splash kind is invalid.
    HostTransientKind kind{HostTransientKind::popup};
    HostPopupFocus focus{HostPopupFocus::passive};
    // The client area's origin in desktop logical units, each coordinate
    // within HostWindowing::maximum_desktop_coordinate; the client size is
    // the entry's initial size.
    DesktopLogicalPoint origin{};
};

// The kind of a window entry, the one rule every adapter uses: a popup
// entry is popup; else a floating entry is floating_tool or
// floating_document; else a tool window is tool; else a window with an
// owner is standard and one without is primary.
[[nodiscard]] HostWindowKind host_window_kind(bool has_owner, bool tool_window,
                                              HostWindowFloating floating, bool popup) noexcept;

// Moves `outer` the least distance that puts it inside `work_area`; a
// rectangle wider or taller than the work area is aligned with its left or
// top edge. The size never changes.
[[nodiscard]] Rect clamp_to_work_area(Rect outer, Rect work_area) noexcept;

// What an adapter tells the windowing seam about a window it is about to
// create. `stable_id` is borrowed for the call.
struct HostWindowDescription final {
    std::string_view stable_id{};
    // Invalid for a window without an owner.
    HostWindowId owner{};
    HostWindowKind kind{HostWindowKind::standard};
    // For a popup: what it is for. A Splash needs supports_splash_windows().
    HostTransientKind transient{HostTransientKind::popup};
};

struct HostWindowAdmission final {
    HostServiceStatus status{};
    HostWindowId window{};
};

// A window's rectangle in desktop logical units (desktop_coordinates.hpp).
struct HostWindowFrame final {
    // The client area: the rectangle the window's model lays out and paints.
    Rect content{};
    // The whole window including native decorations where the host measures
    // them (outer_measured); equal to `content` where it cannot (an X11
    // window manager that publishes no frame extents).
    Rect outer{};
    bool outer_measured{};

    friend bool operator==(const HostWindowFrame& left,
                           const HostWindowFrame& right) noexcept {
        return left.content == right.content && left.outer == right.outer &&
               left.outer_measured == right.outer_measured;
    }
};

struct HostWindowFrameResult final {
    HostServiceStatus status{};
    HostWindowFrame frame{};
};

// Published once for every native move or resize the adapter reports,
// whoever caused it (the user, a frame request, the platform).
struct HostWindowFrameEvent final {
    HostWindowId window{};
    HostWindowFrame frame{};
};

struct HostScreenPointResult final {
    HostServiceStatus status{};
    DesktopLogicalPoint position{};
};

// An accepted result whose `window` is invalid means no window of this run
// is the topmost window at the point (the desktop, or another
// application's window, is).
struct HostWindowAtResult final {
    HostServiceStatus status{};
    HostWindowId window{};
};

struct HostWindowingSnapshot final {
    HostCapabilities capabilities{};
    std::size_t live_windows{};
    std::uint64_t admitted{};
    std::uint64_t retired{};
    std::uint64_t frame_requests{};
    std::uint64_t frame_reports{};
    std::uint64_t rejected_requests{};
    // Floating policy: windows hidden and shown by the policy (application
    // activation, the primary window minimized), and follow moves requested,
    // of which some were clamped to a work area.
    std::uint64_t policy_hides{};
    std::uint64_t policy_shows{};
    std::uint64_t follow_moves{};
    std::uint64_t clamped_follow_moves{};
    bool application_active{true};
    bool started{};
    bool shutdown{};

    [[nodiscard]] std::string to_json() const;
};

// Application-scope windowing for one run (stage DK decisions 6 and 7):
// the table of the run's live windows, runtime destruction, window frames
// in desktop logical units, the pointer's desktop position, the window
// under a desktop point and ordering within the application.
//
// The base class owns the UI-thread check, capability gating, identities,
// the bounds and validation, so every adapter applies the same rules; an
// adapter implements the `*_impl` operations for its platform. A request a
// host cannot perform reports `unsupported` (HostCapability bits
// runtime_windows, window_frames, screen_pointer, window_at_point and
// window_ordering).
//
// Opening a window is not a member: it needs the window's model and
// callbacks, which each adapter receives in its own entry type. The adapter
// calls admit() before it creates the native window and retire() when the
// native window is gone, whatever closed it.
//
// The floating policy (HostWindowKindTraits) is carried out here too: the
// adapter reports application activation, the primary window's minimized
// state and every frame, and the base class decides which floating windows
// are shown and where they move, calling show_impl, hide_impl and
// request_frame_impl. Following works the same whether or not the platform
// also moves owned windows itself (macOS child windows do): a floating
// window's offset is learnt only from moves of the floating window itself,
// never from a move the primary window caused.
//
// Every member runs on the thread that constructed the object. Requests
// from another thread report wrong_thread; requests after shutdown()
// report after_shutdown; unknown or retired identities are
// invalid_argument.
class HostWindowing {
public:
    static constexpr std::size_t maximum_windows = 64U;
    static constexpr std::size_t maximum_floating_windows = 32U;
    static constexpr std::size_t maximum_stable_id_bytes = 1024U;
    // Frame requests: each extent 1 to 32768 logical units, each origin
    // coordinate within plus or minus this bound.
    static constexpr double maximum_desktop_coordinate = 1000000.0;

    explicit HostWindowing(HostCapabilities capabilities);
    virtual ~HostWindowing();
    HostWindowing(const HostWindowing&) = delete;
    HostWindowing& operator=(const HostWindowing&) = delete;

    // Program requests.

    // The window's current frame, read from the platform.
    [[nodiscard]] HostWindowFrameResult frame(HostWindowId window);
    // Asks the platform to give the window's client area the rectangle
    // `content` in desktop logical units. Acceptance means the native
    // request was submitted; the new frame arrives through frame_changed,
    // possibly before this returns, possibly on a later event (X11), and the
    // platform or window manager may adjust it. Extents below the window's
    // minimum size are invalid_argument (adapter rule, as
    // ApplicationWindowHandle::request_client_size).
    [[nodiscard]] HostServiceStatus request_frame(HostWindowId window, Rect content);
    [[nodiscard]] HostScreenPointResult pointer_position();
    // The run's window that is topmost at `point` (desktop logical units),
    // counting native decorations where the host can.
    [[nodiscard]] HostWindowAtResult window_at(DesktopLogicalPoint point);
    // Orders `window` above every other window of this run, never above
    // other applications' windows, without activating it. Owned windows
    // stay above their owners as each platform keeps them.
    [[nodiscard]] HostServiceStatus bring_to_front(HostWindowId window);
    // Closes and destroys a window opened by this run without asking its
    // closing handler: the program decided. Its closed callback runs before
    // this returns and its identity is retired. The primary window and a
    // window that still owns live windows are invalid_argument.
    [[nodiscard]] HostServiceStatus destroy_window(HostWindowId window);
    // Monitors in desktop logical units.
    [[nodiscard]] HostMonitorResult query_monitors();
    // The program shows or hides a window. For a floating window this is
    // the window's wanted visibility; the policy shows it only while the
    // application is active (floating tools) and the primary window is not
    // minimized. Other kinds are shown or hidden directly.
    [[nodiscard]] HostServiceStatus set_visible(HostWindowId window, bool visible);

    // Table queries (no capability needed).
    [[nodiscard]] std::optional<HostWindowId> find(std::string_view stable_id) const noexcept;
    [[nodiscard]] bool live(HostWindowId window) const noexcept;
    [[nodiscard]] std::optional<HostWindowKind> kind(HostWindowId window) const noexcept;
    // The owner, or an invalid identity for a window without one. Empty for
    // an unknown window.
    [[nodiscard]] std::optional<HostWindowId> owner(HostWindowId window) const noexcept;
    [[nodiscard]] HostWindowId primary() const noexcept { return primary_; }
    // Whether the window should be on screen now: what the program asked
    // for, after the floating policy. Adapters show a new window only when
    // this is true. False for an unknown window.
    [[nodiscard]] bool policy_visible(HostWindowId window) const noexcept;
    [[nodiscard]] bool application_active() const noexcept { return snapshot_.application_active; }
    [[nodiscard]] std::size_t window_count() const noexcept { return windows_.size(); }
    [[nodiscard]] std::size_t floating_window_count() const noexcept;
    [[nodiscard]] const HostCapabilities& capabilities() const noexcept {
        return snapshot_.capabilities;
    }
    // Whether the adapter opens native popup windows (HostWindowKind::popup)
    // during the run. False unless the adapter says so; admit() refuses a
    // popup as unsupported without it.
    [[nodiscard]] virtual bool supports_popup_windows() const noexcept;
    // Whether the adapter opens Splash popup windows (HostTransientKind
    // tool_tip and splashscreen): click-through, never key, never activated.
    // False unless the adapter can; admit() refuses a Splash as unsupported
    // without it, and the core then falls back to an overlay. A host reports
    // false only when it truly cannot (owner, 2026-11-08: SPLASH_STUDY D5).
    [[nodiscard]] virtual bool supports_splash_windows() const noexcept;
    // Whether the run's popup and Splash windows have per-pixel alpha now,
    // so the core may draw a shadow or fade around their content. True
    // unless the adapter says otherwise; an X11 run without a compositing
    // manager reports false and its transient windows stay real windows,
    // opaque, with no shadow or fade (owner, 2026-11-08: Q-S2 (b)).
    [[nodiscard]] virtual bool transient_windows_translucent() const noexcept;
    [[nodiscard]] Event<const HostWindowFrameEvent&>& frame_changed() noexcept {
        return frame_changed_;
    }
    [[nodiscard]] HostWindowingSnapshot snapshot() const;
    // Ends the run's windowing: every later request reports after_shutdown
    // and the table is cleared. Called by the adapter as the run ends.
    void shutdown() noexcept;

    // Adapter protocol: only the adapter that owns this object calls these,
    // on the UI thread.

    // Validates and records a window before its native creation. Rules: the
    // stable id is 1 to 1024 bytes of UTF-8 without NUL and unique among
    // live windows; at most maximum_windows live windows; the owner, when
    // given, is live; exactly one primary, admitted first, without an
    // owner; a standard window has an owner; a floating window is owned by
    // the primary window, and at most maximum_floating_windows are live; a
    // popup has an owner and needs supports_popup_windows(), and a Splash
    // popup also supports_splash_windows() (else unsupported). After mark_started() an admission needs runtime_windows
    // and may not be primary. A refused admission changes nothing.
    [[nodiscard]] HostWindowAdmission admit(const HostWindowDescription& description);
    // The native window exists, hidden. Records whether the program wants it
    // visible; the adapter then shows it if policy_visible() says so.
    void window_created(HostWindowId window, bool initially_visible) noexcept;
    // The initial window set is complete; later admissions open windows
    // during the run.
    void mark_started() noexcept;
    // The native window is gone. Unknown identities are ignored.
    void retire(HostWindowId window) noexcept;
    // A native move or resize happened (or the first frame is known). The
    // frame is recorded and frame_changed is published. A move of the
    // primary window moves the windows that follow it; a move of a
    // following window by anything else sets its offset.
    void report_frame(HostWindowId window, const HostWindowFrame& frame);
    // The application became active or inactive (all its windows together).
    // Repeated reports of the same state are ignored.
    void report_application_active(bool active);
    // A window was minimized or restored. For the primary window this hides
    // or shows its floating windows.
    void report_minimized(HostWindowId window, bool minimized);
    // The user closed a hide_on_close window, which the adapter hid: the
    // program no longer wants it visible, so activation does not bring it
    // back.
    void report_hidden_on_close(HostWindowId window) noexcept;

protected:
    // Called only after validation: a live identity, a finite request in
    // range, the capability present, the UI thread.
    [[nodiscard]] virtual HostWindowFrameResult frame_impl(HostWindowId window) = 0;
    [[nodiscard]] virtual HostServiceStatus request_frame_impl(HostWindowId window,
                                                               Rect content) = 0;
    [[nodiscard]] virtual HostScreenPointResult pointer_position_impl() = 0;
    // May return any native result; the base accepts only a live identity
    // or an invalid one.
    [[nodiscard]] virtual HostWindowAtResult window_at_impl(DesktopLogicalPoint point) = 0;
    [[nodiscard]] virtual HostServiceStatus bring_to_front_impl(HostWindowId window) = 0;
    // Must close the native window, run its closed notification and call
    // retire() before returning accepted.
    [[nodiscard]] virtual HostServiceStatus destroy_window_impl(HostWindowId window) = 0;
    [[nodiscard]] virtual HostMonitorResult query_monitors_impl() = 0;
    // Shows the native window without activating it; hides it. Called for
    // floating windows by the policy and for any window by set_visible.
    [[nodiscard]] virtual HostServiceStatus show_impl(HostWindowId window) = 0;
    [[nodiscard]] virtual HostServiceStatus hide_impl(HostWindowId window) = 0;

private:
    struct WindowRecord final {
        HostWindowId id{};
        std::string stable_id{};
        HostWindowId owner{};
        HostWindowKind kind{HostWindowKind::standard};
        // The frame last reported, until the first report.
        std::optional<HostWindowFrame> frame{};
        // Visibility: what the program wants, and what the policy last put
        // on screen.
        bool requested_visible{};
        bool shown{};
        bool minimized{};
        // A following window's offset from the primary window's client
        // origin (until first learned), and the client origins the policy
        // last placed it at (most recent first). A report at one of those
        // origins is the policy's own placement, possibly answered late
        // (X11), and never changes the offset; the targets are forgotten when
        // the user moves the window.
        std::optional<Point> offset{};
        std::array<Point, 4> recent_targets{};
        std::size_t recent_target_count{};
        // The primary window's client origin when its followers were last
        // placed (none before the first placement); a follower move seen
        // while the primary has moved since then is the platform carrying
        // it, not the user.
        std::optional<Point> followed_origin{};
    };

    void apply_visibility(WindowRecord& entry);
    void apply_floating_visibility();
    // Moves the primary window's followers after it moved (report_frame).
    void follow_primary(WindowRecord& primary);
    // Records `origin` as the newest of the follower's policy placements.
    static void remember_policy_target(WindowRecord& follower, Point origin) noexcept;
    // Takes a follower's offset from a move of its own (report_frame).
    void learn_offset(WindowRecord& follower);
    // The admission rules of admit(), without its thread, shutdown,
    // capability and count checks.
    [[nodiscard]] bool admission_valid(const HostWindowDescription& description) const noexcept;

    [[nodiscard]] HostServiceStatus validate_request(HostCapability capability) noexcept;
    [[nodiscard]] HostServiceStatus validate_window_request(HostCapability capability,
                                                            HostWindowId window) noexcept;
    [[nodiscard]] HostServiceStatus reject(HostServiceError error) noexcept;
    [[nodiscard]] WindowRecord* record(HostWindowId window) noexcept;
    [[nodiscard]] const WindowRecord* record(HostWindowId window) const noexcept;

    std::thread::id ui_thread_;
    HostWindowingSnapshot snapshot_;
    // Live windows in admission order. Capacity maximum_windows is reserved
    // at construction; admission refuses beyond it, so the table never
    // reallocates during a run.
    std::vector<WindowRecord> windows_;
    std::uint64_t next_window_{1U};
    HostWindowId primary_{};
    Event<const HostWindowFrameEvent&> frame_changed_;
};

} // namespace gui_forms
