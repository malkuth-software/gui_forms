// Escape in a TextBox and the controls built on it: the box consumes Escape
// only while it has something to cancel (an open suggestion list or appended
// completion, an input-method composition, a selection drag) or when it
// accepts_escape. Otherwise Escape, with or without Shift, bubbles to the
// containers and reaches the window's cancel button.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/auto_complete.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/combo_box_values.hpp"
#include "gui_forms/controls/domain_up_down.hpp"
#include "gui_forms/controls/masked_text_box.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/token_box.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// A container that takes every Escape that bubbles to it, as a docking
// panel or a menu host would, and records the modifiers it came with.
class EscapeWatcher final : public Panel {
public:
    explicit EscapeWatcher(StableId stable_id) : Panel(std::move(stable_id)) {}

    void on_key_bubble(KeyEvent& event) override {
        if (event.action != KeyAction::down || event.physical_key != PhysicalKey::escape) return;
        seen.push_back(event.modifiers);
        event.handled = true;
    }

    std::vector<Modifier> seen{};
};

struct CountCalls final {
    std::size_t* count{};
    void operator()() const { ++(*count); }
};
struct CountClicks final {
    std::size_t* count{};
    void operator()(ButtonBase&) const { ++(*count); }
};

// A watcher holding one child control, in a window.
struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<EscapeWatcher> watcher;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* id, const Control::Ptr& child) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.watcher = make_control<EscapeWatcher>(StableId(std::string(id) + ".watcher"));
    (*fixture.watcher).set_requested_bounds({0.0, 0.0, 320.0, 300.0});
    (*child).set_requested_bounds({10.0, 10.0, 200.0, 28.0});
    (*fixture.watcher).add_child(child);
    (*fixture.root).add_child(fixture.watcher);
    fixture.window = std::make_unique<Window>(fixture.root, Size{320.0, 300.0});
    (*fixture.window).perform_layout();
    return fixture;
}

bool press(Window& window, std::uint32_t key, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, key, modifiers};
    const bool handled = window.dispatch_key(event);
    return handled;
}

bool type(Window& window, const char* text, bool composing = false) {
    TextInputEvent input{};
    input.text_utf8 = text;
    input.composing = composing;
    const bool handled = window.dispatch_text(input);
    return handled;
}

void plain_box_passes_escape_on() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("esc.plain"), "draft");
    Fixture fixture = make_fixture("esc.plain", box);
    Window& window = *fixture.window;
    std::size_t cancels = 0U;
    SubscriptionToken token = (*box).cancelled().subscribe(CountCalls{&cancels});
    require(window.request_focus(box), "the box takes focus");
    require(!(*box).accepts_escape.read(), "a box does not accept Escape by default");
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.size() == 1U &&
                (*fixture.watcher).seen[0] == Modifier::none,
            "with nothing to cancel, Escape bubbles to the container");
    require(press(window, PhysicalKey::escape, Modifier::shift) && (*fixture.watcher).seen.size() == 2U &&
                (*fixture.watcher).seen[1] == Modifier::shift,
            "Shift+Escape bubbles to the container too");
    require(cancels == 0U && (*box).text.read() == "draft" && window.focused_control() == box,
            "a passed-on Escape raises no cancelled and leaves the text and focus alone");

    (*box).multiline = true;
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.size() == 3U,
            "a multi-line box passes Escape on as well");
    (*box).multiline = false;

    (*box).read_only = true;
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.size() == 4U,
            "a read-only box passes Escape on");
}

void accepting_box_takes_plain_escape() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("esc.accepts"), "draft");
    Fixture fixture = make_fixture("esc.accepts", box);
    Window& window = *fixture.window;
    std::size_t cancels = 0U;
    SubscriptionToken token = (*box).cancelled().subscribe(CountCalls{&cancels});
    (*box).accepts_escape = true;
    require(window.request_focus(box), "the box takes focus");
    require(press(window, PhysicalKey::escape) && cancels == 1U && (*fixture.watcher).seen.empty(),
            "a box that accepts Escape raises cancelled and stops the key");
    require(press(window, PhysicalKey::escape, Modifier::shift) && cancels == 1U &&
                (*fixture.watcher).seen.size() == 1U && (*fixture.watcher).seen[0] == Modifier::shift,
            "Shift+Escape is not the accepting box's: it reaches the container");
    require(press(window, PhysicalKey::escape, Modifier::control) && cancels == 1U &&
                (*fixture.watcher).seen.size() == 2U,
            "nor is Ctrl+Escape");
    (*box).accepts_escape = false;
    require(press(window, PhysicalKey::escape) && cancels == 1U && (*fixture.watcher).seen.size() == 3U,
            "turning accepts_escape off passes plain Escape on again");
}

