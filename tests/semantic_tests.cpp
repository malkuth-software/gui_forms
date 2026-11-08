#include "gui_forms/basic_controls.hpp"
#include "gui_forms/collection_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/input_controls.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/window.hpp"
#include "support/named_callbacks.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const SemanticNode* find_node(const std::vector<SemanticNode>& nodes,
                              std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        if (const SemanticNode* found = find_node(node.children, stable_id)) return found;
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

bool has_action(const SemanticNode& node, SemanticAction action) {
    return std::find(node.actions.begin(), node.actions.end(), action) !=
           node.actions.end();
}

class ExerciseSemanticsOffThread final {
public:
    ExerciseSemanticsOffThread(Window& window, bool& snapshot_rejected,
                               bool& action_rejected)
        : window_(window), snapshot_rejected_(snapshot_rejected),
          action_rejected_(action_rejected) {}

    void operator()() const {
        try {
            static_cast<void>(window_.semantic_snapshot());
        } catch (const std::logic_error&) {
            snapshot_rejected_ = true;
        }
        try {
            static_cast<void>(window_.perform_semantic_action(
                "thread.button", SemanticAction::press));
        } catch (const std::logic_error&) {
            action_rejected_ = true;
        }
    }

private:
    Window& window_;
    bool& snapshot_rejected_;
    bool& action_rejected_;
};

void test_snapshot_roles_states_hierarchy_and_json() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("semantics.root"));
    std::shared_ptr<gui_forms::GroupBox> group = make_control<GroupBox>(StableId("semantics.group"), "Receiver");
    (*group).set_requested_bounds({10.0, 10.0, 380.0, 220.0});
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("semantics.label"), "Frequency");
    (*label).set_requested_bounds({12.0, 24.0, 100.0, 24.0});
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("semantics.field"), "101.500");
    (*field).accessible_name = "Frequency in MHz";
    (*field).set_requested_bounds({112.0, 24.0, 150.0, 28.0});
    std::shared_ptr<gui_forms::CheckBox> check = make_control<CheckBox>(StableId("semantics.check"), "Noise reduction");
    (*check).checked = true;
    (*check).set_requested_bounds({12.0, 62.0, 180.0, 28.0});
    std::shared_ptr<gui_forms::ProgressBar> progress = make_control<ProgressBar>(StableId("semantics.progress"));
    (*progress).accessible_name = "Signal buffer";
    (*progress).value = 42.0;
    (*progress).set_requested_bounds({12.0, 104.0, 220.0, 20.0});
    std::shared_ptr<gui_forms::Button> hidden = make_control<Button>(StableId("semantics.hidden"), "Hidden");
    (*hidden).visible = false;
    (*group).add_child(label);
    (*group).add_child(field);
    (*group).add_child(check);
    (*group).add_child(progress);
    (*group).add_child(hidden);
    (*root).add_child(group);

    Window window(root, {420.0, 260.0});
    require(window.request_focus(field), "semantic fixture must retain field focus");
    const SemanticSnapshot first = window.semantic_snapshot();
    const SemanticSnapshot second = window.semantic_snapshot();
    require(first.roots.size() == 1U && first.node_count == 5U,
            "snapshot must expose the named group and four eligible stock controls");
    require(first.to_json() == second.to_json(),
            "unchanged semantic snapshots must serialize deterministically");

    const SemanticNode* group_node = find_node(first.roots, "semantics.group");
    const SemanticNode* field_node = find_node(first.roots, "semantics.field");
    const SemanticNode* check_node = find_node(first.roots, "semantics.check");
    const SemanticNode* progress_node = find_node(first.roots, "semantics.progress");
    require(group_node && (*group_node).role == SemanticRole::group &&
                (*group_node).children.size() == 4U,
            "semantic hierarchy must preserve exposed retained parents");
    require(field_node && (*field_node).role == SemanticRole::text_box &&
                (*field_node).name == "Frequency in MHz" &&
                (*field_node).value == "101.500" &&
                has_semantic_state((*field_node).states, SemanticState::focused) &&
                has_action(*field_node, SemanticAction::set_value),
            "TextBox semantics must publish identity, value, focus and edit action");
    require(check_node && (*check_node).role == SemanticRole::check_box &&
                has_semantic_state((*check_node).states, SemanticState::checked),
            "CheckBox semantics must publish checked state");
    require(progress_node && (*progress_node).role == SemanticRole::progress_bar &&
                (*progress_node).value == "42" &&
                (*progress_node).bounds == Rect{22.0, 114.0, 220.0, 20.0},
            "ProgressBar semantics must publish value and absolute retained bounds");
    require(!find_node(first.roots, "semantics.hidden"),
            "ineligible hidden controls must not leak into semantic snapshots");
}

void test_subtree_visibility_advances_semantic_generation() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("generation.root"));
    const std::shared_ptr<Panel> group = make_control<Panel>(StableId("generation.group"));
    const std::shared_ptr<Button> button = make_control<Button>(StableId("generation.button"), "Action");
    (*group).set_requested_bounds({0, 0, 200, 100});
    (*button).set_requested_bounds({10, 10, 100, 30});
    (*group).add_child(button); (*root).add_child(group);
    Window window(root, {300, 200});
    const SemanticSnapshot visible = window.semantic_snapshot();
    (*group).visible = false;
    const SemanticSnapshot hidden = window.semantic_snapshot();
    require(hidden.generation > visible.generation && hidden.node_count == 0U,
            "hiding an unfocused subtree must invalidate cached semantic trees");
    (*group).visible = true;
    const SemanticSnapshot restored = window.semantic_snapshot();
    require(restored.generation > hidden.generation && restored.node_count == visible.node_count,
            "restoring a subtree must advance semantic generation independently of layout");
}

