#pragma once
// The single-surface window manager (owner decision, 2026-11-15):
// surface_manager.cpp (the run, its windows and the compositing),
// surface_windowing.cpp, surface_services.cpp and surface_dialogs.cpp. The
// host supplies one surface, its input and a present call
// (surface_host.hpp); everything else is here, in portable C++.
//
// Shape:
// - Every window of the run paints into its own CPU Skia raster, as on the
//   desktop hosts. The desk (desk/surface_desk.hpp) keeps the layers.
// - Each turn the manager composes the damaged part of the scene into the
//   host's surface (compose/surface_compose.hpp): the frames' edges, their
//   title strips (small GUI.Forms windows, FrameChrome), the client
//   rasters, the minimised windows' tabs and, where the host has no cursor,
//   a drawn one. Then it presents the damaged device rectangles.
// - Damage is limited: a window's paint damages its own rectangles, a
//   move or restack the old and new frames, nothing is composed while
//   nothing changed, and nothing animates without a live surface asking
//   for frames.
//
// Threads: every type here lives on the run's thread. Only the wake
// handlers it installs are called from other threads, and they only wake
// the host's input queue.
#include "surface_host.hpp"
#include "../desk/surface_desk.hpp"
#include "../../../render/skia/raster/skia_raster.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/window_capsule.hpp"

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

namespace gui_forms::host::surface_detail {

class Runtime;
class NativeWindow;

// The wake every model's handlers call, from any thread. `queue` is the
// host's, which outlives the handlers.
struct Wake final {
    SurfaceInputQueue* queue{};
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
    explicit ActionQueue(SurfaceInputQueue& wakes) : input(&wakes) {}
    SurfaceInputQueue* input;
    std::mutex mutex;
    std::deque<Action> pending;
    void post(Action action);
};

// The state the manager reports to every window: it composes with alpha,
// so the transparent backdrop is supported and the materials are not.
[[nodiscard]] CompositorState surface_compositor_state();

// Every raster borrows the run's one bundled font set (as on the desktop
// hosts). Throws when the faces cannot be registered.
void register_fonts(render::SkiaRaster& raster, const std::shared_ptr<gui_forms::detail::BundledFontSet>& fonts);

// The run's windowing: the desk's layers behind the portable seam.
class SurfaceWindowing final : public HostWindowing {
public:
    explicit SurfaceWindowing(Runtime& runtime);
    // Creates one window: admission, the desk layer, attachment and, for a
    // runtime window, its ready callbacks.
    HostWindowAdmission open(SurfaceApplicationWindow entry, bool runtime);
    // Reports the window's frame after a desk change.
    void report_native_frame(NativeWindow& window);
    // Popups and Splash windows are layers; they are translucent, since the
    // manager composes premultiplied alpha.
    [[nodiscard]] bool supports_popup_windows() const noexcept override;
    [[nodiscard]] bool supports_splash_windows() const noexcept override;
    [[nodiscard]] bool transient_windows_translucent() const noexcept override;
    // run_modal's nested loop: Runtime::step turns on the run's thread,
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
    // The transparent backdrop composes the layer's alpha; opaque copies it;
    // the materials are refused (backend_failure, changing nothing).
    HostServiceStatus apply_window_backdrop_impl(WindowBackdrop backdrop) override;
    HostServiceStatus set_pointer_capture_impl(bool captured, std::uint64_t pointer_id) override;
    // The manager's copy of the clipboard: the host posts the text it sees
    // (SurfaceInputKind::clipboard), and the program's own writes are kept.
    HostClipboardTextResult read_clipboard_text_impl() override;
    HostServiceStatus write_clipboard_text_impl(std::string_view text) override;
    HostDialogResult show_dialog_impl(const HostDialogRequest& request) override;
    HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest& request) override;

private:
    NativeWindow& window_;
};

// The capsule section a desk part is, or none.
[[nodiscard]] WindowCapsuleSection capsule_section(SurfaceDeskPart part) noexcept;

