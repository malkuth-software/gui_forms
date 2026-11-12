// Auto-complete (stage W, matrix row 56): AutoCompleteStringCollection, the
// shared text matcher, and TextBox's suggest, append and suggest-append
// modes: the overlay's open, highlight, accept and cancel order, the keyboard
// and pointer traces, composition input never completed, host sources
// reported unsupported, and the semantics a suggesting box publishes. Then
// ComboBox's editable drop_down style and its auto-complete settings.

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/auto_complete.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/combo_box_values.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/host/types/host_capabilities/host_capabilities.hpp"
#include "gui_forms/host/types/host_capability/host_capability.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

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

// Appends one line per event to a shared trace, so tests can check order.
struct TraceText final {
    std::vector<std::string>* trace{};
    void operator()(const std::string& text) const { (*trace).push_back("text " + text); }
};
struct TraceOpen final {
    std::vector<std::string>* trace{};
    void operator()(bool open) const { (*trace).push_back(open ? "open" : "close"); }
};
struct TraceAccepted final {
    std::vector<std::string>* trace{};
    void operator()(const std::string& text) const { (*trace).push_back("accepted " + text); }
};
struct TraceCancelled final {
    std::vector<std::string>* trace{};
    void operator()() const { (*trace).push_back("cancelled"); }
};
struct TraceCommitted final {
    std::vector<std::string>* trace{};
    void operator()(const std::string& text) const { (*trace).push_back("committed " + text); }
};
struct AcceptEveryEdit final {
    void filter(TextEdit&) {}
};
struct CountChanges final {
    std::size_t* count{};
    void operator()() const { ++(*count); }
};

// A box in a window with room for its suggestion list below it.
struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<TextBox> box;
    std::shared_ptr<TextBox> other;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* id, AutoCompleteMode mode) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.box = make_control<TextBox>(StableId(std::string(id)));
    (*fixture.box).set_requested_bounds({10.0, 10.0, 200.0, 28.0});
    fixture.other = make_control<TextBox>(StableId(std::string(id) + ".other"));
    (*fixture.other).set_requested_bounds({10.0, 260.0, 200.0, 28.0});
    (*fixture.root).add_child(fixture.box);
    (*fixture.root).add_child(fixture.other);
    std::shared_ptr<AutoCompleteStringCollection> cities = (*fixture.box).auto_complete_custom_source.read();
    (*cities).add_range({"Calgary", "California", "Cambridge", "calgary", "Dover", "Calgary", "Caen"});
    (*fixture.box).auto_complete_source = AutoCompleteSource::custom_source;
    (*fixture.box).auto_complete_mode = mode;
    fixture.window = std::make_unique<Window>(fixture.root, Size{320.0, 300.0});
    (*fixture.window).perform_layout();
    require((*fixture.window).request_focus(fixture.box), "the box takes focus");
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

void collection_contract() {
    AutoCompleteStringCollection collection;
    std::size_t changes = 0U;
    SubscriptionToken token = collection.changed().subscribe(CountChanges{&changes});
    require(collection.add("Oslo") == 0U && collection.add("Bergen") == 1U && changes == 2U,
            "add appends, returns the new index and raises changed once each");
    collection.insert(0U, "Tromsø");
    collection.set(2U, "Stavanger");
    require(collection.size() == 3U && collection.at(0U) == "Tromsø" && collection.at(2U) == "Stavanger" &&
                collection.contains("Oslo") && collection.index_of("Oslo") == 1U &&
                !collection.index_of("Bergen") && changes == 4U,
            "insert and set place items and raise changed");
    bool threw = false;
    try {
        collection.add(std::string("\xff", 1U));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && collection.size() == 3U && changes == 4U,
            "invalid UTF-8 throws and changes nothing");
    threw = false;
    try {
        collection.insert(9U, "Bodø");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw && changes == 4U, "an insertion past the end throws and changes nothing");
    threw = false;
    try {
        collection.add_range({"Narvik", std::string("\xc3", 1U)});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && collection.size() == 3U, "add_range validates every item before adding any");
    require(collection.remove("Oslo") && !collection.remove("Oslo") && collection.size() == 2U && changes == 5U,
            "remove takes out the first equal item and reports whether there was one");
    collection.remove_at(0U);
    collection.clear();
    collection.clear();
    require(collection.empty() && changes == 7U, "clearing an empty collection raises nothing");
}

void text_matching() {
    require(find_text_match("California", "cal", TextMatchMode::prefix, false) == 0U,
            "a prefix matches ignoring ASCII case");
    require(!find_text_match("California", "cal", TextMatchMode::prefix, true),
            "a case-sensitive prefix compares case");
    require(!find_text_match("Cal", "Calgary", TextMatchMode::prefix, false),
            "a pattern longer than the candidate does not match");
    require(find_text_match("New Calgary", "CALG", TextMatchMode::substring, false) == 4U,
            "a substring match reports where it starts");
    require(!find_text_match("Calgary", "", TextMatchMode::substring, false),
            "an empty pattern matches nothing");
    require(!find_text_match("Évora", "é", TextMatchMode::prefix, false) &&
                find_text_match("Évora", "É", TextMatchMode::prefix, false) == 0U,
            "letters outside ASCII compare exactly");
}

