#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/events.hpp"
#include "gui_forms/host/types/host_capabilities/host_capabilities.hpp"

#include <cstdint>
#include <string>

namespace gui_forms {

struct GUI_FORMS_API HostServicesSnapshot final {
    HostCapabilities capabilities;
    CursorKind cursor{CursorKind::arrow};
    bool pointer_captured{};
    std::uint64_t captured_pointer_id{};
    std::uint64_t monitor_queries{};
    std::uint64_t cursor_updates{};
    std::uint64_t pointer_capture_updates{};
    std::uint64_t clipboard_reads{};
    std::uint64_t clipboard_writes{};
    std::uint64_t clipboard_generation{};
    std::uint64_t dialog_requests{};
    std::uint64_t dialog_completions{};
    std::uint64_t dialog_cancellations{};
    std::uint64_t sound_requests{};
    std::uint64_t sound_playbacks{};
    std::uint64_t sound_coalesced{};
    std::uint64_t sound_muted{};
    std::uint64_t sound_coalescing_window_nanoseconds{50'000'000U};
    std::uint32_t modal_depth{};
    std::uint32_t maximum_modal_depth{};
    std::uint64_t tray_icons{};
    std::uint64_t tray_icon_updates{};
    std::uint64_t tray_notifications{};
    std::uint64_t tray_menus{};
    std::uint64_t tray_events{};
    std::uint64_t rejected_requests{};
    bool shutdown{};

    [[nodiscard]] std::string to_json() const;
};

} // namespace gui_forms
