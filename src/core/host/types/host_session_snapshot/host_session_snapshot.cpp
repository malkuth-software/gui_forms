#include "gui_forms/host/types/host_session_snapshot/host_session_snapshot.hpp"

#include "gui_forms/host/types/host_types.hpp"

#include <sstream>

namespace gui_forms {

std::string HostSessionSnapshot::to_json() const {
    std::ostringstream output;
    output << "{\"capabilities\":" << capabilities.to_json()
           << ",\"phase\":\"" << host_lifecycle_phase_name(phase) << '"'
           << ",\"last_sequence\":" << last_sequence
           << ",\"events_accepted\":" << events_accepted
           << ",\"events_rejected\":" << events_rejected
           << ",\"close_requests\":" << close_requests
           << ",\"close_cancellations\":" << close_cancellations
           << ",\"display_changes\":" << display_changes
           << ",\"monitor_count\":" << monitor_count
           << ",\"modal_transitions\":" << modal_transitions
           << ",\"modal_input_suppressions\":" << modal_input_suppressions
           << ",\"drag_events\":" << drag_events
           << ",\"drag_drops\":" << drag_drops
           << ",\"modal_depth\":" << modal_depth
           << ",\"attached\":" << (attached ? "true" : "false")
           << ",\"active\":" << (active ? "true" : "false")
           << ",\"occluded\":" << (occluded ? "true" : "false")
           << ",\"closed\":" << (closed ? "true" : "false")
           << ",\"shutdown\":" << (shutdown ? "true" : "false") << '}';
    return output.str();
}

} // namespace gui_forms
