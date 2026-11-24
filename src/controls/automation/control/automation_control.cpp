#include "automation_control.hpp"

#include "../../../core/host/session/session_directory.hpp"

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
    case ControlState::model_directed: return "model_directed";
    case ControlState::input_suspended: return "input_suspended";
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

AutomationControl::AutomationControl(ControlSettings settings, AutomationPresenter* presenter) noexcept
    : settings_(settings), presenter_(presenter) {}

AutomationControl::~AutomationControl() {
    disconnect();
}

void AutomationControl::set_client(const ClientIdentity& client) {
    const bool same = client.client_name == client_.client_name && client.client_version == client_.client_version &&
                      client.model_name == client_.model_name;
    if (same) return;
    client_ = client;
    if (presenter_ != nullptr) (*presenter_).client_changed(client_);
}

ControlRefusal AutomationControl::begin_directed(ChainId chain, FrameTime now) {
    if (!settings_.model_directed_allowed) return ControlRefusal::not_launched_directed;
    switch (state_) {
    case ControlState::user_control:
        enter(ControlState::model_directed, chain, now);
        return ControlRefusal::none;
    case ControlState::model_directed:
        // Held already: the newer chain takes it over, so its end gives
        // input back.
        holder_ = chain;
        last_call_ = now;
        return ControlRefusal::none;
    case ControlState::input_suspended:
        return ControlRefusal::held_otherwise;
    }
    // An unknown state: the fallback, and the request refused.
    release(ControlRelease::disconnected);
    return ControlRefusal::held_otherwise;
}

ControlRefusal AutomationControl::begin_suspend(ChainId chain, FrameTime now) {
    if (!test_controls_built()) return ControlRefusal::not_in_this_build;
    switch (state_) {
    case ControlState::user_control:
        enter(ControlState::input_suspended, chain, now);
        return ControlRefusal::none;
    case ControlState::input_suspended:
        holder_ = chain;
        last_call_ = now;
        return ControlRefusal::none;
    case ControlState::model_directed:
        return ControlRefusal::held_otherwise;
    }
    release(ControlRelease::disconnected);
    return ControlRefusal::held_otherwise;
}

void AutomationControl::end_hold() {
    if (state_ != ControlState::user_control) release(ControlRelease::model_ended);
}

void AutomationControl::chain_finished(ChainId chain) {
    if (state_ != ControlState::user_control && holder_.value != 0U && holder_ == chain) {
        release(ControlRelease::chain_ended);
    }
    if (interrupted_ == chain) interrupted_ = ChainId{};
    if (timed_out_ == chain) timed_out_ = ChainId{};
}

void AutomationControl::user_took_back() {
    if (state_ == ControlState::user_control) return;
    // Recorded before the release, so the chain's next step sees it.
    if (holder_.value != 0U) interrupted_ = holder_;
    ++counters_.holds_taken_back;
    release(ControlRelease::user_took_back);
}

void AutomationControl::disconnect() noexcept {
    if (state_ != ControlState::user_control) release(ControlRelease::disconnected);
    if (gate_installed_) {
        gui_forms::detail::install_user_input_gate(nullptr, nullptr);
        gate_installed_ = false;
    }
    // A question still asked is answered Don't Allow by the disconnect: no
    // model is left to act on it.
    if (consent_ == ConsentState::asking) consent_ = ConsentState::declined;
}

bool AutomationControl::chain_interrupted(ChainId chain) const noexcept {
    const bool interrupted = chain.value != 0U && interrupted_ == chain;
    return interrupted;
}

bool AutomationControl::chain_timed_out(ChainId chain) const noexcept {
    const bool timed_out = chain.value != 0U && timed_out_ == chain;
    return timed_out;
}

void AutomationControl::note_model_call(FrameTime now) noexcept {
    last_call_ = now;
}

void AutomationControl::tick(FrameTime now) {
    switch (state_) {
    case ControlState::user_control:
        return;
    case ControlState::input_suspended:
        if (now - held_since_ >= settings_.suspend_ceiling) {
            if (holder_.value != 0U) timed_out_ = holder_;
            ++counters_.holds_ceilinged;
            release(ControlRelease::ceiling);
            return;
        }
        break;
    case ControlState::model_directed:
        break;
    default:
        release(ControlRelease::disconnected);
        return;
    }
    // A hold no chain owns ends once the model has gone quiet.
    if (holder_.value == 0U && now - last_call_ >= settings_.idle_ceiling) {
        ++counters_.holds_ceilinged;
        release(ControlRelease::ceiling);
    }
}

void AutomationControl::request_consent() {
    if (consent_ != ConsentState::unasked) return;
    consent_ = ConsentState::asking;
    ++counters_.consent_questions;
    if (presenter_ == nullptr) {
        // Nobody can be asked (a script without a window): Don't Allow.
        consent_ = ConsentState::declined;
        return;
    }
    (*presenter_).ask_consent(client_);
}

void AutomationControl::answer_consent(bool allow) {
    // Only the question asked is answered; the answer then holds for the
    // launch.
    if (consent_ != ConsentState::asking) return;
    consent_ = allow ? ConsentState::allowed : ConsentState::declined;
}

bool AutomationControl::holds_user_input(const SurfaceRoot& window) const noexcept {
    if (state_ == ControlState::user_control) return false;
    if (presenter_ != nullptr && (*presenter_).presenter_window(window)) return false;
    return true;
}

bool AutomationControl::gate(void* context, const SurfaceRoot& window) noexcept {
    const AutomationControl& control = *static_cast<const AutomationControl*>(context);
    const bool held = control.holds_user_input(window);
    return held;
}

void AutomationControl::enter(ControlState state, ChainId chain, FrameTime now) {
    state_ = state;
    holder_ = chain;
    held_since_ = now;
    last_call_ = now;
    ++counters_.holds_begun;
    if (!gate_installed_) {
        gui_forms::detail::install_user_input_gate(&AutomationControl::gate, this);
        gate_installed_ = true;
    }
    if (presenter_ != nullptr && state == ControlState::model_directed) {
        // The take-back popup is the user's way out of the hold: a presenter
        // that cannot show it gives the input back before the failure goes
        // on (release's own call is guarded the same way), so the hold never
        // stands without its popup.
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
    const ControlState previous = state_;
    state_ = ControlState::user_control;
    holder_ = ChainId{};
    if (gate_installed_) {
        gui_forms::detail::install_user_input_gate(nullptr, nullptr);
        gate_installed_ = false;
    }
    if (presenter_ != nullptr && previous == ControlState::model_directed) {
        // The popup closes; a presenter failure must not keep input held,
        // which the state above has already given back.
        try {
            (*presenter_).control_changed(state_, client_);
        } catch (...) {
        }
    }
}

} // namespace gui_forms::automation
