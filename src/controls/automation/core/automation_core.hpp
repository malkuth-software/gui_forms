#pragma once

// The automation core (notes:planning/studies/MCP_AUTOMATION_STUDY.md,
// sections 7, 8, 9, 14 and 15; stage G1): it turns tool calls into actions
// on a program's windows, only on their UI thread, and answers with
// outlines, diffs, captures and inspections. It knows nothing of MCP: the
// MCP wiring (src/runtime/automation) and the script runner drive it the
// same way, and the tests drive it on the headless host.
//
// A call is a small step machine, so nothing ever blocks the UI thread:
// begin() runs a call's first step and step() each later one, each at a
// dispatcher turn; a step finishes the call with its reply or asks to be
// resumed at a time (an action waits for idle, `wait` waits for its
// condition, a destructive act waits for the user's answer). The driver
// decides how to wait:
// - the MCP wiring, on its handler task, invokes each step on the UI thread
//   through the window's dispatcher and sleeps between them on the
//   request's context;
// - run() takes an AutomationPump, which advances a headless window's time
//   and pumps its dispatcher between steps (tests, scripts in process).
//
// Ownership: the core owns its running calls, keyed by CallId; drivers
// hold only ids. It observes its windows through WindowRef, so a window
// that closes is seen as gone and never read. The control (holds, consent,
// presenter) is borrowed and outlives the core.
//
// Thread: the UI thread that constructs it, for every member.

#include "../control/automation_control.hpp"
#include "../tools/automation_tools.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gui_forms {
class Window;
class Command;
} // namespace gui_forms

namespace gui_forms::automation {

struct AutomationSettings final {
    // How long an action waits for its window to be idle before replying
    // with what it saw (the reply then says the window was still busy).
    std::chrono::milliseconds settle_limit{5000};
    // How often a waiting call looks again.
    std::chrono::milliseconds poll_interval{8};
    // Idle allows no timer due within this (study 14: 50 ms).
    std::chrono::milliseconds idle_horizon{50};
    // A wait's default and longest timeout.
    std::chrono::milliseconds default_wait_timeout{5000};
    std::chrono::milliseconds maximum_wait_timeout{60000};
    // How long a destructive act waits for the user's answer.
    std::chrono::milliseconds consent_timeout{120000};
    // Outline nodes shown by default and at most (study 7.1, rule 6).
    std::size_t default_node_limit{400U};
    std::size_t maximum_node_limit{5000U};
    // Diff entries of each kind shown before "... and N more".
    std::size_t maximum_diff_entries{40U};
    // Calls running at once, and steps in one batch (round 3: drawing
    // batches are large; 512 steps of a few points each stay well inside
    // argument_limits' megabyte and value count).
    std::size_t maximum_running_calls{16U};
    std::size_t maximum_batch_steps{512U};
};

// A running call, issued by begin. Never reused: the counter refuses at its
// maximum (house style 3).
struct CallId final {
    std::uint64_t value{};
    friend bool operator==(const CallId& left, const CallId& right) noexcept {
        const bool same = left.value == right.value;
        return same;
    }
};

// What a step gave: resume the call at a time, or its reply.
class StepOutcome final {
public:
    [[nodiscard]] static StepOutcome resume_at(FrameTime when) {
        const StepOutcome outcome{std::variant<FrameTime, ToolReply>{when}};
        return outcome;
    }
    [[nodiscard]] static StepOutcome finished(ToolReply reply) {
        const StepOutcome outcome{std::variant<FrameTime, ToolReply>{std::move(reply)}};
        return outcome;
    }
    [[nodiscard]] bool done() const noexcept { return std::holds_alternative<ToolReply>(state_); }
    // When to step again, or null once done.
    [[nodiscard]] const FrameTime* resume_time() const& noexcept { return std::get_if<FrameTime>(&state_); }
    const FrameTime* resume_time() const&& = delete;
    // The reply, or null while running.
    [[nodiscard]] ToolReply* reply() & noexcept { return std::get_if<ToolReply>(&state_); }

private:
    explicit StepOutcome(std::variant<FrameTime, ToolReply> state) noexcept : state_(std::move(state)) {}
    std::variant<FrameTime, ToolReply> state_;
};

// begin's answer: the call's id (empty for a call finished in its first
// step) and its first outcome.
struct CallStart final {
    CallId id{};
    StepOutcome outcome{StepOutcome::resume_at(FrameTime{})};
};

// What run() waits with: the headless window's clock and pump. A named
// interface, the one run-time variation the in-process driver needs.
class AutomationPump {
public:
    virtual ~AutomationPump();
    [[nodiscard]] virtual FrameTime now() const noexcept = 0;
    // Advances time to `until` (or as far as it can), running the
    // dispatcher, the frame schedule and paint on the way.
    virtual void advance(FrameTime until) = 0;
};

// One node of a window's semantic tree, flattened in document order for
// outlines, diffs and resolution (automation_outline.cpp).
struct FlatNode final {
    std::uint64_t runtime_id{};
    // The runtime ID of the control that publishes the node: its own for a
    // control's node, the nearest control's above it for a virtual child.
    std::uint64_t control_runtime_id{};
    bool is_virtual{};
    std::string stable_id{};
    SemanticRole role{SemanticRole::generic};
    std::string name{};
    // Empty for protected content (study 9.1).
    std::string value{};
    SemanticState states{SemanticState::none};
    bool has_numeric{};
    double numeric_value{};
    bool has_range{};
    double minimum_value{};
    double maximum_value{};
    bool modal{};
    Rect bounds{};
    std::vector<SemanticAction> actions{};
    std::size_t depth{};
    // The parent's index, or npos for a root.
    std::size_t parent{};
    // This node and every node below it.
    std::size_t subtree_size{1U};
    bool hovered{};
    bool pressed{};
    // Pressing it opens a popup (a menu, a drop-down).
    bool has_popup{};
    // A row's place in its whole set, 1-based, and the set's size, when the
    // node publishes them (0: none); a virtualised list shows a window of
    // its rows and counts them all.
    std::size_t position_in_set{};
    std::size_t set_size{};
    // It opens a dialog the user answers (MCP round 1): marked with
    // set_opens_dialog, or a name ending in "..." or the ellipsis; with the
    // program's dialog-free tool for it, when it has one.
    bool opens_dialog{};
    std::string dialog_tool{};
    // A dialog's default button (Enter presses it: Button::default_button
    // or the window's accept_button) and its cancel button (Escape: the
    // window's cancel_button or a button whose dialog result is cancel)
    // (MCP round 3).
    bool default_button{};
    bool cancel_button{};
    [[nodiscard]] bool protected_content() const noexcept {
        const bool hidden = has_semantic_state(states, SemanticState::protected_content);
        return hidden;
    }
};

inline constexpr std::size_t no_parent = static_cast<std::size_t>(-1);

// A window the core drives: observed, with the stable ID it was opened
// with (or one the core gave it).
struct AutomationWindow final {
    WindowRef window{};
    std::string stable_id{};
    // A passive popup window (a tooltip): listed and captured, never taken
    // for a dialog.
    bool passive_popup{};
    // The program's handle for the window (act close on its ID asks it to
    // close); empty for a window a test attached without one.
    ApplicationWindowHandle handle{};
};

class AutomationCore final {
public:
    // Borrows `control` (it must outlive the core) and registers the
    // built-in inspect sections.
    explicit AutomationCore(AutomationControl& control, AutomationSettings settings = {});
    ~AutomationCore();
    AutomationCore(const AutomationCore&) = delete;
    AutomationCore& operator=(const AutomationCore&) = delete;

