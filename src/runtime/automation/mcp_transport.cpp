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

void client_seen(void* context, const stxlib::mcp::ClientIdentity& client) noexcept {
    try {
        ClientIdentity identity{};
        identity.client_name = standard_text(client.name());
        identity.client_version = standard_text(client.version());
        identity.model_name = standard_text(client.model());
        (*static_cast<AutomationRun*>(context)).post_client(identity);
    } catch (...) {
        // Out of memory: the tag line keeps the last name.
    }
}

void input_ended(void* context) noexcept {
    try {
        (*static_cast<AutomationRun*>(context)).post_close_all();
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
    } catch (const std::exception& failure) {
        const std::string message(failure.what());
        reply.fail("internal", text_view(message), stxlib::TextView{});
    } catch (...) {
        reply.fail("internal", "the call failed", stxlib::TextView{});
    }
}

} // namespace

struct McpTransport::State final {
    AutomationRun* run{};
    std::string identity{};
    stxlib::mcp::Server server{};
    std::mutex mutex{};
    std::vector<std::string> bound{};
};

std::unique_ptr<McpTransport> McpTransport::start(AutomationRun& run, const std::string& identity,
                                                  std::string& error) {
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).run = &run;
    (*state).identity = identity;
    stxlib::mcp::ServerIdentity server_identity{};
    server_identity.name = text_view((*state).identity);
    server_identity.version = "1";
    server_identity.instructions =
        "A GUI.Forms program. Read it with windows and snapshot; act with act, pointer and keyboard; each act "
        "replies with what changed. Destructive acts ask the user once per launch.";
    stxlib::mcp::ServerOptions options{};
    options.model_meta_key = model_meta_key;
    stxlib::mcp::ServerEvents events{};
    events.context = &run;
    events.client_seen = &client_seen;
    events.input_ended = &input_ended;
    stxlib::OperationResult<stxlib::mcp::Server> created =
        stxlib::mcp::Server::create(server_identity, options, events);
    if (created.failed()) {
        error = "the MCP server could not be created";
        return nullptr;
    }
    (*state).server = std::move(*created.value());
    stxlib::OperationResult<stxlib::ProtocolStreams> streams = stxlib::claim_protocol_streams();
    if (streams.failed()) {
        error = "standard input and output could not be claimed";
        return nullptr;
    }
    stxlib::ProtocolStreams& claimed = *streams.value();
    const stxlib::OperationStatus started =
        (*state).server.start(std::move(claimed.input), std::move(claimed.output));
    if (started.failed()) {
        error = "the MCP server did not start";
        return nullptr;
    }
    return std::unique_ptr<McpTransport>(new McpTransport(std::move(state)));
}

McpTransport::McpTransport(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

McpTransport::~McpTransport() {
    stop();
}

void McpTransport::stop() noexcept {
    if (!state_) return;
    (*state_).server.stop();
    static_cast<void>((*state_).server.wait());
}

void McpTransport::bind_tools(const std::vector<ToolSpec>& specs) {
    State& state = *state_;
    const std::lock_guard<std::mutex> lock(state.mutex);
    // Unbind what is gone, or changed (bound again below).
    for (const std::string& name : state.bound) {
        static_cast<void>(state.server.unbind_tool(text_view(name)));
    }
    state.bound.clear();
    for (const ToolSpec& spec : specs) {
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
            continue;
        }
        state.bound.push_back(spec.name);
    }
}

#else

struct McpTransport::State final {};

std::unique_ptr<McpTransport> McpTransport::start(AutomationRun&, const std::string&, std::string& error) {
    error = "this build has no MCP transport (stx_mcp was not found)";
    return nullptr;
}

McpTransport::McpTransport(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

McpTransport::~McpTransport() = default;

void McpTransport::stop() noexcept {}

void McpTransport::bind_tools(const std::vector<ToolSpec>&) {}

#endif

} // namespace gui_forms::automation