void test_semantic_actions_use_control_behavior() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("actions.root"));
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("actions.button"), "Apply");
    std::shared_ptr<gui_forms::CheckBox> check = make_control<CheckBox>(StableId("actions.check"), "Enabled");
    std::shared_ptr<gui_forms::TrackBar> slider = make_control<TrackBar>(StableId("actions.slider"));
    (*slider).small_change = 2.5;
    (*slider).value = 25.0;
    std::shared_ptr<gui_forms::TextBox> field = make_control<TextBox>(StableId("actions.field"), "old");
    std::shared_ptr<gui_forms::ComboBox> combo = make_control<ComboBox>(StableId("actions.combo"));
    (*combo).items = {"Local", "Remote"};
    (*root).add_child(button);
    (*root).add_child(check);
    (*root).add_child(slider);
    (*root).add_child(field);
    (*root).add_child(combo);
    Window window(root, {500.0, 300.0});

    unsigned presses{};
    SubscriptionToken pressed = (*button).clicked().subscribe(
        test_support::IncrementCounter<unsigned, ButtonBase&>(presses));
    require(window.perform_semantic_action("actions.button", SemanticAction::press) &&
                presses == 1U,
            "semantic press must execute the Button activation contract once");
    require(window.perform_semantic_action("actions.check", SemanticAction::press) &&
                (*check).checked.read(),
            "semantic press must use CheckBox auto-check behavior");
    require(window.perform_semantic_action("actions.slider", SemanticAction::increment) &&
                (*slider).value.read() == 27.5,
            "semantic slider increment must use the public small change");
    require(window.perform_semantic_action("actions.slider", SemanticAction::set_value,
                                            "63.5") && (*slider).value.read() == 63.5,
            "semantic slider set-value must use bounded range behavior");
    require(window.perform_semantic_action("actions.field", SemanticAction::set_value,
                                            "new value") && (*field).text.read() == "new value",
            "semantic text set-value must use the TextBox mutation contract");
    (*field).read_only = true;
    require(!window.perform_semantic_action("actions.field", SemanticAction::set_value,
                                             "blocked") && (*field).text.read() == "new value",
            "semantic actions must preserve TextBox read-only state");
    require(window.perform_semantic_action("actions.combo", SemanticAction::expand) &&
                (*combo).dropped_down.read() && window.focus_scope_depth() == 1U,
            "semantic combo expansion must enter the real overlay focus scope");
    const bool chosen = window.perform_semantic_action("actions.combo.overlay.list.item.1", SemanticAction::press);
    const SemanticSnapshot closed = window.semantic_snapshot();
    require(chosen &&
                (*combo).selected_index.read() == 1U && !(*combo).dropped_down.read() &&
                window.focus_scope_depth() == 0U &&
                !find_node(closed.roots,
                           "actions.combo.overlay.list"),
            "semantic overlay row press must commit, detach, and restore focus safely");
    require(window.perform_semantic_action("actions.combo", SemanticAction::expand) &&
                (*combo).dropped_down.read(),
            "ComboBox must reopen after semantic row activation");
    require(window.perform_semantic_action("actions.combo", SemanticAction::collapse) &&
                !(*combo).dropped_down.read() && window.focus_scope_depth() == 0U,
            "semantic combo collapse must revoke the real overlay scope");
    const SemanticSnapshot after_close = window.semantic_snapshot();
    require(!find_node(after_close.roots,
                       "actions.combo.overlay.list"),
            "detached overlay semantics must disappear in the closing generation");
}

void test_semantics_enforce_ui_thread() {
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("thread.button"), "Threaded");
    Window window(button, {120.0, 30.0});
    bool snapshot_rejected{};
    bool action_rejected{};
    std::thread worker(ExerciseSemanticsOffThread(
        window, snapshot_rejected, action_rejected));
    worker.join();
    require(snapshot_rejected && action_rejected,
            "semantic reads and actions must reject non-owner threads");
}

void test_virtual_list_items_are_selectable() {
    std::shared_ptr<gui_forms::ListBox> list = make_control<ListBox>(StableId("virtual.list"));
    (*list).accessible_name = "Saved locations";
    (*list).items = {"Desktop", "Documents", "Downloads"};
    (*list).set_requested_bounds({10.0, 20.0, 180.0, 82.0});
    Window window(list, {220.0, 130.0});
    const SemanticSnapshot before = window.semantic_snapshot();
    const SemanticNode* list_node = find_node(before.roots, "virtual.list");
    const SemanticNode* item = find_node(before.roots, "virtual.list.item.1");
    require(list_node && (*list_node).children.size() == 3U && item &&
                (*item).role == SemanticRole::list_item && (*item).name == "Documents" &&
                has_action(*item, SemanticAction::select),
            "painted ListBox rows must publish selectable virtual semantic children");
    require(window.perform_semantic_action("virtual.list.item.1",
                                            SemanticAction::select) &&
                (*list).selected_index() == 1U && window.focused_control() == list,
            "virtual list-item selection must route through retained ListBox behavior");
    const SemanticSnapshot after = window.semantic_snapshot();
    const SemanticNode* selected = find_node(after.roots, "virtual.list.item.1");
    require(selected && has_semantic_state((*selected).states, SemanticState::selected) &&
                has_semantic_state((*selected).states, SemanticState::focused),
            "selected virtual list item must publish selection and active focus state");
    std::size_t activated = 99U;
    SubscriptionToken activation = (*list).item_activated().subscribe(
        test_support::RecordValue<std::size_t>(activated));
    require(window.perform_semantic_action("virtual.list.item.2",
                                            SemanticAction::press) &&
                (*list).selected_index() == 2U && activated == 2U,
            "virtual list-item press must complete selection then activation");
}

