// The command routing chain (owner decision DK-29): focused route, active
// panel, active document, main window; revocation when a target or the
// control carrying it is destroyed; bounded nesting; Ctrl+S from a focused
// tool control reaching the active document's command; and commands with
// nothing registered behaving exactly as before.

#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/commands/command_route/command_route.hpp"
#include "gui_forms/commands/types/command_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/ribbon.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/tool_strip.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
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

// Records which handler ran, in order.
struct Journal final {
    std::vector<std::string> entries;
};

// A named handler writing its name to a journal.
class JournalHandler final {
public:
    JournalHandler(Journal& journal, std::string name) : journal_(journal), name_(std::move(name)) {}

    void run(const CommandInvocation& invocation) {
        journal_.entries.push_back(name_ + ":" + invocation.command_id + ":" + invocation.source_id);
    }

private:
    Journal& journal_;
    std::string name_;
};

struct CountInvocations final {
    int* count{};
    void operator()(const CommandInvocation&) const { ++*count; }
};

Delegate<const CommandInvocation&> bind_handler(JournalHandler& handler) {
    const Delegate<const CommandInvocation&> bound =
        Delegate<const CommandInvocation&>::bind<JournalHandler, &JournalHandler::run>(handler);
    return bound;
}

// The window, a tool panel with an editor, and a document area.
struct Workspace final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Panel> tool;
    std::shared_ptr<TextBox> editor;
    std::shared_ptr<Panel> document;
};

Workspace make_workspace() {
    Workspace value;
    value.root = make_control<Panel>(StableId("root"));
    value.tool = make_control<Panel>(StableId("root.tool"));
    value.editor = make_control<TextBox>(StableId("root.tool.editor"));
    value.document = make_control<Panel>(StableId("root.document"));
    (*value.tool).set_requested_bounds({0.0, 0.0, 200.0, 300.0});
    (*value.editor).set_requested_bounds({10.0, 10.0, 180.0, 24.0});
    (*value.document).set_requested_bounds({200.0, 0.0, 400.0, 300.0});
    (*value.tool).add_child(value.editor);
    (*value.root).add_child(value.tool);
    (*value.root).add_child(value.document);
    return value;
}