void suggest_keyboard_trace() {
    Fixture fixture = make_fixture("ac.suggest", AutoCompleteMode::suggest);
    Window& window = *fixture.window;
    TextBox& box = *fixture.box;
    std::vector<std::string> trace;
    SubscriptionToken texts = box.text_changed().subscribe(TraceText{&trace});
    SubscriptionToken opens = box.auto_complete_open_changed().subscribe(TraceOpen{&trace});
    SubscriptionToken accepts = box.auto_complete_accepted().subscribe(TraceAccepted{&trace});
    SubscriptionToken cancels = box.cancelled().subscribe(TraceCancelled{&trace});
    SubscriptionToken commits = box.committed().subscribe(TraceCommitted{&trace});

    require(box.auto_complete_source_status().accepted(), "a custom source is always available");
    require(type(window, "c") && box.auto_complete_open(), "typing a matching prefix opens the list");
    const std::span<const std::string> shown = box.auto_complete_suggestions();
    require(shown.size() == 5U && shown[0] == "Calgary" && shown[1] == "California" &&
                shown[2] == "Cambridge" && shown[3] == "calgary" && shown[4] == "Caen",
            "the list shows matches in source order, each text once");
    require(box.text.read() == "c" && !box.auto_complete_highlighted(), "suggest mode does not change the text");
    require(window.find("ac.suggest.auto-complete.list") != nullptr && window.focused_control() == fixture.box,
            "the list is an overlay and the box keeps the focus");
    window.perform_layout();
    const Rect list_bounds = (*window.find("ac.suggest.auto-complete.list")).absolute_bounds();
    require(list_bounds.y >= 38.0 && list_bounds.width == 200.0,
            "the list opens under the box, as wide as it");
    const SemanticDescriptor open_semantics = box.semantic_descriptor();
    require(open_semantics.has_popup && has_semantic_state(open_semantics.states, SemanticState::expanded),
            "a suggesting box publishes that it opens a list, and expanded while it is open");

    require(type(window, "a") && box.auto_complete_suggestions().size() == 5U, "each edit matches again");
    require(type(window, "m") && box.auto_complete_suggestions().size() == 1U &&
                box.auto_complete_suggestions()[0] == "Cambridge",
            "the list narrows as the user types");
    require(press(window, PhysicalKey::backspace) && box.text.read() == "ca" &&
                box.auto_complete_suggestions().size() == 5U,
            "deleting widens the list again");

    trace.clear();
    require(press(window, PhysicalKey::down) && box.auto_complete_highlighted() == 0U &&
                box.text.read() == "Calgary" && box.selection().caret == Utf8Offset(7U),
            "Down highlights the first suggestion and shows it in the box");
    require(press(window, PhysicalKey::down) && box.auto_complete_highlighted() == 1U && box.text.read() == "California",
            "Down moves to the next suggestion");
    require(press(window, PhysicalKey::up) && press(window, PhysicalKey::up) &&
                !box.auto_complete_highlighted() && box.text.read() == "ca",
            "Up from the first suggestion returns to the typed text");
    require(press(window, PhysicalKey::up) && box.auto_complete_highlighted() == 4U && box.text.read() == "Caen",
            "Up from the typed text highlights the last suggestion");
    require(press(window, PhysicalKey::down) && !box.auto_complete_highlighted() && box.text.read() == "ca",
            "Down from the last suggestion returns to the typed text");
    require(trace.size() == 6U && trace[0] == "text Calgary" && trace[5] == "text ca",
            "each highlight change shows its text once");

    trace.clear();
    require(press(window, PhysicalKey::down) && press(window, PhysicalKey::down) &&
                press(window, PhysicalKey::enter),
            "Enter is consumed while a suggestion is highlighted");
    require(box.text.read() == "California" && !box.auto_complete_open() &&
                box.selection() == TextSelection{Utf8Offset(10U), Utf8Offset(10U)},
            "Enter accepts the highlighted suggestion and closes the list");
    require(trace.size() == 4U && trace[0] == "text Calgary" && trace[1] == "text California" &&
                trace[2] == "close" && trace[3] == "accepted California",
            "accepting shows the text, then closes, then reports the accepted text; Enter commits nothing");
    require(!window.find("ac.suggest.auto-complete.list") && !has_semantic_state(box.semantic_descriptor().states,
                                                                                   SemanticState::expanded),
            "the closed list leaves the window and the box is no longer expanded");

    box.text = "";
    trace.clear();
    require(type(window, "c") && type(window, "a") && press(window, PhysicalKey::down) &&
                press(window, PhysicalKey::escape),
            "Escape is consumed while the list is open");
    require(box.text.read() == "ca" && !box.auto_complete_open() && box.selection().caret == Utf8Offset(2U),
            "Escape closes the list and restores the typed text");
    require(trace.size() == 6U && trace[4] == "text ca" && trace[5] == "close",
            "Escape restores the text before the list closes, and raises no cancelled");
    trace.clear();
    require(!press(window, PhysicalKey::escape) && trace.empty(),
            "with nothing pending, Escape goes on to the window and raises no cancelled");
    box.accepts_escape = true;
    require(press(window, PhysicalKey::escape) && trace.size() == 1U && trace[0] == "cancelled",
            "with nothing pending, a box that accepts Escape raises cancelled");
    box.accepts_escape = false;

    require(type(window, "l") && box.auto_complete_open(), "the list opens again");
    trace.clear();
    require(!press(window, PhysicalKey::enter) && !box.auto_complete_open() &&
                trace.size() == 1U && trace[0] == "close",
            "Enter with nothing highlighted closes the list and goes on to the window");
    box.text = "";
    require(type(window, "c") && box.auto_complete_open(), "the list opens once more");
    box.accepts_enter = true;
    trace.clear();
    require(press(window, PhysicalKey::enter) && !box.auto_complete_open() &&
                trace.size() == 2U && trace[0] == "close" && trace[1] == "committed c",
            "in a box that accepts Enter, it closes the list and commits as usual");
    box.accepts_enter = false;

    require(type(window, "x") && !box.auto_complete_open(), "text that matches nothing shows no list");
}

