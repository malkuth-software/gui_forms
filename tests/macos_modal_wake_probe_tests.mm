#import <AppKit/AppKit.h>

#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/theme/canvas_look/canvas_look.hpp"
#include "gui_forms/timer/timer/timer.hpp"
#include "gui_forms/window/window.hpp"
#include "support/macos_handoff_probe.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

// Work that must wake the AppKit loop by itself (the macOS modal wake
// return, 2026-11-13: Plan Paint's Settings dialog stayed open after its OK
// button posted the close, until something unrelated woke the loop).
//
// Inside show_modal, opened from posted work, the dialog's ready callback
// reproduces what Plan Paint's background change started:
// - a material change: the owner's canvas paints a finish layer, whose
//   tiles are made in idle frames that the paint itself arms;
// - an image replaced from inside on_paint (deferred work of the paint);
// - a Timer restarted outside a tick, whose tick repaints the owner.
// Then nothing on the main thread is scheduled by this probe. A helper
// thread clicks the dialog's OK button with real AppKit events; the
// button's handler posts the close (begin_invoke). The modal must return
// within a bound, with the Timer having ticked, the owner having painted
// after the tick and after the replacement, and the finish tiles made.
// Then the same for an ordinary owned window: a click whose handler posts
// its close must close it within the bound.
//
// The plain window opens ordered front but not key; its click relies on
// click-through (GUIFormsView acceptsFirstMouse:), so it also works when
// the application is not active, as on a desktop covered by other
// programs' dialogs. A round whose close never comes still ends the run
// with the failure reported (finish_if_stuck), rather than leaving it to
// the watchdog.
//
// The helper thread only waits and posts events (postEvent: is admitted
// from any thread). If a bound passes it records the failure and posts one
// application-defined event, so the run ends and the failure is reported
// instead of the watchdog's timeout.

namespace {
using namespace gui_forms;

constexpr const char* owner_title = "GUI.Forms modal wake owner";
constexpr const char* modal_title = "GUI.Forms modal wake dialog";
constexpr const char* plain_title = "GUI.Forms modal wake plain";
constexpr double dialog_width = 260.0;
constexpr double dialog_height = 120.0;
// The OK button's bounds in each dialog, top-down client units.
constexpr gui_forms::Rect ok_bounds{20.0, 60.0, 120.0, 28.0};
// How long a posted close may take to close its window.
constexpr std::chrono::milliseconds close_bound{2000};

// One finish layer, as Plan Paint's felt backing becomes after the change:
// orange peel at 3 %.
CanvasLook finished_look() {
    CanvasLook look{};
    CanvasTextureLayer& layer = look.texture.items[0];
    layer.source = CanvasTextureSource::finish;
    layer.finish.kind = FinishKind::orange_peel;
    layer.finish.paint = Color::rgba(255, 255, 255);
    layer.finish.secondary = Color::rgba(200, 200, 200);
    layer.contrast = 0.03;
    look.texture.count = 1U;
    return look;
}

// The owner's canvas: its plain background, or after the change a finish
// layer; and while a replacement is owed, it replaces its image from inside
// on_paint, as Plan Paint's backing did.
class WakeCanvas final : public Control {
public:
    explicit WakeCanvas(StableId id) : Control(std::move(id)) {}

    void on_paint(Painter& painter, gui_forms::Rect local_damage) override {
        static_cast<void>(local_damage);
        ++paints;
        if (replacement_seen) ++paints_after_replacement;
        const gui_forms::Rect area = client_rectangle();
        if (finished) {
            CanvasView view{};
            paint_canvas_background(painter, *this, look, area, view);
        } else {
            painter.fill_rect(area, Color::rgba(200, 210, 220));
        }
        if (replacement_owed && image.value != 0U) {
            replacement_owed = false;
            replace_from_paint();
        }
    }

