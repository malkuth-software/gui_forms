#pragma once
#include "x11_shm_present.hpp"
#include "x11_present_direct.hpp"
#include "../../../core/host/presentation/presentation_shim.hpp"
#include "../accessibility/linux_accessibility.hpp"
#include "../../../render/skia/raster/skia_raster.hpp"
#include "../../../render/skia/raster/skia_splash_bake.hpp"
#include "../services/x11_tray_icons.hpp"
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
#include "../services/linux_desktop_notifications.hpp"
#endif
#include "../compositor/x11_compositor.hpp"
#include "../settings/xsettings_watch.hpp"
#include "../../../host/linux/application/linux_input_rules.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/host/windowing/desktop_coordinates.hpp"
#include "gui_forms/host/windowing/first_frame_show.hpp"
#include "gui_forms/platform/linux_host.hpp"
#include "gui_forms/scheduler/display_timing/display_timing.hpp"
#include "../../../host/linux/frame_sync/x11_frame_sync.hpp"
#include <X11/Xlib.h>
#include <signal.h>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <cmath>
#include <stxlib/threads.hpp>
#include <vector>

namespace gui_forms::host::linux_detail {
class Runtime;
class NativeWindow;
// A position on the root window in X pixels (XTranslateCoordinates).
struct X11RootPosition final {
  int x{};
  int y{};
};
// The device pixels a logical extent covers at `scale`, rounded to the
// nearest and at least 1 (an X window has no empty side), at most 32768.
[[nodiscard]] inline unsigned device_extent(double logical, double scale) noexcept {
  const double device = std::round(logical * scale);
  if (!(device >= 1.0))
    return 1U;
  if (device > 32768.0)
    return 32768U;
  const unsigned pixels = static_cast<unsigned>(device);
  return pixels;
}
// A device coordinate for the X protocol, whose coordinates are 16-bit
// signed: device (already floored or rounded) clamped to that range, NaN
// as 0. Converting a double outside int's range is undefined, so every
// logical position a program or a control gives goes through here
// (A-gf-host-01-F9).
[[nodiscard]] inline int device_coordinate(double device) noexcept {
  if (std::isnan(device))
    return 0;
  if (device < -32768.0)
    return -32768;
  if (device > 32767.0)
    return 32767;
  const int coordinate = static_cast<int>(device);
  return coordinate;
}
// Application-scope windowing on X11 (stage DK phase W0, decisions 6 and 7;
// x11_windowing.cpp). The X server's desktop is in device pixels; desktop
// logical units map through each monitor's scale (desktop_coordinates.hpp),
// with the run's monitors cached in Runtime::device_monitors. Window
// positions are requests a window manager may adjust; XWayland's handling is
// measured, not assumed (notes:planning/stages/DK-docking/PLAN.md, W0).
class X11Windowing final : public HostWindowing {
public:
  explicit X11Windowing(Runtime &runtime);
  // Creates one window: admission, the X window (transient for its owner's),
  // attachment and, for a runtime window, its ready callbacks. Initial
  // windows receive ready() from run_linux_application once all exist.
  HostWindowAdmission open(LinuxApplicationWindow entry, bool runtime);
  // Reports the window's current frame (ConfigureNotify, first map).
  void report_native_frame(NativeWindow &window);
  // Popup and Splash windows (owner, 2026-11-08: SPLASH_STUDY D5 (b)):
  // override-redirect windows, opened whenever the run has a display. While
  // a compositing manager owns _NET_WM_CM_S<n> and the server has a 32-bit
  // ARGB visual they take that visual and are translucent; without one they
  // stay real windows on the default visual, opaque, and
  // transient_windows_translucent() reports false so the core draws no
  // shadow or fade (owner, 2026-11-08: Q-S2 (b)). They never fall back to
  // overlays.
  [[nodiscard]] bool supports_popup_windows() const noexcept override;
  [[nodiscard]] bool supports_splash_windows() const noexcept override;
  [[nodiscard]] bool transient_windows_translucent() const noexcept override;
  // run_modal's nested loop: Runtime::step turns, with the other windows
  // marked modal (NativeWindow::modal drops their input, as a retained
  // dialog does).
  [[nodiscard]] bool supports_modal_windows() const noexcept override;

protected:
  HostWindowFrameResult frame_impl(HostWindowId window) override;
  HostServiceStatus request_frame_impl(HostWindowId window, Rect content) override;
  HostScreenPointResult pointer_position_impl() override;
  HostWindowAtResult window_at_impl(DesktopLogicalPoint point) override;
  HostServiceStatus bring_to_front_impl(HostWindowId window) override;
  HostServiceStatus destroy_window_impl(HostWindowId window) override;
  HostMonitorResult query_monitors_impl() override;
  void query_monitors_into_impl(HostMonitorResult &result) override;
  HostServiceStatus show_impl(HostWindowId window) override;
  HostServiceStatus hide_impl(HostWindowId window) override;
  HostServiceStatus run_modal_impl(HostWindowId window) override;

private:
  NativeWindow *native(HostWindowId window) noexcept;
  // Whether `window` is `ancestor` or owned by it at any depth.
  [[nodiscard]] bool owned_by(HostWindowId window,
                              HostWindowId ancestor) const noexcept;
  // The child of the root window that holds `window` (the window manager's
  // frame, or the window itself without a reparenting window manager).
  ::Window top_level(::Window window) const;
  // Owner of this object (observing).
  Runtime &runtime_;
};
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).
DragFileListData local_files(const std::vector<std::byte>& data);
// The XDND source (linux_drag_drop.cpp). Not compiled in a high-integrity
// build (cmake/HighIntegrity.cmake).
//
// A local path as a file URI: "file://" and the path's bytes, each byte
// outside RFC 3986's unreserved set and '/' percent-encoded, so local_files
// reads it back unchanged.
[[nodiscard]] std::string xdnd_file_uri(std::string_view path);
// A file list as text/uri-list: one file URI a line, each line ended by
// CR LF.
[[nodiscard]] std::vector<std::byte> xdnd_uri_list(const DragFileListData& files);
// The effect a drag asks for under the modifier `state` (an X key-button
// mask): Shift and Control together link, Shift moves, Control copies,
// as GTK and Qt do; with neither, or when the asked effect is not allowed,
// the first allowed of copy, move and link.
[[nodiscard]] DragEffect xdnd_requested_effect(unsigned state, DragEffect allowed) noexcept;
// How a drag from the program ended: the effect the destination took
// (none when cancelled), the failure that kept it from starting, and the
// button release that ended it, which the window receives after the end is
// reported (the drag's pointer grab took it from the window).
struct XdndDragOutcome final {
  HostServiceStatus status;
  DragEffect performed_effect{DragEffect::none};
  std::optional<XEvent> release;
};
// Runs a drag of `request` (validated by HostServices::start_drag) from
// `window` until it ends: XdndSelection owned with the request's targets,
// the pointer and keyboard grabbed, XdndEnter, XdndPosition and XdndLeave
// sent to the XDND-aware window under the pointer (through its XdndProxy),
// XdndStatus honoured, XdndDrop on the release of the buttons held at the
// start and XdndFinished awaited for at most 5 seconds; Escape cancels.
// Other events are dispatched as the run loop would, so the program's own
// windows take part as destinations and selection requests are answered.
// backend_failure when no button is held or the grab or the selection is
// refused. UI thread.
[[nodiscard]] XdndDragOutcome run_xdnd_drag(NativeWindow &window, const HostDragSourceRequest &request);
// Where a drag from the program starts: the buttons held (an X button mask,
// not empty), the pointer's root position and the key-button state.
struct XdndDragStart final {
  unsigned buttons{};
  int root_x{};
  int root_y{};
  unsigned state{};
};
// run_xdnd_drag after the pointer query, from `start` (a test drives it
// without a held button). The window is held to the end of the run loop's
// turn (Runtime::drag_held_windows); when it closes during the drag, which
// a timer or queued action can do, the drag is cancelled: XdndLeave to the
// target, XdndSelection and the grabs given up, the effect none.
[[nodiscard]] XdndDragOutcome run_xdnd_drag_from(NativeWindow &window, const HostDragSourceRequest &request,
                                                 const XdndDragStart &start);
