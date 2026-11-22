#include "automation_calls.hpp"

#include "../../../core/host/session/session_directory.hpp"
#include "../../commands/command_directory/command_directory.hpp"
#include "../marks/automation_marks_internal.hpp"
#include "../model_extensions/model_inspect_sections.hpp"

#include "gui_forms/automation/marks/automation_marks.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/host/session/host_session.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <exception>
#include <limits>

namespace gui_forms::automation {

AutomationPump::~AutomationPump() = default;

namespace {

using detail::JsonKind;
using detail::JsonValue;

// ---- tool names and schemas ------------------------------------------------------

constexpr std::string_view command_tool_prefix = "command.";

// The arguments every tool that reads or acts on a window takes.
constexpr const char* window_property = "\"window\":{\"type\":\"string\",\"description\":\"a window's stable ID or "
                                        "title; default the active window\"}";
constexpr const char* target_properties =
    "\"target\":{\"type\":\"string\",\"description\":\"a node's stable ID, or #r<n> for a runtime ID\"},"
    "\"within\":{\"type\":\"string\",\"description\":\"an ancestor's stable ID, when the target's ID repeats\"}";

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

void append_built_in_specs(std::vector<ToolSpec>& specs) {
    const std::string targets = target_properties;
    specs.push_back(built_in("windows", "Windows", "Every window: stable ID, title, active, modal, size.",
                             "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",
                             ToolAccess::read_only));
    specs.push_back(built_in(
        "snapshot", "Snapshot",
        "The control tree as an outline: role, name, [stable ID], states, value. Narrow it with root and filter.",
        object_schema("\"root\":{\"type\":\"string\"},\"filter\":{\"type\":\"string\"},"
                      "\"limit\":{\"type\":\"integer\",\"minimum\":1},\"bounds\":{\"type\":\"boolean\"}"),
        ToolAccess::read_only));
    specs.push_back(built_in(
        "act", "Act",
        "A semantic action on a node (press, select, expand, collapse, focus, set_value, increment, decrement, "
        "show_menu, close) or invoke_command on a command's stable ID. Waits for idle and replies with what changed.",
        object_schema(targets +
                      ",\"action\":{\"type\":\"string\",\"enum\":[\"press\",\"select\",\"expand\",\"collapse\","
                      "\"focus\",\"set_value\",\"increment\",\"decrement\",\"show_menu\",\"close\","
                      "\"invoke_command\"]},\"value\":{\"type\":\"string\"}"),
        ToolAccess::acting));
    specs.push_back(built_in(
        "pointer", "Pointer",
        "Pointer input through the program's own input path: move, hover, click, double_click, right_click, down, "
        "up, drag, wheel; at a target (offset inside it, default its centre) or a window position.",
        object_schema(targets +
                      ",\"action\":{\"type\":\"string\",\"enum\":[\"move\",\"hover\",\"click\",\"double_click\","
                      "\"right_click\",\"down\",\"up\",\"drag\",\"wheel\"]},"
                      "\"offset\":{\"type\":\"object\"},\"position\":{\"type\":\"object\"},"
                      "\"path\":{\"type\":\"array\",\"items\":{\"type\":\"object\"},\"maxItems\":256},"
                      "\"button\":{\"type\":\"string\",\"enum\":[\"primary\",\"secondary\",\"middle\"]},"
                      "\"modifiers\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
                      "\"delta\":{\"type\":\"object\"}"),
        ToolAccess::acting));
    specs.push_back(built_in(
        "keyboard", "Keyboard",
        "Key chords (\"Ctrl+S\") and sequences, committed text, and composition, to the focused control or to a "
        "target focused first.",
        object_schema(targets +
                      ",\"keys\":{\"type\":[\"string\",\"array\"],\"items\":{\"type\":\"string\"}},"
                      "\"text\":{\"type\":\"string\"},\"composition\":{\"type\":\"object\"}"),
        ToolAccess::acting));
    specs.push_back(built_in(
        "text", "Text",
        "A text control's value, selection and lines; with select, sets the selection (UTF-8 byte offsets).",
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
        "A window or control as a PNG, drawn fresh from the current state (rendered). Password fields are painted "
        "out.",
        object_schema(targets + ",\"mode\":{\"type\":\"string\",\"enum\":[\"rendered\",\"presented\"]},"
                                "\"scale\":{\"type\":\"number\"},\"margin\":{\"type\":\"number\"},"
                                "\"include_overlays\":{\"type\":\"boolean\"}"),
        ToolAccess::read_only));
    specs.push_back(built_in(
        "inspect", "Inspect",
        "Beyond accessibility: a control's visual state, layout, paint operations and schema, or the window's "
        "metrics, dispatcher, interaction and session; choose sections with what.",
        object_schema(targets + ",\"what\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}"),
        ToolAccess::read_only));
    specs.push_back(built_in(
        "batch", "Batch",
        "Several steps in one call, each {\"tool\": name, ...arguments} or {\"expect\": \"node\"|\"visual\", ...}, "
        "with one combined reply; stops at the first failure.",
        "{\"type\":\"object\",\"properties\":{\"steps\":{\"type\":\"array\",\"items\":{\"type\":\"object\"},"
        "\"minItems\":1,\"maxItems\":64},\"stop_on_error\":{\"type\":\"boolean\"}},\"required\":[\"steps\"],"
        "\"additionalProperties\":false}",
        ToolAccess::acting));
    specs.push_back(built_in(
        "control", "Control",
        "direct_begin and direct_end hold the interface with a take-back popup (launched --model-directed); "
        "suspend_input and resume_input in builds with the test controls; status.",
        "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"direct_begin\","
        "\"direct_end\",\"suspend_input\",\"resume_input\",\"status\"]}},\"required\":[\"action\"],"
        "\"additionalProperties\":false}",
        ToolAccess::acting));
}

bool built_in_name(std::string_view name) noexcept {
    const bool known = name == "windows" || name == "snapshot" || name == "act" || name == "pointer" ||
                       name == "keyboard" || name == "text" || name == "wait" || name == "capture" ||
                       name == "inspect" || name == "batch" || name == "control";
    return known;
}

// A command's tool name: "command." and its stable ID, every character
// outside the tool alphabet as '_', cut to the longest name.
std::string command_tool_name(std::string_view stable_id) {
    std::string name(command_tool_prefix);
    for (const char character : stable_id) {
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        const bool punctuation = character == '_' || character == '-' || character == '.';
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
    bool blocked = false;
    Window* free_window = nullptr;
    for (Window* const candidate : windows) {
        const HostSession* const session = gui_forms::detail::window_session(*candidate);
        if (session == nullptr) continue;
        if ((*session).snapshot().modal_depth != 0U) {
            blocked = true;
        } else if (free_window == nullptr) {
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
        } catch (const std::exception& failure) {
            (*outcome).performed = false;
            (*outcome).detail = std::string("the act failed: ") + failure.what();
        }
    }
};

// ---- small parsers --------------------------------------------------------------------

bool read_point(const JsonValue& object, Point& point) {
    if (object.kind != JsonKind::object) return false;
    const std::optional<double> x = detail::number_member(object, "x");
    const std::optional<double> y = detail::number_member(object, "y");
    if (!x.has_value() || !y.has_value()) return false;
    point = Point{*x, *y};
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
    return event;
}

std::string window_list_line(AutomationCore& core) {
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    std::string line{};
    for (Window* const window : windows) {
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
        CoreRouteAccess::record(core, std::move(event));
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
    // Holds owned by running batches end with them.
    for (const std::unique_ptr<RunningCall>& call : calls_) {
        if ((*call).chain.value != 0U) control_.chain_finished((*call).chain);
    }
    if (AutomationPresenter* const presenter = control_.presenter()) {
        for (AutomationWindow& entry : windows_) {
            if (Window* const window = entry.window.get()) (*presenter).window_detached(*window);
        }
    }
}

bool AutomationCore::attach_window(Window& window, std::string stable_id) {
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
    windows_.push_back(AutomationWindow{window.window_ref(), std::move(stable_id)});
    if (presenter != nullptr) (*presenter).window_attached(window);
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
    std::size_t index = 0U;
    while (index < windows_.size()) {
        if (windows_[index].window.get() == nullptr) {
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
        for (const AutomationWindow& entry : windows_) {
            Window* const window = entry.window.get();
            if ((*window).active()) return window;
        }
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

std::string AutomationCore::window_stable_id(const Window& window) const {
    for (const AutomationWindow& entry : windows_) {
        if (entry.window.get() == &window) return entry.stable_id;
    }
    return std::string{};
}

void AutomationCore::set_client(const ClientIdentity& client) {
    control_.set_client(client);
}

void AutomationCore::flatten_window(Window& window, std::vector<FlatNode>& nodes) {
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const InteractionSnapshot interaction = window.interaction_snapshot();
    detail::flatten_snapshot(snapshot, interaction, nodes);
}

std::uint64_t AutomationCore::tool_generation() const {
    const std::uint64_t generation = tools().generation() + gui_forms::detail::command_directory_generation();
    return generation;
}

void AutomationCore::fill_tool_specs(std::vector<ToolSpec>& specs) {
    specs.clear();
    append_built_in_specs(specs);
    std::vector<ToolSpec> registered{};
    tools().fill_specs(registered);
    for (ToolSpec& spec : registered) specs.push_back(std::move(spec));
    std::vector<Command*> commands{};
    gui_forms::detail::fill_live_commands(commands);
    for (Command* const command : commands) {
        if ((*command).is_disposed() || (*command).stable_id().empty()) continue;
        if (detail::command_hidden_mark((*command).stable_id())) continue;
        const CommandState& state = (*command).state();
        if (!state.visible) continue;
        ToolSpec spec{};
        spec.name = command_tool_name((*command).stable_id());
        spec.title = state.text;
        spec.description = "The program's command \"" + state.text + "\"";
        if (!state.description.empty()) spec.description += ": " + state.description;
        if (!state.shortcut.empty()) spec.description += " (" + state.shortcut + ")";
        if (!state.enabled) {
            spec.description += ". Disabled now";
            if (!state.availability_reason.empty()) spec.description += ": " + state.availability_reason;
        }
        spec.input_schema = object_schema(std::string_view{});
        spec.access = ToolAccess::acting;
        spec.destructive = state.destructive;
        spec.settles = true;
        specs.push_back(std::move(spec));
    }
}

// ---- running calls ---------------------------------------------------------------------------

struct CoreAccess final {
    AutomationCore& core;
    using RunningCall = AutomationCore::RunningCall;

    // The window a call names (its "window" argument), or null with
    // `reply` set.
    Window* call_window(RunningCall& call, ToolReply& reply) {
        const std::optional<std::string> name = detail::text_member(call.arguments, "window");
        Window* const window = core.find_window(name.has_value() ? *name : std::string{});
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
    StepOutcome control_tool(RunningCall& call, FrameTime now);
    StepOutcome windows_tool();
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
};

StepOutcome CoreAccess::windows_tool() {
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    if (windows.empty()) return StepOutcome::finished(ToolReply::failure(ToolError::window_gone, "no window is open"));
    std::string text{};
    for (Window* const window : windows) {
        detail::append_window_line(core, *window, text);
        const HostSession* const session = gui_forms::detail::window_session(*window);
        if (session != nullptr && (*session).snapshot().modal_depth != 0U) {
            text.insert(text.size() - 1U, " (waiting on a modal window)");
        }
    }
    return StepOutcome::finished(ToolReply::success(std::move(text)));
}

bool CoreAccess::resolve_for_act(RunningCall& call, Window& window, std::vector<FlatNode>& nodes, bool required,
                                 std::size_t& index, ToolReply& reply) {
    core.flatten_window(window, nodes);
    index = no_parent;
    const OpenDialog dialog = find_open_dialog(core, window, nodes);
    if (dialog.open && (!dialog.drivable || (dialog.window != nullptr && dialog.window != &window))) {
        reply = ToolReply::failure(ToolError::dialog_open, "a dialog is open: [" + dialog.id + "]",
                                   dialog.drivable ? "act in the dialog's window [" + dialog.id + "]"
                                                   : "read it with snapshot and ask the user to answer it");
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
        reply = ToolReply::failure(ToolError::not_found, "no node [" + *target + "]", resolved.hint);
        return false;
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
    if (!has_semantic_state(node.states, SemanticState::visible)) {
        reply = ToolReply::failure(ToolError::not_visible, detail::node_reference(node) + " is not visible",
                                   "scroll it into view or open its parent first");
        return false;
    }
    return true;
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
    const std::optional<SemanticAction> action = detail::parse_semantic_action(*action_name);
    if (!action.has_value()) {
        return StepOutcome::finished(
            ToolReply::failure(ToolError::invalid_arguments, "no action " + *action_name,
                               "press, select, expand, collapse, focus, set_value, increment, decrement, show_menu, "
                               "close, invoke_command"));
    }
    std::vector<FlatNode> nodes{};
    std::size_t index = no_parent;
    ToolReply refusal{};
    if (!resolve_for_act(call, window, nodes, true, index, refusal)) return StepOutcome::finished(std::move(refusal));
    const FlatNode& node = nodes[index];
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
    act.summary = *action_name + " " + detail::node_reference(node) + " (semantic action)";
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
    if (index != no_parent) {
        const Rect bounds = nodes[index].bounds;
        point = Point{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0};
        const JsonValue* const offset = detail::kind_member(call.arguments, "offset", JsonKind::object);
        Point inside{};
        if (offset != nullptr && read_point(*offset, inside)) point = Point{bounds.x + inside.x, bounds.y + inside.y};
        placed = true;
    }
    const JsonValue* const position = detail::kind_member(call.arguments, "position", JsonKind::object);
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
                ToolReply::failure(ToolError::invalid_arguments, "drag names a path of window positions {x, y}"));
        }
        time += step;
        events.push_back(pointer_event(PointerAction::down, button, point, modifiers, time, 1U));
        Point last = point;
        for (const JsonValue& item : (*path).items) {
            if (!read_point(item, last)) {
                return StepOutcome::finished(
                    ToolReply::failure(ToolError::invalid_arguments, "each path point is {x, y}"));
            }
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
        break;
    }
    // The question could not be asked: Don't Allow.
    return StepOutcome::finished(ToolReply::failure(ToolError::declined, "the destructive-act question was not asked"));
}

StepOutcome CoreAccess::post_act(RunningCall& call, FrameTime now) {
    Window* const window = call.window.get();
    if (window == nullptr) return StepOutcome::finished(ToolReply::failure(ToolError::window_gone, "the window closed"));
    call.paint_passes_before = detail::paint_passes_of(*window);
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    call.windows_before.clear();
    for (Window* const open : windows) call.windows_before.push_back(core.window_stable_id(*open));
    call.outcome = std::make_shared<ActOutcome>();
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
    std::string busy{};
    bool idle = true;
    std::vector<Window*> windows{};
    core.fill_windows(windows);
    for (Window* const open : windows) {
        if (!detail::window_idle(*open, now, core.settings().idle_horizon, busy)) {
            idle = false;
            break;
        }
    }
    if (!idle && now < call.deadline) return StepOutcome::resume_at(now + core.settings().poll_interval);
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
    if (window != nullptr) {
        std::vector<FlatNode> after{};
        core.flatten_window(*window, after);
        detail::append_diff(call.before, after, core.settings().maximum_diff_entries, text);
    } else {
        text += "the window closed\n";
    }
    for (Window* const open : windows) {
        const std::string id = core.window_stable_id(*open);
        if (std::find(call.windows_before.begin(), call.windows_before.end(), id) != call.windows_before.end()) continue;
        text += "opened: window \"" + (*open).text.read() + "\" [" + id + "]\n";
    }
    const std::int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - call.started).count();
    const std::uint64_t frames = window == nullptr ? 0U : detail::paint_passes_of(*window) - call.paint_passes_before;
    if (idle) {
        text += "frames: " + std::to_string(frames) + " presented; idle after " + std::to_string(elapsed) + " ms";
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

StepOutcome CoreAccess::wait_step(RunningCall& call, FrameTime now) {
    Window* const window = call.window.get();
    if (window == nullptr) return StepOutcome::finished(ToolReply::failure(ToolError::window_gone, "the window closed"));
    const WaitSpec& wait = call.wait;
    std::string busy{};
    std::string observed{};
    bool done = false;
    switch (wait.until) {
    case WaitUntil::frame:
        done = detail::paint_passes_of(*window) > wait.paint_passes;
        if (!done && detail::window_idle(*window, now, core.settings().idle_horizon, busy)) {
            // Nothing wants a frame: none will come, which is what a frame
            // wait learns.
            done = true;
            observed = "no frame was wanted";
        }
        break;
    case WaitUntil::idle:
        done = detail::window_idle(*window, now, core.settings().idle_horizon, busy);
        break;
    case WaitUntil::time:
        done = now >= wait.target_time;
        break;
    case WaitUntil::node: {
        std::vector<FlatNode> nodes{};
        core.flatten_window(*window, nodes);
        done = detail::condition_holds(nodes, wait.condition, observed);
        busy = observed;
        break;
    }
    case WaitUntil::user_step:
        done = !user_step_pending(wait.user_step.c_str());
        busy = "the user has not completed [" + wait.user_step + "]";
        break;
    }
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

StepOutcome CoreAccess::control_tool(RunningCall& call, FrameTime now) {
    const std::optional<std::string> action = detail::text_member(call.arguments, "action");
    AutomationControl& control = core.control();
    const std::string name = action.has_value() ? *action : std::string{};
    ControlRefusal refusal = ControlRefusal::none;
    if (name == "direct_begin") {
        refusal = control.begin_directed(call.chain, now);
    } else if (name == "suspend_input") {
        refusal = control.begin_suspend(call.chain, now);
    } else if (name == "direct_end" || name == "resume_input") {
        control.end_hold();
    } else if (name != "status") {
        return StepOutcome::finished(ToolReply::failure(ToolError::invalid_arguments, "no control action " + name));
    }
    switch (refusal) {
    case ControlRefusal::none:
        break;
    case ControlRefusal::not_launched_directed:
        return StepOutcome::finished(ToolReply::failure(
            ToolError::unsupported_action, "the program was not launched with --model-directed",
            "act alongside the user (first come, first served), or ask for a launch with --model-directed"));
    case ControlRefusal::not_in_this_build:
        return StepOutcome::finished(ToolReply::failure(ToolError::not_in_this_build,
                                                        "suspend_input is in debug and test builds only",
                                                        "use direct_begin in a launch with --model-directed"));
    case ControlRefusal::held_otherwise:
        return StepOutcome::finished(ToolReply::failure(ToolError::busy, "the interface is held the other way",
                                                        "end that hold first"));
    }
    std::string text = "ok: control ";
    text += control_state_name(control.state());
    if (control.state() != ControlState::user_control) {
        text += call.chain.value != 0U ? " until this batch completes"
                                       : " until direct_end, the user takes it back, or 30 s without a call";
    }
    return StepOutcome::finished(ToolReply::success(std::move(text)));
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
    std::string arguments_json{};
    detail::object_without(call.arguments, std::string_view{}, arguments_json);
    if (tool == "windows") return windows_tool();
    if (tool == "control") return control_tool(call, now);
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
    const std::optional<bool> stop = detail::boolean_member(call.arguments, "stop_on_error");
    call.stop_on_error = !stop.has_value() || *stop;
    // The batch's own chain: a control step inside it holds until it ends.
    call.chain = ChainId{call.id.value};
    call.phase = CallPhase::batch;
    call.next_step = 0U;
    return batch_step(call, now);
}

StepOutcome CoreAccess::finish_batch(RunningCall& call, ToolReply reply) {
    core.control().chain_finished(call.chain);
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
        if (control.chain_interrupted(call.chain)) {
            return finish_batch(call, ToolReply::failure(ToolError::user_took_control,
                                                         call.transcript + "the user took control back", not_run));
        }
        if (control.chain_timed_out(call.chain)) {
            return finish_batch(call, ToolReply::failure(ToolError::timeout,
                                                         call.transcript + "suspend_input's ceiling passed", not_run));
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
                call.transcript += "step " + number + " expect failed: no window\n";
                return finish_batch(call, ToolReply::failure(ToolError::window_gone, call.transcript));
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
                call.transcript += "step " + number + " expect failed: no expectation " + kind + "\n";
                return finish_batch(call, ToolReply::failure(ToolError::invalid_arguments, call.transcript,
                                                             "expect node or expect visual"));
            }
            std::vector<FlatNode> nodes{};
            core.flatten_window(*window, nodes);
            std::string observed{};
            if (detail::condition_holds(nodes, condition, observed)) {
                call.transcript += "step " + number + " expect: " + observed + "\n";
                continue;
            }
            call.transcript += "step " + number + " expect failed: " + observed + "\n";
            if (call.stop_on_error) {
                std::string hint{};
                if (call.next_step < total) {
                    hint = "steps " + std::to_string(call.next_step + 1U) + " to " + std::to_string(total) + " not run";
                }
                return finish_batch(call, ToolReply::failure(ToolError::timeout, call.transcript, hint));
            }
            continue;
        }
        std::unique_ptr<RunningCall> step = std::make_unique<RunningCall>();
        (*step).id = call.id;
        (*step).tool = call.step_tools[index];
        (*step).chain = call.chain;
        std::string error{};
        if (!detail::read_arguments(call.step_arguments[index], (*step).arguments, error)) {
            call.transcript += "step " + number + " failed: " + error + "\n";
            return finish_batch(call, ToolReply::failure(ToolError::invalid_arguments, call.transcript));
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
    finish = ToolReply::failure(failure.error, call.transcript, hint);
    return true;
}

// ---- the step machine's entry points -------------------------------------------------------------

void AutomationCore::note_calls_finished() noexcept {
    if (!calls_.empty()) return;
    if (AutomationPresenter* const presenter = control_.presenter()) (*presenter).calls_finished();
}

StepOutcome AutomationCore::step_call(RunningCall& call, FrameTime now) {
    CoreAccess access{*this};
    switch (call.phase) {
    case CallPhase::starting:
        return access.first_step(call, now);
    case CallPhase::awaiting_consent:
        return access.consent_then_post(call, now);
    case CallPhase::settling:
        return access.settle(call, now);
    case CallPhase::waiting:
        return access.wait_step(call, now);
    case CallPhase::batch:
        return access.batch_step(call, now);
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
    try {
        start.outcome = step_call(*call, now);
    } catch (const std::exception& failure) {
        if ((*call).chain.value != 0U) control_.chain_finished((*call).chain);
        start.outcome = StepOutcome::finished(ToolReply::failure(ToolError::internal, failure.what()));
        return start;
    }
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
            if (call.chain.value != 0U) control_.chain_finished(call.chain);
            outcome = StepOutcome::finished(ToolReply::failure(ToolError::internal, failure.what()));
        }
        if (outcome.done()) {
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
        const ChainId chain = (*calls_[index]).chain;
        calls_.erase(calls_.begin() + static_cast<std::ptrdiff_t>(index));
        if (chain.value != 0U) control_.chain_finished(chain);
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
