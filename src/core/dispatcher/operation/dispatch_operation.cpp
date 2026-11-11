#include "../state/dispatcher_state.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

bool dispatch_terminal(DispatchOperationState state) noexcept {
    return state == DispatchOperationState::completed ||
           state == DispatchOperationState::cancelled ||
           state == DispatchOperationState::faulted;
}
} // namespace

std::uint64_t DispatchOperation::sequence() const noexcept {
    return work_ ? (*work_).sequence : 0U;
}

DispatchOperationState DispatchOperation::state() const noexcept {
    return work_ ? (*work_).state.load(std::memory_order_acquire)
                 : DispatchOperationState::invalid;
}

bool DispatchOperation::pending() const noexcept {
    return state() == DispatchOperationState::pending;
}

bool DispatchOperation::cancel() noexcept {
    if (!work_) return false;
    DispatchOperationState expected = DispatchOperationState::pending;
    const bool cancelled = (*work_).state.compare_exchange_strong(
        expected, DispatchOperationState::cancelled,
        std::memory_order_acq_rel, std::memory_order_acquire);
    if (cancelled) (*work_).state.notify_all();
    return cancelled;
}

std::exception_ptr DispatchOperation::exception() const noexcept {
    if (!work_) return {};
    // The faulted state publishes the fault (DispatchWork::fault).
    const DispatchOperationState observed = (*work_).state.load(std::memory_order_acquire);
    if (observed != DispatchOperationState::faulted) return {};
    return (*work_).fault;
}

DispatchOperationState DispatchOperation::wait_terminal() const {
    if (!work_) {
        throw std::logic_error(
            "GUI.Forms cannot wait for an invalid dispatch operation");
    }
    DispatchOperationState observed = (*work_).state.load(std::memory_order_acquire);
    while (!dispatch_terminal(observed)) {
        // Wait on the state itself: completion can occur between the check
        // and sleeping. An unrelated condition variable could lose that wake
        // because publishers did not hold the waiter's completion mutex.
        (*work_).state.wait(observed, std::memory_order_acquire);
        observed = (*work_).state.load(std::memory_order_acquire);
    }
    return observed;
}

void DispatchOperation::wait_and_rethrow() const {
    const DispatchOperationState final_state = wait_terminal();
    if (final_state == DispatchOperationState::completed) return;
    if (final_state == DispatchOperationState::faulted) {
        const std::exception_ptr fault = exception();
        if (fault) std::rethrow_exception(fault);
        throw std::runtime_error(
            "GUI.Forms synchronous Invoke faulted without an exception");
    }
    throw DispatchCancelledError();
}

} // namespace gui_forms
