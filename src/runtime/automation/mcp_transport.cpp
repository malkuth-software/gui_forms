// The MCP transport of the automation run (automation_run.hpp): stx_mcp
// (STXlib's stxlib/mcp.hpp, track M1) serving the core's tools over
// standard input and output. Compiled with stx_mcp when CMake found the
// stxlib::mcp target (GUI_FORMS_MCP_TRANSPORT); otherwise start() answers
// that this build has no transport.

#include "automation_run_internal.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#if defined(GUI_FORMS_MCP_TRANSPORT)
#include <stxlib/json.hpp>
#include <stxlib/mcp.hpp>
#include <stxlib/pipes.hpp>
#include <stxlib/text.hpp>
#include <stxlib/threads.hpp>
#endif

namespace gui_forms::automation {

#if defined(GUI_FORMS_MCP_TRANSPORT)

namespace {

// The `_meta` key a client names its model in (study 4.4). The
// specification defines none; this one is GUI.Forms' until it does.
constexpr stxlib::TextView model_meta_key = "model";

// `text` as a view for stx_mcp; empty when it is not UTF-8 (every text
// here is the program's own or the core's, so that does not happen).
stxlib::TextView text_view(const std::string& text) noexcept {
    const stxlib::OperationResult<stxlib::TextView> checked = stxlib::validate_utf8(text.data(), text.size());
    if (checked.failed()) return stxlib::TextView{};
    return *checked.value();
}

std::string standard_text(stxlib::TextView text) {
    std::string made(text.data(), static_cast<std::size_t>(text.count()));
    return made;
}

// A call stops early when its request's context ended (cancelled, or the
// server shutting down).
class ContextStop final : public CallStop {
public:
    explicit ContextStop(const stxlib::Context& context) noexcept : context_(context) {}
    [[nodiscard]] bool stopped() const noexcept override { return context_.done(); }

private:
    const stxlib::Context& context_;
};

// What the server's events reach: the run, and whether this transport
// serves an attach connection (MCP 1.0.5) rather than the process's
// standard streams.
struct EventsContext final {
    AutomationRun* run{};
    bool attached{};
};

void client_seen(void* context, const stxlib::mcp::ClientIdentity& client) noexcept {
    const EventsContext& events = *static_cast<const EventsContext*>(context);
    try {
        ClientIdentity identity{};
        identity.client_name = standard_text(client.name());
        identity.client_version = standard_text(client.version());
        identity.model_name = standard_text(client.model());
        if (!events.attached) (*events.run).stdio_client_seen();
        (*events.run).post_client(identity);
    } catch (...) {
        // Out of memory: the tag line keeps the last name.
    }
}

void input_ended(void* context) noexcept {
    const EventsContext& events = *static_cast<const EventsContext*>(context);
    // An attach connection's end ends its session only: the attach server
    // sees the server's wait return and ends the core's session; the
    // program stays open (it was not launched for the model).
    if (events.attached) return;
    try {
        (*events.run).stdio_input_ended();
        (*events.run).post_close_all();
    } catch (...) {
        // The windows close with the process.
    }
}

// Every tool's handler: the arguments again as JSON text, the call driven
// to its reply on the UI thread, the reply in MCP's terms.
void call_tool(void* binding, const stxlib::mcp::ToolRequest& request, stxlib::mcp::ToolReply& reply) noexcept {
    AutomationRun& run = *static_cast<AutomationRun*>(binding);
    try {
        stxlib::Utf8String arguments{};
        {
            stxlib::JsonWriter writer(arguments);
            writer.copy(request.document(), request.arguments());
        }
        const ContextStop stop(request.context());
        const ToolReply answer =
            run.drive(standard_text(request.tool_name()), standard_text(arguments.view()), &stop);
        if (const ToolSuccess* const success = answer.success_value()) {
            reply.text(text_view((*success).text));
            for (const ToolImage& image : (*success).images) {
                const stxlib::View<stxlib::Byte> bytes(reinterpret_cast<const stxlib::Byte*>(image.bytes.data()),
                                                       image.bytes.size());
                reply.image(bytes, text_view(image.mime_type));
            }
            return;
        }
        const ToolFailure& failure = *answer.failure_value();
        const std::string code(tool_error_name(failure.error));
        reply.fail(text_view(code), text_view(failure.message), text_view(failure.hint));
        // The structured detail (a failed batch's steps) as its own item,
        // JSON, never escaped inside the error's text (round 2).
        if (!failure.detail_json.empty()) reply.text(text_view(failure.detail_json));
    } catch (const std::exception& failure) {
        const std::string message(failure.what());
        reply.fail("internal", text_view(message), stxlib::TextView{});
    } catch (...) {
        reply.fail("internal", "the call failed", stxlib::TextView{});
    }
}

} // namespace

// The program's note (set_model_app_note) as an MCP resource.
constexpr stxlib::TextView app_note_uri = "gui://app/note";

struct McpTransport::State final {
    AutomationRun* run{};
    EventsContext events{};
    std::string identity{};
    std::string instructions{};
    std::string app_note{};
    stxlib::ProtocolStreams streams{};
    stxlib::mcp::Server server{};
    stxlib::Mutex mutex{};
    bool serving{};
    // The specs bound now, by name; a sync binds only what changed.
    std::vector<ToolSpec> bound{};
};

namespace {

void read_app_note(void* binding, const stxlib::mcp::ResourceRequest&, stxlib::mcp::ResourceReply& reply) noexcept {
    const McpTransport::State& state = *static_cast<const McpTransport::State*>(binding);
    reply.text(app_note_uri, "text/plain", text_view(state.app_note));
}

bool same_spec(const ToolSpec& left, const ToolSpec& right) noexcept {
    const bool same = left.name == right.name && left.title == right.title && left.description == right.description &&
                      left.input_schema == right.input_schema && left.access == right.access &&
                      left.destructive == right.destructive;
    return same;
}

// Binds one spec; false (said on stderr) when the server refuses it.
bool bind_one(McpTransport::State& state, const ToolSpec& spec) {
    stxlib::mcp::ToolSpec bound{};
    bound.name = text_view(spec.name);
    bound.title = text_view(spec.title);
    bound.description = text_view(spec.description);
    bound.input_schema = text_view(spec.input_schema);
    bound.annotations.read_only = spec.access == ToolAccess::read_only;
    bound.annotations.destructive = spec.destructive;
    bound.annotations.idempotent = spec.access == ToolAccess::read_only;
    bound.annotations.open_world = false;
    const stxlib::OperationStatus status = state.server.bind_tool(bound, &call_tool, state.run);
    if (status.failed()) {
        std::fprintf(stderr, "GUI.Forms: the tool %s could not be bound\n", spec.name.c_str());
        return false;
    }
    return true;
}

} // namespace

std::unique_ptr<McpTransport> McpTransport::claim(AutomationRun& run, std::string& error) {
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).run = &run;
    stxlib::OperationResult<stxlib::ProtocolStreams> streams = stxlib::claim_protocol_streams();
    if (streams.failed()) {
        error = "standard input and output could not be claimed";
        return nullptr;
    }
    (*state).streams = std::move(*streams.value());
    return std::unique_ptr<McpTransport>(new McpTransport(std::move(state)));
}

