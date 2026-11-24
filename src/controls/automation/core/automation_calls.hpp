#pragma once

// A running call's record and the tool functions the core's sources share
// (automation_core.cpp runs calls; automation_reads.cpp answers the
// read-only tools and the conditions of wait and expect).

#include "automation_internal.hpp"

#include <memory>
#include <optional>

namespace gui_forms::automation {

enum class CallPhase : std::uint8_t {
    // Parsed; its first step runs next.
    starting,
    // An act waits for the user's answer to the take question (the owner's
    // control model, 2026-11-24).
    awaiting_control,
    // A destructive act waits for the user's answer (study 10).
    awaiting_consent,
    // An act was posted; the call waits for it and for idle (study 7.4).
    settling,
    // `wait` waits for its condition.
    waiting,
    // A batch runs its steps.
    batch,
};

enum class ActKind : std::uint8_t {
    semantic,
    command,
    input,
    selection,
    registry,
    reveal,
};

// An act prepared and checked, performed at a later dispatcher turn so that
// a handler's modal loop never holds the call (study 14, "Modal loops").
struct PreparedAct final {
    ActKind kind{ActKind::semantic};
    // The reply's first line after "ok: ".
    std::string summary{};
    // semantic: the node, by its control and virtual child (empty for the
    // control's own node).
    std::uint64_t control_runtime_id{};
    std::string virtual_child{};
    SemanticAction action{SemanticAction::press};
    std::string value{};
    // command: the command's stable ID, found again when performed.
    std::string command_id{};
    // input: the events, delivered in order through the window's session;
    // first, focus the target (keyboard with a target).
    std::vector<HostEventPayload> events{};
    bool focus_first{};
    // selection: the text control and its new selection.
    SemanticTextSelection selection{};
    // registry: the tool and its arguments.
    ToolEntry entry{};
    std::string arguments_json{};
    bool destructive{};
    // The target opens a dialog: the program's dialog-free tool for it, or
    // empty, named when the dialog opens.
    std::string dialog_tool{};
    // reveal: scroll the control into view in every scrolling ancestor.
    // The target is a password field: the reply says only "ok" (study 9.1).
    bool protected_target{};
};

// What a posted act did, read by the call that posted it. UI thread only.
struct ActOutcome final {
    bool ran{};
    bool performed{};
    std::string detail{};
    // A registry tool's own reply, when the act was one.
    std::optional<ToolReply> reply{};
};

enum class WaitUntil : std::uint8_t {
    frame,
    idle,
    time,
    node,
    user_step,
};

// A condition on a node, for wait and for a batch's expect steps.
struct NodeCondition final {
    std::string target{};
    std::string within{};
    // "selected" (set), "-selected" (clear); empty: no state condition.
    std::string state{};
    std::optional<bool> exists{};
    std::optional<std::string> value{};
};

struct WaitSpec final {
    WaitUntil until{WaitUntil::idle};
    FrameTime target_time{};
    std::uint64_t paint_passes{};
    NodeCondition condition{};
    std::string user_step{};
};

struct AutomationCore::RunningCall final {
    CallId id{};
    std::string tool{};
    detail::JsonValue arguments{};
    CallPhase phase{CallPhase::starting};
    FrameTime started{};
    FrameTime deadline{};
    WindowRef window{};
    // The chain whose hold this call's control steps take (a batch's own
    // id, given to its steps); {} outside a batch.
    ChainId chain{};
    PreparedAct act{};
    std::shared_ptr<ActOutcome> outcome{};
    std::vector<FlatNode> before{};
    // The open dialog and the modal scope before the act (empty: none),
    // to tell what the act opened.
    std::string dialog_before{};
    std::string scope_before{};
    // The reply gives the act's changes as counts.
    bool terse{};
    // act names a row its list does not show: resolve_for_act gave the
    // list's node, and the act goes to the row by its ID through the list.
    bool offscreen{};
    std::vector<std::string> windows_before{};
    std::uint64_t paint_passes_before{};
    WaitSpec wait{};
    // A batch's steps, as tool names and argument objects.
    std::vector<std::string> step_tools{};
    std::vector<std::string> step_arguments{};
    std::vector<bool> step_expects{};
    std::size_t next_step{};
    std::unique_ptr<RunningCall> current{};
    std::string transcript{};
    std::vector<ToolImage> images{};
    bool stop_on_error{true};
    // The user's take-backs when the call began: a different count later
    // means the user took control back while it ran.
    std::uint64_t take_backs_at_start{};
    // The act's control: checked (it may act), the take question asked for
    // it (and how many questions were answered before), and whether this
    // act's question gave the model control (its reply then carries a
    // snapshot, saving the model a call).
    bool control_checked{};
    bool asked_control{};
    std::uint64_t takes_answered_before{};
    bool took_control{};
    // The batch's steps' structured transcript (round 2): a JSON array.
    std::string steps_json{};
};

namespace detail {

// Whether `window` is idle at `now` (study 14): no posted work, no frame
// wanted, no timer due within `horizon`. `busy`, when idle is false, says
// what was still busy.
[[nodiscard]] bool window_idle(Window& window, FrameTime now, std::chrono::milliseconds horizon, std::string& busy);
[[nodiscard]] std::uint64_t paint_passes_of(const Window& window);

// The read-only tools. Each answers from `window` (live, attached).
[[nodiscard]] ToolReply snapshot_tool(AutomationCore& core, Window& window, const JsonValue& arguments);
[[nodiscard]] ToolReply text_read_tool(AutomationCore& core, Window& window, const JsonValue& arguments);
[[nodiscard]] ToolReply capture_tool(AutomationCore& core, Window& window, const JsonValue& arguments);
[[nodiscard]] ToolReply inspect_tool(AutomationCore& core, Window& window, const JsonValue& arguments,
                                     std::string_view arguments_json);
// The window line of an outline: role, title, stable ID, states, size.
void append_window_line(AutomationCore& core, Window& window, std::string& text);

// Reads a condition from a wait's or expect's arguments.
void read_condition(const JsonValue& arguments, NodeCondition& condition);
// Whether `condition` holds in `nodes`; `observed` says what was seen.
[[nodiscard]] bool condition_holds(std::span<const FlatNode> nodes, const NodeCondition& condition,
                                   std::string& observed);

// Registers the core's own inspect sections in `tools`.
void register_core_inspect_sections(ToolRegistry& tools);

} // namespace detail
} // namespace gui_forms::automation
