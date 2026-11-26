#include "automation_calls.hpp"

#include "../../../core/host/session/session_directory.hpp"
#include "../../commands/command_directory/command_directory.hpp"
#include "../marks/automation_marks_internal.hpp"
#include "../model_extensions/model_inspect_sections.hpp"

#include "gui_forms/automation/marks/automation_marks.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/content_surface.hpp"
#include "gui_forms/controls/scrollable_control.hpp"
#include "gui_forms/host/session/host_session.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <exception>
#include <limits>

namespace gui_forms::automation {

AutomationPump::~AutomationPump() = default;

namespace {

using detail::JsonKind;
using detail::JsonValue;

// ---- tool names and schemas ------------------------------------------------------

constexpr std::string_view command_tool_prefix = "command_";

// The arguments every tool that reads or acts on a window takes.
constexpr const char* window_property = "\"window\":{\"type\":\"string\",\"description\":\"a window's stable ID or "
                                        "title; default the active window\"}";
constexpr const char* target_properties =
    "\"target\":{\"type\":\"string\",\"description\":\"a node's stable ID, or #r<n> for a runtime ID\"},"
    "\"within\":{\"type\":\"string\",\"description\":\"an ancestor's stable ID, when the target's ID repeats\"}";

// An acting call's answer to a question the program raises meanwhile.
constexpr std::string_view on_unsaved_property =
    "\"on_unsaved\":{\"type\":\"string\",\"enum\":[\"save\",\"discard\"],\"description\":\"The answer to "
    "a save changes? question the act raises: save or discard; without it the act stops at the question and "
    "replies with it.\"}";

std::string object_schema(std::string_view properties) {
    std::string schema = "{\"type\":\"object\",\"properties\":{";
    schema += window_property;
    if (!properties.empty()) {
        schema += ',';
        schema += properties;
    }
    schema += "},\"additionalProperties\":false}";
    return schema;
}

ToolSpec built_in(const char* name, const char* title, const char* description, std::string schema,
                  ToolAccess access) {
    ToolSpec spec{};
    spec.name = name;
    spec.title = title;
    spec.description = description;
    spec.input_schema = std::move(schema);
    spec.access = access;
    spec.thread = ToolThread::ui;
    return spec;
}

// The batch's schema: a step is a tool step, whose other members are that
// tool's arguments (checked against its schema when it runs), or an
// expect guard with exactly these members.
std::string batch_schema() {
    std::string schema =
        "{\"type\":\"object\",\"properties\":{\"steps\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":512,"
        // One step schema, since the MCP server takes no anyOf (STXlib's
        // supported subset): a tool step names tool and carries that tool's
        // arguments beside it; an expect step names expect and takes only
        // the members below, which the core checks (expect_member).
        "\"items\":{\"type\":\"object\",\"minProperties\":1,\"description\":\"a tool step {tool, ...that "
        "tool's arguments} or an expect step {expect, window, target, within, state, value, exists, drawn}\","
        "\"properties\":{\"tool\":{\"type\":\"string\"},"
        "\"expect\":{\"type\":\"string\",\"enum\":[\"node\",\"visual\"]},";
    schema += window_property;
    schema += ",";
    schema += target_properties;
    schema += ",\"state\":{\"type\":\"string\",\"description\":\"a state the node has, or -state for one it lacks\"},"
              "\"value\":{\"type\":\"string\"},\"exists\":{\"type\":\"boolean\"},"
              "\"drawn\":{\"type\":\"boolean\",\"description\":\"visual: whether the state is drawn\"}}}},"
              "\"stop_on_error\":{\"type\":\"boolean\"},\"terse\":{\"type\":\"boolean\"}},\"required\":[\"steps\"],"
              "\"additionalProperties\":false}";
    return schema;
}

// `sections` (the inspect sections, built-in and the program's) as a JSON
// enumeration's items, and as a list for a description.
void section_lists(std::span<const InspectSectionEntry> sections, std::string& items, std::string& names) {
    for (const InspectSectionEntry& section : sections) {
        if (!items.empty()) items += ',';
        detail::append_json_string(section.name, items);
        if (!names.empty()) names += ", ";
        names += section.name;
    }
}

void append_built_in_specs(std::vector<ToolSpec>& specs, std::span<const InspectSectionEntry> sections) {
    const std::string targets = target_properties;
    specs.push_back(built_in("windows", "Windows", "Every window: stable ID, title, active, modal, size.",
                             "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
                             ToolAccess::read_only));
    specs.push_back(built_in(
        "snapshot", "Snapshot",
        "The control tree as an outline: role, name, [stable ID], states, value, the actions each node takes and "
        "what its press opens (needs_user: it opens a dialog the user answers; instead= names a tool that does the "
        "same without one; default and cancel: the buttons Enter and Escape press). A virtualised list says how "
        "many rows it does not show. Narrow it with root and filter; "
        "actions false leaves the actions out. A read: needs no control.",
        object_schema("\"root\":{\"type\":\"string\"},\"filter\":{\"type\":\"string\"},"
                      "\"limit\":{\"type\":\"integer\",\"minimum\":1},\"bounds\":{\"type\":\"boolean\"},"
                      "\"actions\":{\"type\":\"boolean\"}"),
        ToolAccess::read_only));
    specs.push_back(built_in(
        "find", "Find",
        "Nodes by role, name (any case, part of it) and state, each with its [stable ID] and parent; cheaper than a "
        "snapshot when you know what you look for.",
        object_schema("\"role\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},"
                      "\"state\":{\"type\":\"string\"},\"within\":{\"type\":\"string\"},"
                      "\"limit\":{\"type\":\"integer\",\"minimum\":1}"),
        ToolAccess::read_only));
    specs.push_back(built_in(
        "act", "Act",
        "A semantic action on a node (press, select, expand, collapse, focus, set_value, increment, decrement, "
        "show_menu, close), scroll_into_view, or invoke_command on a command's stable ID. A row a virtualised list "
        "does not show is reached by its ID. A dialog you opened may be closed (close on the dialog, as Cancel), "
        "never submitted. close on a program window's own ID closes the window: save first, then close. A "
        "question the program raises during an act (save changes?) is yours and never shown: with no "
        "on_unsaved the act stops there, the program stays as it was, and the reply gives the question and its "
        "choices; on_unsaved save or discard answers it. Waits for idle and replies with what changed; terse "
        "gives counts. Acts need control: in a shared launch (--mcp-shared) your first act asks the user (a "
        "10-second countdown that allows).",
        object_schema(targets +
                      ",\"action\":{\"type\":\"string\",\"enum\":[\"press\",\"select\",\"expand\",\"collapse\","
                      "\"focus\",\"set_value\",\"increment\",\"decrement\",\"show_menu\",\"close\","
                      "\"scroll_into_view\",\"invoke_command\"]},\"value\":{\"type\":\"string\"},"
                      "\"terse\":{\"type\":\"boolean\"}," + std::string(on_unsaved_property)),
        ToolAccess::acting));
    specs.push_back(built_in(
        "reset", "Reset",
        "Puts the interface back in order: closes the dialogs you opened (as Cancel), popups and menus, tooltips "
        "and drags, releases pointer capture, and stops content that froze; replies with what it reset "
        "and what it left (a dialog the user opened). Acts need control: in a shared launch (--mcp-shared) "
        "your first act asks the user (a 10-second countdown that allows).",
        object_schema(std::string_view{}), ToolAccess::acting));
    // A point's schema, in the space `space` names (round 2: the pointer's
    // arguments were bare objects).
    const std::string target_point =
        "{\"type\":\"object\",\"description\":\"Logical pixels from the target's top-left corner (its bounds "
        "as snapshot bounds gives them)\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":"
        "\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":false}";
    const std::string window_point =
        "{\"type\":\"object\",\"description\":\"Window coordinates: logical pixels from the top-left corner of "
        "the window's content area, the space of snapshot bounds\",\"properties\":{\"x\":{\"type\":"
        "\"number\"},\"y\":{\"type\":\"number\"}},\"required\":[\"x\",\"y\"],\"additionalProperties\":"
        "false}";
    const std::string path_point =
        detail::point_schema("With a target, logical pixels from the target's top-left corner (as offset); without "
                             "one, window coordinates (as position)");
    const std::string wheel_delta =
        "{\"type\":\"object\",\"description\":\"Wheel notches: y -1 scrolls down one notch, y 1 up; x scrolls "
        "sideways. Default {x: 0, y: -1}\",\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":"
        "\"number\"}},\"additionalProperties\":false}";
    specs.push_back(built_in(
        "pointer", "Pointer",
        "Pointer input through the program's own input path: move, hover, click, double_click, right_click, down, "
        "up, drag, wheel. Aim with a target (offset inside it, default its centre) or with position in window "
        "coordinates, not both. A drag presses at that point and moves through path: with a target, path points "
        "are in the target's space like offset; without one, window coordinates like position. Acts need "
        "control: in a shared launch (--mcp-shared) your first act asks the user (a 10-second countdown that "
        "allows).",
        object_schema(targets +
                      ",\"action\":{\"type\":\"string\",\"enum\":[\"move\",\"hover\",\"click\",\"double_click\","
                      "\"right_click\",\"down\",\"up\",\"drag\",\"wheel\"]},"
                      "\"offset\":" + target_point + ",\"position\":" + window_point +
                      ",\"path\":{\"type\":\"array\",\"items\":" + path_point + ",\"minItems\":1,\"maxItems\":256},"
                      "\"button\":{\"type\":\"string\",\"enum\":[\"primary\",\"secondary\",\"middle\"]},"
                      "\"modifiers\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
                      "\"delta\":" + wheel_delta + ",\"terse\":{\"type\":\"boolean\"}"),
        ToolAccess::acting));
    specs.push_back(built_in(
        "keyboard", "Keyboard",
        "Key chords (\"Ctrl+S\") and sequences, committed text, and composition, to the focused control or to a "
        "target focused first. Acts need control: in a shared launch (--mcp-shared) your first act asks the "
        "user (a 10-second countdown that allows). Replies with what changed, as act does.",
        object_schema(targets +
                      ",\"keys\":{\"type\":[\"string\",\"array\"],\"items\":{\"type\":\"string\"}},"
                      "\"text\":{\"type\":\"string\"},\"composition\":{\"type\":\"object\"},"
                      "\"terse\":{\"type\":\"boolean\"}"),
        ToolAccess::acting));
    specs.push_back(built_in(
        "text", "Text",
        "A node's whole text: a text control's value, selection and lines (with select, sets the selection, UTF-8 "
        "byte offsets); any other node's full name, value and description, such as a label cut in the outline.",
        object_schema(targets + ",\"select\":{\"type\":\"object\",\"properties\":{\"anchor\":{\"type\":\"integer\","
                                "\"minimum\":0},\"caret\":{\"type\":\"integer\",\"minimum\":0}},"
                                "\"required\":[\"anchor\",\"caret\"]}"),
        ToolAccess::acting));
    specs.push_back(built_in(
        "wait", "Wait",
        "Waits until the next frame, idle, a time (ms), a condition on a node (state, value, exists) or a user step "
        "done.",
        object_schema(targets +
                      ",\"until\":{\"type\":\"string\",\"enum\":[\"frame\",\"idle\",\"ms\",\"node\",\"user_step\"]},"
                      "\"ms\":{\"type\":\"number\",\"minimum\":0},\"timeout_ms\":{\"type\":\"number\",\"minimum\":0},"
                      "\"state\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"},"
                      "\"exists\":{\"type\":\"boolean\"},\"step\":{\"type\":\"string\"}"),
        ToolAccess::read_only));
    specs.push_back(built_in(
        "capture", "Capture",
        "A window or control as a PNG, drawn fresh from the current state; a window's open popup windows (menus, "
        "drop-downs) and its tooltip window are composited over it unless popups is false. Password fields are "
        "painted out. The in-use sign (the band, AI mode's stripes) is left out unless include_sign is true, so "
        "you see the program as its user does without a model. points [{x, y}, ...] (or [x, y] pairs; logical "
        "units from the captured area's top left, at most 1024) answers the colour at each instead of an image, to check a "
        "drawing cheaply. A read: needs no control.",
        object_schema(targets + ",\"mode\":{\"type\":\"string\",\"enum\":[\"rendered\"]},"
                                "\"points\":{\"type\":\"array\",\"maxItems\":1024,\"items\":" +
                                detail::point_schema("") + "},"
                                "\"scale\":{\"type\":\"number\"},\"margin\":{\"type\":\"number\"},"
                                "\"include_overlays\":{\"type\":\"boolean\"},\"popups\":{\"type\":\"boolean\"},"
                                "\"include_sign\":{\"type\":\"boolean\"}"),
        ToolAccess::read_only));
    std::string section_items{};
    std::string section_names{};
    section_lists(sections, section_items, section_names);
    // The sections are named, the program's own among them (MCP round 1:
    // the description named a section that did not exist).
    const std::string inspect_description =
        "Beyond accessibility, sections chosen with what: " + section_names +
        ". With a target, state, layout and schema by default; without, metrics, dispatcher and interaction.";
    specs.push_back(built_in(
        "inspect", "Inspect", inspect_description.c_str(),
        object_schema(targets + ",\"what\":{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[" +
                      section_items + "]}}"),
        ToolAccess::read_only));
    specs.push_back(built_in(
        "batch", "Batch",
        "Several steps in one call with one combined reply: each step is {\"tool\": name, ...that tool's arguments} "
        "or an expect guard {\"expect\": \"node\"|\"visual\", target, state, value, exists, drawn}; the batch stops at "
        "the first failure or unmet expectation; at most 512 steps. terse gives each act's changes as counts.",
        batch_schema(), ToolAccess::acting));
    specs.push_back(built_in(
        "release_control", "Release control",
        "Gives the program back to the user and ends AI mode. In a --mcp-shared launch your first act asks the "
        "user (a 10-second countdown that allows when it ends) and you then hold control until you release it or "
        "30 s pass without a call; in a --mcp-fcfs launch your first act takes control at once and you keep it "
        "until you release it or the user takes it back. Reads never need control.",
        "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}", ToolAccess::read_only));
    specs.push_back(built_in(
        "close", "Close",
        "Quits the program: every program window is asked to close, as the user's quit does. An act: it takes "
        "control first as any act does in this launch (at once under --mcp-fcfs; under --mcp-shared the user may "
        "refuse). A question the program raises (save changes?) is yours and never shown: with no on_unsaved "
        "nothing closes and the reply names it; on_unsaved save or discard answers it. Unlike disconnect, which "
        "ends your session (and, with mode hard, closes), close is about the program.",
        object_schema(on_unsaved_property), ToolAccess::acting));
    specs.push_back(built_in(
        "disconnect", "Disconnect",
        "Ends your session and closes the program cleanly. An act: it takes control first as any act does in "
        "this launch (under --mcp-shared the user may refuse, and the program stays open). The usual path: save "
        "your work with the program's save tool, then disconnect. mode soft (the default) never closes over "
        "unsaved work and never shows anything: if the program asks to save, nothing closes, and the reply names "
        "what to save; save it, then disconnect again. mode hard ends the session regardless: control returns to "
        "the user and the program's own save question is shown to the user, or, with discard_unsaved true, "
        "unsaved changes are discarded and nothing is asked.",
        "{\"type\":\"object\",\"properties\":{\"mode\":{\"type\":\"string\",\"enum\":[\"soft\",\"hard\"]},"
        "\"discard_unsaved\":{\"type\":\"boolean\"}},\"additionalProperties\":false}",
        ToolAccess::acting));
}

bool built_in_name(std::string_view name) noexcept {
    const bool known = name == "windows" || name == "snapshot" || name == "act" || name == "pointer" ||
                       name == "keyboard" || name == "text" || name == "wait" || name == "capture" ||
                       name == "inspect" || name == "batch" || name == "release_control" || name == "find" ||
                       name == "reset" || name == "disconnect" || name == "close";
    return known;
}

// A command's tool name: "command_" and its stable ID, every character
// outside the tool alphabet ('.' included, round 2: clients rename dotted
// names) as '_', cut to the longest name.
std::string command_tool_name(std::string_view stable_id) {
    std::string name(command_tool_prefix);
    for (const char character : stable_id) {
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        const bool punctuation = character == '_' || character == '-';
        name += letter || digit || punctuation ? character : '_';
    }
    if (name.size() > maximum_tool_name_bytes) name.resize(maximum_tool_name_bytes);
    return name;
}

