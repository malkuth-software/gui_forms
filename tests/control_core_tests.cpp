// Checkpoint 28 workstream B (control core API and lifecycle): the event
// state is made on first subscription (A4) and compacted only after a
// disconnect (A5); disposal lets every event's subscribers go and a handler
// may dispose its own control mid-dispatch (A6); a failed reparent leaves
// the child where it was (A9); the z-order index is named from the top and
// a move to the same place changes nothing (A11, A12); the visual context
// takes a record (A13); an undeclared mutation behaves alike in every build
// (A15); a const read of the bindings makes nothing (A16); removed style
// bits are unknown (A17); accessibility retires the semantic snapshot
// (A18); mnemonics decode strictly (A19); and a keyed publication outside
// initialisation runs at once (S6).

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/text.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "control core failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

std::shared_ptr<Panel> make_panel(const std::string& id, Rect bounds) {
    std::shared_ptr<Panel> panel = make_control<Panel>(StableId(id));
    (*panel).set_requested_bounds(bounds);
    return panel;
}

struct CountCalls final {
    int* count{};
    void operator()() const { ++*count; }
};

struct CountNames final {
    int* count{};
    void operator()(const std::string&) const { ++*count; }
};

// A4/A5: an event with no subscriber reports no statistics and emits
// nothing; subscriptions and disconnects are counted, and a disconnected
// slot is not called again.
void test_event_state() {
    Event<> event;
    require(event.statistics().subscriptions_connected == 0U, "an unsubscribed event has no statistics");
    event.emit();
    event.disconnect_all();
    int calls = 0;
    SubscriptionToken first = event.subscribe(CountCalls{&calls});
    SubscriptionToken second = event.subscribe(CountCalls{&calls});
    event.emit();
    require(calls == 2 && event.statistics().subscriptions_connected == 2U, "both subscribers are called");
    first.disconnect();
    event.emit();
    require(calls == 3 && event.statistics().subscriptions_disconnected == 1U,
            "a disconnected subscriber is not called and is counted");
    for (int index = 0; index < 100; ++index) {
        SubscriptionToken extra = event.subscribe(CountCalls{&calls});
        static_cast<void>(extra);
    }
    calls = 0;
    event.emit();
    require(calls == 1, "subscriptions whose tokens died are not called");
    static_cast<void>(second);
}

// The control whose pointer handler disposes it (A6).
class DisposingControl final : public Control {
public:
    explicit DisposingControl(StableId id) : Control(std::move(id)) {}
    void on_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::down) dispose();
    }
};

struct CountPointer final {
    int* count{};
    void operator()(const PointerEvent&) const { ++*count; }
};

// A6: disposal disconnects every event, after the notifications disposal
// itself raises; a handler that disposes its control mid-dispatch ends the
// dispatch to it.
void test_dispose_disconnects() {
    std::shared_ptr<Panel> control = make_panel("dispose.control", {0.0, 0.0, 10.0, 10.0});
    int names = 0;
    SubscriptionToken token = (*control).name_changed().subscribe(CountNames{&names});
    (*control).name = "first";
    require(names == 1, "a live control raises name_changed");
    require((*control).name_changed().statistics().subscriptions_disconnected == 0U, "still connected");
    (*control).dispose();
    require((*control).name_changed().statistics().subscriptions_disconnected == 1U,
            "disposal disconnects name_changed");

    std::shared_ptr<Panel> root = make_panel("dispose.root", {0.0, 0.0, 100.0, 100.0});
    std::shared_ptr<DisposingControl> target = make_control<DisposingControl>(StableId("dispose.target"));
    (*target).set_requested_bounds({10.0, 10.0, 40.0, 40.0});
    (*root).add_child(target);
    Window window(root, Size{100.0, 100.0});
    window.perform_layout();
    int observed = 0;
    SubscriptionToken pointer = (*target).pointer_observed().subscribe(CountPointer{&observed});
    const Point inside{20.0, 20.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, inside}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, inside}));
    require(!(*target).is_alive(), "the handler disposed its control");
    require(observed == 0, "a control disposed by its handler publishes no observation");
    require((*root).children().empty(), "the disposed control left its parent");
}

// A9: a reparent whose stable identity clashes leaves the child in place.
void test_failed_reparent_keeps_the_child() {
    std::shared_ptr<Panel> root = make_panel("reparent.root", {0.0, 0.0, 100.0, 100.0});
    std::shared_ptr<Panel> holder = make_panel("reparent.holder", {0.0, 0.0, 50.0, 50.0});
    std::shared_ptr<Panel> child = make_panel("reparent.child", {0.0, 0.0, 10.0, 10.0});
    std::shared_ptr<Panel> sibling = make_panel("reparent.sibling", {0.0, 0.0, 10.0, 10.0});
    (*holder).add_child(sibling);
    (*holder).add_child(child);
    // A detached parent holding a control with the window root's identity.
    std::shared_ptr<Panel> detached_parent = make_panel("reparent.detached", {0.0, 0.0, 10.0, 10.0});
    std::shared_ptr<Panel> clash = make_panel("reparent.root", {0.0, 0.0, 5.0, 5.0});
    (*detached_parent).add_child(clash);
    (*root).add_child(holder);
    Window window(root, Size{100.0, 100.0});
    bool rejected = false;
    try {
        (*holder).add_child(clash);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected, "a clashing stable identity is rejected");
    require((*clash).parent() == detached_parent && (*detached_parent).children().size() == 1U,
            "the rejected child stays with its previous parent");
    // A move within the window is no clash.
    (*root).add_child(child);
    require((*child).parent() == root && window.find("reparent.child") == child,
            "a move within the window keeps the identity registered");
}