    // Tools and inspect sections beyond the built-in ones: the process-wide
    // registry behind gui_forms/automation/tools/model_tools.hpp, the seam
    // for programs and GUI.Forms' later stages.
    [[nodiscard]] static ToolRegistry& tools() noexcept { return model_tool_registry(); }
    [[nodiscard]] AutomationControl& control() noexcept { return control_; }
    [[nodiscard]] const AutomationSettings& settings() const noexcept { return settings_; }

    // Joins `window` to the automation under `stable_id` (empty: the core
    // names it "window.<n>"); a window joined already keeps its entry. The
    // presenter's own windows are refused (false). Leaves on its own when
    // the window is destroyed. `passive_popup` marks a popup window that
    // never takes focus (ApplicationWindowState::passive_popup).
    bool attach_window(Window& window, std::string stable_id, bool passive_popup = false,
                       ApplicationWindowHandle handle = ApplicationWindowHandle{});
    // Whether `window` joined as a passive popup window.
    [[nodiscard]] bool passive_popup_window(const Window& window) const noexcept;
    void detach_window(const Window& window) noexcept;
    // The live windows, oldest first. Borrowed for the current call.
    void fill_windows(std::vector<Window*>& windows);
    // Whether `window` is the active one to a model: active itself, or a
    // passive popup of it (a tooltip) holds the activation.
    [[nodiscard]] bool window_active(const Window& window) const;
    // The live window named `name` (its stable ID or exact title), or for
    // empty the default: a program window, never a tooltip's or passive
    // popup's (the active one, else the newest modal one, else the first);
    // null when none is.
    [[nodiscard]] Window* find_window(std::string_view name);
    // The stable ID the window was attached with; empty when it is not.
    [[nodiscard]] std::string window_stable_id(const Window& window) const;
    // The program's handle for `window`, or an empty one.
    [[nodiscard]] ApplicationWindowHandle window_handle(const Window& window) const;
    // The handles of the program's own windows (not passive popups), for a
    // disconnect's close.
    void program_window_handles(std::vector<ApplicationWindowHandle>& handles) const;
    // The hard disconnect with the program's default, and the transport's
    // end (a client that crashed or was killed): control returns to the
    // user, later calls are refused, and every program window is asked to
    // close as its close box asks. A question the program raises goes to the
    // user (owner, round 3).
    void hard_disconnect() noexcept;
    // Whether a disconnect ended the model's session.
    [[nodiscard]] bool session_ended() const noexcept { return ended_; }
    // Ends the session: control returns to the user and the in-use sign
    // and the band leave the windows.
    void end_session() noexcept;
    // Starts a new client's session after end_session (an attach
    // connection, MCP 1.0.5): calls are answered again, and the control
    // starts over (AutomationControl::begin_session). A session that has
    // not ended is left as it is.
    void begin_session(FrameTime now);

