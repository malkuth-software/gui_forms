#include "../state/dispatcher_state.hpp"

#include "gui_forms/dispatcher/window_dispatcher/window_dispatcher.hpp"
#include "gui_forms/window.hpp"

#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gui_forms {

namespace {

// Whether the window still accepts work: its dispatcher has not been shut
// down and its teardown has not begun.
bool accepting_work(detail::DispatcherState& state) {
    std::scoped_lock lock(state.mutex);
    const bool accepting = state.accepting && !state.closing;
    return accepting;
}

// Counts one inline Invoke, as Window::invoke does, when the window still
// accepts work; returns false, counting nothing, when it does not.
bool record_inline_invoke(detail::DispatcherState& state) {
    std::scoped_lock lock(state.mutex);
    if (!state.accepting || state.closing) return false;
    ++state.synchronous_invocations;
    ++state.inline_invocations;
    return true;
}

} // namespace

WindowDispatcher::WindowDispatcher(std::shared_ptr<detail::DispatcherState> state) noexcept
    : state_(std::move(state)) {}

DispatchOperation WindowDispatcher::begin_invoke(std::function<void()> callback) const {
    if (!state_) throw std::logic_error("GUI.Forms WindowDispatcher is bound to no window");
    DispatchOperation operation =
        detail::post_dispatch(state_, {}, false, std::move(callback), false, true);
    return operation;
}

DispatchInvokeResult WindowDispatcher::invoke(std::function<void()> callback) const {
    if (!state_) throw std::logic_error("GUI.Forms WindowDispatcher is bound to no window");
    if (!callback) throw std::invalid_argument("GUI.Forms Invoke requires a callback");
    // On the UI thread the window cannot close during the call except from
    // inside the callback, so a window accepting work at the check is alive
    // when the callback starts.
    if (std::this_thread::get_id() == (*state_).ui_thread) {
        if (!record_inline_invoke(*state_)) return DispatchInvokeResult::window_gone;
        callback();
        return DispatchInvokeResult::completed;
    }
    const DispatchOperation operation =
        detail::post_dispatch(state_, {}, false, std::move(callback), true, true);
    const DispatchOperationState final_state = operation.wait_terminal();
    if (final_state == DispatchOperationState::completed) return DispatchInvokeResult::completed;
    if (final_state == DispatchOperationState::faulted) {
        const std::exception_ptr fault = operation.exception();
        if (fault) std::rethrow_exception(fault);
        throw std::runtime_error("GUI.Forms synchronous Invoke faulted without an exception");
    }
    return DispatchInvokeResult::window_gone;
}

bool WindowDispatcher::window_alive() const noexcept {
    if (!state_) return false;
    const bool alive = accepting_work(*state_);
    return alive;
}

WindowDispatcher Window::dispatcher() const {
    WindowDispatcher handle(dispatcher_state_);
    return handle;
}

} // namespace gui_forms
