#include "page_tests.hpp"

#include <string_view>

namespace gallery_pages_tests {

namespace {

constexpr std::size_t no_line = static_cast<std::size_t>(-1);

// The index of the first log line at or after `from` that contains `text`.
std::size_t find_line(const ListBox& log, std::string_view text, std::size_t from = 0U) {
    const std::span<const std::string> items = log.items.read();
    for (std::size_t index = from; index < items.size(); ++index) {
        if (items[index].find(text) != std::string::npos) return index;
    }
    return no_line;
}

void press(Window& window, Point point, Modifier modifiers, std::uint32_t clicks) {
    PointerEvent down{};
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = point;
    down.modifiers = modifiers;
    down.pointer_id = 1U;
    down.click_count = clicks;
    PointerEvent up = down;
    up.action = PointerAction::up;
    static_cast<void>(window.dispatch_pointer(down));
    static_cast<void>(window.dispatch_pointer(up));
}

} // namespace

// The input-and-rules page: the target's WinForms mouse events reach the
// log in WinForms' order; a pointer context request is logged and handled;
// the page's three rules run before the subscribers once armed, and stop
// when revoked.
void test_input_and_rules(gui_forms::Window& window) {
    select_page(window, "input-and-rules");
    const Control::Ptr target = window.find("gallery.input.target");
    const std::shared_ptr<ListBox> log = std::dynamic_pointer_cast<ListBox>(window.find("gallery.input.log"));
    const std::shared_ptr<Label> statistics =
        std::dynamic_pointer_cast<Label>(window.find("gallery.input.rules.statistics"));
    const Control::Ptr armed = window.find("gallery.input.rules.armed");
    const std::shared_ptr<CheckBox> marked = std::dynamic_pointer_cast<CheckBox>(window.find("gallery.input.marked"));
    const Control::Ptr button = window.find("gallery.input.press");
    const Control::Ptr clear = window.find("gallery.input.clear");
    require(target && log && statistics && armed && marked && button && clear,
            "the input-and-rules page publishes its target, log and rule controls by stable id");

    // A click: MouseDown, then Click and MouseClick, then MouseUp.
    click(window, target);
    const std::size_t down = find_line(*log, "MouseDown · primary ×1");
    const std::size_t clicked = find_line(*log, "Click", down == no_line ? 0U : down + 1U);
    const std::size_t mouse_click = find_line(*log, "MouseClick · primary ×1");
    const std::size_t up = find_line(*log, "MouseUp · primary ×1");
    require(down != no_line && clicked != no_line && mouse_click != no_line && up != no_line,
            "a click on the target logs MouseDown, Click, MouseClick and MouseUp");
    require(down < clicked && clicked <= mouse_click && mouse_click < up,
            "the log keeps WinForms' order: MouseDown, Click, MouseClick, MouseUp");

    // A pointer context request is logged at the target and handled there.
    require(window.dispatch_context_request(ContextRequestOrigin::pointer, center(target)),
            "the target handles a pointer context request");
    require(find_line(*log, "ContextMenuRequested · pointer") != no_line,
            "the context request reaches the log");
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::escape}));

    // Arm the rules through the bound check box.
    click(window, clear);
    require((*log).items.read().empty(), "Clear empties the log");
    require((*statistics).text.read() == "Rules not armed", "the rules start unarmed");
    click(window, armed);
    require(find_line(*log, "Rules armed") != no_line && (*statistics).text.read().find("Occurrences") == 0U,
            "checking the bound box arms the rules and shows the window's rule statistics");

    // Rule 1: a Shift+click on the button is consumed before its command.
    press(window, center(button), Modifier::shift, 1U);
    require(find_line(*log, "Rule 1 consumed") != no_line, "the Shift+click rule fires");
    require(find_line(*log, "command ran") == no_line, "a consumed Click withholds the button's command");
    click(window, button);
    require(find_line(*log, "The button's command ran (1)") != no_line, "a plain click still runs the command");

    // Rule 2: H while hovering the target toggles the bound Marked box.
    require(!(*marked).checked.read(), "Marked starts cleared");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, center(target)}));
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::h}));
    static_cast<void>(window.dispatch_key({KeyAction::up, PhysicalKey::h}));
    require((*marked).checked.read() && find_line(*log, "Rule 2") != no_line,
            "H over the target toggles the Value the Marked box binds");

    // Rule 3 notes the double-click before the target's subscribers.
    press(window, center(target), Modifier::none, 1U);
    press(window, center(target), Modifier::none, 2U);
    const std::size_t noted = find_line(*log, "Rule 3 saw MouseDoubleClick");
    const std::size_t subscribed = find_line(*log, "MouseDoubleClick · primary ×2");
    require(noted != no_line && subscribed != no_line && noted < subscribed,
            "the double-click rule runs before the target's MouseDoubleClick subscribers");

    // Revoked, the rules stop.
    click(window, armed);
    require(find_line(*log, "Rules revoked") != no_line && (*statistics).text.read() == "Rules not armed",
            "clearing the bound box revokes the rules");
    click(window, clear);
    press(window, center(button), Modifier::shift, 1U);
    require(find_line(*log, "Rule 1") == no_line && find_line(*log, "command ran") != no_line,
            "with the rules revoked a Shift+click runs the command");
}

} // namespace gallery_pages_tests
