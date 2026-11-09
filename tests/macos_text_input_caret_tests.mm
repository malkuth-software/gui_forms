#import <AppKit/AppKit.h>
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/platform/macos_host.hpp"
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// The macOS host places input method windows by the window's text input
// caret (SurfaceRoot::text_input_caret): the focused control's own caret, or
// its left edge when it names none.

namespace {

// A focusable control naming a caret box in its own coordinates.
class CaretControl final : public gui_forms::Control {
public:
    explicit CaretControl(gui_forms::StableId stable_id) : Control(std::move(stable_id)) {
        focusable = true;
    }
    [[nodiscard]] std::optional<gui_forms::Rect> text_input_caret() const override {
        return gui_forms::Rect{10.0, 4.0, 2.0, 16.0};
    }
};

struct Probe final {
    gui_forms::Window* model{nullptr}; // Owned by run_macos_application.
    std::shared_ptr<CaretControl> caret_control{};
    std::shared_ptr<gui_forms::Control> plain_control{};
    bool finished{false};
    std::string failure{};
};

NSWindow* probe_window() {
    for (NSWindow* candidate in NSApp.windows) {
        if ([candidate.title isEqualToString:@"Text input caret probe"]) return candidate;
    }
    throw std::runtime_error("native window missing");
}

// The screen rectangle the host should report for `caret` (window
// coordinates), converted the way an NSTextInputClient converts.
NSRect expected_screen_rect(NSView* view, const gui_forms::Rect caret) {
    const NSRect local = NSMakeRect(caret.x, caret.y, caret.width, caret.height);
    const NSRect in_window = [view convertRect:local toView:nil];
    return [view.window convertRectToScreen:in_window];
}

bool near(const NSRect left, const NSRect right) {
    return std::fabs(left.origin.x - right.origin.x) < 0.01 &&
        std::fabs(left.origin.y - right.origin.y) < 0.01 &&
        std::fabs(left.size.width - right.size.width) < 0.01 &&
        std::fabs(left.size.height - right.size.height) < 0.01;
}

void require_caret(Probe& probe, const gui_forms::Control::Ptr& focus, const gui_forms::Rect expected,
                   const char* const what) {
    if (!(*probe.model).request_focus(focus)) throw std::runtime_error(std::string(what) + ": focus refused");
    // The host's content view adopts NSTextInputClient; id converts to the
    // protocol-qualified view after the check below.
    const id content = probe_window().contentView;
    if (![content conformsToProtocol:@protocol(NSTextInputClient)])
        throw std::runtime_error("the content view is not a text input client");
    NSView<NSTextInputClient>* view = content;
    if (view == nil)
        throw std::runtime_error("the content view is not a text input client");
    NSRange actual = NSMakeRange(NSNotFound, 0);
    const NSRect reported = [view firstRectForCharacterRange:NSMakeRange(0, 0) actualRange:&actual];
    const NSRect wanted = expected_screen_rect(view, expected);
    std::printf("text-input-caret|case=%s|reported=%.2f,%.2f,%.2f,%.2f|expected=%.2f,%.2f,%.2f,%.2f\n",
        what, reported.origin.x, reported.origin.y, reported.size.width, reported.size.height,
        wanted.origin.x, wanted.origin.y, wanted.size.width, wanted.size.height);
    if (!near(reported, wanted)) throw std::runtime_error(std::string(what) + ": wrong caret rectangle");
}

void check(void* const address) {
    Probe& probe = *static_cast<Probe*>(address);
    try {
        // CaretControl at (40, 30): its caret box (10, 4, 2, 16) in window
        // coordinates.
        require_caret(probe, probe.caret_control, gui_forms::Rect{50.0, 34.0, 2.0, 16.0}, "control_caret");
        // A control naming no caret: its left edge, one unit wide.
        require_caret(probe, probe.plain_control, gui_forms::Rect{40.0, 80.0, 1.0, 24.0}, "left_edge");
        probe.finished = true;
    } catch (const std::exception& error) {
        probe.failure = error.what();
    }
    for (NSWindow* candidate in NSApp.windows) [candidate close];
}

class Ready final {
public:
    explicit Ready(Probe& probe) : probe_(probe) {}
    void operator()(std::function<void()>, std::function<void()>) const {
        const dispatch_time_t time = dispatch_time(DISPATCH_TIME_NOW, 200 * 1000000LL);
        dispatch_after_f(time, dispatch_get_main_queue(), &probe_, check);
    }
private:
    Probe& probe_;
};

} // namespace

int main() {
    Probe probe{};
    const std::shared_ptr<gui_forms::Control> root =
        gui_forms::make_control<gui_forms::Control>(gui_forms::StableId("root"));
    probe.caret_control = gui_forms::make_control<CaretControl>(gui_forms::StableId("caret"));
    (*probe.caret_control).set_requested_bounds({40, 30, 120, 24});
    (*root).add_child(probe.caret_control);
    probe.plain_control = gui_forms::make_control<gui_forms::Control>(gui_forms::StableId("plain"));
    (*probe.plain_control).focusable = true;
    (*probe.plain_control).set_requested_bounds({40, 80, 120, 24});
    (*root).add_child(probe.plain_control);
    gui_forms::host::MacApplicationWindow entry{};
    entry.stable_id = "text-input-caret";
    entry.model = std::make_unique<gui_forms::Window>(root, gui_forms::Size{240, 140});
    probe.model = entry.model.get();
    entry.options.title = "Text input caret probe";
    entry.options.initial_size = {240, 140};
    entry.options.visibility_ready = Ready(probe);
    std::vector<gui_forms::host::MacApplicationWindow> windows{};
    windows.push_back(std::move(entry));
    const int result = gui_forms::host::run_macos_application(std::move(windows));
    if (result != 0 || !probe.finished || !probe.failure.empty()) {
        std::fprintf(stderr, "text input caret probe failed: %s\n", probe.failure.c_str());
        return 1;
    }
    return 0;
}
