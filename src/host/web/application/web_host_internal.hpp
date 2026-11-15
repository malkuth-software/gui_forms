#pragma once
// The browser host's program-thread half (web_host.cpp, web_services.cpp,
// web_dialogs.cpp). The page half is web_host.js, on the browser's main
// thread; the two meet in the page queue (page/web_page_queue.hpp) and the
// present record (page/web_present.hpp).
//
// Threads: every type here lives on the program's thread (main under
// PROXY_TO_PTHREAD), the run's UI thread. Only the wake handlers it installs
// are called from other threads, and they only wake the page queue.
#include "../desk/web_desk.hpp"
#include "../input/web_input_rules.hpp"
#include "../page/web_page_queue.hpp"
#include "../page/web_present.hpp"
#include "../../../render/skia/raster/skia_raster.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/platform/web_host.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// web_host.js, the page side (Emscripten --js-library). Each is proxied to
// the page's main thread: "sync" ones block the program's thread until they
// return (they take strings, read before they return), "async" ones are
// queued and return at once.
extern "C" {
// sync: binds the canvas, installs the page's listeners, gives the record's
// address to the page, and writes the canvas's CSS size and
// devicePixelRatio.
void gf_web_js_attach(gui_forms::host::web_detail::WebPresentFrame* frame, double* width, double* height,
                      double* scale);
// async: asks for an animation frame that presents the record.
void gf_web_js_request_present();
// async: whether each animation frame posts PageRecordKind::frame.
void gf_web_js_want_frames(int wanted);
// sync: the document's title (UTF-8).
void gf_web_js_set_title(const char* title);
// async: the canvas's CSS cursor (a CursorKind), or none while hidden.
void gf_web_js_set_cursor(int kind, int hidden);
// async: setPointerCapture / releasePointerCapture on the canvas.
void gf_web_js_pointer_capture(int captured, int pointer);
// sync: navigator.clipboard.writeText, and the page's copy of the text.
void gf_web_js_write_clipboard(const char* text);
// async: places the hidden input element at the caret (CSS pixels of the
// canvas) and says whether a text control has the keyboard there.
void gf_web_js_place_input(double x, double y, double width, double height, int editing);
// async: the canvas's parent enters or leaves full screen.
void gf_web_js_full_screen();
// sync: opens the browser's file chooser for request `request`; `accept` is
// an input element accept list. The choice arrives as a dialog record.
void gf_web_js_open_files(double request, const char* accept, int multiple);
// sync: asks where `name` is saved (the browser's save chooser where it has
// one, else a download), for request `request`; the file the program then
// closes at `path` is written there. The answer arrives as a dialog record.
void gf_web_js_save_file(double request, const char* path, const char* name, const char* accept);
// sync: the browser's colour chooser for request `request`, starting at
// 0xRRGGBB. The answer arrives as a dialog record ("#rrggbb").
void gf_web_js_choose_color(double request, unsigned int rgb);
}

