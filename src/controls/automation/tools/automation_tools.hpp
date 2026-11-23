#pragma once

// Private to GUI.Forms (src/): the registry behind the public model-tool
// calls (gui_forms/automation/tools/model_tools.hpp), which programs and
// GUI.Forms' own extensions register through. It speaks the standard
// vocabulary, which D15 keeps off the public C++ surface; its enumerations,
// handlers and requests are the public ones.
//
// The automation tool registry: the one seam through which every tool a
// model may call is declared and dispatched (notes:planning/studies/
// MCP_AUTOMATION_STUDY.md, sections 6.1 and 7). The automation core
// registers its built-in tools here (windows, snapshot, act, pointer,
// keyboard, text, wait, capture, inspect, batch, control); a program, and
// GUI.Forms' later stages (the inspection extensions and the WinForms
// converter, study section 18), register theirs the same way. The MCP
// wiring binds every entry to stx_mcp, and a script runs them without a
// model, so a tool declared here is reachable from both.
//
// The shape follows rpclib's bind (study 6.1) without its type erasure: a
// tool is a ToolSpec (its name, description, argument schema and marks) and
// a named handler, a function taking a context pointer, the call and the
// reply it fills (ModelToolHandler). No lambdas, no std::function. The
// registry is process-wide (model_tool_registry), so a program registers
// before Application::run starts the automation.
//
// Inspect sections are the second extension point: `inspect` answers a
// section by name from the sections registered here (the core's own:
// state, layout, paint, metrics, dispatcher, interaction, session, schema;
// later stages add event propagation, layout reasons, resources and state
// summaries).
//
// Thread: the registry is shared by the UI thread (which calls UI tools)
// and the MCP handler tasks (which list tools and call any-thread tools),
// so it is guarded by one mutex. Handlers are never called under it: a
// caller copies the entry and calls it after the lock is released.

#include "gui_forms/automation/tools/model_tools.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms {
class Window;
class Control;
struct SemanticNode;
} // namespace gui_forms

namespace gui_forms::automation {

class AutomationCore;
struct ClientIdentity;

// The named failures a tool answers (study 7.5): the public enumeration.
using ToolError = ModelToolError;

// The code's name as the model sees it ("not_found", "protected", ...).
// Static storage.
[[nodiscard]] const char* tool_error_name(ToolError error) noexcept;

// An image in a tool's reply: PNG bytes, owned by the reply.
struct ToolImage final {
    std::string mime_type{"image/png"};
    std::vector<std::byte> bytes{};
};

// A successful reply: text first (the outline, the diff), an optional JSON
// object for clients that read structured content (it must match the
// tool's declared output, study 13.2), and images.
struct ToolSuccess final {
    std::string text{};
    std::string structured_json{};
    std::vector<ToolImage> images{};
};

// A refused or failed call.
struct ToolFailure final {
    ToolError error{ToolError::internal};
    std::string message{};
    std::string hint{};
};

// A tool's reply: its success or its failure, never both (house style 2).
class ToolReply final {
public:
    [[nodiscard]] static ToolReply success(std::string text);
    [[nodiscard]] static ToolReply success(ToolSuccess value);
    [[nodiscard]] static ToolReply failure(ToolError error, std::string message, std::string hint = {});

    [[nodiscard]] bool succeeded() const noexcept { return std::holds_alternative<ToolSuccess>(state_); }
    // The success, or null for a failure. Borrowed from this reply.
    [[nodiscard]] const ToolSuccess* success_value() const& noexcept { return std::get_if<ToolSuccess>(&state_); }
    const ToolSuccess* success_value() const&& = delete;
    [[nodiscard]] ToolSuccess* success_value() & noexcept { return std::get_if<ToolSuccess>(&state_); }
    // The failure, or null for a success. Borrowed from this reply.
    [[nodiscard]] const ToolFailure* failure_value() const& noexcept { return std::get_if<ToolFailure>(&state_); }
    const ToolFailure* failure_value() const&& = delete;