// The call point of the capsule's help (WINDOW_MANAGER_SPEC: the business
// card and the diagnostic overlay, unified across GUI.Forms programs).
// `open` is called on the manager's thread with `context` and the window
// whose capsule asked; `context` must outlive the run or be cleared first.
// Without one the manager shows the business card in the hand over the
// window (show_business_card), or, for a window without a model, the
// capsule's own help (window_capsule_help_text) in a message box.
struct SurfaceCapsuleHelpTarget final {
    void* context{};
    void (*open)(void* context, HostWindowId window){};
};

// The title strip of a framed window: its title and the window controls
// (the minimise, maximise or restore and close glyphs, or the window
// capsule), painted by GUI.Forms controls into a raster of its own, in the
// desk theme's colours. The edge is a solid layer. No input reaches it:
// the desk hit-tests the title bar, its boxes and the capsule.
class FrameChrome final {
public:
    FrameChrome(Runtime& runtime, std::string_view title, double width, double scale);
    ~FrameChrome();
    FrameChrome(const FrameChrome&) = delete;
    FrameChrome& operator=(const FrameChrome&) = delete;
    void set_title(std::string_view title);
    void set_active(bool active);
    // Which boxes the strip shows, and whether the maximise box shows the
    // restore glyph.
    void set_boxes(bool minimize, bool maximize, bool maximized);
    // Whether the strip shows the capsule (then no boxes), its look, and
    // whether the title shows (with the capsule, only while its question
    // mark is hovered).
    void set_capsule(bool shown, const WindowCapsuleLook& look, bool title_shown);
    // The strip's logical width (the client width) and scale.
    void resize(double width, double scale);
    // Paints its damage into the raster, as NativeWindow::paint does, and
    // appends the painted rectangles (strip logical units) to `painted`;
    // returns whether any changed. `resized` reports a new raster.
    bool paint(std::vector<Rect>& painted, bool& resized);
    [[nodiscard]] const render::SkiaRaster& raster() const& noexcept { return raster_; }
    const render::SkiaRaster& raster() const&& = delete;

private:
    HostDispatchResult dispatch(HostEventPayload payload);
    void layout();

    // Owner of this object (observing).
    Runtime& runtime_;
    // Each member is declared after what it borrows: the model paints with
    // the raster, the session holds the model.
    render::SkiaRaster raster_;
    std::shared_ptr<Panel> root_;
    std::shared_ptr<Label> title_;
    std::shared_ptr<Label> minimize_;
    std::shared_ptr<Label> maximize_;
    std::shared_ptr<Label> close_;
    // The window capsule control, not interactive: the desk hit-tests it and
    // the manager's gesture gives it its look.
    std::shared_ptr<WindowCapsule> capsule_;
    std::unique_ptr<Window> model_;
    std::unique_ptr<HostSession> session_;
    DamageRegion damage_;
    Size size_;
    double scale_{1.0};
    std::uint64_t sequence_{};
    bool active_{};
    bool shows_minimize_{};
    bool shows_maximize_{};
    bool maximized_{};
    bool capsule_shown_{};
    bool title_shown_{true};
    std::string title_text_;
    bool raster_resized_{true};
};

class NativeWindow final : public std::enable_shared_from_this<NativeWindow> {
public:
    NativeWindow(Runtime& runtime, SurfaceApplicationWindow entry, HostWindowId window_id, Rect content,
                 bool initial);
    ~NativeWindow();
    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    void attach();
    void ready();
    void action(ActionKind kind);
    // Frame sequence, gather and paint (as the X11 host's update): the
    // posted work, the schedule, the live frames and the damage; the raster
    // is painted and `composed` gains the rectangles that changed.
    void update();
    void close(HostCloseReason reason, bool force = false);
    HostDispatchResult dispatch(HostEventPayload payload);
    // The desk moved or sized the client area (a user's drag, a frame
    // request, a state change, the surface's resize for the primary
    // window).
    void apply_content(Rect content);
    // A new surface scale: the whole window re-rasters.
    void apply_scale(double next);
    void set_visible(bool shown);
    void set_active(bool active);
    // The title strip's boxes follow the desk's state.
    void refresh_chrome();
    void pointer(PointerEvent event);
    void update_cursor(Point position);
    // The client area's least size.
    [[nodiscard]] Size minimum_size() const noexcept;
    [[nodiscard]] HostWindowFrame frame() const;
    [[nodiscard]] bool has_compose_work() const noexcept;
    // Whether this window asks for display frames (a live surface whose
    // clock is not resting).
    [[nodiscard]] bool wants_frames() const noexcept;

