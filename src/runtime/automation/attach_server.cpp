// The attach server of the automation run (automation_run_internal.hpp;
// MCP 1.0.5, the owner's attach design, revision 3): the attach socket's
// own thread. One connection at a time:
// 1. the endpoint hands over a connection from the same user (another
//    user's never gets this far: attach_endpoint.hpp);
// 2. its first line, peeked and left in the stream, says what it is: a JSON
//    object is MCP (newline-delimited JSON-RPC, MCP's stdio framing);
//    anything else is the plain-text console, and the line its first
//    command;
// 3. the session is resumed (initialize's params.resume, or the console's
//    `resume <token>`) when it is the one waiting with that token, or
//    begins anew: a waiting session ends, the run admits the client or not
//    (AutomationRun::admit_connection: refused while another client is
//    connected; accepted under --mcp, --mcp-shared or an appearance
//    review; otherwise the user is asked in the program, every
//    connection), and a new token is made. A refused MCP connection gets a
//    JSON-RPC error on its initialize's id; a refused console a line;
// 4. MCP or the console runs until the connection ends. A console's `quit`
//    ends the session (control returns to the user, the program stays
//    open); any other end leaves it waiting attach_resume_milliseconds for
//    a resume, after which it ends.
// The console record (ConsoleLog) is the one transcript: the overlay shows
// it, a console's `transcript` answers it, and every session's lines carry
// its label (the client's name and the token's first digits).
// Captures are the engine's own, as for every MCP client.

#include "automation_run_internal.hpp"

#include <stxlib/clocks.hpp>
#include <stxlib/threads.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace gui_forms::automation {

namespace {

// How long a connection may take to send its first line.
constexpr std::int64_t opening_wait_milliseconds = 60000;
// JSON-RPC's code for a refused initialize: the implementation-defined
// server error range.
constexpr int refused_code = -32001;
constexpr std::string_view console_prompt = "gui-forms> ";
// The longest client name a session's label shows.
constexpr std::size_t label_name_bytes = 24U;

// A call or a question stops waiting when the server stops.
class FlagStop final : public CallStop {
public:
    explicit FlagStop(const std::atomic<bool>& flag) noexcept : flag_(flag) {}
    [[nodiscard]] bool stopped() const noexcept override { return flag_.load(); }

private:
    const std::atomic<bool>& flag_;
};

void attach_thread_main(const stxlib::StopSignal& signal, void* argument) noexcept;

// A session's label in the console record: the client's name (or
// "console") and the token's first four digits.
std::string session_label(const AttachClient& client, const std::string& token) {
    std::string name = client.console ? std::string("console") : client.name;
    if (name.empty()) name = "client";
    std::string label = console_clip(name, label_name_bytes);
    if (!token.empty()) label += " " + token.substr(0U, 4U);
    return label;
}

// A tool's reply as a console shows it: the text MCP's result carries, a
// line for each image (images come through MCP), and a failure's code,
// message, hint and detail.
std::string console_reply(const ToolReply& reply) {
    if (const ToolSuccess* const success = reply.success_value()) {
        std::string text = (*success).text;
        for (const ToolImage& image : (*success).images) {
            if (!text.empty()) text += '\n';
            text += "[image: " + image.mime_type + ", " + std::to_string(image.bytes.size()) +
                    " bytes; images come through MCP, or capture with points=[[x,y],...] for pixels]";
        }
        return text;
    }
    const ToolFailure& failure = *reply.failure_value();
    std::string text = "error: " + std::string(tool_error_name(failure.error)) + ": " + failure.message;
    if (!failure.hint.empty()) text += "\nhint: " + failure.hint;
    if (!failure.detail_json.empty()) text += "\n" + failure.detail_json;
    return text;
}

} // namespace

