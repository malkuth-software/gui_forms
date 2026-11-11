// The schema the C ABI serves. See abi_schema.hpp.

#include "abi_schema.hpp"

#include "abi_interaction_fields.hpp"
#include "abi_schema_catalog.hpp"
#include "../windows/abi_window_properties.hpp"

#include "gui_forms/anatomy.hpp"
#include "gui_forms/semantics.hpp"
#include "gui_forms/theme.hpp"

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

// Orders the generated payload table by its C++ spelling (sorted so).
struct InteractionFieldsBefore final {
    bool operator()(const InteractionFieldsEntry& entry, std::string_view payload) const noexcept {
        return entry.payload < payload;
    }
};


void add_name(std::vector<std::string>& names, std::string_view name) {
    names.emplace_back(name);
}

// The vocabulary lists, in gf_schema_vocabulary order, each in the order of
// the library's enum (or, for lists without one, the order they were
// admitted).
void build_vocabularies(Schema& built) {
    std::vector<std::string>& part_roles = built.vocabularies[GF_SCHEMA_VOCABULARY_PART_ROLES];
    for (std::size_t index = 0; index < part_role_count; ++index) {
        add_name(part_roles, part_role_name(static_cast<PartRole>(index)));
    }
    std::vector<std::string>& states = built.vocabularies[GF_SCHEMA_VOCABULARY_STATES];
    for (std::size_t index = 0; index < anatomy_state_count; ++index) {
        add_name(states, anatomy_state_name(static_cast<AnatomyState>(index)));
    }
    std::vector<std::string>& custom_states = built.vocabularies[GF_SCHEMA_VOCABULARY_CUSTOM_STATES];
    for (const CustomStateDeclaration& state : admitted_custom_states) {
        add_name(custom_states, state.name);
    }
    // Mark zero is the absence of a mark.
    std::vector<std::string>& marks = built.vocabularies[GF_SCHEMA_VOCABULARY_MARKS];
    for (std::size_t index = 1; index < part_mark_count; ++index) {
        add_name(marks, part_mark_name(static_cast<PartMark>(index)));
    }
    std::vector<std::string>& inputs = built.vocabularies[GF_SCHEMA_VOCABULARY_ENGINE_INPUTS];
    for (std::size_t index = 0; index < engine_input_count; ++index) {
        add_name(inputs, engine_input_name(static_cast<EngineInput>(index)));
    }
    std::vector<std::string>& values = built.vocabularies[GF_SCHEMA_VOCABULARY_VALUES];
    for (const std::string_view value : part_value_names) {
        add_name(values, value);
    }
    std::vector<std::string>& families = built.vocabularies[GF_SCHEMA_VOCABULARY_FAMILIES];
    for (std::size_t index = 0; index < control_visual_role_count; ++index) {
        add_name(families, control_visual_role_name(static_cast<ControlVisualRole>(index)));
    }
    std::vector<std::string>& roles = built.vocabularies[GF_SCHEMA_VOCABULARY_SEMANTIC_ROLES];
    for (std::size_t index = 0; index < semantic_role_count; ++index) {
        add_name(roles, semantic_role_name(static_cast<SemanticRole>(index)));
    }
    std::vector<std::string>& actions = built.vocabularies[GF_SCHEMA_VOCABULARY_SEMANTIC_ACTIONS];
    for (std::size_t index = 0; index < semantic_action_count; ++index) {
        add_name(actions, semantic_action_name(static_cast<SemanticAction>(index)));
    }
    std::vector<std::string>& semantic_state_names = built.vocabularies[GF_SCHEMA_VOCABULARY_SEMANTIC_STATES];
    for (const SemanticState state : semantic_states) {
        add_name(semantic_state_names, semantic_state_name(state));
    }
    std::vector<std::string>& fields = built.vocabularies[GF_SCHEMA_VOCABULARY_SEMANTIC_FIELDS];
    const std::span<const char* const> field_names = semantic_field_names();
    for (const char* const field : field_names) {
        add_name(fields, field);
    }
    std::vector<std::string>& levels = built.vocabularies[GF_SCHEMA_VOCABULARY_CASCADE_LEVELS];
    for (std::size_t index = 0; index < static_cast<std::size_t>(CascadeLevel::count); ++index) {
        add_name(levels, cascade_level_name(static_cast<CascadeLevel>(index)));
    }
    std::vector<std::string>& operations = built.vocabularies[GF_SCHEMA_VOCABULARY_EXPRESSION_OPERATIONS];
    for (std::size_t index = 0; index < expression_operation_count; ++index) {
        add_name(operations, expression_operation_name(static_cast<ExpressionOperation>(index)));
    }
    // The properties a presentational state machine animates besides the
    // primitives' animated parameters.
    std::vector<std::string>& engine_properties = built.vocabularies[GF_SCHEMA_VOCABULARY_ENGINE_PROPERTIES];
    add_name(engine_properties, opacity_property);
    add_name(engine_properties, scale_property);
    add_name(engine_properties, face_property);
    add_name(engine_properties, translate_x_property);
    add_name(engine_properties, translate_y_property);
}