    // Records who is calling (the tag line, the replies).
    void set_client(const ClientIdentity& client);

    // The step machine. `now` is the window clock's time (FrameClock in a
    // live program, the pump's in a test).
    [[nodiscard]] CallStart begin(std::string_view tool, std::string arguments_json, FrameTime now);
    [[nodiscard]] StepOutcome step(CallId id, FrameTime now);
    // Ends a running call (the client cancelled, or the driver gave up); its
    // hold, if it owns one, ends with it. An unknown id changes nothing.
    void cancel(CallId id) noexcept;
    // Runs a call to completion with `pump` between steps.
    [[nodiscard]] ToolReply run(std::string_view tool, std::string arguments_json, AutomationPump& pump);

    // Every tool a model may call now: the built-in ones, the registry's,
    // and the program's commands (study 7.2). Replaces `specs`.
    void fill_tool_specs(std::vector<ToolSpec>& specs);
    // Changes when the tool list changes (registry or commands).
    [[nodiscard]] std::uint64_t tool_generation() const;

    // For tools of other stages: the flattened tree of `window` with the
    // interaction's hover and press, replacing `nodes`.
    void flatten_window(Window& window, std::vector<FlatNode>& nodes);

    struct RunningCall;

    // The event-propagation trace (inspect's "events" section): the last
    // routed pointer and key events of each window, recorded while the core
    // exists (it installs the route observer, session_directory.hpp).
    struct RoutedEvent final {
        const SurfaceRoot* window{};
        std::uint64_t sequence{};
        bool pointer{};
        std::uint8_t action{};
        std::uint32_t code{};
        Point position{};
        std::string target_stable_id{};
        std::string target_control_id{};
        bool handled{};
    };
    static constexpr std::size_t route_trace_capacity = 128U;
    // Appends the window's recorded events, oldest first, to `text`; only
    // those routed to `target_stable_id` when it is not empty.
    void append_route_trace(const SurfaceRoot& window, std::string_view target_stable_id, std::string& text) const;

private:
    friend struct CoreAccess;
    friend struct CoreRouteAccess;

    StepOutcome step_call(RunningCall& call, FrameTime now);
    void prune_windows() noexcept;
    // Tells the presenter when no call runs any more.
    void note_calls_finished() noexcept;
    // A finished top-level call's reply gains what the model must know
    // whatever it called (round 2 and the owner's control model): the
    // notice that the user took control back or that control returned, the
    // program's pending states, and, for the act whose question gave the
    // model control, the window's snapshot.
    void finish_reply(RunningCall& call, StepOutcome& outcome);

    AutomationControl& control_;
    AutomationSettings settings_;
    std::vector<AutomationWindow> windows_{};
    // A disconnect ended the session: every later call is refused.
    bool ended_{};
    std::uint64_t next_window_number_{1U};
    std::vector<std::unique_ptr<RunningCall>> calls_{};
    // The dialogs a model's act opened, by the ID find_open_dialog gives:
    // the model may close them (as Cancel), never submit them (MCP round 1).
    std::vector<std::string> model_dialogs_{};
    // Each command tool's spec as first seen, for the launch (round 2:
    // stable specs, so tools/list_changed goes out only when a command
    // comes or goes).
    std::vector<ToolSpec> command_specs_{};
    std::uint64_t next_call_{1U};
    // A ring of route_trace_capacity events across windows; route_next_ is
    // the slot written next.
    std::vector<RoutedEvent> route_trace_{};
    std::size_t route_next_{};
    std::uint64_t route_sequence_{};
};

} // namespace gui_forms::automation