void append_modes() {
    Fixture fixture = make_fixture("ac.append", AutoCompleteMode::append);
    Window& window = *fixture.window;
    TextBox& box = *fixture.box;
    std::vector<std::string> trace;
    SubscriptionToken opens = box.auto_complete_open_changed().subscribe(TraceOpen{&trace});
    SubscriptionToken accepts = box.auto_complete_accepted().subscribe(TraceAccepted{&trace});

    require(type(window, "C") && type(window, "A") && box.text.read() == "CAlgary",
            "append keeps the typed case and appends the rest of the first match");
    require(box.selection() == TextSelection{Utf8Offset(2U), Utf8Offset(7U)},
            "the appended part is selected, so typing replaces it");
    require(!box.auto_complete_open() && trace.empty(), "append mode never opens a list");
    require(type(window, "m") && box.text.read() == "CAmbridge", "the next character completes again");
    require(press(window, PhysicalKey::backspace) && box.text.read() == "CAm",
            "Backspace removes the appended part");
    require(press(window, PhysicalKey::backspace) && box.text.read() == "CA",
            "deleting never appends");
    require(type(window, "l") && box.text.read() == "CAlgary", "typing appends again");
    require(press(window, PhysicalKey::escape) && box.text.read() == "CAl" &&
                box.selection().caret == Utf8Offset(3U),
            "Escape removes the appended part");
    require(type(window, "i") && box.text.read() == "CAlifornia", "the completion follows the typed text");
    require(window.focused_control() == fixture.box && press(window, PhysicalKey::tab),
            "Tab is consumed while a completion is appended");
    require(box.text.read() == "CAlifornia" && box.selection() == TextSelection{Utf8Offset(10U), Utf8Offset(10U)} &&
                window.focused_control() == fixture.box && trace.size() == 1U &&
                trace[0] == "accepted CAlifornia",
            "Tab accepts the appended completion, keeps the focus and reports the text");
    require(press(window, PhysicalKey::tab) && window.focused_control() == fixture.other,
            "with nothing pending, Tab moves the focus as usual");

    require(window.request_focus(fixture.box), "the box takes the focus back");
    box.text = "";
    require(type(window, "d") && box.text.read() == "dover", "the completion keeps the typed case");
    require(box.undo() && box.text.read().empty(), "undo removes the typed character and its completion together");
    require(type(window, "c") && type(window, "a") && press(window, PhysicalKey::right) &&
                box.text.read() == "calgary" && box.selection().caret == Utf8Offset(7U),
            "moving the caret keeps the completion as text");
    require(!press(window, PhysicalKey::escape) || box.text.read() == "calgary",
            "after the caret moved, Escape leaves the text alone");

    box.text = "";
    box.maximum_length = 4U;
    require(type(window, "c") && box.text.read() == "c", "a completion past the maximum length is not appended");
    box.maximum_length = 0U;

    box.text = "";
    box.auto_complete_case_sensitive = true;
    require(type(window, "c") && box.text.read() == "calgary",
            "case-sensitive matching skips candidates that differ in case");
    box.auto_complete_case_sensitive = false;

    // Typing in the middle of the text never appends.
    box.text = "ca";
    box.select(Utf8Offset(1U), Utf8Offset(1U));
    require(type(window, "a") && box.text.read() == "caa", "an insertion before the end appends nothing");
}

