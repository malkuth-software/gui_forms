#pragma once

// Control and safety of the automation (notes:planning/studies/
// MCP_AUTOMATION_STUDY.md, sections 8 to 11; rulings 13 to 20, and the
// owner's control model of 2026-11-24, which supersedes ruling 10 and
// retires model.directed):
// - who holds the program: the user, or a model. Reads (snapshot, capture,
//   inspect) need no control. In a shared launch a model's first act asks
//   the user, with a 10-second countdown that allows it when it runs out;
//   the act waits on the answer. Once allowed the model holds control:
//   later acts run at once, and the program is in AI mode (the stripes,
//   the band, the title note and the X cursor). The model gives control
//   back with release_control, or after 30 s without a call. A launch
//   with --mcp is AI-led: the model holds control from the start, with no
//   countdown and no return after idle;
// - while a model holds control the user's input goes to the take-back
//   dialog, not to the program: any press in a held window asks "Do you
//   want to take back control of this program?". Taking it back ends the
//   model's control, interrupts its work, and its next reply says so;
// - the per-launch question about destructive acts (ruling 17);
// - the presenter, which shows all of it to the user.
//
// The control state machine (house style 2: one fallback, which every
// unexpected state or event reaches: the user has control):
//
//   user_control ──first act──► asking ──allowed / countdown ends──► model_control
//   asking ──refused / disconnected──► user_control
//   model_control ──release_control / idle 30 s / take back / pipe closed──► user_control
//   --mcp: model_control from the start; idle never returns it.
//
// The state changes first; then the gate, the presenter and the model's
// reply learn of it (study 8.3).
//
// Thread: the UI thread of the automation's windows, the gate included
// (HostSession asks it on that thread).
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <chrono>
#include <memory>
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
    // A model's act asked to take control; the countdown runs.
    asking,
    model_control,
};

[[nodiscard]] const char* control_state_name(ControlState state) noexcept;

// Why the program came back to the user.
enum class ControlRelease : std::uint8_t {
    // release_control.
    model_ended,
    // The user took control back through the take-back dialog.
    user_took_back,
    // No model call for the grace period (shared launches only).
    idle,
    // The client went away (the pipe closed), or the automation stopped.
    disconnected,
    // The presenter could not show AI mode as control began.
    presenter_failed,
};

// The per-launch answer about destructive acts (study 10).
enum class ConsentState : std::uint8_t {
    unasked,
    asking,
    allowed,
    declined,
};

// A batch's call, whose steps share it; none (0) outside a batch.
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
    // The control state changed: model_control shows AI mode (the stripes,
    // the title note, the X cursor); user_control removes it and closes the
    // take-back dialog.
    virtual void control_changed(ControlState state, const ClientIdentity& client) = 0;
    // Ask, in the program, whether <model> may take control, counting down
    // `countdown`; the answer comes back through
    // AutomationControl::answer_take. When the countdown ends the answer is
    // Allow; closing the dialog is Don't Allow.
    virtual void ask_take(const ClientIdentity& client, std::chrono::milliseconds countdown) = 0;
    // The user pressed in a held window: show "Do you want to take back
    // control of this program?". Taking back calls
    // AutomationControl::user_took_back; declining leaves the model in
    // control. Posted by the gate, so it runs outside the input's dispatch.
    virtual void offer_take_back(const ClientIdentity& client) = 0;
    // Ask, in the program, "Allow <model> to edit files and perform
    // potentially destructive acts?". The answer comes back through
    // AutomationControl::answer_consent; closing the question is Don't
    // Allow.
    virtual void ask_consent(const ClientIdentity& client) = 0;
    // A step needs the user (study 9.3): show it, highlighted.
    virtual void user_step_needed(Window& window, const SemanticNode& node, std::string_view what) = 0;
    // The model went away: every sign is removed.
    virtual void disconnected() noexcept = 0;
    // A window that is the presenter's own (the dialogs). The automation
    // never attaches, reads or drives one; its input is never held.
    [[nodiscard]] virtual bool presenter_window(const SurfaceRoot& window) const noexcept = 0;
};

// The launch's control settings.
struct ControlSettings final {
    // --mcp: the model holds control from the start, with no countdown and
    // no return after idle (the owner's control model, 2026-11-24).
    bool ai_led{};
    // --mcp-appearance-review: acts need no control and nothing is held.
    bool appearance_review{};
    // How long the take question counts down before it allows.
    std::chrono::milliseconds take_countdown{10000};
    // A shared launch's control returns to the user after this long
    // without a model call.
    std::chrono::milliseconds grace{30000};
};

// Whether this build has the test controls (GUI_FORMS_MCP_TEST_CONTROLS, on
// in the debug and test presets; study 17.2): --mcp-appearance-review.
[[nodiscard]] bool test_controls_built() noexcept;

class AutomationControl final {
public:
    // Borrows `presenter` (null: nothing is shown, as in a script without a
    // window); it must outlive the control. An AI-led launch starts in
    // model_control.
    AutomationControl(ControlSettings settings, AutomationPresenter* presenter);
    // Gives control back (disconnected) and removes the gate.
    ~AutomationControl();

