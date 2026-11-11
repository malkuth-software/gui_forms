#include "dispatcher_state.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {
namespace detail {

namespace {

// The calling thread's revocable invokes, if it has any (a sub-surface's
// thread). Each thread reads and writes only its own.
thread_local std::shared_ptr<RevocableInvokes> thread_revocable_invokes{};

// An operation that is already cancelled and holds no callback: what a
// refused post returns.
DispatchOperation refused_operation(const std::shared_ptr<DispatcherState>& state) {
    std::shared_ptr<DispatchWork> refused = std::make_shared<DispatchWork>();
    (*refused).state.store(DispatchOperationState::cancelled, std::memory_order_release);
    (*refused).dispatcher = state;
    return DispatchOperation(std::move(refused));
}

} // namespace

void revoke_invokes(RevocableInvokes& invokes) noexcept {
    std::shared_ptr<DispatchWork> waiting;
    {
        std::scoped_lock lock(invokes.mutex);
        invokes.revoked = true;
        waiting = invokes.waiting.lock();
        invokes.waiting.reset();
    }
    if (!waiting) return;
    // A work that already ran (or was cancelled) keeps its state; a pending
    // one is cancelled, which wakes its waiter. The owning dispatcher's turn
    // skips it, and its callback goes with the work.
    DispatchOperationState expected = DispatchOperationState::pending;
    if ((*waiting).state.compare_exchange_strong(expected, DispatchOperationState::cancelled,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
        (*waiting).state.notify_all();
    }
}

void set_thread_revocable_invokes(std::shared_ptr<RevocableInvokes> invokes) noexcept {
    thread_revocable_invokes = std::move(invokes);
}

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

PostOutcome post_dispatch_outcome(const std::shared_ptr<DispatcherState>& state,
                                  Control::WeakPtr owner,
                                  bool requires_owner,
                                  std::function<void()> callback,
                                  bool synchronous,
                                  bool refuse_when_closed,
                                  DispatchOperation& operation) {
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
            ++(*state).cancelled;
            ++(*state).teardown_refusals;
            operation = refused_operation(state);
            return PostOutcome::refused;
        }
        if (!(*state).accepting) {
            return PostOutcome::closed;
        }
        if (synchronous && !(*state).wake) {
            throw std::logic_error(
                "GUI.Forms worker Invoke requires a running host dispatcher");
        }
        if ((*state).queue.size() >= maximum_posted_callbacks) {
            return PostOutcome::queue_full;
        }
        work = std::make_shared<DispatchWork>();
        if (synchronous && thread_revocable_invokes) {
            // Under the leaf lock, so a revocation either sees this work as
            // waiting or this post sees the revocation.
            std::scoped_lock revocable_lock((*thread_revocable_invokes).mutex);
            if ((*thread_revocable_invokes).revoked) {
                ++(*state).cancelled;
                operation = refused_operation(state);
                return PostOutcome::refused;
            }
            (*thread_revocable_invokes).waiting = work;
        }
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
    operation = DispatchOperation(std::move(work));
    return PostOutcome::posted;
}

DispatchOperation post_dispatch(const std::shared_ptr<DispatcherState>& state,
                                Control::WeakPtr owner,
                                bool requires_owner,
                                std::function<void()> callback,
                                bool synchronous,
                                bool refuse_when_closed) {
    DispatchOperation operation;
    const PostOutcome outcome = post_dispatch_outcome(state, std::move(owner), requires_owner, std::move(callback),
                                                      synchronous, refuse_when_closed, operation);
    if (outcome == PostOutcome::closed) {
        throw std::logic_error("GUI.Forms dispatcher is shut down");
    }
    if (outcome == PostOutcome::queue_full) {
        throw std::length_error("GUI.Forms posted callback limit reached");
    }
    return operation;
}

} // namespace detail
} // namespace gui_forms
