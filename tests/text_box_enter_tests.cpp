// Enter in a TextBox and the controls built on it: a single-line box
// consumes Enter only while it has something to accept (an open suggestion
// list or appended completion, an input-method composition) or when it
// accepts_enter. Otherwise Enter bubbles to the containers and reaches the
// window's accept button. A multi-line box always takes Enter.

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

// A container that takes every Enter that bubbles to it and records the
// modifiers it came with.
class EnterWatcher final : public Panel {
public:
    explicit EnterWatcher(StableId stable_id) : Panel(std::move(stable_id)) {}

    void on_key_bubble(KeyEvent& event) override {
        if (event.action != KeyAction::down || event.physical_key != PhysicalKey::enter) return;
        seen.push_back(event.modifiers);
        event.handled = true;
    }

    std::vector<Modifier> seen{};
};

struct RecordCommits final {
    std::vector<std::string>* commits{};
    void operator()(const std::string& text) const { (*commits).push_back(text); }
};
struct CountClicks final {
    std::size_t* count{};
    void operator()(ButtonBase&) const { ++(*count); }
};

// A watcher holding one child control, in a window.
struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<EnterWatcher> watcher;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* id, const Control::Ptr& child) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.watcher = make_control<EnterWatcher>(StableId(std::string(id) + ".watcher"));
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

void plain_box_passes_enter_on() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("enter.plain"), "draft");
    Fixture fixture = make_fixture("enter.plain", box);
    Window& window = *fixture.window;
    std::vector<std::string> commits;
    SubscriptionToken token = (*box).committed().subscribe(RecordCommits{&commits});
    require(window.request_focus(box), "the box takes focus");
    require(!(*box).accepts_enter.read(), "a box does not accept Enter by default");
    require(press(window, PhysicalKey::enter) && (*fixture.watcher).seen.size() == 1U &&
                (*fixture.watcher).seen[0] == Modifier::none,
            "with nothing to accept, Enter bubbles to the container");
    require(press(window, PhysicalKey::enter, Modifier::shift) && (*fixture.watcher).seen.size() == 2U &&
                (*fixture.watcher).seen[1] == Modifier::shift,
            "Shift+Enter bubbles to the container too");
    require(commits.empty() && (*box).text.read() == "draft" && window.focused_control() == box,
            "a passed-on Enter raises no committed and leaves the text and focus alone");
    (*box).read_only = true;
    require(press(window, PhysicalKey::enter) && (*fixture.watcher).seen.size() == 3U,
            "a read-only box passes Enter on");
}

void accepting_box_takes_plain_enter() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("enter.accepts"), "draft");
    Fixture fixture = make_fixture("enter.accepts", box);
    Window& window = *fixture.window;
    std::vector<std::string> commits;
    SubscriptionToken token = (*box).committed().subscribe(RecordCommits{&commits});
    (*box).accepts_enter = true;
    require(window.request_focus(box), "the box takes focus");
    require(press(window, PhysicalKey::enter) && commits.size() == 1U && commits[0] == "draft" &&
                (*fixture.watcher).seen.empty(),
            "a box that accepts Enter raises committed with its text and stops the key");
    require(press(window, PhysicalKey::enter, Modifier::shift) && commits.size() == 1U &&
                (*fixture.watcher).seen.size() == 1U,
            "Shift+Enter is not the accepting box's: it reaches the container");
    (*box).read_only = true;
    require(press(window, PhysicalKey::enter) && commits.size() == 2U && (*fixture.watcher).seen.size() == 1U,
            "a read-only box that accepts Enter still commits");
    (*box).read_only = false;
    (*box).accepts_enter = false;
    require(press(window, PhysicalKey::enter) && commits.size() == 2U && (*fixture.watcher).seen.size() == 2U,
            "turning accepts_enter off passes plain Enter on again");
}

void multiline_box_takes_enter() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("enter.multiline"), "one");
    (*box).multiline = true;
    Fixture fixture = make_fixture("enter.multiline", box);
    Window& window = *fixture.window;
    std::vector<std::string> commits;
    SubscriptionToken token = (*box).committed().subscribe(RecordCommits{&commits});
    require(window.request_focus(box), "the box takes focus");
    require(press(window, PhysicalKey::enter) && (*box).text.read() == "one\n" && commits.empty() &&
                (*fixture.watcher).seen.empty(),
            "a multi-line box inserts a newline and keeps Enter, whatever accepts_enter says");
}