std::unique_ptr<McpTransport> McpTransport::over(AutomationRun& run, AttachStreams streams) {
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).run = &run;
    (*state).events.attached = true;
    (*state).streams.input = std::move(streams.input);
    (*state).streams.output = std::move(streams.output);
    return std::unique_ptr<McpTransport>(new McpTransport(std::move(state)));
}

void McpTransport::request_stop() noexcept {
    // Server::stop is safe from any thread at any time; the attach server
    // calls this only once serve has returned.
    if (state_) (*state_).server.stop();
}

void McpTransport::wait() noexcept {
    if (!state_ || !serving()) return;
    static_cast<void>((*state_).server.wait());
    const stxlib::ScopedLock lock((*state_).mutex);
    (*state_).serving = false;
}

bool McpTransport::serve(const std::string& identity, const std::string& instructions, const std::string& app_note,
                         const std::vector<ToolSpec>& specs, std::string& error) {
    State& state = *state_;
    const stxlib::ScopedLock lock(state.mutex);
    if (state.serving) return true;
    state.identity = identity;
    state.instructions = instructions;
    state.app_note = app_note;
    stxlib::mcp::ServerIdentity server_identity{};
    server_identity.name = text_view(state.identity);
    server_identity.version = "1";
    server_identity.instructions = text_view(state.instructions);
    stxlib::mcp::ServerOptions options{};
    options.model_meta_key = model_meta_key;
    stxlib::mcp::ServerEvents events{};
    state.events.run = state.run;
    events.context = &state.events;
    events.client_seen = &client_seen;
    events.input_ended = &input_ended;
    stxlib::OperationResult<stxlib::mcp::Server> created =
        stxlib::mcp::Server::create(server_identity, options, events);
    if (created.failed()) {
        error = "the MCP server could not be created";
        return false;
    }
    state.server = std::move(*created.value());
    // Everything known now is bound before serving starts, so a client
    // sees the whole list at once and no list_changed for it.
    for (const ToolSpec& spec : specs) {
        if (bind_one(state, spec)) state.bound.push_back(spec);
    }
    if (!state.app_note.empty()) {
        stxlib::mcp::ResourceSpec note{};
        note.uri = app_note_uri;
        note.name = "app-note";
        note.title = "How this program works";
        note.description = "The program's own note for a model: how its tools and its interface behave.";
        note.mime_type = "text/plain";
        static_cast<void>(state.server.bind_resource(note, &read_app_note, &state));
    }
    const stxlib::OperationStatus started =
        state.server.start(std::move(state.streams.input), std::move(state.streams.output));
    if (started.failed()) {
        error = "the MCP server did not start";
        return false;
    }
    state.serving = true;
    return true;
}