// The live command named by `stable_id`, or null. A borrow for the current
// call (command_directory.hpp).
Command* find_command(std::string_view stable_id) {
    std::vector<Command*> commands{};
    gui_forms::detail::fill_live_commands(commands);
    for (Command* const command : commands) {
        if ((*command).is_disposed()) continue;
        if ((*command).stable_id() == stable_id) return command;
    }
    return nullptr;
}

// The command whose tool name is `tool`, or null.
Command* find_command_tool(std::string_view tool) {
    std::vector<Command*> commands{};
    gui_forms::detail::fill_live_commands(commands);
    for (Command* const command : commands) {
        if ((*command).is_disposed()) continue;
        if (command_tool_name((*command).stable_id()) == tool) return command;
    }
    return nullptr;
}

// ---- the dialog rule (study 9.2) ------------------------------------------------------

struct OpenDialog final {
    bool open{};
    bool drivable{};
    std::string id{};
    // The dialog's window, for a dialog that is a window of its own.
    Window* window{};
};

// Whether a dialog holds the program: a GUI.Forms window the others are
// modal under, a modal dialog node inside `window`, or an operating
// system dialog (every window modal under something that is not ours).
OpenDialog find_open_dialog(AutomationCore& core, Window& window, std::span<const FlatNode> nodes) {
    OpenDialog dialog{};
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    // A window run modally is the dialog (round 2: Plan Paint's Resize
    // dialog is a modal window whose root is a plain group, and a host may
    // not report its owner blocked). The newest modal window is the one the
    // user faces.
    for (std::size_t index = windows.size(); index > 0U; --index) {
        Window* const candidate = windows[index - 1U];
        if (!gui_forms::detail::shown_modally(*candidate)) continue;
        dialog.open = true;
        dialog.window = candidate;
        dialog.id = core.window_stable_id(*candidate);
        dialog.drivable = model_drivable(*candidate);
        return dialog;
    }
    bool blocked = false;
    Window* free_window = nullptr;
    for (Window* const candidate : windows) {
        const HostSession* const session = gui_forms::detail::window_session(*candidate);
        if (session == nullptr) continue;
        if ((*session).snapshot().modal_depth != 0U) {
            blocked = true;
        } else if (free_window == nullptr && !core.passive_popup_window(*candidate)) {
            // A tooltip's window is never the dialog: with only passive
            // popups free, the dialog is a native one.
            free_window = candidate;
        }
    }
    if (blocked) {
        dialog.open = true;
        if (free_window == nullptr) {
            // Every window waits on a dialog that is not ours: a native one.
            dialog.id = "native";
            return dialog;
        }
        dialog.window = free_window;
        dialog.id = core.window_stable_id(*free_window);
        dialog.drivable = model_drivable(*free_window);
        return dialog;
    }
    for (const FlatNode& node : nodes) {
        if (node.role != SemanticRole::dialog || !node.modal) continue;
        dialog.open = true;
        dialog.id = node.stable_id.empty() ? detail::node_reference(node) : node.stable_id;
        dialog.drivable = model_drivable(window);
        dialog.window = &window;
        return dialog;
    }
    // While a modal layer's focus scope holds the window, the semantic
    // snapshot is rooted at the scope's root, below the dialog node (and
    // may name no control at all, an empty dialog panel): the dialog is
    // found among the scope root and its ancestors.
    for (Control::Ptr ancestor = window.active_focus_scope_root(); ancestor != nullptr;
         ancestor = (*ancestor).parent()) {
        const SemanticDescriptor descriptor = (*ancestor).semantic_descriptor();
        if (descriptor.role != SemanticRole::dialog || !descriptor.modal || !descriptor.exposed) continue;
        dialog.open = true;
        const std::string_view id = (*ancestor).stable_id().value();
        dialog.id = id.empty() ? std::string("dialog") : std::string(id);
        dialog.drivable = model_drivable(window);
        dialog.window = &window;
        return dialog;
    }
    return dialog;
}

// ---- performing an act ------------------------------------------------------------------

// Delivers `payload` through the window's session (the user's own door,
// study 8.1), or straight to the window when no host is attached (a bare
// window in a test).
bool deliver_input(Window& window, HostEventPayload payload, std::uint64_t timestamp) {
    HostSession* const session = gui_forms::detail::window_session(window);
    if (session != nullptr) {
        const HostDispatchResult result = (*session).dispatch_model_input(std::move(payload), timestamp);
        return result.accepted();
    }
    if (PointerEvent* const pointer = std::get_if<PointerEvent>(&payload)) {
        static_cast<void>(window.dispatch_pointer(*pointer));
        return true;
    }
    if (KeyEvent* const key = std::get_if<KeyEvent>(&payload)) {
        static_cast<void>(window.dispatch_key(*key));
        return true;
    }
    if (TextInputEvent* const text = std::get_if<TextInputEvent>(&payload)) {
        static_cast<void>(window.dispatch_text(*text));
        return true;
    }
    return false;
}

// Escape pressed and released: how a user cancels a dialog, and how a model
// closes one it opened (it never submits one).
void append_cancel_keys(std::vector<HostEventPayload>& events) {
    KeyEvent down{};
    down.action = KeyAction::down;
    down.physical_key = PhysicalKey::escape;
    events.push_back(down);
    KeyEvent up = down;
    up.action = KeyAction::up;
    events.push_back(up);
}

// Readies `window` for the cancel keys: a tooltip takes Escape as its own
// dismissal, and a window with no focused control (a dialog the program
// opened while another application was frontmost) routes keys nowhere
// (round 3: the Resize dialog never saw the model's Escape). Focuses the
// first control that takes focus, breadth first.
void ready_for_cancel_keys(Window& window) {
    if (window.tool_tip_visible()) window.hide_tool_tip();
    if (window.focused_control()) return;
    const Control::Ptr root = window.root();
    std::vector<Control::Ptr> work{};
    if (root) work.push_back(root);
    for (std::size_t next = 0U; next < work.size(); ++next) {
        const Control::Ptr current = work[next];
        if (current != root && window.request_focus(current)) return;
        for (std::size_t index = 0U; index < (*current).child_count(); ++index) {
            Control::Ptr child = (*current).child_at(index);
            if (child) work.push_back(std::move(child));
        }
    }
}

// Performs a prepared act on the UI thread, at its own dispatcher turn.
// Returns whether the window took it; `detail` says why not.
bool perform_act(Window& window, const PreparedAct& act, const ClientIdentity& client, ActOutcome& outcome) {
    switch (act.kind) {
    case ActKind::semantic: {
        const bool performed = window.perform_semantic_action(RuntimeId{act.control_runtime_id}, act.virtual_child,
                                                              act.action, act.value);
        if (!performed) outcome.detail = "the window refused the action (outside the focus scope, or stale)";
        return performed;
    }
    case ActKind::command: {
        Command* const command = find_command(act.command_id);
        if (command == nullptr) {
            outcome.detail = "the command is gone";
            return false;
        }
        const bool performed = (*command).execute_routed(window, "mcp");
        if (!performed) outcome.detail = "the command is disabled or its routing refused it";
        return performed;
    }
    case ActKind::input: {
        const std::uint64_t dropped_before = gui_forms::detail::model_events_dropped(window);
        if (act.focus_first &&
            !window.perform_semantic_action(RuntimeId{act.control_runtime_id}, act.virtual_child,
                                            SemanticAction::focus)) {
            outcome.detail = "the target could not take focus";
            return false;
        }
        for (const HostEventPayload& event : act.events) {
            std::uint64_t timestamp = 0U;
            if (const PointerEvent* const pointer = std::get_if<PointerEvent>(&event)) {
                timestamp = (*pointer).timestamp_nanoseconds;
            }
            if (!deliver_input(window, event, timestamp)) {
                outcome.detail = "the window's session refused the input (closing, or the event was invalid)";
                return false;
            }
        }
        if (gui_forms::detail::model_events_dropped(window) != dropped_before) {
            // The user pressed while the model's button was held: the user
            // won (first come, first served) and the gesture was cancelled.
            outcome.reply = ToolReply::failure(
                ToolError::user_took_control,
                "the user pressed while the model's gesture was held: the gesture was cancelled and the rest of it "
                "did not reach the program",
                "take a snapshot; the user acted first");
            return false;
        }
        return true;
    }
    case ActKind::selection: {
        const SemanticTextResult result =
            window.set_semantic_text_selection(RuntimeId{act.control_runtime_id}, act.selection);
        if (result != SemanticTextResult::done) {
            outcome.detail = std::string("the selection was refused: ") + semantic_text_result_name(result);
            return false;
        }
        return true;
    }
    case ActKind::close_window: {
        const HostServiceStatus asked = act.window_handle.request_close();
        if (!asked.accepted()) outcome.detail = "the window refused the close request";
        return asked.accepted();
    }
    case ActKind::close_windows: {
        // Copied: a close may end the run that holds the act.
        const std::vector<ApplicationWindowHandle> handles = act.window_handles;
        bool asked_all = true;
        for (const ApplicationWindowHandle& handle : handles) {
            if (!handle.active()) continue;
            if (!handle.request_close().accepted()) asked_all = false;
        }
        if (!asked_all) outcome.detail = "a window refused the close request";
        return asked_all;
    }
    case ActKind::reveal: {
        const Control::Ptr target = window.find(act.command_id);
        if (!target) {
            outcome.detail = "the control is gone";
            return false;
        }
        // Every scrolling ancestor brings it into view, the nearest first,
        // so a list inside a scrolled page is reached as well.
        for (Control::Ptr ancestor = (*target).parent(); ancestor != nullptr; ancestor = (*ancestor).parent()) {
            ScrollableControl* const scroller = dynamic_cast<ScrollableControl*>(ancestor.get());
            if (scroller != nullptr) (*scroller).scroll_control_into_view(target);
        }
        return true;
    }
    case ActKind::registry: {
        ModelToolCall invocation{};
        invocation.arguments_json = act.arguments_json.data();
        invocation.arguments_bytes = act.arguments_json.size();
        invocation.client_name = client.client_name.c_str();
        invocation.model_name = client.model_name.c_str();
        invocation.window = &window;
        ToolReply reply = call_tool_handler(act.entry, invocation);
        const bool performed = reply.succeeded();
        outcome.reply.emplace(std::move(reply));
        return performed;
    }
    }
    outcome.detail = "unknown act";
    return false;
}

// The posted work of an act (study 14: the core never waits for an act's
// call to return, so a handler's modal loop cannot hold the call). Holds
// the window weakly: a window closed first runs nothing.
// After a close act: a native host posts each close request to the UI
// thread, after the act, so the call waits until those have run before its
// questions end. Posted twice in turn: the second post comes after a wake
// asked for once the first ran, so after every close request the act
// posted, even when the first shared a wake the act's turn already held.
struct CloseBarrier final {
    WindowRef window{};
    std::shared_ptr<ActOutcome> outcome{};
    int hops{};

    void operator()() const {
        Window* const target = window.get();
        if (hops > 0 && target != nullptr) {
            try {
                if ((*target).begin_invoke(CloseBarrier{window, outcome, hops - 1}).state() ==
                    DispatchOperationState::pending) {
                    return;
                }
            } catch (...) {
                // A window that takes no more work has run its closes.
            }
        }
        (*outcome).closes_delivered = true;
    }
};

// Posts the barrier; a window that closed with the act (a close a headless
// host answers at once) takes no more work, and its closes have run.
void post_close_barrier(const WindowRef& window, const std::shared_ptr<ActOutcome>& outcome) noexcept {
    Window* const target = window.get();
    try {
        if (target != nullptr &&
            (*target).begin_invoke(CloseBarrier{window, outcome, 1}).state() == DispatchOperationState::pending) {
            return;
        }
    } catch (...) {
        // Refused by a closed window, as below.
    }
    (*outcome).closes_delivered = true;
}

struct PostedAct final {
    WindowRef window{};
    PreparedAct act{};
    ClientIdentity client{};
    std::shared_ptr<ActOutcome> outcome{};

    void operator()() const {
        (*outcome).ran = true;
        Window* const target = window.get();
        if (target == nullptr) {
            (*outcome).detail = "the window closed";
            return;
        }
        try {
            (*outcome).performed = perform_act(*target, act, client, *outcome);
            if ((*outcome).performed && (act.kind == ActKind::close_window || act.kind == ActKind::close_windows)) {
                post_close_barrier(window, outcome);
            }
        } catch (const std::exception& failure) {
            (*outcome).performed = false;
            (*outcome).detail = std::string("the act failed: ") + failure.what();
        }
    }
};

// ---- small parsers --------------------------------------------------------------------

bool read_point(const JsonValue& object, Point& point) {
    double x = 0.0;
    double y = 0.0;
    if (!detail::point_value(object, x, y)) return false;
    point = Point{x, y};
    return true;
}

bool read_modifiers(const JsonValue& arguments, Modifier& modifiers, std::string& error) {
    modifiers = Modifier::none;
    const JsonValue* const list = detail::kind_member(arguments, "modifiers", JsonKind::array);
    if (list == nullptr) return true;
    for (const JsonValue& item : (*list).items) {
        Modifier modifier = Modifier::none;
        if (item.kind != JsonKind::string || !detail::parse_modifier(item.text, modifier)) {
            error = "modifiers are shift, ctrl, alt and meta";
            return false;
        }
        modifiers = modifiers | modifier;
    }
    return true;
}

PointerEvent pointer_event(PointerAction action, PointerButton button, Point position, Modifier modifiers,
                           std::uint64_t timestamp, std::uint32_t clicks) {
    PointerEvent event{};
    event.action = action;
    event.button = button;
    event.position = position;
    event.modifiers = modifiers;
    event.timestamp_nanoseconds = timestamp;
    event.click_count = clicks;
    // The model's own pointer, apart from the user's (session_directory.hpp).
    event.pointer_id = gui_forms::detail::model_pointer_id;
    return event;
}

std::string window_list_line(AutomationCore& core) {
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    std::string line{};
    for (Window* const window : windows) {
        // Tooltip windows are no window a model names (as windows lists).
        if (core.passive_popup_window(*window)) continue;
        if (!line.empty()) line += ' ';
        line += "[" + core.window_stable_id(*window) + "]";
    }
    return line;
}

} // namespace

// ---- the core -----------------------------------------------------------------------------

struct CoreRouteAccess final {
    static void record(AutomationCore& core, AutomationCore::RoutedEvent event) {
        ++core.route_sequence_;
        event.sequence = core.route_sequence_;
        if (core.route_trace_.size() < AutomationCore::route_trace_capacity) {
            core.route_trace_.push_back(std::move(event));
            return;
        }
        core.route_trace_[core.route_next_] = std::move(event);
        core.route_next_ = (core.route_next_ + 1U) % AutomationCore::route_trace_capacity;
    }

    // Tells the presenter where the pointer is in an attached window (the
    // tag line on hover); the presenter's own windows are never attached.
    static void pointer_seen(AutomationCore& core, const SurfaceRoot& window, PointerAction action,
                             Point position) noexcept {
        if (action != PointerAction::move && action != PointerAction::leave) return;
        AutomationPresenter* const presenter = core.control_.presenter();
        if (presenter == nullptr) return;
        for (AutomationWindow& entry : core.windows_) {
            Window* const attached = entry.window.get();
            if (attached == nullptr || static_cast<const SurfaceRoot*>(attached) != &window) continue;
            (*presenter).pointer_moved(*attached, position, action == PointerAction::leave);
            return;
        }
    }
};

namespace {

// The route observer (session_directory.hpp): records into the core.
void observe_route(void* context, const SurfaceRoot& window, const gui_forms::detail::RouteRecord& record) noexcept {
    AutomationCore& core = *static_cast<AutomationCore*>(context);
    try {
        AutomationCore::RoutedEvent event{};
        event.window = &window;
        event.pointer = record.kind == gui_forms::detail::RouteKind::pointer;
        event.action = record.action;
        event.code = record.code;
        event.position = Point{record.x, record.y};
        event.target_stable_id = record.target_stable_id;
        event.target_control_id = record.target_control_id;
        event.handled = record.handled;
        const bool pointer = event.pointer;
        const Point position = event.position;
        CoreRouteAccess::record(core, std::move(event));
        if (pointer) {
            CoreRouteAccess::pointer_seen(core, window, static_cast<PointerAction>(record.action), position);
        }
    } catch (...) {
        // Out of memory: the trace misses one event.
    }
}

const char* pointer_action_text(std::uint8_t action) noexcept {
    switch (static_cast<PointerAction>(action)) {
    case PointerAction::move: return "move";
    case PointerAction::down: return "down";
    case PointerAction::up: return "up";
    case PointerAction::wheel: return "wheel";
    case PointerAction::enter: return "enter";
    case PointerAction::leave: return "leave";
    }
    return "pointer";
}

// inspect's "events" section: the window's routed events, those of the
// target when one is named.
void events_section(void* context, const InspectRequest& request, std::string& text) {
    const AutomationCore& core = *static_cast<const AutomationCore*>(context);
    if (request.window == nullptr) return;
    const std::string_view target = request.node != nullptr ? std::string_view((*request.node).stable_id)
                                                            : std::string_view{};
    core.append_route_trace(*request.window, target, text);
}

} // namespace


