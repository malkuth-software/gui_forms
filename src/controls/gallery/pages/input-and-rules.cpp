#include "pages.hpp"

#include "input-and-rules_form.hpp"

namespace gui_forms::gallery::pages {

namespace {

// The log keeps the newest lines only; the oldest leaves as a new one comes.
constexpr std::size_t maximum_log_lines = 200U;

std::string_view button_name(const PointerButton button) noexcept {
    switch (button) {
    case PointerButton::primary: return "primary";
    case PointerButton::secondary: return "secondary";
    case PointerButton::middle: return "middle";
    case PointerButton::none: return "no button";
    default: return "no button";
    }
}

// "(12, 30)": a position in the target's own coordinates, to whole pixels.
std::string position_text(const Point point) {
    const long x = std::lround(point.x);
    const long y = std::lround(point.y);
    std::string text = "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    return text;
}

// One mouse record as the log shows it: the event, its button, the host's
// click count, the position and the modifiers held.
std::string mouse_line(const std::string_view event, const MouseEventArgs& args) {
    std::string line(event);
    line += " · ";
    line += button_name(args.button);
    line += " ×" + std::to_string(args.clicks);
    line += " at " + position_text(args.location);
    if (has_modifier(args.modifiers, Modifier::shift)) line += " +Shift";
    if (has_modifier(args.modifiers, Modifier::control)) line += " +Ctrl";
    if (has_modifier(args.modifiers, Modifier::alt)) line += " +Alt";
    if (has_modifier(args.modifiers, Modifier::meta)) line += " +Meta";
    return line;
}

} // namespace

// Owns the input-and-rules page that demo/pages/input-and-rules.form
// describes. The target panel's WinForms mouse events and context requests
// are each logged as the window raises them (the log is the event order);
// a context request shows the page's ContextMenu, which has a submenu; and
// the page's window rules, armed through a bound check box, run before the
// target's and the button's subscribers:
//   1. a Shift+click on the button, while the consume box is checked, is
//      consumed: the button's Click subscribers and its command do not run;
//   2. H pressed while the pointer is over the target toggles `marked_`,
//      which the Marked check box shows (a built-in toggle action);
//   3. each MouseDoubleClick on the target is noted before its subscribers.
// Every handler runs on the UI thread. The rules name this page as their
// program actions' context, so the page's disposal disarms them.
class InputAndRulesPage final : public gui_forms::Component {
public:
    explicit InputAndRulesPage(const std::shared_ptr<Surface>& body);

private:
    // The document binds the Commands and the Values (FD-6).
    template <typename> friend class InputAndRulesDocument;

    void mount_document(const std::shared_ptr<Surface>& body);
    void build_context_menu();
    void subscribe_target();
    void log(std::string line);
    void refresh_statistics();
    void arm_rules();
    void disarm_rules();

    void on_mouse_down(const MouseEventArgs& args);
    void on_mouse_up(const MouseEventArgs& args);
    void on_mouse_move(const MouseEventArgs& args);
    void on_mouse_wheel(const MouseEventArgs& args);
    void on_mouse_click(const MouseEventArgs& args);
    void on_mouse_double_click(const MouseEventArgs& args);
    void on_click();
    void on_double_click();
    void on_mouse_enter();
    void on_mouse_hover();
    void on_mouse_leave();
    void on_capture_changed();
    void on_context_requested(ContextMenuRequest& request);
    void on_menu_invoked(const MenuItemInvocation& invocation);
    void on_press();
    void on_clear();
    void on_rules_armed(bool armed);
    void on_marked(bool marked);

    // The rules' program actions (rule.hpp: `void (Context&, const
    // RuleFiring&)`).
    static void rule_consumed_click(InputAndRulesPage& page, const RuleFiring& firing);
    static void rule_toggled_mark(InputAndRulesPage& page, const RuleFiring& firing);
    static void rule_saw_double_click(InputAndRulesPage& page, const RuleFiring& firing);

