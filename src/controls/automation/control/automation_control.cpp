#include "automation_control.hpp"

#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include "../../../core/host/session/session_directory.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <memory>

namespace gui_forms::automation {

AutomationPresenter::~AutomationPresenter() = default;

std::string client_label(const ClientIdentity& client) {
    if (!client.model_name.empty() && !client.client_name.empty()) {
        std::string label = client.model_name + " via " + client.client_name;
        return label;
    }
    if (!client.model_name.empty()) return client.model_name;
    if (!client.client_name.empty()) return client.client_name;
    return std::string("a model");
}

const char* control_state_name(ControlState state) noexcept {
    switch (state) {
    case ControlState::user_control: return "user_control";
    case ControlState::asking: return "asking";
    case ControlState::model_control: return "model_control";
    }
    // A state outside the enumeration reads as the fallback.
    return "user_control";
}

bool test_controls_built() noexcept {
#if defined(GUI_FORMS_MCP_TEST_CONTROLS)
    return true;
#else
    return false;
#endif
}

namespace {

// The gate's held press, run at the UI thread's next turn so the dialog
// never opens inside the input's own dispatch.
// Holds the control weakly: a control destroyed before the turn is not
// called.
struct OfferTakeBackWork final {
    std::weak_ptr<AutomationControl*> control{};
    void operator()() const {
        const std::shared_ptr<AutomationControl*> live = control.lock();
        if (live) (**live).offer_take_back();
    }
};

} // namespace

AutomationControl::AutomationControl(ControlSettings settings, AutomationPresenter* presenter)
    : settings_(settings), presenter_(presenter), grace_(settings.grace),
      self_(gui_forms::detail::make_library_shared<AutomationControl*>(this)) {}

AutomationControl::~AutomationControl() {
    self_.reset();
    disconnect();
}

void AutomationControl::set_client(const ClientIdentity& client) {
    const bool same = client.client_name == client_.client_name && client.client_version == client_.client_version &&
                      client.model_name == client_.model_name;
    if (same) return;
    client_ = client;
    if (presenter_ != nullptr) (*presenter_).client_changed(client_);
}

bool AutomationControl::acts_allowed() const noexcept {
    const bool allowed = settings_.appearance_review || state_ == ControlState::model_control;
    return allowed;
}

void AutomationControl::request_take(FrameTime now) {
    if (state_ != ControlState::user_control || settings_.appearance_review) return;
    ++counters_.takes_asked;
    if (session_consent_ || settings_.first_come) {
        // The session was allowed once, or the launch is first come, first
        // served: no question.
        last_take_allowed_ = true;
        ++counters_.takes_answered;
        enter_model_control(now);
        return;
    }
    if (presenter_ == nullptr) {
        // Nobody can be asked: refused at once.
        last_take_allowed_ = false;
        ++counters_.takes_answered;
        return;
    }
    state_ = ControlState::asking;
    asked_at_ = now;
    last_call_ = now;
    try {
        (*presenter_).ask_take(client_, settings_.take_countdown);
    } catch (...) {
        // A question that cannot be shown is refused; the failure goes on.
        state_ = ControlState::user_control;
        last_take_allowed_ = false;
        ++counters_.takes_answered;
        throw;
    }
}

void AutomationControl::answer_take(bool allow) {
    // Only the question asked is answered.
    if (state_ != ControlState::asking) return;
    last_take_allowed_ = allow;
    ++counters_.takes_answered;
    if (!allow) {
        state_ = ControlState::user_control;
        return;
    }
    session_consent_ = true;
    enter_model_control(last_call_);
}

void AutomationControl::begin_ai_led(FrameTime now) {
    if (!settings_.ai_led || settings_.appearance_review || state_ == ControlState::model_control) return;
    enter_model_control(now);
}

void AutomationControl::set_grace(std::chrono::milliseconds grace) noexcept {
    if (grace.count() == 0) {
        grace_ = settings_.grace;
        return;
    }
    grace_ = std::clamp(grace, std::chrono::milliseconds{1000}, std::chrono::milliseconds{600000});
}

void AutomationControl::release_by_model() {
    // Released on purpose: the next act asks the user again.
    session_consent_ = false;
    if (state_ == ControlState::model_control) release(ControlRelease::model_ended);
}

void AutomationControl::user_took_back() {
    if (state_ != ControlState::model_control) return;
    // Counted before the release, so a running call's next step sees it.
    ++counters_.holds_taken_back;
    session_consent_ = false;
    notice_ = settings_.first_come
                  ? "the user took back control of the program: your work was interrupted; your next act takes it "
                    "again"
                  : "the user took back control of the program: your work was interrupted; acts now ask the user "
                    "again";
    // The program's questions are the user's again from here.
    std::vector<gui_forms::detail::ModelQuestion> dropped{};
    gui_forms::detail::end_model_questions(0U, dropped);
    release(ControlRelease::user_took_back);
}

void AutomationControl::disconnect() noexcept {
    if (state_ == ControlState::model_control) release(ControlRelease::disconnected);
    // A take question still asked is refused by the disconnect.
    if (state_ == ControlState::asking) state_ = ControlState::user_control;
    if (gate_installed_) {
        gui_forms::detail::remove_user_input_gate(this);
        gate_installed_ = false;
    }
    // A question still asked is answered Don't Allow by the disconnect: no
    // model is left to act on it.
    if (consent_ == ConsentState::asking) consent_ = ConsentState::declined;
}

void AutomationControl::begin_session(FrameTime now) {
    disconnect();
    client_ = ClientIdentity{};
    notice_.clear();
    consent_ = ConsentState::unasked;
    consent_failure_.clear();
    session_consent_ = false;
    offer_posted_ = false;
    begin_ai_led(now);
}

std::string AutomationControl::take_notice() {
    std::string notice = std::move(notice_);
    notice_.clear();
    return notice;
}

void AutomationControl::note_model_call(FrameTime now) noexcept {
    last_call_ = now;
}

void AutomationControl::tick(FrameTime now) {
    switch (state_) {
    case ControlState::user_control:
        return;
    case ControlState::asking:
        // The countdown ends as Allow (the presenter's own countdown answers
        // first; this covers a presenter whose timer did not run).
        if (now - asked_at_ >= settings_.take_countdown + std::chrono::milliseconds{1000}) answer_take(true);
        return;
    case ControlState::model_control:
        if (settings_.ai_led || settings_.first_come) return;
        if (now - last_call_ >= grace_) {
            ++counters_.holds_idle_returned;
            notice_ = "control returned to the user after " + std::to_string(grace_.count() / 1000) +
                      (session_consent_ ? " s without a call; your next act takes it again (the user allowed "
                                          "this session)"
                                        : " s without a call; the next act asks the user again");
            release(ControlRelease::idle);
        }
        return;
    }
    release(ControlRelease::disconnected);
}

void AutomationControl::request_consent() {
    if (consent_ != ConsentState::unasked) return;
    consent_ = ConsentState::asking;
    consent_failure_.clear();
    ++counters_.consent_questions;
    if (presenter_ == nullptr) {
        // Nobody can be asked (a script without a window): Don't Allow.
        consent_ = ConsentState::declined;
        return;
    }
    try {
        (*presenter_).ask_consent(client_);
    } catch (...) {
        consent_ = ConsentState::unasked;
        throw;
    }
}

void AutomationControl::consent_not_shown(std::string why) {
    if (consent_ != ConsentState::asking) return;
    consent_ = ConsentState::unasked;
    consent_failure_ = why.empty() ? std::string("the question could not be shown") : std::move(why);
}

void AutomationControl::answer_consent(bool allow) {
    // Only the question asked is answered; the answer then holds for the
    // launch.
    if (consent_ != ConsentState::asking) return;
    consent_ = allow ? ConsentState::allowed : ConsentState::declined;
}

bool AutomationControl::holds_user_input(const SurfaceRoot& window) const noexcept {
    if (state_ != ControlState::model_control) return false;
    if (presenter_ != nullptr && (*presenter_).presenter_window(window)) return false;
    return true;
}

void AutomationControl::offer_take_back() {
    offer_posted_ = false;
    if (state_ != ControlState::model_control || presenter_ == nullptr) return;
    ++counters_.take_back_offers;
    (*presenter_).offer_take_back(client_);
}

bool AutomationControl::gate(void* context, const SurfaceRoot& window, bool press) noexcept {
    AutomationControl& control = *static_cast<AutomationControl*>(context);
    const bool held = control.holds_user_input(window);
    if (held && press && !control.offer_posted_) {
        // Every application window is a Window (attach_application_window).
        const Window& owner = static_cast<const Window&>(window);
        try {
            const DispatchOperation posted = owner.dispatcher().begin_invoke(OfferTakeBackWork{control.self_});
            control.offer_posted_ = posted.state() == DispatchOperationState::pending;
        } catch (...) {
            // A dispatcher that cannot post drops the press; the next one
            // offers again.
            control.offer_posted_ = false;
        }
    }
    return held;
}

bool AutomationControl::native_gate(void* context, const bool press) noexcept {
    AutomationControl& control = *static_cast<AutomationControl*>(context);
    const bool held = control.state_ == ControlState::model_control;
    if (held && press) {
        try {
            control.offer_take_back();
        } catch (...) {
            // A take-back dialog that cannot open drops the press; the next
            // one offers again.
        }
    }
    return held;
}

void AutomationControl::enter_model_control(FrameTime now) {
    state_ = ControlState::model_control;
    last_call_ = now;
    ++counters_.holds_begun;
    if (!gate_installed_) {
        gui_forms::detail::install_user_input_gate(&AutomationControl::gate, this);
        gui_forms::detail::install_native_input_gate(&AutomationControl::native_gate, this);
        gate_installed_ = true;
    }
    if (presenter_ != nullptr) {
        // AI mode and its take-back dialog are the user's way back: a
        // presenter that cannot show them gives control back before the
        // failure goes on.
        try {
            (*presenter_).control_changed(state_, client_);
        } catch (...) {
            release(ControlRelease::presenter_failed);
            throw;
        }
    }
}

void AutomationControl::release(ControlRelease reason) noexcept {
    static_cast<void>(reason);
    state_ = ControlState::user_control;
    grace_ = settings_.grace;
    if (gate_installed_) {
        gui_forms::detail::remove_user_input_gate(this);
        gate_installed_ = false;
    }
    if (presenter_ != nullptr) {
        // AI mode ends; a presenter failure must not keep input held, which
        // the state above has already given back.
        try {
            (*presenter_).control_changed(state_, client_);
        } catch (...) {
        }
    }
}

} // namespace gui_forms::automation
