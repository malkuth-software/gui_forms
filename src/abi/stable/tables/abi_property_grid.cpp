// GF_TABLE_PROPERTY_GRID: a property grid's selection and editing
// operations. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"

#include "gui_forms/controls/panel/property_grid/property_grid.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity select_objects_slot{GF_TABLE_PROPERTY_GRID, 0U};
constexpr SlotIdentity refresh_slot{GF_TABLE_PROPERTY_GRID, 1U};
constexpr SlotIdentity try_set_text_slot{GF_TABLE_PROPERTY_GRID, 2U};
constexpr SlotIdentity reset_property_slot{GF_TABLE_PROPERTY_GRID, 3U};
constexpr SlotIdentity activate_editor_slot{GF_TABLE_PROPERTY_GRID, 4U};

constexpr const char* not_property_grid = "the control is not a property grid";
// More selected objects than a grid shows usefully; the bound keeps the
// staging vector's size checked.
constexpr std::uint64_t maximum_selected_objects = 4096U;

// The grid and a non-empty property name.
gf_status enter_named(gf_object grid, gf_utf8_view name, SlotIdentity slot, std::shared_ptr<PropertyGrid>& typed,
                      std::string_view& property) {
    const gf_status entered = enter_typed_control(grid, slot, not_property_grid, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status read = read_text(name, slot, property);
    if (read != GF_STATUS_OK) {
        return read;
    }
    if (property.empty()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a property name is not empty");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL property_grid_select_objects(gf_object grid, const gf_object* objects,
                                               std::uint64_t count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(select_objects_slot.table_id, select_objects_slot.slot_index);
        if ((objects == nullptr && count != 0U) || count > maximum_selected_objects) {
            return fail(GF_STATUS_INVALID_ARGUMENT, select_objects_slot,
                        "select_objects takes at most 4096 objects, and objects when count is not zero");
        }
        std::shared_ptr<PropertyGrid> typed;
        const gf_status entered = enter_typed_control(grid, select_objects_slot, not_property_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        // Every identity is checked before the grid changes.
        std::vector<Control::Ptr> selected;
        selected.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t index = 0U; index < count; ++index) {
            Control::Ptr control;
            const gf_status found = registry().find_object(objects[index], select_objects_slot, control);
            if (found != GF_STATUS_OK) {
                return found;
            }
            selected.push_back(std::move(control));
        }
        (*typed).set_selected_objects(std::move(selected));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(select_objects_slot);
    }
}

gf_status GF_CALL property_grid_refresh(gf_object grid) noexcept {
    try {
        const ShadowFrameScope shadow_frame(refresh_slot.table_id, refresh_slot.slot_index);
        std::shared_ptr<PropertyGrid> typed;
        const gf_status entered = enter_typed_control(grid, refresh_slot, not_property_grid, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*typed).refresh_properties();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(refresh_slot);
    }
}

gf_status GF_CALL property_grid_try_set_text(gf_object grid, gf_utf8_view name, gf_utf8_view text,
                                             std::uint32_t* accepted) noexcept {
    try {
        const ShadowFrameScope shadow_frame(try_set_text_slot.table_id, try_set_text_slot.slot_index);
        if (accepted == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, try_set_text_slot, "try_set_text needs an accepted output");
        }
        std::shared_ptr<PropertyGrid> typed;
        std::string_view property;
        const gf_status entered = enter_named(grid, name, try_set_text_slot, typed, property);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::string_view value;
        const gf_status read = read_text(text, try_set_text_slot, value);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const bool set = (*typed).try_set_property_text(property, value);
        *accepted = set ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(try_set_text_slot);
    }
}

gf_status GF_CALL property_grid_reset_property(gf_object grid, gf_utf8_view name, std::uint32_t* reset) noexcept {
    try {
        const ShadowFrameScope shadow_frame(reset_property_slot.table_id, reset_property_slot.slot_index);
        if (reset == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, reset_property_slot, "reset_property needs a reset output");
        }
        std::shared_ptr<PropertyGrid> typed;
        std::string_view property;
        const gf_status entered = enter_named(grid, name, reset_property_slot, typed, property);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const bool done = (*typed).reset_property(property);
        *reset = done ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(reset_property_slot);
    }
}

gf_status GF_CALL property_grid_activate_editor(gf_object grid, gf_utf8_view name,
                                                std::uint32_t* activated) noexcept {
    try {
        const ShadowFrameScope shadow_frame(activate_editor_slot.table_id, activate_editor_slot.slot_index);
        if (activated == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, activate_editor_slot,
                        "activate_editor needs an activated output");
        }
        std::shared_ptr<PropertyGrid> typed;
        std::string_view property;
        const gf_status entered = enter_named(grid, name, activate_editor_slot, typed, property);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const bool done = (*typed).activate_property_editor(property);
        *activated = done ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(activate_editor_slot);
    }
}

constexpr gf_property_grid_table table{
    .struct_size = sizeof(gf_property_grid_table),
    .abi_minor = GF_ABI_MINOR,
    .select_objects = &property_grid_select_objects,
    .refresh = &property_grid_refresh,
    .try_set_text = &property_grid_try_set_text,
    .reset_property = &property_grid_reset_property,
    .activate_editor = &property_grid_activate_editor,
};

} // namespace

const gf_property_grid_table& property_grid_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