void test_modal_semantic_scope() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Button> outside = make_control<Button>(StableId("outside"), "Outside");
    std::shared_ptr<Panel> dialog = make_control<Panel>(StableId("dialog"));
    std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("inside"));
    (*root).add_child(outside); (*root).add_child(dialog); (*dialog).add_child(field);
    (*dialog).set_requested_bounds({0, 0, 200, 100});
    (*field).set_requested_bounds({0, 0, 100, 25});
    Window window(root, {400, 300});
    const FocusScopeId scope = window.begin_focus_scope(dialog, field);
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    require(find_node(snapshot.roots, "inside") && !find_node(snapshot.roots, "outside"),
            "modal accessibility exposes only the active scope");
    require(!window.perform_semantic_action("outside", SemanticAction::press),
            "stale accessible objects cannot activate background commands");
    require(window.perform_semantic_action("inside", SemanticAction::set_value, "hello"), "modal field remains editable");
    window.end_focus_scope(scope);
    const SemanticSnapshot outside_snapshot = window.semantic_snapshot();
    require(find_node(outside_snapshot.roots, "outside"), "closing modal restores semantic tree");
}

// A control that publishes one virtual child, "shared", whose stable identity
// every instance repeats (as tree and object views repeat their model's item
// identities), and counts the presses that child receives. An unexposed one
// has no node of its own, so its child is published at its nearest exposed
// ancestor.
class SharedVirtualChild final : public Control {
public:
    SharedVirtualChild(StableId stable_id, bool exposed) : Control(std::move(stable_id)), exposed_(exposed) {}
    [[nodiscard]] int presses() const noexcept { return presses_; }
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::group;
        descriptor.exposed = exposed_;
        return descriptor;
    }
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override {
        SemanticNode node;
        node.stable_id = "shared";
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.name = "Shared";
        node.actions = {SemanticAction::press};
        std::vector<SemanticNode> nodes{node};
        return nodes;
    }
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view) override {
        if (stable_id != "shared" || action != SemanticAction::press) return false;
        ++presses_;
        return true;
    }

private:
    bool exposed_{};
    int presses_{};
};

// Hosts name a published node by its control's runtime identity (interview
// GF017): a disposed or replaced control's identity is refused, and a virtual
// child reaches its own owner even when another control repeats its stable
// identity.
void test_runtime_identity_semantic_actions() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("identity.root"));
    std::shared_ptr<SharedVirtualChild> first = make_control<SharedVirtualChild>(StableId("identity.first"), true);
    std::shared_ptr<SharedVirtualChild> second = make_control<SharedVirtualChild>(StableId("identity.second"), true);
    std::shared_ptr<GroupBox> holder = make_control<GroupBox>(StableId("identity.holder"), "Holder");
    std::shared_ptr<SharedVirtualChild> inner = make_control<SharedVirtualChild>(StableId("identity.inner"), false);
    std::shared_ptr<Button> button = make_control<Button>(StableId("identity.button"), "Press");
    (*first).set_requested_bounds({0, 0, 50, 20});
    (*second).set_requested_bounds({0, 30, 50, 20});
    (*holder).set_requested_bounds({60, 0, 80, 60});
    (*inner).set_requested_bounds({0, 0, 40, 20});
    (*button).set_requested_bounds({0, 60, 50, 20});
    (*root).add_child(first);
    (*root).add_child(second);
    (*root).add_child(holder);
    (*holder).add_child(inner);
    (*root).add_child(button);
    Window window(root, {200, 120});
    int clicks = 0;
    SubscriptionToken clicked =
        (*button).clicked().subscribe(test_support::IncrementCounter<int, ButtonBase&>(clicks));

    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* holder_node = find_node(snapshot.roots, "identity.holder");
    require(holder_node && find_node((*holder_node).children, "shared") && !find_node(snapshot.roots, "identity.inner"),
            "an unexposed control's virtual child is published at its nearest exposed ancestor");
    require(window.perform_semantic_action((*holder).runtime_id(), "shared", SemanticAction::press) &&
                (*inner).presses() == 1 && (*first).presses() == 0 && (*second).presses() == 0,
            "a virtual child named by its nearest exposed ancestor reaches the unexposed control inside it");
    require(window.perform_semantic_action((*second).runtime_id(), "shared", SemanticAction::press) &&
                (*second).presses() == 1 && (*first).presses() == 0,
            "a virtual child named by its owner's runtime identity reaches that owner only");
    require(window.perform_semantic_action("shared", SemanticAction::press) && (*first).presses() == 1,
            "a stable identity alone reaches the first control that claims it");
    require(!window.perform_semantic_action((*first).runtime_id(), "missing", SemanticAction::press),
            "a virtual child its owner does not claim is refused");
    require(window.perform_semantic_action((*button).runtime_id(), "", SemanticAction::press) && clicks == 1,
            "a control's own node is reached by its runtime identity");

    const RuntimeId stale = (*button).runtime_id();
    const Control::Ptr removed = (*root).remove_child(stale);
    require(removed != nullptr, "the button leaves the tree");
    require(!window.perform_semantic_action(stale, "", SemanticAction::press) && clicks == 1,
            "a removed control's runtime identity is refused");
    std::shared_ptr<Button> replacement = make_control<Button>(StableId("identity.button"), "Press");
    (*replacement).set_requested_bounds({0, 60, 50, 20});
    (*root).add_child(replacement);
    int replacement_clicks = 0;
    SubscriptionToken replacement_clicked = (*replacement).clicked().subscribe(
        test_support::IncrementCounter<int, ButtonBase&>(replacement_clicks));
    require(!window.perform_semantic_action(stale, "", SemanticAction::press) && replacement_clicks == 0,
            "a replaced control's old runtime identity never reaches its successor");
    require(window.perform_semantic_action((*replacement).runtime_id(), "", SemanticAction::press) &&
                replacement_clicks == 1,
            "the successor answers to its own runtime identity");
    require(!window.perform_semantic_action(RuntimeId{}, "", SemanticAction::press),
            "the zero runtime identity names no control");
}