// A11/A12: the index from the top; a move to the same place does nothing.
void test_child_index_from_top() {
    std::shared_ptr<Panel> root = make_panel("order.root", {0.0, 0.0, 100.0, 100.0});
    std::shared_ptr<Panel> back = make_panel("order.back", {0.0, 0.0, 10.0, 10.0});
    std::shared_ptr<Panel> front = make_panel("order.front", {0.0, 0.0, 10.0, 10.0});
    (*root).add_child(back);
    (*root).add_child(front);
    require((*root).child_index_from_top((*front).runtime_id()) == std::optional<std::size_t>{0U} &&
                (*root).children().back() == front,
            "the last added child is index 0 from the top and last in painter order");
    Window window(root, Size{100.0, 100.0});
    window.perform_layout();
    const std::uint64_t revision = (*root).children_revision();
    require((*root).set_child_index_from_top((*front).runtime_id(), 0U) && (*root).children_revision() == revision,
            "moving a child to where it is changes nothing");
    require((*root).set_child_index_from_top((*front).runtime_id(), 5U) && (*root).children().front() == front,
            "an index past the last puts the child at the bottom");
    require(!(*root).set_child_index_from_top(RuntimeId{}, 0U), "a child the control does not hold is refused");
}

// A13: the visual context from a record.
void test_visual_context_record() {
    std::shared_ptr<Panel> panel = make_panel("visual.panel", {0.0, 0.0, 10.0, 10.0});
    const ControlVisualContext plain = (*panel).visual_context();
    require(plain.surface == ControlSurfaceState::normal && !plain.selected, "the default record is plain");
    const ControlVisualContext pressed = (*panel).visual_context({.pressed = true, .selected = true});
    require(pressed.surface == ControlSurfaceState::pressed && pressed.selected && !pressed.focused,
            "named fields reach the context");
}

// A15: an undeclared mutation is counted and invalidates conservatively in
// every build; A16: a const read of the bindings makes none.
void test_undeclared_and_bindings() {
    std::shared_ptr<Panel> root = make_panel("undeclared.root", {0.0, 0.0, 100.0, 100.0});
    Window window(root, Size{100.0, 100.0});
    window.perform_layout();
    const std::uint64_t before = window.metrics_snapshot().undeclared_mutations;
    (*root).invalidate_declared(Dirty::none);
    require(window.metrics_snapshot().undeclared_mutations == before + 1U, "the undeclared mutation is counted");
    require(has_dirty((*root).dirty(), Dirty::measure), "and invalidates conservatively, without throwing");
    const Control& reader = *root;
    require(reader.data_bindings().empty(), "a control without bindings reads an empty collection");
    require(reader.data_bindings().empty(), "reading again still makes nothing");
}

// A18: an accessibility-only mark retires the semantic snapshot.
void test_accessibility_dirt_is_read() {
    std::shared_ptr<Panel> root = make_panel("a11y.root", {0.0, 0.0, 100.0, 100.0});
    Window window(root, Size{100.0, 100.0});
    window.perform_layout();
    const std::uint64_t generation = window.semantic_generation();
    (*root).invalidate(Dirty::accessibility);
    require(window.semantic_generation() != generation, "Dirty::accessibility advances the semantic generation");
}

// A19: the strict decoder; a mnemonic on a malformed sequence is U+FFFD.
void test_strict_mnemonics() {
    const Utf8ScalarDecode good = decode_utf8_scalar("\xC3\xA9", Utf8Offset(0U));
    require(good.valid && good.scalar == U'é' && good.length == 2U, "a two-byte scalar decodes");
    const Utf8ScalarDecode overlong = decode_utf8_scalar("\xC0\xAF", Utf8Offset(0U));
    require(!overlong.valid && overlong.length == 1U && overlong.scalar == U'�', "an overlong form is refused");
    const Utf8ScalarDecode surrogate = decode_utf8_scalar("\xED\xA0\x80", Utf8Offset(0U));
    require(!surrogate.valid && surrogate.length == 1U, "a surrogate is refused");
    const Utf8ScalarDecode truncated = decode_utf8_scalar("\xE2\x82", Utf8Offset(0U));
    require(!truncated.valid && truncated.length == 1U, "a truncated sequence is refused");
    const MnemonicText parsed = parse_mnemonic_text("&\xC3\xA9t\xC3\xA9");
    require(parsed.mnemonic == std::optional<char32_t>{U'é'} && parsed.display_text == "\xC3\xA9t\xC3\xA9",
            "a multi-byte mnemonic is its scalar");
    const MnemonicText malformed = parse_mnemonic_text("&\xC0\xAF");
    require(malformed.mnemonic == std::optional<char32_t>{U'�'} && malformed.display_text == "\xC0\xAF",
            "a malformed mnemonic is U+FFFD and its bytes are kept");
}

// S6: a keyed publication outside initialisation reaches subscribers at
// once; inside, once at end_init.
void test_publication_timing() {
    std::shared_ptr<Panel> control = make_panel("publish.control", {0.0, 0.0, 10.0, 10.0});
    int names = 0;
    SubscriptionToken token = (*control).name_changed().subscribe(CountNames{&names});
    (*control).name = "one";
    require(names == 1, "outside initialisation a change is published at once");
    (*control).begin_init();
    (*control).name = "two";
    (*control).name = "three";
    require(names == 1, "inside initialisation nothing is published yet");
    (*control).end_init();
    require(names == 2, "end_init publishes the latest change once");
}

} // namespace

int main() {
    test_event_state();
    test_dispose_disconnects();
    test_failed_reparent_keeps_the_child();
    test_child_index_from_top();
    test_visual_context_record();
    test_undeclared_and_bindings();
    test_accessibility_dirt_is_read();
    test_strict_mnemonics();
    test_publication_timing();
    std::cout << "control core tests passed\n";
    return EXIT_SUCCESS;
}
