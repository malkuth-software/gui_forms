// The stage C3 gate: the control schema read through the C ABI alone, written
// in the format of manifests/control-tables.schema.json, equals that file byte for
// byte (carriage returns aside). The program consumes gf_abi.h and the
// standard library only, as a binding generator would.
//
// Usage: gf_abi_schema_export <control-tables.schema.json> [<written.json>]
// The second path, when given, receives the schema as the ABI gave it, so
// a failure can be compared line by line.

#include "gui_forms/c/gf_abi.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

// The tables this program reads.
struct AbiTables final {
    gf_schema_table schema{};
};

AbiTables tables;

void require_ok(gf_status status, const char* call) {
    if (status != GF_STATUS_OK) {
        throw std::runtime_error(std::string(call) + " failed with status " + std::to_string(status));
    }
}

std::string_view text_of(gf_utf8_view view) {
    const std::string_view text(view.data, static_cast<std::size_t>(view.size));
    return text;
}

// Appends text as a JSON string literal.
void append_json_string(std::string& out, std::string_view text) {
    out += '"';
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '"' || character == '\\') {
            out += '\\';
            out += character;
        } else if (character == '\n') {
            out += "\\n";
        } else if (static_cast<unsigned char>(character) < 0x20U) {
            constexpr std::string_view hex = "0123456789abcdef";
            const unsigned int code = static_cast<unsigned char>(character);
            out += "\\u00";
            out += hex[code >> 4U];
            out += hex[code & 0xFU];
        } else {
            out += character;
        }
    }
    out += '"';
}

void append_key(std::string& out, std::string_view indent, std::string_view key) {
    out += indent;
    append_json_string(out, key);
    out += ": ";
}

void append_bool(std::string& out, bool value) {
    out += value ? "true" : "false";
}

// A number in its shortest round-trip form.
void append_number(std::string& out, double value) {
    std::array<char, 32> text{};
    const std::to_chars_result written = std::to_chars(text.data(), text.data() + text.size(), value);
    out.append(text.data(), static_cast<std::size_t>(written.ptr - text.data()));
}

void append_unsigned(std::string& out, std::uint64_t value) {
    out += std::to_string(value);
}

void append_version(std::string& out, std::uint32_t major, std::uint32_t minor) {
    const std::string text = std::to_string(major) + "." + std::to_string(minor);
    append_json_string(out, text);
}

// A colour packed as 0xRRGGBBAA, written #rrggbbaa.
void append_colour(std::string& out, std::uint32_t packed) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text = "#";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text += digits[(packed >> static_cast<unsigned int>(shift)) & 0xFU];
    }
    append_json_string(out, text);
}

std::string_view value_kind_name(gf_value_kind kind) {
    switch (kind) {
    case GF_VALUE_KIND_NULL: return "null";
    case GF_VALUE_KIND_BOOLEAN: return "boolean";
    case GF_VALUE_KIND_SIGNED: return "signed_integer";
    case GF_VALUE_KIND_UNSIGNED: return "unsigned_integer";
    case GF_VALUE_KIND_NUMBER: return "number";
    case GF_VALUE_KIND_TEXT: return "text";
    case GF_VALUE_KIND_POINT: return "point";
    case GF_VALUE_KIND_SIZE: return "size";
    case GF_VALUE_KIND_RECTANGLE: return "rectangle";
    case GF_VALUE_KIND_INSETS: return "insets";
    case GF_VALUE_KIND_COLOR: return "color";
    case GF_VALUE_KIND_FONT: return "font";
    case GF_VALUE_KIND_IMAGE: return "image";
    case GF_VALUE_KIND_ENUMERATION: return "enumeration";
    case GF_VALUE_KIND_OBJECT: return "object";
    case GF_VALUE_KIND_COLLECTION: return "collection";
    default: return "unknown";
    }
}

std::string_view serialization_name(gf_schema_serialization serialization) {
    switch (serialization) {
    case GF_SCHEMA_SERIALIZATION_HIDDEN: return "hidden";
    case GF_SCHEMA_SERIALIZATION_VISIBLE: return "visible";
    case GF_SCHEMA_SERIALIZATION_CONTENT: return "content";
    default: return "unknown";
    }
}

