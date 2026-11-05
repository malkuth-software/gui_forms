#include "gui_forms/basic_controls.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/controls/panel/combo_box/combo_box.hpp"

#include <cstdlib>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct FocusFixture final {
    FocusFixture() {
        (*outside).set_requested_bounds({0.0, 0.0, 80.0, 24.0});
        (*popup).set_requested_bounds({0.0, 30.0, 160.0, 70.0});
        (*first).set_requested_bounds({4.0, 4.0, 70.0, 24.0});
        (*second).set_requested_bounds({82.0, 4.0, 70.0, 24.0});
        (*popup).add_child(first);
        (*popup).add_child(second);
        (*root).add_child(outside);
        (*root).add_child(popup);
        window = std::make_unique<Window>(root, Size{200.0, 120.0});
        (*window).perform_layout();
    }

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("focus.root"));
    std::shared_ptr<Button> outside =
        make_control<Button>(StableId("focus.outside"), "Outside");
    std::shared_ptr<Panel> popup =
        make_control<Panel>(StableId("focus.popup"));
    std::shared_ptr<Button> first =
        make_control<Button>(StableId("focus.popup.first"), "First");
    std::shared_ptr<Button> second =
        make_control<Button>(StableId("focus.popup.second"), "Second");
    std::unique_ptr<Window> window;
};

class AppendFocusScopeChange final {
public:
    explicit AppendFocusScopeChange(std::vector<std::string>& changes)
        : changes_(changes) {}

    void operator()(const FocusScopeChange& change) const {
        changes_.push_back(std::string(change.opened ? "open:" : "close:") +
                           change.stable_id);
    }

private:
    std::vector<std::string>& changes_;
};

class ObserveFocusScopeClose final {
public:
    ObserveFocusScopeClose(FocusScopeCloseReason& close_reason, bool& saw_close)
        : close_reason_(close_reason), saw_close_(saw_close) {}

    void operator()(const FocusScopeChange& change) const {
        if (!change.opened) {
            close_reason_ = change.close_reason;
            saw_close_ = true;
        }
    }

private:
    FocusScopeCloseReason& close_reason_;
    bool& saw_close_;
};

class BeginFocusScopeOffThread final {
public:
    BeginFocusScopeOffThread(Window& window,
                             const std::shared_ptr<Panel>& popup,
                             bool& rejected)
        : window_(window), popup_(popup), rejected_(rejected) {}

    void operator()() const {
        try {
            static_cast<void>(window_.begin_focus_scope(popup_));
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    Window& window_;
    const std::shared_ptr<Panel>& popup_;
    bool& rejected_;
};

class CloseOpenedFocusScope final {
public:
    explicit CloseOpenedFocusScope(Window& window) : window_(window) {}

    void operator()(const FocusScopeChange& change) const {
        if (change.opened) {
            static_cast<void>(window_.end_focus_scope(change.scope));
        }
    }

private:
    Window& window_;
};

void test_containment_and_tab_traversal() {
    FocusFixture fixture;
    require((*fixture.window).request_focus(fixture.outside),
            "fixture outside control must accept focus");
    const FocusScopeId scope = (*fixture.window).begin_focus_scope(
        fixture.popup, fixture.first);
    require(scope && (*fixture.window).focus_scope_depth() == 1U &&
                (*fixture.window).active_focus_scope_root() == fixture.popup &&
                (*fixture.window).focused_control() == fixture.first,
            "scope entry must publish identity and focus the preferred descendant");
    require(!(*fixture.window).request_focus(fixture.outside) &&
                (*fixture.window).focused_control() == fixture.first,
            "contained scope must reject focus leakage to an outside control");

    KeyEvent tab;
    tab.action = KeyAction::down;
    tab.physical_key = PhysicalKey::tab;
    require((*fixture.window).dispatch_key(tab) &&
                (*fixture.window).focused_control() == fixture.second,
            "Tab must traverse forward inside the active scope");
    require((*fixture.window).dispatch_key(tab) &&
                (*fixture.window).focused_control() == fixture.first,
            "scope traversal must wrap in stable retained-tree order");
    tab.modifiers = Modifier::shift;
    require((*fixture.window).dispatch_key(tab) &&
                (*fixture.window).focused_control() == fixture.second,
            "Shift+Tab must traverse backward inside the active scope");

    require((*fixture.window).end_focus_scope(scope) &&
                (*fixture.window).focused_control() == fixture.outside &&
                (*fixture.window).focus_scope_depth() == 0U,
            "scope exit must restore the focus present at entry");
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.focus_scopes_opened == 1U &&
                metrics.focus_scopes_closed == 1U &&
                metrics.focus_scope_restorations == 1U &&
                metrics.focus_scope_rejections == 1U &&
                metrics.maximum_focus_scope_depth == 1U,
            "focus-scope behavior must be structurally observable in metrics");
}

void test_nested_out_of_order_close_restores_outer_history() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("nested.root"));
    std::shared_ptr<gui_forms::Button> base = make_control<Button>(StableId("nested.base"), "Base");
    std::shared_ptr<gui_forms::Panel> outer = make_control<Panel>(StableId("nested.outer"));
    std::shared_ptr<gui_forms::Button> outer_field =
        make_control<Button>(StableId("nested.outer.field"), "Outer");
    std::shared_ptr<gui_forms::Panel> inner = make_control<Panel>(StableId("nested.inner"));
    std::shared_ptr<gui_forms::Button> inner_field =
        make_control<Button>(StableId("nested.inner.field"), "Inner");
    (*inner).add_child(inner_field);
    (*outer).add_child(outer_field);
    (*outer).add_child(inner);
    (*root).add_child(base);
    (*root).add_child(outer);
    Window window(root, {240.0, 140.0});
    require(window.request_focus(base), "nested fixture base focus must succeed");

    std::vector<std::string> changes;
    SubscriptionToken changed = window.focus_scope_changed().subscribe(
        AppendFocusScopeChange(changes));
    const FocusScopeId outer_scope =
        window.begin_focus_scope(outer, outer_field);
    const FocusScopeId inner_scope =
        window.begin_focus_scope(inner, inner_field);
    require(window.focused_control() == inner_field &&
                window.focus_scope_depth() == 2U,
            "nested scope must own focus at the deepest active level");
    require(window.end_focus_scope(outer_scope) &&
                window.focused_control() == inner_field &&
                window.focus_scope_depth() == 1U,
            "out-of-order outer close must not steal focus from a live inner scope");
    require(window.end_focus_scope(inner_scope) &&
                window.focused_control() == base &&
                window.focus_scope_depth() == 0U,
            "closing the inner scope must collapse tombstones and restore outer history");
    require(changes == std::vector<std::string>{
                "open:nested.outer", "open:nested.inner",
                "close:nested.outer", "close:nested.inner"},
            "nested scope events must remain deterministic under out-of-order close");
}