McpTransport::McpTransport(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

McpTransport::~McpTransport() {
    stop();
}

void McpTransport::stop() noexcept {
    if (!state_ || !serving()) return;
    (*state_).server.stop();
    static_cast<void>((*state_).server.wait());
    const stxlib::ScopedLock lock((*state_).mutex);
    (*state_).serving = false;
}

bool McpTransport::serving() const noexcept {
    const stxlib::ScopedLock lock((*state_).mutex);
    return (*state_).serving;
}

void McpTransport::bind_tools(const std::vector<ToolSpec>& specs) {
    State& state = *state_;
    const stxlib::ScopedLock lock(state.mutex);
    if (!state.serving) return;
    // Only what changed: each bind or unbind sends the client list_changed,
    // so tools whose spec is the same are left bound.
    std::size_t index = 0U;
    while (index < state.bound.size()) {
        const ToolSpec& old = state.bound[index];
        bool kept = false;
        for (const ToolSpec& spec : specs) {
            if (same_spec(old, spec)) {
                kept = true;
                break;
            }
        }
        if (kept) {
            ++index;
            continue;
        }
        static_cast<void>(state.server.unbind_tool(text_view(old.name)));
        state.bound.erase(state.bound.begin() + static_cast<std::ptrdiff_t>(index));
    }
    for (const ToolSpec& spec : specs) {
        bool present = false;
        for (const ToolSpec& old : state.bound) {
            if (old.name == spec.name) {
                present = true;
                break;
            }
        }
        if (!present && bind_one(state, spec)) state.bound.push_back(spec);
    }
}

#else

struct McpTransport::State final {};

std::unique_ptr<McpTransport> McpTransport::claim(AutomationRun&, std::string& error) {
    error = "this build has no MCP transport (stx_mcp was not found)";
    return nullptr;
}

std::unique_ptr<McpTransport> McpTransport::over(AutomationRun&, AttachStreams) {
    return nullptr;
}

void McpTransport::request_stop() noexcept {}

void McpTransport::wait() noexcept {}

bool McpTransport::serve(const std::string&, const std::string&, const std::string&, const std::vector<ToolSpec>&,
                         std::string& error) {
    error = "this build has no MCP transport";
    return false;
}

bool McpTransport::serving() const noexcept {
    return false;
}

McpTransport::McpTransport(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

McpTransport::~McpTransport() = default;

void McpTransport::stop() noexcept {}

void McpTransport::bind_tools(const std::vector<ToolSpec>&) {}

#endif

} // namespace gui_forms::automation
