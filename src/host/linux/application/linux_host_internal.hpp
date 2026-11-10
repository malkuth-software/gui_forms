#pragma once
#include "../accessibility/linux_accessibility.hpp"
#include "../../../render/skia/raster/skia_raster.hpp"
#include "../../../render/skia/raster/skia_splash_bake.hpp"
#include "../services/x11_tray_icons.hpp"
#include "../compositor/x11_compositor.hpp"
#include "../settings/xsettings_watch.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/platform/linux_host.hpp"
#include <X11/Xlib.h>
#include <signal.h>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>

namespace gui_forms::host::linux_detail {
class Runtime;
class NativeWindow;
// Application-scope windowing on X11 (stage DK phase W0, decisions 6 and 7;
// x11_windowing.cpp). The X server's desktop is in device pixels and this
// host runs at scale 1, so desktop logical units are pixels. Window
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
DragFileListData local_files(const std::vector<std::byte>& data);
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
class Services final : public HostServices {
public:
  explicit Services(NativeWindow &window);
  ~Services() override;
  // Offers an X event to the tray icons; true when one of them took it.
  bool tray_event(const XEvent &event);

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
  HostClipboardTextResult read_clipboard_text_impl() override;
  HostServiceStatus write_clipboard_text_impl(std::string_view text) override;
  HostClipboardImageResult read_clipboard_image_impl() override;
  HostClipboardFilesResult read_clipboard_files_impl() override;
  HostServiceStatus write_clipboard_image_impl(HostImageView image) override;
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
  std::vector<CursorEntry> cursors_;
  NativeWindow &window_;
  // Created with the first icon.
  std::unique_ptr<X11TrayIcons> tray_;
};
struct Wake final {
  int descriptor{-1};
  void operator()() const noexcept;
};
enum class ActionKind { close, show, hide, fullscreen };
struct Action final {
  std::weak_ptr<NativeWindow> window;
  ActionKind kind;
};
struct ActionQueue final {
  std::mutex mutex;
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
  bool drag_message(const XClientMessageEvent &message);
  void action(ActionKind kind);
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
  // Reports one present that copied `copied`: the live frames' share as live
  // frames, the rest as the retained present (when `receipt` is set), the
  // duration split by pixels. Returns whether the window accepted the receipt.
  bool report_presented(const std::optional<PaintReceipt>& receipt, std::uint64_t elapsed,
                        PresentedArea copied);
  HostDispatchResult dispatch(HostEventPayload payload);
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
  // The window's frame in desktop units (pixels): the client area at its
  // root position, and the outer frame from _NET_FRAME_EXTENTS when the
  // window manager publishes it.
  HostWindowFrame frame() const;
  // Asks the window manager for a client rectangle (static gravity, so the
  // position is the client area's, not the frame's).
  HostServiceStatus request_frame(Rect content);
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
  GC gc{};
  XIC input{};
  Size size;
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
  SubscriptionToken closing_token;
  std::uint64_t sequence{};
  // The last press, for the computed click count (linux_input_rules.hpp).
  LinuxClickHistory click_history{};
  std::vector<char> presentation;
  // The invalidation overlay's present-time storage (gui_forms/diagnostics/
  // invalidation_overlay), reused: the scratch frame the outlines are
  // composited in (sized on first use), the device areas they cover now and
  // those they covered at the last present, and the rectangles a present
  // copies with them. Window pixels are device pixels here (scale 1).
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
  std::vector<std::size_t> live_frame_sources;
  std::vector<Rect> live_regions;
  std::vector<std::uint64_t> live_frame_pixels;
  std::vector<std::uint64_t> live_frame_rectangles;
  // A taken presentation region that was never presented: the next tick
  // repaints whole clips.
  bool live_region_lost{};
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
  // Reports the current monitors to every attached window
  // (HostDisplayEvent) after RandR announced a screen change (audit N66).
  void report_displays();
  void dispatch(XEvent &event);
  Atom atom(const char *name) const;
  // RandR's first event number, where the build has Xrandr and the server
  // offers it; -1 otherwise, and no screen change is reported.
  int randr_event_base{-1};
  Display *display{};
  std::array<std::uint32_t, 256> physical_keys{};
  XIM input_method{};
  int wake_pipe[2]{-1, -1};
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
};
HostDialogResult retained_dialog(NativeWindow &owner,
                                 const HostDialogRequest &request);
// The display's monitors into `result` (audit N66): RandR 1.5 monitors where
// the build has Xrandr and the server offers them, otherwise the default
// screen as one monitor; each work area is the frame within _NET_WORKAREA.
// Every scale is 1, the X11 host's rendering scale. UI thread.
void query_x11_monitors(const Runtime &runtime, HostMonitorResult &result);
// The run's open operation (LinuxOpenWindow). Only the UI thread reaches the
// windowing, and only while the run lives.
struct RunOpenState final {
  const std::thread::id thread{std::this_thread::get_id()};
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
