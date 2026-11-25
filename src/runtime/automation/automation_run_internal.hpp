#pragma once

// The automation run's class, shared by its parts: the run itself
// (automation_run.cpp), the MCP transport (mcp_transport.cpp), the script
// runner (automation_script.cpp) and the attach server (attach_server.cpp).
// See automation_run.hpp.

#include "attach_endpoint.hpp"
#include "automation_run.hpp"

#include "../../controls/automation/console/console_log.hpp"
#include "../../controls/automation/control/automation_control.hpp"
#include "../../controls/automation/core/automation_core.hpp"
#include "../../controls/automation/presenter/stock_presenter.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/dispatcher/window_dispatcher/window_dispatcher.hpp"
#include "gui_forms/window.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
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
class AttachServer;

// A client of the attach socket as it named itself: self-reported, never
// verified (initialize's clientInfo; a console session has none).
struct AttachClient final {
    std::string name{};
    std::string version{};
    // A plain-text console session, not MCP.
    bool console{};
};

// How long a session whose connection dropped can be resumed (owner, attach
// revision 3: sessions survive reconnects), and its token's life.
inline constexpr std::int64_t attach_resume_milliseconds = 120000;

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
    // early, or one whose windows are all gone, answers cancelled. The
    // console record shows the call as `typed` when a console session typed
    // it, else as "call <tool> <arguments>".
    [[nodiscard]] ToolReply drive(std::string_view tool, std::string arguments_json, const CallStop* stop,
                                  std::string_view typed = std::string_view{});
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
    // Starts serving MCP, once, with the tools, commands and note known now
    // (posted by the first window's join, so it runs after that window's
    // ready callback). UI thread.
    void serve_when_ready();
    // Whether serving has started (no transport: true, nothing to start).
    [[nodiscard]] bool serving_started() const noexcept;
    // Binds the tools again when their list changed (the registry or the
    // program's commands), and rewrites the attach socket's discovery entry
    // with the count.
    void sync_tools();
    void close_all() noexcept;

    [[nodiscard]] const AutomationLaunch& launch() const noexcept { return launch_; }

    // ---- MCP 1.0.5: the console's record and attach (any thread) ----

    // The record of every call the console shows.
    [[nodiscard]] ConsoleLog& console_log() noexcept { return console_log_; }
    // A line of the run's own in the record, and the console told.
    void console_note(std::string_view note);
    // Tells the console, at the UI thread's next turn, that the record has
    // new lines.
    void post_console();
    // Whether a socket connection from `client` may begin a session: empty
    // when it may, else why not. Refused while another client is
    // connected; accepted at once under --mcp, --mcp-shared or an
    // appearance review; otherwise the user is asked in the program
    // (StockPresenter::ask_connect) and this waits for the answer, refusing
    // when `stop` stops, the run ends, or no answer comes within
    // connect_question_limit.
    [[nodiscard]] std::string admit_connection(const AttachClient& client, const CallStop* stop);
    // An admitted connection's session begins: the windows join the core if
    // they had not (a program launched without a flag joins none until its
    // first connection), the core starts a new session, and the client is
    // named as it named itself. `label` names the session in the console
    // record. False when the run has no window.
    // `token` is the session's, repeated in a disconnect's reply. A launch
    // without a flag asked its user for the connection, and that answer is
    // the session's consent to control (no take question follows).
    [[nodiscard]] bool begin_attached_session(const AttachClient& client, const std::string& label,
                                              const std::string& token);
    // Serves MCP on `transport` with the tools, commands and note known now,
    // the session's `token` in the instructions, and binds later changes to
    // it too. False, with `error` set, when the server cannot start.
    [[nodiscard]] bool serve_attached(McpTransport& transport, const std::string& token, std::string& error);
    // The session's connection ended: nothing more is bound to its
    // transport. The session itself stays for a resume
    // (attach_resume_milliseconds) or until end_attached_session.
    void detach_transport() noexcept;
    // The session's connection dropped without quit (the client stopped,
    // crashed or was killed): nothing is bound to its transport, and at
    // once the model's question scope closes and control and the sign
    // return to the user, so the program is the user's to use and close
    // (MCP round 4). The session waits for its resume, keeping the user's
    // consent.
    void attached_connection_dropped() noexcept;
    // The waiting session resumed: the sign shows again.
    void attached_connection_resumed();
    // The attached session ended (it quit, its resume time passed, another
    // client began one, or the run stops): the core's session ends and
    // control returns to the user; the program stays open, since it was not
    // launched for the model.
    void end_attached_session();
    // The help a console session answers for `topic` (automation_help.hpp):
    // empty, the overview with the commands, the session's token and the
    // program's note; `tools`, a tool's name, or a documentation topic.
    [[nodiscard]] std::string console_help(std::string_view topic, const std::string& token);
    // The banner a console session opens with: what every program says,
    // the session's token line and the help pointer.
    [[nodiscard]] std::string console_banner(const std::string& token) const;
    // The program's name for people (the product, else the identity) and
    // the executable's path, for the discovery entry.
    [[nodiscard]] static AttachProgram attach_program();
    // The stdio client named itself, or its input ended (mcp_transport.cpp).
    void stdio_client_seen() noexcept { stdio_client_seen_.store(true); }
    void stdio_input_ended() noexcept { stdio_ended_.store(true); }
    // How long a connection question waits for the user.
    static constexpr std::chrono::seconds connect_question_limit{180};

    // ---- the UI thread ----

    // A window that became ready before the windows joined (a program
    // launched without a flag, until its first connection).
    struct PendingJoin final {
        WindowRef window{};
        std::string stable_id{};
        bool passive_popup{};
        ApplicationWindowHandle handle{};
    };
    // Joins `window` to the core and the presenter.
    void join_window(Window& window, std::string_view stable_id, bool passive_popup,
                     const ApplicationWindowHandle& handle);
    // Joins every pending window still alive; later windows join as they
    // become ready.
    void join_pending();

