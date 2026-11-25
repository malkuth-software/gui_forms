// The attach server of the automation run (automation_run_internal.hpp;
// MCP 1.0.5, the owner's attach ruling, revision 2): the attach endpoint's
// own thread. One connection at a time:
// 1. the endpoint hands over a connection from the same user (another
//    user's never gets this far: attach_endpoint.hpp);
// 2. the bridge's hello names the client, as the client named itself;
// 3. the run admits it or not (AutomationRun::admit_connection: refused
//    while another client is connected; accepted under --mcp, --mcp-shared
//    or an appearance review; otherwise the user is asked in the program,
//    every connection, nothing remembered) and the answer line goes back;
// 4. an admitted connection's session begins (the windows join, the core
//    starts a new session), MCP is served over it until its input ends,
//    and the session ends: control returns to the user and the program
//    stays open.
// Captures are the engine's own, as for every MCP client (the owner's
// revision 2 withdrew routing them through the operating system's capture).

#include "automation_run_internal.hpp"

#include <stxlib/threads.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace gui_forms::automation {

namespace {

// A connection's question stops waiting when the server stops.
class FlagStop final : public CallStop {
public:
    explicit FlagStop(const std::atomic<bool>& flag) noexcept : flag_(flag) {}
    [[nodiscard]] bool stopped() const noexcept override { return flag_.load(); }

private:
    const std::atomic<bool>& flag_;
};

void attach_thread_main(const stxlib::StopSignal& signal, void* argument) noexcept;

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
    // Under mutex: the connection's input handle while its hello is read
    // and answered (-1 for none); the destructor aborts it.
    std::int64_t greeting{-1};
};

namespace {

// Clears the greeting handle when the greeting ends, however it ends.
class GreetingEnd final {
public:
    explicit GreetingEnd(AttachServer::State* state) noexcept : state_(state) {}
    ~GreetingEnd() { end(); }
    GreetingEnd(const GreetingEnd&) = delete;
    GreetingEnd& operator=(const GreetingEnd&) = delete;
    void end() noexcept {
        if (state_ == nullptr) return;
        const stxlib::ScopedLock lock((*state_).mutex);
        (*state_).greeting = -1;
        state_ = nullptr;
    }

private:
    AttachServer::State* state_{};
};

// Serves one connection to its end.
void serve_connection(AttachServer::State& state, AttachStreams streams) {
    AutomationRun& run = *state.run;
    {
        const stxlib::ScopedLock lock(state.mutex);
        state.greeting = static_cast<std::int64_t>(streams.input.native_handle());
    }
    // Every return below ends the greeting first.
    GreetingEnd greeting_end{&state};
    std::string line{};
    if (!read_attach_line(streams.input, line)) return;
    AttachHello hello{};
    if (!parse_attach_hello(line, hello)) {
        AttachAnswer refused{};
        refused.reason = "this is a GUI.Forms attach endpoint; connect with gui-forms-attach";
        static_cast<void>(write_attach_line(streams.output, attach_answer_line(refused)));
        return;
    }
    const FlagStop stop(state.stop);
    const AttachAnswer answer = run.admit_connection(hello, &stop);
    if (!write_attach_line(streams.output, attach_answer_line(answer)) || !answer.accepted) {
        if (!answer.accepted) run.console_note("attach connection refused: " + answer.reason);
        return;
    }
    greeting_end.end();
    std::unique_ptr<McpTransport> transport = McpTransport::over(run, std::move(streams));
    if (!transport) {
        std::fputs("GUI.Forms: attach: this build has no MCP transport\n", stderr);
        return;
    }
    if (!run.begin_attached_session(hello)) return;
    std::string error{};
    if (!run.serve_attached(*transport, error)) {
        std::fprintf(stderr, "GUI.Forms: attach: MCP did not start: %s\n", error.c_str());
        run.end_attached_session();
        return;
    }
    {
        const stxlib::ScopedLock lock(state.mutex);
        state.live = transport.get();
    }
    // A stop asked before the transport was published is honoured here.
    if (state.stop.load()) (*transport).request_stop();
    (*transport).wait();
    {
        const stxlib::ScopedLock lock(state.mutex);
        state.live = nullptr;
    }
    run.end_attached_session();
}

void attach_thread_main(const stxlib::StopSignal&, void* argument) noexcept {
    AttachServer::State& state = *static_cast<AttachServer::State*>(argument);
    for (;;) {
        if (state.stop.load()) return;
        AttachStreams streams{};
        std::string note{};
        AttachWait waited = AttachWait::failed;
        try {
            waited = (*state.endpoint).accept(streams, note);
        } catch (...) {
            note = "the attach endpoint failed";
        }
        switch (waited) {
        case AttachWait::interrupted:
            return;
        case AttachWait::failed:
            std::fprintf(stderr, "GUI.Forms: attach: %s\n", note.c_str());
            return;
        case AttachWait::refused:
            std::fprintf(stderr, "GUI.Forms: attach: %s\n", note.c_str());
            try {
                (*state.run).console_note(note);
            } catch (...) {
                // No memory for the note: the refusal stands.
            }
            continue;
        case AttachWait::connected:
            try {
                serve_connection(state, std::move(streams));
            } catch (const std::exception& failure) {
                std::fprintf(stderr, "GUI.Forms: attach: the connection failed: %s\n", failure.what());
                (*state.run).end_attached_session();
            } catch (...) {
                (*state.run).end_attached_session();
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
    (*state).endpoint = AttachEndpoint::listen(automation_identity(), error);
    if (!(*state).endpoint) return nullptr;
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

AttachServer::~AttachServer() {
    if (!state_) return;
    State& state = *state_;
    state.stop.store(true);
    (*state.endpoint).interrupt();
    {
        const stxlib::ScopedLock lock(state.mutex);
        if (state.greeting != -1) abort_attach_connection(state.greeting);
        if (state.live != nullptr) (*state.live).request_stop();
    }
    if (state.joinable) static_cast<void>(state.thread.join());
    state.joinable = false;
}

} // namespace gui_forms::automation
