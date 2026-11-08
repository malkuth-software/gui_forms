#pragma once

#include "gui_forms/events/key_commands/key_commands.hpp"
#include "gui_forms/host/types/host_capability/host_capability.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gui_forms {

// One host protocol version and the feature it added.
struct HostProtocolRevision final {
    std::uint32_t version{};
    const char* feature{};
};

// The host protocol's versions, one row each, from version 8 (earlier
// versions are not recorded). The current version is the last row's. A
// change to the protocol adds a row at the end, so two changes that claim
// the same version both add the same line and conflict when merged, and
// the checks below refuse a skipped or repeated version.
inline constexpr std::array<HostProtocolRevision, 4> host_protocol_revisions{
    {{8U, "the host's keyboard-command defaults (HostCapabilities::key_map)"},
    {9U, "the compositor state report (HostCompositorEvent, stage T5)"},
    {10U, "the platform's input settings (HostInputSettingsEvent: the double-click interval)"},
    {11U, "the platform's mouse conventions: context requests (HostContextRequestEvent), native hover "
          "(HostHoverEvent) and the drag and hover settings (HostInputSettingsEvent::pointer)"},
}};

// Whether the rows run consecutively from 8, each with a feature.
constexpr bool host_protocol_revisions_consecutive() noexcept {
    for (std::size_t index = 0U; index < host_protocol_revisions.size(); ++index) {
        const HostProtocolRevision& row = host_protocol_revisions[index];
        if (row.version != 8U + static_cast<std::uint32_t>(index)) return false;
        if (row.feature == nullptr || row.feature[0] == '\0') return false;
    }
    return true;
}

static_assert(host_protocol_revisions_consecutive(),
              "host protocol versions are added one row at a time, in order, each with its feature");

struct HostCapabilities final {
    // The last row of host_protocol_revisions.
    static constexpr std::uint32_t current_protocol_version =
        host_protocol_revisions[host_protocol_revisions.size() - 1U].version;

    std::uint32_t protocol_version{current_protocol_version};
    std::string platform{"unknown"};
    HostCapability available{HostCapability::none};
    // The keys this host's convention gives the named keyboard commands
    // (owner decision DK-30, interview G5-1): host data, not core policy.
    // The first HostSession of an application seeds the application's
    // KeyBindings with it (owner decision DK-44); the application's
    // rebindings stay over it.
    KeyMap key_map{pc_key_map()};

    [[nodiscard]] bool supports(HostCapability capability) const noexcept {
        return has_capability(available, capability);
    }
    [[nodiscard]] std::string to_json() const;
};

} // namespace gui_forms
