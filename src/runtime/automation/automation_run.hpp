#pragma once

// The automation's run (notes:planning/studies/MCP_AUTOMATION_STUDY.md,
// sections 4 and 13; stage G2): what Application::run starts when the
// program was launched with --mcp (or GUI_FORMS_MCP=1) or --mcp-script, on
// every desktop host alike.
//
// - The launch is read from the process's own arguments and environment
//   (read_automation_launch), so a program needs no code for MCP; a program
//   that parses argv removes the automation's arguments first
//   (automation::take_mcp_arguments).
// - The run owns the presenter, the control and the core, all used on the
//   UI thread of the application's windows (the main thread). Every
//   application window joins the core as it becomes ready (the window-ready
//   hook, session_directory.hpp), under its stable ID in the run.
// - With --mcp, standard input and output carry MCP: stx_mcp claims them
//   (descriptor 1 then points at standard error) and serves on its own
//   tasks. Each tool call is driven from its handler task: every step runs
//   on the UI thread through a window's dispatcher (WindowDispatcher::invoke,
//   so the handler never touches a window), and the task sleeps between
//   steps on the request's context, which ends the call when the client
//   cancels it. The input's end is the portable shutdown: every window is
//   asked to close (study 4.1).
// - With --mcp-script <file>, the run reads the file's lines (each a tool
//   call: {"tool": name, ...arguments}, or an expectation
//   {"expect": "node"|"visual", ...}), runs them as one batch's steps after
//   the first window is ready, prints each reply to standard output, and
//   asks every window to close (the exit code stays the program's).
//
// The transport is compiled when the build has stx_mcp
// (GUI_FORMS_MCP_TRANSPORT, set by CMake when the stxlib::mcp target
// exists); without it --mcp says so on standard error and runs the program
// without MCP. The whole run is absent when GUI_FORMS_MCP is off.
//
// Thread: constructed and destroyed on the thread that runs
// Application::run; the transport's tasks reach it only through the members
// documented as any-thread.

#include <memory>
#include <string>

namespace gui_forms::automation {

struct AutomationLaunch final {
    // --mcp or GUI_FORMS_MCP=1.
    bool mcp{};
    // --model-directed: direct_begin is allowed (study 8.3).
    bool model_directed{};
    // --mcp-script <file>; empty for none.
    std::string script_path{};
    [[nodiscard]] bool requested() const noexcept { return mcp || !script_path.empty(); }
};

// The launch as the process was started. Empty in a build without
// GUI_FORMS_MCP, and in a high-integrity build (which reads no environment
// variable and serves no automation).
[[nodiscard]] AutomationLaunch read_automation_launch();

// The program's MCP identity: the program's own (set_mcp_identity), else
// app.<vendor>.<product> from the program identity, lowercased and kept to
// letters and digits, else "app.guiforms.program".
[[nodiscard]] std::string automation_identity();

class AutomationRun;

// Application::run's scope: starts the run when the launch asks for one and
// ends it when the run's windowing has ended. Not copyable.
class AutomationRunScope final {
public:
    AutomationRunScope();
    ~AutomationRunScope();
    AutomationRunScope(const AutomationRunScope&) = delete;
    AutomationRunScope& operator=(const AutomationRunScope&) = delete;

private:
    std::unique_ptr<AutomationRun> run_{};
};

} // namespace gui_forms::automation