void suggest_append_mode() {
    Fixture fixture = make_fixture("ac.both", AutoCompleteMode::suggest_append);
    Window& window = *fixture.window;
    TextBox& box = *fixture.box;
    require(type(window, "c") && box.text.read() == "calgary" && box.auto_complete_open() &&
                !box.auto_complete_highlighted(),
            "suggest-append appends the first match and opens the list");
    require(press(window, PhysicalKey::down) && box.text.read() == "Calgary" && box.selection().empty(),
            "Down replaces the appended completion with the highlighted suggestion");
    require(press(window, PhysicalKey::up) && box.text.read() == "c",
            "returning to the typed text drops the completion");
    require(type(window, "a") && box.text.read() == "calgary" && press(window, PhysicalKey::escape) &&
                box.text.read() == "ca" && !box.auto_complete_open(),
            "Escape removes the appended part and closes the list");
    require(type(window, "m") && box.text.read() == "cambridge" && press(window, PhysicalKey::enter) &&
                box.text.read() == "cambridge" && !box.auto_complete_open() && box.selection().empty(),
            "Enter accepts the appended completion");
}

void composition_is_never_completed() {
    Fixture fixture = make_fixture("ac.ime", AutoCompleteMode::suggest_append);
    Window& window = *fixture.window;
    TextBox& box = *fixture.box;
    TextInputEvent composing{};
    composing.text_utf8 = "c";
    composing.composing = true;
    require(window.dispatch_text(composing) && box.text.read() == "c" && !box.auto_complete_open(),
            "composition text is neither completed nor suggested for");
    TextInputEvent replaced{};
    replaced.text_utf8 = "ca";
    replaced.composing = true;
    replaced.replacement_start = 0;
    replaced.replacement_length = 1;
    require(window.dispatch_text(replaced) && box.text.read() == "ca" && !box.auto_complete_open(),
            "an updated composition is left as the input method wrote it");
    TextInputEvent committed{};
    committed.text_utf8 = "ca";
    require(window.dispatch_text(committed) && box.text.read().rfind("ca", 0) == 0 &&
                box.text.read().find("caca") == std::string::npos,
            "the commit replaces the marked text (N106)");
    require(type(window, "l") && box.text.read() == "calgary" && box.auto_complete_open(),
            "committed input completes as usual");
    TextInputEvent more{};
    more.text_utf8 = "i";
    more.composing = true;
    require(window.dispatch_text(more) && box.text.read() == "cali" && !box.auto_complete_open(),
            "composition closes an open list and replaces the appended part without completing");
}

void pointer_and_dismissal() {
    Fixture fixture = make_fixture("ac.pointer", AutoCompleteMode::suggest);
    Window& window = *fixture.window;
    TextBox& box = *fixture.box;
    std::vector<std::string> trace;
    SubscriptionToken opens = box.auto_complete_open_changed().subscribe(TraceOpen{&trace});
    SubscriptionToken accepts = box.auto_complete_accepted().subscribe(TraceAccepted{&trace});
    require(type(window, "c"), "typed");
    window.perform_layout();
    const Rect list = (*window.find("ac.pointer.auto-complete.list")).absolute_bounds();
    const Point third{list.x + 20.0, list.y + 2.0 + 2.0 * 26.0 + 13.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, third}) &&
                box.auto_complete_highlighted() == 2U && box.text.read() == "Cambridge" &&
                window.focused_control() == fixture.box,
            "pressing a row highlights it without taking the focus");
    require(window.dispatch_pointer({PointerAction::up, PointerButton::primary, third}) &&
                !box.auto_complete_open() && box.text.read() == "Cambridge" &&
                trace.size() == 3U && trace[0] == "open" && trace[1] == "close" &&
                trace[2] == "accepted Cambridge",
            "releasing on the row accepts it");

    box.text = "";
    require(type(window, "c") && box.auto_complete_open(), "the list opens again");
    window.perform_layout();
    const Rect field = box.absolute_bounds();
    const Point inside{field.x + 50.0, field.y + 10.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, inside}) &&
                !box.auto_complete_open() && box.text.read() == "c",
            "a press in the box itself reaches it, places the caret and closes the list");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, inside}));
    require(type(window, "a") && box.auto_complete_open(), "the list opens again");
    const Point away{300.0, 150.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, away}) &&
                !box.auto_complete_open() && box.text.read() == "ca",
            "a press outside the list closes it and keeps the text");

    require(type(window, "l") && box.auto_complete_open(), "the list opens again");
    require(window.request_focus(fixture.other) && !box.auto_complete_open() && box.text.read() == "cal",
            "losing the focus closes the list and keeps the text");
    require(window.request_focus(fixture.box) && type(window, "g") && box.auto_complete_open(),
            "the list opens again");
    box.visible = false;
    require(!box.auto_complete_open() && !window.find("ac.pointer.auto-complete"),
            "hiding the box revokes its list");
}

