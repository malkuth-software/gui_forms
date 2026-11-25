#pragma once

// The help a program's socket console and its MCP instructions give (owner,
// MCP round 4), generated from one source and never written for MCP alone:
// - `help tools` and `help <tool>` come from the tools' own documentation,
//   the descriptions and argument schemas the program and GUI.Forms already
//   declare (register_model_tool, the commands' text and description, the
//   built-in tools), so an agent and a person read the same text;
// - `help mcp`, `help console`, `help sessions`, `help control` and the other
//   topics come from GUI.Forms' own documentation (docs/user/
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

// What the help says about the session and the program.
struct HelpContext final {
    // The session's token; empty for none (a launch on standard input).
    std::string token{};
    // The program's own note for a model (gui://app/note); may be empty.
    std::string note{};
    // A plain-text console asks (the commands are listed); otherwise MCP.
    bool console{};
};

// The one line every opening carries (the console's banner, the MCP
// instructions).
inline constexpr std::string_view help_pointer = "Type `help` for commands, `help mcp` for how sessions work.";

// The session token's line: the token, how to resume with it, and why
// (the person at the computer may not be there to answer again; an agent
// that launches a program itself uses --mcp). Empty for an empty token.
[[nodiscard]] std::string session_token_line(std::string_view token);

// The MCP instructions: `base` (what every program says), the token's line
// and the help pointer near the top, each on its own line, then the
// program's note.
[[nodiscard]] std::string session_instructions(std::string_view base, const HelpContext& context);

// The help for `topic` (trimmed; empty is the overview), with the tools the
// program serves now.
[[nodiscard]] std::string help_text(std::string_view topic, const std::vector<ToolSpec>& tools,
                                    const HelpContext& context);

// The topics the embedded documentation answers, in order ("mcp" first).
[[nodiscard]] std::vector<std::string> help_topics();

// The tools whose description is empty: what `help <tool>` cannot explain.
[[nodiscard]] std::vector<std::string> undocumented_tools(const std::vector<ToolSpec>& tools);

// The edit distance between two names (insertions, deletions and
// substitutions of bytes), for the nearest topic.
[[nodiscard]] std::size_t help_distance(std::string_view left, std::string_view right);

} // namespace gui_forms::automation