// A control that is not focusable and answers the focus action as handled
// without moving focus anywhere.
class ClaimsFocus final : public Control {
public:
    explicit ClaimsFocus(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] int focus_requests() const noexcept { return focus_requests_; }
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::group;
        descriptor.actions = {SemanticAction::focus};
        descriptor.exposed = true;
        return descriptor;
    }
    bool on_semantic_action(SemanticAction action, std::string_view) override {
        if (action != SemanticAction::focus) return false;
        ++focus_requests_;
        return true;
    }

private:
    int focus_requests_{};
};

// Whether the window's focused control is `control` or lies inside it.
bool focus_inside(const Window& window, const Control::Ptr& control) {
    const Control::Ptr focused = window.focused_control();
    const bool inside = focused && (focused == control || (*control).contains(*focused));
    return inside;
}

// The semantic focus action on a composite that is not focusable itself
// (focus belongs to its inner editor or part) is left to the composite's own
// on_semantic_action. The window performs it only when focus then lies
// inside the composite, and still refuses it where nothing can take focus.
void test_semantic_focus_reaches_composites() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("focus.root"));
    std::shared_ptr<NumericUpDown> numeric = make_control<NumericUpDown>(StableId("focus.numeric"));
    std::shared_ptr<DomainUpDown> domain = make_control<DomainUpDown>(StableId("focus.domain"));
    std::shared_ptr<MaskedTextBox> masked = make_control<MaskedTextBox>(StableId("focus.masked"), "(000) 000-0000");
    std::shared_ptr<TokenBox> tokens = make_control<TokenBox>(StableId("focus.tokens"));
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("focus.split"));
    std::shared_ptr<Panel> plain = make_control<Panel>(StableId("focus.plain"));
    std::shared_ptr<ClaimsFocus> claims = make_control<ClaimsFocus>(StableId("focus.claims"));
    std::shared_ptr<Button> button = make_control<Button>(StableId("focus.button"), "Press");
    (*numeric).set_requested_bounds({0, 0, 120, 24});
    (*domain).set_requested_bounds({0, 30, 120, 24});
    (*masked).set_requested_bounds({0, 60, 160, 24});
    (*tokens).set_requested_bounds({0, 90, 200, 28});
    (*split).set_requested_bounds({0, 130, 240, 80});
    (*plain).set_requested_bounds({250, 0, 60, 40});
    (*claims).set_requested_bounds({250, 50, 60, 40});
    (*button).set_requested_bounds({250, 100, 60, 24});
    (*root).add_child(numeric);
    (*root).add_child(domain);
    (*root).add_child(masked);
    (*root).add_child(tokens);
    (*root).add_child(split);
    (*root).add_child(plain);
    (*root).add_child(claims);
    (*root).add_child(button);
    Window window(root, {320, 220});
    window.perform_layout();

    const std::array<Control::Ptr, 5> composites{numeric, domain, masked, tokens, split};
    for (std::size_t index = 0; index < composites.size(); ++index) {
        const Control::Ptr& composite = composites[index];
        require(!(*composite).focusable(), "the composite is not focusable itself");
        require(window.request_focus(button) && window.focused_control() == button,
                "focus starts on the button");
        require(window.perform_semantic_action((*composite).stable_id().value(), SemanticAction::focus),
                "semantic focus on a composite named by stable identity is performed");
        require(focus_inside(window, composite) && window.focused_control() != composite,
                "the composite moves focus to its inner editor or part");
        require(window.request_focus(button), "focus returns to the button");
        require(window.perform_semantic_action((*composite).runtime_id(), "", SemanticAction::focus) &&
                    focus_inside(window, composite),
                "semantic focus on a composite named by runtime identity is performed");
    }

    require(window.request_focus(button), "focus returns to the button");
    require(!window.perform_semantic_action("focus.plain", SemanticAction::focus) &&
                window.focused_control() == button,
            "a control that is not focusable and has nothing to take focus is refused");
    require(!window.perform_semantic_action("focus.claims", SemanticAction::focus) &&
                (*claims).focus_requests() == 1 && window.focused_control() == button,
            "a control that answers focus without moving it into itself is refused");
    require(window.perform_semantic_action("focus.button", SemanticAction::focus) &&
                window.focused_control() == button,
            "a focusable control still takes focus directly");

    (*numeric).enabled = false;
    require(!window.perform_semantic_action("focus.numeric", SemanticAction::focus) &&
                window.focused_control() == button,
            "a disabled composite is refused");
}