private:
    // The tools the program serves now, read on the UI thread; `error` says
    // why none could be read.
    [[nodiscard]] std::vector<ToolSpec> current_specs(std::string& error);
    // A disconnect's reply repeats the session's token line.
    void append_session_token(ToolReply& reply);
    // drive's call itself, between its two records.
    [[nodiscard]] ToolReply drive_call(std::string_view tool, std::string arguments_json, const CallStop* stop);
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
    // Under mutex_: the attached session's token; empty without one.
    std::string session_token_{};
    std::atomic<bool> ended_{false};
    // Calls being driven: the run's end waits a moment for them, so a reply
    // to the call that closed the last window (disconnect) still reaches the
    // client before the transport stops.
    std::atomic<int> driving_{0};
    std::uint64_t tool_generation_{};
    // UI thread: the tool generation the discovery entry last counted
    // (none yet: the maximum).
    std::uint64_t published_generation_{~std::uint64_t{0}};

    std::unique_ptr<McpTransport> transport_{};
    std::unique_ptr<ScriptRunner> script_{};

    ConsoleLog console_log_{};
    // UI thread: whether ready windows join the core now (a launch that
    // asked for MCP), or wait in pending_ for the first attach connection.
    bool joins_windows_{};
    std::vector<PendingJoin> pending_{};
    std::atomic<bool> stdio_client_seen_{false};
    std::atomic<bool> stdio_ended_{false};
    std::atomic<bool> attach_live_{false};
    // Under mutex_: the attached session's transport while it serves, for
    // sync_tools.
    McpTransport* attached_transport_{};
    std::unique_ptr<AttachServer> attach_{};
};

// ---- the MCP transport (mcp_transport.cpp) ----

class McpTransport final {
public:
    // Claims standard input and output for MCP, so nothing the program
    // prints reaches them; null, with `error` set, when it cannot (no
    // stx_mcp in this build, or the streams cannot be claimed). Serving
    // starts with serve.
    [[nodiscard]] static std::unique_ptr<McpTransport> claim(AutomationRun& run, std::string& error);
    // Serves over an attach connection's streams instead (MCP 1.0.5): the
    // input's end ends this session only (the program stays open, and the
    // attach server ends the core's session), and the client it names is
    // not the stdio client.
    [[nodiscard]] static std::unique_ptr<McpTransport> over(AutomationRun& run, AttachStreams streams);
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
    // Asks serving to end and returns; wait (or stop) does the rest. Any
    // thread.
    void request_stop() noexcept;
    // Blocks until serving ends: the input ended or failed, the output
    // failed, or request_stop. Once; the thread that served calls it.
    void wait() noexcept;

    struct State;

private:
    explicit McpTransport(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

// ---- the attach server (attach_server.cpp) ----

// Listens on the attach endpoint (attach_endpoint.hpp) on its own thread and
// serves one connection at a time: its first line says MCP or the console,
// the session is resumed or admitted (AutomationRun::admit_connection),
// then MCP or the console runs over the connection until it ends, and the
// session waits attach_resume_milliseconds for a resume.
class AttachServer final {
public:
    // Null, with `error` set, when the endpoint cannot listen or the thread
    // cannot start. Says the socket's path on standard error.
    [[nodiscard]] static std::unique_ptr<AttachServer> start(AutomationRun& run, std::string& error);
    // Stops listening, ends a session being served, and joins the thread.
    ~AttachServer();
    AttachServer(const AttachServer&) = delete;
    AttachServer& operator=(const AttachServer&) = delete;
    // Rewrites the discovery entry (AttachEndpoint::publish). Any thread.
    void publish(std::size_t tools, std::string_view note);

    struct State;

private:
    explicit AttachServer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

// Whether a program launched as `launch` listens for attach connections:
// every desktop program of a build with MCP (not a high-integrity build,
// not a script run), unless GUI_FORMS_MCP_ATTACH=0.
[[nodiscard]] bool attach_wanted(const AutomationLaunch& launch);

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
