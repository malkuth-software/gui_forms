#import <AppKit/AppKit.h>

#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/input_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/window/window.hpp"
#include "support/macos_handoff_probe.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A modal window over the running window, read back from AppKit (Mac handoff
// 2026-11-09, item 2b): the modal is opened from posted work
// (Window::begin_invoke) after the owner's text box has focus. While it is
// up, AppKit's modal window is the dialog and it is key, posted work for the
// owner still runs and a text change in the owner still paints (the host's
// main-thread work goes through performSelectorOnMainThread in the common
// modes). A dialog result set from posted work closes it. Afterwards the
// owner NSWindow is key again with its raster view first responder, and the
// text box focused before the modal opened is focused again.

namespace {
using namespace gui_forms;

constexpr const char* owner_title = "GUI.Forms modal probe owner";
constexpr const char* modal_title = "GUI.Forms modal probe dialog";

struct ModalProbe final {
    handoff_probe::Report report{};
    handoff_probe::Watchdog* watchdog{};
    Window* owner{};
    Window* modal{};
    ApplicationWindowHandle owner_handle{};
    std::shared_ptr<TextBox> field{};
    std::shared_ptr<TextBox> counter{};
    std::uint64_t paints_before_tick{};
    unsigned ticks{};
    bool tick_saw_modal{};
    bool result_posted_ran{};
    DialogResult dialog_result{DialogResult::none};
    bool modal_status_accepted{};
};

std::unique_ptr<Window> make_owner(ModalProbe& probe) {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("modal.probe.owner"));
    (*root).set_requested_bounds({0.0, 0.0, 360.0, 200.0});
    probe.field = make_control<TextBox>(StableId("modal.probe.field"));
    (*probe.field).set_requested_bounds({20.0, 20.0, 240.0, 28.0});
    probe.counter = make_control<TextBox>(StableId("modal.probe.counter"));
    (*probe.counter).set_requested_bounds({20.0, 70.0, 240.0, 28.0});
    (*root).add_child(probe.field);
    (*root).add_child(probe.counter);
    std::unique_ptr<Window> window = std::make_unique<Window>(root, gui_forms::Size{360.0, 200.0});
    return window;
}

void check_after_modal(void* context);

// Posted to the owner while the modal is up: proof that the host's posted
// work still runs, and a change that must paint.
struct OwnerTick final {
    ModalProbe* probe{};
    void operator()() const {
        ++(*probe).ticks;
        (*probe).tick_saw_modal = [NSApp modalWindow] != nil;
        (*(*probe).counter).text = "tick " + std::to_string((*probe).ticks);
    }
};

// Posted to the dialog: its dialog result asks it to close.
struct SetDialogResult final {
    ModalProbe* probe{};
    void operator()() const {
        (*probe).result_posted_ran = true;
        (*(*probe).modal).set_dialog_result(DialogResult::ok);
    }
};

void check_modal_flow(void* const context) {
    ModalProbe& probe = *static_cast<ModalProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(4);
    report.check("posted work for the owner ran during the modal", probe.ticks == 1U && probe.tick_saw_modal,
                 "ticks=" + std::to_string(probe.ticks) + (probe.tick_saw_modal ? " (modal up)" : " (no modal)"));
    const std::uint64_t paints = (*probe.owner).metrics_snapshot().paint_passes;
    report.check("damage in the owner painted during the modal", paints > probe.paints_before_tick,
                 "owner paint_passes " + std::to_string(probe.paints_before_tick) + " -> " + std::to_string(paints));
    NSWindow* const modal_native = handoff_probe::find_window(modal_title);
    report.check("modal still up before its result", [NSApp modalWindow] == modal_native && modal_native != nil,
                 "modalWindow=" + handoff_probe::describe_object([NSApp modalWindow]));
    static_cast<void>((*probe.modal).begin_invoke(SetDialogResult{&probe}));
}

void check_modal_up(void* const context) {
    ModalProbe& probe = *static_cast<ModalProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(3);
    NSWindow* const owner_native = handoff_probe::find_window(owner_title);
    NSWindow* const modal_native = handoff_probe::find_window(modal_title);
    report.check("NSApp modalWindow is the dialog", modal_native != nil && [NSApp modalWindow] == modal_native,
                 "modalWindow title=" + std::string([NSApp modalWindow] == nil ? "nil" : [[NSApp modalWindow].title UTF8String]));
    report.check("the dialog is key", modal_native != nil && [NSApp keyWindow] == modal_native,
                 "keyWindow title=" + std::string([NSApp keyWindow] == nil ? "nil" : [[NSApp keyWindow].title UTF8String]));
    report.check("the owner is not key under the modal", owner_native != nil && !owner_native.keyWindow, "");
    probe.paints_before_tick = (*probe.owner).metrics_snapshot().paint_passes;
    static_cast<void>((*probe.owner).begin_invoke(OwnerTick{&probe}));
    handoff_probe::after(0.6, &probe, &check_modal_flow);
}

