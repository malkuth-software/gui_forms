// TokenBox (stage W, matrix row 56): tokens with stable identities, entries
// accepted by Enter, delimiters and auto-complete, Backspace and the arrow
// keys over tokens, the close glyph, the published parts and states, the
// wrapping layout, semantics and refusals.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct TraceAdded final {
    std::vector<std::string>* trace{};
    void operator()(const Token& token) const { (*trace).push_back("added " + token.text); }
};
struct TraceRemoved final {
    std::vector<std::string>* trace{};
    void operator()(const Token& token) const { (*trace).push_back("removed " + token.text); }
};
struct TraceSelected final {
    std::vector<std::string>* trace{};
    const TokenBox* box{};
    void operator()(std::optional<TokenId> id) const {
        if (!id) {
            (*trace).push_back("selected none");
            return;
        }
        const std::optional<std::size_t> index = (*box).token_index(*id);
        (*trace).push_back("selected " + (index ? (*box).tokens()[*index].text : std::string("?")));
    }
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<TokenBox> box;
    std::shared_ptr<TextBox> other;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* id, double width = 300.0, double height = 60.0) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.box = make_control<TokenBox>(StableId(std::string(id)));
    (*fixture.box).set_requested_bounds({10.0, 10.0, width, height});
    fixture.other = make_control<TextBox>(StableId(std::string(id) + ".other"));
    (*fixture.other).set_requested_bounds({10.0, 280.0, 200.0, 28.0});
    (*fixture.root).add_child(fixture.box);
    (*fixture.root).add_child(fixture.other);
    fixture.window = std::make_unique<Window>(fixture.root, Size{400.0, 320.0});
    (*fixture.window).perform_layout();
    require((*fixture.window).request_focus((*fixture.box).editor()), "the editor takes the focus");
    return fixture;
}

bool type(Window& window, const char* text) {
    TextInputEvent input{};
    input.text_utf8 = text;
    const bool handled = window.dispatch_text(input);
    return handled;
}

bool press(Window& window, std::uint32_t key, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, key, modifiers};
    const bool handled = window.dispatch_key(event);
    return handled;
}

bool texts_are(const TokenBox& box, std::vector<std::string> expected) {
    const std::span<const Token> tokens = box.tokens();
    if (tokens.size() != expected.size()) return false;
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
        if (tokens[index].text != expected[index]) return false;
    }
    return true;
}

void tokens_and_identity() {
    std::shared_ptr<TokenBox> box = make_control<TokenBox>(StableId("tokens.model"));
    std::vector<std::string> trace;
    SubscriptionToken added = (*box).token_added().subscribe(TraceAdded{&trace});
    SubscriptionToken removed = (*box).token_removed().subscribe(TraceRemoved{&trace});
    const TokenId first = (*box).add_token("  alpha ");
    const TokenId second = (*box).add_token("beta");
    require(texts_are(*box, {"alpha", "beta"}) && first != second && second.value > first.value,
            "add_token trims the text and gives each token a new identity");
    require((*box).token_index(second) == 1U && trace.size() == 2U && trace[0] == "added alpha",
            "token_index finds a token by identity and token_added reports each addition");
    bool threw = false;
    try {
        static_cast<void>((*box).add_token("   "));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*box).tokens().size() == 2U, "empty text is refused");
    threw = false;
    try {
        static_cast<void>((*box).add_token(std::string("\xff", 1U)));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*box).tokens().size() == 2U, "invalid UTF-8 is refused");
    require((*box).remove_token(first) && !(*box).remove_token(first) && texts_are(*box, {"beta"}) &&
                !(*box).token_index(first) && (*box).token_index(second) == 0U,
            "removing by identity works once; the identity is then stale and the other keeps its own");
    const TokenId third = (*box).add_token("gamma");
    require(third.value > second.value, "identities are never reused");
    threw = false;
    try {
        (*box).select_token(first);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && !(*box).selected_token(), "a stale identity cannot be selected");
    (*box).clear_tokens();
    require((*box).tokens().empty() && trace.back() == "removed beta", "clear removes the last token last");
    threw = false;
    try {
        (*box).delimiters = "a,";
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*box).delimiters.read() == ",;", "a letter is not a delimiter");
    (*box).delimiters = " ";
    require((*box).delimiters.read() == " ", "the space may be a delimiter");
    threw = false;
    try {
        (*box).auto_complete_source = AutoCompleteSource::list_items;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a token box has no list items to complete from");
}