    void replace_from_paint() {
        const BorrowedPointer<Window> window = attached_window();
        if (!window) return;
        std::vector<std::byte> pixels(static_cast<std::size_t>(16U * 16U * 4U), std::byte{0x80});
        const ImageLoadResult replaced =
            (*window).replace_bgra32_premultiplied(image, 16U, 16U, 16U * 4U, pixels, *this);
        replacement_seen = static_cast<bool>(replaced);
    }

    CanvasLook look{finished_look()};
    ImageId image{};
    bool finished{};
    bool replacement_owed{};
    bool replacement_seen{};
    unsigned paints{};
    unsigned paints_after_replacement{};
};

// The helper thread's view of one click-and-close round: the native window
// number and the click point (window base coordinates), set on the main
// thread before the round starts; `closed` set on the main thread when the
// window has closed.
struct ClickRound final {
    std::atomic<NSInteger> window_number{0};
    std::atomic<double> x{0.0};
    std::atomic<double> y{0.0};
    std::atomic<bool> armed{false};
    std::atomic<bool> closed{false};
    std::atomic<bool> timed_out{false};
    std::atomic<long long> elapsed_milliseconds{-1};
};

struct WakeProbe final {
    handoff_probe::Report report{};
    handoff_probe::Watchdog* watchdog{};
    Window* owner{};
    ApplicationWindowHandle owner_handle{};
    std::shared_ptr<WakeCanvas> canvas{};
    std::unique_ptr<Timer> timer{};
    SubscriptionToken tick_subscription{};
    bool ticked{};
    unsigned owner_paints_at_tick{};
    ClickRound modal_round{};
    ClickRound plain_round{};
    Window* modal{};
    Window* plain{};
    ApplicationWindowHandle plain_handle{};
    SubscriptionToken modal_click{};
    SubscriptionToken plain_click{};
    DialogResult dialog_result{DialogResult::none};
    bool modal_status_accepted{};
    // Set by finish_run, which runs once (after the plain window closed, or
    // from finish_if_stuck).
    bool finished{};
};

// The helper thread's body for one round: waits for the round to be armed,
// lets the loop go idle, clicks, and waits for the close within the bound.
class ClickAndWait final {
public:
    explicit ClickAndWait(ClickRound& round) noexcept : round_(&round) {}

    void operator()() const {
        ClickRound& round = *round_;
        // The plain round arms only after the whole modal round: wait up
        // to 40 seconds, within the watchdog's 60.
        for (int wait = 0; wait < 800 && !round.armed.load(); ++wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!round.armed.load()) return;
        // Long enough for the change's first paints and for the loop to
        // have nothing left to do but wait.
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        post_click(round);
        const std::chrono::steady_clock::time_point clicked = std::chrono::steady_clock::now();
        while (!round.closed.load() && std::chrono::steady_clock::now() - clicked < close_bound) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const std::chrono::steady_clock::duration took = std::chrono::steady_clock::now() - clicked;
        round.elapsed_milliseconds.store(std::chrono::duration_cast<std::chrono::milliseconds>(took).count());
        if (round.closed.load()) return;
        round.timed_out.store(true);
        // Unblocks a loop that is waiting, so that the run ends and reports.
        NSEvent* const wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                                 location:NSZeroPoint
                                            modifierFlags:0
                                                timestamp:0.0
                                             windowNumber:0
                                                  context:nil
                                                  subtype:0
                                                    data1:0
                                                    data2:0];
        if (wake != nil) [NSApp postEvent:wake atStart:NO];
    }

private:
    static void post_click(const ClickRound& round) {
        const NSPoint point = NSMakePoint(round.x.load(), round.y.load());
        const NSTimeInterval now = [[NSProcessInfo processInfo] systemUptime];
        NSEvent* const down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                                 location:point
                                            modifierFlags:0
                                                timestamp:now
                                             windowNumber:round.window_number.load()
                                                  context:nil
                                              eventNumber:0
                                               clickCount:1
                                                 pressure:1.0f];
        NSEvent* const up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                               location:point
                                          modifierFlags:0
                                              timestamp:now + 0.05
                                           windowNumber:round.window_number.load()
                                                context:nil
                                            eventNumber:0
                                             clickCount:1
                                               pressure:0.0f];
        if (down != nil) [NSApp postEvent:down atStart:NO];
        if (up != nil) [NSApp postEvent:up atStart:NO];
    }

    ClickRound* round_;
};