// One vocabulary list on one line.
void append_vocabulary_list(std::string& out, gf_schema_vocabulary vocabulary) {
    std::uint64_t count = 0U;
    require_ok(tables.schema.vocabulary_count(vocabulary, &count), "vocabulary_count");
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_utf8_view name{};
        require_ok(tables.schema.vocabulary_at(vocabulary, index, &name), "vocabulary_at");
        if (index > 0U) out += ", ";
        append_json_string(out, text_of(name));
    }
    out += ']';
}

struct VocabularyKey final {
    std::string_view key;
    gf_schema_vocabulary vocabulary = 0U;
};

void append_vocabulary(std::string& out) {
    constexpr std::array<VocabularyKey, 11> keys{{
        {"part_roles", GF_SCHEMA_VOCABULARY_PART_ROLES},
        {"states", GF_SCHEMA_VOCABULARY_STATES},
        {"custom_states", GF_SCHEMA_VOCABULARY_CUSTOM_STATES},
        {"marks", GF_SCHEMA_VOCABULARY_MARKS},
        {"engine_inputs", GF_SCHEMA_VOCABULARY_ENGINE_INPUTS},
        {"values", GF_SCHEMA_VOCABULARY_VALUES},
        {"families", GF_SCHEMA_VOCABULARY_FAMILIES},
        {"semantic_roles", GF_SCHEMA_VOCABULARY_SEMANTIC_ROLES},
        {"semantic_actions", GF_SCHEMA_VOCABULARY_SEMANTIC_ACTIONS},
        {"semantic_states", GF_SCHEMA_VOCABULARY_SEMANTIC_STATES},
        {"semantic_fields", GF_SCHEMA_VOCABULARY_SEMANTIC_FIELDS},
    }};
    out += "  \"vocabulary\": {\n";
    for (std::size_t index = 0; index < keys.size(); ++index) {
        append_key(out, "    ", keys[index].key);
        append_vocabulary_list(out, keys[index].vocabulary);
        out += index + 1U < keys.size() ? ",\n" : "\n";
    }
    out += "  },\n";
}

void append_primitive_list(std::string& out, std::uint64_t primitive, gf_schema_primitive_list list,
                           std::uint64_t count) {
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_utf8_view name{};
        require_ok(tables.schema.primitive_name_at(primitive, list, index, &name), "primitive_name_at");
        if (index > 0U) out += ", ";
        append_json_string(out, text_of(name));
    }
    out += ']';
}

void append_parameters(std::string& out, std::uint64_t primitive, std::uint64_t count) {
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_schema_parameter parameter{};
        parameter.struct_size = sizeof(parameter);
        require_ok(tables.schema.parameter_at(primitive, index, &parameter), "parameter_at");
        out += index == 0U ? "\n" : ",\n";
        out += "          {\"name\": ";
        append_json_string(out, text_of(parameter.name));
        out += ", \"kind\": ";
        if (parameter.kind == GF_SCHEMA_PARAMETER_KIND_NUMBER) {
            out += "\"number\", \"minimum\": ";
            append_number(out, parameter.minimum);
            out += ", \"maximum\": ";
            append_number(out, parameter.maximum);
            out += ", \"default\": ";
            append_number(out, parameter.default_number);
        } else if (parameter.kind == GF_SCHEMA_PARAMETER_KIND_CHOICE) {
            out += "\"choice\", \"choices\": [";
            for (std::uint64_t choice = 0; choice < parameter.choice_count; ++choice) {
                gf_utf8_view name{};
                require_ok(tables.schema.parameter_choice_at(primitive, index, choice, &name), "parameter_choice_at");
                if (choice > 0U) out += ", ";
                append_json_string(out, text_of(name));
            }
            out += "], \"default\": ";
            gf_utf8_view chosen{};
            const std::uint64_t default_choice = static_cast<std::uint64_t>(parameter.default_number);
            require_ok(tables.schema.parameter_choice_at(primitive, index, default_choice, &chosen), "parameter_choice_at");
            append_json_string(out, text_of(chosen));
        } else {
            out += "\"colour\", \"default\": ";
            append_colour(out, parameter.default_color);
        }
        out += ", \"animated\": ";
        append_bool(out, parameter.animated == 1U);
        out += '}';
    }
    out += count == 0U ? "]" : "\n        ]";
}

// The names manifests/control-schema.json spells the effect constants with.
std::string_view effect_time_model_name(gf_schema_effect_time_model model) {
    if (model == GF_SCHEMA_EFFECT_TIME_MODEL_STATIC_POSE) return "static_pose";
    if (model == GF_SCHEMA_EFFECT_TIME_MODEL_TRANSITION) return "transition";
    if (model == GF_SCHEMA_EFFECT_TIME_MODEL_AMBIENT) return "ambient";
    throw std::runtime_error("effect_at reported an unknown time model " + std::to_string(model));
}