// One turn of the program's own work under a drag (run_xdnd_drag's loop):
// the queued actions and accessibility work, then every window's timers,
// frames and paint (Runtime::service_windows). Returns how long the loop
// may wait for its next event: the earliest window deadline, bounded by
// `maximum_wait` (negative for no bound). UI thread.
[[nodiscard]] int xdnd_drag_turn(Runtime &runtime, int maximum_wait);
#endif
// A present's source: the raster's premultiplied BGRA8 pixels, row_bytes
// apart, and the window's alpha mask, presented when present_alpha (a
// non-opaque backdrop is granted) and otherwise set opaque.
struct NativePresentSource final {
  const unsigned char* pixels{};
  std::size_t row_bytes{0};
  unsigned long alpha_mask{0};
  bool present_alpha{false};
};
// How a present converts pixels for a native image's layout. plain_copy is
// the common visual (32-bit native byte order, blue, green and red in the low
// three bytes, alpha presented in the fourth or absent): the raster's rows as
// they are. The others are the fallback kernels: byte_channels moves 8-bit
// channels into other byte positions, scaled32 scales channels of other
// widths into 32-bit pixels, and generic writes each pixel with XPutPixel.
enum class NativePresentKernel : std::uint8_t { plain_copy, byte_channels, scaled32, generic };
// Converts the rectangle [left, right) x [top, bottom) of `source` into
// `image` (whose data holds bytes_per_line * height bytes and covers the
// rectangle) and returns the kernel used; nullopt, converting nothing, for a
// mask wider than 32 bits.
[[nodiscard]] std::optional<NativePresentKernel> convert_present_rectangle(XImage& image,
                                                                         const NativePresentSource& source,
                                                                         int left, int top, int right, int bottom);
// A window's copy path, the presentation shim's safe fallback
// (docs/user/presentation.md): each rectangle converted into the window's
// native image and sent with XShmPutImage where MIT-SHM works and XPutImage
// otherwise, then one finish (XSync) or flush, exactly as before the shim.
// With host scroll on (GUI_FORMS_PRESENT=scroll) and a compositing manager
// keeping the window's contents, scroll moves presented pixels with
// XCopyArea. Owned by its NativeWindow, declared before the shim that
// borrows it; UI thread only.
class X11CopyPath final : public presentation::PresentationCopyPath {
public:
  explicit X11CopyPath(NativeWindow &window) noexcept : window_(window) {}
  [[nodiscard]] bool scrolls() const noexcept override;
  bool scroll(const presentation::PresentScrollPlan &plan) override;
  presentation::PresentOutcome copy(const presentation::PresentSourcePixels &source,
                                    std::span<const presentation::PresentRect> rects) override;