namespace gui_forms::host::web_detail {

class Runtime;
class NativeWindow;

// The wake every model's handlers call, from any thread.
struct Wake final {
    void operator()() const noexcept;
};

// A window request published to the program (show, hide, close, full
// screen), callable from any thread: queued, and run by the next turn.
enum class ActionKind : std::uint8_t { close, show, hide, fullscreen };
struct Action final {
    std::weak_ptr<NativeWindow> window{};
    ActionKind kind{ActionKind::close};
};
struct ActionQueue final {
    std::mutex mutex;
    std::deque<Action> pending;
    void post(Action action);
};

// The state the page reports to every window: it composites with alpha, so
// the transparent backdrop is supported and the materials are not.
[[nodiscard]] CompositorState web_compositor_state();

// The fonts the run's rasters read: /gui_forms/fonts in the program's file
// system (the page's font packs mount there), or GUI_FORMS_FONT_DIR.
[[nodiscard]] std::filesystem::path fonts_directory();
// Every raster borrows the process's one bundled font set (as on the
// desktop hosts). Throws when the required faces are missing.
void register_fonts(render::SkiaRaster& raster);

// The run's windowing: the desk's layers (web_desk.hpp) behind the portable
// seam.
class WebWindowing final : public HostWindowing {
public:
    explicit WebWindowing(Runtime& runtime);
    // Creates one window: admission, the desk layer, attachment and, for a
    // runtime window, its ready callbacks.
    HostWindowAdmission open(WebApplicationWindow entry, bool runtime);
    // Reports the window's frame after a desk change.
    void report_native_frame(NativeWindow& window);
    // Popups and Splash windows are layers; they are translucent, since the
    // page composites premultiplied alpha.
    [[nodiscard]] bool supports_popup_windows() const noexcept override;
    [[nodiscard]] bool supports_splash_windows() const noexcept override;
    [[nodiscard]] bool transient_windows_translucent() const noexcept override;
    // run_modal's nested loop: Runtime::step turns on the program's thread,
    // which may block, with the desk's modal blocking.
    [[nodiscard]] bool supports_modal_windows() const noexcept override;

protected:
    HostWindowFrameResult frame_impl(HostWindowId window) override;
    HostServiceStatus request_frame_impl(HostWindowId window, Rect content) override;
    HostScreenPointResult pointer_position_impl() override;
    HostWindowAtResult window_at_impl(DesktopLogicalPoint point) override;
    HostServiceStatus bring_to_front_impl(HostWindowId window) override;
    HostServiceStatus destroy_window_impl(HostWindowId window) override;
    HostMonitorResult query_monitors_impl() override;
    HostServiceStatus show_impl(HostWindowId window) override;
    HostServiceStatus hide_impl(HostWindowId window) override;
    HostServiceStatus run_modal_impl(HostWindowId window) override;

private:
    // Owner of this object (observing).
    Runtime& runtime_;
};

class Services final : public HostServices {
public:
    explicit Services(NativeWindow& window);
    ~Services() override;

protected:
    HostMonitorResult query_monitors_impl() override;
    HostServiceStatus set_cursor_impl(CursorKind cursor) override;
    // The transparent backdrop presents the layer's alpha; opaque draws it
    // opaque; the materials are refused (backend_failure, changing nothing).
    HostServiceStatus apply_window_backdrop_impl(WindowBackdrop backdrop) override;
    HostServiceStatus set_pointer_capture_impl(bool captured, std::uint64_t pointer_id) override;
    // The page's copy of the clipboard: a browser grants no synchronous
    // read, so the page records the text of every paste and copy event it
    // sees (PageRecordKind::clipboard) and of the program's own writes.
    HostClipboardTextResult read_clipboard_text_impl() override;
    HostServiceStatus write_clipboard_text_impl(std::string_view text) override;
    HostDialogResult show_dialog_impl(const HostDialogRequest& request) override;
    HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest& request) override;

private:
    NativeWindow& window_;
};

// The frame around an owned window's layer: its title and close glyph,
// painted by GUI.Forms controls into a raster of their own (the title bar
// strip above the client area); the border is a solid quad. No input
// reaches it: the desk hit-tests the title bar and the close box.
class FrameChrome final {
public:
    FrameChrome(Runtime& runtime, std::string_view title, double width, double scale);
    ~FrameChrome();
    FrameChrome(const FrameChrome&) = delete;
    FrameChrome& operator=(const FrameChrome&) = delete;
    void set_title(std::string_view title);
    void set_active(bool active);
    // The strip's logical width (the frame's outer width) and scale.
    void resize(double width, double scale);
    // Paints its damage into the raster, as NativeWindow::paint does, and
    // appends the device rectangles to upload; returns whether any changed.
    bool paint(std::vector<WebDeviceRect>& uploaded, bool& resized);
    [[nodiscard]] std::uint32_t surface() const noexcept { return surface_; }
    [[nodiscard]] const render::SkiaRaster& raster() const& noexcept { return raster_; }
    const render::SkiaRaster& raster() const&& = delete;

private:
    HostDispatchResult dispatch(HostEventPayload payload);

    // Each member is declared after what it borrows: the model paints with
    // the raster, the session holds the model.
    render::SkiaRaster raster_;
    std::shared_ptr<Panel> root_;
    std::shared_ptr<Label> title_;
    std::shared_ptr<Label> close_;
    std::unique_ptr<Window> model_;
    std::unique_ptr<HostSession> session_;
    DamageRegion damage_;
    Size size_;
    double scale_{1.0};
    std::uint32_t surface_{};
    std::uint64_t sequence_{};
    bool active_{};
    bool raster_resized_{true};
};