std::string_view effect_input_name(gf_schema_effect_input input) {
    if (input == GF_SCHEMA_EFFECT_INPUT_NONE) return "none";
    if (input == GF_SCHEMA_EFFECT_INPUT_POINTER) return "pointer";
    if (input == GF_SCHEMA_EFFECT_INPUT_POINTER_AND_KEYS) return "pointer_and_keys";
    throw std::runtime_error("effect_at reported an unknown input " + std::to_string(input));
}

// engine.effects: the effect programs, one per line in registration order
// (effect_count and effect_at, since 1.1). The manifest leaves out whether
// the plugin verified each, since that depends on the build.
void append_effects(std::string& out) {
    std::uint64_t count = 0;
    require_ok(tables.schema.effect_count(&count), "effect_count");
    append_key(out, "    ", "effects");
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_schema_effect effect{};
        effect.struct_size = sizeof(effect);
        require_ok(tables.schema.effect_at(index, &effect), "effect_at");
        const bool setup_on_geometry = (effect.flags & GF_SCHEMA_EFFECT_FLAGS_SETUP_ON_GEOMETRY) != 0U;
        out += index == 0U ? "\n      {" : ",\n      {";
        out += "\"name\": ";
        append_json_string(out, text_of(effect.name));
        out += ", \"parent\": ";
        append_json_string(out, text_of(effect.parent));
        out += ", \"time_model\": ";
        append_json_string(out, effect_time_model_name(effect.time_model));
        out += ", \"engine_version\": ";
        append_unsigned(out, effect.engine_version);
        out += ", \"cost\": {\"fixed\": ";
        append_unsigned(out, effect.cost_fixed);
        out += ", \"per_device_pixel\": ";
        append_unsigned(out, effect.cost_per_device_pixel);
        out += "}, \"setup_cost\": ";
        append_unsigned(out, effect.setup_cost);
        out += ", \"setup_on_geometry\": ";
        append_bool(out, setup_on_geometry);
        out += ", \"state_budget\": ";
        append_unsigned(out, effect.state_budget);
        out += ", \"frames_per_second\": ";
        append_number(out, effect.frames_per_second);
        out += ", \"outsets\": [";
        append_number(out, effect.outsets.left);
        out += ", ";
        append_number(out, effect.outsets.top);
        out += ", ";
        append_number(out, effect.outsets.right);
        out += ", ";
        append_number(out, effect.outsets.bottom);
        out += "], \"input\": ";
        append_json_string(out, effect_input_name(effect.input));
        out += '}';
    }
    out += count == 0U ? "]" : "\n    ]";
}