    Runtime& runtime;
    SurfaceApplicationWindow entry;
    HostWindowId window_id{};
    // Initial windows keep their state until the run returns; runtime
    // windows are released by Runtime::reclaim after they close.
    bool initial{true};
    // The client area in surface logical units, and the surface's scale.
    Rect content{};
    double scale{1.0};
    bool closed{};
    bool visible{};
    bool active{};
    bool composing{};
    // The layer composes the raster's alpha (a granted transparent
    // backdrop); popups always do.
    bool present_alpha{};
    std::unique_ptr<Services> services;
    std::unique_ptr<HostSession> session;
    render::SkiaRaster raster;
    // The whole layer changed (a new raster, a new backdrop) since the last
    // compose.
    bool raster_resized{true};
    DamageRegion damage;
    // The rectangles painted since the last compose, client logical units.
    std::vector<Rect> composed;
    std::unique_ptr<FrameChrome> chrome;
    // The strip's painted rectangles since the last compose, strip units.
    std::vector<Rect> chrome_composed;
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
    SurfaceClickHistory click_history{};
    CursorKind current_cursor{CursorKind::arrow};

private:
    bool gather_live_frames();
    void compose_live_frames();
    bool report_presented(const std::optional<PaintReceipt>& receipt, std::uint64_t elapsed, PresentedArea copied);
    void paint(DamageRegion& screen, bool regions_taken);
};

// One host dialog under way (open, save, colour): the run's thread runs
// nested turns until the host's dialog record names it.
struct PendingDialog final {
    std::uint64_t ticket{};
    bool done{};
    SurfaceDialogOutcome outcome{SurfaceDialogOutcome::cancelled};
    std::string text{};
    std::vector<std::string> paths{};
};

// A press on a title bar box or a tab, acted on at its release over the
// same part.
struct PressedPart final {
    HostWindowId window{};
    SurfaceDeskPart part{SurfaceDeskPart::none};
};

