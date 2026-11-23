#include "../../dispatcher/state/dispatcher_state.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <stdexcept>
#include <stxlib/threads.hpp>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {
void record_inline_invoke(
    const std::shared_ptr<detail::DispatcherState>& state) {
    const stxlib::ScopedLock lock((*state).mutex);
    if (!(*state).accepting) {
        throw std::logic_error("GUI.Forms dispatcher is shut down");
    }
    ++(*state).synchronous_invocations;
    ++(*state).inline_invocations;
}

} // namespace

bool Control::invoke_required() const noexcept {
    std::shared_ptr<detail::DispatcherState> state;
    {
        const stxlib::ScopedLock lock(dispatcher_mutex_);
        state = dispatcher_state_;
    }
    return state && stxlib::current_thread() != (*state).ui_thread;
}

DispatchOperation Control::begin_invoke(std::function<void()> callback) {
    std::shared_ptr<detail::DispatcherState> state;
    {
        const stxlib::ScopedLock lock(dispatcher_mutex_);
        state = dispatcher_state_;
    }
    return detail::post_dispatch(state, weak_from_this(), true,
                                 std::move(callback));
}

void Control::invoke(std::function<void()> callback) {
    if (!callback) {
        throw std::invalid_argument("GUI.Forms Invoke requires a callback");
    }
    std::shared_ptr<detail::DispatcherState> state;
    {
        const stxlib::ScopedLock lock(dispatcher_mutex_);
        state = dispatcher_state_;
    }
    if (!state) {
        throw std::logic_error(
            "GUI.Forms Invoke requires an attached live dispatcher");
    }
    if (stxlib::current_thread() == (*state).ui_thread) {
        if (!is_alive()) {
            throw std::logic_error(
                "GUI.Forms Invoke requires an attached live control");
        }
        record_inline_invoke(state);
        callback();
        return;
    }
    DispatchOperation operation = detail::post_dispatch(
        state, weak_from_this(), true, std::move(callback), true);
    operation.wait_and_rethrow();
}

} // namespace gui_forms