void append_engine(std::string& out) {
    gf_schema_engine engine{};
    engine.struct_size = sizeof(engine);
    require_ok(tables.schema.engine(&engine), "engine");
    out += "  \"engine\": {\n";
    append_key(out, "    ", "version");
    append_version(out, engine.version_major, engine.version_minor);
    out += ",\n";
    append_key(out, "    ", "cascade_levels");
    append_vocabulary_list(out, GF_SCHEMA_VOCABULARY_CASCADE_LEVELS);
    out += ",\n";
    append_key(out, "    ", "budgets");
    out += "{\"operations_per_device_pixel\": ";
    append_unsigned(out, engine.operations_per_device_pixel);
    out += ", \"animated_operations_per_frame\": ";
    append_unsigned(out, engine.animated_operations_per_frame);
    out += "},\n";
    append_key(out, "    ", "primitives");
    out += '[';
    for (std::uint64_t index = 0; index < engine.primitive_count; ++index) {
        gf_schema_primitive primitive{};
        primitive.struct_size = sizeof(primitive);
        require_ok(tables.schema.primitive_at(index, &primitive), "primitive_at");
        out += index == 0U ? "\n" : ",\n";
        out += "      {\n";
        append_key(out, "        ", "name");
        append_json_string(out, text_of(primitive.name));
        out += ",\n";
        append_key(out, "        ", "introduced");
        append_version(out, primitive.introduced_major, primitive.introduced_minor);
        out += ",\n";
        append_key(out, "        ", "roles");
        append_primitive_list(out, index, GF_SCHEMA_PRIMITIVE_LIST_ROLES, primitive.role_count);
        out += ",\n";
        append_key(out, "        ", "states_read");
        append_primitive_list(out, index, GF_SCHEMA_PRIMITIVE_LIST_STATES_READ, primitive.state_read_count);
        out += ",\n";
        append_key(out, "        ", "inputs_read");
        append_primitive_list(out, index, GF_SCHEMA_PRIMITIVE_LIST_INPUTS_READ, primitive.input_read_count);
        out += ",\n";
        append_key(out, "        ", "cost");
        out += "{\"per_device_pixel\": ";
        append_unsigned(out, primitive.cost_per_device_pixel);
        out += ", \"fixed\": ";
        append_unsigned(out, primitive.cost_fixed);
        out += ", \"preparation_per_device_pixel\": ";
        append_unsigned(out, primitive.cost_preparation_per_device_pixel);
        out += "},\n";
        append_key(out, "        ", "outsets");
        out += '[';
        append_number(out, primitive.outsets.left);
        out += ", ";
        append_number(out, primitive.outsets.top);
        out += ", ";
        append_number(out, primitive.outsets.right);
        out += ", ";
        append_number(out, primitive.outsets.bottom);
        out += "],\n";
        append_key(out, "        ", "parameters");
        append_parameters(out, index, primitive.parameter_count);
        out += "\n      }";
    }
    out += engine.primitive_count == 0U ? "]" : "\n    ]";
    out += ",\n";
    append_key(out, "    ", "expressions");
    out += "{\"maximum_operations\": ";
    append_unsigned(out, engine.expression_maximum_operations);
    out += ", \"maximum_stack\": ";
    append_unsigned(out, engine.expression_maximum_stack);
    out += ", \"operations\": ";
    append_vocabulary_list(out, GF_SCHEMA_VOCABULARY_EXPRESSION_OPERATIONS);
    out += "},\n";
    append_key(out, "    ", "published_values");
    out += "{\"maximum\": ";
    append_unsigned(out, engine.published_values_maximum);
    out += "},\n";
    append_key(out, "    ", "transitions");
    out += "{\"engine_properties\": ";
    append_vocabulary_list(out, GF_SCHEMA_VOCABULARY_ENGINE_PROPERTIES);
    out += ", \"maximum_states\": ";
    append_unsigned(out, engine.transition_maximum_states);
    out += ", \"maximum_per_part\": ";
    append_unsigned(out, engine.transition_maximum_per_part);
    out += "},\n";
    append_key(out, "    ", "ambient");
    out += "{\"maximum_frames_per_second\": ";
    append_number(out, engine.ambient_maximum_frames_per_second);
    out += ", \"maximum_window_fraction\": ";
    append_number(out, engine.ambient_maximum_window_fraction);
    out += "},\n";
    append_effects(out);
    out += "\n  },\n";
}

// The states of a type, an item system or a part (see tables.schema.state_at).
void append_states(std::string& out, std::uint64_t type, std::uint64_t system, std::uint64_t part,
                   std::uint64_t count) {
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_utf8_view name{};
        require_ok(tables.schema.state_at(type, system, part, index, &name), "state_at");
        if (index > 0U) out += ", ";
        append_json_string(out, text_of(name));
    }
    out += ']';
}

void append_custom_states(std::string& out, std::uint64_t type, std::uint64_t system, std::uint64_t count) {
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_utf8_view name{};
        require_ok(tables.schema.custom_state_at(type, system, index, &name), "custom_state_at");
        if (index > 0U) out += ", ";
        append_json_string(out, text_of(name));
    }
    out += ']';
}

void append_parts(std::string& out, std::string_view indent, std::uint64_t type, std::uint64_t system,
                  std::uint64_t count) {
    out += "[\n";
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_schema_part part{};
        part.struct_size = sizeof(part);
        require_ok(tables.schema.part_at(type, system, index, &part), "part_at");
        out += indent;
        out += "  {\"name\": ";
        append_json_string(out, text_of(part.name));
        out += ", \"role\": ";
        append_json_string(out, text_of(part.role));
        out += ", \"composed_from\": ";
        if (part.composed_from.size == 0U) {
            out += "null";
        } else {
            append_json_string(out, text_of(part.composed_from));
        }
        out += ", \"states\": ";
        append_states(out, type, system, index, part.state_count);
        out += index + 1U < count ? "},\n" : "}\n";
    }
    out += indent;
    out += ']';
}