class NativeWindow final : public std::enable_shared_from_this<NativeWindow> {
public:
    NativeWindow(Runtime& runtime, WebApplicationWindow entry, HostWindowId window_id, Rect content, bool initial);
    ~NativeWindow();
    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    void attach();
    void ready();
    void action(ActionKind kind);
    // Frame sequence, gather and paint (as the X11 host's update): the
    // posted work, the schedule, the live frames and the damage; while
    // `may_paint` (no frame is waiting for the page) the raster is painted
    // and `uploaded` holds the device rectangles that changed.
    void update(bool may_paint);
    void close(HostCloseReason reason, bool force = false);
    HostDispatchResult dispatch(HostEventPayload payload);
    // The desk moved or sized the client area (a user's drag, a frame
    // request, the canvas's resize for the primary window).
    void apply_content(Rect content);
    // A new canvas scale: the whole window re-rasters.
    void apply_scale(double next);
    void set_visible(bool shown);
    void set_active(bool active);
    void pointer(PointerEvent event);
    void update_cursor(Point position);
    // The client area's least size: Window's minimum where written, else
    // the option's.
    [[nodiscard]] Size minimum_size() const noexcept;
    [[nodiscard]] HostWindowFrame frame() const;
    [[nodiscard]] bool has_present_work() const noexcept;
    // Whether this window asks for animation frames (a live surface whose
    // clock is not resting).
    [[nodiscard]] bool wants_frames() const noexcept;

    Runtime& runtime;
    WebApplicationWindow entry;
    HostWindowId window_id{};
    // Initial windows keep their state until the run returns; runtime
    // windows are released by Runtime::reclaim after they close and after
    // the page has let go of their pixels.
    bool initial{true};
    // The client area in canvas logical units, and the canvas's scale.
    Rect content{};
    double scale{1.0};
    bool closed{};
    bool visible{};
    bool active{};
    bool composing{};
    // The layer presents the raster's alpha (a granted transparent
    // backdrop); popups always do.
    bool present_alpha{};
    std::unique_ptr<Services> services;
    std::unique_ptr<HostSession> session;
    render::SkiaRaster raster;
    std::uint32_t surface{};
    // The raster was resized since the page last uploaded it: it uploads
    // whole.
    bool raster_resized{true};
    DamageRegion damage;
    // This turn's device rectangles to upload (web_device_rects).
    std::vector<WebDeviceRect> uploaded;
    std::unique_ptr<FrameChrome> chrome;
    std::vector<WebDeviceRect> chrome_uploaded;
    bool chrome_resized{};
    // Scroll copies (SurfaceRoot::take_scroll_copies), as the X11 host keeps
    // them.
    std::vector<ScrollCopy> pending_copies;
    std::vector<ScrollCopy> taken_copies;
    std::vector<Rect> applied_copy_clips;
    // The live frames, as the X11 host keeps them (linux_host_internal.hpp).
    std::vector<LiveSurfacePresentation> live_updates;
    std::vector<LiveSurfaceFrame> live_frames;
    std::vector<std::size_t> live_frame_sources;
    std::vector<Rect> live_regions;
    std::vector<std::uint64_t> live_frame_pixels;
    std::vector<std::uint64_t> live_frame_rectangles;
    bool live_region_lost{};
    bool live_idle{};
    SubscriptionToken closing_token;
    std::uint64_t sequence{};
    WebClickHistory click_history{};
    CursorKind current_cursor{CursorKind::arrow};

private:
    bool gather_live_frames();
    void compose_live_frames();
    bool report_presented(const std::optional<PaintReceipt>& receipt, std::uint64_t elapsed, PresentedArea copied);
    void paint(DamageRegion& screen, bool regions_taken);
};

// One browser dialog under way (open, save, colour): the program's thread
// runs nested turns until the page's dialog record names it.
struct PendingDialog final {
    std::uint64_t request{};
    bool done{};
    PageDialogOutcome outcome{PageDialogOutcome::cancelled};
    std::string text{};
    std::vector<std::string> paths{};
};

class Runtime final {
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    std::shared_ptr<NativeWindow> add(WebApplicationWindow entry, HostWindowId window_id, Rect content,
                                      bool initial);
    // One loop turn: waits for page records, a wake or the next deadline
    // (not at all when work is owed), handles every record, updates every
    // window, and hands a frame to the page when one is due and the last
    // one was presented.
    void step();
    // Releases closed runtime windows once the page holds no frame of
    // theirs.
    void reclaim();
    [[nodiscard]] NativeWindow* window(HostWindowId window_id) noexcept;
    [[nodiscard]] bool primary_open() const noexcept;
    // The window the keyboard goes to: the desk's active window.
    [[nodiscard]] NativeWindow* keyboard_window() noexcept;
    // Moves the keyboard to `window` (the desk decides what is allowed) and
    // tells the windows that gained and lost it.
    void activate(HostWindowId window, bool takes_focus = true);
    void activate_topmost();
    // A layer was moved, shown, hidden or restacked: the next turn presents.
    void scene_changed() noexcept { scene_dirty_ = true; }
    // A surface identity, never reused in the run.
    [[nodiscard]] std::uint32_t allocate_surface() noexcept { return ++next_surface_; }
    // A desk identity for a host dialog's layer, outside the windowing
    // table's range (its identities count up from 1).
    [[nodiscard]] HostWindowId allocate_private_window() noexcept {
        ++private_windows_;
        const HostWindowId id{private_window_base + private_windows_};
        return id;
    }
    static constexpr std::uint64_t private_window_base = 1ULL << 62U;
    // The page deletes the surface's texture with the next frame.
    void release_surface(std::uint32_t surface);
    // Waits in nested turns until the page answers `request` (a browser
    // dialog), or the run ends.
    PendingDialog wait_for_dialog(std::uint64_t request);
    // The next dialog request number for the page.
    [[nodiscard]] std::uint64_t next_dialog_request() noexcept { return ++dialog_requests_; }

