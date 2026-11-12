#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/types/command_types.hpp"
#include "gui_forms/event/delegate/delegate.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The command routing chain (owner decision DK-29). A window offers a
// routed command to command targets in this order, and the first target
// holding a handler for the command's identity (Command::stable_id) runs
// it:
//
// 1. the focused route: the focused control, then each of its ancestors up
//    to the root, for every one that carries a target
//    (Control::set_command_target);
// 2. the active panel, 3. the active document, 4. the main window: one
//    target each, named on the window (Window::set_command_target).
//
// When no target handles it, Command::execute_routed raises the command's
// own `invoked` event, as Command::execute does, so a window with nothing
// registered routes every command exactly as before the chain existed.
// Menus, the ribbon, tool strips, shortcuts, buttons bound to a command
// (ButtonBase::bind, CommandBinding), gallery commits and the notification
// area's menus route their commands (owner decision DK-47); a
// docking workspace names the active panel and document. A floating window
// names its main window's targets for the last three stages, so a document
// command reaches the active document from there.
enum class CommandRouteStage : std::uint8_t {
    focused_route,
    active_panel,
    active_document,
    main_window,
};

enum class CommandRouteOutcome : std::uint8_t {
    // No target in the chain handles the command; nothing ran.
    unhandled,
    // A target's handler ran (or, for Window::resolve_command, would run).
    handled,
    // The routing was nested deeper than maximum_command_route_depth inside
    // handlers; nothing ran.
    refused,
};

// Where a routing ended. `stage` and `target_name` describe the handling
// target, and are focused_route and empty unless the outcome is handled.
struct GUI_FORMS_API CommandRouteResult final {
    CommandRouteOutcome outcome{CommandRouteOutcome::unhandled};
    CommandRouteStage stage{CommandRouteStage::focused_route};
    std::string target_name;
};

// A handler may route another command; a routing nested deeper than this
// inside handlers is refused, which stops a handler that routes its own
// command from recursing without end.
inline constexpr std::size_t maximum_command_route_depth = 8U;

// The command handlers one place in the chain offers: a document's Save, a
// tool panel's Refresh. A target holds at most one handler per command
// identity. Handlers are named delegates: the target neither owns nor
// extends the life of their objects, so whoever owns a handler's object
// removes the handler, or the whole target, before that object dies. The
// owner of a target (usually through a shared_ptr held by a control or a
// docking workspace) decides its life; a window and its controls' focused
// route only observe it, and a destroyed target drops out of the chain.
//
// A target belongs to the UI thread of the windows that route to it and is
// not synchronized.
class GUI_FORMS_API CommandTarget final {
public:
    // `name` identifies the target in route results and diagnostics; it may
    // be empty.
    explicit CommandTarget(std::string name = {});
    CommandTarget(const CommandTarget&) = delete;
    CommandTarget& operator=(const CommandTarget&) = delete;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    // Installs the handler for the command with identity `command_id`,
    // replacing any it had. An empty identity or an unbound delegate throws
    // std::invalid_argument and changes nothing.
    void set_handler(std::string command_id, Delegate<const CommandInvocation&> handler);
    // Removes the handler for `command_id`; false when there was none. A
    // handler may remove itself, or any other, while it runs.
    bool remove_handler(std::string_view command_id);
    [[nodiscard]] bool handles(std::string_view command_id) const noexcept;
    [[nodiscard]] std::size_t handler_count() const noexcept { return handlers_.size(); }

    // Runs the handler for `invocation.command_id` with `invocation`; false,
    // running nothing, when there is none. The handler is copied before it
    // runs, so it may change this target's handlers.
    bool invoke(const CommandInvocation& invocation) const;

private:
    struct Handler final {
        std::string command_id;
        Delegate<const CommandInvocation&> run;
    };

    std::string name_;
    std::vector<Handler> handlers_;
};

} // namespace gui_forms