  // Set by each present before it runs the shim: the alpha the source is
  // presented with, and the area the copy adds to (null outside a present).
  unsigned long alpha_mask{0};
  bool present_alpha{false};
  PresentedArea *copied{};
  // The host's policy turned host scroll on for this window.
  bool scroll_enabled{};

private:
  // The owner (observing): the copy path is its member.
  NativeWindow &window_;
};
class Services final : public HostServices {
public:
  explicit Services(NativeWindow &window);
  ~Services() override;
  // Offers an X event to the tray icons; true when one of them took it.
  bool tray_event(const XEvent &event);
  // Relative pointer mode (gui_forms/events/relative_pointer): XI2 raw
  // motion on the root window for the deltas (raw_values unaccelerated,
  // valuators.values accelerated), a core pointer grab confined to an
  // InputOnly child covering the confine rectangle (confined) or to the
  // window with the pointer warped back after each sample (locked), and
  // XFixes to hide the pointer. While confined or locked, the window
  // dispatches no MotionNotify (relative_takes_motion).
  [[nodiscard]] bool relative_takes_motion() const noexcept {
    return relative_.active && relative_.mode != RelativePointerMode::free;
  }
  // One raw motion sample in device pixels, both forms, at `timestamp`
  // (FrameClock nanoseconds): one HostRelativeMotionEvent while the mode
  // is in force; nothing otherwise.
  void relative_raw_motion(Point raw, Point accelerated, std::uint64_t timestamp);

protected:
  HostMonitorResult query_monitors_impl() override;
  HostServiceStatus set_cursor_impl(CursorKind cursor) override;
  // The granted backdrop (stage T5, C4). The visual was fixed when the
  // window was made, and the present packs alpha whenever the window has
  // it, so nothing native changes: `opaque` is always accepted,
  // `transparent` only for a window made with the ARGB visual, and the
  // materials never (backend_failure, changing nothing).
  HostServiceStatus apply_window_backdrop_impl(WindowBackdrop backdrop) override;
  // Window's chrome properties through ICCCM and EWMH: WM_NAME and
  // _NET_WM_NAME, _NET_WM_ICON, _MOTIF_WM_HINTS for the frame and boxes,
  // _NET_WM_STATE (maximized, above, skip-taskbar) and iconify for the
  // state, _NET_WM_WINDOW_OPACITY, WM_NORMAL_HINTS for the minimum, and a
  // centred position before the first map. A tool window frame is the
  // ordinary frame: X has no smaller one to ask for.
  [[nodiscard]] WindowChromeFields supported_window_chrome() const noexcept override;
  HostWindowChromeResult apply_window_chrome_impl(const WindowChrome &chrome,
                                                  WindowChromeFields fields) override;
  HostServiceStatus set_custom_cursor_impl(const CursorImagesPtr&, const CursorImage&) override;
  void shutdown_impl() noexcept override;
  HostServiceStatus set_pointer_capture_impl(bool captured,
                                             std::uint64_t) override;
  HostServiceStatus set_relative_pointer_impl(const HostRelativePointerRequest &request) override;
  HostServiceStatus warp_pointer_impl(Point position) override;
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  // Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).
  HostServiceStatus open_url_impl(std::string_view url_utf8) override;
#endif
#if !defined(GUI_FORMS_HIGH_INTEGRITY) || defined(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
  HostClipboardTextResult read_clipboard_text_impl() override;
  HostServiceStatus write_clipboard_text_impl(std::string_view text) override;
#endif
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  HostClipboardImageResult read_clipboard_image_impl() override;
  HostClipboardFilesResult read_clipboard_files_impl() override;
  HostServiceStatus write_clipboard_image_impl(HostImageView image) override;
  // A drag to other programs (run_xdnd_drag): the drag ends, and is
  // reported, before this returns; then the window receives the release.
  // A window closed during its drag reports it cancelled (effect none).
  HostServiceStatus start_drag_impl(std::uint64_t session_id, const HostDragSourceRequest &request) override;
  // Desktop notifications through the freedesktop notification service
  // (LinuxDesktopNotifications), created with the first.
  HostServiceStatus show_desktop_notification_impl(HostDesktopNotificationId notification_id,
                                                   const HostDesktopNotificationRequest &request) override;
  void withdraw_desktop_notification_impl(HostDesktopNotificationId notification_id) noexcept override;
#endif
  HostDialogResult show_dialog_impl(const HostDialogRequest &request) override;
  HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest &) override;
  HostServiceStatus add_tray_icon_impl(HostTrayIconId icon_id,
                                       const HostTrayIconRequest &request) override;
  HostServiceStatus update_tray_icon_impl(HostTrayIconId icon_id,
                                          const HostTrayIconRequest &request) override;
  void remove_tray_icon_impl(HostTrayIconId icon_id) noexcept override;

private:
  struct CursorEntry final {
    CursorImagesPtr images;
    double scale;
    ::Cursor native;
  };
  void forward_tray_event(const HostTrayIconEvent &event) { publish_tray_icon_event(event); }
  void forward_notification_event(const HostDesktopNotificationEvent &event) {
    publish_desktop_notification_event(event);
  }
  // Gives the pointer back: ungrabbed, shown, raw motion deselected, the
  // confine window destroyed. Cannot fail in a way that keeps it.
  void release_relative_pointer() noexcept;
  std::vector<CursorEntry> cursors_;
  NativeWindow &window_;
  // The mode as applied (inactive when none), the InputOnly window a
  // confined grab is confined to (0 when none), whether XFixes hid the
  // pointer, and where a locked pointer is held (device pixels).
  HostRelativePointerRequest relative_{};
  ::Window confine_window_{};
  bool relative_cursor_hidden_{};
  int lock_x_{};
  int lock_y_{};
  // Created with the first icon.
  std::unique_ptr<X11TrayIcons> tray_;
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  // Created with the first desktop notification.
  std::unique_ptr<LinuxDesktopNotifications> notifications_;
