// GF_TABLE_SCHEMA: the control schema and the engine. See gf_abi.h.

#include "abi_tables.hpp"
#include "abi_records.hpp"

#include "../registry/abi_registry.hpp"
#include "../schema/abi_schema.hpp"

#include "gui_forms/anatomy.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity vocabulary_count_slot{GF_TABLE_SCHEMA, 0U};
constexpr SlotIdentity vocabulary_at_slot{GF_TABLE_SCHEMA, 1U};
constexpr SlotIdentity type_count_slot{GF_TABLE_SCHEMA, 2U};
constexpr SlotIdentity find_type_slot{GF_TABLE_SCHEMA, 3U};
constexpr SlotIdentity type_at_slot{GF_TABLE_SCHEMA, 4U};
constexpr SlotIdentity item_system_at_slot{GF_TABLE_SCHEMA, 5U};
constexpr SlotIdentity part_at_slot{GF_TABLE_SCHEMA, 6U};
constexpr SlotIdentity state_at_slot{GF_TABLE_SCHEMA, 7U};
constexpr SlotIdentity custom_state_at_slot{GF_TABLE_SCHEMA, 8U};
constexpr SlotIdentity property_at_slot{GF_TABLE_SCHEMA, 9U};
constexpr SlotIdentity choice_at_slot{GF_TABLE_SCHEMA, 10U};
constexpr SlotIdentity interaction_at_slot{GF_TABLE_SCHEMA, 11U};
constexpr SlotIdentity engine_slot{GF_TABLE_SCHEMA, 12U};
constexpr SlotIdentity primitive_at_slot{GF_TABLE_SCHEMA, 13U};
constexpr SlotIdentity primitive_name_at_slot{GF_TABLE_SCHEMA, 14U};
constexpr SlotIdentity parameter_at_slot{GF_TABLE_SCHEMA, 15U};
constexpr SlotIdentity parameter_choice_at_slot{GF_TABLE_SCHEMA, 16U};

// One schema call on the UI thread. A library that had no UI thread binds
// the calling thread for the call and is unbound again when the call ends,
// however it ends, unless the call left something that holds it.
class SchemaCall final {
public:
    SchemaCall() noexcept = default;
    SchemaCall(const SchemaCall&) = delete;
    SchemaCall& operator=(const SchemaCall&) = delete;
    ~SchemaCall() {
        if (entered_) {
            registry().unbind_if_idle();
        }
    }

    // Enters the UI thread and builds the schema at the first call.
    [[nodiscard]] gf_status enter(SlotIdentity slot, const Schema*& built) {
        const gf_status entered = registry().enter_ui(slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        entered_ = true;
        built = &schema();
        return GF_STATUS_OK;
    }

private:
    bool entered_ = false;
};

gf_utf8_view view_of(std::string_view text) noexcept {
    const gf_utf8_view view{text.data(), text.size()};
    return view;
}

gf_status check_index(std::uint64_t index, std::size_t count, SlotIdentity slot) {
    if (index >= count) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the index is past the end of its list");
    }
    return GF_STATUS_OK;
}

std::uint32_t packed_color(Color color) noexcept {
    const std::uint32_t packed = (static_cast<std::uint32_t>(color.red) << 24U) |
                                 (static_cast<std::uint32_t>(color.green) << 16U) |
                                 (static_cast<std::uint32_t>(color.blue) << 8U) |
                                 static_cast<std::uint32_t>(color.alpha);
    return packed;
}