void sources_and_refusals() {
    Fixture fixture = make_fixture("ac.sources", AutoCompleteMode::suggest_append);
    Window& window = *fixture.window;
    TextBox& box = *fixture.box;

    bool threw = false;
    try {
        box.auto_complete_source = AutoCompleteSource::list_items;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && box.auto_complete_source.read() == AutoCompleteSource::custom_source,
            "a plain TextBox refuses the list_items source");
    threw = false;
    try {
        box.auto_complete_custom_source = nullptr;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && box.auto_complete_custom_source.read() != nullptr, "a null custom source is refused");

    // Shared collections: one list, two boxes, changes seen by both.
    std::shared_ptr<AutoCompleteStringCollection> shared = std::make_shared<AutoCompleteStringCollection>();
    (*shared).add("Lisbon");
    box.auto_complete_custom_source = shared;
    (*fixture.other).auto_complete_custom_source = shared;
    require((*fixture.other).auto_complete_custom_source.read() == box.auto_complete_custom_source.read(),
            "one collection may serve several boxes");
    box.auto_complete_mode = AutoCompleteMode::suggest;
    require(type(window, "l") && box.auto_complete_suggestions().size() == 1U, "the shared list is matched");
    (*shared).add("Leeds");
    require(box.auto_complete_suggestions().size() == 2U && box.auto_complete_suggestions()[1] == "Leeds",
            "a change to the collection refreshes an open list");
    (*shared).clear();
    require(!box.auto_complete_open(), "a list with nothing left to show closes");

    // Host sources report unsupported and offer nothing.
    box.text = "";
    box.auto_complete_source = AutoCompleteSource::file_system;
    require(box.auto_complete_source_status().error == HostServiceError::unsupported,
            "without a host, a host source is unsupported");
    host::HeadlessHost host(window);
    require(box.auto_complete_source_status().error == HostServiceError::unsupported,
            "no host supplies host sources yet");
    HostAutoCompleteRequest request{};
    request.source = HostAutoCompleteSource::all_url;
    request.prefix_utf8 = "http";
    const HostAutoCompleteResult answer = host.services().query_auto_complete(request);
    require(answer.status.error == HostServiceError::unsupported && answer.candidates_utf8.empty(),
            "the host service reports unsupported with no candidates");
    require(type(window, "l") && !box.auto_complete_open() && box.text.read() == "l",
            "a host source offers no completion");
    box.auto_complete_source = AutoCompleteSource::none;
    require(box.auto_complete_source_status().accepted(), "no source is not a failure");
}

// A host whose completion source answers with whatever the test gives it, to
// pin how HostServices::query_auto_complete checks an adapter's answer.
class CandidateServices final : public HostServices {
public:
    CandidateServices() : HostServices({HostCapabilities::current_protocol_version, "candidates", HostCapability::lifecycle}) {}
    ~CandidateServices() override { shutdown(); }
    std::vector<std::string> answer{};

protected:
    HostMonitorResult query_monitors_impl() override { return {{HostServiceError::unsupported}, {}}; }
    HostServiceStatus set_cursor_impl(CursorKind) override { return {HostServiceError::unsupported}; }
    HostServiceStatus set_pointer_capture_impl(bool, std::uint64_t) override { return {HostServiceError::unsupported}; }
    HostClipboardTextResult read_clipboard_text_impl() override { return {}; }
    HostServiceStatus write_clipboard_text_impl(std::string_view) override { return {HostServiceError::unsupported}; }
    HostDialogResult show_dialog_impl(const HostDialogRequest&) override { return {}; }
    HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest&) override { return {HostServiceError::unsupported}; }
    HostAutoCompleteResult query_auto_complete_impl(const HostAutoCompleteRequest&) override {
        HostAutoCompleteResult result;
        result.candidates_utf8 = answer;
        return result;
    }
};