// A node publishes the focusable state when a control it hides (one that
// publishes no node of its own) can take focus, and the focused state while
// focus is on such a control, so a client sees focus on the composite.
void test_hidden_focus_is_published_on_the_composite() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("hidden.root"));
    std::shared_ptr<NumericUpDown> numeric = make_control<NumericUpDown>(StableId("hidden.numeric"));
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("hidden.split"));
    std::shared_ptr<TokenBox> tokens = make_control<TokenBox>(StableId("hidden.tokens"));
    std::shared_ptr<Panel> plain = make_control<Panel>(StableId("hidden.plain"));
    std::shared_ptr<Button> button = make_control<Button>(StableId("hidden.button"), "Press");
    (*numeric).set_requested_bounds({0, 0, 120, 24});
    (*split).set_requested_bounds({0, 30, 240, 80});
    (*tokens).set_requested_bounds({0, 120, 200, 28});
    (*plain).set_requested_bounds({250, 0, 60, 40});
    (*button).set_requested_bounds({250, 50, 60, 24});
    (*root).add_child(numeric);
    (*root).add_child(split);
    (*root).add_child(tokens);
    (*root).add_child(plain);
    (*root).add_child(button);
    Window window(root, {320, 160});
    window.perform_layout();
    require(window.request_focus(button), "focus starts on the button");

    const SemanticSnapshot resting = window.semantic_snapshot();
    const SemanticNode* numeric_node = find_node(resting.roots, "hidden.numeric");
    const SemanticNode* split_node = find_node(resting.roots, "hidden.split");
    const SemanticNode* tokens_node = find_node(resting.roots, "hidden.tokens");
    const SemanticNode* editor_node = find_node(resting.roots, "hidden.tokens.editor");
    const SemanticNode* plain_node = find_node(resting.roots, "hidden.plain");
    require(numeric_node && has_semantic_state((*numeric_node).states, SemanticState::focusable) &&
                !has_semantic_state((*numeric_node).states, SemanticState::focused),
            "NumericUpDown's node is focusable through its hidden editor, and not focused");
    require(split_node && has_semantic_state((*split_node).states, SemanticState::focusable),
            "SplitContainer's node is focusable through its splitter, which publishes no node");
    require(tokens_node && !has_semantic_state((*tokens_node).states, SemanticState::focusable) && editor_node &&
                has_semantic_state((*editor_node).states, SemanticState::focusable),
            "TokenBox publishes its editor as a node, which is focusable; the list itself is not");
    require(plain_node == nullptr, "a plain panel publishes no node");

    require(window.perform_semantic_action("hidden.numeric", SemanticAction::focus),
            "focus on NumericUpDown is performed");
    const SemanticSnapshot focused = window.semantic_snapshot();
    numeric_node = find_node(focused.roots, "hidden.numeric");
    const SemanticNode* button_node = find_node(focused.roots, "hidden.button");
    require(numeric_node && has_semantic_state((*numeric_node).states, SemanticState::focused) && button_node &&
                !has_semantic_state((*button_node).states, SemanticState::focused),
            "while its hidden editor has focus, NumericUpDown's node is the focused one");

    require(window.perform_semantic_action("hidden.split", SemanticAction::focus), "focus on SplitContainer is performed");
    const SemanticSnapshot split_focused = window.semantic_snapshot();
    split_node = find_node(split_focused.roots, "hidden.split");
    numeric_node = find_node(split_focused.roots, "hidden.numeric");
    require(split_node && has_semantic_state((*split_node).states, SemanticState::focused) && numeric_node &&
                !has_semantic_state((*numeric_node).states, SemanticState::focused),
            "focus on the splitter shows on SplitContainer's node and leaves NumericUpDown's");

    require(window.perform_semantic_action("hidden.tokens", SemanticAction::focus), "focus on TokenBox is performed");
    const SemanticSnapshot tokens_focused = window.semantic_snapshot();
    tokens_node = find_node(tokens_focused.roots, "hidden.tokens");
    editor_node = find_node(tokens_focused.roots, "hidden.tokens.editor");
    require(tokens_node && !has_semantic_state((*tokens_node).states, SemanticState::focused) && editor_node &&
                has_semantic_state((*editor_node).states, SemanticState::focused),
            "TokenBox's published editor is the focused node, not the list");

    (*numeric).enabled = false;
    const SemanticSnapshot disabled = window.semantic_snapshot();
    numeric_node = find_node(disabled.roots, "hidden.numeric");
    require(numeric_node && !has_semantic_state((*numeric_node).states, SemanticState::focusable),
            "a disabled composite's hidden editor cannot take focus, so its node is not focusable");
}

// A control that publishes every field of the stage A vocabulary (12.13).
class StageAFields final : public Control {
public:
    explicit StageAFields(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::tree_item;
        descriptor.name = "Item";
        descriptor.level = 3U;
        descriptor.modal = true;
        descriptor.row_index = 4U;
        descriptor.column_index = 2U;
        descriptor.row_count = 10U;
        descriptor.column_count = 5U;
        descriptor.access_key = "F";
        descriptor.small_step = 1.0;
        descriptor.large_step = 10.0;
        descriptor.text_selection = SemanticTextSelection{Utf8Offset(1U), Utf8Offset(3U)};
        descriptor.multiline = true;
        descriptor.announces_on_appearance = true;
        descriptor.states = SemanticState::checkable | SemanticState::multiselectable;
        descriptor.exposed = true;
        return descriptor;
    }
};

// The stage A atoms: the role and state names, the field list, and every
// field from a descriptor to its node and the snapshot's JSON.
void test_stage_a_vocabulary_reaches_the_snapshot() {
    require(std::string_view(semantic_role_name(SemanticRole::tree_grid)) == "tree_grid" &&
                std::string_view(semantic_role_name(SemanticRole::live_region)) == "live_region" &&
                std::string_view(semantic_state_name(SemanticState::multiselectable)) == "multiselectable",
            "the new roles and states have their names");
    const std::span<const char* const> fields = semantic_field_names();
    require(fields.size() == 18U && std::string_view(fields[5]) == "level" &&
                std::string_view(fields[15]) == "multiline" && std::string_view(fields[16]) == "tool_tip" &&
                std::string_view(fields[17]) == "announces_on_appearance",
            "the field list names the admitted fields in order");
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("stage-a.root"));
    std::shared_ptr<StageAFields> item = make_control<StageAFields>(StableId("stage-a.item"));
    (*item).set_requested_bounds({0, 0, 40, 20});
    (*root).add_child(item);
    Window window(root, {100, 50});
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* node = find_node(snapshot.roots, "stage-a.item");
    require(node != nullptr && (*node).role == SemanticRole::tree_item && (*node).level == 3U && (*node).modal &&
                (*node).row_index == 4U && (*node).column_index == 2U && (*node).row_count == 10U &&
                (*node).column_count == 5U && (*node).access_key == "F" && (*node).small_step == 1.0 &&
                (*node).large_step == 10.0 && (*node).text_selection &&
                (*(*node).text_selection).anchor == Utf8Offset(1U) && (*(*node).text_selection).caret == Utf8Offset(3U) &&
                (*node).multiline && (*node).announces_on_appearance &&
                has_semantic_state((*node).states, SemanticState::checkable) &&
                has_semantic_state((*node).states, SemanticState::multiselectable),
            "every stage A field reaches the node");
    const std::string json = snapshot.to_json();
    require(json.find("\"role\":\"tree_item\"") != std::string::npos &&
                json.find("\"level\":3,\"modal\":true,\"row_index\":4,\"column_index\":2,\"row_count\":10,"
                          "\"column_count\":5,\"access_key\":\"F\",\"small_step\":1,\"large_step\":10,"
                          "\"text_selection\":{\"anchor\":1,\"caret\":3},\"multiline\":true") != std::string::npos &&
                json.find("\"announces_on_appearance\":true") != std::string::npos,
            "the snapshot's JSON carries every stage A field");
}

