// The Windows Forms key events and the games-style key state (0.5 key
// input, key_events.hpp): their order (key_state_changed, PreviewKeyDown,
// the window's KeyPreview, KeyDown, the control's own handling, KeyPress,
// KeyUp), handled and suppress_key_press, is_input_key against dialog
// processing, input-method composition, auto-repeat, simultaneous keys,
// modifiers as keys, the key-state query, the releases on deactivation,
// and the portable Keys codes.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "support/routed_input_probe.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

using Log = std::vector<std::string>;

// A focusable control that logs its own key and text handling and handles
// nothing unless told to.
class Probe final : public gui_forms::tests::RoutedInputProbe<Control> {
public:
    Probe(StableId stable_id, Log& log) : gui_forms::tests::RoutedInputProbe<Control>(std::move(stable_id)), log_(log) { focusable = true; }
    void routed_key(KeyEvent& event) override {
        log_.push_back(event.action == KeyAction::down ? "on_key:down" : "on_key:up");
        event.handled = handles_keys;
    }
    void on_text_input(TextInputEvent& event) override {
        log_.push_back(std::string(event.composing ? "on_text:composing:" : "on_text:") + event.text_utf8);
        event.handled = true;
    }
    bool handles_keys{};

private:
    Log& log_;
};

struct NoteState final {
    Log* log{};
    void operator()(const KeyStateChange& change) const {
        std::string entry = change.down ? "state:down:" : "state:up:";
        entry += std::to_string(change.physical_key);
        if (change.released_by_deactivation) entry += ":deactivated";
        (*log).push_back(entry);
    }
};

struct NoteKey final {
    Log* log{};
    const char* name{};
    bool handle{};
    bool suppress{};
    void operator()(KeyEventArgs& args) const {
        std::string entry = name;
        if (args.repeat) entry += ":repeat";
        if (args.released_by_deactivation) entry += ":deactivated";
        (*log).push_back(entry);
        args.handled = handle;
        args.suppress_key_press = suppress;
    }
};

struct NotePreview final {
    Log* log{};
    bool input_key{};
    void operator()(PreviewKeyDownEventArgs& args) const {
        (*log).push_back("preview:" + std::to_string(args.key_code));
        args.is_input_key = input_key;
    }
};

struct NotePress final {
    Log* log{};
    const char* name{};
    bool handle{};
    void operator()(KeyPressEventArgs& args) const {
        (*log).push_back(std::string(name) + ":" + args.text + ":" + std::to_string(args.key_char));
        args.handled = handle;
    }
};

// A window over a root panel with two probes, the first focused.
struct Fixture final {
    Log log;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("keys.root"));
    std::shared_ptr<Probe> first = make_control<Probe>(StableId("keys.first"), log);
    std::shared_ptr<Probe> second = make_control<Probe>(StableId("keys.second"), log);
    std::unique_ptr<Window> window;
    Fixture() {
        (*first).set_requested_bounds({0.0, 0.0, 50.0, 20.0});
        (*second).set_requested_bounds({0.0, 30.0, 50.0, 20.0});
        (*root).add_child(first);
        (*root).add_child(second);
        window = std::make_unique<Window>(root, Size{200.0, 100.0});
        (*window).perform_layout();
        (*window).set_active(true);
        require((*window).request_focus(first), "the first probe takes focus");
    }
};

KeyEvent key(KeyAction action, std::uint32_t physical, Modifier modifiers = Modifier::none, bool repeat = false) {
    KeyEvent event;
    event.action = action;
    event.physical_key = physical;
    event.modifiers = modifiers;
    event.repeat = repeat;
    return event;
}

TextInputEvent text(const char* utf8, bool composing = false) {
    TextInputEvent event;
    event.text_utf8 = utf8;
    event.composing = composing;
    return event;
}

void test_order_of_one_key() {
    Fixture f;
    Window& window = *f.window;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back(window.key_state_changed().subscribe(NoteState{&f.log}));
    tokens.push_back(window.key_down().subscribe(NoteKey{&f.log, "window_down"}));
    tokens.push_back(window.key_up().subscribe(NoteKey{&f.log, "window_up"}));
    tokens.push_back(window.key_press().subscribe(NotePress{&f.log, "window_press"}));
    tokens.push_back((*f.first).preview_key_down().subscribe(NotePreview{&f.log}));
    tokens.push_back((*f.first).key_down().subscribe(NoteKey{&f.log, "control_down"}));
    tokens.push_back((*f.first).key_up().subscribe(NoteKey{&f.log, "control_up"}));
    tokens.push_back((*f.first).key_press().subscribe(NotePress{&f.log, "control_press"}));
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::a)));
    static_cast<void>(window.dispatch_text(text("a")));
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::a)));
    const Log expected{"state:down:4", "preview:65", "window_down", "control_down", "on_key:down",
                       "window_press:a:97", "control_press:a:97", "on_text:a",
                       "state:up:4", "window_up", "control_up", "on_key:up"};
    require(f.log == expected, "one key raises its events in the Windows Forms order");
}