void append_item_systems(std::string& out, std::uint64_t type, std::uint64_t count) {
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_schema_item_system system{};
        system.struct_size = sizeof(system);
        require_ok(tables.schema.item_system_at(type, index, &system), "item_system_at");
        out += index == 0U ? "\n" : ",\n";
        out += "        {\n";
        append_key(out, "          ", "name");
        append_json_string(out, text_of(system.name));
        out += ",\n";
        append_key(out, "          ", "parts");
        append_parts(out, "          ", type, index, system.part_count);
        out += ",\n";
        append_key(out, "          ", "states");
        append_states(out, type, index, GF_SCHEMA_NONE, system.state_count);
        out += ",\n";
        append_key(out, "          ", "custom_states");
        append_custom_states(out, type, index, system.custom_state_count);
        out += "\n        }";
    }
    out += count == 0U ? "]" : "\n      ]";
}

// Which of a type's property lists append_properties writes: its own
// (property_at, choice_at) or the ones it provides to its children
// (child_property_at, child_choice_at, since 1.1).
enum class PropertyList : std::uint8_t { own, provided_to_children };

void append_properties(std::string& out, std::uint64_t type, std::uint64_t count, PropertyList list) {
    decltype(gf_schema_table::property_at) read_property = tables.schema.property_at;
    decltype(gf_schema_table::choice_at) read_choice = tables.schema.choice_at;
    if (list == PropertyList::provided_to_children) {
        read_property = tables.schema.child_property_at;
        read_choice = tables.schema.child_choice_at;
    }
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_schema_property property{};
        property.struct_size = sizeof(property);
        require_ok(read_property(type, index, &property), "property_at");
        out += index == 0U ? "\n" : ",\n";
        out += "        {\"name\": ";
        append_json_string(out, text_of(property.name));
        out += ", \"kind\": ";
        append_json_string(out, value_kind_name(property.kind));
        out += ", \"category\": ";
        append_json_string(out, text_of(property.category));
        out += ", \"enumeration\": ";
        if (property.enumeration.size == 0U) {
            out += "null";
        } else {
            append_json_string(out, text_of(property.enumeration));
        }
        out += ", \"choices\": [";
        for (std::uint64_t choice = 0; choice < property.choice_count; ++choice) {
            gf_schema_choice value{};
            value.struct_size = sizeof(value);
            require_ok(read_choice(type, index, choice, &value), "choice_at");
            if (choice > 0U) out += ", ";
            append_json_string(out, text_of(value.name));
        }
        out += "]";
        out += ", \"readable\": ";
        append_bool(out, (property.flags & GF_SCHEMA_PROPERTY_FLAGS_READABLE) != 0U);
        out += ", \"writable\": ";
        append_bool(out, (property.flags & GF_SCHEMA_PROPERTY_FLAGS_WRITABLE) != 0U);
        out += ", \"bindable\": ";
        append_bool(out, (property.flags & GF_SCHEMA_PROPERTY_FLAGS_BINDABLE) != 0U);
        out += ", \"nullable\": ";
        append_bool(out, (property.flags & GF_SCHEMA_PROPERTY_FLAGS_NULLABLE) != 0U);
        out += ", \"serialization\": ";
        append_json_string(out, serialization_name(property.serialization));
        out += "}";
    }
    out += count == 0U ? "]" : "\n      ]";
}

void append_interactions(std::string& out, std::uint64_t type, std::uint64_t count) {
    out += '[';
    for (std::uint64_t index = 0; index < count; ++index) {
        gf_schema_interaction interaction{};
        interaction.struct_size = sizeof(interaction);
        require_ok(tables.schema.interaction_at(type, index, &interaction), "interaction_at");
        out += index == 0U ? "\n" : ",\n";
        out += "        {\"name\": ";
        append_json_string(out, text_of(interaction.name));
        out += ", \"payload\": ";
        append_json_string(out, text_of(interaction.payload));
        out += ", \"mutable_payload\": ";
        append_bool(out, interaction.mutable_payload == 1U);
        out += "}";
    }
    out += count == 0U ? "]" : "\n      ]";
}

std::string_view kind_name(gf_schema_kind kind) {
    switch (kind) {
    case GF_SCHEMA_KIND_CONTROL: return "control";
    case GF_SCHEMA_KIND_COMPONENT: return "component";
    case GF_SCHEMA_KIND_SERVICE: return "service";
    default: return "unknown";
    }
}

