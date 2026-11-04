#include "../../dispatcher/state/dispatcher_state.hpp"

#include "gui_forms/surface_root/surface_root.hpp"
// Complete Window: a control's back link is still its Window (stage S1
// moves it to the surface root with the control table).
#include "gui_forms/window.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {
void record_inline_invoke(
    const std::shared_ptr<detail::DispatcherState>& state) {
    std::scoped_lock lock((*state).mutex);
    if (!(*state).accepting) {
        throw std::logic_error("GUI.Forms dispatcher is shut down");
    }
    ++(*state).synchronous_invocations;
    ++(*state).inline_invocations;
}

} // namespace

DispatchOperation SurfaceRoot::begin_invoke(std::function<void()> callback) {
    return detail::post_dispatch(dispatcher_state_, {}, false,
                                 std::move(callback));
}

DispatchOperation SurfaceRoot::begin_invoke(const Control::Ptr& owner,
                                       std::function<void()> callback) {
    if (!owner) {
        throw std::invalid_argument(
            "GUI.Forms owned BeginInvoke requires a control");
    }
    return detail::post_dispatch(dispatcher_state_, owner, true,
                                 std::move(callback));
}

void SurfaceRoot::invoke(std::function<void()> callback) {
    if (!callback) {
        throw std::invalid_argument("GUI.Forms Invoke requires a callback");
    }
    if (check_access()) {
        record_inline_invoke(dispatcher_state_);
        callback();
        return;
    }
    DispatchOperation operation = detail::post_dispatch(
        dispatcher_state_, {}, false, std::move(callback), true);
    operation.wait_and_rethrow();
}

void SurfaceRoot::invoke(const Control::Ptr& owner,
                    std::function<void()> callback) {
    if (!owner) {
        throw std::invalid_argument("GUI.Forms owned Invoke requires a control");
    }
    if (!callback) {
        throw std::invalid_argument("GUI.Forms Invoke requires a callback");
    }
    if (check_access()) {
        if (!(*owner).is_alive() || (*owner).window_ != this) {
            throw std::logic_error(
                "GUI.Forms owned Invoke requires an attached live control");
        }
        record_inline_invoke(dispatcher_state_);
        callback();
        return;
    }
    DispatchOperation operation = detail::post_dispatch(
        dispatcher_state_, owner, true, std::move(callback), true);
    operation.wait_and_rethrow();
}

DispatchDrainResult SurfaceRoot::drain_posted_work(std::size_t maximum_callbacks) {
    require_root_thread("posted callback drain");
    if (maximum_callbacks == 0U ||
        maximum_callbacks > maximum_callbacks_per_dispatch_turn) {
        throw std::invalid_argument(
            "GUI.Forms dispatch turn callback bound is invalid");
    }

    std::vector<std::shared_ptr<detail::DispatchWork>> turn;
    {
        std::scoped_lock lock((*dispatcher_state_).mutex);
        (*dispatcher_state_).wake_pending = false;
        const std::size_t count = std::min(
            maximum_callbacks, (*dispatcher_state_).queue.size());
        turn.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            turn.push_back(std::move((*dispatcher_state_).queue.front()));
            (*dispatcher_state_).queue.pop_front();
        }
    }

    DispatchDrainResult result;
    for (const std::shared_ptr<gui_forms::detail::DispatchWork>& work : turn) {
        DispatchOperationState expected = DispatchOperationState::pending;
        if (!(*work).state.compare_exchange_strong(
                expected, DispatchOperationState::running,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            if (expected == DispatchOperationState::cancelled) {
                ++result.cancelled;
                (*work).callback = {};
            }
            continue;
        }

        const Control::Ptr owner = (*work).owner.lock();
        if ((*work).requires_owner &&
            (!owner || !(*owner).is_alive() || (*owner).window_ != this)) {
            (*work).callback = {};
            (*work).state.store(DispatchOperationState::cancelled,
                              std::memory_order_release);
            (*work).state.notify_all();
            ++result.cancelled;
            continue;
        }

        try {
            // Moving a std::function need not empty its source (libc++
            // copies a callable kept in its small buffer), so the work item
            // is emptied explicitly: a completed operation must not keep the
            // callable, or what it owns, alive.
            std::function<void()> callback = std::move((*work).callback);
            (*work).callback = {};
            callback();
            (*work).state.store(DispatchOperationState::completed,
                              std::memory_order_release);
            (*work).state.notify_all();
            ++result.invoked;
        } catch (...) {
            {
                std::scoped_lock lock((*work).fault_mutex);
                (*work).fault = std::current_exception();
            }
            (*work).callback = {};
            (*work).state.store(DispatchOperationState::faulted,
                              std::memory_order_release);
            (*work).state.notify_all();
            ++result.faulted;
        }
        metrics_.record_callback_emitted();
    }

    {
        std::scoped_lock lock((*dispatcher_state_).mutex);
        (*dispatcher_state_).invoked += result.invoked;
        (*dispatcher_state_).cancelled += result.cancelled;
        (*dispatcher_state_).faulted += result.faulted;

        // Explicitly cancelled work that was beyond this turn's snapshot does
        // not consume future dispatch capacity.
        for (detail::DispatcherState::Queue::iterator current =
                 (*dispatcher_state_).queue.begin();
             current != (*dispatcher_state_).queue.end();) {
            if ((*(*current)).state.load(std::memory_order_acquire) ==
                DispatchOperationState::cancelled) {
                (*(*current)).callback = {};
                current = (*dispatcher_state_).queue.erase(current);
                ++(*dispatcher_state_).cancelled;
                ++result.cancelled;
            } else {
                ++current;
            }
        }

        result.remaining = (*dispatcher_state_).queue.size();
        if ((*dispatcher_state_).accepting && result.remaining != 0U &&
            (*dispatcher_state_).wake && !(*dispatcher_state_).wake_pending) {
            (*dispatcher_state_).wake_pending = true;
            ++(*dispatcher_state_).wake_requests;
            // Under the lock, as in post_dispatch.
            (*dispatcher_state_).wake();
        }
        result.wake_requested = (*dispatcher_state_).wake_pending;
    }
    return result;
}