void test_handled_and_suppress() {
    Fixture f;
    Window& window = *f.window;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back(window.key_down().subscribe(NoteKey{&f.log, "window_down", true}));
    tokens.push_back((*f.first).key_down().subscribe(NoteKey{&f.log, "control_down"}));
    require(window.dispatch_key(key(KeyAction::down, PhysicalKey::b)) && f.log == Log{"window_down"},
            "a window key_down handler that handles the key keeps it from the control");
    tokens.clear();

    f.log.clear();
    tokens.push_back((*f.first).key_down().subscribe(NoteKey{&f.log, "control_down", false, true}));
    tokens.push_back((*f.first).key_press().subscribe(NotePress{&f.log, "control_press"}));
    require(window.dispatch_key(key(KeyAction::down, PhysicalKey::c)) && f.log == Log{"control_down"},
            "suppress_key_press handles the key");
    require(window.dispatch_text(text("c")) && f.log == Log{"control_down"},
            "suppress_key_press drops the key's text: no KeyPress, no text input");
    tokens.clear();
    f.log.clear();
    tokens.push_back((*f.first).key_press().subscribe(NotePress{&f.log, "control_press", true}));
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::d)));
    require(window.dispatch_text(text("d")) && f.log == Log{"on_key:down", "control_press:d:100"},
            "a new key ends the suppression; a handled KeyPress keeps the text from the control");
}

void test_composition_and_text() {
    Fixture f;
    Window& window = *f.window;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back((*f.first).key_press().subscribe(NotePress{&f.log, "press"}));
    static_cast<void>(window.dispatch_text(text("ka", true)));
    static_cast<void>(window.dispatch_text(text("\xE3\x81\x8B", true)));
    require(f.log == Log{"on_text:composing:ka", "on_text:composing:\xE3\x81\x8B"},
            "composing text raises no KeyPress and reaches the control untouched");
    f.log.clear();
    static_cast<void>(window.dispatch_text(text("\xE3\x81\x8B")));
    require(f.log == Log{"press:\xE3\x81\x8B:12363", "on_text:\xE3\x81\x8B"},
            "the committed text raises KeyPress with its one character");
    f.log.clear();
    static_cast<void>(window.dispatch_text(text("ab")));
    require(f.log == Log{"press:ab:0", "on_text:ab"}, "text of several characters has no key_char");
}

// Input order rule 3: suppress_key_press on KeyDown cancels its KeyPress
// and text; handled does not (it only ends the route). A composition's
// commit is kept even after a suppressed KeyDown.
void test_suppress_key_press_cancels_key_press() {
    Fixture f;
    Window& window = *f.window;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back((*f.first).key_down().subscribe(NoteKey{&f.log, "control_down", true}));
    tokens.push_back((*f.first).key_press().subscribe(NotePress{&f.log, "control_press"}));
    require(window.dispatch_key(key(KeyAction::down, PhysicalKey::e)) && f.log == Log{"control_down"},
            "the control's key_down handles the key");
    static_cast<void>(window.dispatch_text(text("e")));
    require(f.log == Log{"control_down", "control_press:e:101", "on_text:e"},
            "a handled KeyDown still raises KeyPress and delivers the text");
    tokens.clear();

    f.log.clear();
    tokens.push_back(window.key_down().subscribe(NoteKey{&f.log, "window_down", false, true}));
    tokens.push_back((*f.first).key_press().subscribe(NotePress{&f.log, "control_press"}));
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::f)));
    static_cast<void>(window.dispatch_text(text("f")));
    require(f.log == Log{"window_down"}, "suppress_key_press on the window's KeyDown cancels the KeyPress");
    tokens.clear();

    f.log.clear();
    static_cast<void>(window.dispatch_text(text("ka", true)));
    tokens.push_back((*f.first).key_down().subscribe(NoteKey{&f.log, "control_down", false, true}));
    tokens.push_back((*f.first).key_press().subscribe(NotePress{&f.log, "control_press"}));
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::enter)));
    static_cast<void>(window.dispatch_text(text("\xE3\x81\x8B")));
    require(f.log == Log{"on_text:composing:ka", "control_down", "control_press:\xE3\x81\x8B:12363",
                         "on_text:\xE3\x81\x8B"},
            "suppress_key_press during a composition keeps the IME's commit");
}