// Arms `round` for the native window titled `title`: its number and the OK
// button's centre in window base coordinates (bottom-up).
void arm_round(ClickRound& round, const char* title) {
    NSWindow* const native = handoff_probe::find_window(title);
    if (native == nil) return;
    const double height = native.contentView.bounds.size.height;
    round.window_number.store(native.windowNumber);
    round.x.store(ok_bounds.x + ok_bounds.width / 2.0);
    round.y.store(height - (ok_bounds.y + ok_bounds.height / 2.0));
    round.armed.store(true);
}

// The owner Timer's tick: proof that a Timer restarted outside a tick fires
// inside the modal session, and a change that must publish a frame.
struct OwnerTick final {
    WakeProbe* probe{};
    void operator()() const {
        if ((*probe).ticked) return;
        (*probe).ticked = true;
        (*probe).owner_paints_at_tick = (*(*probe).canvas).paints;
        (*(*probe).canvas).invalidate(Dirty::paint);
        (*(*probe).timer).stop();
    }
};

// Posted to the dialog by its OK button's handler: the close.
struct CloseDialog final {
    WakeProbe* probe{};
    void operator()() const {
        if ((*probe).modal != nullptr) (*(*probe).modal).set_dialog_result(DialogResult::ok);
    }
};

struct DialogOkClicked final {
    WakeProbe* probe{};
    void operator()(ButtonBase&) const {
        if ((*probe).modal != nullptr) static_cast<void>((*(*probe).modal).begin_invoke(CloseDialog{probe}));
    }
};

// The modal dialog is ready (inside show_modal, before its loop): the
// change, the in-paint replacement and the Timer restarted outside a tick.
struct ModalReady final {
    WakeProbe* probe{};
    void operator()(Window& window, ApplicationWindowHandle) const {
        (*probe).modal = &window;
        (*(*probe).watchdog).reached(3);
        WakeCanvas& canvas = *(*probe).canvas;
        canvas.finished = true;
        canvas.replacement_owed = true;
        canvas.invalidate(Dirty::paint);
        (*(*probe).timer).stop();
        (*(*probe).timer).start();
        arm_round((*probe).modal_round, modal_title);
    }
};

std::shared_ptr<Panel> make_dialog_root(const char* id, std::shared_ptr<Button>& ok) {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId(std::string(id)));
    (*root).set_requested_bounds({0.0, 0.0, dialog_width, dialog_height});
    ok = make_control<Button>(StableId(std::string(id) + ".ok"));
    (*ok).text = "OK";
    (*ok).set_requested_bounds(ok_bounds);
    (*root).add_child(ok);
    return root;
}

void check_plain(void* context);

// Posted to the owner: opens the modal and stays in its nested loop.
struct OpenModal final {
    WakeProbe* probe{};
    void operator()() const {
        (*(*probe).watchdog).reached(2);
        std::shared_ptr<Button> ok{};
        const std::shared_ptr<Panel> root = make_dialog_root("modal.wake.dialog", ok);
        (*probe).modal_click = (*ok).clicked().subscribe(DialogOkClicked{probe});
        ApplicationWindow entry{};
        entry.stable_id = "modal.wake.dialog";
        entry.owner_id = "modal.wake.owner";
        entry.model = std::make_unique<Window>(root, gui_forms::Size{dialog_width, dialog_height});
        entry.options.title = modal_title;
        entry.options.initial_size = {dialog_width, dialog_height};
        entry.options.ready = ModalReady{probe};
        const ApplicationWindows windows = (*probe).owner_handle.windows();
        const ApplicationModalResult result = windows.show_modal(std::move(entry));
        (*probe).modal_round.closed.store(true);
        (*probe).modal_status_accepted = result.status.accepted();
        (*probe).dialog_result = result.dialog_result;
        (*probe).modal = nullptr;
        (*probe).modal_click.disconnect();
        handoff_probe::after(0.5, probe, &check_plain);
    }
};