void suggestion_list_takes_escape() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("esc.suggest"));
    std::shared_ptr<AutoCompleteStringCollection> towns = (*box).auto_complete_custom_source.read();
    (*towns).add_range({"Calgary", "California", "Cambridge"});
    (*box).auto_complete_source = AutoCompleteSource::custom_source;
    (*box).auto_complete_mode = AutoCompleteMode::suggest_append;
    Fixture fixture = make_fixture("esc.suggest", box);
    Window& window = *fixture.window;
    require(window.request_focus(box), "the box takes focus");
    require(type(window, "c") && (*box).auto_complete_open() && (*box).text.read() == "calgary",
            "typing opens the list and appends a completion");
    require(press(window, PhysicalKey::escape) && !(*box).auto_complete_open() && (*box).text.read() == "c" &&
                (*fixture.watcher).seen.empty(),
            "Escape closes the list, removes the completion and is consumed");
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.size() == 1U,
            "the next Escape, with nothing left to cancel, bubbles");
    require(type(window, "a") && (*box).auto_complete_open(), "the list opens again");
    require(press(window, PhysicalKey::escape, Modifier::shift) && !(*box).auto_complete_open() &&
                (*fixture.watcher).seen.size() == 1U,
            "Shift+Escape also closes an open list first");
}

void composition_takes_escape() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("esc.ime"));
    Fixture fixture = make_fixture("esc.ime", box);
    Window& window = *fixture.window;
    require(window.request_focus(box), "the box takes focus");
    require(type(window, "\xE3\x81\x8B", true) && (*box).text.read() == "\xE3\x81\x8B",
            "composition text is shown in the box");
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.empty() &&
                (*box).text.read() == "\xE3\x81\x8B",
            "Escape during a composition is consumed and leaves the text to the input method");
    require(press(window, PhysicalKey::escape, Modifier::shift) && (*fixture.watcher).seen.empty(),
            "so is Shift+Escape");
    TextInputEvent commit{};
    commit.text_utf8 = "\xE5\x8F\xAF";
    commit.replacement_start = 0;
    commit.replacement_length = 1;
    require(window.dispatch_text(commit) && (*box).text.read() == "\xE5\x8F\xAF",
            "the input method commits the composition");
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.size() == 1U,
            "after the commit, Escape bubbles again");

    require(type(window, "a", true) && press(window, PhysicalKey::escape) &&
                (*fixture.watcher).seen.size() == 1U,
            "a new composition takes Escape again");
    TextInputEvent cancel{};
    cancel.composing = true;
    cancel.replacement_start = 1;
    cancel.replacement_length = 1;
    static_cast<void>(window.dispatch_text(cancel));
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.size() == 2U,
            "an empty composition ends it, and Escape bubbles");

    require(type(window, "b", true), "a composition starts again");
    (*box).text = "reset";
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.size() == 3U,
            "set_text abandons the composition");
}

// Audit N106: each update of a composition replaces its marked text, and the
// commit replaces it too, when the input method names no range.
void composition_updates_replace_marked_text() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("esc.marked"), "x");
    Fixture fixture = make_fixture("esc.marked", box);
    Window& window = *fixture.window;
    require(window.request_focus(box), "the box takes focus");
    require(type(window, "k", true) && (*box).text.read() == "xk", "the first update inserts at the caret");
    require(type(window, "ka", true) && (*box).text.read() == "xka", "the second update replaces the first");
    require(type(window, "\xE3\x81\x8B", true) && (*box).text.read() == "x\xE3\x81\x8B",
            "the converted update replaces the romaji");
    require(type(window, "\xE3\x81\x8B") && (*box).text.read() == "x\xE3\x81\x8B",
            "the commit replaces the marked text once");
    require(type(window, "y") && (*box).text.read() == "x\xE3\x81\x8By", "after the commit, typing inserts");
    require(type(window, "z", true) && type(window, "", true) && (*box).text.read() == "x\xE3\x81\x8By",
            "an empty update cancels the composition and removes its text");
}

void selection_drag_takes_escape() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("esc.drag"), "drag across this text");
    Fixture fixture = make_fixture("esc.drag", box);
    Window& window = *fixture.window;
    require(window.request_focus(box), "the box takes focus");
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, Point{14.0, 20.0}}) &&
                window.dispatch_pointer({PointerAction::move, PointerButton::primary, Point{120.0, 20.0}}),
            "a press and a move start a selection drag");
    require((*box).has_pointer_capture() && !(*box).selection().empty(), "the drag holds capture and selects");
    const TextSelection dragged = (*box).selection();
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.empty() &&
                !(*box).has_pointer_capture() && (*box).selection() == dragged,
            "Escape ends the drag, keeps the selection and is consumed");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::primary, Point{180.0, 20.0}}));
    require((*box).selection() == dragged, "after Escape, moving no longer selects");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, Point{180.0, 20.0}}));
    require(press(window, PhysicalKey::escape) && (*fixture.watcher).seen.size() == 1U,
            "with the drag over, Escape bubbles");
}

