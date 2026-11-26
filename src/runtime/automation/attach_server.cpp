// The attach server of the automation run (automation_run_internal.hpp;
// the owner's attach design, revision 3, reworked to the owner's UCI model
// in MCP round 5: "one connection per command is not how UCI works", and
// "its just a chained sequence"). The attach socket's own thread accepts
// every connection and gives each a thread of its own:
// 1. the endpoint hands over a connection from the same user (another
//    user's never gets this far: attach_endpoint.hpp). That is the only
//    permission: nothing asks per connection or per command;
// 2. its first line, peeked and left in the stream, says what it is: a JSON
//    object is MCP (newline-delimited JSON-RPC, MCP's stdio framing);
//    anything else is the plain-text console, and the line its first
//    command;
// 3. MCP or the console runs until the connection ends. There are no
//    sessions and no tokens: control, the console record and any pending
//    state are the program's, so a line acts on the program as the last one
//    did, on this connection or another, and one command per connection
//    costs nothing. The run serialises the calls (AutomationRun::drive), so
//    the lines of every connection form one chained sequence.
// When the last connection closes, a question scope a call left open
// closes (the program's own questions are the user's again); control stays
// as it is, returning on release_control, 30 s without a call (the shared
// model) or the user taking it back.
// The console record (ConsoleLog) is the one transcript: the overlay shows
// it, a console's `transcript` answers it, and each connection's lines
// carry its label (the client's name, else the connecting process's ID).
// Captures are the engine's own, as for every MCP client.

#include "automation_run_internal.hpp"
#include "../../controls/automation/help/automation_help.hpp"

#include <stxlib/clocks.hpp>
#include <stxlib/threads.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::automation {

namespace {

// How long a connection may wait before its first line.
constexpr std::int64_t opening_wait_milliseconds = 60000;
// Each slice of the accept, so finished connections' threads are joined.
constexpr std::int64_t accept_slice_milliseconds = 1000;
// The JSON-RPC error code of a connection that cannot be served.
constexpr int refused_code = -32001;
constexpr std::string_view console_prompt = "gui-forms> ";
// The longest client name a label shows.
constexpr std::size_t label_name_bytes = 24U;

// A call stops waiting when the server stops.
class FlagStop final : public CallStop {
public:
    explicit FlagStop(const std::atomic<bool>& flag) noexcept : flag_(flag) {}
    [[nodiscard]] bool stopped() const noexcept override { return flag_.load(); }

private:
    const std::atomic<bool>& flag_;
};

void attach_thread_main(const stxlib::StopSignal& signal, void* argument) noexcept;
void connection_thread_main(const stxlib::StopSignal& signal, void* argument) noexcept;

// A connection's label in the console record: the client's name, else
// "console" or "client" with the connecting process's ID.
std::string connection_label(const AttachClient& client, std::int64_t process) {
    if (!client.console && !client.name.empty()) return console_clip(client.name, label_name_bytes);
    std::string label = client.console ? std::string("console") : std::string("client");
    if (process > 0) label += " pid " + std::to_string(process);
    return label;
}

} // namespace

// One accepted connection and the thread that serves it.
struct AttachConnection final {
    AttachServer::State* server{};
    AttachStreams streams{};
    stxlib::Thread thread{};
    bool joinable{};
    std::atomic<bool> done{false};
    // Under the server's mutex: the input handle while it is read here
    // (-1 once MCP serves it), and the transport serving it; the server's
    // destructor aborts the one and stops the other.
    std::int64_t handle{-1};
    McpTransport* live{};
};

struct AttachServer::State final {
    AutomationRun* run{};
    std::unique_ptr<AttachEndpoint> endpoint{};
    std::atomic<bool> stop{false};
    stxlib::Thread thread{};
    bool joinable{};
    stxlib::Mutex mutex{};
    // Under mutex: every connection still being served or not yet joined.
    std::vector<std::unique_ptr<AttachConnection>> connections{};
};

