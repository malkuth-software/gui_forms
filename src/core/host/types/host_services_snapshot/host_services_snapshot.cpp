#include "gui_forms/host/types/host_services_snapshot/host_services_snapshot.hpp"

#include "../../../json/json_text/json_text.hpp"

#include "gui_forms/host/types/host_types.hpp"

#include <sstream>

namespace gui_forms {

std::string HostServicesSnapshot::to_json() const {
    std::ostringstream output;
    // Numbers in the classic locale whatever the global one (the pattern of
    // A-gf-core-11-F1).
    detail::prepare_json_stream(output);
    output << "{\"capabilities\":" << capabilities.to_json()
           << ",\"cursor\":\"" << cursor_kind_name(cursor) << '"'
           << ",\"pointer_captured\":" << (pointer_captured ? "true" : "false")
           << ",\"captured_pointer_id\":" << captured_pointer_id
           << ",\"monitor_queries\":" << monitor_queries
           << ",\"cursor_updates\":" << cursor_updates
           << ",\"pointer_capture_updates\":" << pointer_capture_updates
           << ",\"clipboard_reads\":" << clipboard_reads
           << ",\"clipboard_writes\":" << clipboard_writes
           << ",\"clipboard_generation\":" << clipboard_generation
           << ",\"dialog_requests\":" << dialog_requests
           << ",\"dialog_completions\":" << dialog_completions
           << ",\"dialog_cancellations\":" << dialog_cancellations
           << ",\"sound_requests\":" << sound_requests
           << ",\"sound_playbacks\":" << sound_playbacks
           << ",\"sound_coalesced\":" << sound_coalesced
           << ",\"sound_muted\":" << sound_muted
           << ",\"sound_coalescing_window_nanoseconds\":"
           << sound_coalescing_window_nanoseconds
           << ",\"modal_depth\":" << modal_depth
           << ",\"maximum_modal_depth\":" << maximum_modal_depth
           << ",\"tray_icons\":" << tray_icons
           << ",\"tray_icon_updates\":" << tray_icon_updates
           << ",\"tray_notifications\":" << tray_notifications
           << ",\"tray_menus\":" << tray_menus
           << ",\"tray_events\":" << tray_events
           << ",\"rejected_requests\":" << rejected_requests
           << ",\"shutdown\":" << (shutdown ? "true" : "false") << '}';
    return output.str();
}

} // namespace gui_forms