// The type at index, after its index is checked.
gf_status find_type_at(const Schema& built, std::uint64_t type, SlotIdentity slot, const SchemaType*& found) {
    const gf_status checked = check_index(type, built.types.size(), slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    found = &built.types[static_cast<std::size_t>(type)];
    return GF_STATUS_OK;
}

// The anatomy scope a part or state index counts in: the type itself
// (system GF_SCHEMA_NONE) or one of its item systems. A type without an
// anatomy has an empty scope.
struct AnatomyScope final {
    std::span<const PartDeclaration> parts;
    std::span<const AnatomyState> states;
    std::span<const CustomStateDeclaration> custom_states;
};

gf_status find_scope(const SchemaType& type, std::uint64_t system, SlotIdentity slot, AnatomyScope& scope) {
    if (type.anatomy == nullptr) {
        if (system != GF_SCHEMA_NONE) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the type has no item systems");
        }
        scope = AnatomyScope{};
        return GF_STATUS_OK;
    }
    const ControlAnatomy& anatomy = *type.anatomy;
    if (system == GF_SCHEMA_NONE) {
        scope = AnatomyScope{anatomy.parts, anatomy.states, anatomy.custom_states};
        return GF_STATUS_OK;
    }
    const gf_status checked = check_index(system, anatomy.item_systems.size(), slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    const ItemSystemDeclaration& declaration = anatomy.item_systems[static_cast<std::size_t>(system)];
    scope = AnatomyScope{declaration.parts, declaration.states, declaration.custom_states};
    return GF_STATUS_OK;
}

gf_status find_primitive(const Schema& built, std::uint64_t primitive, SlotIdentity slot,
                         const PrimitiveDescriptor*& found) {
    const gf_status checked = check_index(primitive, built.primitives.size(), slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    found = &(*built.primitives[static_cast<std::size_t>(primitive)]).descriptor();
    return GF_STATUS_OK;
}

gf_status GF_CALL schema_vocabulary_count(gf_schema_vocabulary vocabulary, std::uint64_t* count) noexcept {
    try {
        if (count == nullptr || vocabulary >= vocabulary_list_count) {
            return fail(GF_STATUS_INVALID_ARGUMENT, vocabulary_count_slot,
                        "vocabulary_count needs a known vocabulary and an output");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(vocabulary_count_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *count = (*built).vocabularies[vocabulary].size();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(vocabulary_count_slot);
    }
}

gf_status GF_CALL schema_vocabulary_at(gf_schema_vocabulary vocabulary, std::uint64_t index,
                                       gf_utf8_view* name) noexcept {
    try {
        if (name == nullptr || vocabulary >= vocabulary_list_count) {
            return fail(GF_STATUS_INVALID_ARGUMENT, vocabulary_at_slot,
                        "vocabulary_at needs a known vocabulary and an output");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(vocabulary_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::vector<std::string>& names = (*built).vocabularies[vocabulary];
        const gf_status checked = check_index(index, names.size(), vocabulary_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        *name = view_of(names[static_cast<std::size_t>(index)]);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(vocabulary_at_slot);
    }
}

gf_status GF_CALL schema_type_count(std::uint64_t* count) noexcept {
    try {
        if (count == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, type_count_slot, "type_count needs an output");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(type_count_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *count = (*built).types.size();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(type_count_slot);
    }
}

gf_status GF_CALL schema_find_type(gf_utf8_view name, std::uint64_t* type) noexcept {
    try {
        if (type == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, find_type_slot, "find_type needs an output");
        }
        std::string_view text;
        const gf_status read = read_text(name, find_type_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (text.empty()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, find_type_slot, "a type name is not empty");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(find_type_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::size_t found = find_catalog_type(text);
        if (found == (*built).types.size()) {
            return fail(GF_STATUS_NOT_FOUND, find_type_slot, "the schema has no type of that name");
        }
        *type = found;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(find_type_slot);
    }
}

gf_status GF_CALL schema_type_at(std::uint64_t type, gf_schema_type* record) noexcept {
    try {
        const gf_status valid = check_record(record, type_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(type_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const SchemaType* found = nullptr;
        const gf_status located = find_type_at(*built, type, type_at_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        gf_schema_type complete{};
        complete.struct_size = sizeof(gf_schema_type);
        complete.kind = (*found).kind;
        complete.id = view_of((*found).id);
        complete.type_name = view_of((*found).type_name);
        if ((*found).anatomy != nullptr) {
            const ControlAnatomy& anatomy = *(*found).anatomy;
            complete.part_count = anatomy.parts.size();
            complete.state_count = anatomy.states.size();
            complete.custom_state_count = anatomy.custom_states.size();
            complete.item_system_count = anatomy.item_systems.size();
        }
        complete.property_count = (*found).properties.size();
        complete.interaction_count = (*found).interactions.size();
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(type_at_slot);
    }
}

gf_status GF_CALL schema_item_system_at(std::uint64_t type, std::uint64_t system,
                                        gf_schema_item_system* record) noexcept {
    try {
        const gf_status valid = check_record(record, item_system_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        if (system == GF_SCHEMA_NONE) {
            return fail(GF_STATUS_INVALID_ARGUMENT, item_system_at_slot, "item_system_at needs an item system index");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(item_system_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const SchemaType* found = nullptr;
        const gf_status located = find_type_at(*built, type, item_system_at_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        AnatomyScope scope;
        const gf_status scoped = find_scope(*found, system, item_system_at_slot, scope);
        if (scoped != GF_STATUS_OK) {
            return scoped;
        }
        const ItemSystemDeclaration& declaration =
            (*(*found).anatomy).item_systems[static_cast<std::size_t>(system)];
        gf_schema_item_system complete{};
        complete.struct_size = sizeof(gf_schema_item_system);
        complete.name = view_of(declaration.name);
        complete.part_count = scope.parts.size();
        complete.state_count = scope.states.size();
        complete.custom_state_count = scope.custom_states.size();
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(item_system_at_slot);
    }
}

gf_status GF_CALL schema_part_at(std::uint64_t type, std::uint64_t system, std::uint64_t part,
                                 gf_schema_part* record) noexcept {
    try {
        const gf_status valid = check_record(record, part_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(part_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const SchemaType* found = nullptr;
        const gf_status located = find_type_at(*built, type, part_at_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        AnatomyScope scope;
        const gf_status scoped = find_scope(*found, system, part_at_slot, scope);
        if (scoped != GF_STATUS_OK) {
            return scoped;
        }
        const gf_status checked = check_index(part, scope.parts.size(), part_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        const PartDeclaration& declaration = scope.parts[static_cast<std::size_t>(part)];
        gf_schema_part complete{};
        complete.struct_size = sizeof(gf_schema_part);
        complete.name = view_of(declaration.name);
        complete.role = view_of(part_role_name(declaration.role));
        complete.composed_from = view_of(declaration.composed_from);
        complete.state_count = declaration.states.size();
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(part_at_slot);
    }
}

gf_status GF_CALL schema_state_at(std::uint64_t type, std::uint64_t system, std::uint64_t part,
                                  std::uint64_t state, gf_utf8_view* name) noexcept {
    try {
        if (name == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, state_at_slot, "state_at needs an output");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(state_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const SchemaType* found = nullptr;
        const gf_status located = find_type_at(*built, type, state_at_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        AnatomyScope scope;
        const gf_status scoped = find_scope(*found, system, state_at_slot, scope);
        if (scoped != GF_STATUS_OK) {
            return scoped;
        }
        std::span<const AnatomyState> states = scope.states;
        if (part != GF_SCHEMA_NONE) {
            const gf_status part_checked = check_index(part, scope.parts.size(), state_at_slot);
            if (part_checked != GF_STATUS_OK) {
                return part_checked;
            }
            states = scope.parts[static_cast<std::size_t>(part)].states;
        }
        const gf_status checked = check_index(state, states.size(), state_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        *name = view_of(anatomy_state_name(states[static_cast<std::size_t>(state)]));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(state_at_slot);
    }
}

gf_status GF_CALL schema_custom_state_at(std::uint64_t type, std::uint64_t system, std::uint64_t state,
                                         gf_utf8_view* name) noexcept {
    try {
        if (name == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, custom_state_at_slot, "custom_state_at needs an output");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(custom_state_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const SchemaType* found = nullptr;
        const gf_status located = find_type_at(*built, type, custom_state_at_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        AnatomyScope scope;
        const gf_status scoped = find_scope(*found, system, custom_state_at_slot, scope);
        if (scoped != GF_STATUS_OK) {
            return scoped;
        }
        const gf_status checked = check_index(state, scope.custom_states.size(), custom_state_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        *name = view_of(scope.custom_states[static_cast<std::size_t>(state)].name);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(custom_state_at_slot);
    }
}

gf_status GF_CALL schema_property_at(std::uint64_t type, std::uint64_t property,
                                     gf_schema_property* record) noexcept {
    try {
        const gf_status valid = check_record(record, property_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(property_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const SchemaType* found = nullptr;
        const gf_status located = find_type_at(*built, type, property_at_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        const gf_status checked = check_index(property, (*found).properties.size(), property_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        const SchemaProperty& held = (*found).properties[static_cast<std::size_t>(property)];
        gf_schema_property complete{};
        complete.struct_size = sizeof(gf_schema_property);
        complete.kind = held.kind;
        complete.name = view_of(held.name);
        complete.category = view_of(held.category);
        complete.enumeration = view_of(held.enumeration);
        complete.choice_count = held.choices.size();
        complete.flags = held.flags;
        complete.serialization = held.serialization;
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(property_at_slot);
    }
}

gf_status GF_CALL schema_choice_at(std::uint64_t type, std::uint64_t property, std::uint64_t choice,
                                   gf_schema_choice* record) noexcept {
    try {
        const gf_status valid = check_record(record, choice_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(choice_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const SchemaType* found = nullptr;
        const gf_status located = find_type_at(*built, type, choice_at_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        const gf_status property_checked = check_index(property, (*found).properties.size(), choice_at_slot);
        if (property_checked != GF_STATUS_OK) {
            return property_checked;
        }
        const SchemaProperty& held = (*found).properties[static_cast<std::size_t>(property)];
        const gf_status checked = check_index(choice, held.choices.size(), choice_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        const SchemaChoice& value = held.choices[static_cast<std::size_t>(choice)];
        gf_schema_choice complete{};
        complete.struct_size = sizeof(gf_schema_choice);
        complete.name = view_of(value.name);
        complete.value = value.value;
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(choice_at_slot);
    }
}

gf_status GF_CALL schema_interaction_at(std::uint64_t type, std::uint64_t interaction,
                                        gf_schema_interaction* record) noexcept {
    try {
        const gf_status valid = check_record(record, interaction_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(interaction_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const SchemaType* found = nullptr;
        const gf_status located = find_type_at(*built, type, interaction_at_slot, found);
        if (located != GF_STATUS_OK) {
            return located;
        }
        const gf_status checked = check_index(interaction, (*found).interactions.size(), interaction_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        const SchemaInteraction& held = (*found).interactions[static_cast<std::size_t>(interaction)];
        gf_schema_interaction complete{};
        complete.struct_size = sizeof(gf_schema_interaction);
        complete.mutable_payload = held.mutable_payload ? 1U : 0U;
        complete.name = view_of(held.name);
        complete.payload = view_of(held.payload);
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(interaction_at_slot);
    }
}

gf_status GF_CALL schema_engine(gf_schema_engine* record) noexcept {
    try {
        const gf_status valid = check_record(record, engine_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(engine_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        gf_schema_engine complete{};
        complete.struct_size = sizeof(gf_schema_engine);
        complete.version_major = current_engine_version.major;
        complete.version_minor = current_engine_version.minor;
        complete.operations_per_device_pixel = maximum_operations_per_device_pixel;
        complete.animated_operations_per_frame = maximum_animated_operations_per_frame;
        complete.expression_maximum_operations = maximum_expression_operations;
        complete.expression_maximum_stack = maximum_expression_stack;
        complete.published_values_maximum = maximum_published_values;
        complete.transition_maximum_states = maximum_presentation_states;
        complete.transition_maximum_per_part = maximum_part_transitions;
        complete.ambient_maximum_frames_per_second = maximum_ambient_frames_per_second;
        complete.ambient_maximum_window_fraction = maximum_ambient_window_fraction;
        complete.primitive_count = (*built).primitives.size();
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(engine_slot);
    }
}

gf_status GF_CALL schema_primitive_at(std::uint64_t primitive, gf_schema_primitive* record) noexcept {
    try {
        const gf_status valid = check_record(record, primitive_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(primitive_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const PrimitiveDescriptor* descriptor = nullptr;
        const gf_status located = find_primitive(*built, primitive, primitive_at_slot, descriptor);
        if (located != GF_STATUS_OK) {
            return located;
        }
        gf_schema_primitive complete{};
        complete.struct_size = sizeof(gf_schema_primitive);
        complete.introduced_major = (*descriptor).introduced.major;
        complete.introduced_minor = (*descriptor).introduced.minor;
        complete.cost_per_device_pixel = (*descriptor).cost.per_device_pixel;
        complete.cost_fixed = (*descriptor).cost.fixed;
        complete.cost_preparation_per_device_pixel = (*descriptor).cost.preparation_per_device_pixel;
        complete.name = view_of((*descriptor).name);
        complete.outsets.left = (*descriptor).outsets.left;
        complete.outsets.top = (*descriptor).outsets.top;
        complete.outsets.right = (*descriptor).outsets.right;
        complete.outsets.bottom = (*descriptor).outsets.bottom;
        complete.role_count = (*descriptor).roles.size();
        complete.state_read_count = (*descriptor).states_read.size();
        complete.input_read_count = (*descriptor).inputs_read.size();
        complete.parameter_count = (*descriptor).parameters.size();
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(primitive_at_slot);
    }
}

gf_status GF_CALL schema_primitive_name_at(std::uint64_t primitive, gf_schema_primitive_list list,
                                           std::uint64_t index, gf_utf8_view* name) noexcept {
    try {
        if (name == nullptr || list > GF_SCHEMA_PRIMITIVE_LIST_INPUTS_READ) {
            return fail(GF_STATUS_INVALID_ARGUMENT, primitive_name_at_slot,
                        "primitive_name_at needs a known list and an output");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(primitive_name_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const PrimitiveDescriptor* descriptor = nullptr;
        const gf_status located = find_primitive(*built, primitive, primitive_name_at_slot, descriptor);
        if (located != GF_STATUS_OK) {
            return located;
        }
        std::size_t count = 0U;
        if (list == GF_SCHEMA_PRIMITIVE_LIST_ROLES) {
            count = (*descriptor).roles.size();
        } else if (list == GF_SCHEMA_PRIMITIVE_LIST_STATES_READ) {
            count = (*descriptor).states_read.size();
        } else {
            count = (*descriptor).inputs_read.size();
        }
        const gf_status checked = check_index(index, count, primitive_name_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        const std::size_t position = static_cast<std::size_t>(index);
        if (list == GF_SCHEMA_PRIMITIVE_LIST_ROLES) {
            *name = view_of(part_role_name((*descriptor).roles[position]));
        } else if (list == GF_SCHEMA_PRIMITIVE_LIST_STATES_READ) {
            *name = view_of(anatomy_state_name((*descriptor).states_read[position]));
        } else {
            *name = view_of(engine_input_name((*descriptor).inputs_read[position]));
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(primitive_name_at_slot);
    }
}

gf_schema_parameter_kind parameter_kind_of(PrimitiveParameterKind kind) noexcept {
    switch (kind) {
    case PrimitiveParameterKind::number:
        return GF_SCHEMA_PARAMETER_KIND_NUMBER;
    case PrimitiveParameterKind::choice:
        return GF_SCHEMA_PARAMETER_KIND_CHOICE;
    case PrimitiveParameterKind::colour:
        return GF_SCHEMA_PARAMETER_KIND_COLOR;
    }
    return GF_SCHEMA_PARAMETER_KIND_NUMBER;
}

gf_status GF_CALL schema_parameter_at(std::uint64_t primitive, std::uint64_t parameter,
                                      gf_schema_parameter* record) noexcept {
    try {
        const gf_status valid = check_record(record, parameter_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(parameter_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const PrimitiveDescriptor* descriptor = nullptr;
        const gf_status located = find_primitive(*built, primitive, parameter_at_slot, descriptor);
        if (located != GF_STATUS_OK) {
            return located;
        }
        const gf_status checked = check_index(parameter, (*descriptor).parameters.size(), parameter_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        const PrimitiveParameterDeclaration& declaration =
            (*descriptor).parameters[static_cast<std::size_t>(parameter)];
        gf_schema_parameter complete{};
        complete.struct_size = sizeof(gf_schema_parameter);
        complete.kind = parameter_kind_of(declaration.kind);
        complete.name = view_of(declaration.name);
        complete.minimum = declaration.minimum;
        complete.maximum = declaration.maximum;
        complete.default_number = declaration.default_number;
        complete.default_color = packed_color(declaration.default_colour);
        complete.animated = declaration.animated ? 1U : 0U;
        complete.choice_count = declaration.choices.size();
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(parameter_at_slot);
    }
}

gf_status GF_CALL schema_parameter_choice_at(std::uint64_t primitive, std::uint64_t parameter,
                                             std::uint64_t choice, gf_utf8_view* name) noexcept {
    try {
        if (name == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, parameter_choice_at_slot, "parameter_choice_at needs an output");
        }
        SchemaCall call;
        const Schema* built = nullptr;
        const gf_status entered = call.enter(parameter_choice_at_slot, built);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const PrimitiveDescriptor* descriptor = nullptr;
        const gf_status located = find_primitive(*built, primitive, parameter_choice_at_slot, descriptor);
        if (located != GF_STATUS_OK) {
            return located;
        }
        const gf_status parameter_checked =
            check_index(parameter, (*descriptor).parameters.size(), parameter_choice_at_slot);
        if (parameter_checked != GF_STATUS_OK) {
            return parameter_checked;
        }
        const PrimitiveParameterDeclaration& declaration =
            (*descriptor).parameters[static_cast<std::size_t>(parameter)];
        const gf_status checked = check_index(choice, declaration.choices.size(), parameter_choice_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        *name = view_of(declaration.choices[static_cast<std::size_t>(choice)]);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(parameter_choice_at_slot);
    }
}

constexpr gf_schema_table table{
    .struct_size = sizeof(gf_schema_table),
    .abi_minor = GF_ABI_MINOR,
    .vocabulary_count = &schema_vocabulary_count,
    .vocabulary_at = &schema_vocabulary_at,
    .type_count = &schema_type_count,
    .find_type = &schema_find_type,
    .type_at = &schema_type_at,
    .item_system_at = &schema_item_system_at,
    .part_at = &schema_part_at,
    .state_at = &schema_state_at,
    .custom_state_at = &schema_custom_state_at,
    .property_at = &schema_property_at,
    .choice_at = &schema_choice_at,
    .interaction_at = &schema_interaction_at,
    .engine = &schema_engine,
    .primitive_at = &schema_primitive_at,
    .primitive_name_at = &schema_primitive_name_at,
    .parameter_at = &schema_parameter_at,
    .parameter_choice_at = &schema_parameter_choice_at,
};

} // namespace

const gf_schema_table& schema_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