gf_schema_serialization serialization_of(PropertySerializationVisibility visibility) noexcept {
    switch (visibility) {
    case PropertySerializationVisibility::hidden:
        return GF_SCHEMA_SERIALIZATION_HIDDEN;
    case PropertySerializationVisibility::visible:
        return GF_SCHEMA_SERIALIZATION_VISIBLE;
    case PropertySerializationVisibility::content:
        return GF_SCHEMA_SERIALIZATION_CONTENT;
    }
    return GF_SCHEMA_SERIALIZATION_HIDDEN;
}

SchemaProperty property_of(const PropertyDescriptor& descriptor) {
    SchemaProperty property;
    property.name = descriptor.name;
    property.kind = value_kind_of(descriptor.kind);
    property.category = descriptor.category;
    if (descriptor.enumeration != nullptr) {
        const PropertyEnumDescriptor& enumeration = *descriptor.enumeration;
        property.enumeration = enumeration.type_name;
        property.choices.reserve(enumeration.choices.size());
        for (const PropertyEnumChoice& choice : enumeration.choices) {
            property.choices.push_back(SchemaChoice{choice.name, choice.value});
        }
    }
    gf_schema_property_flags flags = 0U;
    if (descriptor.readable) flags |= GF_SCHEMA_PROPERTY_FLAGS_READABLE;
    if (descriptor.writable) flags |= GF_SCHEMA_PROPERTY_FLAGS_WRITABLE;
    if (descriptor.bindable) flags |= GF_SCHEMA_PROPERTY_FLAGS_BINDABLE;
    if (descriptor.nullable) flags |= GF_SCHEMA_PROPERTY_FLAGS_NULLABLE;
    property.flags = flags;
    property.serialization = serialization_of(descriptor.serialization_visibility);
    return property;
}

SchemaType type_of(const SchemaCatalogEntry& entry) {
    SchemaSample sample;
    (*entry.describe)(sample);
    SchemaType type;
    type.id = entry.id;
    type.type_name = entry.type_name;
    type.anatomy = sample.anatomy;
    if (sample.anatomy != nullptr) {
        type.kind = GF_SCHEMA_KIND_CONTROL;
    } else if (sample.recorded) {
        type.kind = GF_SCHEMA_KIND_COMPONENT;
    } else {
        type.kind = GF_SCHEMA_KIND_SERVICE;
    }
    type.properties.reserve(sample.properties.size());
    for (const PropertyDescriptor& descriptor : sample.properties) {
        type.properties.push_back(property_of(descriptor));
    }
    type.child_properties.reserve(sample.child_properties.size());
    for (const PropertyDescriptor& descriptor : sample.child_properties) {
        type.child_properties.push_back(property_of(descriptor));
    }
    type.interactions.reserve(sample.interactions.size());
    for (const InteractionDeclaration& declaration : sample.interactions) {
        type.interactions.push_back(SchemaInteraction{std::string(declaration.name),
                                                      std::string(interaction_payload_fields(declaration.payload)),
                                                      declaration.mutable_payload});
    }
    return type;
}

// The window type: a component whose properties are Window's chrome
// properties and whose interactions are the ones every window raises.
SchemaType window_type() {
    SchemaType type;
    type.id = window_type_id;
    type.type_name = "Window";
    type.kind = GF_SCHEMA_KIND_COMPONENT;
    const std::span<const WindowPropertyEntry> chrome = window_properties();
    type.properties.reserve(chrome.size());
    for (const WindowPropertyEntry& entry : chrome) {
        SchemaProperty property;
        property.name = std::string(entry.name);
        property.kind = entry.kind;
        property.category = std::string(entry.category);
        property.enumeration = std::string(entry.enumeration);
        property.choices.reserve(entry.choices.size());
        for (const WindowPropertyChoice& choice : entry.choices) {
            property.choices.push_back(SchemaChoice{std::string(choice.name), choice.value});
        }
        property.flags = GF_SCHEMA_PROPERTY_FLAGS_READABLE | GF_SCHEMA_PROPERTY_FLAGS_WRITABLE;
        property.serialization = GF_SCHEMA_SERIALIZATION_VISIBLE;
        type.properties.push_back(std::move(property));
    }
    std::vector<InteractionDeclaration> interactions;
    Window::append_interactions(interactions);
    type.interactions.reserve(interactions.size());
    for (const InteractionDeclaration& declaration : interactions) {
        type.interactions.push_back(SchemaInteraction{std::string(declaration.name),
                                                      std::string(interaction_payload_fields(declaration.payload)),
                                                      declaration.mutable_payload});
    }
    return type;
}

