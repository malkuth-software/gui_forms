#include "gui_forms/window.hpp"

#include "gui_forms/commands/command_route/command_route.hpp"

#include <array>
#include <stdexcept>

namespace gui_forms {
namespace {

// Routings in progress on this thread, across windows: a handler that
// routes again nests one deeper. Kept per thread rather than on a window so
// that nothing of a window is touched after a handler returns.
thread_local std::size_t command_route_depth = 0U;

// Holds one level of routing depth for as long as a handler runs, also when
// it throws.
class CommandRouteDepthHold final {
public:
    CommandRouteDepthHold() noexcept { ++command_route_depth; }
    ~CommandRouteDepthHold() { --command_route_depth; }
    CommandRouteDepthHold(const CommandRouteDepthHold&) = delete;
    CommandRouteDepthHold& operator=(const CommandRouteDepthHold&) = delete;
};

// The slot of a single-target stage in SurfaceRoot::command_targets_.
std::size_t command_target_slot(CommandRouteStage stage) {
    switch (stage) {
    case CommandRouteStage::active_panel: return 0U;
    case CommandRouteStage::active_document: return 1U;
    case CommandRouteStage::main_window: return 2U;
    case CommandRouteStage::focused_route:
        throw std::invalid_argument(
            "the focused route has no single command target; controls carry its targets");
    }
    throw std::invalid_argument("a command route stage outside the enumeration has no target");
}

// The first target in the chain with a handler for one command, and its
// stage; an empty target when none has one.
struct CommandRouteMatch final {
    std::shared_ptr<CommandTarget> target;
    CommandRouteStage stage{CommandRouteStage::focused_route};
};

CommandRouteMatch find_command_handler(const SurfaceRoot& window, std::string_view command_id) {
    CommandRouteMatch match{};
    if (command_id.empty()) return match;
    Control::Ptr control = window.focused_control();
    while (control) {
        const std::shared_ptr<CommandTarget>& carried = (*control).command_target();
        if (carried && (*carried).handles(command_id)) {
            match.target = carried;
            match.stage = CommandRouteStage::focused_route;
            return match;
        }
        control = (*control).parent();
    }
    const std::array<CommandRouteStage, 3> stages{CommandRouteStage::active_panel, CommandRouteStage::active_document,
                                                  CommandRouteStage::main_window};
    for (const CommandRouteStage stage : stages) {
        const std::shared_ptr<CommandTarget> named = window.command_target(stage);
        if (!named || !(*named).handles(command_id)) continue;
        match.target = named;
        match.stage = stage;
        return match;
    }
    return match;
}

} // namespace

void SurfaceRoot::set_command_target(CommandRouteStage stage, const std::shared_ptr<CommandTarget>& target) {
    require_root_thread("command target");
    const std::size_t slot = command_target_slot(stage);
    command_targets_[slot] = target;
}

std::shared_ptr<CommandTarget> SurfaceRoot::command_target(CommandRouteStage stage) const {
    const std::size_t slot = command_target_slot(stage);
    std::shared_ptr<CommandTarget> target = command_targets_[slot].lock();
    return target;
}

CommandRouteResult SurfaceRoot::resolve_command(std::string_view command_id) const {
    CommandRouteResult result{};
    const CommandRouteMatch match = find_command_handler(*this, command_id);
    if (!match.target) return result;
    result.outcome = CommandRouteOutcome::handled;
    result.stage = match.stage;
    result.target_name = (*match.target).name();
    return result;
}

CommandRouteResult SurfaceRoot::route_command(const CommandInvocation& invocation) {
    require_root_thread("command routing");
    CommandRouteResult result{};
    if (command_route_depth >= maximum_command_route_depth) {
        result.outcome = CommandRouteOutcome::refused;
        return result;
    }
    const CommandRouteMatch match = find_command_handler(*this, invocation.command_id);
    if (!match.target) return result;
    result.outcome = CommandRouteOutcome::handled;
    result.stage = match.stage;
    result.target_name = (*match.target).name();
    // `match` keeps the target alive while its handler runs; nothing of this
    // window is read after the handler returns.
    const CommandRouteDepthHold hold{};
    static_cast<void>((*match.target).invoke(invocation));
    return result;
}

} // namespace gui_forms
