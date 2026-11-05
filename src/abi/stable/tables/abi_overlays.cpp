// GF_TABLE_OVERLAYS: overlay roots and tooltips. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"

#include "gui_forms/components/tool_tip/tool_tip.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity open_overlay_slot{GF_TABLE_OVERLAYS, 0U};
constexpr SlotIdentity close_overlay_slot{GF_TABLE_OVERLAYS, 1U};
constexpr SlotIdentity set_tool_tip_slot{GF_TABLE_OVERLAYS, 2U};
constexpr SlotIdentity show_tool_tip_slot{GF_TABLE_OVERLAYS, 3U};
constexpr SlotIdentity hide_tool_tip_slot{GF_TABLE_OVERLAYS, 4U};

// The open overlays, by the overlay's identity. Owned here and used on the UI
// thread only. A token the window has closed (its owner left the window,
// the window closed) is disconnected and is replaced or dropped on the next
// open or close of that overlay; the rest close when the process ends.
std::unordered_map<gf_object, OverlayToken>& open_overlays() {
    static std::unordered_map<gf_object, OverlayToken> overlays;
    return overlays;
}

gf_status enter_tool_tip(gf_object object, SlotIdentity slot, std::shared_ptr<ToolTip>& tool_tip) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr component;
    const gf_status found = registry().find_component(object, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    std::shared_ptr<ToolTip> cast = std::dynamic_pointer_cast<ToolTip>(component);
    if (!cast) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the object is not a tool_tip component");
    }
    tool_tip = std::move(cast);
    return GF_STATUS_OK;
}

gf_status GF_CALL overlays_open_overlay(gf_object owner, gf_object overlay) noexcept {
    try {
        const ShadowFrameScope shadow_frame(open_overlay_slot.table_id, open_overlay_slot.slot_index);
        Control::Ptr owner_control;
        const gf_status entered = enter_live_control(owner, open_overlay_slot, owner_control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control::Ptr overlay_control;
        const gf_status found = registry().find_object(overlay, open_overlay_slot, overlay_control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        std::unordered_map<gf_object, OverlayToken>& overlays = open_overlays();
        const std::unordered_map<gf_object, OverlayToken>::iterator existing = overlays.find(overlay);
        if (existing != overlays.end() && (*existing).second.connected()) {
            return fail(GF_STATUS_INVALID_STATE, open_overlay_slot, "the overlay is already open");
        }
        Window* const window = (*owner_control).attached_window();
        if (window == nullptr) {
            return fail(GF_STATUS_INVALID_STATE, open_overlay_slot, "the owner is not in a window");
        }
        // Window::open_overlay refuses an overlay with a parent and an owner that
        // cannot own one (std::logic_error: GF_STATUS_INVALID_STATE).
        OverlayToken token = (*window).open_overlay(owner_control, overlay_control);
        overlays.insert_or_assign(overlay, std::move(token));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(open_overlay_slot);
    }
}

gf_status GF_CALL overlays_close_overlay(gf_object overlay) noexcept {
    try {
        const ShadowFrameScope shadow_frame(close_overlay_slot.table_id, close_overlay_slot.slot_index);
        const gf_status entered = registry().enter_ui(close_overlay_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::unordered_map<gf_object, OverlayToken>& overlays = open_overlays();
        const std::unordered_map<gf_object, OverlayToken>::iterator existing = overlays.find(overlay);
        if (existing == overlays.end()) {
            return fail(GF_STATUS_NOT_FOUND, close_overlay_slot, "the overlay is not open");
        }
        const bool was_open = (*existing).second.connected();
        // Erasing the token disconnects it, closing the overlay.
        overlays.erase(existing);
        if (!was_open) {
            return fail(GF_STATUS_NOT_FOUND, close_overlay_slot, "the overlay was already closed by its window");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(close_overlay_slot);
    }
}

gf_status GF_CALL overlays_set_tool_tip(gf_object tool_tip, gf_object target, gf_utf8_view text) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_tool_tip_slot.table_id, set_tool_tip_slot.slot_index);
        std::shared_ptr<ToolTip> provider;
        const gf_status entered = enter_tool_tip(tool_tip, set_tool_tip_slot, provider);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control::Ptr target_control;
        const gf_status found = registry().find_object(target, set_tool_tip_slot, target_control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        std::string_view read;
        const gf_status checked = read_text(text, set_tool_tip_slot, read);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        (*provider).set_tool_tip(target_control, std::string(read));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_tool_tip_slot);
    }
}

gf_status GF_CALL overlays_show_tool_tip(gf_object tool_tip, gf_object target, std::uint64_t duration_ms) noexcept {
    try {
        const ShadowFrameScope shadow_frame(show_tool_tip_slot.table_id, show_tool_tip_slot.slot_index);
        std::shared_ptr<ToolTip> provider;
        const gf_status entered = enter_tool_tip(tool_tip, show_tool_tip_slot, provider);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control::Ptr target_control;
        const gf_status found = registry().find_object(target, show_tool_tip_slot, target_control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        if (duration_ms == 0U) {
            (*provider).show(target_control);
            return GF_STATUS_OK;
        }
        const std::chrono::milliseconds duration{static_cast<std::chrono::milliseconds::rep>(duration_ms)};
        (*provider).show(target_control, duration);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(show_tool_tip_slot);
    }
}

gf_status GF_CALL overlays_hide_tool_tip(gf_object tool_tip) noexcept {
    try {
        const ShadowFrameScope shadow_frame(hide_tool_tip_slot.table_id, hide_tool_tip_slot.slot_index);
        std::shared_ptr<ToolTip> provider;
        const gf_status entered = enter_tool_tip(tool_tip, hide_tool_tip_slot, provider);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*provider).hide();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(hide_tool_tip_slot);
    }
}

constexpr gf_overlays_table table{
    .struct_size = sizeof(gf_overlays_table),
    .abi_minor = GF_ABI_MINOR,
    .open_overlay = &overlays_open_overlay,
    .close_overlay = &overlays_close_overlay,
    .set_tool_tip = &overlays_set_tool_tip,
    .show_tool_tip = &overlays_show_tool_tip,
    .hide_tool_tip = &overlays_hide_tool_tip,
};

} // namespace

const gf_overlays_table& overlays_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