// The announcement rule every host shares (semantic_announcement_due): a
// known live region announces a new, non-empty name; a new one only when it
// announces on appearance (a toast); nothing else announces.
void test_announcement_rule() {
    SemanticNode region;
    region.role = SemanticRole::live_region;
    region.name = "Message archived.";
    require(!semantic_announcement_due(nullptr, region), "a new live region announces nothing by default");
    region.announces_on_appearance = true;
    require(semantic_announcement_due(nullptr, region), "a new live region that announces on appearance is spoken");
    SemanticNode same = region;
    require(!semantic_announcement_due(&same, region), "an unchanged name is not spoken again");
    SemanticNode renamed = region;
    renamed.name = "Message restored.";
    renamed.announces_on_appearance = false;
    require(semantic_announcement_due(&region, renamed), "a known live region speaks its new name");
    SemanticNode emptied = region;
    emptied.name.clear();
    require(!semantic_announcement_due(&region, emptied) && !semantic_announcement_due(nullptr, emptied),
            "an empty name is never spoken");
    SemanticNode label = region;
    label.role = SemanticRole::static_text;
    require(!semantic_announcement_due(nullptr, label) && !semantic_announcement_due(&region, label),
            "only a live region announces");
}

// A node of a given role, exposed.
class RoleProbe final : public Control {
public:
    RoleProbe(StableId stable_id, SemanticRole role) : Control(std::move(stable_id)), role_(role) {}
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = role_;
        descriptor.name = "Probe";
        descriptor.exposed = true;
        return descriptor;
    }

private:
    SemanticRole role_;
};

// A read-only reading within a range, as an instrument publishes one.
class MeterProbe final : public Control {
public:
    explicit MeterProbe(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::meter;
        descriptor.name = "Signal level";
        descriptor.value = "-12 dB";
        descriptor.numeric_value = -12.0;
        descriptor.minimum_value = -60.0;
        descriptor.maximum_value = 0.0;
        descriptor.states = SemanticState::read_only;
        descriptor.exposed = true;
        return descriptor;
    }
};

// The instrument vocabulary (W-5): the meter role has its name, comes after
// every earlier role (names are only added), and reaches the snapshot with
// its reading and range.
// The specialised roles (W-4): appended after meter with their names; each
// falls back to the role it specialises, and every other role to itself.
void test_specialised_roles() {
    require(std::string_view(semantic_role_name(SemanticRole::toggle_switch)) == "toggle_switch" &&
                std::string_view(semantic_role_name(SemanticRole::search_box)) == "search_box",
            "the switch and search roles have their names");
    const std::size_t meter = static_cast<std::size_t>(SemanticRole::meter);
    require(static_cast<std::size_t>(SemanticRole::toggle_switch) == meter + 1U &&
                static_cast<std::size_t>(SemanticRole::search_box) + 1U == semantic_role_count,
            "the switch and search roles are appended after meter");
    require(semantic_role_fallback(SemanticRole::toggle_switch) == SemanticRole::check_box &&
                semantic_role_fallback(SemanticRole::search_box) == SemanticRole::text_box,
            "a switch falls back to a check box, a search box to a text box");
    bool own = true;
    for (std::size_t index = 0U; index < semantic_role_count; ++index) {
        const SemanticRole role = static_cast<SemanticRole>(index);
        const bool specialised = role == SemanticRole::toggle_switch || role == SemanticRole::search_box;
        if (!specialised && semantic_role_fallback(role) != role) own = false;
    }
    require(own, "every other role is its own fallback");
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("special.root"));
    std::shared_ptr<RoleProbe> toggle =
        make_control<RoleProbe>(StableId("special.switch"), SemanticRole::toggle_switch);
    std::shared_ptr<RoleProbe> search =
        make_control<RoleProbe>(StableId("special.search"), SemanticRole::search_box);
    (*toggle).set_requested_bounds({0, 0, 40, 20});
    (*search).set_requested_bounds({0, 20, 80, 20});
    (*root).add_child(toggle);
    (*root).add_child(search);
    Window window(root, {100, 50});
    const std::string json = window.semantic_snapshot().to_json();
    require(json.find("\"role\":\"toggle_switch\"") != std::string::npos &&
                json.find("\"role\":\"search_box\"") != std::string::npos,
            "the snapshot's JSON names both roles");
}

// A SearchField (coordinator ruling) is one search_box node: the inner
// editor is not published, the node carries the query, the editor's
// selection and the focus, and its text requests reach the editor.
void test_search_field_is_one_node() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("single.root"));
    std::shared_ptr<SearchField> field = make_control<SearchField>(StableId("single.search"));
    (*field).set_requested_bounds({0, 0, 200, 30});
    (*field).accessible_name = std::string{"Search"};
    (*root).add_child(field);
    Window window(root, {240, 60});
    (*field).text = std::string{"ab"};
    require(window.request_focus((*field).editor()), "the editor takes the focus");
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* node = find_node(snapshot.roots, "single.search");
    require(node != nullptr && (*node).role == SemanticRole::search_box && (*node).value == "ab" &&
                (*node).children.empty() && !find_node(snapshot.roots, "single.search.editor"),
            "the field is one search_box node and its editor is not published");
    require((*node).text_selection.has_value() && has_semantic_state((*node).states, SemanticState::focused),
            "the node carries the editor's selection and its focus");
    SemanticTextLayout layout;
    require(window.semantic_text_layout((*field).runtime_id(), layout) == SemanticTextResult::done &&
                layout.graphemes.size() == 2U,
            "the node's text layout is the editor's");
    const SemanticTextSelection all{Utf8Offset(0U), Utf8Offset(2U)};
    require(window.set_semantic_text_selection((*field).runtime_id(), all) == SemanticTextResult::done &&
                (*(*field).editor()).semantic_descriptor().text_selection == std::optional<SemanticTextSelection>{all},
            "a selection set on the node selects in the editor");
}