// Characterization (house-style review of the window layer): an adapter's
// accepted answer passes only with at most the request's maximum of
// non-empty UTF-8 strings without NUL; anything else is backend_failure
// with no candidates, and counts as a rejected request.
void host_candidate_checks() {
    CandidateServices services;
    HostAutoCompleteRequest request{};
    request.prefix_utf8 = "a";
    request.maximum_candidates = 2U;
    services.answer = {"alpha", "amber"};
    const HostAutoCompleteResult accepted = services.query_auto_complete(request);
    require(accepted.status.accepted() && accepted.candidates_utf8.size() == 2U &&
                accepted.candidates_utf8[1] == "amber",
            "valid host candidates are returned as given");
    const std::uint64_t rejected_before = services.snapshot().rejected_requests;
    services.answer = {"alpha", "amber", "apple"};
    const HostAutoCompleteResult too_many = services.query_auto_complete(request);
    require(too_many.status.error == HostServiceError::backend_failure && too_many.candidates_utf8.empty(),
            "more candidates than the maximum are a backend failure with none returned");
    services.answer = {"alpha", ""};
    const HostAutoCompleteResult empty = services.query_auto_complete(request);
    require(empty.status.error == HostServiceError::backend_failure && empty.candidates_utf8.empty(),
            "an empty candidate is a backend failure");
    services.answer = {std::string("al\0pha", 6U)};
    const HostAutoCompleteResult nul = services.query_auto_complete(request);
    require(nul.status.error == HostServiceError::backend_failure && nul.candidates_utf8.empty(),
            "a candidate with NUL is a backend failure");
    services.answer = {std::string("a\xFF", 2U)};
    const HostAutoCompleteResult invalid = services.query_auto_complete(request);
    require(invalid.status.error == HostServiceError::backend_failure && invalid.candidates_utf8.empty(),
            "a candidate that is not UTF-8 is a backend failure");
    require(services.snapshot().rejected_requests == rejected_before + 4U,
            "each refused answer counts as one rejected request");
    request.prefix_utf8 = std::string_view("\xFF", 1U);
    const HostAutoCompleteResult bad_prefix = services.query_auto_complete(request);
    require(bad_prefix.status.error == HostServiceError::invalid_utf8, "a prefix that is not UTF-8 is refused");
}

void inactive_boxes() {
    Fixture fixture = make_fixture("ac.inactive", AutoCompleteMode::suggest_append);
    Window& window = *fixture.window;
    TextBox& box = *fixture.box;
    require(type(window, "c") && box.auto_complete_open(), "an ordinary box completes");
    box.use_system_password_character = true;
    require(!box.auto_complete_open(), "masking the box closes its list");
    box.text = "";
    require(type(window, "c") && box.text.read() == "c" && !box.auto_complete_open() &&
                !box.semantic_descriptor().has_popup,
            "a masked box never completes");
    box.use_system_password_character = false;
    box.text = "";
    box.multiline = true;
    require(type(window, "c") && box.text.read() == "c" && !box.auto_complete_open(),
            "a multi-line box never completes");
    box.multiline = false;
    box.text = "";
    AcceptEveryEdit acceptor;
    box.set_edit_filter(Delegate<TextEdit&>::bind<AcceptEveryEdit, &AcceptEveryEdit::filter>(acceptor));
    require(type(window, "c") && box.text.read() == "c" && !box.auto_complete_open(),
            "a box with an edit filter leaves completion to the composite that filters it");
    box.set_edit_filter(Delegate<TextEdit&>{});
    box.text = "";
    box.auto_complete_mode = AutoCompleteMode::none;
    require(type(window, "c") && box.text.read() == "c" && !box.auto_complete_open() &&
                !box.semantic_descriptor().has_popup,
            "mode none completes nothing and publishes no overlay");
}

void paste_suggests_without_appending() {
    Fixture fixture = make_fixture("ac.paste", AutoCompleteMode::suggest_append);
    Window& window = *fixture.window;
    TextBox& box = *fixture.box;
    host::HeadlessHost host(window);
    require(host.services().write_clipboard_text("Cam").accepted(), "the clipboard holds text");
    require(press(window, PhysicalKey::v, Modifier::control) && box.text.read() == "Cam" &&
                box.auto_complete_open() && box.auto_complete_suggestions().size() == 1U,
            "a paste opens the list for the pasted text but appends nothing");
}


struct TraceIndex final {
    std::vector<std::string>* trace{};
    void operator()(std::optional<std::size_t> index) const {
        (*trace).push_back(index ? "index " + std::to_string(*index) : std::string("index none"));
    }
};
struct TraceDropDown final {
    std::vector<std::string>* trace{};
    void operator()(bool open) const { (*trace).push_back(open ? "drop" : "fold"); }
};

struct ComboFixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ComboBox> combo;
    std::shared_ptr<TextBox> other;
    std::unique_ptr<Window> window;
};

ComboFixture make_combo(const char* id) {
    ComboFixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.combo = make_control<ComboBox>(StableId(std::string(id)));
    (*fixture.combo).set_requested_bounds({10.0, 10.0, 220.0, 30.0});
    (*fixture.combo).items = {"Calgary", "California", "Cambridge", "Dover"};
    fixture.other = make_control<TextBox>(StableId(std::string(id) + ".other"));
    (*fixture.other).set_requested_bounds({10.0, 260.0, 200.0, 28.0});
    (*fixture.root).add_child(fixture.combo);
    (*fixture.root).add_child(fixture.other);
    fixture.window = std::make_unique<Window>(fixture.root, Size{320.0, 300.0});
    (*fixture.window).perform_layout();
    return fixture;
}