struct ModalReady final {
    ModalProbe* probe{};
    void operator()(Window& window, ApplicationWindowHandle) const {
        (*probe).modal = &window;
        handoff_probe::after(0.6, probe, &check_modal_up);
    }
};

// Posted to the owner: opens the modal and stays in its nested loop until
// it closes.
struct OpenModal final {
    ModalProbe* probe{};
    void operator()() const {
        const std::shared_ptr<Panel> root = make_control<Panel>(StableId("modal.probe.dialog"));
        (*root).set_requested_bounds({0.0, 0.0, 260.0, 120.0});
        const std::shared_ptr<TextBox> answer = make_control<TextBox>(StableId("modal.probe.answer"));
        (*answer).set_requested_bounds({20.0, 20.0, 200.0, 28.0});
        (*root).add_child(answer);
        ApplicationWindow entry{};
        entry.stable_id = "modal.probe.dialog";
        entry.owner_id = "modal.probe.owner";
        entry.model = std::make_unique<Window>(root, gui_forms::Size{260.0, 120.0});
        entry.options.title = modal_title;
        entry.options.initial_size = {260.0, 120.0};
        entry.options.ready = ModalReady{probe};
        const ApplicationWindows windows = (*probe).owner_handle.windows();
        const ApplicationModalResult result = windows.show_modal(std::move(entry));
        (*probe).modal_status_accepted = result.status.accepted();
        (*probe).dialog_result = result.dialog_result;
        (*probe).modal = nullptr;
        handoff_probe::after(0.6, probe, &check_after_modal);
    }
};

void check_before_modal(void* const context) {
    ModalProbe& probe = *static_cast<ModalProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(2);
    NSWindow* const owner_native = handoff_probe::find_window(owner_title);
    report.check("owner NSWindow is key before the modal", owner_native != nil && [NSApp keyWindow] == owner_native,
                 "keyWindow=" + handoff_probe::describe_object([NSApp keyWindow]));
    report.check("owner text box focused before the modal",
                 (*probe.owner).focused_control() == probe.field, "");
    static_cast<void>((*probe.owner).begin_invoke(OpenModal{&probe}));
}

void check_after_modal(void* const context) {
    ModalProbe& probe = *static_cast<ModalProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(5);
    report.check("show_modal accepted", probe.modal_status_accepted, "");
    report.check("dialog result ok (set from posted work)",
                 probe.result_posted_ran && probe.dialog_result == DialogResult::ok,
                 "dialog_result=" + std::to_string(static_cast<int>(probe.dialog_result)));
    report.check("no modal window after close", [NSApp modalWindow] == nil, "");
    NSWindow* const owner_native = handoff_probe::find_window(owner_title);
    NSWindow* const key = [NSApp keyWindow];
    report.check("owner NSWindow is key again ([NSApp keyWindow])", owner_native != nil && key == owner_native,
                 "keyWindow title=" + std::string(key == nil ? "nil" : [key.title UTF8String]));
    NSView* const raster = owner_native == nil ? nil : handoff_probe::find_raster_view(owner_native.contentView);
    report.check("owner first responder is its raster view",
                 raster != nil && owner_native.firstResponder == raster,
                 "firstResponder=" + handoff_probe::describe_object(owner_native.firstResponder));
    const Control::Ptr focused = (*probe.owner).focused_control();
    report.check("text box focused before the modal is focused again", focused == probe.field,
                 "focused=" + std::string(focused == nullptr ? "none" : std::string((*focused).stable_id().value())));
    const HostServiceStatus closed = probe.owner_handle.request_close();
    report.check("owner close request accepted", closed.accepted(), "");
}

struct OwnerReady final {
    ModalProbe* probe{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        (*probe).owner = &window;
        (*probe).owner_handle = handle;
        (*(*probe).watchdog).reached(1);
        const bool focused = window.request_focus((*probe).field);
        (*probe).report.check("owner text box takes focus", focused, "");
        handoff_probe::after(0.6, probe, &check_before_modal);
    }
};

} // namespace

int main() {
    handoff_probe::Watchdog watchdog(60);
    ModalProbe probe{};
    probe.watchdog = &watchdog;
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    probe.report.check("application created", static_cast<bool>(created), "");
    if (!created) {
        watchdog.finish();
        probe.report.finish();
        return 1;
    }
    ApplicationWindow owner{};
    owner.stable_id = "modal.probe.owner";
    owner.model = make_owner(probe);
    owner.options.title = owner_title;
    owner.options.initial_size = {360.0, 200.0};
    owner.options.ready = OwnerReady{&probe};
    std::vector<ApplicationWindow> windows;
    windows.push_back(std::move(owner));
    const ApplicationResult result = (*created.application).run(std::move(windows));
    probe.report.check("run accepted", result.accepted(),
                       "error=" + std::to_string(static_cast<int>(result.error)));
    probe.report.check("no callback exception", !result.callback_exception, "");
    probe.field.reset();
    probe.counter.reset();
    watchdog.finish();
    probe.report.finish();
    const int status = probe.report.failures() == 0U ? 0 : 1;
    return status;
}
