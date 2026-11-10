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
    // Written once, by the thread that ran the callback, before it stores
    // `state` faulted with release order; read only after a reader observes
    // faulted with acquire order (DispatchOperation::exception). The state
    // publishes it, so it needs no lock of its own.
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

// The blocking invokes of one thread that its owner may revoke: a
// sub-surface's thread, which its parent joins on the parent's UI thread
// (SubSurfaceThread::stop). While the parent is in that join it cannot run a
// synchronous invoke the sub-surface made onto it, so the stop revokes them:
// the invoke waiting now is cancelled, and every later one is refused as
// cancelled, never queued. `mutex` guards the other fields and is a leaf
// lock: post_dispatch takes it under a dispatcher's, and nothing is taken
// under it.
struct RevocableInvokes final {
    std::mutex mutex;
    bool revoked{};
    // The synchronous invoke this thread queued last; expired or terminal
    // once it ran.
    std::weak_ptr<DispatchWork> waiting{};
};

// Revokes `invokes` (see RevocableInvokes). Any thread; idempotent.
void revoke_invokes(RevocableInvokes& invokes) noexcept;

// Names the calling thread's revocable invokes (null: none, the default).
// Set by the thread itself, at its start, and cleared before it ends.
void set_thread_revocable_invokes(std::shared_ptr<RevocableInvokes> invokes) noexcept;

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

// What a post did (post_dispatch_outcome). Only `posted` and `refused`
// fill the caller's operation.
enum class PostOutcome : std::uint8_t {
    posted,
    // Refused by name: the window's teardown began, or the calling thread's
    // blocking invokes were revoked. The operation is already cancelled.
    refused,
    // The dispatcher shut down; nothing was posted.
    closed,
    // The queue holds maximum_posted_callbacks; nothing was posted.
    queue_full,
};

// Posts like post_dispatch, but returns the expected outcomes (a full queue,
// a closed dispatcher, a refusal) instead of throwing them, for a caller that
// must tell them apart (the background worker's thread). `operation`
// receives the posted or refused work. A missing callback or dispatcher, a
// synchronous post without a running host, and a failed host wake still
// throw, as in post_dispatch, and leave nothing posted.
[[nodiscard]] PostOutcome post_dispatch_outcome(
    const std::shared_ptr<DispatcherState>& state,
    Control::WeakPtr owner,
    bool requires_owner,
    std::function<void()> callback,
    bool synchronous,
    bool refuse_when_closed,
    DispatchOperation& operation);

// post_dispatch_outcome for callers that treat a closed dispatcher and a full
// queue as errors: they throw std::logic_error and std::length_error.
[[nodiscard]] DispatchOperation post_dispatch(
    const std::shared_ptr<DispatcherState>& state,
    Control::WeakPtr owner,
    bool requires_owner,
    std::function<void()> callback,
    bool synchronous = false,
    bool refuse_when_closed = false);

} // namespace gui_forms::detail