// Posted to the plain window by its OK button's handler: its close.
struct ClosePlain final {
    WakeProbe* probe{};
    void operator()() const { static_cast<void>((*probe).plain_handle.request_close()); }
};

struct PlainOkClicked final {
    WakeProbe* probe{};
    void operator()(ButtonBase&) const {
        if ((*probe).plain != nullptr) static_cast<void>((*(*probe).plain).begin_invoke(ClosePlain{probe}));
    }
};

// Ends a stuck plain-window round with the failure reported (below).
void finish_if_stuck(void* context);

struct PlainReady final {
    WakeProbe* probe{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        (*probe).plain = &window;
        (*probe).plain_handle = handle;
        // The same Timer restarted outside a tick, without a modal.
        (*probe).ticked = false;
        (*(*probe).timer).stop();
        (*(*probe).timer).start();
        (*probe).report.check("the plain window is found for its click",
                              handoff_probe::find_window(plain_title) != nil, "");
        arm_round((*probe).plain_round, plain_title);
        // After the helper's settling wait, the close bound and a margin.
        const double stuck_after = 0.8 + static_cast<double>(close_bound.count()) / 1000.0 + 1.5;
        handoff_probe::after(stuck_after, probe, &finish_if_stuck);
    }
};

void finish_run(void* context);

struct PlainClosed final {
    WakeProbe* probe{};
    void operator()() const {
        (*probe).plain_round.closed.store(true);
        (*probe).plain = nullptr;
        handoff_probe::after(0.3, probe, &finish_run);
    }
};

void check_plain(void* const context) {
    WakeProbe& probe = *static_cast<WakeProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(4);
    report.check("the posted close ended the modal within the bound",
                 !probe.modal_round.timed_out.load() && probe.modal_status_accepted &&
                     probe.dialog_result == DialogResult::ok,
                 "elapsed=" + std::to_string(probe.modal_round.elapsed_milliseconds.load()) + " ms" +
                     (probe.modal_round.timed_out.load() ? " (timed out; woken by the helper)" : ""));
    WakeCanvas& canvas = *probe.canvas;
    report.check("the Timer restarted outside a tick fired inside the modal", probe.ticked, "");
    report.check("the owner published a frame after the tick", canvas.paints > probe.owner_paints_at_tick,
                 "paints at tick " + std::to_string(probe.owner_paints_at_tick) + ", now " +
                     std::to_string(canvas.paints));
    report.check("the image replaced in on_paint painted afterwards",
                 canvas.replacement_seen && canvas.paints_after_replacement > 0U,
                 "paints after the replacement " + std::to_string(canvas.paints_after_replacement));
    report.check("the finish tiles armed by the paint were made in idle frames",
                 !(*probe.owner).engine_parts().finish_tiles_waiting(), "");

    std::shared_ptr<Button> ok{};
    const std::shared_ptr<Panel> root = make_dialog_root("modal.wake.plain", ok);
    probe.plain_click = (*ok).clicked().subscribe(PlainOkClicked{&probe});
    ApplicationWindow entry{};
    entry.stable_id = "modal.wake.plain";
    entry.owner_id = "modal.wake.owner";
    entry.model = std::make_unique<Window>(root, gui_forms::Size{dialog_width, dialog_height});
    entry.options.title = plain_title;
    entry.options.initial_size = {dialog_width, dialog_height};
    entry.options.ready = PlainReady{&probe};
    entry.options.closed = PlainClosed{&probe};
    const ApplicationWindowOpenResult opened = probe.owner_handle.windows().open_window(std::move(entry));
    report.check("the plain window opened", opened.status.accepted(), "");
}

