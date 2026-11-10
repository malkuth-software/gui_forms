#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/dispatcher.hpp"

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace gui_forms::detail {

struct DispatcherState;

struct DispatchWork final {
    std::uint64_t sequence{};
    std::atomic<DispatchOperationState> state{DispatchOperationState::pending};
    std::function<void()> callback;
    Control::WeakPtr owner;
    bool requires_owner{};
    bool synchronous{};
    std::weak_ptr<DispatcherState> dispatcher;
    mutable std::mutex fault_mutex;
    std::exception_ptr fault;
};

struct DispatcherState final {
    using Queue = std::deque<std::shared_ptr<DispatchWork>>;

    explicit DispatcherState(std::thread::id thread) : ui_thread(thread) {}

    mutable std::mutex mutex;
    Queue queue;
    std::function<void()> wake;
    std::thread::id ui_thread;
    std::uint64_t next_sequence{1U};
    std::uint64_t posted{};
    std::uint64_t invoked{};
    std::uint64_t cancelled{};
    std::uint64_t faulted{};
    std::uint64_t wake_requests{};
    std::uint64_t coalesced_wakes{};
    std::uint64_t synchronous_invocations{};
    std::uint64_t inline_invocations{};
    std::uint64_t marshalled_invocations{};
    std::size_t maximum_pending{};
    // Work refused while the window is being destroyed (see `closing`).
    std::uint64_t teardown_refusals{};
    bool accepting{true};
    bool wake_pending{};
    // Set when the window's destructor starts. A post that finds the
    // dispatcher shut down while this holds is refused by name (the
    // operation is returned already cancelled) instead of throwing: work
    // posted from a teardown notification could never run, so it meets the
    // fate it would have met a moment earlier, when shutdown cancelled
    // everything pending. Outside teardown, posting after shutdown throws.
    bool closing{};
};

// Wakes the host: sets wake_pending, counts the request and calls `wake`.
// Call with `mutex` held, `wake` set and wake_pending clear. The host's wake
// runs under the lock, so that a host removing its handler
// (set_dispatch_wake_handler, shutdown_dispatcher) waits for a wake already
// under way and is never called after it returns. Every host's wake only
// signals (a flag, a posted message, a pipe byte, a queued block) and never
// re-enters the dispatcher. A wake that throws (it could not allocate its
// message or block) leaves wake_pending clear and the request uncounted, so
// the next post wakes again, and the exception leaves to the caller.
void wake_host_locked(DispatcherState& state);

[[nodiscard]] DispatchOperation post_dispatch(
    const std::shared_ptr<DispatcherState>& state,
    Control::WeakPtr owner,
    bool requires_owner,
    std::function<void()> callback,
    bool synchronous = false,
    bool refuse_when_closed = false);

} // namespace gui_forms::detail
