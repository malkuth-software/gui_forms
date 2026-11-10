#include "dispatcher_state.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {
namespace detail {

void wake_host_locked(DispatcherState& state) {
    state.wake_pending = true;
    ++state.wake_requests;
    try {
        state.wake();
    } catch (...) {
        state.wake_pending = false;
        --state.wake_requests;
        throw;
    }
}

DispatchOperation post_dispatch(const std::shared_ptr<DispatcherState>& state,
                                Control::WeakPtr owner,
                                bool requires_owner,
                                std::function<void()> callback,
                                bool synchronous,
                                bool refuse_when_closed) {
    if (!callback) {
        throw std::invalid_argument("GUI.Forms BeginInvoke requires a callback");
    }
    if (!state) {
        throw std::logic_error(
            "GUI.Forms BeginInvoke requires an attached live dispatcher");
    }

    std::shared_ptr<DispatchWork> work;
    {
        std::scoped_lock lock((*state).mutex);
        // A closed window: its dispatcher shut down, or its teardown began
        // (which removes the host wake, so marshalled work could never run).
        // WindowDispatcher asks for the refusal in both cases; Window's own
        // posts are refused only once a teardown has shut the dispatcher
        // down, and throw below otherwise.
        const bool closed = !(*state).accepting || (*state).closing;
        if ((refuse_when_closed && closed) || (!(*state).accepting && (*state).closing)) {
            // The teardown refusal: an operation that is already cancelled
            // and holds no callback. The callback is released with this
            // call's argument.
            std::shared_ptr<DispatchWork> refused = std::make_shared<DispatchWork>();
            (*refused).state.store(DispatchOperationState::cancelled, std::memory_order_release);
            (*refused).dispatcher = state;
            ++(*state).cancelled;
            ++(*state).teardown_refusals;
            return DispatchOperation(std::move(refused));
        }
        if (!(*state).accepting) {
            throw std::logic_error("GUI.Forms dispatcher is shut down");
        }
        if (synchronous && !(*state).wake) {
            throw std::logic_error(
                "GUI.Forms worker Invoke requires a running host dispatcher");
        }
        if ((*state).queue.size() >= maximum_posted_callbacks) {
            throw std::length_error("GUI.Forms posted callback limit reached");
        }
        work = std::make_shared<DispatchWork>();
        (*work).sequence = (*state).next_sequence++;
        if ((*state).next_sequence == 0U) (*state).next_sequence = 1U;
        (*work).callback = std::move(callback);
        (*work).owner = std::move(owner);
        (*work).requires_owner = requires_owner;
        (*work).synchronous = synchronous;
        (*work).dispatcher = state;
        (*state).queue.push_back(work);
        ++(*state).posted;
        if (synchronous) {
            ++(*state).synchronous_invocations;
            ++(*state).marshalled_invocations;
        }
        (*state).maximum_pending = std::max((*state).maximum_pending,
                                          (*state).queue.size());
        if ((*state).wake) {
            if (!(*state).wake_pending) {
                try {
                    wake_host_locked(*state);
                } catch (...) {
                    // The post fails as a whole: its work leaves the queue
                    // (the last entry, under this lock) and the counters
                    // return to what they were, so the caller's exception
                    // means nothing was posted. The callback is released
                    // with `work` as the exception leaves.
                    (*state).queue.pop_back();
                    --(*state).posted;
                    if (synchronous) {
                        --(*state).synchronous_invocations;
                        --(*state).marshalled_invocations;
                    }
                    throw;
                }
            } else {
                ++(*state).coalesced_wakes;
            }
        }
    }
    return DispatchOperation(std::move(work));
}

} // namespace detail
} // namespace gui_forms