namespace {

// Joins the threads of connections that ended. The attach thread only.
void join_finished(AttachServer::State& state) {
    std::vector<std::unique_ptr<AttachConnection>> finished{};
    {
        const stxlib::ScopedLock lock(state.mutex);
        for (std::size_t index = 0U; index < state.connections.size();) {
            if (!(*state.connections[index]).done.load()) {
                ++index;
                continue;
            }
            finished.push_back(std::move(state.connections[index]));
            state.connections.erase(state.connections.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    for (const std::unique_ptr<AttachConnection>& connection : finished) {
        if ((*connection).joinable) static_cast<void>((*connection).thread.join());
    }
}

// Writes `text`, a line break when it lacks one, and the prompt.
bool console_answer(AttachStreams& streams, std::string text) {
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += console_prompt;
    return write_attach_line(streams.output, text);
}

// Serves an MCP connection whose first line `opening` read.
void serve_mcp(AttachConnection& connection, const AttachOpening& opening) {
    AttachServer::State& state = *connection.server;
    AutomationRun& run = *state.run;
    AttachClient client{};
    client.name = opening.client_name;
    client.version = opening.client_version;
    const FlagStop stop(state.stop);
    std::string refusal{};
    if (!run.begin_attached(client, &stop, refusal)) {
        // The initialize is taken from the stream and answered with the
        // reason, so the client says why it could not be served.
        std::string taken{};
        static_cast<void>(read_attach_line(connection.streams.input, taken, maximum_opening_line));
        static_cast<void>(
            write_attach_line(connection.streams.output, json_rpc_error_line(opening.id_json, refused_code, refusal)));
        return;
    }
    run.console_log().set_session(connection_label(client, connection.streams.peer_process));
    std::unique_ptr<McpTransport> transport = McpTransport::over(run, std::move(connection.streams));
    if (!transport) {
        std::fputs("GUI.Forms: attach: this build has no MCP transport\n", stderr);
        run.end_attached(nullptr);
        return;
    }
    std::string error{};
    if (!run.serve_attached(*transport, error)) {
        std::fprintf(stderr, "GUI.Forms: attach: MCP did not start: %s\n", error.c_str());
        run.end_attached(nullptr);
        return;
    }
    {
        const stxlib::ScopedLock lock(state.mutex);
        connection.live = transport.get();
        connection.handle = -1;
    }
    // A stop asked before the transport was published is honoured here.
    if (state.stop.load()) (*transport).request_stop();
    (*transport).wait();
    {
        const stxlib::ScopedLock lock(state.mutex);
        connection.live = nullptr;
    }
    run.end_attached(transport.get());
}

// Serves a console connection whose first line was peeked: one command a
// line, each answered, until the connection ends or quits.
void serve_console(AttachConnection& connection) {
    AttachServer::State& state = *connection.server;
    AutomationRun& run = *state.run;
    AttachStreams& streams = connection.streams;
    std::string line{};
    if (!read_attach_line(streams.input, line)) return;
    AttachClient client{};
    client.console = true;
    const FlagStop stop(state.stop);
    std::string refusal{};
    if (!run.begin_attached(client, &stop, refusal)) {
        static_cast<void>(write_attach_line(streams.output, "refused: " + refusal + "\n"));
        return;
    }
    const std::string label = connection_label(client, streams.peer_process);
    run.console_log().set_session(label);
    bool first = true;
    for (;;) {
        if (state.stop.load()) break;
        if (!first && !read_attach_line(streams.input, line)) break;
        first = false;
        bool quit = false;
        std::size_t failed_at = 0U;
        const std::string answer = run.console_chain(line, &stop, label, quit, failed_at);
        if (quit) {
            static_cast<void>(write_attach_line(streams.output, "closed\n"));
            break;
        }
        if (!console_answer(streams, answer)) break;
    }
    run.end_attached(nullptr);
}

// Serves one connection to its end.
void serve_connection(AttachConnection& connection) {
    std::string line{};
    if (!peek_attach_line(connection.streams, line, opening_wait_milliseconds)) return;
    const AttachOpening opening = read_attach_opening(line);
    if (opening.json) {
        serve_mcp(connection, opening);
    } else {
        serve_console(connection);
    }
}

void connection_thread_main(const stxlib::StopSignal&, void* argument) noexcept {
    AttachConnection& connection = *static_cast<AttachConnection*>(argument);
    try {
        serve_connection(connection);
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "GUI.Forms: attach: the connection failed: %s\n", failure.what());
    } catch (...) {
        std::fputs("GUI.Forms: attach: the connection failed\n", stderr);
    }
    {
        const stxlib::ScopedLock lock((*connection.server).mutex);
        connection.handle = -1;
    }
    connection.done.store(true);
}

// Gives an accepted connection its own thread.
void start_connection(AttachServer::State& state, AttachStreams streams) {
    std::unique_ptr<AttachConnection> connection = std::make_unique<AttachConnection>();
    (*connection).server = &state;
    (*connection).handle = static_cast<std::int64_t>(streams.input.native_handle());
    (*connection).streams = std::move(streams);
    AttachConnection* const started = connection.get();
    {
        const stxlib::ScopedLock lock(state.mutex);
        state.connections.push_back(std::move(connection));
    }
    stxlib::OperationResult<stxlib::Thread> thread = stxlib::Thread::start(&connection_thread_main, started);
    if (thread.failed()) {
        // Nothing serves it: closed with its streams when joined.
        std::fputs("GUI.Forms: attach: a connection's thread did not start\n", stderr);
        (*started).done.store(true);
        return;
    }
    (*started).thread = std::move(*thread.value());
    (*started).joinable = true;
}

void attach_thread_main(const stxlib::StopSignal&, void* argument) noexcept {
    AttachServer::State& state = *static_cast<AttachServer::State*>(argument);
    for (;;) {
        if (state.stop.load()) return;
        join_finished(state);
        AttachStreams streams{};
        std::string note{};
        AttachWait waited = AttachWait::failed;
        try {
            waited = (*state.endpoint).accept(streams, note, accept_slice_milliseconds);
        } catch (...) {
            note = "the attach endpoint failed";
        }
        switch (waited) {
        case AttachWait::interrupted:
            return;
        case AttachWait::failed:
            std::fprintf(stderr, "GUI.Forms: attach: %s\n", note.c_str());
            return;
        case AttachWait::timed_out:
            continue;
        case AttachWait::refused:
            std::fprintf(stderr, "GUI.Forms: attach: %s\n", note.c_str());
            (*state.run).console_note(note);
            continue;
        case AttachWait::connected:
            try {
                start_connection(state, std::move(streams));
            } catch (...) {
                std::fputs("GUI.Forms: attach: a connection could not be taken\n", stderr);
            }
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
    AttachProgram program = AutomationRun::attach_program();
    program.access = run.access_line();
    program.door = run.door_id();
    (*state).endpoint = AttachEndpoint::listen(program, error);
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

std::string AttachServer::address() const {
    const std::string path = (*(*state_).endpoint).address();
    return path;
}

AttachServer::~AttachServer() {
    if (!state_) return;
    State& state = *state_;
    state.stop.store(true);
    (*state.endpoint).interrupt();
    if (state.joinable) static_cast<void>(state.thread.join());
    state.joinable = false;
    {
        const stxlib::ScopedLock lock(state.mutex);
        for (const std::unique_ptr<AttachConnection>& connection : state.connections) {
            if ((*connection).handle != -1) abort_attach_connection((*connection).handle);
            if ((*connection).live != nullptr) (*(*connection).live).request_stop();
        }
    }
    std::vector<std::unique_ptr<AttachConnection>> connections{};
    {
        const stxlib::ScopedLock lock(state.mutex);
        connections = std::move(state.connections);
        state.connections.clear();
    }
    for (const std::unique_ptr<AttachConnection>& connection : connections) {
        if ((*connection).joinable) static_cast<void>((*connection).thread.join());
    }
}

// ---- the command folder's reader -----------------------------------------------------

struct DoorServer::State final {
    AutomationRun* run{};
    CommandDoor* door{};
    std::atomic<bool> stop{false};
    stxlib::Thread thread{};
    bool joinable{};
};

namespace {

// Each slice of the reader's wait, so a stop is seen.
constexpr std::int64_t door_slice_milliseconds = 1000;

// Runs one line of the folder as if it came on a connection of its own.
void serve_door_line(DoorServer::State& state, const std::string& line) {
    AutomationRun& run = *state.run;
    CommandDoor& door = *state.door;
    const DoorLine read = read_door_line(line);
    if (read.command.find_first_not_of(" \t\r") == std::string::npos) {
        // An empty or blank line gets a reply of its own, so a reader's
        // `cat out` never waits for one that never comes.
        door.reply(door_reply_frame("error: empty command", read.request, true));
        return;
    }
    AttachClient client{};
    client.name = "pipe " + door.id();
    const FlagStop stop(state.stop);
    std::string refusal{};
    if (!run.begin_attached(client, &stop, refusal)) {
        door.reply(door_reply_frame("refused: " + refusal, read.request, true));
        return;
    }
    bool quit = false;
    std::size_t failed_at = 0U;
    std::string answer = run.console_chain(read.command, &stop, client.name, quit, failed_at);
    if (quit) answer += std::string(answer.empty() ? "" : "\n") + "the folder stays: quit closes only a socket connection";
    run.end_attached(nullptr);
    const bool chained = split_console_chain(read.command).size() > 1U;
    door.reply(door_reply_frame(answer, read.request, failed_at != 0U, chained ? failed_at : 0U));
}

void door_thread_main(const stxlib::StopSignal&, void* argument) noexcept {
    DoorServer::State& state = *static_cast<DoorServer::State*>(argument);
    for (;;) {
        if (state.stop.load()) return;
        std::string line{};
        const DoorWait waited = (*state.door).read_command(line, door_slice_milliseconds);
        if (waited == DoorWait::timed_out) continue;
        if (waited != DoorWait::line) return;
        try {
            serve_door_line(state, line);
        } catch (const std::exception& failure) {
            std::fprintf(stderr, "GUI.Forms: command folder: a line failed: %s\n", failure.what());
        } catch (...) {
            std::fputs("GUI.Forms: command folder: a line failed\n", stderr);
        }
    }
}

} // namespace

DoorServer::DoorServer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

std::unique_ptr<DoorServer> DoorServer::start(AutomationRun& run, CommandDoor& door, std::string& error) {
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).run = &run;
    (*state).door = &door;
    State* const started_state = state.get();
    stxlib::OperationResult<stxlib::Thread> started = stxlib::Thread::start(&door_thread_main, started_state);
    if (started.failed()) {
        error = "the command folder's thread did not start";
        return nullptr;
    }
    (*state).thread = std::move(*started.value());
    (*state).joinable = true;
    return std::unique_ptr<DoorServer>(new DoorServer(std::move(state)));
}

DoorServer::~DoorServer() {
    if (!state_) return;
    State& state = *state_;
    state.stop.store(true);
    (*state.door).interrupt();
    if (state.joinable) static_cast<void>(state.thread.join());
}

} // namespace gui_forms::automation
