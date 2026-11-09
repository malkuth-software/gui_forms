#include "gui_forms/host/types/host_capabilities/host_capabilities.hpp"

#include <array>
#include <sstream>
#include <utility>

namespace gui_forms {
namespace {

std::string json_escape(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += character; break;
        }
    }
    return result;
}

// One chord as {"key":<HID usage>,"modifiers":<Modifier bits>}; nothing for
// an empty chord. `first` says whether no chord precedes it in the list.
void append_chord(std::ostringstream& output, KeyChord chord, bool first) {
    if (chord.empty()) return;
    if (!first) output << ',';
    output << "{\"key\":" << chord.physical_key
           << ",\"modifiers\":" << static_cast<unsigned>(chord.modifiers) << '}';
}

} // namespace

std::string HostCapabilities::to_json() const {
    constexpr std::array<std::pair<HostCapability, const char*>, 36> names{{
        {HostCapability::lifecycle, "lifecycle"},
        {HostCapability::scale_notifications, "scale_notifications"},
        {HostCapability::monitor_geometry, "monitor_geometry"},
        {HostCapability::occlusion, "occlusion"},
        {HostCapability::scheduled_wake, "scheduled_wake"},
        {HostCapability::pointer_input, "pointer_input"},
        {HostCapability::keyboard_input, "keyboard_input"},
        {HostCapability::text_composition, "text_composition"},
        {HostCapability::pointer_capture, "pointer_capture"},
        {HostCapability::cursor, "cursor"},
        {HostCapability::clipboard, "clipboard"},
        {HostCapability::clipboard_images, "clipboard_images"},
        {HostCapability::typed_drag_destination, "typed_drag_destination"},
        {HostCapability::dialogs, "dialogs"},
        {HostCapability::menus, "menus"},
        {HostCapability::font_discovery, "font_discovery"},
        {HostCapability::accessibility, "accessibility"},
        {HostCapability::typed_drag_source, "typed_drag_source"},
        {HostCapability::sound_cues, "sound_cues"},
        {HostCapability::frame_capture, "frame_capture"},
        {HostCapability::notification_area, "notification_area"},
        {HostCapability::notification_area_messages, "notification_area_messages"},
        {HostCapability::notification_area_menus, "notification_area_menus"},
        {HostCapability::runtime_windows, "runtime_windows"},
        {HostCapability::window_frames, "window_frames"},
        {HostCapability::screen_pointer, "screen_pointer"},
        {HostCapability::window_at_point, "window_at_point"},
        {HostCapability::window_ordering, "window_ordering"},
        {HostCapability::transparent_windows, "transparent_windows"},
        {HostCapability::window_backdrops, "window_backdrops"},
        {HostCapability::compositor_notifications, "compositor_notifications"},
        {HostCapability::presentation_preferences, "presentation_preferences"},
        {HostCapability::transient_surfaces, "transient_surfaces"},
        {HostCapability::opaque_transient_surfaces, "opaque_transient_surfaces"},
        {HostCapability::window_corners, "window_corners"},
        {HostCapability::window_state_reports, "window_state_reports"},
    }};
    std::ostringstream output;
    output << "{\"protocol_version\":" << protocol_version
           << ",\"platform\":\"" << json_escape(platform)
           << "\",\"available_bits\":"
           << static_cast<std::uint64_t>(available) << ",\"capabilities\":[";
    bool first = true;
    for (const std::pair<HostCapability, const char*>& named_capability : names) {
        const HostCapability capability = named_capability.first;
        const char* const name = named_capability.second;
        if (!supports(capability)) continue;
        output << (first ? "\"" : ",\"") << name << '"';
        first = false;
    }
    output << "],\"key_map\":{";
    for (std::size_t index = 0; index < key_command_count; ++index) {
        const KeyCommand command = static_cast<KeyCommand>(index);
        const KeyBinding& binding = key_map.binding(command);
        output << (index == 0U ? "\"" : ",\"") << key_command_name(command) << "\":[";
        append_chord(output, binding.primary, true);
        append_chord(output, binding.secondary, binding.primary.empty());
        output << ']';
    }
    output << "}}";
    return output.str();
}

} // namespace gui_forms
