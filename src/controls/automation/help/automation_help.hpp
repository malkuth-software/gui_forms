#pragma once

// The help a program's socket console and its MCP instructions give (owner,
// MCP round 4), generated from one source and never written for MCP alone:
// - `help tools` and `help <tool>` come from the tools' own documentation,
//   the descriptions and argument schemas the program and GUI.Forms already
//   declare (register_model_tool, the commands' text and description, the
//   built-in tools), so an agent and a person read the same text;
// - `help mcp`, `help mcp.console`, `help mcp.connections`,
//   `help mcp.control` and the other topics come from GUI.Forms' own documentation (docs/user/
//   talk-to-a-program.md), compiled into the library at build time;
// - `help gui.forms` lists the user guide's pages (docs/user/index.md) with
//   their published addresses.
// A topic it does not know is answered with the nearest one it does. A tool
// with no description says so plainly (undocumented_tools names them).

#include "../tools/automation_tools.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation {

// What the help says about the program.
struct HelpContext final {
    // The program's own note for a model (gui://app/note); may be empty.
    std::string note{};
    // Who may command the program and how control works (access_summary
    // and the launch's control); may be empty.
    std::string access{};
    // A plain-text console asks (the commands are listed); otherwise MCP.
    bool console{};
};

// Who may command the program, said before and on connecting (the owner's
// UCI model, MCP round 5): the user's own account, nothing more.
inline constexpr std::string_view access_summary =
    "This program takes commands from your own user account only (its socket is private to you); nothing asks "
    "per connection or command, and every line acts on the program as the last one did, on any connection. "
    "Control: the first act takes it; it holds until release_control or the user taking it back (and, in a "
    "shared launch, 30 s without a call).";

// The line the MCP instructions give for help: the socket console's `help`,
// or --mcp-help without connecting.
inline constexpr std::string_view mcp_help_pointer =
    "Help, a dotted tree as `uci show` gives one: run the program with --mcp-help [path] (no socket), or send "
    "`help [path]` as a plain-text line on its socket.";

// The MCP instructions: `base` (what every program says), the access line
// and the help pointer, each on its own line, then the program's note.
[[nodiscard]] std::string session_instructions(std::string_view base, const HelpContext& context);

// The help for a dotted `path` (trimmed; empty is the top), with the tools
// the program serves now (the owner's UCI model, MCP round 5):
// - the top lists the groups, the tools in none, and the topics;
// - a group (`plan_paint`, `command`: the shortest name prefix two or more
//   tools share, as long as all its tools share it) lists its tools as
//   `plan_paint.draw`;
// - `plan_paint.draw` (or the tool's own name, plan_paint_draw) is one
//   tool: its description, arguments and an example;
// - `mcp` is docs/user/talk-to-a-program.md, `mcp.control` and the like its
//   sections (also bare: `control`); `gui.forms` the user guide's pages;
//   `tools` every tool.
// A path it does not know is answered with the nearest one it does.
[[nodiscard]] std::string help_text(std::string_view topic, const std::vector<ToolSpec>& tools,
                                    const HelpContext& context);

// The topics the embedded documentation answers, in order ("mcp" first).
// The whole tree as one text, for the command folder's `help` file: the
// top, then each group and each tool under its `help <path>` line.
[[nodiscard]] std::string help_tree_text(const std::vector<ToolSpec>& tools, const HelpContext& context);
// The tool a console word names: its name, its dotted path (both in any
// case), or `release` for release_control; empty when none.
[[nodiscard]] std::string tool_for_path(std::string_view path, const std::vector<ToolSpec>& tools);
// A dotted path to show as help's example: the first tool of the first
// group other than the built-in commands (`plan_paint.draw`), else of any
// group; empty when no tool is in a group.
[[nodiscard]] std::string help_example_path(const std::vector<ToolSpec>& tools);
[[nodiscard]] std::vector<std::string> help_topics();

// The tools whose description is empty: what `help <tool>` cannot explain.
[[nodiscard]] std::vector<std::string> undocumented_tools(const std::vector<ToolSpec>& tools);

// The edit distance between two names (insertions, deletions and
// substitutions of bytes), for the nearest topic.
[[nodiscard]] std::size_t help_distance(std::string_view left, std::string_view right);

} // namespace gui_forms::automation