    Command press_{"gallery.input.press.command", "Click me"};
    Command clear_{"gallery.input.clear.command", "Clear the log"};
    Value<bool> log_moves_{false};
    Value<bool> rules_armed_{false};
    Value<bool> consume_shift_clicks_{true};
    Value<bool> marked_{false};
    // The menu's commands, shared with the rows that show them.
    std::shared_ptr<Command> copy_{std::make_shared<Command>("gallery.input.menu.copy", "Copy")};
    std::shared_ptr<Command> paste_{std::make_shared<Command>("gallery.input.menu.paste", "Paste")};
    std::shared_ptr<Command> zoom_in_{std::make_shared<Command>("gallery.input.menu.zoom-in", "Zoom in")};
    std::shared_ptr<Command> zoom_out_{std::make_shared<Command>("gallery.input.menu.zoom-out", "Zoom out")};
    std::shared_ptr<Command> zoom_reset_{
        std::make_shared<Command>("gallery.input.menu.zoom-reset", "Actual size")};
    // Declared after the Commands and Values its controls bind, so it is
    // released first.
    InputAndRulesDocument<InputAndRulesPage> document_{};
    ContextMenu context_menu_{"gallery.input.context"};
    std::shared_ptr<Panel> target_{};
    std::shared_ptr<ListBox> log_{};
    std::shared_ptr<Label> log_count_{};
    std::shared_ptr<Label> statistics_{};
    // The armed rules' tokens; empty while the rules are not armed. The
    // rules belong to the window (RuleSet); the tokens only observe them.
    RuleToken click_rule_{};
    RuleToken mark_rule_{};
    RuleToken double_click_rule_{};
    // The window whose rules were armed, observed for the statistics line.
    Window* ruled_window_{};
    std::uint64_t lines_logged_{};
    std::uint64_t presses_{};
};

InputAndRulesPage::InputAndRulesPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    target_ = document_.gallery_input_target;
    log_ = document_.gallery_input_log;
    log_count_ = document_.gallery_input_log_count;
    statistics_ = document_.gallery_input_rules_statistics;
    build_context_menu();
    subscribe_target();
    on(press_.invoked(), *this, &InputAndRulesPage::on_press);
    on(clear_.invoked(), *this, &InputAndRulesPage::on_clear);
    on(rules_armed_.changed(), *this, &InputAndRulesPage::on_rules_armed);
    on(marked_.changed(), *this, &InputAndRulesPage::on_marked);
}

void InputAndRulesPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size. The copy of the child list keeps the iteration valid while the
    // root shrinks.
    Control& root = *document_.gallery_input_and_rules_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    const std::array<std::shared_ptr<ScaledGroupBox>, 3> groups{
        document_.gallery_input_target_group, document_.gallery_input_rules_group,
        document_.gallery_input_log_group};
    for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
        (*group).set_style(page_style());
    }
}

// Copy and Paste, a separator, then Zoom, a submenu of three rows.
void InputAndRulesPage::build_context_menu() {
    std::vector<MenuItemSpec> zoom{};
    zoom.push_back(MenuItemSpec("zoom.in", MenuItemKind::command, zoom_in_));
    zoom.push_back(MenuItemSpec("zoom.out", MenuItemKind::command, zoom_out_));
    zoom.push_back(MenuItemSpec("zoom.reset", MenuItemKind::command, zoom_reset_));
    std::vector<MenuItemSpec> items{};
    items.push_back(MenuItemSpec("copy", MenuItemKind::command, copy_));
    items.push_back(MenuItemSpec("paste", MenuItemKind::command, paste_));
    items.push_back(MenuItemSpec("separator.zoom", MenuItemKind::separator));
    items.push_back(MenuItemSpec("zoom", MenuItemKind::submenu, {}, "Zoom", std::move(zoom)));
    context_menu_.set_items(std::move(items));
    on(context_menu_.item_invoked(), *this, &InputAndRulesPage::on_menu_invoked);
}

