#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host/types/host_capabilities/host_capabilities.hpp"
#include "gui_forms/host/types/host_lifecycle_phase/host_lifecycle_phase.hpp"

#include <cstdint>
#include <string>

namespace gui_forms {

struct GUI_FORMS_API HostSessionSnapshot final {
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
    // HostRelativeMotionEvent samples delivered to the window.
    std::uint64_t relative_motion_events{};
    // GestureEvent reports delivered to the window.
    std::uint64_t gesture_events{};
    std::uint64_t drag_events{};
    std::uint64_t drag_drops{};
    // A model's input delivered through HostSession::dispatch_model_input,
    // and the user's input events held back while a model held the
    // interface (model.directed or suspend_input; MCP automation study,
    // sections 8.2 and 8.3).
    std::uint64_t model_input_events{};
    std::uint64_t user_input_held{};
    std::uint32_t modal_depth{};
    bool attached{};
    bool active{};
    bool occluded{};
    bool closed{};
    bool shutdown{};

    [[nodiscard]] std::string to_json() const;
};

} // namespace gui_forms