// The plain round's close never came: the run still ends, with the round's
// failure reported by finish_run. Does nothing once the window closed.
void finish_if_stuck(void* const context) {
    WakeProbe& probe = *static_cast<WakeProbe*>(context);
    if (probe.plain_round.closed.load() || probe.finished) return;
    probe.plain_round.timed_out.store(true);
    if (probe.plain != nullptr) static_cast<void>(probe.plain_handle.request_close());
    finish_run(context);
}

void finish_run(void* const context) {
    WakeProbe& probe = *static_cast<WakeProbe*>(context);
    if (probe.finished) return;
    probe.finished = true;
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(5);
    report.check("the posted close closed the plain window within the bound", !probe.plain_round.timed_out.load(),
                 "elapsed=" + std::to_string(probe.plain_round.elapsed_milliseconds.load()) + " ms");
    report.check("the Timer restarted outside a tick fired without a modal", probe.ticked, "");
    probe.plain_click.disconnect();
    probe.tick_subscription.disconnect();
    // Released with its window still open, on the UI thread.
    probe.timer.reset();
    const HostServiceStatus closed = probe.owner_handle.request_close();
    report.check("owner close request accepted", closed.accepted(), "");
}

void open_modal_step(void* const context) {
    WakeProbe& probe = *static_cast<WakeProbe*>(context);
    static_cast<void>((*probe.owner).begin_invoke(OpenModal{&probe}));
}

struct OwnerReady final {
    WakeProbe* probe{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        (*probe).owner = &window;
        (*probe).owner_handle = handle;
        (*(*probe).watchdog).reached(1);
        const std::vector<std::byte> pixels(static_cast<std::size_t>(16U * 16U * 4U), std::byte{0xFF});
        const ImageLoadResult loaded = window.load_bgra32_premultiplied(16U, 16U, 16U * 4U, pixels);
        (*probe).report.check("the canvas image loaded", static_cast<bool>(loaded), "");
        if (loaded) (*(*probe).canvas).image = loaded.image;
        (*probe).timer = std::make_unique<Timer>(window, std::chrono::milliseconds(120));
        (*probe).tick_subscription = (*(*probe).timer).tick().subscribe(OwnerTick{probe});
        handoff_probe::after(0.6, probe, &open_modal_step);
    }
};

} // namespace

int main() {
    handoff_probe::Watchdog watchdog(60);
    WakeProbe probe{};
    probe.watchdog = &watchdog;
    std::thread modal_helper{ClickAndWait(probe.modal_round)};
    std::thread plain_helper{ClickAndWait(probe.plain_round)};
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    probe.report.check("application created", static_cast<bool>(created), "");
    if (created) {
        const std::shared_ptr<Panel> root = make_control<Panel>(StableId("modal.wake.owner"));
        (*root).set_requested_bounds({0.0, 0.0, 360.0, 240.0});
        probe.canvas = make_control<WakeCanvas>(StableId("modal.wake.canvas"));
        (*probe.canvas).set_requested_bounds({10.0, 10.0, 340.0, 220.0});
        (*root).add_child(probe.canvas);
        ApplicationWindow owner{};
        owner.stable_id = "modal.wake.owner";
        owner.model = std::make_unique<Window>(root, gui_forms::Size{360.0, 240.0});
        owner.options.title = owner_title;
        owner.options.initial_size = {360.0, 240.0};
        owner.options.ready = OwnerReady{&probe};
        std::vector<ApplicationWindow> windows;
        windows.push_back(std::move(owner));
        const ApplicationResult result = (*created.application).run(std::move(windows));
        probe.report.check("run accepted", result.accepted(),
                           "error=" + std::to_string(static_cast<int>(result.error)));
        probe.report.check("no callback exception", !result.callback_exception, "");
    }
    // A helper whose round never armed returns after its wait.
    modal_helper.join();
    plain_helper.join();
    probe.canvas.reset();
    watchdog.finish();
    probe.report.finish();
    const int status = probe.report.failures() == 0U ? 0 : 1;
    return status;
}