void composites_pass_escape_on() {
    std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("esc.combo"));
    (*combo).items.append("Alpha");
    (*combo).items.append("Beta");
    (*combo).drop_down_style = ComboBoxStyle::drop_down;
    Fixture combo_fixture = make_fixture("esc.combo", combo);
    require((*combo_fixture.window).request_focus((*combo).editor()), "the combo editor takes focus");
    require(press(*combo_fixture.window, PhysicalKey::escape) &&
                (*combo_fixture.watcher).seen.size() == 1U,
            "Escape in a closed ComboBox editor bubbles");
    (*combo).dropped_down = true;
    require(press(*combo_fixture.window, PhysicalKey::escape) && !(*combo).dropped_down.read() &&
                (*combo_fixture.watcher).seen.size() == 1U,
            "Escape with the ComboBox list open closes it and is consumed");

    std::shared_ptr<MaskedTextBox> masked = make_control<MaskedTextBox>(StableId("esc.masked"), "000-0000");
    Fixture masked_fixture = make_fixture("esc.masked", masked);
    require((*masked_fixture.window).request_focus((*masked).editor()), "the masked editor takes focus");
    require(press(*masked_fixture.window, PhysicalKey::escape, Modifier::shift) &&
                (*masked_fixture.watcher).seen.size() == 1U,
            "Shift+Escape in a MaskedTextBox bubbles");

    std::shared_ptr<DomainUpDown> domain = make_control<DomainUpDown>(StableId("esc.domain"));
    Fixture domain_fixture = make_fixture("esc.domain", domain);
    require((*domain_fixture.window).request_focus((*domain).editor()), "the domain editor takes focus");
    require(press(*domain_fixture.window, PhysicalKey::escape) && (*domain_fixture.watcher).seen.size() == 1U,
            "Escape in a DomainUpDown bubbles");

    std::shared_ptr<NumericUpDown> numeric = make_control<NumericUpDown>(StableId("esc.numeric"));
    Fixture numeric_fixture = make_fixture("esc.numeric", numeric);
    require((*numeric_fixture.window).request_focus((*numeric).editor()), "the numeric editor takes focus");
    require(press(*numeric_fixture.window, PhysicalKey::escape) && (*numeric_fixture.watcher).seen.size() == 1U,
            "Escape in a NumericUpDown bubbles");

    std::shared_ptr<TokenBox> tokens = make_control<TokenBox>(StableId("esc.tokens"));
    static_cast<void>((*tokens).add_token("one"));
    Fixture token_fixture = make_fixture("esc.tokens", tokens);
    require((*token_fixture.window).request_focus((*tokens).editor()), "the token editor takes focus");
    require(press(*token_fixture.window, PhysicalKey::left) && (*tokens).selected_token(),
            "Left at the start selects the last token");
    require(press(*token_fixture.window, PhysicalKey::escape) && !(*tokens).selected_token() &&
                (*token_fixture.watcher).seen.empty(),
            "Escape with a token selected clears the selection and is consumed");
    require(press(*token_fixture.window, PhysicalKey::escape) && (*token_fixture.watcher).seen.size() == 1U,
            "Escape with nothing selected bubbles");
}

void dialog_cancel_from_text_box() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("esc.dialog"));
    std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("esc.dialog.field"), "name");
    std::shared_ptr<Button> cancel = make_control<Button>(StableId("esc.dialog.cancel"), "Cancel");
    (*field).set_requested_bounds({4.0, 4.0, 120.0, 24.0});
    (*cancel).set_requested_bounds({4.0, 36.0, 70.0, 28.0});
    (*root).add_child(field);
    (*root).add_child(cancel);
    Window window(root, {240.0, 104.0});
    window.perform_layout();
    std::size_t clicks = 0U;
    std::size_t cancels = 0U;
    SubscriptionToken click_token = (*cancel).clicked().subscribe(CountClicks{&clicks});
    SubscriptionToken cancel_token = (*field).cancelled().subscribe(CountCalls{&cancels});
    window.set_cancel_button(cancel);
    require(window.request_focus(field), "the field takes focus");
    require(press(window, PhysicalKey::escape) && clicks == 1U &&
                window.dialog_result() == DialogResult::cancel && cancels == 0U,
            "Escape in a text box reaches the window's cancel button");
    window.set_dialog_result(DialogResult::none);
    require(press(window, PhysicalKey::escape, Modifier::shift) && clicks == 2U &&
                window.dialog_result() == DialogResult::cancel,
            "so does Shift+Escape");
    window.set_dialog_result(DialogResult::none);
    (*field).accepts_escape = true;
    require(press(window, PhysicalKey::escape) && clicks == 2U && cancels == 1U &&
                window.dialog_result() == DialogResult::none,
            "a field that accepts Escape cancels its own edit and the dialog stays");
}
} // namespace

int main() {
    plain_box_passes_escape_on();
    accepting_box_takes_plain_escape();
    suggestion_list_takes_escape();
    composition_takes_escape();
    composition_updates_replace_marked_text();
    selection_drag_takes_escape();
    composites_pass_escape_on();
    dialog_cancel_from_text_box();
    std::cout << "text box Escape tests passed\n";
    return 0;
}