void test_owner_unavailable_cleanup_and_nesting_guard() {
    FocusFixture fixture;
    require((*fixture.window).request_focus(fixture.outside),
            "cleanup fixture outside focus must succeed");
    FocusScopeCloseReason close_reason = FocusScopeCloseReason::explicit_close;
    bool saw_close = false;
    SubscriptionToken changed = (*fixture.window).focus_scope_changed().subscribe(
        ObserveFocusScopeClose(close_reason, saw_close));
    static_cast<void>((*fixture.window).begin_focus_scope(
        fixture.popup, fixture.first));

    bool rejected_foreign_nested_scope = false;
    try {
        static_cast<void>((*fixture.window).begin_focus_scope(fixture.outside));
    } catch (const std::logic_error&) {
        rejected_foreign_nested_scope = true;
    }
    require(rejected_foreign_nested_scope,
            "a nested scope must remain inside the containing retained subtree");

    (*fixture.popup).visible = false;
    require(saw_close &&
                close_reason == FocusScopeCloseReason::owner_unavailable &&
                (*fixture.window).focus_scope_depth() == 0U &&
                (*fixture.window).focused_control() == fixture.outside,
            "hiding a scope root must close it and restore eligible outside focus");
}

void test_ui_thread_enforcement_and_empty_focus_traversal() {
    FocusFixture fixture;
    KeyEvent tab;
    tab.action = KeyAction::down;
    tab.physical_key = PhysicalKey::tab;
    require((*fixture.window).dispatch_key(tab) &&
                (*fixture.window).focused_control() == fixture.outside,
            "Tab must establish initial focus when no control is active");

    bool rejected = false;
    std::thread worker(BeginFocusScopeOffThread(
        *fixture.window, fixture.popup, rejected));
    worker.join();
    require(rejected && (*fixture.window).focus_scope_depth() == 0U &&
                (*fixture.window).metrics_snapshot().rejected_wrong_thread_operations == 1U,
            "focus-scope mutation must preserve renderer-free UI-thread enforcement");
}

void test_bounded_nesting() {
    FocusFixture fixture;
    std::array<FocusScopeId, maximum_focus_scope_depth> scopes{};
    for (FocusScopeId& scope : scopes) {
        scope = (*fixture.window).begin_focus_scope(fixture.popup);
    }
    bool rejected = false;
    try {
        static_cast<void>((*fixture.window).begin_focus_scope(fixture.popup));
    } catch (const std::length_error&) {
        rejected = true;
    }
    require(rejected &&
                (*fixture.window).focus_scope_depth() == maximum_focus_scope_depth,
            "focus-scope nesting must stop at its declared portable bound");
    for (std::array<FocusScopeId, maximum_focus_scope_depth>::reverse_iterator scope =
             scopes.rbegin();
         scope != scopes.rend(); ++scope) {
        require((*fixture.window).end_focus_scope(*scope),
                "bounded focus scopes must close in reverse order");
    }
    require((*fixture.window).focus_scope_depth() == 0U &&
                (*fixture.window).metrics_snapshot().maximum_focus_scope_depth ==
                    maximum_focus_scope_depth,
            "bounded nesting depth must be observable and fully recoverable");
}

