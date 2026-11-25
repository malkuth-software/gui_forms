#pragma once
// A program's own model tools (notes:planning/studies/MCP_AUTOMATION_STUDY.md,
// sections 6.1 and 7): tools and inspect sections a GUI.Forms program
// declares for a model, served beside the library's built-in ones when the
// program runs with --mcp (or GUI_FORMS_MCP=1) and run by --mcp-script.
// The library's own extensions register through these same calls.
//
//     ModelToolSpec spec{};
//     spec.name = "convert.form";
//     spec.description = "Converts a Windows Forms designer file.";
//     spec.input_schema = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}";
//     register_model_tool(spec, &convert_form, &converter);
//
// A tool is a spec and a named handler: a function taking its context, the
// call and the reply it fills (house style: no lambdas, no std::function).
// Registrations are process-wide and may be made before Application::run;
// the tool list a connected client sees changes with them.
//
// The safety rules hold for every tool (study 9 and 10):
// - a tool marked destructive asks the user's one question of the launch
//   before its first call, and is refused once the user said Don't Allow;
// - an acting tool runs on the UI thread and is refused while a dialog the
//   program did not instrument is open; one that settles runs as an act,
//   posted, waited on until the window is idle and answered with what
//   changed;
// - a handler reads the program through its own C++ objects and is
//   responsible for its reply: it must not put a password field's text, or
//   anything the program would not show the user, in it.
//
// Threads: a ModelToolThread::ui handler runs on the UI thread of the
// program's first window; a ModelToolThread::any handler runs on the
// transport's thread and must not touch controls. Registration may be made
// from any thread. A handler runs after the registry's lock is released, so
// it may register and unregister tools; its context must stay valid until
// unregister_model_tool returned and no call of it is running.

#include "gui_forms/export/application_export.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms {
class Window;
class Control;
struct SemanticNode;

namespace automation {

// What a tool may do, as MCP's annotations tell the client: read_only
// (readOnlyHint) or acting (it changes the program).
enum class ModelToolAccess : std::uint8_t {
    read_only,
    acting,
};

// Where the handler runs.
enum class ModelToolThread : std::uint8_t {
    ui,
    any,
};

// The named failures a tool answers (study 7.5), then the dispatcher's own.
// A failure reaches the model as a tool result marked as an error, with the
// code's name, a message and a hint saying what to do next.
enum class ModelToolError : std::uint8_t {
    // No node with that stable ID; the hint names the closest IDs by name.
    not_found,
    // The control is gone; take a new snapshot.
    stale_identity,
    // The ID names more than one node; name the parent.
    ambiguous,
    disabled,
    not_visible,
    // Not among the node's actions; the hint lists the actions it has.
    unsupported_action,
    // A dialog is open and not instrumented for a model (study 9.2).
    dialog_open,
    // A security approval waits for the user (study 9.3).
    needs_user,
    // The user took control back (study 8.3).
    user_took_control,
    // The user refused destructive acts for this launch (study 10).
    declined,
    // A password field (study 9.1).
    protected_content,
    // A wait ran out; the hint says what was still busy.
    timeout,
    // A control this build does not have (study 17.2).
    not_in_this_build,
    // The arguments do not match the tool's schema.
    invalid_arguments,
    unknown_tool,
    // The tool's window closed, or no window is attached.
    window_gone,
    // The client cancelled the call, or the program is shutting down.
    cancelled,
    // The program's limit on concurrent calls is reached.
    busy,
    // The program failed (an exception at the tool boundary).
    internal,
    // A batch's expect step found another value or state than it named; the
    // message gives what it observed.
    expectation_failed,
};

// The code's name as the model sees it ("not_found", "protected", ...).
// Static storage.
[[nodiscard]] GUI_FORMS_API const char* model_tool_error_name(ModelToolError error) noexcept;

// A tool's declaration. The texts are UTF-8, NUL-terminated and copied by
// register_model_tool.
struct GUI_FORMS_API ModelToolSpec final {
    // 1 to 64 bytes of ASCII letters, digits, '_' and '-', starting with a
    // letter (no '.': clients rename dotted names, round 2); unique among
    // the program's tools and the built-in ones, and not starting
    // `command_` (the commands' own tools).
    const char* name{};
    // A short title for a client's list; may be null.
    const char* title{};
    // What the tool does, for the model: at most 4096 bytes.
    const char* description{};
    // The arguments' JSON Schema, one object of at most 16384 bytes; null
    // or empty takes any object.
    const char* input_schema{};
    ModelToolAccess access{ModelToolAccess::read_only};
    // Every call changes something the user cannot take back: the first
    // call of the launch asks the user (study 10).
    bool destructive{};
    // Must be ui for an acting tool.
    ModelToolThread thread{ModelToolThread::ui};
    // An acting UI tool that settles runs as an act (posted, then waited on
    // until the window is idle, with a diff in its reply).
    bool settles{};
};

// One call of a tool, borrowed for the handler's run.
struct GUI_FORMS_API ModelToolCall final {
    // The arguments: one JSON object, UTF-8, not NUL-terminated.
    const char* arguments_json{};
    std::size_t arguments_bytes{};
    // Who is calling, as the client states it (not verified); NUL-terminated
    // and possibly empty.
    const char* client_name{};
    const char* model_name{};
    // The window the call names (its "window" argument, else the active
    // one) for a UI tool; null for a ModelToolThread::any tool.
    Window* window{};
};

struct ModelToolReplyState;

// The reply a handler fills, borrowed for its run: text, structured
// content and images, or one failure. A reply left empty succeeds with no
// text; after fail, the rest is ignored.
class GUI_FORMS_API ModelToolReply final {
public:
    explicit ModelToolReply(ModelToolReplyState& state) noexcept : state_(&state) {}
    ModelToolReply(const ModelToolReply&) = delete;
    ModelToolReply& operator=(const ModelToolReply&) = delete;

