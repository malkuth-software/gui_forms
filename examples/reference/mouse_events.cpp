// The WinForms mouse events, a context request and a rule beside them
// (docs/user/mouse-events.md), driven headless: a window holding a panel
// and a button (forms/mouse_events.form), an owning component subscribed
// to their events, then a click, a double-click and a keyboard context
// request, and a consuming rule that withholds the button's Click.

#include "mouse_events_form.hpp"   // generated from forms/mouse_events.form

#include <gui_forms/component.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/rules.hpp>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace gf = gui_forms;

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

// Owns its subscriptions: disposing it disconnects them all. `log` gets one
// letter per event, so the order reads at a glance: d(own), c(lick),
// m(ouse click), a(ctivation), u(p), D(ouble click), x (context request).
class Journal final : public gf::Component {
public:
    Journal(gf::Button& ok, gf::Panel& surface) {
        gf::on(ok.mouse_down(), *this, &Journal::down);
        gf::on(ok.click(), *this, &Journal::clicked);
        gf::on(ok.mouse_click(), *this, &Journal::mouse_clicked);
        gf::on(ok.clicked(), *this, &Journal::activated);
        gf::on(ok.mouse_up(), *this, &Journal::up);
        gf::on(surface.double_click(), *this, &Journal::doubled);
        gf::on(surface.context_menu_requested(), *this, &Journal::menu);
    }

    std::string log{};
    unsigned rule_firings{};

private:
    void down(const gf::MouseEventArgs& args) {
        // The position is in the button's own coordinates.
        require(args.location.x >= 0.0 && args.location.y >= 0.0, "MouseDown carries a local position");
        log.push_back('d');
    }
    void clicked() { log.push_back('c'); }
    void mouse_clicked(const gf::MouseEventArgs& args) {
        require(args.clicks == 1U, "a single click's count is 1");
        log.push_back('m');
    }
    void activated() { log.push_back('a'); }
    void up() { log.push_back('u'); }
    void doubled() { log.push_back('D'); }
    void menu(gf::ContextMenuRequest& request) {
        // A keyboard request has no pointer position: the control picks its
        // anchor. Handling it keeps it from the panel's ancestors.
        log.push_back('x');
        request.handled = true;
    }
};

// A named rule action: rules take plain functions with an explicit context.
void count_firing(Journal& journal, const gf::RuleFiring&) { ++journal.rule_firings; }

void press_and_release(gf::Window& window, gf::Point position, std::uint32_t clicks) {
    gf::PointerEvent move{gf::PointerAction::move, gf::PointerButton::none, position};
    move.click_count = 0U;
    gf::PointerEvent down{gf::PointerAction::down, gf::PointerButton::primary, position};
    down.click_count = clicks;
    gf::PointerEvent up{gf::PointerAction::up, gf::PointerButton::primary, position};
    up.click_count = clicks;
    static_cast<void>(window.dispatch_pointer(move));
    static_cast<void>(window.dispatch_pointer(down));
    static_cast<void>(window.dispatch_pointer(up));
}

void run() {
    reference::MouseWindow window{};   // binds nothing, so it takes no model
    window.perform_layout();
    const std::shared_ptr<gf::Button> ok = window.example_ok;   // request_focus takes the shared pointer
    Journal journal(*ok, *window.example_surface);

    // A click: MouseDown, Click, MouseClick, the button's activation, MouseUp.
    press_and_release(window, {20.0, 20.0}, 1U);
    require(journal.log == "dcmau", "a click raises the events in WinForms' order");

    // A double-click on the panel: the host counts the second press (2),
    // and DoubleClick arrives on its release.
    journal.log.clear();
    press_and_release(window, {200.0, 100.0}, 1U);
    press_and_release(window, {200.0, 100.0}, 2U);
    require(journal.log == "D", "the panel's second release raises DoubleClick");

    // A keyboard context request goes to the focused control, then to its
    // ancestors: the button leaves it, and the panel handles it.
    require(window.request_focus(ok), "the button takes focus");
    journal.log.clear();
    const bool handled = window.dispatch_context_request(gf::ContextRequestOrigin::keyboard, gf::Point{});
    require(handled && journal.log == "x", "the panel handles the keyboard context request");

    // A consuming rule on the button's Click runs before the subscribers and
    // keeps Click, and the activation, from them; MouseUp is still raised.
    gf::Rule swallow{};
    swallow.trigger = gf::trigger::click(*ok);
    swallow.actions.push_back(gf::action(journal, &count_firing));
    swallow.consume = true;
    const gf::RuleAddResult added = window.rules().add(swallow);
    require(added.refusal == gf::RuleRefusal::none, "the rule is armed");
    journal.log.clear();
    press_and_release(window, {20.0, 20.0}, 1U);
    require(journal.rule_firings == 1U && journal.log == "dmu",
            "the consumed Click reaches neither its subscriber nor the activation");

    // Disposing the journal disconnects every subscription it owns.
    journal.dispose();
    journal.log.clear();
    press_and_release(window, {20.0, 20.0}, 1U);
    require(journal.log.empty(), "a disposed owner hears nothing");
}

} // namespace

int main() {
    try {
        run();
    } catch (const std::exception& failure) {
        std::cerr << "mouse_events: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "mouse_events: ok\n";
    return 0;
}