class Runtime final {
public:
    Runtime(SurfaceHost& host, std::shared_ptr<gui_forms::detail::BundledFontSet> fonts);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    std::shared_ptr<NativeWindow> add(SurfaceApplicationWindow entry, HostWindowId window_id, Rect content,
                                      bool initial);
    // One loop turn: waits for input, a wake or the next deadline (not at
    // all when work is owed), handles every record, updates every window,
    // and composes and presents the surface's damage unless the host still
    // holds the last present.
    void step();
    // Releases closed runtime windows.
    void reclaim();
    [[nodiscard]] NativeWindow* window(HostWindowId window_id) noexcept;
    [[nodiscard]] bool primary_open() const noexcept;
    // The window the keyboard goes to: the desk's active window.
    [[nodiscard]] NativeWindow* keyboard_window() noexcept;
    // Moves the keyboard to `window` (the desk decides what is allowed) and
    // tells the windows that gained and lost it.
    void activate(HostWindowId window, bool takes_focus = true);
    void activate_topmost();
    // Window states through the desk, with the window's content, its
    // strip and the windowing's reports following. Each returns whether
    // the state changed.
    bool minimize_window(HostWindowId window);
    bool maximize_window(HostWindowId window);
    bool restore_window(HostWindowId window);
    bool tile_window(HostWindowId window, SurfaceTileSide side);
    // Maximises a normal or tiled window and restores a maximised one.
    bool toggle_maximized(HostWindowId window);
    // Carries out one of the capsule's commands and shows its look.
    void capsule_outcome(const WindowCapsuleOutcome& outcome);
    // Pushes the capsule's look to the strip of its window, and the resting
    // look to the strip that showed it before.
    void show_capsule_look();
    // The capsule's help (capsule_help; else the business card in the hand
    // over the window, or the capsule's own help in a message box for a
    // window without a model).
    void open_capsule_help(HostWindowId window);
    // `window` closed, was minimised or lost the keyboard: its capsule
    // forgets every gesture.
    void forget_capsule(HostWindowId window);
    // A layer was moved, shown, hidden, restacked or changed state: the
    // next turn composes the difference.
    void scene_changed() noexcept { scene_dirty_ = true; }
    // The cursor the window under the pointer asks for: the host's, or the
    // drawn one.
    void show_cursor(CursorKind cursor);
    // A desk identity for a host dialog's layer, outside the windowing
    // table's range (its identities count up from 1).
    [[nodiscard]] HostWindowId allocate_private_window() noexcept {
        ++private_windows_;
        const HostWindowId id{private_window_base + private_windows_};
        return id;
    }
    static constexpr std::uint64_t private_window_base = 1ULL << 62U;
    // Waits in nested turns until the host answers `ticket` (a host
    // dialog), or the run ends.
    PendingDialog wait_for_dialog(std::uint64_t ticket);
    [[nodiscard]] std::uint64_t next_dialog_ticket() noexcept { return ++dialog_tickets_; }
    // The surface's whole content is composed on the next turn.
    void damage_surface() noexcept;

    SurfaceHost& host;
    SurfaceInputQueue& queue;
    const std::shared_ptr<gui_forms::detail::BundledFontSet> fonts;
    SurfaceDesk desk;
    std::shared_ptr<ActionQueue> actions;
    std::unique_ptr<SurfaceWindowing> windowing;
    std::vector<std::shared_ptr<NativeWindow>> windows;
    // The surface: its logical size and device scale.
    Size extent{1.0, 1.0};
    double scale{1.0};
    bool host_focused{true};
    bool host_hidden{};
    // The clipboard copy (Services::read_clipboard_text_impl).
    std::string clipboard_text;
    std::uint64_t clipboard_generation{};
    // The last pointer position on the surface, and the window it was over.
    Point pointer_position{};
    HostWindowId pointer_window{};
    // The window a press went to: it keeps the pointer until the release.
    HostWindowId pressed_window{};
    PressedPart pressed_part{};
    // The window capsule's gestures (one at a time across the run), and
    // the call point of its help (SurfaceCapsuleHelpTarget), empty until
    // the business card and diagnostic overlay task attaches it.
    WindowCapsuleGesture capsule;
    SurfaceCapsuleHelpTarget capsule_help{};
    bool quitting{};

private:
    void handle(SurfaceInputRecord& record);
    void handle_pointer(const SurfaceInputRecord& record);
    void handle_frame_press(const SurfaceInputRecord& record, const SurfaceDeskHit& hit);
    // Pointer input while a capsule press lasts; returns whether it took
    // the record.
    bool handle_capsule_pointer(const SurfaceInputRecord& record);
    // Moves the capsule's animation on by the time since the last turn.
    void advance_capsule();
    // Whether the capsule's window asks for reduced motion.
    [[nodiscard]] bool capsule_reduced_motion() noexcept;
    // The snap target a capsule move shows, or empty.
    [[nodiscard]] Rect snap_preview_rect() const noexcept;
    void handle_wheel(const SurfaceInputRecord& record);
    void handle_key(const SurfaceInputRecord& record);
    void handle_text(const SurfaceInputRecord& record, bool composition);
    void handle_resize(const SurfaceInputRecord& record);
    void handle_drop(const SurfaceInputRecord& record);
    void leave_pointer_window(Point point);
    // Tells the windows that lost and gained the keyboard after a desk
    // change that moved it.
    void activation_moved(HostWindowId before);
    // The client window and position a surface point reaches for pointer
    // input: the pressed window while a press lasts, else the desk's hit.
    [[nodiscard]] SurfaceDeskHit pointer_target(Point point) const noexcept;
    // Composes the damage into the host's surface and presents it.
    void compose();
    void gather_damage();
    void build_layers();
    [[nodiscard]] Rect cursor_rect() const noexcept;
    void place_input();
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline(bool& immediate);