struct CountClicks final {
    std::size_t* count{};
    void operator()(ButtonBase&) const { ++(*count); }
};

// An empty token box passes Enter on to the window's accept button; an
// entry takes it (owner decision W-6).
void empty_entry_passes_enter_on() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("tokens.dialog"));
    std::shared_ptr<TokenBox> box = make_control<TokenBox>(StableId("tokens.dialog.box"));
    std::shared_ptr<Button> accept = make_control<Button>(StableId("tokens.dialog.accept"), "OK");
    (*box).set_requested_bounds({10.0, 10.0, 300.0, 60.0});
    (*accept).set_requested_bounds({10.0, 90.0, 80.0, 28.0});
    (*accept).set_dialog_result(DialogResult::ok);
    (*root).add_child(box);
    (*root).add_child(accept);
    Window window(root, {400.0, 160.0});
    window.perform_layout();
    std::size_t clicks = 0U;
    SubscriptionToken click_token = (*accept).clicked().subscribe(CountClicks{&clicks});
    window.set_accept_button(accept);
    require(window.request_focus((*box).editor()), "the editor takes focus");
    require(press(window, PhysicalKey::enter) && clicks == 1U && window.dialog_result() == DialogResult::ok &&
                (*box).tokens().empty(),
            "Enter in an empty token box reaches the accept button");
    window.set_dialog_result(DialogResult::none);
    require(type(window, "   ") && press(window, PhysicalKey::enter) && clicks == 2U && (*box).tokens().empty(),
            "an entry of only spaces passes Enter on too");
    window.set_dialog_result(DialogResult::none);
    (*(*box).editor()).text = "";
    require(type(window, "tag") && press(window, PhysicalKey::enter) && clicks == 2U &&
                (*box).tokens().size() == 1U && window.dialog_result() == DialogResult::none,
            "with an entry, Enter makes a token and the dialog stays");
    require(press(window, PhysicalKey::enter) && clicks == 3U, "emptied again, Enter goes on");
}

void entries_become_tokens() {
    Fixture fixture = make_fixture("tokens.entries");
    Window& window = *fixture.window;
    TokenBox& box = *fixture.box;
    TextBox& editor = *box.editor();
    std::vector<std::string> trace;
    SubscriptionToken added = box.token_added().subscribe(TraceAdded{&trace});
    require(type(window, "alpha") && press(window, PhysicalKey::enter) && texts_are(box, {"alpha"}) &&
                editor.text.read().empty() && trace.size() == 1U,
            "Enter turns the typed entry into a token and empties the editor");
    require(!press(window, PhysicalKey::enter) && box.tokens().size() == 1U,
            "Enter on an empty entry adds nothing and is not taken (W decision 6)");
    require(type(window, "beta,gam") && texts_are(box, {"alpha", "beta"}) && editor.text.read() == "gam",
            "a delimiter ends an entry; the text after it stays in the editor");
    require(type(window, "ma;") && texts_are(box, {"alpha", "beta", "gamma"}) && editor.text.read().empty(),
            "each delimiter takes what was typed before it");
    require(type(window, " delta , ,epsilon;zeta") &&
                texts_are(box, {"alpha", "beta", "gamma", "delta", "epsilon"}) && editor.text.read() == "zeta",
            "text with several delimiters makes several tokens, skipping empty pieces");
    box.delimiters = "";
    require(type(window, ",") && editor.text.read() == "zeta," && box.tokens().size() == 5U,
            "with no delimiters only Enter ends an entry");
}

