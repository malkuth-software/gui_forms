#include "headless_capabilities.hpp"

#include <sstream>

namespace gui_forms::host {

HostCapabilities headless_capabilities() {
    HostCapabilities capabilities{HostCapabilities::current_protocol_version,
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
                HostCapability::window_corners |
                // Tests dispatch HostWindowStateEvent themselves.
                HostCapability::window_state_reports
#if !defined(_WIN32)
                // Hosted content is real where it exists (POSIX).
                | HostCapability::hosted_content
#endif
                ,
            // The reference host reports the PC convention.
            pc_key_map()};
    // The simulated platform may report either corner shape.
    capabilities.window_corner_shapes = WindowCornerShapes{true, true};
    return capabilities;
}
} // namespace gui_forms::host