#endif
};
// The run's wake pipe (A-gf-host-01-F8): both ends, closed when the last
// holder goes. The Runtime and every Wake copy (the model's wake handlers,
// the program's host_ready wake, the action queue) share it, so a copy a
// worker calls after the run has ended writes into this pipe, which a full
// buffer refuses (non-blocking), never into a descriptor the process has
// since reused; the read end stays open with it, so the write raises no
// SIGPIPE either.
struct WakePipe final {
  int descriptors[2]{-1, -1};
  WakePipe() = default;
  WakePipe(const WakePipe &) = delete;
  WakePipe &operator=(const WakePipe &) = delete;
  ~WakePipe();
};
struct Wake final {
  std::shared_ptr<const WakePipe> pipe{};
  void operator()() const noexcept;
};
enum class ActionKind { close, show, hide, fullscreen };
struct Action final {
  std::weak_ptr<NativeWindow> window;
  ActionKind kind;
};
struct ActionQueue final {
  stxlib::Mutex mutex;
  std::deque<Action> pending;
  Wake wake;
  void post(Action action);
};
struct WindowAction final {
  std::weak_ptr<NativeWindow> window;
  std::weak_ptr<ActionQueue> queue;
  ActionKind kind;
  void operator()() const;
};
class NativeWindow final : public std::enable_shared_from_this<NativeWindow> {
public:
  NativeWindow(Runtime &runtime, LinuxApplicationWindow entry);
  ~NativeWindow();
  void attach();
  // Types and shapes a popup or Splash window before it is first mapped.
  void mark_transient(const HostPopupPlacement &popup);
  void ready();
  void event(XEvent &event);
  void update();
  // A frame presented through the direct path was shown at `ust`
  // (microseconds of CLOCK_MONOTONIC) on refresh `msc`: the display's
  // timing it gives reaches the model (set_display_timing).
  void present_completed(std::uint64_t ust, std::uint64_t msc);
  // Reads the nominal refresh rate of the monitor under the window
  // (x11_refresh_period_at) and reports it as nominal timing, unless the
  // model already holds the display's own timing at that rate.
  void report_nominal_rate();
  // The compositor's frame synchronisation (src/host/linux/frame_sync):
  // creates the basic and extended XSync counters and names them in
  // _NET_WM_SYNC_REQUEST_COUNTER where the server has XSync; false (and
  // _NET_WM_SYNC_REQUEST stays out of WM_PROTOCOLS) otherwise.
  bool create_frame_sync();
  void destroy_frame_sync() noexcept;
  void set_sync_counter(XID counter, std::uint64_t value) noexcept;
  // Around each present: the extended counter odd, then even, and a
  // resize's basic request answered once the frame is sent.
  void begin_frame_sync() noexcept;
  void end_frame_sync() noexcept;
  // _NET_WM_SYNC_REQUEST, _NET_WM_FRAME_DRAWN and _NET_WM_FRAME_TIMINGS:
  // the frame timings reach the model as display timing
  // (set_display_timing); a compositor that sends only drawn stamps feeds
  // the estimator (note_presentation_completed). False for any other
  // message.
  bool frame_sync_message(const XClientMessageEvent &message);
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  // XDND drops (linux_drag_drop.cpp): not compiled in a high-integrity build.
  bool drag_message(const XClientMessageEvent &message);
#endif
  void action(ActionKind kind);
  // The first-frame show (gui_forms/host/windowing/first_frame_show.hpp),
  // after each update: a complete frame painted into the raster while the
  // window is unmapped, or the deadline at `now`, maps it. Nothing unless
  // the gate waits.
  void advance_first_show(FrameTime now);
  // Maps the window for a show the gate returned with `cause`. Shown by a
  // painted frame, the first Expose copies the raster; otherwise the whole
  // window is damaged and painted, as before the gate.
  void map_for_show(FirstFrameShowCause cause);
  void close(HostCloseReason reason, bool force = false);
  // Portable frame capture (capture_path host option): runs once after the
  // first presented frame whose damage covered the whole window, then closes.
  void frame_capture_if_due(bool covers_window);
  // The frame sequence (stage DP decision 2), gather: takes each live
  // surface's new frame once and the region of each fragment to draw; returns
  // true when a presentation region was taken from a surface. A frame that is
  // not opaque (or any frame, on the prepared-text raster) is composed by
  // retained painting instead: its region is added to `damage`.
  bool gather_live_frames();
  // Compose: draws each gathered frame into the raster under its region. A
  // fragment that fails to draw falls back to retained painting.
  void compose_live_frames();
  void report_live_shown(std::size_t source);
  // Reports one present that copied `copied`: the live frames' share as live
  // frames, the rest as the retained present (when `receipt` is set), the
  // duration split by pixels. Returns whether the window accepted the receipt.
  bool report_presented(const stxlib::Optional<PaintReceipt>& receipt, std::uint64_t elapsed,
                        PresentedArea copied);
  HostDispatchResult dispatch(HostEventPayload payload);
  // One touchpad pinch event (XI 2.4, Runtime::dispatch), its values in
  // device pixels as the server reports them: dispatched as a GestureEvent.
  void pinch_event(LinuxPinchStage stage, bool cancelled, Point device_position,
                   double absolute_scale, double delta_angle_clockwise, Point device_delta,
                   unsigned modifier_state, Time time);
  // Reports the compositor's state to this window (stage T5, C4): after
  // attach, and on each XFixes notification about _NET_WM_CM_S<screen>. A
  // reading that differs from the window's last report only by its
  // generation is not sent.
  void report_compositor_state();
  // Reports the window's full-screen state (HostWindowStateEvent; owner,
  // 2026-11-08: the window outer band) after attach, on each _NET_WM_STATE
  // change, and when the host's own override-redirect fallback toggles. A
  // refused report is tried again on the next change.
  void report_window_state();
  // Writes _NET_WM_STATE from the wanted states (skip-taskbar for utility
  // windows and Window's ShowInTaskbar, above for TopMost, maximized for
  // WindowState) while the window is unmapped: a window manager drops the
  // property when a window is withdrawn, so it is written before every map.
  void restate_wm_state();
  // The client area's least size: Window's MinimumSize where a side is
  // written non-zero, else the size the window opened with.
  [[nodiscard]] Size minimum_size() const noexcept;
  HostDialogResult dialog(const HostDialogRequest &request);
  // The window's frame in desktop logical units: the client area at its
  // root position (desktop_logical_point over the run's monitors) and its
  // logical size, and the outer frame from _NET_FRAME_EXTENTS (device
  // pixels, divided by the window's scale) when the window manager
  // publishes it.
  HostWindowFrame frame() const;
  // ConfigureNotify: tells the model the window moved when its client
  // area's root position changed since the last ConfigureNotify (not for a
  // passive popup or a Splash, which nothing is placed against), so a
  // menu's Popup windows close (SurfaceRoot::report_frame_moved). The
  // position is read from the server, since a real event's position is
  // relative to a reparenting window manager's frame and only a synthetic
  // one is root-relative (ICCCM 4.1.5). A live resize under X11 is a run of
  // ConfigureNotify events the window manager drives, with no start event
  // to the client: a resize from the left or top edge moves the client
  // area and is reported here; one from the right or bottom edge is not.
  void report_moved_to_model();
  // The client area's root position in X pixels, read from the server
  // (XTranslateCoordinates); none when the server cannot translate it.
  [[nodiscard]] std::optional<X11RootPosition> root_position() const;
  // Whether moves of this window are reported to its model: every window
  // but a passive popup and a Splash.
  [[nodiscard]] bool reports_frame_moves() const noexcept;
  // Asks the window manager for a client rectangle in desktop logical units
  // (static gravity, so the position is the client area's, not the
  // frame's): the origin through desktop_device_point, the extent at the
  // scale of the monitor it lands on.
  HostServiceStatus request_frame(Rect content);
  // The device scale for the window where it now is: the scale of the
  // monitor holding its client area's centre (Runtime::scale_at).
  [[nodiscard]] double scale_here() const;
  // Moves the window to `next` scale (audit N66, option a): a
  // HostScaleEvent, the raster re-rastered at the next update over the
  // whole window, the minimum size hint and the accessibility extents
  // rescaled, and a native resize that keeps the logical size (the
  // macOS and Windows rule; WM_DPICHANGED's suggested rectangle). A window
  // the window manager holds full screen or maximized keeps its device size
  // and reports the logical size that gives. Nothing for an unchanged scale.
  void apply_scale(double next);
  // The input settings for this window in its logical units: the desktop's
  // pixel distances (drag rectangle, double-click distance) divided by the
  // window's scale where the desktop set them.
  [[nodiscard]] HostInputSettingsEvent input_settings_event() const;
  // Writes WM_NORMAL_HINTS' minimum (minimum_size at the window's scale),
  // keeping the hints' other fields.
  void write_minimum_hint();
  Runtime &runtime;
  LinuxApplicationWindow entry;
  // The window's identity in the run (X11Windowing).
  HostWindowId window_id{};
  // Initial windows keep their native state until the run returns; runtime
  // windows are released by Runtime::reclaim after they close.
  bool initial{true};
  ::Window xid{};
  // The window's visual and depth, fixed at creation: the run's ARGB
  // visual for a window declared non-opaque when the server offers one
  // (`argb`), else the screen's default. `alpha_mask` is the pixel bits
  // holding alpha, 0 without the ARGB visual.
  Visual *visual{};
  int depth{};
  unsigned long alpha_mask{};
  bool argb{};
  // The present carries the raster's alpha only while a non-opaque
  // backdrop is granted (set by the services' apply_window_backdrop);
  // otherwise an ARGB window presents alpha 255, as an opaque window does.
  bool present_alpha{};
  // The ARGB window's own colormap, freed after the window; None otherwise.
  Colormap colormap{};
  // The last compositor report's generation; 0 before the first.
  CompositorGeneration compositor_generation{};
  // A compositing manager ran at the last compositor report: the window's
  // contents are kept off screen, so host scroll may move them.
  bool compositor_running{};
  GC gc{};
  XIC input{};
  // The client area in logical units: the device size divided by `scale`.
  Size size;
  // The window's device scale (audit N66, option a): logical units to X
  // pixels. Set at creation from the monitor it opens on, and changed only
  // by apply_scale.
  double scale{1.0};
  // The client area in X pixels as the server last reported it
  // (ConfigureNotify), or as created.
  int device_width{};
  int device_height{};
  // The client area's root position in X pixels at the first map, then at
  // each ConfigureNotify (report_moved_to_model); none before the first map
  // or ConfigureNotify, whichever comes first, which sets it without
  // reporting a move. The map sets it because without a window manager
  // the first ConfigureNotify is the window's first move.
  std::optional<X11RootPosition> reported_root_position;
  bool closed{};
  bool visible{};
  // Holds the X input focus (FocusIn without a grab); the run is active
  // while any of its windows does.
  bool focused{};
  // A utility window for the window manager (decision 11): tool and
  // floating windows, typed _NET_WM_WINDOW_TYPE_UTILITY and kept off the
  // taskbar (_NET_WM_STATE_SKIP_TASKBAR).
  bool utility{};
  // Where a window hidden by the floating policy was, so mapping it again
  // puts it back (a window manager may place a remapped window anew).
  std::optional<Rect> hidden_content;
  bool modal{};
  // Window's chrome properties the host keeps for restate_wm_state and
  // minimum_size; shown_once is set at the first map (a start position
  // applies only before it).
  bool chrome_top_most{};
  bool chrome_skip_taskbar{};
  FormWindowState chrome_window_state{FormWindowState::normal};
  Size chrome_minimum_size{};
  bool shown_once{};
  // The first-frame show gate (first_frame_show.hpp). While it waits the
  // window stays unmapped and update() paints the model into the raster
  // without presenting (first_show_staging, read by
  // present_raster_rectangles); first_show_staged records that such a
  // present was asked for, so a complete staged frame (damage acknowledged)
  // can release the show. first_show_copy_pending: the window was mapped
  // with that frame in the raster, so the Expose burst that follows copies
  // the raster instead of repainting; cleared at the burst's last event.
  FirstFrameShowGate first_show{};
  bool first_show_staging{};
  bool first_show_staged{};
  bool first_show_copy_pending{};
  bool frame_capture_completed{};
  bool frame_capture_saved{};
  bool cursor_set{};
  bool fallback_fullscreen{};
  // The full-screen state the session last accepted (report_window_state).
  bool window_state_reported{};
  bool reported_full_screen{};
  Rect restored_frame;
  std::array<bool, 256> pressed_keys{};
  CursorKind current_cursor{CursorKind::arrow};
  std::unique_ptr<LinuxAccessibility> accessibility;
  std::unique_ptr<Services> services;
  std::unique_ptr<HostSession> session;
  render::SkiaRaster raster;
  DamageRegion damage;
  // Scroll copies taken from the model and not yet applied to the raster, in
  // order (SurfaceRoot::take_scroll_copies); scratch for the take; the clips
  // the current tick applied, damaged again if it presents nothing; and the
  // count applied.
  std::vector<ScrollCopy> pending_copies;
  std::vector<ScrollCopy> taken_copies;
  std::vector<Rect> applied_copy_clips;
  std::uint64_t scroll_copies_applied{};
  // Host scroll (docs/user/presentation.md), while the presentation
  // policy turns it on: the copies applied this tick in device pixels,
  // which the shim moves on screen, and the screen rectangles without the
  // copies' clips. Reused.
  std::vector<presentation::PresentScroll> present_scrolls;
  DamageRegion screen_moved;
  SubscriptionToken closing_token;
  std::uint64_t sequence{};
  // The last press, for the computed click count (linux_input_rules.hpp).
  LinuxClickHistory click_history{};
  // The touchpad pinch in progress (XI 2.4 gesture events): its scale since
  // it began, for the per-event ratio (linux_input_rules.hpp).
  LinuxPinchState pinch{};
  std::vector<char> presentation;
  // A present's rectangles in device pixels (the logical damage times the
  // window's scale), reused across presents.
  std::vector<Rect> present_device;
  // The shared-memory image presents use where MIT-SHM is available
  // (audit G4/N67); released in ~NativeWindow, before the display closes.
  X11SharedImage shared_image;
  // The presentation shim (docs/user/presentation.md): the copy path it
  // borrows, declared first so that it outlives the shim; the shim, made
  // with the window's first present and released in ~NativeWindow before
  // the display closes; the present's device rectangles, reused.
  X11CopyPath copy_path{*this};
  std::unique_ptr<presentation::PresentationShim> presenter;
  // The shim's X11 Present direct path (observing; owned by `presenter`),
  // null while the presentation policy leaves it off. Receives the
  // window's PresentIdleNotify events (Runtime::dispatch).
  X11PresentPath *direct_path{};
  // The display's timing from the direct path's completions (Present's ust
  // and msc; present_completed), and whether the nominal rate of the
  // monitor under the window was read since the window last moved or the
  // screens changed (report_nominal_rate).
  RefreshCounterTiming refresh_timing;
  bool nominal_rate_read{};
  // The sync counters (zero without XSync or before they are made), the
  // values they are set to, the drawn times of the last few frames by
  // counter value (a timings message gives its presentation as an offset
  // from its frame's), and whether any timings message gave a timing.
  struct DrawnFrame final {
    std::uint64_t counter{};
    std::uint64_t drawn_microseconds{};
  };
  XID sync_basic_counter{};
  XID sync_extended_counter{};
  X11FrameSyncCounter frame_sync;
  std::array<DrawnFrame, 8> drawn_frames{};
  std::size_t drawn_frame_next{};
  bool frame_timings_seen{};
  // The presentation policy turned host scroll on (set with the shim).
  bool host_scroll_policy{};
  std::vector<presentation::PresentRect> present_rects;
  // The invalidation overlay's present-time storage (gui_forms/diagnostics/
  // invalidation_overlay), reused: the scratch frame the outlines are
  // composited in (sized on first use), the device areas they cover now and
  // those they covered at the last present, and the device rectangles a
  // present copies with them.
  std::vector<std::byte> overlay_scratch;
  std::vector<DevicePixelRect> overlay_regions;
  std::vector<DevicePixelRect> overlay_drawn;
  std::vector<Rect> overlay_present;
  // Per tick, reused across ticks. Each surface's frame is taken once
  // (`live_frames`, holding its read lease until the tick is composed), with
  // the device pixels and rectangles presented for it. Each fragment of a
  // clip (`live_updates`) names its frame in `live_frame_sources` and the
  // region to draw directly in `live_regions`, empty when retained painting
  // composes it or nothing changed.
  std::vector<LiveSurfacePresentation> live_updates;
  std::vector<LiveSurfaceFrame> live_frames;
  // The sources this tick reported shown (report_live_shown).
  std::vector<bool> live_reported;
  std::vector<std::size_t> live_frame_sources;
  std::vector<Rect> live_regions;
  std::vector<std::uint64_t> live_frame_pixels;
  std::vector<std::uint64_t> live_frame_rectangles;
  // A taken presentation region that was never presented: the next tick
  // repaints whole clips.
  bool live_region_lost{};
  // The live clock rests (audit E4): the last gather found no new frame,
  // so the run stops its 16 ms sampling for this window and the idle
  // handshake is armed; a producer's publication writes the wake pipe.
  bool live_idle{};
  ::Window drag_source{};
  Atom drag_type{};
  DragEvent drag;
  std::uint64_t drag_sequence{};
};
struct SelectionTransfer final {
  ::Window requestor{};
  Atom property{};
  Atom target{};
  std::vector<std::byte> bytes;
  std::size_t offset{};
  std::chrono::steady_clock::time_point deadline;
};
// How long a loop may wait after a turn, in milliseconds (negative: no
// bound), and whether any window is still open (Runtime::service_windows).
struct RunWait final {
  int milliseconds{-1};
  bool active{};
};
class Runtime final {
public:
  Runtime();
  ~Runtime();
  // Windows without an identity (retained dialogs, tests) are not part of
  // the windowing table.
  std::shared_ptr<NativeWindow> add(LinuxApplicationWindow entry,
                                    ::Window owner = 0,
                                    HostWindowId window_id = {},
                                    bool initial = true);
  void step(int maximum_wait = -1);
  // The parts of a turn a nested loop (the XDND source's drag) repeats so
  // the program keeps running under it.
  //
  // Empties the wake pipe and runs the queued window actions and the
  // accessibility bus's work; true when an action ran.
  bool run_woken_work();
  // Updates every window (its timers, frames, first show and paint) and
  // returns how long the loop may then wait: the earliest of
  // `maximum_wait` (negative for no bound), the selection transfers'
  // deadlines, each window's next wake, first show deadline, live clock and
  // overlay fade; and whether any window is still open.
  RunWait service_windows(int maximum_wait);
  // Waits at most `milliseconds` (negative: no bound) for an X event, the
  // wake pipe or the accessibility bus.
  void wait_for_work(int milliseconds);
  // Releases runtime windows that closed, between loop turns.
  void reclaim();
  // Reports application activation to the windowing after a turn's events:
  // active while any window holds the focus. Nothing is reported before
  // the first focus is seen (an X server without a window manager may
  // never give one).
  void report_activation();
  bool ever_focused{};
  // Reports the input settings to every attached window
  // (HostInputSettingsEvent); a window whose settings already match ignores
  // the report.
  void report_input_settings();
  // The interval click counting uses: the reported one, or the default
  // before the watch exists.
  [[nodiscard]] std::uint32_t double_click_milliseconds() const noexcept;
  // The desktop's double-click distance (XSettingsWatch), or the default.
  [[nodiscard]] double double_click_distance() const noexcept;
  // Refreshes device_monitors, then reports them to every attached window
  // (HostDisplayEvent, device units) after RandR announced a screen change
  // or the desktop's scale changed (audit N66), and moves each window to
  // the scale of the monitor it is on (NativeWindow::apply_scale).
  void report_displays();
  // The desktop's device scale (XSettingsWatch::scale), or 1 before the
  // watch exists. X11 states one density for the whole desktop, so every
  // monitor carries it.
  [[nodiscard]] double desktop_scale() const noexcept;
  // The scale of the monitor holding `point`, or of the nearest one
  // (nearest_device_monitor over device_monitors); the desktop's scale
  // while no monitor is known.
  [[nodiscard]] double scale_at(DesktopDevicePoint point) const noexcept;
  // The run's monitors in device units (query_x11_monitors), read at
  // construction and again by report_displays. The coordinate conversions
  // of the windowing seam and scale_at read it.
  std::vector<HostMonitor> device_monitors;
  void dispatch(XEvent &event);
  Atom atom(const char *name) const;
  // RandR's first event number, where the build has Xrandr and the server
  // offers it; -1 otherwise, and no screen change is reported.
  int randr_event_base{-1};
  // XInput 2's major opcode where the build has libXi (GUI_FORMS_HAVE_XI2)
  // and the server offers XI 2.0 or later; -1 otherwise, and relative
  // pointer mode is not offered (Services drops relative_pointer and
  // raw_pointer_motion).
  int xi_opcode{-1};
  // Whether the server speaks XI 2.4 and the build's libXi defines its
  // gesture events: each window then selects the touchpad pinch
  // (XI_GesturePinchBegin/Update/End). Otherwise no gesture is reported
  // (an older server sends a pinch as nothing, never as wheel events).
  bool xi_gestures{false};
  // The Present extension's opcode (x11_present_direct.hpp); -1 without it.
  int present_opcode{-1};
  // The server offers XSync (GUI_FORMS_HAVE_XSYNC): windows make their
  // frame sync counters (NativeWindow::create_frame_sync).
  bool xsync_available{};
  // The server's 32-bit millisecond time of an event, unwrapped and mapped
  // onto the steady clock (HostEventClock): PointerEvent and
  // HostRelativeMotionEvent timestamps. UI thread.
  [[nodiscard]] std::uint64_t server_time_nanoseconds(Time time) noexcept;
  HostEventClock server_clock{};
  std::uint32_t last_server_time{};
  std::uint64_t server_time_wraps{};
  bool server_time_seen{};
  Display *display{};
  std::array<std::uint32_t, 256> physical_keys{};
  XIM input_method{};
  // The pipe's ends, as wake_channel holds them; valid while it does.
  int wake_pipe[2]{-1, -1};
  std::shared_ptr<WakePipe> wake_channel{};
  // SIGTERM and SIGHUP end the session (a log-out or shut-down sends them;
  // HostCloseReason::session_end). Runtime() installs a handler for each
  // only where the disposition is still the default, so an application's
  // own handler stays in force, and ~Runtime restores the previous action.
  // The handler sets a flag and writes to wake_pipe; step() then closes
  // every window. With two runtimes at once only the first installs.
  struct sigaction previous_terminate{};
  struct sigaction previous_hangup{};
  bool terminate_installed{};
  bool hangup_installed{};
  // Closes every window, newest first, with HostCloseReason::session_end
  // (step(), after a session-ending signal).
  void end_session();
  std::shared_ptr<ActionQueue> actions;
  std::vector<std::shared_ptr<NativeWindow>> windows;
  // Created by run_linux_application right after the runtime; destroyed
  // after ~Runtime has closed every window.
  std::unique_ptr<X11Windowing> windowing;
  // The compositing manager's selection (stage T5, C4). Made after the
  // display opens; released before it closes, after every window.
  std::unique_ptr<X11CompositorWatch> compositor;
  // Made at the end of Runtime(), released before the display closes.
  std::unique_ptr<XSettingsWatch> input_settings;
  ::Window clipboard_owner{};
  std::string clipboard_text;
  HostImage clipboard_image;
  std::vector<std::byte> clipboard_png;
  std::uint64_t clipboard_generation{};
  std::vector<std::byte>
  read_selection(NativeWindow &window, Atom selection, Atom target,
                 Time time = CurrentTime,
                 std::size_t limit = HostImage::maximum_bytes);
  std::vector<SelectionTransfer> transfers;
  void selection_property(const XPropertyEvent &event);
  void selection_request(const XSelectionRequestEvent &request);
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
  // One target a drag from the program offers on XdndSelection, and its
  // bytes.
  struct DragOfferTarget final {
    Atom target{};
    std::vector<std::byte> bytes;
  };
  // The window owning XdndSelection for a running drag (0 when none) and
  // what it offers; set and cleared by run_xdnd_drag.
  ::Window drag_offer_owner{};
  std::vector<DragOfferTarget> drag_offer;
  // The offered bytes for `target`, or nullptr when it is not offered.
  [[nodiscard]] const std::vector<std::byte> *drag_offer_bytes(Atom target) const noexcept;
  // The windows drags from the program started from, held until the run
  // loop's turn ends (reclaim) so a handler that closes a source window
  // during its drag (whose loop runs the program's timers and actions) frees
  // nothing the drag or its services still use.
  std::vector<std::shared_ptr<NativeWindow>> drag_held_windows;
#endif

private:
  // Writes `bytes` to the requestor's `property` for `request` (by INCR
  // above 64 KiB); false when the transfer table is full or the requestor
  // is gone, and the request is refused.
  bool write_selection_reply(const XSelectionRequestEvent &request, Atom property, std::vector<std::byte> bytes);
};
HostDialogResult retained_dialog(NativeWindow &owner,
                                 const HostDialogRequest &request);