void keyboard_over_tokens() {
    Fixture fixture = make_fixture("tokens.keys", 360.0);
    Window& window = *fixture.window;
    TokenBox& box = *fixture.box;
    TextBox& editor = *box.editor();
    static_cast<void>(box.add_token("alpha"));
    static_cast<void>(box.add_token("beta"));
    static_cast<void>(box.add_token("gamma"));
    std::vector<std::string> trace;
    SubscriptionToken removed = box.token_removed().subscribe(TraceRemoved{&trace});
    SubscriptionToken selected = box.selected_token_changed().subscribe(TraceSelected{&trace, &box});
    require(press(window, PhysicalKey::backspace) && box.selected_token() == box.tokens()[2].id &&
                box.tokens().size() == 3U,
            "Backspace at the start of an empty editor selects the last token");
    require(press(window, PhysicalKey::backspace) && texts_are(box, {"alpha", "beta"}) && !box.selected_token(),
            "a second Backspace removes it");
    require(trace.size() == 3U && trace[0] == "selected gamma" && trace[1] == "selected none" &&
                trace[2] == "removed gamma",
            "removing a selected token clears the selection, then reports the removal");
    require(press(window, PhysicalKey::left) && box.selected_token() == box.tokens()[1].id,
            "Left at the start selects the last token");
    require(press(window, PhysicalKey::left) && box.selected_token() == box.tokens()[0].id &&
                press(window, PhysicalKey::left) && box.selected_token() == box.tokens()[0].id,
            "Left moves to the previous token and stops at the first");
    require(press(window, PhysicalKey::right) && box.selected_token() == box.tokens()[1].id &&
                press(window, PhysicalKey::right) && !box.selected_token(),
            "Right moves to the next token, and past the last back to the editor");
    require(press(window, PhysicalKey::left) && press(window, PhysicalKey::home) &&
                box.selected_token() == box.tokens()[0].id && press(window, PhysicalKey::end) &&
                !box.selected_token(),
            "Home selects the first token and End returns to the editor");
    require(press(window, PhysicalKey::left) && press(window, PhysicalKey::escape) && !box.selected_token(),
            "Escape returns to the editor");
    require(press(window, PhysicalKey::left) && press(window, PhysicalKey::delete_forward) &&
                texts_are(box, {"alpha"}),
            "Delete removes the selected token");
    require(press(window, PhysicalKey::left) && type(window, "x") && !box.selected_token() && editor.text.read() == "x",
            "typing returns to the editor");
    require(press(window, PhysicalKey::backspace) && editor.text.read().empty() && box.tokens().size() == 1U &&
                !box.selected_token(),
            "Backspace with text before the caret edits the text");
    require(press(window, PhysicalKey::left) && box.selected_token() && window.request_focus(fixture.other) &&
                !box.selected_token(),
            "losing the focus clears the token selection");
}

void pointer_and_parts() {
    Fixture fixture = make_fixture("tokens.pointer", 300.0, 60.0);
    Window& window = *fixture.window;
    TokenBox& box = *fixture.box;
    const TokenId alpha = box.add_token("alpha");
    const TokenId beta = box.add_token("beta");
    window.perform_layout();
    std::vector<TokenBox::Item> items = box.realized_items();
    require(items.size() == 2U && items[0].id == alpha &&
                items[1].stable_id == "tokens.pointer.token." + std::to_string(beta.value),
            "each laid-out token is a realized item named by its identity");
    const TokenBox::ItemParts first = items[0].parts;
    require(first.token_face.present && first.token_label.present && first.token_close.present &&
                first.token_face.bounds.contains(Point{first.token_label.bounds.x + 1.0,
                                                       first.token_label.bounds.y + 1.0}) &&
                first.token_close.bounds.x > first.token_label.bounds.x,
            "a token publishes its face, label and close glyph, the glyph after the label");
    const TokenBox::Parts parts = box.parts();
    require(parts.well.present && parts.field.present &&
                parts.field.bounds.x > items[1].parts.token_face.bounds.x,
            "the editor's field follows the last token");
    const Rect absolute = box.absolute_bounds();
    const Rect face = items[1].parts.token_face.bounds;
    const Point on_face{absolute.x + face.x + 4.0, absolute.y + face.y + face.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, on_face}));
    require(box.realized_items()[1].states.hot, "the token under the pointer is hot");
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, on_face}) &&
                box.selected_token() == beta && window.focused_control() == box.editor(),
            "pressing a token selects it and keeps the focus in the editor");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, on_face}));
    const TokenBox::Item selected_item = box.realized_items()[1];
    require(selected_item.states.selected && box.states().focused, "the selected token publishes selected");

    const Rect close = items[0].parts.token_close.bounds;
    const Point on_close{absolute.x + close.x + close.width * 0.5, absolute.y + close.y + close.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, on_close}));
    require(box.realized_items()[0].parts.token_close.states.contains(AnatomyState::hot),
            "the close glyph under the pointer is hot");
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, on_close}) &&
                box.realized_items()[0].parts.token_close.states.contains(AnatomyState::pressed) &&
                box.tokens().size() == 2U,
            "pressing the close glyph shows it pressed and removes nothing yet");
    const Point away{absolute.x + 250.0, absolute.y + 40.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, away}));
    require(box.tokens().size() == 2U, "releasing away from the glyph cancels");
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, on_close}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, on_close}) &&
                texts_are(box, {"beta"}),
            "releasing over the close glyph removes its token");
}