void test_input_key_and_dialog_keys() {
    Fixture f;
    Window& window = *f.window;
    require(window.dispatch_key(key(KeyAction::down, PhysicalKey::tab)) && window.focused_control() == f.second,
            "an unhandled Tab moves the focus");
    require(window.dispatch_key(key(KeyAction::down, PhysicalKey::tab, Modifier::shift)) &&
                window.focused_control() == f.first,
            "Shift+Tab moves the focus back");
    std::vector<SubscriptionToken> tokens;
    tokens.push_back((*f.first).preview_key_down().subscribe(NotePreview{&f.log, true}));
    f.log.clear();
    require(!window.dispatch_key(key(KeyAction::down, PhysicalKey::tab)) && window.focused_control() == f.first &&
                f.log == Log{"preview:9", "on_key:down"},
            "is_input_key gives Tab to the control instead of focus traversal");
}

void test_repeat_simultaneous_and_state() {
    Fixture f;
    Window& window = *f.window;
    std::vector<SubscriptionToken> tokens;
    tokens.push_back(window.key_state_changed().subscribe(NoteState{&f.log}));
    tokens.push_back(window.key_down().subscribe(NoteKey{&f.log, "down"}));
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::up)));
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::up, Modifier::none, true)));
    require(f.log == Log{"state:down:82", "down", "on_key:down", "down:repeat", "on_key:down"},
            "an auto-repeat raises KeyDown with repeat and no key_state_changed");
    // Fourteen keys at once, Shift and Alt among them as keys of their own.
    const std::vector<std::uint32_t> held{
        PhysicalKey::up, PhysicalKey::down, PhysicalKey::left, PhysicalKey::right, PhysicalKey::w,
        PhysicalKey::s, PhysicalKey::page_up, PhysicalKey::page_down, PhysicalKey::q, PhysicalKey::e,
        PhysicalKey::r, PhysicalKey::f, PhysicalKey::left_shift, PhysicalKey::right_alt};
    for (const std::uint32_t physical : held) {
        static_cast<void>(window.dispatch_key(key(KeyAction::down, physical)));
    }
    for (const std::uint32_t physical : held) {
        require(window.is_key_down(physical), "every one of fourteen held keys reads as down");
    }
    require(!window.is_key_down(PhysicalKey::z) && !window.is_key_down(PhysicalKey::right_shift),
            "a key not pressed reads as up, the other Shift included");
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::w)));
    require(!window.is_key_down(PhysicalKey::w) && window.is_key_down(PhysicalKey::s),
            "a release ends only its own key");

    // Losing activation releases the rest, with up events marked.
    f.log.clear();
    tokens.push_back(window.key_up().subscribe(NoteKey{&f.log, "up"}));
    tokens.push_back((*f.first).key_up().subscribe(NoteKey{&f.log, "control_up"}));
    window.set_active(false);
    for (const std::uint32_t physical : held) {
        require(!window.is_key_down(physical), "deactivation releases every held key");
    }
    require(f.log.size() == 13U * 3U && f.log[0] == "state:up:82:deactivated" &&
                f.log[1] == "up:deactivated" && f.log[2] == "control_up:deactivated",
            "each released key raises key_state_changed and key_up on the window and the control");
}

void test_key_codes() {
    require(key_code_from_physical_key(PhysicalKey::a) == 0x41U && key_code_from_physical_key(PhysicalKey::z) == 0x5AU,
            "letters are Keys.A to Keys.Z");
    require(key_code_from_physical_key(0x27U) == 0x30U && key_code_from_physical_key(0x1EU) == 0x31U,
            "digits are Keys.D0 to Keys.D9");
    require(key_code_from_physical_key(PhysicalKey::left_shift) == 0x10U &&
                key_code_from_physical_key(PhysicalKey::right_control) == 0x11U &&
                key_code_from_physical_key(PhysicalKey::left_alt) == 0x12U,
            "modifier keys report the side-less ShiftKey, ControlKey and Menu");
    require(key_code_from_physical_key(PhysicalKey::f1) == 0x70U && key_code_from_physical_key(0x68U) == 0x7CU &&
                key_code_from_physical_key(0x59U) == 0x61U && key_code_from_physical_key(PhysicalKey::up) == 0x26U,
            "function keys, keypad and arrows have their Keys codes");
    require(key_code_from_physical_key(0x01U) == 0U, "a usage with no Keys code is zero");
    require(single_key_char("a") == 0x61U && single_key_char("\xC3\xA9") == 0xE9U &&
                single_key_char("\xF0\x9F\x98\x80") == 0x1F600U && single_key_char("ab") == 0U &&
                single_key_char("\xC0\x80") == 0U && single_key_char("") == 0U,
            "single_key_char reads one scalar value and refuses the rest");
}

} // namespace

int main() {
    try {
        test_order_of_one_key();
        test_handled_and_suppress();
        test_composition_and_text();
        test_suppress_key_press_cancels_key_press();
        test_input_key_and_dialog_keys();
        test_repeat_simultaneous_and_state();
        test_key_codes();
        std::cout << "gui_forms_key_events_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_key_events_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