struct AttachServer::State final {
    AutomationRun* run{};
    std::unique_ptr<AttachEndpoint> endpoint{};
    std::atomic<bool> stop{false};
    stxlib::Thread thread{};
    bool joinable{};
    stxlib::Mutex mutex{};
    // Under mutex: the transport serving now, once serve returned; the
    // destructor asks it to stop.
    McpTransport* live{};
    // Under mutex: the connection's input handle while it is read here (its
    // first line, a refusal, the console) or until its transport is live
    // (-1 for none); the destructor aborts it.
    std::int64_t connection{-1};

    // The attach thread only: the session, whether its connection is gone
    // (waiting for a resume until `resume_until`), its token and label.
    bool session{};
    bool waiting{};
    stxlib::IntervalTime resume_until{};
    std::string token{};
    std::string label{};
};

namespace {

// Keeps the connection's handle in the state while it lives here, and
// clears it however the connection ends.
class ConnectionMark final {
public:
    ConnectionMark(AttachServer::State& state, std::int64_t handle) noexcept : state_(&state) {
        const stxlib::ScopedLock lock(state.mutex);
        state.connection = handle;
    }
    ~ConnectionMark() {
        const stxlib::ScopedLock lock((*state_).mutex);
        (*state_).connection = -1;
    }
    ConnectionMark(const ConnectionMark&) = delete;
    ConnectionMark& operator=(const ConnectionMark&) = delete;

private:
    AttachServer::State* state_{};
};

// Ends the session (no resume).
void end_session(AttachServer::State& state) {
    if (!state.session) return;
    state.session = false;
    state.waiting = false;
    state.token.clear();
    state.label.clear();
    (*state.run).end_attached_session();
}

// The session's connection ended without `quit`: it waits for a resume.
void leave_session(AttachServer::State& state) {
    (*state.run).detach_transport();
    if (!state.session) return;
    if (state.stop.load()) return;
    state.waiting = true;
    state.resume_until =
        stxlib::IntervalTime::now().after(stxlib::Duration::milliseconds(attach_resume_milliseconds));
    (*state.run).console_note("disconnected; the session can be resumed for " +
                              std::to_string(attach_resume_milliseconds / 1000) + " seconds");
}

// Resumes the waiting session or begins a new one for `client`. False,
// with `refusal` set, when neither may happen.
bool take_session(AttachServer::State& state, const AttachClient& client, const std::string& resume,
                  std::string& refusal) {
    AutomationRun& run = *state.run;
    if (!resume.empty()) {
        if (state.session && state.waiting && resume == state.token) {
            state.waiting = false;
            run.console_note("session resumed");
            return true;
        }
        refusal = "no session with that token is waiting to resume (it ended, or its " +
                  std::to_string(attach_resume_milliseconds / 1000) +
                  " seconds passed); connect without a token to begin a new one";
        return false;
    }
    // Another client begins: the waiting session ends.
    end_session(state);
    const FlagStop stop(state.stop);
    refusal = run.admit_connection(client, &stop);
    if (!refusal.empty()) return false;
    const std::string token = make_session_token();
    if (token.empty()) {
        refusal = "the program could not make a session token (no entropy from the host)";
        return false;
    }
    const std::string label = session_label(client, token);
    if (!run.begin_attached_session(client, label)) {
        refusal = "the program has no window";
        return false;
    }
    state.session = true;
    state.waiting = false;
    state.token = token;
    state.label = label;
    return true;
}

// Serves an MCP connection whose first line `opening` read.
void serve_mcp(AttachServer::State& state, AttachStreams streams, const AttachOpening& opening) {
    AutomationRun& run = *state.run;
    std::unique_ptr<ConnectionMark> mark =
        std::make_unique<ConnectionMark>(state, static_cast<std::int64_t>(streams.input.native_handle()));
    AttachClient client{};
    client.name = opening.client_name;
    client.version = opening.client_version;
    std::string refusal{};
    if (!take_session(state, client, opening.resume, refusal)) {
        // The initialize is taken from the stream and answered with the
        // reason, so the client says why it could not connect.
        std::string taken{};
        static_cast<void>(read_attach_line(streams.input, taken, maximum_opening_line));
        static_cast<void>(
            write_attach_line(streams.output, json_rpc_error_line(opening.id_json, refused_code, refusal)));
        run.console_note("socket connection refused: " + refusal);
        return;
    }
    std::unique_ptr<McpTransport> transport = McpTransport::over(run, std::move(streams));
    if (!transport) {
        std::fputs("GUI.Forms: attach: this build has no MCP transport\n", stderr);
        leave_session(state);
        return;
    }
    std::string error{};
    if (!run.serve_attached(*transport, state.token, error)) {
        std::fprintf(stderr, "GUI.Forms: attach: MCP did not start: %s\n", error.c_str());
        leave_session(state);
        return;
    }
    {
        const stxlib::ScopedLock lock(state.mutex);
        state.live = transport.get();
    }
    mark.reset();
    // A stop asked before the transport was published is honoured here.
    if (state.stop.load()) (*transport).request_stop();
    (*transport).wait();
    {
        const stxlib::ScopedLock lock(state.mutex);
        state.live = nullptr;
    }
    leave_session(state);
}

// Writes `text`, a line break when it lacks one, and the prompt.
bool console_answer(AttachStreams& streams, std::string text) {
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += console_prompt;
    return write_attach_line(streams.output, text);
}

// Serves a console connection whose first line was peeked.
void serve_console(AttachServer::State& state, AttachStreams streams) {
    AutomationRun& run = *state.run;
    const ConnectionMark mark(state, static_cast<std::int64_t>(streams.input.native_handle()));
    std::string line{};
    if (!read_attach_line(streams.input, line)) return;
    ConsoleCommand command = parse_console_command(line);
    AttachClient client{};
    client.console = true;
    const std::string resume = command.verb == ConsoleVerb::resume ? command.token : std::string{};
    std::string refusal{};
    if (!take_session(state, client, resume, refusal)) {
        static_cast<void>(write_attach_line(streams.output, "refused: " + refusal + "\n"));
        run.console_note("socket connection refused: " + refusal);
        return;
    }
    const FlagStop stop(state.stop);
    // The first line was a resume (answered here), help (the opening is
    // the help), or the session's first command (run below, after the
    // help).
    bool pending = command.verb != ConsoleVerb::resume && command.verb != ConsoleVerb::none &&
                   command.verb != ConsoleVerb::help;
    const std::string opening = command.verb == ConsoleVerb::resume
                                    ? "resumed session " + state.token
                                    : run.console_help(state.token);
    if (!console_answer(streams, opening)) {
        leave_session(state);
        return;
    }
    bool quit = false;
    for (;;) {
        if (state.stop.load()) break;
        if (!pending) {
            if (!read_attach_line(streams.input, line)) break;
            command = parse_console_command(line);
        }
        pending = false;
        std::string answer{};
        switch (command.verb) {
        case ConsoleVerb::none:
            break;
        case ConsoleVerb::help:
            answer = run.console_help(state.token);
            break;
        case ConsoleVerb::tools:
            answer = run.console_tools();
            break;
        case ConsoleVerb::call:
            answer = console_reply(run.drive(command.tool, command.arguments_json, &stop, line));
            break;
        case ConsoleVerb::transcript:
            answer = run.console_log().transcript_text();
            break;
        case ConsoleVerb::resume:
            answer = "this connection is already in session " + state.token;
            break;
        case ConsoleVerb::quit:
            quit = true;
            break;
        case ConsoleVerb::unknown:
            answer = "error: " + command.error;
            break;
        }
        if (quit) {
            static_cast<void>(write_attach_line(streams.output, "session ended\n"));
            break;
        }
        if (!console_answer(streams, answer)) break;
    }
    if (quit) {
        end_session(state);
    } else {
        leave_session(state);
    }
}

// Serves one connection to its end.
void serve_connection(AttachServer::State& state, AttachStreams streams) {
    std::string line{};
    {
        const ConnectionMark mark(state, static_cast<std::int64_t>(streams.input.native_handle()));
        if (!peek_attach_line(streams, line, opening_wait_milliseconds)) return;
    }
    const AttachOpening opening = read_attach_opening(line);
    if (opening.json) {
        serve_mcp(state, std::move(streams), opening);
    } else {
        serve_console(state, std::move(streams));
    }
}

void attach_thread_main(const stxlib::StopSignal&, void* argument) noexcept {
    AttachServer::State& state = *static_cast<AttachServer::State*>(argument);
    for (;;) {
        if (state.stop.load()) return;
        AttachStreams streams{};
        std::string note{};
        AttachWait waited = AttachWait::failed;
        std::int64_t limit = -1;
        if (state.session && state.waiting) {
            const stxlib::IntervalTime now = stxlib::IntervalTime::now();
            limit = state.resume_until <= now ? 0 : state.resume_until.since(now).count_milliseconds();
        }
        try {
            waited = (*state.endpoint).accept(streams, note, limit);
        } catch (...) {
            note = "the attach endpoint failed";
        }
        try {
            switch (waited) {
            case AttachWait::interrupted:
                return;
            case AttachWait::failed:
                std::fprintf(stderr, "GUI.Forms: attach: %s\n", note.c_str());
                return;
            case AttachWait::timed_out:
                (*state.run).console_note("the session's resume time passed");
                end_session(state);
                continue;
            case AttachWait::refused:
                std::fprintf(stderr, "GUI.Forms: attach: %s\n", note.c_str());
                (*state.run).console_note(note);
                continue;
            case AttachWait::connected:
                serve_connection(state, std::move(streams));
                continue;
            }
        } catch (const std::exception& failure) {
            std::fprintf(stderr, "GUI.Forms: attach: the connection failed: %s\n", failure.what());
            if (state.stop.load()) return;
            leave_session(state);
            continue;
        } catch (...) {
            if (state.stop.load()) return;
            leave_session(state);
            continue;
        default: // A value outside AttachWait: the endpoint is in no state to go on.
            return;
        }
    }
}

} // namespace

