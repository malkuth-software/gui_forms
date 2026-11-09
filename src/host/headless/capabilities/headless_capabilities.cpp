#include "headless_capabilities.hpp"

#include <sstream>

namespace gui_forms::host {

HostCapabilities headless_capabilities() {
    return {HostCapabilities::current_protocol_version,
            "headless-reference",
            HostCapability::lifecycle |
                HostCapability::scale_notifications |
                HostCapability::occlusion |
                HostCapability::scheduled_wake |
                HostCapability::pointer_input |
                HostCapability::keyboard_input |
                HostCapability::text_composition |
                HostCapability::monitor_geometry |
                HostCapability::pointer_capture |
                HostCapability::cursor |
                HostCapability::clipboard |
                HostCapability::clipboard_images |
                HostCapability::typed_drag_destination |
                HostCapability::dialogs |
                HostCapability::sound_cues |
                HostCapability::notification_area |
                HostCapability::notification_area_messages |
                HostCapability::notification_area_menus |
                HostCapability::runtime_windows |
                HostCapability::window_frames |
                HostCapability::screen_pointer |
                HostCapability::window_at_point |
                HostCapability::window_ordering |
                // The reference host simulates the compositor: tests report
                // its state (HostCompositorEvent) and the services record the
                // backdrop applied.
                HostCapability::transparent_windows |
                HostCapability::window_backdrops |
                HostCapability::compositor_notifications |
                HostCapability::presentation_preferences |
                HostCapability::transient_surfaces |
                // The reference host simulates a platform that shapes
                // corners: tests set what it reports
                // (HeadlessHostServices::set_corner_report).
                HostCapability::window_corners,
            // The reference host reports the PC convention.
            pc_key_map()};
}
} // namespace gui_forms::host