void AutomationCore::append_route_trace(const SurfaceRoot& window, std::string_view target_stable_id,
                                        std::string& text) const {
    std::size_t shown = 0U;
    const std::size_t count = route_trace_.size();
    const std::size_t first = count < route_trace_capacity ? 0U : route_next_;
    text += "events (oldest first, as each window's route delivered them):\n";
    for (std::size_t step = 0U; step < count; ++step) {
        const RoutedEvent& event = route_trace_[(first + step) % count];
        if (event.window != &window) continue;
        if (!target_stable_id.empty() && event.target_stable_id != target_stable_id) continue;
        text += "  #" + std::to_string(event.sequence) + " ";
        if (event.pointer) {
            text += std::string("pointer ") + pointer_action_text(event.action) + " button " +
                    std::to_string(event.code) + " at " + std::to_string(event.position.x) + "," +
                    std::to_string(event.position.y);
        } else {
            text += std::string("key ") + (event.action == 0U ? "down" : "up") + " physical " +
                    std::to_string(event.code);
        }
        text += " -> ";
        text += event.target_stable_id.empty() ? (event.target_control_id.empty() ? std::string("(no target)")
                                                                                 : "(" + event.target_control_id + ")")
                                               : "[" + event.target_stable_id + "]";
        text += event.handled ? " handled\n" : " not handled\n";
        ++shown;
    }
    if (shown == 0U) text += "  none recorded\n";
}

AutomationCore::AutomationCore(AutomationControl& control, AutomationSettings settings)
    : control_(control), settings_(settings) {
    detail::register_core_inspect_sections(tools());
    static_cast<void>(register_model_inspect_sections(tools()));
    static_cast<void>(tools().add_inspect_section(
        "events", "the pointer and key events the window routed, and where each went (the event-propagation trace)",
        &string_section<&events_section>, this));
    gui_forms::detail::install_route_observer(&observe_route, this);
}

AutomationCore::~AutomationCore() {
    gui_forms::detail::install_route_observer(nullptr, nullptr);
    // The events section reads this core; the built-in sections read none
    // and stay registered for the next core.
    InspectSectionEntry events{};
    if (tools().find_inspect_section("events", events) && events.context == this) {
        static_cast<void>(tools().remove_inspect_section("events"));
    }
    if (AutomationPresenter* const presenter = control_.presenter()) {
        for (AutomationWindow& entry : windows_) {
            if (Window* const window = entry.window.get()) (*presenter).window_detached(*window);
        }
    }
}

bool AutomationCore::attach_window(Window& window, std::string stable_id, const bool passive_popup,
                                   ApplicationWindowHandle handle) {
    AutomationPresenter* const presenter = control_.presenter();
    if (presenter != nullptr && (*presenter).presenter_window(window)) return false;
    prune_windows();
    for (const AutomationWindow& entry : windows_) {
        if (entry.window.get() == &window) return true;
    }
    if (stable_id.empty()) {
        stable_id = "window." + std::to_string(next_window_number_);
        ++next_window_number_;
    }
    windows_.push_back(AutomationWindow{window.window_ref(), std::move(stable_id), passive_popup, std::move(handle)});
    // A tooltip's or a passive popup's window carries no sign (round 2:
    // the band and veil belong to the program's own windows).
    if (presenter != nullptr && !passive_popup) (*presenter).window_attached(window);
    return true;
}

void AutomationCore::detach_window(const Window& window) noexcept {
    for (std::size_t index = 0U; index < windows_.size(); ++index) {
        Window* const live = windows_[index].window.get();
        if (live != &window) continue;
        windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(index));
        if (AutomationPresenter* const presenter = control_.presenter()) (*presenter).window_detached(*live);
        return;
    }
}

void AutomationCore::prune_windows() noexcept {
    // The presenter's own windows (its dialogs, the console and its button)
    // are never the model's, however they came to join: they are dropped
    // here, so no tool lists, counts or finds them (1.0.5 console review:
    // windows counted the console's button as a popup).
    const AutomationPresenter* const presenter = control_.presenter();
    std::size_t index = 0U;
    while (index < windows_.size()) {
        const Window* const window = windows_[index].window.get();
        if (window == nullptr || (presenter != nullptr && (*presenter).presenter_window(*window))) {
            windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        ++index;
    }
}

void AutomationCore::fill_windows(std::vector<Window*>& windows) {
    prune_windows();
    windows.clear();
    for (const AutomationWindow& entry : windows_) windows.push_back(entry.window.get());
}

Window* AutomationCore::find_window(std::string_view name) {
    prune_windows();
    if (windows_.empty()) return nullptr;
    if (name.empty()) {
        // The default is a program window, never a tooltip's or a passive
        // popup's (MCP round 3: after the first act a hidden 1x1 tooltip
        // window was active, and snapshot {} read it instead of [main]).
        // The active program window first, else the newest modal one (the
        // dialog waiting on its answer), else the first.
        Window* modal = nullptr;
        Window* first = nullptr;
        for (const AutomationWindow& entry : windows_) {
            Window* const window = entry.window.get();
            if (entry.passive_popup) continue;
            if (window_active(*window)) return window;
            if (first == nullptr) first = window;
            if (gui_forms::detail::shown_modally(*window)) modal = window;
        }
        if (modal != nullptr) return modal;
        if (first != nullptr) return first;
        return windows_.front().window.get();
    }
    for (const AutomationWindow& entry : windows_) {
        if (entry.stable_id == name) return entry.window.get();
    }
    for (const AutomationWindow& entry : windows_) {
        Window* const window = entry.window.get();
        if ((*window).text.read() == name) return window;
    }
    return nullptr;
}

bool AutomationCore::window_active(const Window& window) const {
    if (window.active()) return true;
    // A passive popup of this window (a tooltip, ID "<window>/tool-tip.N")
    // holding the activation leaves the window itself the active one to a
    // model, which never names the popup.
    std::string owner_prefix{};
    for (const AutomationWindow& entry : windows_) {
        if (entry.window.get() == &window) {
            if (entry.passive_popup) return false;
            owner_prefix = entry.stable_id + "/";
        }
    }
    if (owner_prefix.size() < 2U) return false;
    for (const AutomationWindow& entry : windows_) {
        const Window* const popup = entry.window.get();
        if (!entry.passive_popup || popup == nullptr || !(*popup).active()) continue;
        if (entry.stable_id.compare(0U, owner_prefix.size(), owner_prefix) == 0) return true;
    }
    return false;
}

bool AutomationCore::passive_popup_window(const Window& window) const noexcept {
    for (const AutomationWindow& entry : windows_) {
        if (entry.window.get() == &window) return entry.passive_popup;
    }
    return false;
}

void AutomationCore::program_window_handles(std::vector<ApplicationWindowHandle>& handles) const {
    for (const AutomationWindow& entry : windows_) {
        if (!entry.passive_popup && entry.handle.active()) handles.push_back(entry.handle);
    }
}

void AutomationCore::drop_model_work() noexcept {
    // The calls themselves run on to their replies (a disconnect's own call
    // runs this); only their question scopes close, so what the program
    // asks from here is the user's.
    for (const std::unique_ptr<RunningCall>& call : calls_) (*call).questions_open = false;
    std::vector<gui_forms::detail::ModelQuestion> asked{};
    gui_forms::detail::end_model_questions(0U, asked);
}

void AutomationCore::end_session() noexcept {
    ended_ = true;
    drop_model_work();
    control_.disconnect();
    if (AutomationPresenter* const presenter = control_.presenter()) (*presenter).disconnected();
}

void AutomationCore::begin_session(FrameTime now) {
    if (!ended_) return;
    ended_ = false;
    control_.begin_session(now);
}

void AutomationCore::hard_disconnect() noexcept {
    end_session();
    try {
        std::vector<ApplicationWindowHandle> handles{};
        program_window_handles(handles);
        for (const ApplicationWindowHandle& handle : handles) {
            if (handle.active()) static_cast<void>(handle.request_close());
        }
    } catch (...) {
        // A window that cannot be asked closes with the run.
    }
}

ApplicationWindowHandle AutomationCore::window_handle(const Window& window) const {
    for (const AutomationWindow& entry : windows_) {
        if (entry.window.get() == &window) return entry.handle;
    }
    return ApplicationWindowHandle{};
}

std::string AutomationCore::window_stable_id(const Window& window) const {
    for (const AutomationWindow& entry : windows_) {
        if (entry.window.get() == &window) return entry.stable_id;
    }
    return std::string{};
}

void AutomationCore::set_client(const ClientIdentity& client) {
    control_.set_client(client);
}

namespace {

// The runtime IDs of a window's default and cancel buttons (FlatNode::
// default_button, cancel_button), walking its tree with a work list.
void find_dialog_buttons(Window& window, std::vector<std::uint64_t>& defaults, std::vector<std::uint64_t>& cancels) {
    const Control::Ptr accept = window.accept_button();
    const Control::Ptr cancel = window.cancel_button();
    if (accept) defaults.push_back((*accept).runtime_id().value);
    if (cancel) cancels.push_back((*cancel).runtime_id().value);
    std::vector<Control::Ptr> work{};
    const Control::Ptr root = window.root();
    if (root) work.push_back(root);
    while (!work.empty()) {
        const Control::Ptr control = std::move(work.back());
        work.pop_back();
        if (const Button* const button = dynamic_cast<const Button*>(control.get())) {
            if ((*button).default_button.read()) defaults.push_back((*button).runtime_id().value);
            if ((*button).dialog_result.read() == DialogResult::cancel) cancels.push_back((*button).runtime_id().value);
        }
        for (std::size_t index = 0U; index < (*control).child_count(); ++index) {
            Control::Ptr child = (*control).child_at(index);
            if (child) work.push_back(std::move(child));
        }
    }
}

} // namespace

void AutomationCore::flatten_window(Window& window, std::vector<FlatNode>& nodes) {
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const InteractionSnapshot interaction = window.interaction_snapshot();
    detail::flatten_snapshot(snapshot, interaction, nodes);
    std::vector<std::uint64_t> defaults{};
    std::vector<std::uint64_t> cancels{};
    find_dialog_buttons(window, defaults, cancels);
    if (defaults.empty() && cancels.empty()) return;
    for (FlatNode& node : nodes) {
        if (node.is_virtual) continue;
        node.default_button = std::find(defaults.begin(), defaults.end(), node.runtime_id) != defaults.end();
        node.cancel_button = std::find(cancels.begin(), cancels.end(), node.runtime_id) != cancels.end();
    }
}

std::uint64_t AutomationCore::tool_generation() const {
    const std::uint64_t generation = tools().generation() + gui_forms::detail::command_directory_generation();
    return generation;
}

void AutomationCore::fill_tool_specs(std::vector<ToolSpec>& specs) {
    specs.clear();
    std::vector<InspectSectionEntry> sections{};
    tools().fill_inspect_sections(sections);
    append_built_in_specs(specs, sections);
    std::vector<ToolSpec> registered{};
    tools().fill_specs(registered);
    for (ToolSpec& spec : registered) specs.push_back(std::move(spec));
    std::vector<Command*> commands{};
    gui_forms::detail::fill_live_commands(commands);
    for (Command* const command : commands) {
        if ((*command).is_disposed() || (*command).stable_id().empty()) continue;
        if (detail::command_hidden_mark((*command).stable_id())) continue;
        // The spec depends only on what does not change as the program runs
        // (round 2: a spec that named "disabled now" or followed visibility
        // changed with every state change, and each change rebound the tool
        // and sent tools/list_changed). Whether it is enabled now is the
        // call's answer.
        const CommandState& state = (*command).state();
        const std::string name = command_tool_name((*command).stable_id());
        // Commands sharing a stable ID (a menu's Save and the Quick Access
        // Toolbar's) are one tool: a second spec of the name failed to bind
        // (round 2).
        bool listed = false;
        for (const ToolSpec& served : specs) listed = listed || served.name == name;
        if (listed) continue;
        // A command's text may change as it runs ("Undo Draw"); its first
        // spec stands for the launch.
        bool cached = false;
        for (const ToolSpec& known : command_specs_) {
            if (known.name != name) continue;
            specs.push_back(known);
            cached = true;
            break;
        }
        if (cached) continue;
        ToolSpec spec{};
        spec.name = name;
        spec.title = state.text;
        spec.description = "The program's command \"" + state.text + "\"";
        if (!state.description.empty()) spec.description += ": " + state.description;
        if (!state.shortcut.empty()) spec.description += " (" + state.shortcut + ")";
        spec.description += ". A question it raises (save changes?) is yours and never shown: with no on_unsaved "
                            "the call stops there and replies with it; save first, or pass on_unsaved";
        spec.input_schema = object_schema(on_unsaved_property);
        spec.access = ToolAccess::acting;
        spec.destructive = state.destructive;
        spec.settles = true;
        command_specs_.push_back(spec);
        specs.push_back(std::move(spec));
    }
}

// The input schema of the tool named `tool`, into `schema`: a built-in
// tool's, a program tool's or a command's; false for no such tool.
bool tool_schema(std::string_view tool, std::string& schema) {
    if (built_in_name(tool)) {
        std::vector<InspectSectionEntry> sections{};
        AutomationCore::tools().fill_inspect_sections(sections);
        std::vector<ToolSpec> specs{};
        append_built_in_specs(specs, sections);
        for (const ToolSpec& spec : specs) {
            if (spec.name != tool) continue;
            schema = spec.input_schema;
            return true;
        }
        return false;
    }
    if (tool.compare(0U, command_tool_prefix.size(), command_tool_prefix) == 0 && find_command_tool(tool) != nullptr) {
        schema = object_schema(std::string_view{});
        return true;
    }
    ToolEntry entry{};
    if (!AutomationCore::tools().find(tool, entry)) return false;
    schema = entry.spec.input_schema;
    return true;
}

// The members an expect step takes.
bool expect_member(std::string_view name, bool visual) noexcept {
    const bool member = name == "expect" || name == "window" || name == "target" || name == "within" ||
                        name == "state" || name == "value" || name == "exists" || (visual && name == "drawn");
    return member;
}

// Whether `target` names a row of the list `list` by the list's own
// numbering ("<list>.<part>..."): the longest such list wins.
bool names_a_row_of(std::string_view target, std::string_view list) noexcept {
    const bool row = !list.empty() && target.size() > list.size() + 1U && target.substr(0U, list.size()) == list &&
                     target[list.size()] == '.';
    return row;
}

// ---- running calls ---------------------------------------------------------------------------

// The destructive-act question could not be shown: said truthfully (round
// 2), never "the user did not allow".
StepOutcome consent_not_shown(const AutomationControl& control) {
    const std::string why = control.consent_failure().empty() ? std::string("it could not be shown")
                                                              : control.consent_failure();
    return StepOutcome::finished(ToolReply::failure(ToolError::declined,
                                                    "the destructive-act question was not shown: " + why,
                                                    "the act did not run; the next destructive act asks again"));
}

struct CoreAccess final {
    AutomationCore& core;
    using RunningCall = AutomationCore::RunningCall;

    // The window a call names (its "window" argument), or null with
    // `reply` set.
    Window* call_window(RunningCall& call, ToolReply& reply) {
        const std::optional<std::string> name = detail::text_member(call.arguments, "window");
        Window* window = core.find_window(name.has_value() ? *name : std::string{});
        if (!name.has_value()) {
            // A target that is a window's own ID (a modal window's dialog)
            // names its window (round 3: act close on [editor-dialog-1]
            // went to the main window and was refused as dialog_open).
            const std::optional<std::string> target = detail::text_member(call.arguments, "target");
            Window* const named = target.has_value() ? core.find_window(*target) : nullptr;
            if (named != nullptr && core.window_stable_id(*named) == *target) window = named;
        }
        if (window == nullptr) {
            const std::string known = window_list_line(core);
            reply = ToolReply::failure(ToolError::window_gone,
                                       name.has_value() ? "no window [" + *name + "]" : "no window is open",
                                       known.empty() ? std::string{} : "windows: " + known);
            return nullptr;
        }
        call.window = (*window).window_ref();
        return window;
    }

    StepOutcome first_step(RunningCall& call, FrameTime now);
    StepOutcome prepare_act(RunningCall& call, Window& window, FrameTime now);
    StepOutcome prepare_pointer(RunningCall& call, Window& window, FrameTime now);
    StepOutcome prepare_keyboard(RunningCall& call, Window& window, FrameTime now);
    StepOutcome prepare_selection(RunningCall& call, Window& window, FrameTime now);
    StepOutcome prepare_command(RunningCall& call, Window& window, Command& command, FrameTime now);
    StepOutcome prepare_wait(RunningCall& call, Window& window, FrameTime now);
    StepOutcome prepare_batch(RunningCall& call, FrameTime now);
    StepOutcome release_control_tool();
    bool wait_reached(Window& window, const WaitSpec& wait, FrameTime now, std::string& busy,
                      std::string& observed);
    StepOutcome disconnect_tool(RunningCall& call, FrameTime now);
    StepOutcome close_tool(RunningCall& call, FrameTime now);
    // Whether the call acts (and so needs control), and the take step:
    // finished (refused, or still waiting) unless it set control_checked.
    bool needs_control(const RunningCall& call);
    StepOutcome control_gate(RunningCall& call, FrameTime now);
    StepOutcome windows_tool();
    StepOutcome find_tool(RunningCall& call, Window& window);
    StepOutcome reset_tool(RunningCall& call, FrameTime now);
    StepOutcome reset_wait(RunningCall& call, FrameTime now);
    // After an act: a dialog that it opened is the model's to close, never
    // to submit; the reply says so.
    void note_dialog_opened(RunningCall& call, Window& window, std::span<const FlatNode> after, std::string& text);
    // Whether `dialog` is open and the model's own (one its act opened).
    [[nodiscard]] bool model_dialog(const OpenDialog& dialog) const;
    StepOutcome registry_tool(RunningCall& call, const ToolEntry& entry, std::string_view arguments_json,
                              FrameTime now);

    // Resolves a target for an act; checks the dialog rule, user steps,
    // disabled and hidden. On a refusal fills `reply`.
    bool resolve_for_act(RunningCall& call, Window& window, std::vector<FlatNode>& nodes, bool required,
                         std::size_t& index, ToolReply& reply);
    // The consent step, then the posting of the act.
    StepOutcome consent_then_post(RunningCall& call, FrameTime now);
    StepOutcome post_act(RunningCall& call, FrameTime now);
    StepOutcome settle(RunningCall& call, FrameTime now);
    StepOutcome wait_step(RunningCall& call, FrameTime now);
    StepOutcome batch_step(RunningCall& call, FrameTime now);
    // Records a finished step of a batch in its transcript; true when the
    // batch ends with `finish` (a failure under stop_on_error, or the user
    // took control).
    bool record_step(RunningCall& call, std::size_t index, ToolReply& reply, ToolReply& finish);
    // Ends a batch, giving back a hold it owns.
    StepOutcome finish_batch(RunningCall& call, ToolReply reply);
    // A batch step's record in the structured transcript (round 2), and a
    // failed batch's reply: the failing step's line as the message, every
    // step so far as JSON detail ({"steps":[{"step","tool","ok","text"}]}).
    static void note_step(RunningCall& call, std::size_t index, std::string_view tool, bool ok,
                          std::string_view text);
    static ToolReply batch_failure(RunningCall& call, ToolError error, std::string message, std::string hint);
};

StepOutcome CoreAccess::windows_tool() {
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    if (windows.empty()) return StepOutcome::finished(ToolReply::failure(ToolError::window_gone, "no window is open"));
    std::string text{};
    for (Window* const window : windows) {
        // Only the program's real windows (round 2): tooltip and passive
        // popup windows are neither listed nor counted (1.0.5 console
        // review: the count told the model about windows it cannot use).
        if (core.passive_popup_window(*window)) continue;
        detail::append_window_line(core, *window, text);
        const HostSession* const session = gui_forms::detail::window_session(*window);
        if (session != nullptr && (*session).snapshot().modal_depth != 0U) {
            text.insert(text.size() - 1U, " (waiting on a modal window)");
        }
    }
    AutomationControl& control = core.control();
    text += "control: ";
    if (control.settings().appearance_review) {
        text += "appearance review: acts need no control";
    } else if (control.state() == ControlState::model_control) {
        text += control.settings().ai_led ? "you hold it (AI-led launch)"
                                          : "you hold it until release_control or 30 s without a call";
    } else if (control.state() == ControlState::asking) {
        text += "the user is being asked";
    } else {
        text += "the user has it; your first act asks them";
    }
    text += "\n";
    return StepOutcome::finished(ToolReply::success(std::move(text)));
}

void CoreAccess::note_dialog_opened(RunningCall& call, Window& window, std::span<const FlatNode> after,
                                    std::string& text) {
    const OpenDialog dialog = find_open_dialog(core, window, after);
    if (!dialog.open || dialog.id == call.dialog_before) return;
    if (dialog.drivable) {
        text += "a dialog opened: [" + dialog.id + "]; the program lets a model answer it\n";
        return;
    }
    if (dialog.id == "native") {
        text += "a system dialog opened: it needs the user, and no tool can close it; ask the user to answer it\n";
    } else {
        std::vector<std::string>& opened = core.model_dialogs_;
        if (std::find(opened.begin(), opened.end(), dialog.id) == opened.end()) opened.push_back(dialog.id);
        text += "a dialog opened: [" + dialog.id +
                "]; it needs the user: ask them to answer it, or close it as Cancel (act close on [" + dialog.id +
                "], or reset); a model never submits a dialog\n";
    }
    if (!call.act.dialog_tool.empty()) {
        text += "next time, the tool " + call.act.dialog_tool + " does this with arguments and no dialog\n";
    }
}

StepOutcome CoreAccess::find_tool(RunningCall& call, Window& window) {
    std::vector<FlatNode> nodes{};
    core.flatten_window(window, nodes);
    const std::optional<std::string> role = detail::text_member(call.arguments, "role");
    const std::optional<std::string> name = detail::text_member(call.arguments, "name");
    const std::optional<std::string> state = detail::text_member(call.arguments, "state");
    const std::optional<std::string> within = detail::text_member(call.arguments, "within");
    if (!role.has_value() && !name.has_value() && !state.has_value()) {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::invalid_arguments, "find names a role, a name or a state"));
    }
    if (role.has_value() && !detail::known_role_name(*role)) {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "no role " + *role,
                                                        "roles: " + detail::role_names()));
    }
    std::size_t first = 0U;
    std::size_t end = nodes.size();
    if (within.has_value()) {
        const detail::ResolvedTarget scope = detail::resolve_target(nodes, *within, std::string_view{});
        if (scope.resolution != detail::Resolution::found) {
            return StepOutcome::finished(ToolReply::failure(ToolError::not_found, "no single node [" + *within + "]",
                                                            scope.hint));
        }
        first = scope.index;
        end = scope.index + nodes[scope.index].subtree_size;
    }
    const std::optional<double> limit = detail::number_member(call.arguments, "limit");
    std::size_t maximum = core.settings().default_node_limit;
    if (limit.has_value() && *limit >= 1.0) {
        maximum = std::min(static_cast<std::size_t>(*limit), core.settings().maximum_node_limit);
    }
    std::string text{};
    std::size_t found = 0U;
    for (std::size_t index = first; index < end; ++index) {
        const FlatNode& node = nodes[index];
        if (role.has_value() && *role != semantic_role_name(node.role)) continue;
        if (name.has_value() && !detail::contains_any_case(node.name, *name)) continue;
        if (state.has_value()) {
            NodeCondition condition{};
            condition.state = *state;
            condition.target = node.stable_id.empty() ? "#r" + std::to_string(node.runtime_id) : node.stable_id;
            std::string observed{};
            std::vector<FlatNode> single(1U, node);
            single[0].parent = no_parent;
            if (!detail::condition_holds(single, condition, observed)) continue;
        }
        ++found;
        if (found > maximum) continue;
        detail::append_node_line(node, false, text);
        detail::append_node_actions(node, text);
        text += node.parent == no_parent ? std::string(" in the window") : " in " + detail::node_reference(nodes[node.parent]);
        text += '\n';
    }
    if (found == 0U) {
        return StepOutcome::finished(ToolReply::success("no node matches; a snapshot shows the whole tree"));
    }
    if (found > maximum) {
        text += "... " + std::to_string(found - maximum) + " more matches not shown (limit " + std::to_string(maximum) +
                ")\n";
    }
    return StepOutcome::finished(ToolReply::success(std::move(text)));
}