AttachServer::AttachServer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

std::unique_ptr<AttachServer> AttachServer::start(AutomationRun& run, std::string& error) {
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).run = &run;
    (*state).endpoint = AttachEndpoint::listen(AutomationRun::attach_program(), error);
    if (!(*state).endpoint) return nullptr;
    // Where the socket is, for a person or an agent reading the program's
    // output (talk-to-a-program.md).
    std::fprintf(stderr, "GUI.Forms: MCP socket: %s\n", (*(*state).endpoint).address().c_str());
    State* const started_state = state.get();
    stxlib::OperationResult<stxlib::Thread> started = stxlib::Thread::start(&attach_thread_main, started_state);
    if (started.failed()) {
        error = "the attach thread did not start";
        return nullptr;
    }
    (*state).thread = std::move(*started.value());
    (*state).joinable = true;
    return std::unique_ptr<AttachServer>(new AttachServer(std::move(state)));
}

void AttachServer::publish(std::size_t tools, std::string_view note) {
    State& state = *state_;
    const stxlib::ScopedLock lock(state.mutex);
    (*state.endpoint).publish(tools, note);
}

AttachServer::~AttachServer() {
    if (!state_) return;
    State& state = *state_;
    state.stop.store(true);
    (*state.endpoint).interrupt();
    {
        const stxlib::ScopedLock lock(state.mutex);
        if (state.connection != -1) abort_attach_connection(state.connection);
        if (state.live != nullptr) (*state.live).request_stop();
    }
    if (state.joinable) static_cast<void>(state.thread.join());
    state.joinable = false;
}

} // namespace gui_forms::automation