DispatcherSnapshot SurfaceRoot::dispatcher_snapshot() const noexcept {
    if (!dispatcher_state_) return {};
    std::scoped_lock lock((*dispatcher_state_).mutex);
    return {
        (*dispatcher_state_).posted,
        (*dispatcher_state_).invoked,
        (*dispatcher_state_).cancelled,
        (*dispatcher_state_).faulted,
        (*dispatcher_state_).wake_requests,
        (*dispatcher_state_).coalesced_wakes,
        (*dispatcher_state_).synchronous_invocations,
        (*dispatcher_state_).inline_invocations,
        (*dispatcher_state_).marshalled_invocations,
        (*dispatcher_state_).queue.size(),
        (*dispatcher_state_).maximum_pending,
        (*dispatcher_state_).teardown_refusals,
        (*dispatcher_state_).accepting,
        (*dispatcher_state_).wake_pending,
    };
}

void SurfaceRoot::set_dispatch_wake_handler(std::function<void()> wake_handler) {
    require_root_thread("dispatcher wake-handler mutation");
    {
        std::scoped_lock lock((*dispatcher_state_).mutex);
        if (!wake_handler) {
            bool synchronous_waiter{};
            for (const std::shared_ptr<detail::DispatchWork>& work :
                 (*dispatcher_state_).queue) {
                if ((*work).synchronous &&
                    (*work).state.load(std::memory_order_acquire) ==
                        DispatchOperationState::pending) {
                    synchronous_waiter = true;
                    break;
                }
            }
            if (synchronous_waiter) {
                throw std::logic_error(
                    "GUI.Forms cannot remove a host wake handler while a "
                    "synchronous Invoke is pending");
            }
        }
        (*dispatcher_state_).wake = std::move(wake_handler);
        (*dispatcher_state_).wake_pending = false;
        if ((*dispatcher_state_).accepting && !(*dispatcher_state_).queue.empty() &&
            (*dispatcher_state_).wake) {
            (*dispatcher_state_).wake_pending = true;
            ++(*dispatcher_state_).wake_requests;
            // Under the lock, as in post_dispatch.
            (*dispatcher_state_).wake();
        }
    }
}

void SurfaceRoot::begin_dispatcher_teardown() noexcept {
    if (!dispatcher_state_) return;
    std::scoped_lock lock((*dispatcher_state_).mutex);
    (*dispatcher_state_).closing = true;
    // No host is woken from here on: the host may be going too. Work posted
    // during the teardown is queued and then cancelled by the shutdown that
    // ends it.
    (*dispatcher_state_).wake = {};
    (*dispatcher_state_).wake_pending = false;
}

void SurfaceRoot::close_dispatcher() noexcept {
    if (!dispatcher_state_) return;
    std::deque<std::shared_ptr<detail::DispatchWork>> pending;
    {
        std::scoped_lock lock((*dispatcher_state_).mutex);
        if (!(*dispatcher_state_).accepting) return;
        (*dispatcher_state_).accepting = false;
        (*dispatcher_state_).wake_pending = false;
        (*dispatcher_state_).wake = {};
        pending.swap((*dispatcher_state_).queue);
    }
    std::uint64_t cancelled = 0U;
    for (const std::shared_ptr<gui_forms::detail::DispatchWork>& work : pending) {
        DispatchOperationState expected = DispatchOperationState::pending;
        if ((*work).state.compare_exchange_strong(
                expected, DispatchOperationState::cancelled,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            (*work).callback = {};
            (*work).state.notify_all();
            ++cancelled;
        }
    }
    if (cancelled != 0U) {
        std::scoped_lock lock((*dispatcher_state_).mutex);
        (*dispatcher_state_).cancelled += cancelled;
    }
}
} // namespace gui_forms