void combo_drop_down_style() {
    ComboFixture fixture = make_combo("combo.edit");
    Window& window = *fixture.window;
    ComboBox& combo = *fixture.combo;
    combo.selected_index = 1U;
    require(!combo.editor() && combo.text.read() == "California" && combo.focusable.read(),
            "a new combo box is a drop-down list with no editor");
    combo.drop_down_style = ComboBoxStyle::drop_down;
    const std::shared_ptr<TextBox> editor = combo.editor();
    require(editor && (*editor).text.read() == "California" && combo.selected_index.read() == 1U &&
                !combo.focusable.read() && window.find("combo.edit.editor") == editor,
            "the drop_down style adds an editor showing the selected item");
    window.perform_layout();
    const Rect field = (*editor).absolute_bounds();
    require(field.x >= 10.0 && field.x + field.width <= 10.0 + 220.0 - 24.0,
            "the editor fills the field left of the drop-down button");
    const SemanticDescriptor semantics = combo.semantic_descriptor();
    require(semantics.role == SemanticRole::combo_box && semantics.value == "California" &&
                !semantics.include_descendants,
            "the field publishes the editor's text as its value");

    std::vector<std::string> trace;
    SubscriptionToken texts = combo.text_changed().subscribe(TraceText{&trace});
    SubscriptionToken indices = combo.selected_index_changed().subscribe(TraceIndex{&trace});
    SubscriptionToken drops = combo.drop_down_changed().subscribe(TraceDropDown{&trace});
    require(window.request_focus(editor), "the editor takes the focus");
    (*editor).select_all();
    require(type(window, "d") && combo.text.read() == "d" && !combo.selected_index.read(),
            "typed text that equals no item clears the selection");
    require(type(window, "OVER") && combo.text.read() == "dOVER" && combo.selected_index.read() == 3U,
            "typed text equal to an item, ignoring ASCII case, selects it and stays as typed");
    require(trace.size() == 4U && trace[0] == "text d" && trace[1] == "index none" &&
                trace[2] == "text dOVER" && trace[3] == "index 3",
            "each edit raises text_changed, then any selection change");
    trace.clear();
    require(press(window, PhysicalKey::up) && combo.selected_index.read() == 2U && combo.text.read() == "Cambridge",
            "Up selects the previous item and shows it");
    require(trace.size() == 2U && trace[0] == "text Cambridge" && trace[1] == "index 2",
            "choosing an item shows its text, then raises the selection");
    require(press(window, PhysicalKey::down) && combo.text.read() == "Dover", "Down selects the next item");

    trace.clear();
    require(press(window, PhysicalKey::f4) && combo.dropped_down.read() && window.focus_scope_depth() == 0U &&
                window.focused_control() == editor,
            "F4 opens the list without taking the focus from the editor");
    require(!(*window.find("combo.edit.overlay.list")).focusable.read(), "the open list never takes the focus");
    require(press(window, PhysicalKey::up) && combo.selected_index.read() == 2U && combo.dropped_down.read() &&
                (*std::static_pointer_cast<ListBox>(window.find("combo.edit.overlay.list"))).selected_index() == 2U,
            "Up moves through the open list and the field shows each item");
    require(type(window, "s") && combo.text.read() == "Cambridges" && combo.dropped_down.read() && !combo.selected_index.read(),
            "typing goes on in the editor while the list is open");
    require(press(window, PhysicalKey::enter) && !combo.dropped_down.read() && combo.text.read() == "Cambridges",
            "Enter closes the list");
    require(press(window, PhysicalKey::down, Modifier::alt) && combo.dropped_down.read(), "Alt+Down opens the list");
    require(press(window, PhysicalKey::escape) && !combo.dropped_down.read(), "Escape closes the list");

    require(press(window, PhysicalKey::f4) && combo.dropped_down.read(), "the list opens again");
    window.perform_layout();
    const Rect list = (*window.find("combo.edit.overlay.list")).absolute_bounds();
    require(list.y >= 40.0 && list.width == 220.0, "the list opens under the field, as wide as it");
    const Point first{list.x + 20.0, list.y + 2.0 + 13.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, first}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, first}) &&
                !combo.dropped_down.read() && combo.selected_index.read() == 0U && combo.text.read() == "Calgary" &&
                window.focused_control() == editor,
            "clicking a row chooses it, closes the list and keeps the focus in the editor");
    const Point button{10.0 + 220.0 - 10.0, 25.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, button}) &&
                combo.dropped_down.read(),
            "a press on the button opens the list");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, button}));
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, button}) &&
                !combo.dropped_down.read(),
            "a press on the button of an open list reaches the field and closes it");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, button}));
    require(press(window, PhysicalKey::f4) && window.request_focus(fixture.other) && !combo.dropped_down.read(),
            "the list closes when the field loses the focus");

    combo.text = "CAMBRIDGE";
    require(combo.text.read() == "CAMBRIDGE" && combo.selected_index.read() == 2U,
            "set_text sets the editor's text and the selection follows it");
    require(combo.property_value("Text") == BindingValue{std::string("CAMBRIDGE")},
            "the Text property is the field's text");
    require(window.perform_semantic_action("combo.edit", SemanticAction::set_value, "Somewhere") &&
                combo.text.read() == "Somewhere" && !combo.selected_index.read(),
            "an assistive technology may set any text");
    combo.items = {"Somewhere", "Elsewhere"};
    require(combo.selected_index.read() == 0U, "new items are matched against the text");

    combo.drop_down_style = ComboBoxStyle::drop_down_list;
    require(!combo.editor() && !window.find("combo.edit.editor") && combo.focusable.read() &&
                combo.selected_index.read() == 0U && combo.text.read() == "Somewhere",
            "back in the drop_down_list style the editor is gone and the selection kept");
}

