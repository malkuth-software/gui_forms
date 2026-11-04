#include "gui_forms/anatomy/types/anatomy_types.hpp"

#include <array>

namespace gui_forms {
namespace {

// Indexed by the enumerator's value; the static_asserts keep the tables and
// the enumerations the same length.
constexpr std::array<std::string_view, part_role_count> part_role_names{
    "region",
    "shadow",
    "face",
    "edge",
    "keyline",
    "perimeter",
    "sheen",
    "seam",
    "divider",
    "caption",
    "cap",
    "well",
    "track",
    "fill",
    "thumb",
    "actuator",
    "indicator",
    "activity",
    "glyph",
    "icon",
    "label",
    "content",
    "caret",
    "disclosure",
    "grip",
    "chip",
    "focus",
    "preselect",
    "selection",
    "validation",
    "drop",
    "image",
    "plot",
    "graticule",
    "trace",
};
static_assert(part_role_names.size() == part_role_count);

constexpr std::array<std::string_view, anatomy_state_count> anatomy_state_names{
    "normal",
    "hot",
    "pressed",
    "captured",
    "focused",
    "focus_visible",
    "preselect",
    "selected",
    "checked",
    "indeterminate",
    "current",
    "active_tool",
    "enabled",
    "disabled",
    "unavailable",
    "read_only",
    "invalid",
    "pending",
    "open",
    "closed",
    "expanding",
    "collapsing",
    "interrupted",
    "drop_accepted",
    "drop_rejected",
    "key",
    "secondary_participating",
    "drag_proximate",
    "deactivated",
    "high_contrast",
    "reduced_motion",
};
static_assert(anatomy_state_names.size() == anatomy_state_count);

constexpr std::array<std::string_view, 30> anatomy_defect_names{
    "none",
    "control_id_not_snake_case",
    "no_parts",
    "too_many_parts",
    "part_name_not_snake_case",
    "duplicate_part_name",
    "part_role_out_of_range",
    "composed_from_not_snake_case",
    "state_out_of_range",
    "duplicate_state",
    "too_many_custom_states",
    "custom_state_not_snake_case",
    "custom_state_shadows_standard_state",
    "duplicate_custom_state",
    "too_many_item_parts",
    "item_part_name_not_snake_case",
    "duplicate_item_part_name",
    "item_part_role_out_of_range",
    "item_state_out_of_range",
    "duplicate_item_state",
    "too_many_item_custom_states",
    "item_custom_state_not_snake_case",
    "item_custom_state_shadows_standard_state",
    "duplicate_item_custom_state",
    "too_many_item_systems",
    "item_system_name_not_snake_case",
    "duplicate_item_system_name",
    "item_system_without_parts",
    "part_state_out_of_range",
    "duplicate_part_state",
};
static_assert(anatomy_defect_names.size() ==
              static_cast<std::size_t>(AnatomyDefect::duplicate_part_state) + 1U);

[[nodiscard]] AnatomyValidation defect_at(AnatomyDefect defect,
                                          std::size_t index) noexcept {
    AnatomyValidation result;
    result.defect = defect;
    result.index = index;
    return result;
}

[[nodiscard]] AnatomyValidation item_defect_at(AnatomyDefect defect, std::size_t system,
                                               std::size_t index) noexcept {
    AnatomyValidation result;
    result.defect = defect;
    result.index = index;
    result.item_system = system;
    return result;
}

// The part-state check of validate_control_anatomy for one part: the
// defect, or none.
[[nodiscard]] AnatomyDefect part_states_defect(const PartDeclaration& part) noexcept {
    for (std::size_t index = 0; index < part.states.size(); ++index) {
        const AnatomyState state = part.states[index];
        if (static_cast<std::size_t>(state) >= anatomy_state_count) {
            return AnatomyDefect::part_state_out_of_range;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (part.states[earlier] == state) {
                return AnatomyDefect::duplicate_part_state;
            }
        }
    }
    return AnatomyDefect::none;
}

// True when `name` is one of the control's own parts or a part of an item
// system declared before `system`.
[[nodiscard]] bool part_name_taken(const ControlAnatomy& anatomy, std::size_t system,
                                   std::string_view name) noexcept {
    if (find_part(anatomy, name).has_value()) {
        return true;
    }
    for (std::size_t earlier = 0; earlier < system; ++earlier) {
        if (find_item_part(anatomy.item_systems[earlier], name).has_value()) {
            return true;
        }
    }
    return false;
}

// The checks of validate_control_anatomy for one item system.
[[nodiscard]] AnatomyValidation validate_item_system(const ControlAnatomy& anatomy,
                                                     std::size_t system_index) noexcept {
    const ItemSystemDeclaration& system = anatomy.item_systems[system_index];
    if (!is_registered_name(system.name)) {
        return item_defect_at(AnatomyDefect::item_system_name_not_snake_case, system_index, 0U);
    }
    for (std::size_t earlier = 0; earlier < system_index; ++earlier) {
        if (anatomy.item_systems[earlier].name == system.name) {
            return item_defect_at(AnatomyDefect::duplicate_item_system_name, system_index, 0U);
        }
    }
    if (system.parts.empty()) {
        return item_defect_at(AnatomyDefect::item_system_without_parts, system_index, 0U);
    }
    if (system.parts.size() > maximum_control_parts) {
        return item_defect_at(AnatomyDefect::too_many_item_parts, system_index, 0U);
    }
    for (std::size_t index = 0; index < system.parts.size(); ++index) {
        const PartDeclaration& part = system.parts[index];
        if (!is_registered_name(part.name)) {
            return item_defect_at(AnatomyDefect::item_part_name_not_snake_case, system_index, index);
        }
        if (static_cast<std::size_t>(part.role) >= part_role_count) {
            return item_defect_at(AnatomyDefect::item_part_role_out_of_range, system_index, index);
        }
        if (!part.composed_from.empty() && !is_registered_name(part.composed_from)) {
            return item_defect_at(AnatomyDefect::composed_from_not_snake_case, system_index, index);
        }
        const AnatomyDefect states_defect = part_states_defect(part);
        if (states_defect != AnatomyDefect::none) {
            return item_defect_at(states_defect, system_index, index);
        }
        // An item part may repeat neither another item part nor one of the
        // control's own parts.
        if (part_name_taken(anatomy, system_index, part.name)) {
            return item_defect_at(AnatomyDefect::duplicate_item_part_name, system_index, index);
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (system.parts[earlier].name == part.name) {
                return item_defect_at(AnatomyDefect::duplicate_item_part_name, system_index, index);
            }
        }
    }
    for (std::size_t index = 0; index < system.states.size(); ++index) {
        const AnatomyState state = system.states[index];
        if (static_cast<std::size_t>(state) >= anatomy_state_count) {
            return item_defect_at(AnatomyDefect::item_state_out_of_range, system_index, index);
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (system.states[earlier] == state) {
                return item_defect_at(AnatomyDefect::duplicate_item_state, system_index, index);
            }
        }
    }
    if (system.custom_states.size() > maximum_custom_states) {
        return item_defect_at(AnatomyDefect::too_many_item_custom_states, system_index, 0U);
    }
    for (std::size_t index = 0; index < system.custom_states.size(); ++index) {
        const std::string_view name = system.custom_states[index].name;
        if (!is_registered_name(name)) {
            return item_defect_at(AnatomyDefect::item_custom_state_not_snake_case, system_index, index);
        }
        const std::optional<AnatomyState> standard = anatomy_state_from_name(name);
        if (standard.has_value()) {
            return item_defect_at(AnatomyDefect::item_custom_state_shadows_standard_state,
                                  system_index, index);
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (system.custom_states[earlier].name == name) {
                return item_defect_at(AnatomyDefect::duplicate_item_custom_state, system_index, index);
            }
        }
    }
    AnatomyValidation valid;
    return valid;
}

} // namespace

std::string_view part_role_name(PartRole role) noexcept {
    const std::size_t index = static_cast<std::size_t>(role);
    if (index >= part_role_names.size()) {
        return {};
    }
    return part_role_names[index];
}

std::string_view anatomy_state_name(AnatomyState state) noexcept {
    const std::size_t index = static_cast<std::size_t>(state);
    if (index >= anatomy_state_names.size()) {
        return {};
    }
    return anatomy_state_names[index];
}

std::string_view engine_input_name(EngineInput input) noexcept {
    switch (input) {
    case EngineInput::pointer_x: return "pointer_x";
    case EngineInput::pointer_y: return "pointer_y";
    case EngineInput::time_in_state: return "time_in_state";
    case EngineInput::display_scale: return "display_scale";
    case EngineInput::text_scale: return "text_scale";
    case EngineInput::count: return {};
    }
    return {};
}

std::optional<PartRole> part_role_from_name(std::string_view name) noexcept {
    for (std::size_t index = 0; index < part_role_names.size(); ++index) {
        if (part_role_names[index] == name) {
            return static_cast<PartRole>(index);
        }
    }
    return std::nullopt;
}

std::optional<AnatomyState> anatomy_state_from_name(std::string_view name) noexcept {
    for (std::size_t index = 0; index < anatomy_state_names.size(); ++index) {
        if (anatomy_state_names[index] == name) {
            return static_cast<AnatomyState>(index);
        }
    }
    return std::nullopt;
}

std::optional<PartIndex> find_declared_part(std::span<const PartDeclaration> declared,
                                            std::string_view part_name) noexcept {
    for (std::size_t index = 0; index < declared.size(); ++index) {
        if (declared[index].name == part_name) {
            PartIndex found;
            found.value = static_cast<std::uint8_t>(index);
            return found;
        }
    }
    return std::nullopt;
}

std::optional<PartIndex> find_part(const ControlAnatomy& anatomy,
                                   std::string_view part_name) noexcept {
    const std::optional<PartIndex> found = find_declared_part(anatomy.parts, part_name);
    return found;
}

std::optional<ItemSystemIndex> find_item_system(const ControlAnatomy& anatomy,
                                                std::string_view system_name) noexcept {
    for (std::size_t index = 0; index < anatomy.item_systems.size(); ++index) {
        if (anatomy.item_systems[index].name == system_name) {
            ItemSystemIndex found;
            found.value = static_cast<std::uint8_t>(index);
            return found;
        }
    }
    return std::nullopt;
}

std::optional<PartIndex> find_item_part(const ItemSystemDeclaration& system,
                                        std::string_view part_name) noexcept {
    const std::optional<PartIndex> found = find_declared_part(system.parts, part_name);
    return found;
}

std::optional<CustomStateIndex> find_custom_state(
    const ControlAnatomy& anatomy, std::string_view state_name) noexcept {
    for (std::size_t index = 0; index < anatomy.custom_states.size(); ++index) {
        if (anatomy.custom_states[index].name == state_name) {
            CustomStateIndex found;
            found.value = static_cast<std::uint8_t>(index);
            return found;
        }
    }
    return std::nullopt;
}

std::optional<CustomStateIndex> find_item_custom_state(
    const ItemSystemDeclaration& system, std::string_view state_name) noexcept {
    for (std::size_t index = 0; index < system.custom_states.size(); ++index) {
        if (system.custom_states[index].name == state_name) {
            CustomStateIndex found;
            found.value = static_cast<std::uint8_t>(index);
            return found;
        }
    }
    return std::nullopt;
}

bool declares_state(const ControlAnatomy& anatomy, AnatomyState state) noexcept {
    for (std::size_t index = 0; index < anatomy.states.size(); ++index) {
        if (anatomy.states[index] == state) {
            return true;
        }
    }
    return false;
}

bool declares_item_state(const ItemSystemDeclaration& system, AnatomyState state) noexcept {
    for (std::size_t index = 0; index < system.states.size(); ++index) {
        if (system.states[index] == state) {
            return true;
        }
    }
    return false;
}

bool declares_part_state(const PartDeclaration& part, AnatomyState state) noexcept {
    for (std::size_t index = 0; index < part.states.size(); ++index) {
        if (part.states[index] == state) {
            return true;
        }
    }
    return false;
}

std::string_view anatomy_defect_name(AnatomyDefect defect) noexcept {
    const std::size_t index = static_cast<std::size_t>(defect);
    if (index >= anatomy_defect_names.size()) {
        return {};
    }
    return anatomy_defect_names[index];
}

AnatomyValidation validate_control_anatomy(const ControlAnatomy& anatomy) noexcept {
    if (!is_registered_name(anatomy.control_id)) {
        return defect_at(AnatomyDefect::control_id_not_snake_case, 0U);
    }
    if (anatomy.parts.empty()) {
        return defect_at(AnatomyDefect::no_parts, 0U);
    }
    if (anatomy.parts.size() > maximum_control_parts) {
        return defect_at(AnatomyDefect::too_many_parts, 0U);
    }
    for (std::size_t index = 0; index < anatomy.parts.size(); ++index) {
        const PartDeclaration& part = anatomy.parts[index];
        if (!is_registered_name(part.name)) {
            return defect_at(AnatomyDefect::part_name_not_snake_case, index);
        }
        if (static_cast<std::size_t>(part.role) >= part_role_count) {
            return defect_at(AnatomyDefect::part_role_out_of_range, index);
        }
        if (!part.composed_from.empty() && !is_registered_name(part.composed_from)) {
            return defect_at(AnatomyDefect::composed_from_not_snake_case, index);
        }
        const AnatomyDefect states_defect = part_states_defect(part);
        if (states_defect != AnatomyDefect::none) {
            return defect_at(states_defect, index);
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (anatomy.parts[earlier].name == part.name) {
                return defect_at(AnatomyDefect::duplicate_part_name, index);
            }
        }
    }
    for (std::size_t index = 0; index < anatomy.states.size(); ++index) {
        const AnatomyState state = anatomy.states[index];
        if (static_cast<std::size_t>(state) >= anatomy_state_count) {
            return defect_at(AnatomyDefect::state_out_of_range, index);
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (anatomy.states[earlier] == state) {
                return defect_at(AnatomyDefect::duplicate_state, index);
            }
        }
    }
    if (anatomy.custom_states.size() > maximum_custom_states) {
        return defect_at(AnatomyDefect::too_many_custom_states, 0U);
    }
    for (std::size_t index = 0; index < anatomy.custom_states.size(); ++index) {
        const std::string_view name = anatomy.custom_states[index].name;
        if (!is_registered_name(name)) {
            return defect_at(AnatomyDefect::custom_state_not_snake_case, index);
        }
        const std::optional<AnatomyState> standard = anatomy_state_from_name(name);
        if (standard.has_value()) {
            return defect_at(AnatomyDefect::custom_state_shadows_standard_state, index);
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (anatomy.custom_states[earlier].name == name) {
                return defect_at(AnatomyDefect::duplicate_custom_state, index);
            }
        }
    }
    if (anatomy.item_systems.size() > maximum_item_systems) {
        return defect_at(AnatomyDefect::too_many_item_systems, 0U);
    }
    for (std::size_t system = 0; system < anatomy.item_systems.size(); ++system) {
        const AnatomyValidation item_validation = validate_item_system(anatomy, system);
        if (item_validation.defect != AnatomyDefect::none) {
            return item_validation;
        }
    }
    AnatomyValidation valid;
    return valid;
}

} // namespace gui_forms
