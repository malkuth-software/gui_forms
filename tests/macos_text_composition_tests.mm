#import <AppKit/AppKit.h>
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/platform/macos_host.hpp"
#include "support/routed_input_probe.hpp"
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// The macOS host's NSTextInputClient side of an input method composition
// (handoff §3.1 rows 1 and 2, audits N105 and N106), driven directly the way
// the Japanese input method drives it, so no input method or tester is
// needed. The focused control logs every key and text event it receives:
//
// 1. "ka" composes to "か": setMarkedText shows inline marked text, and the
//    control receives each step as composing text.
// 2. While marked text is held, keyDown: returns before the model: the
//    control receives no key.
// 3. insertText: (the input method's answer to Return) commits once; a later
//    unmarkText adds nothing.
// 4. Escape: the input method clears the marked text with an empty
//    setMarkedText; nothing is committed.
// 5. unmarkText mid-composition (a click elsewhere) commits the marked text
//    once; a second unmarkText adds nothing.
// 6. With no composition open, keyDown: reaches the control again.

namespace {

using EventLog = std::vector<std::string>;

// "か" (U+304B) in UTF-8.
constexpr const char* hiragana_ka_utf8 = "\xE3\x81\x8B";

// macOS virtual key code of Return (kVK_Return).
constexpr unsigned short return_key_code = 36U;

// A focusable control that records the key and text events routed to it.
class CompositionTarget final : public gui_forms::tests::RoutedInputProbe<gui_forms::Control> {
public:
    CompositionTarget(gui_forms::StableId stable_id, EventLog& log) : gui_forms::tests::RoutedInputProbe<gui_forms::Control>(std::move(stable_id)), log_(log) {
        focusable = true;
    }
    void routed_key(gui_forms::KeyEvent& event) override {
        log_.push_back(event.action == gui_forms::KeyAction::down ? "key:down" : "key:up");
        event.handled = true;
    }
    void on_text_input(gui_forms::TextInputEvent& event) override {
        log_.push_back(std::string(event.composing ? "composing:" : "text:") + event.text_utf8);
        event.handled = true;
    }

private:
    EventLog& log_; // Owned by Probe, which outlives the run.
};

struct Probe final {
    gui_forms::Window* model{nullptr}; // Owned by run_macos_application.
    EventLog log{};
    std::shared_ptr<CompositionTarget> target{};
    bool finished{false};
    std::string failure{};
};

NSWindow* probe_window() {
    for (NSWindow* candidate in NSApp.windows) {
        if ([candidate.title isEqualToString:@"Text composition probe"]) return candidate;
    }
    throw std::runtime_error("native window missing");
}

// The host's content view, which adopts NSTextInputClient.
NSView<NSTextInputClient>* input_client() {
    const id content = probe_window().contentView;
    if (![content conformsToProtocol:@protocol(NSTextInputClient)])
        throw std::runtime_error("the content view is not a text input client");
    NSView<NSTextInputClient>* view = content;
    if (view == nil) throw std::runtime_error("the content view is not a text input client");
    return view;
}

NSString* ns_string(const char* const utf8) {
    return [NSString stringWithUTF8String:utf8];
}

// A Return key-down for the probe window, as AppKit delivers it.
NSEvent* return_key_down() {
    NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                      location:NSZeroPoint
                                 modifierFlags:0
                                     timestamp:0.0
                                  windowNumber:probe_window().windowNumber
                                       context:nil
                                    characters:@"\r"
                   charactersIgnoringModifiers:@"\r"
                                     isARepeat:NO
                                       keyCode:return_key_code];
    if (event == nil) throw std::runtime_error("could not make a Return key event");
    return event;
}

std::string joined(const EventLog& log) {
    std::string text;
    for (const std::string& entry : log) {
        if (!text.empty()) text += ",";
        text += entry;
    }
    return text;
}

void require_log(Probe& probe, const EventLog& expected, const char* const what) {
    std::printf("text-composition|case=%s|log=%s\n", what, joined(probe.log).c_str());
    if (probe.log != expected)
        throw std::runtime_error(std::string(what) + ": expected [" + joined(expected) + "], got [" +
                                 joined(probe.log) + "]");
    probe.log.clear();
}