    // Appends UTF-8 text; false (appending nothing) for text that is not
    // UTF-8 or would pass 4 MiB.
    bool text(const char* utf8, std::size_t bytes);
    // Sets the structured content: one JSON object matching the tool's
    // declared output; false for anything else.
    bool structured(const char* json, std::size_t bytes);
    // Adds an image (PNG unless mime_type names another image type); false
    // for no bytes, more than 16 MiB, or a mime type outside the image types.
    bool image(const unsigned char* bytes, std::size_t count, const char* mime_type);
    // Fails the call with error, a message and a hint (each NUL-terminated,
    // may be null).
    void fail(ModelToolError error, const char* message, const char* hint);

private:
    ModelToolReplyState* state_;
};

using ModelToolHandler = void (*)(void* context, const ModelToolCall& call, ModelToolReply& reply);

enum class ModelToolRegistration : std::uint8_t {
    added,
    // The name is not 1 to 64 bytes of the allowed characters.
    invalid_name,
    // The name is taken, or is a built-in tool's or a command's
    // (command.<id>).
    duplicate,
    // 256 tools, or 64 inspect sections, are registered already.
    full,
    // No handler, a description too long, a schema that is not one object,
    // text that is not UTF-8, or an acting tool off the UI thread.
    invalid_spec,
};

// Static storage.
[[nodiscard]] GUI_FORMS_API const char* model_tool_registration_name(ModelToolRegistration registration) noexcept;

// Declares a tool. In a build without the automation (high-integrity, or
// GUI_FORMS_MCP off) the tool is recorded and never served.
[[nodiscard]] GUI_FORMS_API ModelToolRegistration register_model_tool(const ModelToolSpec& spec,
                                                                      ModelToolHandler handler, void* context);
// Withdraws the tool `name`; false when none has it.
GUI_FORMS_API bool unregister_model_tool(const char* name);

// One inspect section's request, borrowed for the handler's run: the
// window, and the node and control a target named (null for none).
struct GUI_FORMS_API ModelInspectRequest final {
    Window* window{};
    const SemanticNode* node{};
    Control* control{};
    const char* arguments_json{};
    std::size_t arguments_bytes{};
};

struct ModelInspectTextState;

// The text an inspect section appends to, borrowed for its run.
class GUI_FORMS_API ModelInspectText final {
public:
    explicit ModelInspectText(ModelInspectTextState& state) noexcept : state_(&state) {}
    ModelInspectText(const ModelInspectText&) = delete;
    ModelInspectText& operator=(const ModelInspectText&) = delete;

    // Appends UTF-8 text; false (appending nothing) for text that is not
    // UTF-8. inspect cuts a section at 64 KiB.
    bool append(const char* utf8, std::size_t bytes);

private:
    ModelInspectTextState* state_;
};

using ModelInspectHandler = void (*)(void* context, const ModelInspectRequest& request, ModelInspectText& text);

// Declares an inspect section `name` (inspect's `what`), described for the
// model. UI thread handlers only: inspect runs on the UI thread. The texts
// are NUL-terminated and copied.
[[nodiscard]] GUI_FORMS_API ModelToolRegistration register_model_inspect_section(const char* name,
                                                                                 const char* description,
                                                                                 ModelInspectHandler handler,
                                                                                 void* context);
GUI_FORMS_API bool unregister_model_inspect_section(const char* name);

} // namespace automation
} // namespace gui_forms
