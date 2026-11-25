#pragma once

// The automation run's class, shared by its parts: the run itself
// (automation_run.cpp), the MCP transport (mcp_transport.cpp) and the
// script runner (automation_script.cpp). See automation_run.hpp.

#include "automation_run.hpp"

#include "../../controls/automation/control/automation_control.hpp"
#include "../../controls/automation/core/automation_core.hpp"
#include "../../controls/automation/presenter/stock_presenter.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/dispatcher/window_dispatcher/window_dispatcher.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <stxlib/threads.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation {

// What a driven call waits on between steps: whether to stop early (the
// MCP request's context ended). Null: never stop early (a script).
class CallStop {
public:
    virtual ~CallStop();
    [[nodiscard]] virtual bool stopped() const noexcept = 0;
};

class McpTransport;
class ScriptRunner;

class AutomationRun final {
public:
    explicit AutomationRun(AutomationLaunch launch);
    ~AutomationRun();
    AutomationRun(const AutomationRun&) = delete;
    AutomationRun& operator=(const AutomationRun&) = delete;

    // Installs the window-ready hook and starts the transport or the script.
    // Main thread, before the run's first window is ready.
    void start();

    // The window-ready hook (session_directory.hpp). Main thread.
    static void window_ready(void* context, SurfaceRoot& window, std::string_view stable_id,
                             const ApplicationWindowHandle& handle) noexcept;

    // ---- any thread ----

    // Runs one tool call to its reply: each step on the UI thread, sleeping
    // between steps until the step's resume time or `stop`. A call stopped
    // early, or one whose windows are all gone, answers cancelled.
    [[nodiscard]] ToolReply drive(std::string_view tool, std::string arguments_json, const CallStop* stop);
    // Records the client's identity, at the UI thread's next turn.
    void post_client(const ClientIdentity& client);
    // Asks every window to close, at the UI thread's next turn (the
    // portable shutdown: the client closed MCP's input, or a script ended).
    void post_close_all();
    // Waits until the first window joined (a script waits for it), or the run
    // ended; false then.
    [[nodiscard]] bool wait_for_window(const CallStop* stop) const;

    // ---- the UI thread ----

    [[nodiscard]] AutomationCore& core() noexcept { return core_; }
    // Binds the tools again when their list changed (the registry or the
    // program's commands).
    // Starts serving MCP, once, with the tools, commands and note known now
    // (the first window's join). UI thread.
    void serve_when_ready();
    void sync_tools();
    void close_all() noexcept;

    [[nodiscard]] const AutomationLaunch& launch() const noexcept { return launch_; }

private:
    // A live window's dispatcher, the oldest first; empty when none is.
    [[nodiscard]] WindowDispatcher home_dispatcher() const;

    AutomationLaunch launch_;
    StockPresenter presenter_{};
    AutomationControl control_;
    AutomationCore core_;

    mutable stxlib::Mutex mutex_{};
    std::vector<WindowDispatcher> dispatchers_{};
    // Under mutex_: the portable shutdown was asked for. A window that joins
    // afterwards closes as it joins (a client that closes the input before
    // the first window is ready would otherwise leave the program running).
    bool closing_{false};
    // Under mutex_: the client last seen, kept for a window that joins
    // later (MCP round 1: a client names itself in initialize, usually
    // before the first window is ready, and the sign never showed).
    bool client_seen_{false};
    ClientIdentity client_{};
    std::atomic<bool> ended_{false};
    // Calls being driven: the run's end waits a moment for them, so a reply
    // to the call that closed the last window (disconnect) still reaches the
    // client before the transport stops.
    std::atomic<int> driving_{0};
    std::uint64_t tool_generation_{};

    std::unique_ptr<McpTransport> transport_{};
    std::unique_ptr<ScriptRunner> script_{};
};

// ---- the MCP transport (mcp_transport.cpp) ----

class McpTransport final {
public:
    // Claims standard input and output for MCP, so nothing the program
    // prints reaches them; null, with `error` set, when it cannot (no
    // stx_mcp in this build, or the streams cannot be claimed). Serving
    // starts with serve.
    [[nodiscard]] static std::unique_ptr<McpTransport> claim(AutomationRun& run, std::string& error);
    // Serves MCP as `identity` with `instructions`, the program's note as a
    // resource (none for empty) and `specs` bound before the first message
    // is read, so a client sees them at once and no list_changed for them.
    // Once; false, with `error` set, when the server cannot start.
    [[nodiscard]] bool serve(const std::string& identity, const std::string& instructions,
                             const std::string& app_note, const std::vector<ToolSpec>& specs, std::string& error);
    [[nodiscard]] bool serving() const noexcept;
    ~McpTransport();
    McpTransport(const McpTransport&) = delete;
    McpTransport& operator=(const McpTransport&) = delete;

    // Binds `specs` as the server's tools: unbinds the ones gone or
    // changed and binds the new or changed ones, leaving the rest (each bind
    // and unbind sends the client list_changed). Nothing before serve. Any
    // thread.
    void bind_tools(const std::vector<ToolSpec>& specs);
    // Stops serving and waits for the handler tasks.
    void stop() noexcept;

    struct State;

private:
    explicit McpTransport(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

// ---- the script runner (automation_script.cpp) ----

class ScriptRunner final {
public:
    // Reads the script and starts its thread; null, with `error` set, when
    // the file cannot be read or a line is not a JSON object.
    [[nodiscard]] static std::unique_ptr<ScriptRunner> start(AutomationRun& run, const std::string& path,
                                                              std::string& error);
    // Stops the script (its current call answers cancelled) and joins it.
    ~ScriptRunner();
    ScriptRunner(const ScriptRunner&) = delete;
    ScriptRunner& operator=(const ScriptRunner&) = delete;

    struct State;

private:
    explicit ScriptRunner(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace gui_forms::automation