void require_marked(NSView<NSTextInputClient>* view, const NSUInteger length, const char* const what) {
    const NSRange marked = [view markedRange];
    if (length == 0U) {
        if ([view hasMarkedText] || marked.location != NSNotFound)
            throw std::runtime_error(std::string(what) + ": marked text remains");
        return;
    }
    if (![view hasMarkedText] || marked.location != 0U || marked.length != length)
        throw std::runtime_error(std::string(what) + ": wrong marked range");
}

// Opens a "ka" composition that has turned into "か", as the Japanese input
// method sends it: romaji first, then the kana, each with the caret at its
// end and no replacement.
void compose_ka(Probe& probe, NSView<NSTextInputClient>* view) {
    const NSRange none = NSMakeRange(NSNotFound, 0);
    [view setMarkedText:@"k" selectedRange:NSMakeRange(1, 0) replacementRange:none];
    [view setMarkedText:ns_string(hiragana_ka_utf8) selectedRange:NSMakeRange(1, 0) replacementRange:none];
    require_log(probe, EventLog{"composing:k", std::string("composing:") + hiragana_ka_utf8}, "compose");
    require_marked(view, 1U, "compose");
    const NSRange selected = [view selectedRange];
    if (selected.location != 1U || selected.length != 0U)
        throw std::runtime_error("compose: the caret is not after the marked text");
}

void run_cases(Probe& probe) {
    if (!(*probe.model).request_focus(probe.target)) throw std::runtime_error("focus refused");
    NSView<NSTextInputClient>* view = input_client();
    const std::string committed = std::string("text:") + hiragana_ka_utf8;

    // 1 and 2: inline marked text, and keyDown: returns early while it is
    // held. Without an input method, AppKit maps Return to insertNewline:,
    // which the host leaves inert, so nothing at all reaches the control.
    compose_ka(probe, view);
    [view keyDown:return_key_down()];
    require_log(probe, EventLog{}, "key_during_composition");
    require_marked(view, 1U, "key_during_composition");

    // 3: Return commits once.
    [view insertText:ns_string(hiragana_ka_utf8) replacementRange:NSMakeRange(NSNotFound, 0)];
    require_marked(view, 0U, "commit");
    [view unmarkText];
    require_log(probe, EventLog{committed}, "commit");

    // 4: Escape cancels.
    compose_ka(probe, view);
    [view setMarkedText:@"" selectedRange:NSMakeRange(0, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
    require_marked(view, 0U, "cancel");
    [view unmarkText];
    require_log(probe, EventLog{"composing:"}, "cancel");

    // 5: a click elsewhere commits once.
    compose_ka(probe, view);
    [view unmarkText];
    require_marked(view, 0U, "unmark");
    [view unmarkText];
    require_log(probe, EventLog{committed}, "unmark");

    // 6: with no composition, the key reaches the control, which handles it,
    // so the host does not hand it to the input method.
    [view keyDown:return_key_down()];
    require_log(probe, EventLog{"key:down"}, "key_without_composition");
}

void check(void* const address) {
    Probe& probe = *static_cast<Probe*>(address);
    try {
        run_cases(probe);
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
    probe.target = gui_forms::make_control<CompositionTarget>(gui_forms::StableId("target"), probe.log);
    (*probe.target).set_requested_bounds({40, 30, 160, 24});
    (*root).add_child(probe.target);
    gui_forms::host::MacApplicationWindow entry{};
    entry.stable_id = "text-composition";
    entry.model = std::make_unique<gui_forms::Window>(root, gui_forms::Size{240, 100});
    probe.model = entry.model.get();
    entry.options.title = "Text composition probe";
    entry.options.initial_size = {240, 100};
    entry.options.visibility_ready = Ready(probe);
    std::vector<gui_forms::host::MacApplicationWindow> windows{};
    windows.push_back(std::move(entry));
    const int result = gui_forms::host::run_macos_application(std::move(windows));
    if (result != 0 || !probe.finished || !probe.failure.empty()) {
        std::fprintf(stderr, "text composition probe failed: %s\n", probe.failure.c_str());
        return 1;
    }
    return 0;
}