// The display's monitors into `result` (audit N66): RandR 1.5 monitors where
// the build has Xrandr and the server offers them, otherwise the default
// screen as one monitor; each work area is the frame within _NET_WORKAREA.
// Every scale is the desktop's (Runtime::desktop_scale, from Xft.dpi). Device
// units. UI thread.
void query_x11_monitors(const Runtime &runtime, HostMonitorResult &result);
// The nominal refresh period of the RandR mode of the first enabled CRTC
// containing root point (`x`, `y`) in X pixels (design note frame-source,
// section 3): the mode's pixel clock over the pixels a refresh scans. Zero
// without RandR, outside every CRTC, or for a rate outside 1 to 1000 Hz.
// UI thread.
[[nodiscard]] FrameInterval x11_refresh_period_at(Display *display, int x, int y);
// The run's open operation (LinuxOpenWindow). Only the UI thread reaches the
// windowing, and only while the run lives.
struct RunOpenState final {
  const stxlib::ThreadIdentity thread{stxlib::current_thread()};
  X11Windowing *windowing{};
};
struct RunOpen final {
  std::weak_ptr<RunOpenState> owner{};
  HostWindowAdmission operator()(LinuxApplicationWindow entry) const;
};
// Ends the windowing when run_linux_application leaves its run, on every
// path: revokes the open operation, tells the program, shuts the table down.
class WindowingEndGuard final {
public:
  WindowingEndGuard(RunOpenState *state, LinuxApplicationOptions *options,
                    X11Windowing *windowing) noexcept
      : state_(state), options_(options), windowing_(windowing) {}
  WindowingEndGuard(const WindowingEndGuard &) = delete;
  WindowingEndGuard &operator=(const WindowingEndGuard &) = delete;
  ~WindowingEndGuard();

private:
  RunOpenState *state_;
  LinuxApplicationOptions *options_;
  X11Windowing *windowing_;
};
} // namespace gui_forms::host::linux_detail