void nothing_registered_behaves_as_before() {
    Workspace workspace = make_workspace();
    Window window(workspace.root, {600.0, 300.0});
    window.perform_layout();
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    int invoked = 0;
    const SubscriptionToken token = (*save).invoked().subscribe(CountInvocations{&invoked});
    require(window.resolve_command("save").outcome == CommandRouteOutcome::unhandled,
            "with nothing registered no target handles a command");
    require((*save).execute_routed(window, "menu") && invoked == 1,
            "with nothing registered a routed command raises invoked, as execute does");
    (*save).set_enabled(false);
    require(!(*save).execute_routed(window, "menu") && invoked == 1, "a disabled command does not run when routed");
    bool threw = false;
    try {
        window.set_command_target(CommandRouteStage::focused_route, std::make_shared<CommandTarget>("x"));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && !window.command_target(CommandRouteStage::active_panel),
            "the focused route has no single target to name");
    CommandTarget target("checks");
    threw = false;
    try {
        target.set_handler("", Delegate<const CommandInvocation&>{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && target.handler_count() == 0U, "a handler needs an identity and a bound delegate");
}

void chain_order_and_revocation() {
    Workspace workspace = make_workspace();
    Window window(workspace.root, {600.0, 300.0});
    window.perform_layout();
    Journal journal;
    JournalHandler editor_handler(journal, "editor");
    JournalHandler tool_handler(journal, "tool");
    JournalHandler panel_handler(journal, "panel");
    JournalHandler document_handler(journal, "document");
    JournalHandler main_handler(journal, "main");

    std::shared_ptr<CommandTarget> editor_target = std::make_shared<CommandTarget>("editor");
    std::shared_ptr<CommandTarget> tool_target = std::make_shared<CommandTarget>("tool");
    std::shared_ptr<CommandTarget> panel_target = std::make_shared<CommandTarget>("panel");
    std::shared_ptr<CommandTarget> document_target = std::make_shared<CommandTarget>("document");
    std::shared_ptr<CommandTarget> main_target = std::make_shared<CommandTarget>("main");
    (*editor_target).set_handler("refresh", bind_handler(editor_handler));
    (*tool_target).set_handler("refresh", bind_handler(tool_handler));
    (*panel_target).set_handler("refresh", bind_handler(panel_handler));
    (*document_target).set_handler("refresh", bind_handler(document_handler));
    (*main_target).set_handler("refresh", bind_handler(main_handler));
    (*workspace.editor).set_command_target(editor_target);
    (*workspace.tool).set_command_target(tool_target);
    window.set_command_target(CommandRouteStage::active_panel, panel_target);
    window.set_command_target(CommandRouteStage::active_document, document_target);
    window.set_command_target(CommandRouteStage::main_window, main_target);
    require(window.command_target(CommandRouteStage::active_document) == document_target,
            "the window names the active document's target");
    require(window.request_focus(workspace.editor), "the editor takes focus");

    const std::shared_ptr<Command> refresh = std::make_shared<Command>("refresh", "Refresh");
    int invoked = 0;
    const SubscriptionToken token = (*refresh).invoked().subscribe(CountInvocations{&invoked});

    // Each step removes the handler that ran, so the next place in the
    // chain takes the command.
    const std::vector<std::string> expected{"editor", "tool", "panel", "document", "main"};
    const std::vector<CommandRouteStage> stages{CommandRouteStage::focused_route, CommandRouteStage::focused_route,
                                                CommandRouteStage::active_panel, CommandRouteStage::active_document,
                                                CommandRouteStage::main_window};
    std::vector<std::shared_ptr<CommandTarget>> targets{editor_target, tool_target, panel_target,
                                                              document_target, main_target};
    for (std::size_t step = 0; step < expected.size(); ++step) {
        const CommandRouteResult resolved = window.resolve_command("refresh");
        require(resolved.outcome == CommandRouteOutcome::handled && resolved.stage == stages[step] &&
                    resolved.target_name == expected[step],
                "the chain resolves in the order focused route, active panel, active document, main window");
        require((*refresh).execute_routed(window, "test") && journal.entries.size() == step + 1U &&
                    journal.entries.back() == expected[step] + ":refresh:test",
                "exactly the resolved handler runs, with the invocation");
        require((*targets[step]).remove_handler("refresh"), "the handler is removed");
    }
    require(invoked == 0, "a command a target handles does not raise its own invoked event");
    require((*refresh).execute_routed(window, "test") && invoked == 1 && journal.entries.size() == 5U,
            "with every handler removed the command raises invoked again");

    // Revocation: the window only observes its targets, and a control's
    // target goes with the control.
    (*document_target).set_handler("save", bind_handler(document_handler));
    (*tool_target).set_handler("save", bind_handler(tool_handler));
    require(window.resolve_command("save").target_name == "tool", "the focused route comes first");
    static_cast<void>((*workspace.tool).remove_child((*workspace.editor).runtime_id()));
    require(window.resolve_command("save").target_name == "document",
            "focus left with the removed editor, so the tool's route no longer applies");
    targets.clear();
    document_target.reset();
    require(!window.command_target(CommandRouteStage::active_document) &&
                window.resolve_command("save").outcome == CommandRouteOutcome::unhandled,
            "a destroyed target drops out of the chain");
}

// A handler that routes its own command again, without end.
class Recurse final {
public:
    Recurse(Window& window, Command& command) : window_(window), command_(command) {}

    void run(const CommandInvocation&) {
        ++depth;
        const bool ran = command_.execute_routed(window_, "again");
        if (!ran) refused = true;
    }

    int depth{};
    bool refused{};

private:
    Window& window_;
    Command& command_;
};

// A handler that removes itself while it runs.
class RemoveSelf final {
public:
    explicit RemoveSelf(CommandTarget& target) : target_(target) {}

    void run(const CommandInvocation&) {
        ++runs;
        removed = target_.remove_handler("close");
    }

    int runs{};
    bool removed{};

private:
    CommandTarget& target_;
};

void reentrancy() {
    Workspace workspace = make_workspace();
    Window window(workspace.root, {600.0, 300.0});
    window.perform_layout();
    const std::shared_ptr<Command> again = std::make_shared<Command>("again", "Again");
    std::shared_ptr<CommandTarget> main_target = std::make_shared<CommandTarget>("main");
    Recurse recurse(window, *again);
    (*main_target).set_handler("again", Delegate<const CommandInvocation&>::bind<Recurse, &Recurse::run>(recurse));
    window.set_command_target(CommandRouteStage::main_window, main_target);
    require((*again).execute_routed(window, "test"), "the outermost routing runs its handler");
    require(recurse.depth == static_cast<int>(maximum_command_route_depth) && recurse.refused,
            "routing nested past the bound is refused, so a self-routing handler stops");

    RemoveSelf remove_self(*main_target);
    (*main_target).set_handler("close", Delegate<const CommandInvocation&>::bind<RemoveSelf, &RemoveSelf::run>(
                                            remove_self));
    const CommandInvocation close{"close", "test", 1U};
    require(window.route_command(close).outcome == CommandRouteOutcome::handled && remove_self.runs == 1 &&
                remove_self.removed && !(*main_target).handles("close"),
            "a handler may remove itself while it runs");
    require(window.route_command(close).outcome == CommandRouteOutcome::unhandled && remove_self.runs == 1,
            "a removed handler no longer runs");
}

void shortcut_from_a_tool_reaches_the_document() {
    Workspace workspace = make_workspace();
    Window window(workspace.root, {600.0, 300.0});
    window.perform_layout();
    Journal journal;
    JournalHandler document_handler(journal, "document");
    std::shared_ptr<CommandTarget> document_target = std::make_shared<CommandTarget>("document");
    (*document_target).set_handler("save", bind_handler(document_handler));
    window.set_command_target(CommandRouteStage::active_document, document_target);

    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    int invoked = 0;
    const SubscriptionToken token = (*save).invoked().subscribe(CountInvocations{&invoked});
    const AcceleratorToken shortcut = (*save).bind_shortcut(window, {PhysicalKey::s, Modifier::control});
    require(window.request_focus(workspace.editor), "the tool panel's editor takes focus");
    const KeyEvent key{.physical_key = PhysicalKey::s, .modifiers = Modifier::control};
    require(window.dispatch_key(key), "Ctrl+S is handled");
    require(journal.entries.size() == 1U && journal.entries[0] == "document:save:shortcut" && invoked == 0,
            "Ctrl+S from a focused tool control reaches the active document's command");

    // A tool strip and a ribbon route their commands the same way.
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("root.strip"));
    std::vector<ToolStripItemSpec> items;
    items.push_back({.stable_id = "strip-save", .text = "Save", .command = save});
    (*strip).set_items(std::move(items));
    (*strip).set_requested_bounds({0.0, 270.0, 600.0, 25.0});
    (*workspace.root).add_child(strip);
    window.perform_layout();
    require(window.perform_semantic_action("strip-save", SemanticAction::press) && journal.entries.size() == 2U &&
                journal.entries[1] == "document:save:root.strip" && invoked == 0,
            "a tool strip button routes its command to the active document");

    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    std::vector<RibbonTabSpec> tabs;
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec file{.stable_id = "file", .caption = "File"};
    file.commands.push_back({.stable_id = "ribbon-save", .command = save});
    home.groups.push_back(std::move(file));
    tabs.push_back(std::move(home));
    (*ribbon).set_tabs(std::move(tabs));
    (*ribbon).set_requested_bounds({0.0, 0.0, 600.0, 140.0});
    Window ribbon_window(ribbon, {600.0, 140.0});
    ribbon_window.perform_layout();
    ribbon_window.set_command_target(CommandRouteStage::active_document, document_target);
    require((*ribbon).on_semantic_child_action("ribbon.command.ribbon-save", SemanticAction::press, {}) &&
                journal.entries.size() == 3U && journal.entries[2] == "document:save:ribbon" && invoked == 0,
            "a ribbon command routes to the active document named on its window");
}

} // namespace

int main() {
    nothing_registered_behaves_as_before();
    chain_order_and_revocation();
    reentrancy();
    shortcut_from_a_tool_reaches_the_document();
    std::cout << "Command routing chain: order, revocation, nesting and Ctrl+S to the active document passed\n";
    return 0;
}