void suggestion_list_takes_enter() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("enter.suggest"));
    std::shared_ptr<AutoCompleteStringCollection> towns = (*box).auto_complete_custom_source.read();
    (*towns).add_range({"Calgary", "California", "Cambridge"});
    (*box).auto_complete_source = AutoCompleteSource::custom_source;
    (*box).auto_complete_mode = AutoCompleteMode::suggest;
    Fixture fixture = make_fixture("enter.suggest", box);
    Window& window = *fixture.window;
    std::vector<std::string> commits;
    SubscriptionToken token = (*box).committed().subscribe(RecordCommits{&commits});
    require(window.request_focus(box), "the box takes focus");
    require(type(window, "c") && (*box).auto_complete_open(), "typing opens the list");
    require(press(window, PhysicalKey::down) && press(window, PhysicalKey::enter) &&
                (*box).text.read() == "Calgary" && !(*box).auto_complete_open() &&
                (*fixture.watcher).seen.empty() && commits.empty(),
            "Enter accepts the highlighted suggestion and is consumed");
    require(press(window, PhysicalKey::enter) && (*fixture.watcher).seen.size() == 1U,
            "the next Enter, with nothing left to accept, bubbles");
    require(type(window, "x") && !(*box).auto_complete_open(), "text that matches nothing shows no list");
    (*box).text = "";
    require(type(window, "c") && (*box).auto_complete_open(), "the list opens again");
    require(press(window, PhysicalKey::enter) && !(*box).auto_complete_open() &&
                (*fixture.watcher).seen.size() == 2U,
            "Enter with nothing highlighted closes the list and goes on");
}

void composition_takes_enter() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("enter.ime"));
    (*box).accepts_enter = true;
    Fixture fixture = make_fixture("enter.ime", box);
    Window& window = *fixture.window;
    std::vector<std::string> commits;
    SubscriptionToken token = (*box).committed().subscribe(RecordCommits{&commits});
    require(window.request_focus(box), "the box takes focus");
    require(type(window, "\xE3\x81\x8B", true), "composition text is shown in the box");
    require(press(window, PhysicalKey::enter) && (*fixture.watcher).seen.empty() && commits.empty(),
            "Enter during a composition is consumed and commits nothing");
    TextInputEvent commit{};
    commit.text_utf8 = "\xE5\x8F\xAF";
    commit.replacement_start = 0;
    commit.replacement_length = 1;
    require(window.dispatch_text(commit) && (*box).text.read() == "\xE5\x8F\xAF",
            "the input method commits the composition");
    require(press(window, PhysicalKey::enter) && commits.size() == 1U && commits[0] == "\xE5\x8F\xAF",
            "after the commit, Enter commits the text");

    std::shared_ptr<TextBox> note = make_control<TextBox>(StableId("enter.ime.note"), "a");
    (*note).multiline = true;
    Fixture note_fixture = make_fixture("enter.ime.note", note);
    require((*note_fixture.window).request_focus(note), "the note takes focus");
    require(type(*note_fixture.window, "b", true) && press(*note_fixture.window, PhysicalKey::enter) &&
                (*note).text.read() == "ab",
            "a multi-line box inserts no newline during a composition");
}