std::string render_schema() {
    std::string out;
    out += "{\n";
    out += "  \"schema\": 2,\n";
    out += "  \"notes\": [\n";
    out += "    \"Generated by gui_forms_gallery_pages_tests from the run-time registry "
           "(stage T1): set GUI_FORMS_CONTROL_SCHEMA_WRITE=1 and run that test to rewrite it. "
           "Do not edit.\",\n";
    out += "    \"Controls list their parts, states, item systems, properties and interactions; "
           "components list their interactions. Every name is stored in snake_case; CSS-family "
           "documents write it in kebab-case and displays in PascalCase.\"\n";
    out += "  ],\n";
    append_vocabulary(out);
    append_engine(out);
    std::uint64_t type_count = 0U;
    require_ok(tables.schema.type_count(&type_count), "type_count");
    out += "  \"types\": [";
    for (std::uint64_t index = 0; index < type_count; ++index) {
        gf_schema_type type{};
        type.struct_size = sizeof(type);
        require_ok(tables.schema.type_at(index, &type), "type_at");
        out += index == 0U ? "\n" : ",\n";
        out += "    {\n";
        append_key(out, "      ", "id");
        append_json_string(out, text_of(type.id));
        out += ",\n";
        append_key(out, "      ", "type");
        append_json_string(out, text_of(type.type_name));
        out += ",\n";
        append_key(out, "      ", "kind");
        append_json_string(out, kind_name(type.kind));
        if (type.kind == GF_SCHEMA_KIND_CONTROL) {
            out += ",\n";
            append_key(out, "      ", "parts");
            append_parts(out, "      ", index, GF_SCHEMA_NONE, type.part_count);
            out += ",\n";
            append_key(out, "      ", "states");
            append_states(out, index, GF_SCHEMA_NONE, GF_SCHEMA_NONE, type.state_count);
            out += ",\n";
            append_key(out, "      ", "custom_states");
            append_custom_states(out, index, GF_SCHEMA_NONE, type.custom_state_count);
            out += ",\n";
            append_key(out, "      ", "item_systems");
            append_item_systems(out, index, type.item_system_count);
            out += ",\n";
            append_key(out, "      ", "properties");
            append_properties(out, index, type.property_count, PropertyList::own);
            // Exported only when there are some, as the C++ export does.
            std::uint64_t child_count = 0U;
            require_ok(tables.schema.child_property_count(index, &child_count), "child_property_count");
            if (child_count != 0U) {
                out += ",\n";
                append_key(out, "      ", "child_properties");
                append_properties(out, index, child_count, PropertyList::provided_to_children);
            }
        }
        out += ",\n";
        append_key(out, "      ", "interactions");
        append_interactions(out, index, type.interaction_count);
        out += "\n    }";
    }
    out += "\n  ]\n}\n";
    return out;
}

// The committed file's text without carriage returns.
std::string read_committed(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::string("cannot read ") + path);
    }
    const std::string raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string text;
    text.reserve(raw.size());
    for (const char character : raw) {
        if (character != '\r') text += character;
    }
    return text;
}

// The first line on which two texts differ, counted from one.
std::size_t first_different_line(std::string_view left, std::string_view right) {
    std::size_t line = 1U;
    const std::size_t shorter = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < shorter; ++index) {
        if (left[index] != right[index]) return line;
        if (left[index] == '\n') ++line;
    }
    return line;
}

int run(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: gf_abi_schema_export <control-tables.schema.json> [<written.json>]\n";
        return 2;
    }
    tables.schema.struct_size = sizeof(tables.schema);
    require_ok(gf_abi_get_table(GF_TABLE_SCHEMA, GF_ABI_MAJOR, GF_ABI_MINOR, &tables.schema, sizeof(tables.schema)),
               "gf_abi_get_table");
    const std::string written = render_schema();
    if (argc > 2) {
        std::ofstream file(argv[2], std::ios::binary | std::ios::trunc);
        file << written;
    }
    const std::string committed = read_committed(argv[1]);
    std::uint64_t type_count = 0U;
    require_ok(tables.schema.type_count(&type_count), "type_count");
    if (committed != written) {
        std::cerr << "the schema read through the C ABI differs from " << argv[1] << " from line "
                  << first_different_line(committed, written) << "\n";
        return 1;
    }
    std::cout << "C ABI schema: " << type_count << " types, " << written.size()
              << " bytes, equal to the committed control schema\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "gf_abi_schema_export: " << error.what() << "\n";
        return 1;
    }
}