namespace {

// The controls of `control`'s subtree that are content surfaces whose
// content froze (stale), into `stale`. An explicit work list: no recursion.
void fill_stale_surfaces(const Control::Ptr& control, std::vector<std::shared_ptr<ContentSurface>>& stale) {
    std::vector<Control::Ptr> work{control};
    while (!work.empty()) {
        const Control::Ptr current = work.back();
        work.pop_back();
        if (!current) continue;
        std::shared_ptr<ContentSurface> surface = std::dynamic_pointer_cast<ContentSurface>(current);
        if (surface && (*surface).status() == ContentSurfaceStatus::stale) stale.push_back(std::move(surface));
        for (std::size_t index = 0U; index < (*current).child_count(); ++index) work.push_back((*current).child_at(index));
    }
}

} // namespace

StepOutcome CoreAccess::reset_tool(RunningCall& call, FrameTime now) {
    std::vector<Window*> windows{};
    const std::optional<std::string> named = detail::text_member(call.arguments, "window");
    if (named.has_value()) {
        ToolReply refusal{};
        Window* const window = call_window(call, refusal);
        if (window == nullptr) return StepOutcome::finished(std::move(refusal));
        windows.push_back(window);
    } else {
        core.fill_windows(windows);
    }
    std::string done{};
    std::string left{};
    for (Window* const window : windows) {
        const std::string window_id = core.window_stable_id(*window);
        std::vector<FlatNode> nodes{};
        core.flatten_window(*window, nodes);
        // Dialogs: the model's own close as Cancel; the user's stay.
        const OpenDialog dialog = find_open_dialog(core, *window, nodes);
        if (dialog.open && dialog.window == window && !dialog.drivable) {
            if (model_dialog(dialog)) {
                // Escape, as the user cancels; closed when the dialog is gone.
                ready_for_cancel_keys(*window);
                std::vector<HostEventPayload> keys{};
                append_cancel_keys(keys);
                const std::uint64_t pressed = detail::nanoseconds_of(now);
                for (const HostEventPayload& key : keys) static_cast<void>(deliver_input(*window, key, pressed));
                // Checked by reset_wait: a modal window closes once its
                // nested loop has ended.
                call.reset_windows.push_back(window_id);
                call.reset_dialogs.push_back(dialog.id);
            } else {
                left += "left the dialog [" + dialog.id + "]: the user opened it\n";
            }
        } else if (dialog.open && dialog.id == "native" && window == windows.front()) {
            left += "a system dialog is open: only the user can answer it\n";
        }
        // Popups and menus: what is expanded and opens a popup collapses,
        // the innermost first; whatever popup window is left closes as when
        // the window moves.
        core.flatten_window(*window, nodes);
        std::size_t collapsed = 0U;
        for (std::size_t index = nodes.size(); index > 0U; --index) {
            const FlatNode& node = nodes[index - 1U];
            if (!node.has_popup || !has_semantic_state(node.states, SemanticState::expanded)) continue;
            if (std::find(node.actions.begin(), node.actions.end(), SemanticAction::collapse) == node.actions.end()) {
                continue;
            }
            const bool performed = (*window).perform_semantic_action(
                RuntimeId{node.control_runtime_id}, node.is_virtual ? node.stable_id : std::string{},
                SemanticAction::collapse);
            if (performed) ++collapsed;
        }
        std::size_t popups = 0U;
        for (std::size_t index = 0U; index < Window::maximum_popup_windows; ++index) {
            if ((*window).shown_popup_window(index).model != nullptr) ++popups;
        }
        if (popups != 0U) (*window).report_frame_moved();
        if (collapsed + popups != 0U) {
            done += "closed " + std::to_string(collapsed + popups) + " popups and menus in [" + window_id + "]\n";
        }
        if ((*window).tool_tip_visible()) {
            (*window).hide_tool_tip();
            done += "hid the tooltip in [" + window_id + "]\n";
        }
        // Input left held: buttons up, capture released, a drag cancelled.
        const InteractionSnapshot interaction = (*window).interaction_snapshot();
        const std::uint64_t time = detail::nanoseconds_of(now);
        if (interaction.primary_down) {
            static_cast<void>(deliver_input(*window,
                                            pointer_event(PointerAction::up, PointerButton::primary,
                                                          interaction.pointer_position, Modifier::none, time, 1U),
                                            time));
            done += "released the held primary button in [" + window_id + "]\n";
        }
        if (interaction.secondary_down) {
            static_cast<void>(deliver_input(*window,
                                            pointer_event(PointerAction::up, PointerButton::secondary,
                                                          interaction.pointer_position, Modifier::none, time, 1U),
                                            time));
            done += "released the held secondary button in [" + window_id + "]\n";
        }
        if ((*window).captured_control()) {
            (*window).release_pointer();
            done += "released pointer capture in [" + window_id + "]\n";
        }
        (*window).cancel_drag();
        // Content that froze stops; its program may start it again.
        std::vector<std::shared_ptr<ContentSurface>> stale{};
        fill_stale_surfaces((*window).root(), stale);
        for (const std::shared_ptr<ContentSurface>& surface : stale) {
            (*surface).terminate();
            const std::string_view id = (*surface).stable_id().value();
            done += "stopped frozen content [" + (id.empty() ? std::string("content") : std::string(id)) + "] in [" +
                    window_id + "]\n";
        }
    }
    call.reset_done = std::move(done);
    call.reset_left = std::move(left);
    call.phase = CallPhase::resetting;
    call.deadline = now + core.settings().settle_limit;
    return reset_wait(call, now);
}

StepOutcome CoreAccess::reset_wait(RunningCall& call, FrameTime now) {
    for (std::size_t index = call.reset_dialogs.size(); index > 0U; --index) {
        const std::size_t at = index - 1U;
        Window* const window = core.find_window(call.reset_windows[at]);
        bool closed = window == nullptr;
        if (!closed) {
            std::vector<FlatNode> nodes{};
            core.flatten_window(*window, nodes);
            const OpenDialog after = find_open_dialog(core, *window, nodes);
            closed = !(after.open && after.id == call.reset_dialogs[at]);
        }
        if (!closed) continue;
        call.reset_done += "closed the dialog [" + call.reset_dialogs[at] + "] as Cancel\n";
        call.reset_windows.erase(call.reset_windows.begin() + static_cast<std::ptrdiff_t>(at));
        call.reset_dialogs.erase(call.reset_dialogs.begin() + static_cast<std::ptrdiff_t>(at));
    }
    if (!call.reset_dialogs.empty() && now < call.deadline) {
        return StepOutcome::resume_at(now + core.settings().poll_interval);
    }
    for (const std::string& dialog : call.reset_dialogs) {
        call.reset_left += "the dialog [" + dialog + "] did not close: ask the user to close it\n";
    }
    std::string text =
        call.reset_done.empty() ? std::string("ok: nothing needed resetting\n") : "ok: reset\n" + call.reset_done;
    text += call.reset_left;
    return StepOutcome::finished(ToolReply::success(std::move(text)));
}