void test_open_callback_may_close_without_late_focus() {
    FocusFixture fixture;
    require((*fixture.window).request_focus(fixture.outside),
            "reentrant fixture outside focus must succeed");
    SubscriptionToken changed = (*fixture.window).focus_scope_changed().subscribe(
        CloseOpenedFocusScope(*fixture.window));
    const FocusScopeId scope = (*fixture.window).begin_focus_scope(
        fixture.popup, fixture.first);
    require(scope && (*fixture.window).focus_scope_depth() == 0U &&
                (*fixture.window).focused_control() == fixture.outside &&
                !(*fixture.window).end_focus_scope(scope),
            "a synchronous open handler may close the scope without a late focus leak");
}

void test_combo_popup_in_modal_scope() {
    FocusFixture fixture;
    std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("modal.combo"));
    (*combo).set_requested_bounds({4, 35, 100, 25});
    (*combo).set_items({"RGB", "OKHSL"});
    (*fixture.popup).add_child(combo);
    (*fixture.window).perform_layout();
    FocusScopeId modal = (*fixture.window).begin_focus_scope(fixture.popup, combo);
    for (int iteration = 0; iteration < 3; ++iteration) {
        (*combo).set_dropped_down(true);
        (*fixture.window).perform_layout();
        require((*fixture.window).focus_scope_depth() == 2 && (*combo).dropped_down(),
                "owned combo overlay may enter a nested dialog focus scope");
        require(!(*fixture.window).request_focus(fixture.outside),
                "open combo still contains focus away from unrelated controls");
        (*combo).set_dropped_down(false);
        require((*fixture.window).focus_scope_depth() == 1 && (*fixture.window).focused_control() == combo,
                "closing a nested combo restores its dialog owner");
    }
    std::shared_ptr<Panel> foreign = make_control<Panel>(StableId("foreign.popup"));
    PopupToken token = (*fixture.window).open_popup(fixture.outside, foreign);
    bool rejected = false;
    try { static_cast<void>((*fixture.window).begin_focus_scope(foreign)); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected && (*fixture.window).focus_scope_depth() == 1,
            "a popup owned outside the modal scope cannot enter it");
    token.disconnect();
    (*combo).set_dropped_down(true);
    (*fixture.popup).visible = false;
    require(!(*combo).dropped_down() && (*fixture.window).focus_scope_depth() == 0,
            "hiding a modal owner revokes its nested combo and both focus scopes");
    static_cast<void>((*fixture.window).end_focus_scope(modal));
}

void test_tab_index_and_tab_stop_define_deterministic_traversal() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("tab.root"));
    std::shared_ptr<gui_forms::Button> late = make_control<Button>(StableId("tab.late"), "Late");
    std::shared_ptr<gui_forms::Button> skipped = make_control<Button>(StableId("tab.skipped"), "Skipped");
    std::shared_ptr<gui_forms::Button> first = make_control<Button>(StableId("tab.first"), "First");
    (*late).tab_index = 20U;
    (*skipped).tab_index = 10U;
    (*skipped).tab_stop = false;
    (*first).tab_index = 0U;
    (*root).add_child(late);
    (*root).add_child(skipped);
    (*root).add_child(first);
    Window window(root, {200.0, 100.0});

    require(window.move_focus(true) && window.focused_control() == first,
            "Tab traversal must begin at the smallest retained TabIndex");
    require(window.move_focus(true) && window.focused_control() == late,
            "TabStop false must remove a focusable control from traversal");
    require(window.request_focus(skipped) && window.focused_control() == skipped,
            "TabStop false must not prevent explicit programmatic focus");
    require(window.request_focus(late),
            "reverse traversal fixture must restore a traversable starting point");
    require(window.move_focus(false) && window.focused_control() == first,
            "reverse traversal must use the same deterministic TabIndex order");
}

} // namespace

int main() {
    try {
        test_combo_popup_in_modal_scope();
        test_containment_and_tab_traversal();
        test_nested_out_of_order_close_restores_outer_history();
        test_owner_unavailable_cleanup_and_nesting_guard();
        test_ui_thread_enforcement_and_empty_focus_traversal();
        test_bounded_nesting();
        test_open_callback_may_close_without_late_focus();
        test_tab_index_and_tab_stop_define_deterministic_traversal();
        std::cout << "gui_forms_focus_scope_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_focus_scope_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