void InputAndRulesPage::subscribe_target() {
    Panel& target = *target_;
    on(target.mouse_down(), *this, &InputAndRulesPage::on_mouse_down);
    on(target.mouse_up(), *this, &InputAndRulesPage::on_mouse_up);
    on(target.mouse_move(), *this, &InputAndRulesPage::on_mouse_move);
    on(target.mouse_wheel(), *this, &InputAndRulesPage::on_mouse_wheel);
    on(target.click(), *this, &InputAndRulesPage::on_click);
    on(target.mouse_click(), *this, &InputAndRulesPage::on_mouse_click);
    on(target.double_click(), *this, &InputAndRulesPage::on_double_click);
    on(target.mouse_double_click(), *this, &InputAndRulesPage::on_mouse_double_click);
    on(target.mouse_enter(), *this, &InputAndRulesPage::on_mouse_enter);
    on(target.mouse_hover(), *this, &InputAndRulesPage::on_mouse_hover);
    on(target.mouse_leave(), *this, &InputAndRulesPage::on_mouse_leave);
    on(target.mouse_capture_changed(), *this, &InputAndRulesPage::on_capture_changed);
    on(target.context_menu_requested(), *this, &InputAndRulesPage::on_context_requested);
}

// Appends one line, drops the oldest past maximum_log_lines, keeps the
// newest line in view and refreshes the count and the rules' statistics.
void InputAndRulesPage::log(std::string line) {
    ++lines_logged_;
    (*log_).items.append(std::move(line));
    if ((*log_).items.read().size() > maximum_log_lines) (*log_).items.erase(0U, 1U);
    const std::size_t count = (*log_).items.read().size();
    (*log_).top_index = count - 1U;
    (*log_count_).text = std::to_string(lines_logged_) + " events logged · " + std::to_string(count) +
                           " shown";
    refresh_statistics();
}

void InputAndRulesPage::refresh_statistics() {
    if (ruled_window_ == nullptr || click_rule_.state() != RuleTokenState::armed) {
        (*statistics_).text = "Rules not armed";
        return;
    }
    const RuleSetStatistics statistics = (*ruled_window_).rules().statistics();
    (*statistics_).text = "Occurrences " + std::to_string(statistics.occurrences) + " · fired " +
                            std::to_string(statistics.fired) + " · consumed " +
                            std::to_string(statistics.consumed);
}

// The rules need the window, which the page reaches through its target
// once the Gallery is shown. Unattached, nothing is armed and the log
// says so; the box is left as the user set it (writing the Value from its
// own change would re-enter its subscribers), so clearing and checking it
// again arms the rules.
void InputAndRulesPage::arm_rules() {
    Window* const window = (*target_).attached_window();
    if (window == nullptr) {
        log("Rules not armed: the page is not in a window");
        return;
    }
    RuleSet& rules = (*window).rules();

    Rule consume_click{};
    consume_click.trigger = trigger::click(*document_.gallery_input_press);
    consume_click.conditions.push_back(condition::modifiers(Modifier::shift));
    consume_click.conditions.push_back(condition::is_true(consume_shift_clicks_));
    consume_click.actions.push_back(action(*this, &InputAndRulesPage::rule_consumed_click));
    consume_click.consume = true;
    const RuleAddResult click_added = rules.add(consume_click);

    Rule mark{};
    mark.trigger = trigger::key_pressed(PhysicalKey::h);
    mark.conditions.push_back(condition::hovering(*target_));
    mark.actions.push_back(builtin::toggle(marked_));
    mark.actions.push_back(action(*this, &InputAndRulesPage::rule_toggled_mark));
    mark.consume = true;
    const RuleAddResult mark_added = rules.add(mark);

    Rule double_click{};
    double_click.trigger = trigger::mouse_double_click(*target_);
    double_click.actions.push_back(action(*this, &InputAndRulesPage::rule_saw_double_click));
    const RuleAddResult double_click_added = rules.add(double_click);

    click_rule_ = click_added.token;
    mark_rule_ = mark_added.token;
    double_click_rule_ = double_click_added.token;
    ruled_window_ = window;
    log("Rules armed: Shift+click consumed, H over the target, double-click noted");
}