bool CoreAccess::resolve_for_act(RunningCall& call, Window& window, std::vector<FlatNode>& nodes, bool required,
                                 std::size_t& index, ToolReply& reply) {
    core.flatten_window(window, nodes);
    index = no_parent;
    const OpenDialog dialog = find_open_dialog(core, window, nodes);
    if (dialog.open && (!dialog.drivable || (dialog.window != nullptr && dialog.window != &window))) {
        std::string hint = dialog.drivable ? "act in the dialog's window [" + dialog.id + "]"
                                           : "read it with snapshot and ask the user to answer it";
        if (!dialog.drivable && model_dialog(dialog)) {
            hint = "you opened it: close it as Cancel (act close on [" + dialog.id +
                   "], or reset); a model never submits a dialog";
        }
        reply = ToolReply::failure(ToolError::dialog_open, "a dialog is open: [" + dialog.id + "]", hint);
        return false;
    }
    const std::optional<std::string> target = detail::text_member(call.arguments, "target");
    if (!target.has_value()) {
        if (required) reply = ToolReply::failure(ToolError::invalid_arguments, call.tool + " names a target");
        return !required;
    }
    const std::optional<std::string> within = detail::text_member(call.arguments, "within");
    const detail::ResolvedTarget resolved =
        detail::resolve_target(nodes, *target, within.has_value() ? *within : std::string{});
    if (resolved.resolution == detail::Resolution::ambiguous) {
        reply = ToolReply::failure(ToolError::ambiguous, "[" + *target + "] names more than one node", resolved.hint);
        return false;
    }
    if (resolved.resolution == detail::Resolution::not_found) {
        // A row a virtualised list does not show: reached through the list
        // that numbers it (MCP round 1), when the act is one a row takes.
        const std::optional<std::string> action = detail::text_member(call.arguments, "action");
        const bool row_action = call.tool == "act" && action.has_value() &&
                                (*action == "press" || *action == "select" || *action == "focus" ||
                                 *action == "expand" || *action == "collapse");
        std::size_t owner = no_parent;
        for (std::size_t candidate = 0U; row_action && candidate < nodes.size(); ++candidate) {
            const FlatNode& list = nodes[candidate];
            const bool named = within.has_value() ? list.stable_id == *within : names_a_row_of(*target, list.stable_id);
            if (!named || list.is_virtual) continue;
            if (owner == no_parent || list.stable_id.size() > nodes[owner].stable_id.size()) owner = candidate;
        }
        if (owner == no_parent) {
            reply = ToolReply::failure(ToolError::not_found, "no node [" + *target + "]", resolved.hint);
            return false;
        }
        call.offscreen = true;
        index = owner;
        return true;
    }
    index = resolved.index;
    const FlatNode& node = nodes[index];
    std::string what{};
    if (!node.stable_id.empty() && detail::pending_user_step(node.stable_id, what)) {
        if (AutomationPresenter* const presenter = core.control().presenter()) {
            // The presenter shows the step on the node it names.
            SemanticNode shown{};
            shown.runtime_id = node.runtime_id;
            shown.stable_id = node.stable_id;
            shown.name = node.name;
            shown.bounds = node.bounds;
            (*presenter).user_step_needed(window, shown, what);
        }
        reply = ToolReply::failure(ToolError::needs_user, detail::node_reference(node) + " needs the user: " + what,
                                   "wait with until user_step and step " + node.stable_id + ", then carry on");
        return false;
    }
    if (!has_semantic_state(node.states, SemanticState::enabled)) {
        reply = ToolReply::failure(ToolError::disabled, detail::node_reference(node) + " is disabled");
        return false;
    }
    const std::optional<std::string> action = detail::text_member(call.arguments, "action");
    const bool revealing = call.tool == "act" && action.has_value() && *action == "scroll_into_view";
    if (!revealing && !has_semantic_state(node.states, SemanticState::visible)) {
        reply = ToolReply::failure(ToolError::not_visible, detail::node_reference(node) + " is not visible",
                                   "act scroll_into_view on it, or open its parent first");
        return false;
    }
    return true;
}

bool CoreAccess::model_dialog(const OpenDialog& dialog) const {
    const std::vector<std::string>& opened = core.model_dialogs_;
    const bool own = dialog.open && std::find(opened.begin(), opened.end(), dialog.id) != opened.end();
    return own;
}

StepOutcome CoreAccess::prepare_act(RunningCall& call, Window& window, FrameTime now) {
    const std::optional<std::string> action_name = detail::text_member(call.arguments, "action");
    if (!action_name.has_value()) {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "act names an action"));
    }
    if (*action_name == "invoke_command") {
        const std::optional<std::string> target = detail::text_member(call.arguments, "target");
        Command* const command = target.has_value() ? find_command(*target) : nullptr;
        if (command == nullptr || detail::command_hidden_mark(*target)) {
            return StepOutcome::finished(ToolReply::failure(ToolError::not_found,
                                                            "no command [" + (target.has_value() ? *target : "") + "]",
                                                            "the tool list names the program's commands"));
        }
        return prepare_command(call, window, *command, now);
    }
    // Closing a dialog the model's own act opened, as Cancel (MCP round 1):
    // the dialog's node may lie above the tree its scope shows.
    if (*action_name == "close") {
        const std::optional<std::string> target = detail::text_member(call.arguments, "target");
        std::vector<FlatNode> nodes{};
        core.flatten_window(window, nodes);
        const OpenDialog dialog = find_open_dialog(core, window, nodes);
        Control::Ptr dialog_control{};
        if (target.has_value() && dialog.window == &window && *target == dialog.id) {
            dialog_control = window.find(dialog.id);
            // A window run modally is the dialog itself (round 3: act close
            // on the Resize dialog's window was refused as a dialog_open).
            if (!dialog_control && dialog.id == core.window_stable_id(window)) dialog_control = window.root();
        }
        if (dialog_control && !dialog.drivable && model_dialog(dialog)) {
            // Escape, as the user cancels: the dialog's own node lies
            // outside its focus scope, where no semantic action reaches.
            ready_for_cancel_keys(window);
            PreparedAct& act = call.act;
            act.kind = ActKind::input;
            append_cancel_keys(act.events);
            act.summary = "close [" + dialog.id + "] (as Cancel: the dialog you opened)";
            call.before = std::move(nodes);
            return post_act(call, now);
        }
        // A program window's own ID: the window is asked to close, as its
        // close box asks; a question its program raises is the model's.
        const ApplicationWindowHandle handle = core.window_handle(window);
        if (target.has_value() && *target == core.window_stable_id(window) && handle.active() &&
            !(dialog.open && dialog.window == &window)) {
            PreparedAct& act = call.act;
            act.kind = ActKind::close_window;
            act.window_handle = handle;
            act.destructive = window_destructive(window);
            act.summary = "close [" + *target + "] (the window, as its close box)";
            call.before = std::move(nodes);
            return post_act(call, now);
        }
    }
    if (*action_name == "scroll_into_view") {
        std::vector<FlatNode> nodes{};
        std::size_t index = no_parent;
        ToolReply refusal{};
        if (!resolve_for_act(call, window, nodes, true, index, refusal)) return StepOutcome::finished(std::move(refusal));
        const FlatNode& node = nodes[index];
        if (call.offscreen || node.is_virtual || node.stable_id.empty() || !window.find(node.stable_id)) {
            return StepOutcome::finished(ToolReply::failure(
                ToolError::unsupported_action, detail::node_reference(node) + " is not a control with a stable ID",
                "a row comes into view when it is selected or focused by its ID"));
        }
        PreparedAct& act = call.act;
        act.kind = ActKind::reveal;
        act.command_id = node.stable_id;
        act.summary = "scroll_into_view " + detail::node_reference(node);
        call.before = std::move(nodes);
        return post_act(call, now);
    }
    const std::optional<SemanticAction> action = detail::parse_semantic_action(*action_name);
    if (!action.has_value()) {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::invalid_arguments, "no action " + *action_name,
                               "press, select, expand, collapse, focus, set_value, increment, decrement, show_menu, "
                               "close, scroll_into_view, invoke_command"));
    }
    std::vector<FlatNode> nodes{};
    std::size_t index = no_parent;
    ToolReply refusal{};
    if (!resolve_for_act(call, window, nodes, true, index, refusal)) return StepOutcome::finished(std::move(refusal));
    const FlatNode& node = nodes[index];
    if (call.offscreen) {
        // The row by its ID, through the list that numbers it; the list
        // says whether it has such a row.
        const std::string row = *detail::text_member(call.arguments, "target");
        PreparedAct& act = call.act;
        act.kind = ActKind::semantic;
        act.control_runtime_id = node.control_runtime_id;
        act.virtual_child = row;
        act.action = *action;
        act.destructive = window_destructive(window) || detail::action_destructive_mark(row, *action);
        act.summary = *action_name + " [" + row + "] (a row " + detail::node_reference(node) + " does not show)";
        call.before = std::move(nodes);
        return consent_then_post(call, now);
    }
    if (std::find(node.actions.begin(), node.actions.end(), *action) == node.actions.end()) {
        std::string actions{};
        for (const SemanticAction offered : node.actions) {
            if (!actions.empty()) actions += ", ";
            actions += semantic_action_name(offered);
        }
        return StepOutcome::finished(ToolReply::failure(
            ToolError::unsupported_action, detail::node_reference(node) + " does not " + *action_name,
            actions.empty() ? std::string("it has no actions") : "its actions: " + actions));
    }
    PreparedAct& act = call.act;
    act.kind = ActKind::semantic;
    act.control_runtime_id = node.control_runtime_id;
    act.virtual_child = node.is_virtual ? node.stable_id : std::string{};
    act.action = *action;
    const std::optional<std::string> value = detail::text_member(call.arguments, "value");
    if (value.has_value()) act.value = *value;
    act.protected_target = node.protected_content();
    act.destructive = window_destructive(window) || detail::action_destructive_mark(node.stable_id, *action);
    act.dialog_tool = node.dialog_tool;
    act.summary = *action_name + " " + detail::node_reference(node) + " (semantic action)";
    // A navigator entry or a command-like item offers both: select moves the
    // selection there and opens nothing, which a model reading the reply
    // easily takes for the press it meant.
    if (*action == SemanticAction::select
        && std::find(node.actions.begin(), node.actions.end(), SemanticAction::press) != node.actions.end()) {
        act.summary += "; select only selects; press opens it";
    }
    call.before = std::move(nodes);
    return consent_then_post(call, now);
}

StepOutcome CoreAccess::prepare_command(RunningCall& call, Window& window, Command& command, FrameTime now) {
    std::vector<FlatNode> nodes{};
    core.flatten_window(window, nodes);
    const OpenDialog dialog = find_open_dialog(core, window, nodes);
    if (dialog.open && !dialog.drivable) {
        return StepOutcome::finished(ToolReply::failure(ToolError::dialog_open, "a dialog is open: [" + dialog.id + "]",
                                                        "read it with snapshot and ask the user to answer it"));
    }
    const CommandState& state = command.state();
    if (!state.enabled) {
        return StepOutcome::finished(ToolReply::failure(ToolError::disabled,
                                                        "the command [" + command.stable_id() + "] is disabled",
                                                        state.availability_reason));
    }
    PreparedAct& act = call.act;
    act.kind = ActKind::command;
    act.command_id = command.stable_id();
    act.destructive = state.destructive || window_destructive(window);
    static_cast<void>(detail::opens_dialog_mark(command.stable_id(), act.dialog_tool));
    act.summary = "invoke_command [" + command.stable_id() + "]";
    call.before = std::move(nodes);
    return consent_then_post(call, now);
}

StepOutcome CoreAccess::prepare_pointer(RunningCall& call, Window& window, FrameTime now) {
    const std::optional<std::string> action = detail::text_member(call.arguments, "action");
    if (!action.has_value()) {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "pointer names an action"));
    }
    std::vector<FlatNode> nodes{};
    std::size_t index = no_parent;
    ToolReply refusal{};
    if (!resolve_for_act(call, window, nodes, false, index, refusal)) return StepOutcome::finished(std::move(refusal));
    Point point{};
    bool placed = false;
    const JsonValue* const position = detail::kind_member(call.arguments, "position", JsonKind::object);
    if (index != no_parent && position != nullptr) {
        return StepOutcome::finished(ToolReply::failure(
            ToolError::invalid_arguments, "pointer takes a target or a position, not both",
            "offset aims inside the target; position is in window coordinates"));
    }
    // A target's top-left corner: the origin of offset and of a drag's path.
    Point origin{};
    if (index != no_parent) {
        const Rect bounds = nodes[index].bounds;
        origin = Point{bounds.x, bounds.y};
        point = Point{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0};
        const JsonValue* const offset = detail::kind_member(call.arguments, "offset", JsonKind::object);
        Point inside{};
        if (offset != nullptr && read_point(*offset, inside)) point = Point{bounds.x + inside.x, bounds.y + inside.y};
        placed = true;
    }
    if (!placed && position != nullptr) placed = read_point(*position, point);
    if (!placed && *action != "up" && *action != "wheel") {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::invalid_arguments, "pointer names a target or a position {x, y}"));
    }
    if (!placed) point = window.interaction_snapshot().pointer_position;
    Modifier modifiers = Modifier::none;
    std::string error{};
    if (!read_modifiers(call.arguments, modifiers, error)) {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, error));
    }
    PointerButton button = PointerButton::primary;
    const std::optional<std::string> button_name = detail::text_member(call.arguments, "button");
    if (button_name.has_value() && *button_name == "secondary") button = PointerButton::secondary;
    if (button_name.has_value() && *button_name == "middle") button = PointerButton::middle;
    if (*action == "right_click") button = PointerButton::secondary;
    // One millisecond between events, as a person's fastest click.
    std::uint64_t time = detail::nanoseconds_of(now);
    constexpr std::uint64_t step = 1000000U;
    std::vector<HostEventPayload>& events = call.act.events;
    events.clear();
    events.push_back(pointer_event(PointerAction::move, PointerButton::none, point, modifiers, time, 1U));
    if (*action == "click" || *action == "right_click") {
        time += step;
        events.push_back(pointer_event(PointerAction::down, button, point, modifiers, time, 1U));
        time += step;
        events.push_back(pointer_event(PointerAction::up, button, point, modifiers, time, 1U));
    } else if (*action == "double_click") {
        for (std::uint32_t clicks = 1U; clicks <= 2U; ++clicks) {
            time += step;
            events.push_back(pointer_event(PointerAction::down, button, point, modifiers, time, clicks));
            time += step;
            events.push_back(pointer_event(PointerAction::up, button, point, modifiers, time, clicks));
        }
    } else if (*action == "down") {
        time += step;
        events.push_back(pointer_event(PointerAction::down, button, point, modifiers, time, 1U));
    } else if (*action == "up") {
        time += step;
        events.push_back(pointer_event(PointerAction::up, button, point, modifiers, time, 1U));
    } else if (*action == "drag") {
        const JsonValue* const path = detail::kind_member(call.arguments, "path", JsonKind::array);
        if (path == nullptr || (*path).items.empty()) {
            return StepOutcome::finished(
                ToolReply::failure(ToolError::invalid_arguments,
                                   "drag names a path of points {x, y}: in the target's space with a target, "
                                   "window coordinates without"));
        }
        time += step;
        events.push_back(pointer_event(PointerAction::down, button, point, modifiers, time, 1U));
        Point last = point;
        for (const JsonValue& item : (*path).items) {
            Point given{};
            if (!read_point(item, given)) {
                return StepOutcome::finished(
                    ToolReply::failure(ToolError::invalid_arguments, "each path point is {x, y} (or [x, y])"));
            }
            // With a target, in its space (as offset); else window
            // coordinates (round 2: target and path agree).
            last = Point{origin.x + given.x, origin.y + given.y};
            time += step;
            events.push_back(pointer_event(PointerAction::move, button, last, modifiers, time, 1U));
        }
        time += step;
        events.push_back(pointer_event(PointerAction::up, button, last, modifiers, time, 1U));
    } else if (*action == "wheel") {
        Point delta{0.0, -1.0};
        const JsonValue* const given = detail::kind_member(call.arguments, "delta", JsonKind::object);
        if (given != nullptr) static_cast<void>(read_point(*given, delta));
        PointerEvent wheel = pointer_event(PointerAction::wheel, PointerButton::none, point, modifiers, time + step, 1U);
        wheel.wheel_delta = delta;
        events.push_back(wheel);
    } else if (*action != "move" && *action != "hover") {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "no pointer action " + *action));
    }
    PreparedAct& act = call.act;
    act.kind = ActKind::input;
    act.focus_first = false;
    if (index != no_parent) {
        const FlatNode& node = nodes[index];
        act.protected_target = node.protected_content();
        const bool presses = *action != "move" && *action != "hover" && *action != "wheel";
        act.destructive = window_destructive(window) ||
                          (presses && detail::action_destructive_mark(node.stable_id, SemanticAction::press));
        act.summary = *action + " " + detail::node_reference(node) + " (pointer)";
    } else {
        act.destructive = window_destructive(window);
        act.summary = *action + " at " + std::to_string(point.x) + "," + std::to_string(point.y) + " (pointer)";
    }
    call.before = std::move(nodes);
    return consent_then_post(call, now);
}