void test_meter_role_reaches_the_snapshot() {
    require(std::string_view(semantic_role_name(SemanticRole::meter)) == "meter",
            "the meter role has its name");
    require(static_cast<std::size_t>(SemanticRole::meter) == static_cast<std::size_t>(SemanticRole::live_region) + 1U,
            "the meter role is appended after live_region");
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("meter.root"));
    std::shared_ptr<MeterProbe> meter = make_control<MeterProbe>(StableId("meter.level"));
    (*meter).set_requested_bounds({0, 0, 80, 20});
    (*root).add_child(meter);
    Window window(root, {100, 50});
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* node = find_node(snapshot.roots, "meter.level");
    require(node != nullptr && (*node).role == SemanticRole::meter && (*node).name == "Signal level" &&
                (*node).numeric_value == std::optional<double>{-12.0} &&
                (*node).minimum_value == std::optional<double>{-60.0} &&
                (*node).maximum_value == std::optional<double>{0.0} &&
                has_semantic_state((*node).states, SemanticState::read_only),
            "a meter node carries its reading, its range and read-only");
    const std::string json = snapshot.to_json();
    require(json.find("\"role\":\"meter\"") != std::string::npos, "the snapshot's JSON names the meter role");
}

// The helper every control's virtual semantic children used to copy, kept
// verbatim as the reference the shared helper must match byte for byte.
std::uint64_t former_copy(std::string_view id) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : id) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    const std::uint64_t result = hash | (1ULL << 63U);
    return result;
}

void test_virtual_runtime_ids_are_unchanged() {
    const std::array<std::string_view, 7> ids{
        "", "a", "foobar", "site.stack.3", "strip.tab.doc:file:///a.txt", "café",
        "a stable ID long enough to carry the hash through many rounds of the loop"};
    for (const std::string_view id : ids) {
        require(virtual_semantic_runtime_id(id) == former_copy(id),
                "the shared virtual runtime ID matches the former copies");
        require((virtual_semantic_runtime_id(id) & (1ULL << 63U)) != 0U, "a virtual runtime ID has its top bit set");
    }
    // Pinned values, so a change to the reference copy cannot hide one.
    require(virtual_semantic_runtime_id("") == 0x94650fb0739d0383ULL &&
                virtual_semantic_runtime_id("a") == 0xc4bd8ad473cd9906ULL &&
                virtual_semantic_runtime_id("site.stack.3") == 0xe9a74df8c42ada75ULL &&
                virtual_semantic_runtime_id("café") == 0x8385b6459d53dff7ULL,
            "the virtual runtime IDs keep their recorded values");
    require(is_virtual_semantic_runtime_id(virtual_semantic_runtime_id("a")) &&
                !is_virtual_semantic_runtime_id(1U) && !is_virtual_semantic_runtime_id(0U),
            "a virtual runtime ID is told from a control's and from zero");
}

// Two trees showing one model publish items with the same stable IDs, as the
// Gallery's tree and tree list do. An action named by a tree's runtime
// identity reaches that tree's item; one named by stable identity alone
// reaches the first tree in tree order.
void test_trees_sharing_item_identities_route_by_owner() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("trees.root"));
    std::shared_ptr<TreeView> left = make_control<TreeView>(StableId("trees.left"));
    std::shared_ptr<TreeView> right = make_control<TreeView>(StableId("trees.right"));
    std::vector<TreeViewItem> items;
    items.push_back(TreeViewItem{"src", "src", 0U, false, false, true, {}});
    items.push_back(TreeViewItem{"docs", "docs", 0U, false, false, true, {}});
    (*left).set_items(items);
    (*right).set_items(items);
    (*left).set_requested_bounds({10.0, 10.0, 180.0, 120.0});
    (*right).set_requested_bounds({210.0, 10.0, 180.0, 120.0});
    (*root).add_child(left);
    (*root).add_child(right);
    Window window(root, {420.0, 160.0});
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* left_node = find_node(snapshot.roots, "trees.left");
    const SemanticNode* right_node = find_node(snapshot.roots, "trees.right");
    require(left_node != nullptr && right_node != nullptr &&
                find_node((*left_node).children, "docs") != nullptr &&
                find_node((*right_node).children, "docs") != nullptr,
            "both trees publish an item docs");
    require(window.perform_semantic_action((*right).runtime_id(), "docs", SemanticAction::select) &&
                (*right).selected_id.read() == "docs" && (*left).selected_id.read() != "docs",
            "an action named by the right tree's runtime identity reaches its item");
    require(window.perform_semantic_action("docs", SemanticAction::select) &&
                (*left).selected_id.read() == "docs",
            "an action named by stable identity alone reaches the first tree's item");
}

