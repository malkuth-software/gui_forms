#pragma once

#include "gui_forms/host/types/host_capabilities/host_capabilities.hpp"
#include "gui_forms/host/types/host_lifecycle_phase/host_lifecycle_phase.hpp"

#include <cstdint>
#include <string>

namespace gui_forms {

struct HostSessionSnapshot final {
    HostCapabilities capabilities;
    HostLifecyclePhase phase{HostLifecyclePhase::constructed};
    std::uint64_t last_sequence{};
    std::uint64_t events_accepted{};
    std::uint64_t events_rejected{};
    std::uint64_t close_requests{};
    std::uint64_t close_cancellations{};
    std::uint64_t display_changes{};
    std::uint64_t monitor_count{};
    std::uint64_t modal_transitions{};
    std::uint64_t modal_input_suppressions{};
    std::uint64_t drag_events{};
    std::uint64_t drag_drops{};
    std::uint32_t modal_depth{};
    bool attached{};
    bool active{};
    bool occluded{};
    bool closed{};
    bool shutdown{};

    [[nodiscard]] std::string to_json() const;
};

} // namespace gui_forms