StepOutcome CoreAccess::prepare_keyboard(RunningCall& call, Window& window, FrameTime now) {
    std::vector<FlatNode> nodes{};
    std::size_t index = no_parent;
    ToolReply refusal{};
    if (!resolve_for_act(call, window, nodes, false, index, refusal)) return StepOutcome::finished(std::move(refusal));
    std::vector<std::string> chords{};
    const JsonValue* const keys = call.arguments.find("keys");
    if (keys != nullptr && (*keys).kind == JsonKind::string) chords.push_back((*keys).text);
    if (keys != nullptr && (*keys).kind == JsonKind::array) {
        for (const JsonValue& item : (*keys).items) {
            if (item.kind == JsonKind::string) chords.push_back(item.text);
        }
    }
    const std::optional<std::string> text = detail::text_member(call.arguments, "text");
    const JsonValue* const composition = detail::kind_member(call.arguments, "composition", JsonKind::object);
    if (chords.empty() && !text.has_value() && composition == nullptr) {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::invalid_arguments, "keyboard names keys, text or a composition"));
    }
    std::vector<HostEventPayload>& events = call.act.events;
    events.clear();
    for (const std::string& chord_text : chords) {
        detail::KeyChord chord{};
        if (!detail::parse_key_chord(chord_text, chord)) {
            return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments,
                                                            "no key " + chord_text,
                                                            "chords such as Ctrl+S, Shift+Tab, Enter, F2, Alt+Down"));
        }
        KeyEvent down{};
        down.action = KeyAction::down;
        down.physical_key = chord.physical_key;
        down.modifiers = chord.modifiers;
        events.push_back(down);
        KeyEvent up = down;
        up.action = KeyAction::up;
        events.push_back(up);
    }
    if (text.has_value()) {
        TextInputEvent typed{};
        typed.text_utf8 = *text;
        events.push_back(typed);
    }
    if (composition != nullptr) {
        TextInputEvent composing{};
        const std::optional<std::string> composed = detail::text_member(*composition, "text");
        composing.text_utf8 = composed.has_value() ? *composed : std::string{};
        composing.composing = true;
        const std::optional<double> start = detail::number_member(*composition, "replacement_start");
        const std::optional<double> length = detail::number_member(*composition, "replacement_length");
        if (start.has_value()) composing.replacement_start = static_cast<std::int32_t>(*start);
        if (length.has_value()) composing.replacement_length = static_cast<std::int32_t>(*length);
        events.push_back(composing);
    }
    PreparedAct& act = call.act;
    act.kind = ActKind::input;
    act.destructive = window_destructive(window);
    if (index != no_parent) {
        const FlatNode& node = nodes[index];
        act.focus_first = true;
        act.control_runtime_id = node.control_runtime_id;
        act.virtual_child = node.is_virtual ? node.stable_id : std::string{};
        act.protected_target = node.protected_content();
        act.summary = "keyboard into " + detail::node_reference(node);
    } else {
        act.focus_first = false;
        act.summary = "keyboard to the focused control";
    }
    // Typing into a field the model was told to fill is allowed; the reply
    // then says only "ok" (study 9.1). Without a target, the focused node
    // decides.
    for (const FlatNode& node : nodes) {
        if (index == no_parent && has_semantic_state(node.states, SemanticState::focused) &&
            node.protected_content()) {
            act.protected_target = true;
        }
    }
    call.before = std::move(nodes);
    return consent_then_post(call, now);
}

StepOutcome CoreAccess::prepare_selection(RunningCall& call, Window& window, FrameTime now) {
    std::vector<FlatNode> nodes{};
    std::size_t index = no_parent;
    ToolReply refusal{};
    if (!resolve_for_act(call, window, nodes, true, index, refusal)) return StepOutcome::finished(std::move(refusal));
    const FlatNode& node = nodes[index];
    if (node.protected_content()) {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::protected_content, detail::node_reference(node) + " is a password field"));
    }
    const JsonValue* const select = detail::kind_member(call.arguments, "select", JsonKind::object);
    if (select == nullptr) {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::invalid_arguments, "select is {anchor, caret}, UTF-8 byte offsets"));
    }
    const std::optional<double> anchor = detail::number_member(*select, "anchor");
    const std::optional<double> caret = detail::number_member(*select, "caret");
    if (!anchor.has_value() || !caret.has_value() || *anchor < 0.0 || *caret < 0.0) {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::invalid_arguments, "select is {anchor, caret}, UTF-8 byte offsets"));
    }
    PreparedAct& act = call.act;
    act.kind = ActKind::selection;
    act.control_runtime_id = node.runtime_id;
    act.selection = SemanticTextSelection{Utf8Offset(static_cast<std::size_t>(*anchor)),
                                          Utf8Offset(static_cast<std::size_t>(*caret))};
    act.destructive = false;
    act.summary = "select text in " + detail::node_reference(node);
    call.before = std::move(nodes);
    return consent_then_post(call, now);
}

StepOutcome CoreAccess::consent_then_post(RunningCall& call, FrameTime now) {
    if (!call.act.destructive) return post_act(call, now);
    AutomationControl& control = core.control();
    if (control.consent() == ConsentState::unasked) {
        control.request_consent();
        call.deadline = now + core.settings().consent_timeout;
    }
    switch (control.consent()) {
    case ConsentState::allowed:
        return post_act(call, now);
    case ConsentState::declined:
        return StepOutcome::finished(ToolReply::failure(
            ToolError::declined, "the user did not allow destructive acts for this launch",
            "non-destructive acts still work"));
    case ConsentState::asking:
        if (now >= call.deadline) {
            return StepOutcome::finished(ToolReply::failure(ToolError::timeout,
                                                            "the user has not answered the destructive-act question",
                                                            "ask the user to answer it in the program"));
        }
        call.phase = CallPhase::awaiting_consent;
        return StepOutcome::resume_at(now + core.settings().poll_interval);
    case ConsentState::unasked:
        // Still unasked after asking: the question could not be shown.
        return consent_not_shown(control);
    }
    return consent_not_shown(control);
}

StepOutcome CoreAccess::post_act(RunningCall& call, FrameTime now) {
    Window* const window = call.window.get();
    if (window == nullptr) return StepOutcome::finished(ToolReply::failure(ToolError::window_gone, "the window closed"));
    call.paint_passes_before = detail::paint_passes_of(*window);
    const OpenDialog dialog = find_open_dialog(core, *window, call.before);
    call.dialog_before = dialog.open ? dialog.id : std::string{};
    call.scope_before = detail::modal_scope_of(*window);
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    call.windows_before.clear();
    for (Window* const open : windows) {
        // Tooltip windows come and go with the pointer; no model names them.
        if (!core.passive_popup_window(*open)) call.windows_before.push_back(core.window_stable_id(*open));
    }
    call.outcome = gui_forms::detail::make_library_shared<ActOutcome>();
    // A question the program raises from here to the reply is the model's,
    // a close's prompt included, which comes after the act returns.
    gui_forms::detail::begin_model_questions(call.id.value, call.unsaved);
    call.questions_open = true;
    // So are the effects the program reports (report_model_effect).
    detail::begin_model_effects();
    PostedAct work{call.window, call.act, core.control().client(), call.outcome};
    static_cast<void>((*window).begin_invoke(std::move(work)));
    if (AutomationPresenter* const presenter = core.control().presenter()) {
        (*presenter).activity(*window, AutomationActivity::acting);
    }
    call.phase = CallPhase::settling;
    call.started = now;
    call.deadline = now + core.settings().settle_limit;
    return StepOutcome::resume_at(now + core.settings().poll_interval);
}

StepOutcome CoreAccess::settle(RunningCall& call, FrameTime now) {
    const ActOutcome& outcome = *call.outcome;
    Window* const window = call.window.get();
    if (!outcome.ran) {
        if (window == nullptr) {
            return StepOutcome::finished(ToolReply::failure(ToolError::window_gone, "the window closed first"));
        }
        if (now < call.deadline) return StepOutcome::resume_at(now + core.settings().poll_interval);
        return StepOutcome::finished(
            ToolReply::failure(ToolError::timeout, "the window did not reach the act",
                               "its UI thread is busy; wait and take a snapshot"));
    }
    if (!outcome.performed) {
        if (outcome.reply.has_value()) return StepOutcome::finished(*outcome.reply);
        return StepOutcome::finished(ToolReply::failure(ToolError::unsupported_action, outcome.detail,
                                                        "take a new snapshot"));
    }
    const bool closing = call.act.kind == ActKind::close_window || call.act.kind == ActKind::close_windows;
    if (closing && !outcome.closes_delivered && window != nullptr && now < call.deadline) {
        return StepOutcome::resume_at(now + core.settings().poll_interval);
    }
    std::string busy{};
    bool idle = true;
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    for (Window* const open : windows) {
        // A tooltip's window comes and goes with its own timers; it is no
        // program work an act waits on.
        if (core.passive_popup_window(*open)) continue;
        if (!detail::window_idle(*open, now, core.settings().idle_horizon, busy)) {
            idle = false;
            break;
        }
    }
    // A program tool whose reply is complete returns once its window has
    // settled or its short limit passed, never waiting out the window's
    // timers and frames: its reply says what it did.
    const bool program_tool = call.act.kind == ActKind::registry || call.act.kind == ActKind::command;
    const bool tool_replied = outcome.reply.has_value() && (*outcome.reply).success_value() != nullptr;
    const bool returns_now = program_tool && (call.act.kind == ActKind::command || tool_replied);
    const FrameTime limit = returns_now ? std::min(call.deadline, call.started + core.settings().program_settle_limit)
                                        : call.deadline;
    if (!idle && now < limit) return StepOutcome::resume_at(now + core.settings().poll_interval);
    // A registry tool's reply is its own; the diff follows it.
    std::string text{};
    if (outcome.reply.has_value()) {
        if (const ToolSuccess* const success = (*outcome.reply).success_value()) text = (*success).text + "\n";
    }
    if (call.act.protected_target) {
        // Study 9.1: typing into a password field replies only ok.
        return StepOutcome::finished(ToolReply::success("ok"));
    }
    text += "ok: " + call.act.summary + "\n";
    // A disconnect's reply is its outcome; the windows are closing.
    if (call.act.kind == ActKind::close_windows) return StepOutcome::finished(ToolReply::success(std::move(text)));
    // Where the act's effects start: with none, the reply says so (round 3:
    // keyboard Escape replied a bare ok).
    const std::size_t effects_start = text.size();
    if (window != nullptr) {
        std::vector<FlatNode> after{};
        core.flatten_window(*window, after);
        detail::DiffOptions diff{};
        diff.maximum_entries = core.settings().maximum_diff_entries;
        diff.terse = call.terse;
        diff.scope_before = call.scope_before;
        diff.scope_after = detail::modal_scope_of(*window);
        detail::append_diff(call.before, after, diff, text);
        note_dialog_opened(call, *window, after, text);
    } else {
        text += "the window closed\n";
    }
    for (Window* const open : windows) {
        // A tooltip's window is no window a model uses (round 3: the Resize
        // dialog's tooltip was named as opened).
        if (core.passive_popup_window(*open)) continue;
        const std::string id = core.window_stable_id(*open);
        if (std::find(call.windows_before.begin(), call.windows_before.end(), id) != call.windows_before.end()) continue;
        text += "opened: window \"" + (*open).text.read() + "\" [" + id + "]\n";
    }
    for (const std::string& id : call.windows_before) {
        bool open_now = false;
        for (Window* const open : windows) open_now = open_now || core.window_stable_id(*open) == id;
        if (!open_now) text += "closed: window [" + id + "]\n";
    }
    // What the program says the act did that no node shows (round 5:
    // Escape cancelling a floating shape replied "effect: none").
    std::vector<std::string> described{};
    detail::take_model_effects(described);
    for (const std::string& effect : described) text += "effect: " + effect + "\n";
    // A program tool's own reply says what it did (a drawing changes pixels,
    // not nodes: 1.0.5 review, plan_paint_draw replied "effect: none").
    if (text.size() == effects_start && !tool_replied) {
        text += "effect: none (no node changed, focus did not move, no window opened or closed)\n";
    }
    const std::int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - call.started).count();
    const std::uint64_t frames = window == nullptr ? 0U : detail::paint_passes_of(*window) - call.paint_passes_before;
    if (idle) {
        text += "frames: " + std::to_string(frames) + " presented; idle after " + std::to_string(elapsed) + " ms";
    } else if (returns_now) {
        // Not waited on: the window goes on with its own work.
        text += "frames: " + std::to_string(frames) + " presented; replied after " + std::to_string(elapsed) + " ms";
    } else {
        text += "frames: " + std::to_string(frames) + " presented; still busy after " + std::to_string(elapsed) +
                " ms: " + busy;
    }
    return StepOutcome::finished(ToolReply::success(std::move(text)));
}

StepOutcome CoreAccess::prepare_wait(RunningCall& call, Window& window, FrameTime now) {
    const std::optional<std::string> until = detail::text_member(call.arguments, "until");
    const std::string kind = until.has_value() ? *until : std::string("idle");
    const AutomationSettings& settings = core.settings();
    std::chrono::milliseconds timeout = settings.default_wait_timeout;
    const std::optional<double> given = detail::number_member(call.arguments, "timeout_ms");
    if (given.has_value()) {
        const double clamped = std::min(*given, static_cast<double>(settings.maximum_wait_timeout.count()));
        timeout = std::chrono::milliseconds(static_cast<std::int64_t>(clamped));
    }
    WaitSpec& wait = call.wait;
    if (kind == "frame") {
        wait.until = WaitUntil::frame;
        wait.paint_passes = detail::paint_passes_of(window);
    } else if (kind == "idle") {
        wait.until = WaitUntil::idle;
    } else if (kind == "ms") {
        const std::optional<double> milliseconds = detail::number_member(call.arguments, "ms");
        if (!milliseconds.has_value()) {
            return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "until ms names ms"));
        }
        const double clamped = std::min(*milliseconds, static_cast<double>(settings.maximum_wait_timeout.count()));
        wait.until = WaitUntil::time;
        wait.target_time = now + std::chrono::milliseconds(static_cast<std::int64_t>(clamped));
        timeout = std::chrono::milliseconds(static_cast<std::int64_t>(clamped)) + settings.default_wait_timeout;
    } else if (kind == "node") {
        wait.until = WaitUntil::node;
        detail::read_condition(call.arguments, wait.condition);
        if (wait.condition.target.empty()) {
            return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "until node names a target"));
        }
    } else if (kind == "user_step") {
        wait.until = WaitUntil::user_step;
        const std::optional<std::string> step = detail::text_member(call.arguments, "step");
        if (!step.has_value()) {
            return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "until user_step names step"));
        }
        // A step the program never required would pass at once (round 2):
        // refused.
        if (!detail::user_step_known(*step)) {
            return StepOutcome::finished(ToolReply::failure(
                ToolError::not_found, "no user step " + *step + ": the program never asked the user for it",
                "a step is named by the stable ID an act's needs_user reply gave"));
        }
        wait.user_step = *step;
    } else {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "no wait until " + kind,
                                                        "frame, idle, ms, node, user_step"));
    }
    call.phase = CallPhase::waiting;
    call.started = now;
    call.deadline = now + timeout;
    if (AutomationPresenter* const presenter = core.control().presenter()) {
        (*presenter).activity(window, AutomationActivity::waiting);
    }
    return wait_step(call, now);
}

// Whether `wait` is met in `window` now; `busy` says what it still waits
// for and `observed` what it saw.
bool CoreAccess::wait_reached(Window& window, const WaitSpec& wait, FrameTime now, std::string& busy,
                              std::string& observed) {
    switch (wait.until) {
    case WaitUntil::frame:
        if (detail::paint_passes_of(window) > wait.paint_passes) return true;
        if (detail::window_idle(window, now, core.settings().idle_horizon, busy)) {
            // Nothing wants a frame: none will come, which is what a frame
            // wait learns.
            observed = "no frame was wanted";
            return true;
        }
        return false;
    case WaitUntil::idle:
        return detail::window_idle(window, now, core.settings().idle_horizon, busy);
    case WaitUntil::time:
        return now >= wait.target_time;
    case WaitUntil::node: {
        std::vector<FlatNode> nodes{};
        core.flatten_window(window, nodes);
        const bool holds = detail::condition_holds(nodes, wait.condition, observed);
        busy = observed;
        return holds;
    }
    case WaitUntil::user_step:
        busy = "the user has not completed [" + wait.user_step + "]";
        return !user_step_pending(wait.user_step.c_str());
    }
    return false;
}

StepOutcome CoreAccess::wait_step(RunningCall& call, FrameTime now) {
    Window* const window = call.window.get();
    if (window == nullptr) return StepOutcome::finished(ToolReply::failure(ToolError::window_gone, "the window closed"));
    const WaitSpec& wait = call.wait;
    std::string busy{};
    std::string observed{};
    const bool done = wait_reached(*window, wait, now, busy, observed);
    const std::int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - call.started).count();
    if (done) {
        std::string text = "ok: waited " + std::to_string(elapsed) + " ms";
        if (!observed.empty()) text += "; " + observed;
        return StepOutcome::finished(ToolReply::success(std::move(text)));
    }
    if (now >= call.deadline) {
        return StepOutcome::finished(ToolReply::failure(ToolError::timeout,
                                                        "the wait ran out after " + std::to_string(elapsed) + " ms",
                                                        "still busy: " + busy));
    }
    FrameTime next = now + core.settings().poll_interval;
    if (wait.until == WaitUntil::time && wait.target_time < next) next = wait.target_time;
    return StepOutcome::resume_at(next);
}