void InputAndRulesPage::disarm_rules() {
    click_rule_.revoke();
    mark_rule_.revoke();
    double_click_rule_.revoke();
    ruled_window_ = nullptr;
    log("Rules revoked");
}

void InputAndRulesPage::on_mouse_down(const MouseEventArgs& args) { log(mouse_line("MouseDown", args)); }
void InputAndRulesPage::on_mouse_up(const MouseEventArgs& args) { log(mouse_line("MouseUp", args)); }

void InputAndRulesPage::on_mouse_move(const MouseEventArgs& args) {
    if (log_moves_.get()) log(mouse_line("MouseMove", args));
}

void InputAndRulesPage::on_mouse_wheel(const MouseEventArgs& args) {
    if (!log_moves_.get()) return;
    std::string line = mouse_line("MouseWheel", args);
    line += " · delta " + position_text(args.wheel_delta);
    log(std::move(line));
}

void InputAndRulesPage::on_mouse_click(const MouseEventArgs& args) { log(mouse_line("MouseClick", args)); }

void InputAndRulesPage::on_mouse_double_click(const MouseEventArgs& args) {
    log(mouse_line("MouseDoubleClick", args));
}

void InputAndRulesPage::on_click() { log("Click"); }
void InputAndRulesPage::on_double_click() { log("DoubleClick"); }
void InputAndRulesPage::on_mouse_enter() { log("MouseEnter"); }
void InputAndRulesPage::on_mouse_hover() { log("MouseHover"); }
void InputAndRulesPage::on_mouse_leave() { log("MouseLeave"); }
void InputAndRulesPage::on_capture_changed() { log("MouseCaptureChanged"); }

// Shows the page's menu where the request asks: at the pointer for a
// pointer request, at the target's top left for a keyboard one (the
// request's own window position). The request is handled, so the target's
// ancestors are not offered it.
void InputAndRulesPage::on_context_requested(ContextMenuRequest& request) {
    const bool keyboard = request.origin == ContextRequestOrigin::keyboard;
    std::string line = keyboard ? "ContextMenuRequested · keyboard" : "ContextMenuRequested · pointer";
    line += " at " + position_text(request.location);
    log(std::move(line));
    const MenuOpenMode mode = keyboard ? MenuOpenMode::keyboard : MenuOpenMode::pointer;
    context_menu_.show(target_, request.window_position, mode);
    request.handled = true;
}

void InputAndRulesPage::on_menu_invoked(const MenuItemInvocation& invocation) {
    log("Menu row " + invocation.item_id + " invoked");
}

void InputAndRulesPage::on_press() {
    ++presses_;
    log("The button's command ran (" + std::to_string(presses_) + ")");
}

void InputAndRulesPage::on_clear() {
    (*log_).items.clear();
    lines_logged_ = 0U;
    (*log_count_).text = "No events yet";
    refresh_statistics();
}

void InputAndRulesPage::on_rules_armed(const bool armed) {
    const bool already = click_rule_.state() == RuleTokenState::armed;
    if (armed && !already) arm_rules();
    else if (!armed && already) disarm_rules();
}

void InputAndRulesPage::on_marked(const bool marked) {
    log(marked ? "Marked is now checked" : "Marked is now cleared");
}

void InputAndRulesPage::rule_consumed_click(InputAndRulesPage& page, const RuleFiring& firing) {
    static_cast<void>(firing);
    page.log("Rule 1 consumed a Shift+click: the button's command did not run");
}

void InputAndRulesPage::rule_toggled_mark(InputAndRulesPage& page, const RuleFiring& firing) {
    static_cast<void>(firing);
    page.log("Rule 2: H over the target toggled Marked");
}

void InputAndRulesPage::rule_saw_double_click(InputAndRulesPage& page, const RuleFiring& firing) {
    std::string line = "Rule 3 saw MouseDoubleClick before the subscribers";
    if (firing.mouse != nullptr) line += " ×" + std::to_string((*firing.mouse).clicks);
    page.log(std::move(line));
}

void add_input_and_rules(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<InputAndRulesPage>(page));
}

} // namespace gui_forms::gallery::pages
