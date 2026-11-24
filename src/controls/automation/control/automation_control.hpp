#pragma once

// Control and safety of the automation (notes:planning/studies/
// MCP_AUTOMATION_STUDY.md, sections 8 to 11; rulings 10 to 20):
// - who holds the interface: the user (always, by default: first come,
//   first served), a model in model.directed with its take-back popup, or a
//   model that suspended user input in a build with the test controls;
// - the per-launch question about destructive acts;
// - the presenter, which shows all of it to the user (the border glint and
//   tag line, the take-back popup, the destructive-act question, a step
//   that needs the user).
//
// The control state machine (house style 2: one fallback, which every
// unexpected state or event reaches: the user has control):
//
//   user_control ──direct_begin──► model_directed ──direct_end / chain done──► user_control
//   user_control ──suspend_input─► input_suspended ──resume / chain done────► user_control
//   model_directed, input_suspended ──take back / fail / ceiling / pipe closed──► user_control
//
// The state changes first; then the gate, the presenter and the model's
// reply learn of it (study 8.3).
//
// Thread: the UI thread of the automation's windows, the gate included
// (HostSession asks it on that thread).

#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms {
class SurfaceRoot;
class Window;
struct SemanticNode;
} // namespace gui_forms

namespace gui_forms::automation {

// Who is connected, as the client states it (study 4.4); not verified.
struct ClientIdentity final {
    std::string client_name{};
    std::string client_version{};
    std::string model_name{};
};

// The tag line's text (study 11.2): "Claude Opus 5.5 via Claude Code" when
// the client names its model, the client's name otherwise, "a model" with
// neither.
[[nodiscard]] std::string client_label(const ClientIdentity& client);

enum class ControlState : std::uint8_t {
    user_control,
    model_directed,
    input_suspended,
};

[[nodiscard]] const char* control_state_name(ControlState state) noexcept;

// What a model's request to hold the interface was answered.
enum class ControlRefusal : std::uint8_t {
    none,
    // direct_begin without --model-directed or model.directed (study 8.3).
    not_launched_directed,
    // suspend_input in a build without GUI_FORMS_MCP_TEST_CONTROLS.
    not_in_this_build,
    // The interface is held already in the other way.
    held_otherwise,
};

// Why the interface came back to the user.
enum class ControlRelease : std::uint8_t {
    // direct_end or resume_input.
    model_ended,
    // The chain that took it completed, or failed.
    chain_ended,
    // The user took control back through the popup.
    user_took_back,
    // A ceiling passed: suspend_input's hard ceiling, or no model call for
    // the idle ceiling.
    ceiling,
    // The client went away (the pipe closed), or the automation stopped.
    disconnected,
};

// The per-launch answer about destructive acts (study 10).
enum class ConsentState : std::uint8_t {
    unasked,
    asking,
    allowed,
    declined,
};

// A chain of calls that holds the interface: a batch's call, or none (0)
// for a standalone control call, which holds until direct_end, a ceiling,
// the user or a disconnect.
struct ChainId final {
    std::uint64_t value{};
    friend bool operator==(const ChainId& left, const ChainId& right) noexcept {
        const bool same = left.value == right.value;
        return same;
    }
};

// What a model is doing, for the border glint (study 11.1). A read glints
// along the border for a moment; an act and a wait light it steadily until
// the calls finish, since a moving glint schedules frames and would keep the
// window from the idle the act or wait is waiting for (study 14).
enum class AutomationActivity : std::uint8_t {
    reading,
    acting,
    waiting,
};

// What the program shows the user. The stock presenter draws it in
// GUI.Forms' design language (src/controls/automation/presenter); tests
// give their own. Virtual: the one place a run-time choice is needed
// (house style 7). Every call is on the UI thread and returns promptly;
// none may call back into the control.
class AutomationPresenter {
public:
    virtual ~AutomationPresenter();
    // A window joined the automation, or left it.
    virtual void window_attached(Window& window) = 0;
    virtual void window_detached(Window& window) noexcept = 0;
    // A client named itself, or changed its name: the tag line shows it
    // briefly and the border takes its quiet tint.
    virtual void client_changed(const ClientIdentity& client) = 0;
    // A model read the program's state or acted on it in `window`.
    virtual void activity(Window& window, AutomationActivity activity) = 0;
    // No call is running any more: the border goes back to its quiet tint.
    virtual void calls_finished() noexcept = 0;
    // The pointer moved in `window` to `position` (window coordinates), or
    // left it: the tag line shows while it hovers the top border (study
    // 11.2).
    virtual void pointer_moved(Window& window, Point position, bool left) noexcept = 0;
    // The control state changed: model_directed shows the take-back popup,
    // user_control closes it.
    virtual void control_changed(ControlState state, const ClientIdentity& client) = 0;
    // Ask, in the program, "Allow <model> to edit files and perform
    // potentially destructive acts?". The answer comes back through
    // AutomationControl::answer_consent; closing the question is Don't
    // Allow.
    virtual void ask_consent(const ClientIdentity& client) = 0;
    // A step needs the user (study 9.3): show it, highlighted.
    virtual void user_step_needed(Window& window, const SemanticNode& node, std::string_view what) = 0;
    // The model went away: every sign is removed.
    virtual void disconnected() noexcept = 0;
    // A window that is the presenter's own (the popups). The automation
    // never attaches, reads or drives one; its input is never held.
    [[nodiscard]] virtual bool presenter_window(const SurfaceRoot& window) const noexcept = 0;
};

// The launch's control settings.
struct ControlSettings final {
    // --model-directed or model.directed was given (study 8.3, step 1).
    bool model_directed_allowed{};
    // How long suspend_input may hold input whatever the chain does (study
    // 8.2: 30 s by default).
    std::chrono::milliseconds suspend_ceiling{30000};
    // A hold that no chain owns ends when no model call has arrived for
    // this long: the model's work is taken as complete.
    std::chrono::milliseconds idle_ceiling{30000};
};

// Whether this build admits suspend_input (GUI_FORMS_MCP_TEST_CONTROLS, on
// in the debug and test presets; study 17.2).
[[nodiscard]] bool test_controls_built() noexcept;

class AutomationControl final {
public:
    // Borrows `presenter` (null: nothing is shown, as in a script without a
    // window); it must outlive the control. Installs no gate until a model
    // holds the interface.
    AutomationControl(ControlSettings settings, AutomationPresenter* presenter) noexcept;
    // Gives the interface back (disconnected) and removes the gate.
    ~AutomationControl();
    AutomationControl(const AutomationControl&) = delete;
    AutomationControl& operator=(const AutomationControl&) = delete;