StepOutcome CoreAccess::release_control_tool() {
    AutomationControl& control = core.control();
    if (control.state() != ControlState::model_control) {
        return StepOutcome::finished(ToolReply::success("ok: you did not hold control; the user has it"));
    }
    control.release_by_model();
    return StepOutcome::finished(
        ToolReply::success("ok: control returned to the user; AI mode ended. Your next act asks the user again"));
}

// disconnect (owner, round 3). Soft asks every program window to close
// with the program's questions the model's: one asked stops the close, and
// the reply names it (finish_reply). Hard gives control back and asks them
// to close with the questions the user's, or answered No for
// discard_unsaved. It is an act (needs_control): the take came first, so
// the user is in control here only in an appearance review, and then only
// the session ends.
StepOutcome CoreAccess::disconnect_tool(RunningCall& call, FrameTime now) {
    const std::optional<std::string> mode = detail::text_member(call.arguments, "mode");
    if (mode.has_value() && *mode != "soft" && *mode != "hard") {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::invalid_arguments, "no mode " + *mode, "soft or hard"));
    }
    call.disconnect_hard = mode.has_value() && *mode == "hard";
    const std::optional<bool> discard = detail::boolean_member(call.arguments, "discard_unsaved");
    call.discard_unsaved = discard.has_value() && *discard;
    if (call.discard_unsaved && !call.disconnect_hard) {
        return StepOutcome::finished(ToolReply::failure(
            ToolError::invalid_arguments, "discard_unsaved goes with mode hard",
            "save first and disconnect again, or disconnect with mode hard and discard_unsaved true"));
    }
    AutomationControl& control = core.control();
    if (control.state() != ControlState::model_control) {
        core.end_session();
        return StepOutcome::finished(ToolReply::success(
            "ok: disconnected; the user holds the program, so it stays open as it is"));
    }
    if (call.disconnect_hard && !call.discard_unsaved) {
        core.hard_disconnect();
        return StepOutcome::finished(ToolReply::success(
            "ok: disconnected (hard); control is the user's and every window was asked to close; the program asks "
            "the user about any unsaved work"));
    }
    ToolReply refusal{};
    if (call_window(call, refusal) == nullptr) return StepOutcome::finished(std::move(refusal));
    PreparedAct& act = call.act;
    act.kind = ActKind::close_windows;
    core.program_window_handles(act.window_handles);
    act.summary = call.discard_unsaved ? "disconnected (hard); unsaved changes discarded and every window closed"
                                       : "disconnected; every window closed and the program is ending";
    call.unsaved = call.discard_unsaved ? gui_forms::detail::ModelQuestionAnswer::discard
                                        : gui_forms::detail::ModelQuestionAnswer::none;
    if (call.discard_unsaved) core.end_session();
    return post_act(call, now);
}

// close (owner, MCP 1.0.6: a model may "simply connect and then call
// program close"): every program window is asked to close, as quitting
// does, with the program's questions the model's: with no on_unsaved a save
// question stops the close and the reply names it; on_unsaved answers it.
// An act: the take came first.
StepOutcome CoreAccess::close_tool(RunningCall& call, FrameTime now) {
    ToolReply refusal{};
    if (call_window(call, refusal) == nullptr) return StepOutcome::finished(std::move(refusal));
    PreparedAct& act = call.act;
    act.kind = ActKind::close_windows;
    core.program_window_handles(act.window_handles);
    act.summary = "closed every program window; the program is ending";
    return post_act(call, now);
}

bool CoreAccess::needs_control(const RunningCall& call) {
    const std::string& tool = call.tool;
    // disconnect is an act (owner, MCP 1.0.6: "the act of messaging in
    // order to tell it to close, paradoxically is an action"): it takes
    // control as any act does in the launch's mode, then closes per its mode.
    if (tool == "act" || tool == "pointer" || tool == "keyboard" || tool == "reset" || tool == "disconnect" ||
        tool == "close") {
        return true;
    }
    if (tool == "text") return call.arguments.find("select") != nullptr;
    if (tool.compare(0U, command_tool_prefix.size(), command_tool_prefix) == 0) return true;
    if (built_in_name(tool)) return false;
    ToolEntry entry{};
    const bool acting = core.tools().find(tool, entry) && entry.spec.access == ToolAccess::acting;
    return acting;
}

StepOutcome CoreAccess::control_gate(RunningCall& call, FrameTime now) {
    AutomationControl& control = core.control();
    if (control.acts_allowed()) {
        call.control_checked = true;
        // Allowed after this call asked: its reply carries the snapshot.
        call.took_control = call.asked_control;
        return StepOutcome::resume_at(now);
    }
    if (!call.asked_control) {
        call.asked_control = true;
        call.takes_answered_before = control.takes_answered();
        control.request_take(now);
        call.deadline = now + control.settings().take_countdown + core.settings().consent_timeout;
    }
    if (control.state() == ControlState::model_control) {
        call.control_checked = true;
        call.took_control = true;
        return StepOutcome::resume_at(now);
    }
    if (control.state() == ControlState::asking) {
        if (now >= call.deadline) {
            return StepOutcome::finished(ToolReply::failure(ToolError::timeout,
                                                            "the user has not answered whether you may take control",
                                                            "ask the user to answer it in the program"));
        }
        call.phase = CallPhase::awaiting_control;
        return StepOutcome::resume_at(now + core.settings().poll_interval);
    }
    // Back with the user: refused, or the question could not be shown.
    const bool answered = control.takes_answered() != call.takes_answered_before;
    if (answered && !control.last_take_allowed() && control.presenter() != nullptr && call.tool == "disconnect") {
        return StepOutcome::finished(ToolReply::failure(
            ToolError::declined, "the user declined to let you take control, so the program stays open",
            "the program is the user's; your next act, disconnect included, asks again"));
    }
    if (answered && !control.last_take_allowed() && control.presenter() != nullptr) {
        return StepOutcome::finished(ToolReply::failure(
            ToolError::declined, "the user declined to let you take control of the program",
            "reads still work; your next act asks the user again"));
    }
    return StepOutcome::finished(ToolReply::failure(ToolError::declined,
                                                    "the take question could not be shown, so you may not act",
                                                    "the program has no window to ask from; reads still work"));
}

StepOutcome CoreAccess::registry_tool(RunningCall& call, const ToolEntry& entry, std::string_view arguments_json,
                                      FrameTime now) {
    const ClientIdentity& client = core.control().client();
    Window* window = nullptr;
    if (entry.spec.thread == ToolThread::ui) {
        ToolReply refusal{};
        window = call_window(call, refusal);
        if (window == nullptr) return StepOutcome::finished(std::move(refusal));
    }
    if (entry.spec.access == ToolAccess::acting) {
        // A program's acting tool meets the dialog rule as act does (study
        // 9.2): nothing it registers drives past an uninstrumented dialog.
        std::vector<FlatNode> nodes{};
        core.flatten_window(*window, nodes);
        const OpenDialog dialog = find_open_dialog(core, *window, nodes);
        if (dialog.open && (!dialog.drivable || (dialog.window != nullptr && dialog.window != window))) {
            return StepOutcome::finished(ToolReply::failure(
                ToolError::dialog_open, "a dialog is open: [" + dialog.id + "]",
                dialog.drivable ? "act in the dialog's window [" + dialog.id + "]"
                                : "read it with snapshot and ask the user to answer it"));
        }
    }
    if (entry.spec.access == ToolAccess::acting && entry.spec.settles && entry.spec.thread == ToolThread::ui) {
        if (window == nullptr) {
            ToolReply refusal{};
            window = call_window(call, refusal);
            if (window == nullptr) return StepOutcome::finished(std::move(refusal));
        }
        PreparedAct& act = call.act;
        act.kind = ActKind::registry;
        act.entry = entry;
        act.arguments_json = std::string(arguments_json);
        act.destructive = entry.spec.destructive;
        act.summary = entry.spec.name;
        core.flatten_window(*window, call.before);
        return consent_then_post(call, now);
    }
    if (entry.spec.destructive && core.control().consent() != ConsentState::allowed) {
        // A destructive tool that does not settle still asks first.
        if (window == nullptr) {
            ToolReply refusal{};
            window = call_window(call, refusal);
            if (window == nullptr) return StepOutcome::finished(std::move(refusal));
        }
        PreparedAct& act = call.act;
        act.kind = ActKind::registry;
        act.entry = entry;
        act.arguments_json = std::string(arguments_json);
        act.destructive = true;
        act.summary = entry.spec.name;
        core.flatten_window(*window, call.before);
        return consent_then_post(call, now);
    }
    ModelToolCall invocation{};
    invocation.arguments_json = arguments_json.data();
    invocation.arguments_bytes = arguments_json.size();
    invocation.client_name = client.client_name.c_str();
    invocation.model_name = client.model_name.c_str();
    invocation.window = window;
    ToolReply reply = call_tool_handler(entry, invocation);
    return StepOutcome::finished(std::move(reply));
}

StepOutcome CoreAccess::first_step(RunningCall& call, FrameTime now) {
    const std::string& tool = call.tool;
    if (core.session_ended()) {
        return StepOutcome::finished(ToolReply::failure(
            ToolError::needs_user, "your session ended (disconnect); the program is the user's",
            "start the program again to connect"));
    }
    std::string arguments_json{};
    detail::object_without(call.arguments, std::string_view{}, arguments_json);
    // Unknown members are refused, not ignored (MCP round 1).
    std::string schema{};
    if (tool_schema(tool, schema)) {
        std::string known{};
        const std::string unknown = detail::unknown_members(call.arguments, schema, known);
        if (!unknown.empty()) {
            return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, tool + " takes no " + unknown,
                                                            known.empty() ? std::string("it takes no arguments")
                                                                          : "its arguments: " + known));
        }
    }
    const std::optional<bool> terse = detail::boolean_member(call.arguments, "terse");
    if (terse.has_value()) call.terse = *terse;
    const std::optional<std::string> unsaved = detail::text_member(call.arguments, "on_unsaved");
    if (unsaved.has_value()) {
        if (*unsaved == "save") {
            call.unsaved = gui_forms::detail::ModelQuestionAnswer::save;
        } else if (*unsaved == "discard") {
            call.unsaved = gui_forms::detail::ModelQuestionAnswer::discard;
        } else {
            return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments,
                                                            "no on_unsaved " + *unsaved, "save or discard"));
        }
    }
    if (tool == "windows") return windows_tool();
    if (tool == "reset") return reset_tool(call, now);
    if (tool == "release_control") return release_control_tool();
    // An act needs control (the owner's control model): its first ask
    // waits on the user's answer. disconnect and close are acts.
    if (!call.control_checked && needs_control(call)) {
        StepOutcome gate = control_gate(call, now);
        if (!call.control_checked) return gate;
    }
    if (tool == "disconnect") return disconnect_tool(call, now);
    if (tool == "close") return close_tool(call, now);
    if (tool == "batch") return prepare_batch(call, now);
    if (built_in_name(tool)) {
        ToolReply refusal{};
        Window* const window = call_window(call, refusal);
        if (window == nullptr) return StepOutcome::finished(std::move(refusal));
        AutomationPresenter* const presenter = core.control().presenter();
        if (tool == "snapshot" || tool == "capture" || tool == "inspect" ||
            (tool == "text" && call.arguments.find("select") == nullptr)) {
            if (presenter != nullptr) (*presenter).activity(*window, AutomationActivity::reading);
        }
        if (tool == "snapshot") return StepOutcome::finished(detail::snapshot_tool(core, *window, call.arguments));
        if (tool == "find") {
            if (presenter != nullptr) (*presenter).activity(*window, AutomationActivity::reading);
            return find_tool(call, *window);
        }
        if (tool == "capture") return StepOutcome::finished(detail::capture_tool(core, *window, call.arguments));
        if (tool == "inspect") {
            return StepOutcome::finished(detail::inspect_tool(core, *window, call.arguments, arguments_json));
        }
        if (tool == "text") {
            if (call.arguments.find("select") != nullptr) return prepare_selection(call, *window, now);
            return StepOutcome::finished(detail::text_read_tool(core, *window, call.arguments));
        }
        if (tool == "act") return prepare_act(call, *window, now);
        if (tool == "pointer") return prepare_pointer(call, *window, now);
        if (tool == "keyboard") return prepare_keyboard(call, *window, now);
        if (tool == "wait") return prepare_wait(call, *window, now);
    }
    if (tool.compare(0U, command_tool_prefix.size(), command_tool_prefix) == 0) {
        Command* const command = find_command_tool(tool);
        if (command != nullptr && !detail::command_hidden_mark((*command).stable_id())) {
            ToolReply refusal{};
            Window* const window = call_window(call, refusal);
            if (window == nullptr) return StepOutcome::finished(std::move(refusal));
            return prepare_command(call, *window, *command, now);
        }
    }
    ToolEntry entry{};
    if (core.tools().find(tool, entry)) return registry_tool(call, entry, arguments_json, now);
    return StepOutcome::finished(
        ToolReply::failure(ToolError::unknown_tool, "no tool " + tool, "list the tools to see what this program has"));
}

// ---- batch (ruling 22) ---------------------------------------------------------------------

StepOutcome CoreAccess::prepare_batch(RunningCall& call, FrameTime now) {
    if (call.chain.value != 0U) {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "a batch cannot hold a batch"));
    }
    const JsonValue* const steps = detail::kind_member(call.arguments, "steps", JsonKind::array);
    if (steps == nullptr || (*steps).items.empty()) {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "batch names steps"));
    }
    if ((*steps).items.size() > core.settings().maximum_batch_steps) {
        return StepOutcome::finished(ToolReply::failure(
            ToolError::invalid_arguments,
            "a batch holds at most " + std::to_string(core.settings().maximum_batch_steps) + " steps"));
    }
    for (std::size_t index = 0U; index < (*steps).items.size(); ++index) {
        const JsonValue& step = (*steps).items[index];
        const std::string number = std::to_string(index + 1U);
        if (step.kind != JsonKind::object) {
            return StepOutcome::finished(
                ToolReply::failure(ToolError::invalid_arguments, "step " + number + " is not an object"));
        }
        const std::optional<std::string> tool = detail::text_member(step, "tool");
        const std::optional<std::string> expect = detail::text_member(step, "expect");
        if (tool.has_value() == expect.has_value()) {
            return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments,
                                                            "step " + number + " names a tool or an expect"));
        }
        if (tool.has_value() && *tool == "batch") {
            return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "a batch cannot hold a batch"));
        }
        std::string arguments{};
        detail::object_without(step, tool.has_value() ? std::string_view("tool") : std::string_view{}, arguments);
        call.step_tools.push_back(tool.has_value() ? *tool : *expect);
        call.step_arguments.push_back(std::move(arguments));
        call.step_expects.push_back(expect.has_value());
    }
    for (std::size_t index = 0U; index < call.step_tools.size(); ++index) {
        if (!call.step_expects[index]) continue;
        // An expect step takes exactly its members.
        JsonValue arguments{};
        std::string error{};
        static_cast<void>(detail::read_arguments(call.step_arguments[index], arguments, error));
        const bool visual = call.step_tools[index] == "visual";
        for (const std::string& name : arguments.names) {
            if (expect_member(name, visual)) continue;
            return StepOutcome::finished(ToolReply::failure(
                ToolError::invalid_arguments, "step " + std::to_string(index + 1U) + ": an expect takes no " + name,
                visual ? "expect visual takes target, within, window, state, drawn, value, exists"
                       : "expect node takes target, within, window, state, value, exists"));
        }
    }
    const std::optional<bool> stop = detail::boolean_member(call.arguments, "stop_on_error");
    call.stop_on_error = !stop.has_value() || *stop;
    // The batch's own chain: a control step inside it holds until it ends.
    call.chain = ChainId{call.id.value};
    call.phase = CallPhase::batch;
    call.next_step = 0U;
    return batch_step(call, now);
}

