#pragma once

// Every live Command of the process, so the automation core can offer a
// program's commands to a model as tools (notes:planning/studies/
// MCP_AUTOMATION_STUDY.md, section 7.2: "Programs' commands are also listed
// as tools automatically"). A command notes itself when it is made with an
// identity and forgets itself when it is destroyed; nothing else owns or
// keeps it.
//
// Thread: commands belong to their UI thread, but a program may make them
// on several, so the list is guarded by one mutex. fill_live_commands hands
// out borrowed pointers for the current call on the UI thread that owns the
// commands: a command destroyed later is not reached through them, since
// the caller reads them before any callback runs.

#include <cstdint>
#include <vector>

namespace gui_forms {
class Command;
}

namespace gui_forms::detail {

void note_command(Command& command);
void forget_command(const Command& command) noexcept;
// Replaces `commands` with the live commands, oldest first.
void fill_live_commands(std::vector<Command*>& commands);
// Changes whenever a command is noted or forgotten.
[[nodiscard]] std::uint64_t command_directory_generation() noexcept;

} // namespace gui_forms::detail
