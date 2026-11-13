#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>

namespace gui_forms {

inline constexpr std::size_t maximum_posted_callbacks = 4096U;
inline constexpr std::size_t maximum_callbacks_per_dispatch_turn = 1024U;

// What WindowDispatcher::invoke did: the callback ran, or the window was
// gone (closed, or being destroyed) before it could, so it did not run.
enum class DispatchInvokeResult : std::uint8_t {
    completed,
    window_gone,
};

enum class DispatchOperationState : std::uint8_t {
    invalid,
    pending,
    running,
    completed,
    cancelled,
    faulted,
};

struct GUI_FORMS_API DispatchDrainResult final {
    std::uint64_t invoked{};
    std::uint64_t cancelled{};
    std::uint64_t faulted{};
    std::size_t remaining{};
    bool wake_requested{};
};

struct GUI_FORMS_API DispatcherSnapshot final {
    std::uint64_t posted{};
    std::uint64_t invoked{};
    std::uint64_t cancelled{};
    std::uint64_t faulted{};
    std::uint64_t wake_requests{};
    std::uint64_t coalesced_wakes{};
    std::uint64_t synchronous_invocations{};
    std::uint64_t inline_invocations{};
    std::uint64_t marshalled_invocations{};
    std::size_t pending{};
    std::size_t maximum_pending{};
    // Posts refused, already cancelled, because they reached a shut-down
    // dispatcher while its window was being destroyed (counted in
    // `cancelled` too).
    std::uint64_t teardown_refusals{};
    bool accepting{};
    bool wake_pending{};
};

} // namespace gui_forms