void CoreAccess::note_step(RunningCall& call, std::size_t index, std::string_view tool, bool ok,
                           std::string_view text) {
    if (!call.steps_json.empty()) call.steps_json += ",";
    call.steps_json += "{\"step\":" + std::to_string(index + 1U) + ",\"tool\":";
    detail::append_json_string(tool, call.steps_json);
    call.steps_json += ok ? ",\"ok\":true,\"text\":" : ",\"ok\":false,\"error\":";
    detail::append_json_string(text, call.steps_json);
    call.steps_json += "}";
}

ToolReply CoreAccess::batch_failure(RunningCall& call, ToolError error, std::string message, std::string hint) {
    ToolReply reply = ToolReply::failure(error, std::move(message), std::move(hint));
    if (ToolFailure* const failure = reply.failure_value()) {
        (*failure).detail_json = "{\"steps\":[" + call.steps_json + "]}";
    }
    return reply;
}

StepOutcome CoreAccess::finish_batch(RunningCall& call, ToolReply reply) {
    call.current.reset();
    return StepOutcome::finished(std::move(reply));
}

StepOutcome CoreAccess::batch_step(RunningCall& call, FrameTime now) {
    AutomationControl& control = core.control();
    while (true) {
        const std::size_t total = call.step_tools.size();
        std::string not_run{};
        if (call.next_step < total) {
            not_run = "steps " + std::to_string(call.next_step + 1U) + " to " + std::to_string(total) + " not run";
        }
        if (control.take_backs() != call.take_backs_at_start) {
            return finish_batch(call, batch_failure(call, ToolError::user_took_control,
                                                    "the user took control back", not_run));
        }
        if (call.current != nullptr) {
            StepOutcome outcome = core.step_call(*call.current, now);
            if (!outcome.done()) return outcome;
            ToolReply reply = *outcome.reply();
            call.current.reset();
            ToolReply finish{};
            if (record_step(call, call.next_step - 1U, reply, finish)) return finish_batch(call, std::move(finish));
            continue;
        }
        if (call.next_step >= total) {
            ToolSuccess success{};
            success.text = call.transcript + "ok: " + std::to_string(total) + " steps";
            success.images = std::move(call.images);
            return finish_batch(call, ToolReply::success(std::move(success)));
        }
        const std::size_t index = call.next_step;
        ++call.next_step;
        const std::string number = std::to_string(index + 1U);
        if (call.step_expects[index]) {
            // An expect step: a condition checked now, against a fresh tree.
            JsonValue arguments{};
            std::string error{};
            static_cast<void>(detail::read_arguments(call.step_arguments[index], arguments, error));
            ToolReply refusal{};
            RunningCall probe{};
            probe.arguments = std::move(arguments);
            Window* const window = call_window(probe, refusal);
            if (window == nullptr) {
                note_step(call, index, "expect", false, "no window");
                return finish_batch(call, batch_failure(call, ToolError::window_gone,
                                                        "step " + number + " expect failed: no window", {}));
            }
            NodeCondition condition{};
            detail::read_condition(probe.arguments, condition);
            const std::string& kind = call.step_tools[index];
            if (kind == "visual") {
                // A visual expectation reads hover and press from the
                // visual inspection, carried on the flattened nodes.
                const std::optional<std::string> state = detail::text_member(probe.arguments, "state");
                const std::optional<bool> drawn = detail::boolean_member(probe.arguments, "drawn");
                if (state.has_value()) {
                    const std::string name = *state == "hover" ? std::string("hovered")
                                             : *state == "press" ? std::string("pressed")
                                                                 : *state;
                    condition.state = drawn.has_value() && !*drawn ? "-" + name : name;
                }
            } else if (kind != "node") {
                note_step(call, index, "expect", false, "no expectation " + kind);
                return finish_batch(call, batch_failure(call, ToolError::invalid_arguments,
                                                        "step " + number + " expect failed: no expectation " + kind,
                                                        "expect node or expect visual"));
            }
            // An expectation that names nothing to check would pass at once
            // (round 2): refused.
            if (condition.state.empty() && !condition.value.has_value() && !condition.exists.has_value()) {
                const std::string missing = kind == "visual" ? "expect visual names a state (hover, press, focused, "
                                                               "...) to check"
                                                             : "expect node names a state, a value or exists";
                note_step(call, index, "expect", false, missing);
                return finish_batch(call, batch_failure(call, ToolError::invalid_arguments,
                                                        "step " + number + " expect failed: " + missing, {}));
            }
            std::vector<FlatNode> nodes{};
            core.flatten_window(*window, nodes);
            std::string observed{};
            if (detail::condition_holds(nodes, condition, observed)) {
                call.transcript += "step " + number + " expect: " + observed + "\n";
                note_step(call, index, "expect", true, observed);
                continue;
            }
            call.transcript += "step " + number + " expect failed: " + observed + "\n";
            note_step(call, index, "expect", false, observed);
            if (call.stop_on_error) {
                std::string hint{};
                if (call.next_step < total) {
                    hint = "steps " + std::to_string(call.next_step + 1U) + " to " + std::to_string(total) + " not run";
                }
                return finish_batch(call, batch_failure(call, ToolError::expectation_failed,
                                                        "step " + number + " expect failed: " + observed, hint));
            }
            continue;
        }
        std::unique_ptr<RunningCall> step = std::make_unique<RunningCall>();
        (*step).id = call.id;
        (*step).tool = call.step_tools[index];
        (*step).chain = call.chain;
        (*step).terse = call.terse;
        std::string error{};
        if (!detail::read_arguments(call.step_arguments[index], (*step).arguments, error)) {
            note_step(call, index, call.step_tools[index], false, error);
            return finish_batch(call, batch_failure(call, ToolError::invalid_arguments,
                                                    "step " + number + " failed: " + error, {}));
        }
        StepOutcome outcome = first_step(*step, now);
        if (!outcome.done()) {
            call.current = std::move(step);
            return outcome;
        }
        ToolReply finish{};
        if (record_step(call, index, *outcome.reply(), finish)) return finish_batch(call, std::move(finish));
    }
}

bool CoreAccess::record_step(RunningCall& call, std::size_t index, ToolReply& reply, ToolReply& finish) {
    const std::size_t total = call.step_tools.size();
    const std::string number = std::to_string(index + 1U);
    const std::string& tool = call.step_tools[index];
    if (ToolSuccess* const success = reply.success_value()) {
        call.transcript += "step " + number + " " + tool + ": " + (*success).text + "\n";
        note_step(call, index, tool, true, (*success).text);
        for (ToolImage& image : (*success).images) call.images.push_back(std::move(image));
        return false;
    }
    const ToolFailure& failure = *reply.failure_value();
    call.transcript +=
        "step " + number + " " + tool + " failed: " + tool_error_name(failure.error) + ": " + failure.message + "\n";
    if (!call.stop_on_error && failure.error != ToolError::user_took_control) return false;
    std::string hint = failure.hint;
    if (index + 1U < total) {
        if (!hint.empty()) hint += "; ";
        hint += "steps " + std::to_string(index + 2U) + " to " + std::to_string(total) + " not run";
    }
    note_step(call, index, tool, false, std::string(tool_error_name(failure.error)) + ": " + failure.message);
    finish = batch_failure(call, failure.error,
                           "step " + number + " " + tool + " failed: " + failure.message, hint);
    return true;
}

// ---- the step machine's entry points -------------------------------------------------------------

void AutomationCore::note_calls_finished() noexcept {
    if (!calls_.empty()) return;
    if (AutomationPresenter* const presenter = control_.presenter()) (*presenter).calls_finished();
}

namespace {

const char* choice_name(HostDialogChoice choice) noexcept {
    switch (choice) {
    case HostDialogChoice::ok: return "OK";
    case HostDialogChoice::yes: return "Yes";
    case HostDialogChoice::no: return "No";
    case HostDialogChoice::retry: return "Retry";
    case HostDialogChoice::abort: return "Abort";
    case HostDialogChoice::ignore: return "Ignore";
    case HostDialogChoice::try_again: return "Try Again";
    case HostDialogChoice::continue_: return "Continue";
    case HostDialogChoice::help: return "Help";
    case HostDialogChoice::none:
    case HostDialogChoice::cancel: return "Cancel";
    }
    return "Cancel";
}

const char* buttons_names(HostMessageButtons buttons) noexcept {
    switch (buttons) {
    case HostMessageButtons::ok: return "OK";
    case HostMessageButtons::ok_cancel: return "OK, Cancel";
    case HostMessageButtons::yes_no: return "Yes, No";
    case HostMessageButtons::yes_no_cancel: return "Yes, No, Cancel";
    case HostMessageButtons::retry_cancel: return "Retry, Cancel";
    case HostMessageButtons::abort_retry_ignore: return "Abort, Retry, Ignore";
    case HostMessageButtons::cancel_try_continue: return "Cancel, Try Again, Continue";
    }
    return "OK";
}

// The questions the program raised during a call, for its reply: each
// was the model's, answered and never shown (owner, round 3).
// Appends the questions the program asked during the call; true when one
// stopped it (a question answered with nothing, a cancel).
bool append_model_questions(const std::vector<gui_forms::detail::ModelQuestion>& asked, bool disconnecting,
                            std::string& text) {
    bool stopped = false;
    for (const gui_forms::detail::ModelQuestion& question : asked) {
        if (question.path_dialog) {
            text += "\nquestion: the program opened " + question.title +
                    "; it was cancelled, not shown (give a path to the program's own tool instead)";
            continue;
        }
        text += "\nquestion: the program asked \"" + question.title;
        if (!question.message.empty()) text += ": " + question.message;
        text += "\" (" + std::string(buttons_names(question.buttons)) + "), answered " + choice_name(question.choice) +
                "; not shown to the user";
        if (question.choice == HostDialogChoice::none && question.buttons != HostMessageButtons::ok) stopped = true;
    }
    if (stopped && disconnecting) {
        text += "\nNothing closed and nothing was shown; the program stays as it was and you keep your session. "
                "Save the work it names (the program's save tool), then disconnect again; or disconnect with mode "
                "\"hard\" (the user is asked) or mode \"hard\" and discard_unsaved true.";
    } else if (stopped) {
        text += "\nThe program stopped there and stays as it was; nothing is left waiting. To go on, save first "
                "(the program's save tool), then repeat the call; or repeat it with on_unsaved \"save\" or "
                "\"discard\".";
    }
    return stopped;
}

} // namespace

void AutomationCore::finish_reply(RunningCall& call, StepOutcome& outcome) {
    const ToolReply* const finished = outcome.reply();
    if (finished == nullptr) return;
    ToolReply reply = *finished;
    std::string extra{};
    const bool disconnecting = call.tool == "disconnect";
    if (call.questions_open) {
        std::vector<gui_forms::detail::ModelQuestion> asked{};
        gui_forms::detail::end_model_questions(call.id.value, asked);
        call.questions_open = false;
        const bool stopped = append_model_questions(asked, disconnecting, extra);
        if (stopped && call.tool == "close") reply = ToolReply::success("not closed: the program has unsaved work");
        if (stopped && disconnecting) {
            // A soft disconnect balked: the session goes on.
            reply = ToolReply::success("not disconnected: the program has unsaved work");
        }
    }
    if (disconnecting && !call.disconnect_hard && reply.success_value() != nullptr &&
        (*reply.success_value()).text.rfind("ok:", 0) == 0) {
        // A soft disconnect that went through: the session ends.
        end_session();
    }
    const std::string notice = control_.take_notice();
    if (!notice.empty()) extra += "\nnote: " + notice;
    detail::append_pending_states(extra);
    if (ToolSuccess* const success = reply.success_value()) {
        if (call.took_control && !disconnecting && call.tool != "close") {
            Window* const window = call.window.get();
            detail::JsonValue none{};
            std::string error{};
            if (window != nullptr && detail::read_arguments("{}", none, error)) {
                const ToolReply snapshot = detail::snapshot_tool(*this, *window, none);
                if (const ToolSuccess* const outline = snapshot.success_value()) {
                    (*success).text += "\nYou now hold control (AI mode): later acts run at once until you call "
                                       "release_control or 30 s pass without a call. The window now:\n" +
                                       (*outline).text;
                }
            }
        }
        (*success).text += extra;
    } else if (ToolFailure* const failure = reply.failure_value()) {
        if (!extra.empty()) (*failure).hint += extra;
    }
    outcome = StepOutcome::finished(std::move(reply));
}

StepOutcome AutomationCore::step_call(RunningCall& call, FrameTime now) {
    CoreAccess access{*this};
    switch (call.phase) {
    case CallPhase::starting:
        return access.first_step(call, now);
    case CallPhase::awaiting_control:
        call.phase = CallPhase::starting;
        return access.first_step(call, now);
    case CallPhase::awaiting_consent:
        return access.consent_then_post(call, now);
    case CallPhase::settling:
        return access.settle(call, now);
    case CallPhase::waiting:
        return access.wait_step(call, now);
    case CallPhase::batch:
        return access.batch_step(call, now);
    case CallPhase::resetting:
        return access.reset_wait(call, now);
    }
    return StepOutcome::finished(ToolReply::failure(ToolError::internal, "the call reached an unknown phase"));
}

CallStart AutomationCore::begin(std::string_view tool, std::string arguments_json, FrameTime now) {
    CallStart start{};
    control_.note_model_call(now);
    control_.tick(now);
    if (calls_.size() >= settings_.maximum_running_calls) {
        start.outcome = StepOutcome::finished(ToolReply::failure(
            ToolError::busy, "the program is running " + std::to_string(calls_.size()) + " calls", "try again"));
        return start;
    }
    if (next_call_ == std::numeric_limits<std::uint64_t>::max()) {
        start.outcome = StepOutcome::finished(ToolReply::failure(ToolError::internal, "call identities are exhausted"));
        return start;
    }
    std::unique_ptr<RunningCall> call = std::make_unique<RunningCall>();
    (*call).id = CallId{next_call_};
    ++next_call_;
    (*call).tool = std::string(tool);
    (*call).started = now;
    std::string error{};
    if (!detail::read_arguments(arguments_json, (*call).arguments, error)) {
        start.outcome = StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, error));
        return start;
    }
    (*call).take_backs_at_start = control_.take_backs();
    try {
        start.outcome = step_call(*call, now);
    } catch (const std::exception& failure) {
        start.outcome = StepOutcome::finished(ToolReply::failure(ToolError::internal, failure.what()));
        // A question scope the failed step opened would answer the program's
        // questions (a close's save question) for the model from then on.
        if ((*call).questions_open) {
            std::vector<gui_forms::detail::ModelQuestion> asked{};
            gui_forms::detail::end_model_questions((*call).id.value, asked);
        }
        return start;
    }
    if (start.outcome.done()) finish_reply(*call, start.outcome);
    if (!start.outcome.done()) {
        start.id = (*call).id;
        calls_.push_back(std::move(call));
        return start;
    }
    note_calls_finished();
    return start;
}

StepOutcome AutomationCore::step(CallId id, FrameTime now) {
    control_.tick(now);
    for (std::size_t index = 0U; index < calls_.size(); ++index) {
        if ((*calls_[index]).id != id) continue;
        RunningCall& call = *calls_[index];
        StepOutcome outcome = StepOutcome::finished(ToolReply::failure(ToolError::internal, "the call failed"));
        try {
            outcome = step_call(call, now);
        } catch (const std::exception& failure) {
            outcome = StepOutcome::finished(ToolReply::failure(ToolError::internal, failure.what()));
        }
        if (outcome.done()) {
            finish_reply(call, outcome);
            calls_.erase(calls_.begin() + static_cast<std::ptrdiff_t>(index));
            note_calls_finished();
        }
        return outcome;
    }
    return StepOutcome::finished(ToolReply::failure(ToolError::cancelled, "the call is not running"));
}

void AutomationCore::cancel(CallId id) noexcept {
    for (std::size_t index = 0U; index < calls_.size(); ++index) {
        if ((*calls_[index]).id != id) continue;
        if ((*calls_[index]).questions_open) {
            std::vector<gui_forms::detail::ModelQuestion> asked{};
            gui_forms::detail::end_model_questions(id.value, asked);
        }
        calls_.erase(calls_.begin() + static_cast<std::ptrdiff_t>(index));
        note_calls_finished();
        return;
    }
}

ToolReply AutomationCore::run(std::string_view tool, std::string arguments_json, AutomationPump& pump) {
    CallStart start = begin(tool, std::move(arguments_json), pump.now());
    StepOutcome outcome = std::move(start.outcome);
    while (!outcome.done()) {
        const FrameTime* const when = outcome.resume_time();
        pump.advance(*when);
        outcome = step(start.id, pump.now());
    }
    ToolReply reply = *outcome.reply();
    return reply;
}

} // namespace gui_forms::automation