void composites_pass_enter_on() {
    std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("enter.combo"));
    (*combo).items.append("Alpha");
    (*combo).drop_down_style = ComboBoxStyle::drop_down;
    Fixture combo_fixture = make_fixture("enter.combo", combo);
    require((*combo_fixture.window).request_focus((*combo).editor()), "the combo editor takes focus");
    require(press(*combo_fixture.window, PhysicalKey::enter) && (*combo_fixture.watcher).seen.size() == 1U,
            "Enter in a closed ComboBox editor bubbles");
    (*combo).dropped_down = true;
    require(press(*combo_fixture.window, PhysicalKey::enter) && !(*combo).dropped_down.read() &&
                (*combo_fixture.watcher).seen.size() == 1U,
            "Enter with the ComboBox list open closes it and is consumed");

    std::shared_ptr<MaskedTextBox> masked = make_control<MaskedTextBox>(StableId("enter.masked"), "000-0000");
    Fixture masked_fixture = make_fixture("enter.masked", masked);
    require((*masked_fixture.window).request_focus((*masked).editor()), "the masked editor takes focus");
    require(press(*masked_fixture.window, PhysicalKey::enter) && (*masked_fixture.watcher).seen.size() == 1U,
            "Enter in a MaskedTextBox bubbles");

    std::shared_ptr<DomainUpDown> domain = make_control<DomainUpDown>(StableId("enter.domain"));
    Fixture domain_fixture = make_fixture("enter.domain", domain);
    require((*domain_fixture.window).request_focus((*domain).editor()), "the domain editor takes focus");
    require(press(*domain_fixture.window, PhysicalKey::enter) && (*domain_fixture.watcher).seen.size() == 1U,
            "Enter in a DomainUpDown bubbles");

    std::shared_ptr<NumericUpDown> numeric = make_control<NumericUpDown>(StableId("enter.numeric"));
    Fixture numeric_fixture = make_fixture("enter.numeric", numeric);
    require((*numeric_fixture.window).request_focus((*numeric).editor()), "the numeric editor takes focus");
    require(press(*numeric_fixture.window, PhysicalKey::enter) && (*numeric_fixture.watcher).seen.empty(),
            "NumericUpDown still takes Enter to commit its value, before its editor");

    std::shared_ptr<TokenBox> tokens = make_control<TokenBox>(StableId("enter.tokens"));
    Fixture token_fixture = make_fixture("enter.tokens", tokens);
    require((*token_fixture.window).request_focus((*tokens).editor()), "the token editor takes focus");
    require(!(*(*tokens).editor()).accepts_enter.read() && press(*token_fixture.window, PhysicalKey::enter) &&
                (*token_fixture.watcher).seen.size() == 1U,
            "an empty TokenBox passes Enter on (W decision 6)");
    require(type(*token_fixture.window, "one") && (*(*tokens).editor()).accepts_enter.read(),
            "with an entry the TokenBox editor accepts Enter");
    require(press(*token_fixture.window, PhysicalKey::enter) &&
                (*tokens).tokens().size() == 1U && (*token_fixture.watcher).seen.size() == 1U,
            "Enter in a TokenBox turns the text into a token and is consumed");
}

void dialog_accept_from_text_box() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("enter.dialog"));
    std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("enter.dialog.field"), "name");
    std::shared_ptr<Button> accept = make_control<Button>(StableId("enter.dialog.accept"), "OK");
    (*field).set_requested_bounds({4.0, 4.0, 120.0, 24.0});
    (*accept).set_requested_bounds({4.0, 36.0, 70.0, 28.0});
    (*accept).dialog_result = DialogResult::ok;
    (*root).add_child(field);
    (*root).add_child(accept);
    Window window(root, {240.0, 104.0});
    window.perform_layout();
    std::size_t clicks = 0U;
    std::vector<std::string> commits;
    SubscriptionToken click_token = (*accept).clicked().subscribe(CountClicks{&clicks});
    SubscriptionToken commit_token = (*field).committed().subscribe(RecordCommits{&commits});
    window.set_accept_button(accept);
    require(window.request_focus(field), "the field takes focus");
    require(press(window, PhysicalKey::enter) && clicks == 1U &&
                window.dialog_result() == DialogResult::ok && commits.empty(),
            "Enter in a text box reaches the window's accept button");
    window.set_dialog_result(DialogResult::none);
    require(press(window, PhysicalKey::enter, Modifier::shift) && clicks == 2U &&
                window.dialog_result() == DialogResult::ok,
            "so does Shift+Enter");
    window.set_dialog_result(DialogResult::none);
    (*field).accepts_enter = true;
    require(press(window, PhysicalKey::enter) && clicks == 2U && commits.size() == 1U &&
                window.dialog_result() == DialogResult::none,
            "a field that accepts Enter commits its own edit and the dialog stays");
}
} // namespace

int main() {
    plain_box_passes_enter_on();
    accepting_box_takes_plain_enter();
    multiline_box_takes_enter();
    suggestion_list_takes_enter();
    composition_takes_enter();
    composites_pass_enter_on();
    dialog_accept_from_text_box();
    std::cout << "text box Enter tests passed\n";
    return 0;
}