    std::deque<SurfaceInputRecord> records_;
    // The scene and tabs last composed, and this turn's.
    std::vector<SurfaceDeskQuad> quads_;
    std::vector<SurfaceDeskQuad> composed_quads_;
    std::vector<SurfaceDeskTab> tabs_;
    std::vector<SurfaceDeskTab> composed_tabs_;
    // The surface's damage, logical units, until composed.
    DamageRegion surface_damage_;
    std::vector<SurfaceComposeLayer> layers_;
    std::vector<SurfaceDeviceRect> device_rects_;
    SurfaceView view_{};
    std::vector<PendingDialog*> dialogs_;
    SurfaceClickHistory title_clicks_{};
    // The drawn cursor (when the host has none): its kind, whether the
    // pointer is over the surface, its sprite, and where it was composed.
    CursorKind cursor_kind_{CursorKind::arrow};
    bool cursor_inside_{};
    SurfaceCursorSprite cursor_sprite_{};
    CursorKind cursor_sprite_kind_{CursorKind::arrow};
    std::uint32_t cursor_sprite_factor_{};
    Rect cursor_composed_{};
    // The capsule: the window whose strip shows its look, its animation's
    // clock while it runs, and the snap preview last composed.
    HostWindowId capsule_shown_window_{};
    std::chrono::steady_clock::time_point capsule_clock_{};
    bool capsule_ticking_{};
    Rect snap_composed_{};
    std::uint64_t dialog_tickets_{};
    std::uint64_t drag_sessions_{};
    std::uint64_t private_windows_{};
    bool scene_dirty_{true};
    bool frames_wanted_{};
    // The host still reads the last present (SurfacePresentResult::pending).
    bool present_pending_{};
    // Another surface holds the screen (SurfacePresentResult::inactive).
    bool surface_inactive_{};
    // The caret last given to the host (place_input).
    std::optional<Rect> placed_caret_{};
    bool placed_editing_{};
};

// The message box the manager draws (surface_dialogs.cpp): a modal layer
// over its owner, run with nested turns.
HostDialogResult message_dialog(NativeWindow& owner, const HostDialogRequest& request,
                                const HostMessageDialogRequest& message);
// The host's own file, save and colour choosers (surface_dialogs.cpp).
HostDialogResult host_dialog(NativeWindow& owner, const HostDialogRequest& request);

// The run's open operation (SurfaceOpenWindow), as RunOpen on Linux.
struct RunOpenState final {
    const std::thread::id thread{std::this_thread::get_id()};
    SurfaceWindowing* windowing{};
};
struct RunOpen final {
    std::weak_ptr<RunOpenState> owner{};
    HostWindowAdmission operator()(SurfaceApplicationWindow entry) const;
};
// Ends the windowing when the run leaves, on every path: revokes the open
// operation, tells the program, shuts the table down.
class WindowingEndGuard final {
public:
    WindowingEndGuard(RunOpenState* state, SurfaceApplicationOptions* options, SurfaceWindowing* windowing) noexcept
        : state_(state), options_(options), windowing_(windowing) {}
    WindowingEndGuard(const WindowingEndGuard&) = delete;
    WindowingEndGuard& operator=(const WindowingEndGuard&) = delete;
    ~WindowingEndGuard();

private:
    RunOpenState* state_;
    SurfaceApplicationOptions* options_;
    SurfaceWindowing* windowing_;
};

} // namespace gui_forms::host::surface_detail