// A control that publishes virtual children without runtime IDs, as the
// Ribbon and BackstageView do: a group with a nested item, and one item that
// carries its own runtime ID.
class UnnumberedVirtualChildren final : public Control {
public:
    explicit UnnumberedVirtualChildren(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::group;
        descriptor.exposed = true;
        return descriptor;
    }
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override {
        SemanticNode item;
        item.stable_id = "unnumbered.group.item";
        item.role = SemanticRole::button;
        item.actions = {SemanticAction::press};
        SemanticNode group;
        group.stable_id = "unnumbered.group";
        group.role = SemanticRole::group;
        group.children.push_back(item);
        SemanticNode numbered;
        numbered.stable_id = "unnumbered.numbered";
        numbered.runtime_id = numbered_runtime_id;
        numbered.role = SemanticRole::button;
        std::vector<SemanticNode> nodes;
        nodes.push_back(group);
        nodes.push_back(numbered);
        return nodes;
    }
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view) override {
        if (stable_id != "unnumbered.group.item" || action != SemanticAction::press) return false;
        ++presses_;
        return true;
    }
    [[nodiscard]] int presses() const noexcept { return presses_; }

    // Deliberately not a value virtual_semantic_runtime_id gives, so the test
    // sees that a published runtime ID is kept as it is.
    static constexpr std::uint64_t numbered_runtime_id = 0x8000000000000005ULL;

private:
    int presses_{};
};

// The window gives a virtual child published with runtime ID zero, at any
// depth, the runtime ID its stable ID gives, so hosts never confuse it with
// the window's own object (zero), and keeps a runtime ID a control set.
void test_unnumbered_virtual_children_get_runtime_ids() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("unnumbered.root"));
    std::shared_ptr<UnnumberedVirtualChildren> owner =
        make_control<UnnumberedVirtualChildren>(StableId("unnumbered.owner"));
    (*owner).set_requested_bounds({0.0, 0.0, 80.0, 40.0});
    (*root).add_child(owner);
    Window window(root, {100.0, 60.0});
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* group = find_node(snapshot.roots, "unnumbered.group");
    const SemanticNode* item = find_node(snapshot.roots, "unnumbered.group.item");
    const SemanticNode* numbered = find_node(snapshot.roots, "unnumbered.numbered");
    require(group != nullptr && item != nullptr && numbered != nullptr,
            "the virtual children are published");
    require((*group).runtime_id == virtual_semantic_runtime_id("unnumbered.group") &&
                (*item).runtime_id == virtual_semantic_runtime_id("unnumbered.group.item"),
            "a virtual child published with runtime ID zero gets the one its stable ID gives");
    require(is_virtual_semantic_runtime_id((*group).runtime_id) &&
                is_virtual_semantic_runtime_id((*item).runtime_id),
            "an assigned runtime ID is told from a control's");
    require((*numbered).runtime_id == UnnumberedVirtualChildren::numbered_runtime_id,
            "a runtime ID the control published is kept");
    require(window.perform_semantic_action((*owner).runtime_id(), "unnumbered.group.item",
                                           SemanticAction::press) &&
                (*owner).presses() == 1,
            "a nested virtual child is reached through its owner's runtime identity");
}

// A control whose virtual child's ID, or the panel a node names as
// controlled, breaks the StableId segment rule.
class MisnamedVirtualChild final : public Control {
public:
    MisnamedVirtualChild(StableId stable_id, std::string child_id, std::string controlled)
        : Control(std::move(stable_id)), child_id_(std::move(child_id)), controlled_(std::move(controlled)) {}
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::group;
        descriptor.exposed = true;
        return descriptor;
    }
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override {
        SemanticNode child;
        child.stable_id = child_id_;
        child.role = SemanticRole::tab;
        child.controlled_panel = controlled_;
        std::vector<SemanticNode> nodes;
        nodes.push_back(child);
        return nodes;
    }

private:
    std::string child_id_;
    std::string controlled_;
};

bool snapshot_refused(const std::string& child_id, const std::string& controlled) {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("misnamed.root"));
    std::shared_ptr<MisnamedVirtualChild> owner =
        make_control<MisnamedVirtualChild>(StableId("misnamed.owner"), child_id, controlled);
    (*owner).set_requested_bounds({0.0, 0.0, 80.0, 40.0});
    (*root).add_child(owner);
    Window window(root, {100.0, 60.0});
    bool refused{};
    try {
        static_cast<void>(window.semantic_snapshot());
    } catch (const std::invalid_argument& error) {
        refused = std::string_view(error.what()).find("misnamed.owner") != std::string_view::npos;
    }
    return refused;
}

// Virtual semantic node IDs follow the StableId segment rule, as control IDs
// do; the window checks them when a control publishes them.
void test_virtual_node_ids_follow_the_segment_rule() {
    require(!snapshot_refused("misnamed.owner.tab-row", "misnamed.owner.command-plane"),
            "dash-segment virtual IDs are published");
    require(snapshot_refused("misnamed.owner.tab_row", ""), "an underscore virtual ID is refused, naming its control");
    require(snapshot_refused("misnamed.owner.tab-row", "misnamed.owner.command_plane"),
            "a controlled panel that breaks the rule is refused");
    require(snapshot_refused("", ""), "an empty virtual ID is refused");
}

} // namespace

int main() {
    try {
        test_virtual_runtime_ids_are_unchanged();
        test_modal_semantic_scope();
        test_subtree_visibility_advances_semantic_generation();
        test_snapshot_roles_states_hierarchy_and_json();
        test_semantic_actions_use_control_behavior();
        test_virtual_list_items_are_selectable();
        test_runtime_identity_semantic_actions();
        test_trees_sharing_item_identities_route_by_owner();
        test_unnumbered_virtual_children_get_runtime_ids();
        test_virtual_node_ids_follow_the_segment_rule();
        test_semantic_focus_reaches_composites();
        test_hidden_focus_is_published_on_the_composite();
        test_stage_a_vocabulary_reaches_the_snapshot();
        test_announcement_rule();
        test_meter_role_reaches_the_snapshot();
        test_specialised_roles();
        test_search_field_is_one_node();
        test_semantics_enforce_ui_thread();
        std::cout << "gui_forms_semantic_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_semantic_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