    [[nodiscard]] ControlState state() const noexcept { return state_; }
    [[nodiscard]] AutomationPresenter* presenter() const noexcept { return presenter_; }
    [[nodiscard]] const ClientIdentity& client() const noexcept { return client_; }
    // Records who is calling; tells the presenter when it changed.
    void set_client(const ClientIdentity& client);

    // control direct_begin and suspend_input. `chain` is the batch that asks
    // (its completion gives input back), or {} for a standalone call.
    [[nodiscard]] ControlRefusal begin_directed(ChainId chain, FrameTime now);
    [[nodiscard]] ControlRefusal begin_suspend(ChainId chain, FrameTime now);
    // control direct_end and resume_input: back to the user whoever holds.
    void end_hold();
    // A chain finished (completed or failed): a hold it owns ends.
    void chain_finished(ChainId chain);
    // The take-back popup was used: the hold ends, and the chain that holds
    // it is interrupted (its next step answers user_took_control).
    void user_took_back();
    // The pipe closed, or the automation stops.
    void disconnect() noexcept;
    // Whether `chain`'s steps must stop: the user took control back, or
    // suspend_input's ceiling passed, while it held the interface.
    [[nodiscard]] bool chain_interrupted(ChainId chain) const noexcept;
    [[nodiscard]] bool chain_timed_out(ChainId chain) const noexcept;
    // A model call arrived (resets the idle ceiling).
    void note_model_call(FrameTime now) noexcept;
    // Applies the ceilings. Called with every step and from the host's
    // polls.
    void tick(FrameTime now);

    // The per-launch question (study 10). request_consent asks once; while
    // it is asked every destructive call waits; the answer holds for the
    // rest of the launch.
    [[nodiscard]] ConsentState consent() const noexcept { return consent_; }
    void request_consent();
    void answer_consent(bool allow);

    // The gate's answer (session_directory.hpp): user input to `window` is
    // held while a model holds the interface, except input to the
    // presenter's own windows (the take-back popup is the one thing the
    // user can click, study 8.3 step 3).
    [[nodiscard]] bool holds_user_input(const SurfaceRoot& window) const noexcept;

    struct Counters final {
        std::uint64_t holds_begun{};
        std::uint64_t holds_taken_back{};
        std::uint64_t holds_ceilinged{};
        std::uint64_t consent_questions{};
    };
    [[nodiscard]] Counters counters() const noexcept { return counters_; }

private:
    static bool gate(void* context, const SurfaceRoot& window) noexcept;
    // The one transition out of a hold: the state changes, then the gate,
    // then the presenter (study 8.3: "the state changes first").
    void release(ControlRelease reason) noexcept;
    void enter(ControlState state, ChainId chain, FrameTime now);

    ControlSettings settings_;
    AutomationPresenter* presenter_{};
    ClientIdentity client_{};
    ControlState state_{ControlState::user_control};
    ChainId holder_{};
    FrameTime held_since_{};
    FrameTime last_call_{};
    // The chain the user interrupted, or whose suspend ceiling passed, while
    // it held; cleared when that chain finishes.
    ChainId interrupted_{};
    ChainId timed_out_{};
    bool gate_installed_{};
    ConsentState consent_{ConsentState::unasked};
    Counters counters_{};
};

} // namespace gui_forms::automation