    AutomationControl(const AutomationControl&) = delete;
    AutomationControl& operator=(const AutomationControl&) = delete;

    [[nodiscard]] ControlState state() const noexcept { return state_; }
    [[nodiscard]] AutomationPresenter* presenter() const noexcept { return presenter_; }
    [[nodiscard]] const ClientIdentity& client() const noexcept { return client_; }
    [[nodiscard]] const ControlSettings& settings() const noexcept { return settings_; }

    // Records who is calling; tells the presenter when it changed.
    void set_client(const ClientIdentity& client);

    // Whether an act may run now: the model holds control, or the launch
    // reviews appearance.
    [[nodiscard]] bool acts_allowed() const noexcept;
    // A model's act needs control: in user_control the take question is
    // asked (once until it is answered); in every other state nothing
    // changes. Without a presenter nobody can answer, and the take is
    // refused at once.
    void request_take(FrameTime now);
    // The take question's answer (the dialog, or its countdown's end).
    void answer_take(bool allow);
    // How the last take question was answered: true allowed, false refused
    // or never shown. Read by the act that waited on it.
    [[nodiscard]] bool last_take_allowed() const noexcept { return last_take_allowed_; }
    // How many take questions were answered so far (an act compares it to
    // tell its own question's answer from an older one).
    [[nodiscard]] std::uint64_t takes_answered() const noexcept { return counters_.takes_answered; }
    // Takes an AI-led launch's control at once (the run calls it at start).
    void begin_ai_led(FrameTime now);
    // A model call's grace override, from 1 s to 10 minutes (0 keeps the
    // launch's).
    void set_grace(std::chrono::milliseconds grace) noexcept;
    // release_control: back to the user.
    void release_by_model();
    // The take-back dialog was answered Take Back: control ends, the
    // running chain is interrupted, and the model's next reply says so.
    void user_took_back();
    // The pipe closed, or the automation stops.
    void disconnect() noexcept;
    // How many times the user has taken control back: a call that saw a
    // different count when it began was interrupted.
    [[nodiscard]] std::uint64_t take_backs() const noexcept { return counters_.holds_taken_back; }
    // The notice owed to the model since the user took control back (or
    // since idle returned it), or empty; taking it clears it.
    [[nodiscard]] std::string take_notice();
    // A model call arrived (resets the grace period).
    void note_model_call(FrameTime now) noexcept;
    // Applies the countdown and the grace period. Called with every step
    // and from the host's polls.
    void tick(FrameTime now);

    // The per-launch question (study 10). request_consent asks once; while
    // it is asked every destructive call waits; the answer holds for the
    // rest of the launch.
    [[nodiscard]] ConsentState consent() const noexcept { return consent_; }
    void request_consent();
    void answer_consent(bool allow);
    // The question could not be shown (`why`): it stays unasked, so a
    // later destructive act asks again, and the act that asked says why
    // (round 2: a question never shown is never "the user did not allow").
    void consent_not_shown(std::string why);
    [[nodiscard]] const std::string& consent_failure() const& noexcept { return consent_failure_; }
    const std::string& consent_failure() const&& = delete;

    // The gate's answer (session_directory.hpp): user input to `window` is
    // held while a model holds control, except input to the presenter's own
    // windows (the take-back dialog is what the user can use). A held press
    // offers the take-back dialog.
    [[nodiscard]] bool holds_user_input(const SurfaceRoot& window) const noexcept;

    struct Counters final {
        std::uint64_t holds_begun{};
        std::uint64_t holds_taken_back{};
        std::uint64_t holds_idle_returned{};
        std::uint64_t takes_asked{};
        std::uint64_t takes_answered{};
        std::uint64_t take_back_offers{};
        std::uint64_t consent_questions{};
    };
    [[nodiscard]] Counters counters() const noexcept { return counters_; }

    // The gate's held press (posted to the UI thread's next turn): offers
    // the take-back dialog. Public for the posted work item.
    void offer_take_back();

private:
    static bool gate(void* context, const SurfaceRoot& window, bool press) noexcept;
    // The gate for a native dialog the host runs (an NSAlert): held while
    // the model holds control, and a press offers the take-back dialog at
    // once (the host's modal loop calls it between events).
    static bool native_gate(void* context, bool press) noexcept;
    // The one transition out of model_control: the state changes, then the
    // gate, then the presenter (study 8.3: "the state changes first").
    void release(ControlRelease reason) noexcept;
    void enter_model_control(FrameTime now);

    ControlSettings settings_;
    AutomationPresenter* presenter_{};
    ClientIdentity client_{};
    ControlState state_{ControlState::user_control};
    FrameTime asked_at_{};
    FrameTime last_call_{};
    std::chrono::milliseconds grace_{};
    bool last_take_allowed_{};
    std::string notice_{};
    bool gate_installed_{};
    // A held press is posted once until the dialog it offers is shown.
    bool offer_posted_{};
    ConsentState consent_{ConsentState::unasked};
    std::string consent_failure_{};
    Counters counters_{};
    // Posted work holds this weakly; reset first in the destructor.
    std::shared_ptr<AutomationControl*> self_{};
};

} // namespace gui_forms::automation