    ToolReply() : state_(ToolFailure{}) {}

private:
    explicit ToolReply(std::variant<ToolSuccess, ToolFailure> state) noexcept : state_(std::move(state)) {}
    std::variant<ToolSuccess, ToolFailure> state_;
};

// What a tool may do, as MCP's annotations tell the client (study 10):
// read_only says readOnlyHint; acting says it changes the program. Whether
// a particular call is destructive depends on its target (a destructive
// command, control action or window, marked by the program) and is decided
// at the call; `ToolSpec::destructive` marks a tool every call of which is.
using ToolAccess = ModelToolAccess;

// Where a tool's handler runs. UI tools run on the UI thread of the
// automation's windows, at a dispatcher turn, and may touch windows;
// any-thread tools run on the caller's thread (an MCP handler task or the
// script runner) and touch no window: a converter reading files, say.
using ToolThread = ModelToolThread;

// The longest tool name, and its alphabet: ASCII letters, digits, '_',
// '-' and '.', starting with a letter (MCP's tool name rule).
inline constexpr std::size_t maximum_tool_name_bytes = 64U;
// The most tools one registry holds, programs' commands not counted.
inline constexpr std::size_t maximum_registered_tools = 256U;
// The longest description and argument schema a tool may declare.
inline constexpr std::size_t maximum_tool_description_bytes = 4096U;
inline constexpr std::size_t maximum_tool_schema_bytes = 16384U;

struct ToolSpec final {
    std::string name{};
    std::string title{};
    std::string description{};
    // The JSON Schema of the arguments object, as text. Empty is an object
    // without properties. The dispatcher checks that the arguments are one
    // JSON object; the handler checks the members it reads.
    std::string input_schema{};
    ToolAccess access{ToolAccess::read_only};
    // Every call of the tool is a destructive act (asks the per-launch
    // question, study 10, and carries destructiveHint).
    bool destructive{};
    ToolThread thread{ToolThread::ui};
    // After an acting UI call succeeds, the core waits for the window to be
    // idle and replies with what changed (study 7.4), as act does.
    bool settles{};
};

// A tool's handler: a named function, never a lambda. `context` is the
// pointer given with the tool and must outlive its registration.
using ToolHandler = ModelToolHandler;

// The state behind a ModelToolReply: what the handler gave so far.
struct ModelToolReplyState final {
    ToolSuccess success{};
    bool failed{};
    ToolFailure failure{};
};

// The reply a handler filled.
[[nodiscard]] ToolReply finished_reply(ModelToolReplyState& state);

struct ToolEntry final {
    ToolSpec spec{};
    ToolHandler handler{};
    void* context{};
};

// Calls `entry`'s handler with `call` and returns its reply; an exception
// from the handler is the call's internal failure.
[[nodiscard]] ToolReply call_tool_handler(const ToolEntry& entry, const ModelToolCall& call);

// What `inspect` gives a section: the window and, when the call named a
// target, the node and its nearest control (null for a window-wide
// request), and the call's arguments.
using InspectRequest = ModelInspectRequest;

// One section of `inspect`: appends its text (bounded by the section; say
// what was left out). UI thread.
using InspectSectionHandler = ModelInspectHandler;

// The state behind a ModelInspectText.
struct ModelInspectTextState final {
    std::string text{};
};

// Calls a section written against a std::string (the library's own).
template <void (*Body)(void* context, const InspectRequest& request, std::string& text)>
void string_section(void* context, const InspectRequest& request, ModelInspectText& text) {
    std::string written{};
    Body(context, request, written);
    static_cast<void>(text.append(written.data(), written.size()));
}

struct InspectSectionEntry final {
    std::string name{};
    std::string description{};
    InspectSectionHandler handler{};
    void* context{};
};

// Runs `entry`'s handler for `request` and returns its text.
[[nodiscard]] std::string call_inspect_section(const InspectSectionEntry& entry,
                                               const InspectRequest& request);

inline constexpr std::size_t maximum_inspect_sections = 64U;

using ToolRegistration = ModelToolRegistration;

[[nodiscard]] const char* tool_registration_name(ToolRegistration registration) noexcept;
// Whether `name` is a tool name the registry accepts.
[[nodiscard]] bool valid_tool_name(std::string_view name) noexcept;

class ToolRegistry final {
public:
    ToolRegistry() = default;
    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    // Registers a tool. A refusal registers nothing. Any thread.
    [[nodiscard]] ToolRegistration add(ToolSpec spec, ToolHandler handler, void* context);
    // Removes the tool named `name`; false when none is. Once this returns
    // the registry makes no new call to its handler; a call already copied
    // out may still be running, so the context must outlive it (the core
    // removes UI tools on the UI thread, between calls).
    bool remove(std::string_view name);
    // Copies the entry named `name` into `entry`; false, leaving it, when
    // none is.
    [[nodiscard]] bool find(std::string_view name, ToolEntry& entry) const;
    // Replaces `specs` with every registered tool's spec, in registration
    // order.
    void fill_specs(std::vector<ToolSpec>& specs) const;

    [[nodiscard]] ToolRegistration add_inspect_section(std::string name, std::string description,
                                                       InspectSectionHandler handler, void* context);
    bool remove_inspect_section(std::string_view name);
    [[nodiscard]] bool find_inspect_section(std::string_view name, InspectSectionEntry& entry) const;
    void fill_inspect_sections(std::vector<InspectSectionEntry>& sections) const;

    // Changes whenever a tool is added or removed, so the MCP wiring can tell
    // the client its tool list changed.
    [[nodiscard]] std::uint64_t generation() const;

private:
    mutable std::mutex mutex_{};
    std::vector<ToolEntry> tools_{};
    std::vector<InspectSectionEntry> sections_{};
    std::uint64_t generation_{1U};
};

// The process-wide registry: every program's tools and sections and the
// library's own, registered through model_tools.hpp or here. Never
// destroyed.
[[nodiscard]] ToolRegistry& model_tool_registry() noexcept;

} // namespace gui_forms::automation