void wrapping_layout() {
    Fixture fixture = make_fixture("tokens.wrap", 200.0, 60.0);
    Window& window = *fixture.window;
    TokenBox& box = *fixture.box;
    for (int index = 0; index < 8; ++index) {
        static_cast<void>(box.add_token("token " + std::to_string(index)));
    }
    window.perform_layout();
    const std::vector<TokenBox::Item> items = box.realized_items();
    require(items.size() >= 2U && items.size() < 8U,
            "tokens wrap onto further lines and only the lines that fit are realized");
    require(items.back().parts.token_face.bounds.y > items.front().parts.token_face.bounds.y,
            "a later line sits below the first");
    const Size desired = box.measure(Size{200.0, 1000.0});
    require(desired.height > 60.0, "measure asks for the height every line needs");
}

void semantics() {
    Fixture fixture = make_fixture("tokens.semantics", 360.0);
    Window& window = *fixture.window;
    TokenBox& box = *fixture.box;
    box.accessible_name = "Recipients";
    const TokenId alpha = box.add_token("alpha");
    const TokenId beta = box.add_token("beta");
    window.perform_layout();
    const SemanticDescriptor descriptor = box.semantic_descriptor();
    require(descriptor.role == SemanticRole::list && descriptor.name == "Recipients",
            "the box publishes the list of its tokens");
    const std::vector<SemanticNode> nodes = box.semantic_virtual_children();
    const std::string beta_id = "tokens.semantics.token." + std::to_string(beta.value);
    require(nodes.size() == 2U && nodes[1].stable_id == beta_id && nodes[1].role == SemanticRole::list_item &&
                nodes[1].name == "beta" && nodes[1].position_in_set == 2U && nodes[1].set_size == 2U,
            "each token is a list item with its position in the set");
    require(nodes[0].children.size() == 1U && nodes[0].children[0].role == SemanticRole::button &&
                nodes[0].children[0].name == "Remove alpha",
            "each token offers a remove button");
    require(window.perform_semantic_action(beta_id, SemanticAction::select) && box.selected_token() == beta &&
                has_semantic_state(box.semantic_virtual_children()[1].states, SemanticState::selected),
            "a semantic select selects the token");
    const std::string remove_alpha = "tokens.semantics.token." + std::to_string(alpha.value) + ".remove";
    require(window.perform_semantic_action(remove_alpha, SemanticAction::press) && texts_are(box, {"beta"}),
            "pressing a token's remove button removes it");
}

void auto_complete_makes_tokens() {
    Fixture fixture = make_fixture("tokens.complete", 360.0, 40.0);
    Window& window = *fixture.window;
    TokenBox& box = *fixture.box;
    (*box.auto_complete_custom_source.read()).add_range({"Dartmouth", "Derby", "Dover"});
    box.auto_complete_source = AutoCompleteSource::custom_source;
    box.auto_complete_mode = AutoCompleteMode::suggest;
    require(box.auto_complete_mode.read() == AutoCompleteMode::suggest &&
                (*box.editor()).auto_complete_mode.read() == AutoCompleteMode::suggest,
            "the settings are the editor's");
    require(type(window, "d") && (*box.editor()).auto_complete_open(), "typing suggests");
    window.perform_layout();
    const Rect list = (*window.find("tokens.complete.editor.auto-complete.list")).absolute_bounds();
    require(list.width == 360.0, "the suggestions span the whole box");
    require(press(window, PhysicalKey::down) && press(window, PhysicalKey::down) &&
                press(window, PhysicalKey::enter) && texts_are(box, {"Derby"}) &&
                (*box.editor()).text.read().empty() && !(*box.editor()).auto_complete_open(),
            "accepting a suggestion makes it a token");
    box.auto_complete_mode = AutoCompleteMode::append;
    require(type(window, "do") && (*box.editor()).text.read() == "dover" && press(window, PhysicalKey::tab) &&
                texts_are(box, {"Derby", "dover"}) && window.focused_control() == box.editor(),
            "Tab takes an appended completion as a token");
}

} // namespace

int main() {
    tokens_and_identity();
    entries_become_tokens();
    empty_entry_passes_enter_on();
    keyboard_over_tokens();
    pointer_and_parts();
    wrapping_layout();
    semantics();
    auto_complete_makes_tokens();
    std::cout << "token box tests passed\n";
    return 0;
}
