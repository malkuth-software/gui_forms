#pragma once

#include "gui_forms/export/application_export.hpp"
#include "../services/headless_host_services.hpp"
#include "gui_forms/window.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace gui_forms::host {

// The headless application's double-click interval: a fixed test value,
// reported as a host value (HostInputSettingsEvent) after each window's
// attach. It differs from default_double_click_milliseconds, so a test can
// tell a host report from none.
inline constexpr std::uint32_t headless_double_click_milliseconds = 500U;

// Deterministic reference adapter for host-event conformance tests. It owns no
// clock and performs no background work; the caller supplies every timestamp.
class GUI_FORMS_API HeadlessHost final {
public:
    explicit HeadlessHost(Window& window);
    ~HeadlessHost();
    HeadlessHost(const HeadlessHost&) = delete;
    HeadlessHost& operator=(const HeadlessHost&) = delete;

    [[nodiscard]] HostDispatchResult dispatch(
        HostEventPayload payload, std::uint64_t timestamp_nanoseconds);
    [[nodiscard]] DispatchDrainResult pump_dispatcher(
        std::size_t maximum_callbacks = maximum_callbacks_per_dispatch_turn);
    [[nodiscard]] bool dispatcher_wake_pending() const noexcept {
        return dispatcher_wake_pending_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool paint_wake_pending() const noexcept {
        return paint_wake_pending_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool consume_paint_wake() noexcept {
        return paint_wake_pending_.exchange(false, std::memory_order_acq_rel);
    }
    [[nodiscard]] HostSession& session() noexcept { return session_; }
    [[nodiscard]] const HostSession& session() const noexcept { return session_; }
    [[nodiscard]] HostServices& services() noexcept { return services_; }
    [[nodiscard]] const HostServices& services() const noexcept { return services_; }
    // The reference services themselves, for tests that drive or read their
    // simulated native state (the backdrop, stage T5).
    [[nodiscard]] HeadlessHostServices& headless_services() noexcept { return services_; }
    [[nodiscard]] const std::string& trace() const& noexcept { return trace_; }
    const std::string& trace() const&& = delete;

private:
    void request_dispatcher_wake() noexcept;
    void request_paint_wake() noexcept;
    void observe_dispatch(const HostEvent& event,
                          const HostDispatchResult& result);

    Window* window_{};
    std::atomic<bool> dispatcher_wake_pending_{};
    std::atomic<bool> paint_wake_pending_{};
    HeadlessHostServices services_;
    HostSession session_;
    SubscriptionToken observation_;
    std::uint64_t next_sequence_{1};
    std::string trace_;
};

} // namespace gui_forms::host