    PageQueue& queue;
    WebDesk desk;
    std::shared_ptr<ActionQueue> actions;
    std::unique_ptr<WebWindowing> windowing;
    std::vector<std::shared_ptr<NativeWindow>> windows;
    // The canvas: its logical size and device scale.
    Size extent{1.0, 1.0};
    double scale{1.0};
    bool page_focused{};
    bool page_hidden{};
    // Shared with the page; allocated once and never freed (the page may
    // read it until the document goes away).
    WebPresentFrame* frame{};
    // The page's clipboard copy (Services::read_clipboard_text_impl).
    std::string clipboard_text;
    std::uint64_t clipboard_generation{};
    // The last pointer position on the canvas, and the window it was over.
    Point pointer_position{};
    HostWindowId pointer_window{};
    // The window a press went to: it keeps the pointer until the release.
    HostWindowId pressed_window{};
    // The window whose close box was pressed, closed if released there.
    HostWindowId close_pressed{};
    bool quitting{};

private:
    void handle(PageRecord& record);
    void handle_pointer(const PageRecord& record);
    void handle_wheel(const PageRecord& record);
    void handle_key(const PageRecord& record);
    void handle_text(const PageRecord& record, bool composition);
    void handle_resize(const PageRecord& record);
    void handle_drop(const PageRecord& record);
    // The client window and position a canvas point reaches for pointer
    // input: the pressed window while a press lasts, else the desk's hit.
    [[nodiscard]] WebDeskHit pointer_target(Point point) const noexcept;
    void present();
    void place_input();
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline(bool& immediate);

    std::deque<PageRecord> records_;
    std::vector<WebDeskQuad> quads_;
    std::vector<std::uint32_t> released_;
    std::vector<PendingDialog*> dialogs_;
    std::uint32_t next_surface_{};
    std::uint64_t dialog_requests_{};
    std::uint64_t drag_sessions_{};
    std::uint64_t private_windows_{};
    bool scene_dirty_{true};
    bool frames_wanted_{};
    bool presented_once_{};
    // The caret last given to the page (place_input).
    std::optional<Rect> placed_caret_{};
    bool placed_editing_{};
};

// The message box the host draws (web_dialogs.cpp): a modal layer over its
// owner, run with nested turns.
HostDialogResult message_dialog(NativeWindow& owner, const HostDialogRequest& request,
                                const HostMessageDialogRequest& message);
// The browser's file, save and colour choosers (web_dialogs.cpp).
HostDialogResult browser_dialog(NativeWindow& owner, const HostDialogRequest& request);

// The run's open operation (WebOpenWindow), as RunOpen on Linux.
struct RunOpenState final {
    const std::thread::id thread{std::this_thread::get_id()};
    WebWindowing* windowing{};
};
struct RunOpen final {
    std::weak_ptr<RunOpenState> owner{};
    HostWindowAdmission operator()(WebApplicationWindow entry) const;
};
// Ends the windowing when run_web_application leaves its run, on every
// path: revokes the open operation, tells the program, shuts the table down.
class WindowingEndGuard final {
public:
    WindowingEndGuard(RunOpenState* state, WebApplicationOptions* options, WebWindowing* windowing) noexcept
        : state_(state), options_(options), windowing_(windowing) {}
    WindowingEndGuard(const WindowingEndGuard&) = delete;
    WindowingEndGuard& operator=(const WindowingEndGuard&) = delete;
    ~WindowingEndGuard();

private:
    RunOpenState* state_;
    WebApplicationOptions* options_;
    WebWindowing* windowing_;
};

} // namespace gui_forms::host::web_detail