void combo_auto_complete() {
    ComboFixture fixture = make_combo("combo.ac");
    Window& window = *fixture.window;
    ComboBox& combo = *fixture.combo;
    combo.auto_complete_source = AutoCompleteSource::list_items;
    combo.auto_complete_mode = AutoCompleteMode::suggest_append;
    require(combo.auto_complete_source.read() == AutoCompleteSource::list_items &&
                combo.auto_complete_mode.read() == AutoCompleteMode::suggest_append,
            "the settings are kept in the drop_down_list style");
    combo.drop_down_style = ComboBoxStyle::drop_down;
    const std::shared_ptr<TextBox> editor = combo.editor();
    require((*editor).auto_complete_mode.read() == AutoCompleteMode::suggest_append,
            "the editor takes the combo box's auto-complete settings");
    require(window.request_focus(editor), "the editor takes the focus");
    std::vector<std::string> trace;
    SubscriptionToken accepts = (*editor).auto_complete_accepted().subscribe(TraceAccepted{&trace});
    require(type(window, "c") && combo.text.read() == "calgary" && (*editor).auto_complete_open() &&
                (*editor).auto_complete_suggestions().size() == 3U && !combo.dropped_down.read(),
            "list_items completes from the items: the first is appended and the matches suggested");
    require(combo.selected_index.read() == 0U, "the appended completion equals an item, so it is selected");
    window.perform_layout();
    const Rect list = (*window.find("combo.ac.editor.auto-complete.list")).absolute_bounds();
    require(list.width == 220.0 && list.y >= 40.0, "the suggestion list opens under the whole field");
    require(press(window, PhysicalKey::down) && press(window, PhysicalKey::down) &&
                combo.text.read() == "California" && combo.selected_index.read() == 1U && !combo.dropped_down.read(),
            "Down moves through the suggestions, not the combo box's own list");
    require(press(window, PhysicalKey::enter) && !(*editor).auto_complete_open() &&
                combo.text.read() == "California" && trace.size() == 1U,
            "Enter accepts the suggestion");
    require(press(window, PhysicalKey::down) && combo.text.read() == "Cambridge",
            "with the suggestions closed, Down chooses the next item");

    (*editor).select_all();
    require(type(window, "c") && (*editor).auto_complete_open() && press(window, PhysicalKey::f4) &&
                combo.dropped_down.read() && !(*editor).auto_complete_open(),
            "opening the combo box's list closes the suggestions");
    require(press(window, PhysicalKey::escape) && !combo.dropped_down.read(), "the list closes");

    combo.items.append("Canterbury");
    (*editor).select_all();
    require(type(window, "can") && combo.text.read() == "canterbury",
            "list_items follows the items");
    std::shared_ptr<AutoCompleteStringCollection> towns = std::make_shared<AutoCompleteStringCollection>();
    (*towns).add("Dartmouth");
    combo.auto_complete_custom_source = towns;
    combo.auto_complete_source = AutoCompleteSource::custom_source;
    require((*editor).auto_complete_custom_source.read() == towns, "custom_source hands the collection to the editor");
    (*editor).select_all();
    require(type(window, "d") && combo.text.read() == "dartmouth" && !combo.selected_index.read(),
            "custom_source completes from the collection, not the items");
    combo.auto_complete_source = AutoCompleteSource::history_list;
    (*editor).select_all();
    require(type(window, "d") && combo.text.read() == "d" && !(*editor).auto_complete_open(),
            "a host source offers nothing");
    bool threw = false;
    try {
        combo.auto_complete_custom_source = nullptr;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a null custom source is refused");
}
} // namespace

int main() {
    collection_contract();
    text_matching();
    suggest_keyboard_trace();
    append_modes();
    suggest_append_mode();
    composition_is_never_completed();
    pointer_and_dismissal();
    sources_and_refusals();
    host_candidate_checks();
    inactive_boxes();
    paste_suggests_without_appending();
    combo_drop_down_style();
    combo_auto_complete();
    std::cout << "auto-complete tests passed\n";
    return 0;
}
