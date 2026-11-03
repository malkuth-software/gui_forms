#pragma once

#include "gui_forms/dispatcher/types/dispatcher_types.hpp"

#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace gui_forms {

class Control;
class Window;
namespace detail {
struct DispatchWork;
}

class DispatchCancelledError final : public std::runtime_error {
public:
    DispatchCancelledError()
        : std::runtime_error(
              "GUI.Forms synchronous Invoke was cancelled before execution") {}
};

// A posted operation remains queued when this observation handle is dropped.
// Cancellation is explicit, matching BeginInvoke-style fire-and-forget use
// while still allowing deterministic revocation when a caller needs it.
class DispatchOperation final {
public:
    DispatchOperation() = default;
    // Internal construction seam; DispatchWork is intentionally incomplete to
    // consumers, so only the core dispatcher can produce a meaningful value.
    explicit DispatchOperation(std::shared_ptr<detail::DispatchWork> work)
        : work_(std::move(work)) {}

    [[nodiscard]] std::uint64_t sequence() const noexcept;
    [[nodiscard]] DispatchOperationState state() const noexcept;
    [[nodiscard]] bool pending() const noexcept;
    [[nodiscard]] bool cancel() noexcept;
    [[nodiscard]] std::exception_ptr exception() const noexcept;

private:
    friend class Window;
    friend class Control;
    friend class WindowDispatcher;
    // Waits without pumping until the work is completed, cancelled or
    // faulted, and returns that state.
    [[nodiscard]] DispatchOperationState wait_terminal() const;
    void wait_and_rethrow() const;
    std::shared_ptr<detail::DispatchWork> work_;
};

} // namespace gui_forms
