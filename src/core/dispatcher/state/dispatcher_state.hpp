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

[[nodiscard]] DispatchOperation post_dispatch(
    const std::shared_ptr<DispatcherState>& state,
    Control::WeakPtr owner,
    bool requires_owner,
    std::function<void()> callback,
    bool synchronous = false,
    bool refuse_when_closed = false);

} // namespace gui_forms::detail