std::unique_ptr<Schema> build_schema() {
    std::unique_ptr<Schema> built = std::make_unique<Schema>();
    build_vocabularies(*built);
    (*built).types.reserve(schema_catalog.size() + 1U);
    for (const SchemaCatalogEntry& entry : schema_catalog) {
        (*built).types.push_back(type_of(entry));
    }
    (*built).types.push_back(window_type());
    (*built).primitives = builtin_primitive_registry().primitives();
    return built;
}

} // namespace

gf_value_kind value_kind_of(BindingValueKind kind) noexcept {
    switch (kind) {
    case BindingValueKind::null: return GF_VALUE_KIND_NULL;
    case BindingValueKind::boolean: return GF_VALUE_KIND_BOOLEAN;
    case BindingValueKind::signed_integer: return GF_VALUE_KIND_SIGNED;
    case BindingValueKind::unsigned_integer: return GF_VALUE_KIND_UNSIGNED;
    case BindingValueKind::number: return GF_VALUE_KIND_NUMBER;
    case BindingValueKind::text: return GF_VALUE_KIND_TEXT;
    case BindingValueKind::point: return GF_VALUE_KIND_POINT;
    case BindingValueKind::size: return GF_VALUE_KIND_SIZE;
    case BindingValueKind::rectangle: return GF_VALUE_KIND_RECTANGLE;
    case BindingValueKind::insets: return GF_VALUE_KIND_INSETS;
    case BindingValueKind::color: return GF_VALUE_KIND_COLOR;
    case BindingValueKind::font: return GF_VALUE_KIND_FONT;
    case BindingValueKind::image: return GF_VALUE_KIND_IMAGE;
    case BindingValueKind::enumeration: return GF_VALUE_KIND_ENUMERATION;
    case BindingValueKind::object: return GF_VALUE_KIND_OBJECT;
    case BindingValueKind::collection: return GF_VALUE_KIND_COLLECTION;
    }
    return GF_VALUE_KIND_NULL;
}

const Schema& schema() {
    // Leaked on purpose: the text it lends lives until the process ends.
    // Only the UI thread reaches here, so no two builds run at once, and the
    // registry's lock orders this write before any later thread's read.
    static Schema* built = nullptr;
    if (built == nullptr) {
        built = build_schema().release();
    }
    return *built;
}

std::size_t catalog_type_count() noexcept {
    return schema_catalog.size();
}

std::size_t find_catalog_type(std::string_view name) {
    const std::string id = registered_name(name);
    for (std::size_t index = 0; index < schema_catalog.size(); ++index) {
        if (schema_catalog[index].id == id) {
            return index;
        }
    }
    return schema_catalog.size();
}

bool names_window_type(std::string_view name) {
    const std::string id = registered_name(name);
    const bool window = id == window_type_id;
    return window;
}

std::size_t find_schema_type(std::string_view name) {
    const std::size_t found = find_catalog_type(name);
    if (found != schema_catalog.size()) return found;
    if (names_window_type(name)) return schema_catalog.size();
    const std::size_t none = schema_catalog.size() + 1U;
    return none;
}

std::string_view catalog_type_id(std::size_t index) {
    const std::string_view id = schema_catalog.at(index).id;
    return id;
}

Component::Ptr create_catalog_type(std::size_t index, StableId stable_id) {
    const Component::Ptr object = (*schema_catalog.at(index).create)(std::move(stable_id));
    return object;
}

std::string_view catalog_type_id_of(const Component& object) {
    // A schema type is found by its exact type: one with no anatomy of its
    // own publishes its base's (a plot item the base "control", a wizard page
    // "panel"). An application's own subclass publishes its nearest schema
    // type's anatomy, and with it that type's id.
    for (const SchemaCatalogEntry& entry : schema_catalog) {
        if ((*entry.is_exact)(object)) {
            return entry.id;
        }
    }
    const Control* control = dynamic_cast<const Control*>(&object);
    if (control != nullptr) {
        return (*control).anatomy().control_id;
    }
    return std::string_view{};
}

std::string_view interaction_payload_fields(std::string_view payload) noexcept {
    const std::array<InteractionFieldsEntry, interaction_fields.size()>::const_iterator found =
        std::lower_bound(interaction_fields.begin(), interaction_fields.end(), payload, InteractionFieldsBefore{});
    if (found == interaction_fields.end() || (*found).payload != payload) {
        return std::string_view();
    }
    return (*found).fields;
}

} // namespace gui_forms::abi1
