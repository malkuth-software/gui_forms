#include "support.hpp"

#include "gui_forms/components/about_dialog/about_dialog.hpp"
#include "gui_forms/controls/callout.hpp"
#include "gui_forms/controls/help_book.hpp"
#include "gui_forms/controls/help_book_system.hpp"
#include "gui_forms/controls/help_book_viewer.hpp"
#include "gui_forms/controls/gauge_base.hpp"
#include "gui_forms/controls/modal_layer.hpp"
#include "gui_forms/controls/wizard_page.hpp"
#include "gui_forms/controls/sparkline.hpp"
#include "gui_forms/controls/plot_annotation_item.hpp"
#include "gui_forms/controls/plot_axis_item.hpp"
#include "gui_forms/controls/plot_series_item.hpp"
#include "gui_forms/controls/plot_view.hpp"
#include "gui_forms/controls/value_bar.hpp"

#include "gui_forms/controls/toast.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/schema.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace gallery_pages_tests {
namespace {

// The control schema (stage T1): every registry type's parts, states, item
// systems, properties and interactions, read from a laid-out Gallery and
// from directly constructed components, written as one JSON document. The
// committed copy is manifests/control-schema.json; CMake passes its path in
// GUI_FORMS_CONTROL_SCHEMA. The test fails when the export and the
// committed copy differ, and GUI_FORMS_CONTROL_SCHEMA_WRITE=1 rewrites the
// committed copy instead.

// One registry type as the export records it. Controls carry an anatomy and
// properties; components carry interactions only.
struct TypeRecord final {
    const ControlAnatomy* anatomy{};
    std::vector<PropertyDescriptor> properties;
    // The properties the type provides to its children; exported only when
    // there are some.
    std::vector<PropertyDescriptor> child_properties;
    std::vector<InteractionDeclaration> interactions;
    bool seen{};
};

std::string_view value_kind_name(BindingValueKind kind) {
    switch (kind) {
    case BindingValueKind::null: return "null";
    case BindingValueKind::boolean: return "boolean";
    case BindingValueKind::signed_integer: return "signed_integer";
    case BindingValueKind::unsigned_integer: return "unsigned_integer";
    case BindingValueKind::number: return "number";
    case BindingValueKind::text: return "text";
    case BindingValueKind::point: return "point";
    case BindingValueKind::size: return "size";
    case BindingValueKind::rectangle: return "rectangle";
    case BindingValueKind::insets: return "insets";
    case BindingValueKind::color: return "color";
    case BindingValueKind::font: return "font";
    case BindingValueKind::image: return "image";
    case BindingValueKind::enumeration: return "enumeration";
    case BindingValueKind::object: return "object";
    case BindingValueKind::collection: return "collection";
    }
    return "unknown";
}

std::string_view serialization_name(PropertySerializationVisibility visibility) {
    switch (visibility) {
    case PropertySerializationVisibility::hidden: return "hidden";
    case PropertySerializationVisibility::visible: return "visible";
    case PropertySerializationVisibility::content: return "content";
    }
    return "unknown";
}

// Appends `text` as a JSON string literal.
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

void append_state_list(std::string& out, std::span<const AnatomyState> states) {
    out += '[';
    for (std::size_t index = 0; index < states.size(); ++index) {
        if (index > 0U) out += ", ";
        append_json_string(out, anatomy_state_name(states[index]));
    }
    out += ']';
}

void append_name_list(std::string& out, std::span<const std::string_view> names) {
    out += '[';
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0U) out += ", ";
        append_json_string(out, names[index]);
    }
    out += ']';
}

// The atom vocabulary (design language 8.5 and 12.7): every part role,
// state, admitted custom state, mark, engine input, value type, recipe
// family, semantic role, semantic action and semantic node field, in enum
// order. `none` is the absence of a mark, not a mark, and is left out.
// Values and node fields have no enum; they are listed in the order they
// were admitted.
void append_vocabulary(std::string& out) {
    std::vector<std::string_view> names;
    out += "  \"vocabulary\": {\n";
    names.clear();
    for (std::size_t index = 0; index < part_role_count; ++index) {
        names.push_back(part_role_name(static_cast<PartRole>(index)));
    }
    append_key(out, "    ", "part_roles");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (std::size_t index = 0; index < anatomy_state_count; ++index) {
        names.push_back(anatomy_state_name(static_cast<AnatomyState>(index)));
    }
    append_key(out, "    ", "states");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (std::size_t index = 0; index < admitted_custom_states.size(); ++index) {
        names.push_back(admitted_custom_states[index].name);
    }
    append_key(out, "    ", "custom_states");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (std::size_t index = 1; index < part_mark_count; ++index) {
        names.push_back(part_mark_name(static_cast<PartMark>(index)));
    }
    append_key(out, "    ", "marks");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (std::size_t index = 0; index < engine_input_count; ++index) {
        names.push_back(engine_input_name(static_cast<EngineInput>(index)));
    }
    append_key(out, "    ", "engine_inputs");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (const std::string_view value : part_value_names) {
        names.push_back(value);
    }
    append_key(out, "    ", "values");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (std::size_t index = 0; index < control_visual_role_count; ++index) {
        names.push_back(control_visual_role_name(static_cast<ControlVisualRole>(index)));
    }
    append_key(out, "    ", "families");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (std::size_t index = 0; index < semantic_role_count; ++index) {
        names.push_back(semantic_role_name(static_cast<SemanticRole>(index)));
    }
    append_key(out, "    ", "semantic_roles");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (std::size_t index = 0; index < semantic_action_count; ++index) {
        names.push_back(semantic_action_name(static_cast<SemanticAction>(index)));
    }
    append_key(out, "    ", "semantic_actions");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    for (const SemanticState state : semantic_states) {
        names.push_back(semantic_state_name(state));
    }
    append_key(out, "    ", "semantic_states");
    append_name_list(out, names);
    out += ",\n";
    names.clear();
    const std::span<const char* const> fields = semantic_field_names();
    for (std::size_t index = 0; index < fields.size(); ++index) {
        names.push_back(fields[index]);
    }
    append_key(out, "    ", "semantic_fields");
    append_name_list(out, names);
    out += "\n  },\n";
}

// A number in its shortest round-trip form, which std::to_chars gives the
// same on every host.
void append_number(std::string& out, double value) {
    std::array<char, 32> text{};
    const std::to_chars_result written = std::to_chars(text.data(), text.data() + text.size(), value);
    out.append(text.data(), static_cast<std::size_t>(written.ptr - text.data()));
}

void append_unsigned(std::string& out, std::uint64_t value) {
    out += std::to_string(value);
}

void append_version(std::string& out, EngineVersion version) {
    const std::string text = std::to_string(version.major) + "." + std::to_string(version.minor);
    append_json_string(out, text);
}

// A colour as #rrggbbaa.
void append_colour(std::string& out, Color colour) {
    constexpr std::string_view digits = "0123456789abcdef";
    const std::array<std::uint8_t, 4> channels{colour.red, colour.green, colour.blue, colour.alpha};
    std::string text = "#";
    for (const std::uint8_t channel : channels) {
        text += digits[channel >> 4U];
        text += digits[channel & 0x0FU];
    }
    append_json_string(out, text);
}

void append_parameters(std::string& out, std::span<const PrimitiveParameterDeclaration> parameters) {
    out += '[';
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const PrimitiveParameterDeclaration& parameter = parameters[index];
        out += index == 0U ? "\n" : ",\n";
        out += "          {";
        append_json_string(out, "name");
        out += ": ";
        append_json_string(out, parameter.name);
        out += ", ";
        append_json_string(out, "kind");
        out += ": ";
        if (parameter.kind == PrimitiveParameterKind::number) {
            append_json_string(out, "number");
            out += ", ";
            append_json_string(out, "minimum");
            out += ": ";
            append_number(out, parameter.minimum);
            out += ", ";
            append_json_string(out, "maximum");
            out += ": ";
            append_number(out, parameter.maximum);
            out += ", ";
            append_json_string(out, "default");
            out += ": ";
            append_number(out, parameter.default_number);
        } else if (parameter.kind == PrimitiveParameterKind::choice) {
            append_json_string(out, "choice");
            out += ", ";
            append_json_string(out, "choices");
            out += ": ";
            append_name_list(out, parameter.choices);
            out += ", ";
            append_json_string(out, "default");
            out += ": ";
            append_json_string(out, parameter.choices[static_cast<std::size_t>(parameter.default_number)]);
        } else {
            append_json_string(out, "colour");
            out += ", ";
            append_json_string(out, "default");
            out += ": ";
            append_colour(out, parameter.default_colour);
        }
        out += ", ";
        append_json_string(out, "animated");
        out += ": ";
        append_bool(out, parameter.animated);
        out += '}';
    }
    out += parameters.empty() ? "]" : "\n        ]";
}

// The engine (T2 plan, E4): the version this build implements, the
// cascade's levels, the budgets of GIVEN F1-1, and every registered
// primitive with its atoms, parameters, cost and outsets, in registration
// order.
void append_engine(std::string& out) {
    out += "  \"engine\": {\n";
    append_key(out, "    ", "version");
    append_version(out, current_engine_version);
    out += ",\n";
    std::vector<std::string_view> names;
    for (std::size_t index = 0; index < static_cast<std::size_t>(CascadeLevel::count); ++index) {
        names.push_back(cascade_level_name(static_cast<CascadeLevel>(index)));
    }
    append_key(out, "    ", "cascade_levels");
    append_name_list(out, names);
    out += ",\n";
    append_key(out, "    ", "budgets");
    out += '{';
    append_json_string(out, "operations_per_device_pixel");
    out += ": ";
    append_unsigned(out, maximum_operations_per_device_pixel);
    out += ", ";
    append_json_string(out, "animated_operations_per_frame");
    out += ": ";
    append_unsigned(out, maximum_animated_operations_per_frame);
    out += "},\n";
    append_key(out, "    ", "primitives");
    out += '[';
    const std::span<const EnginePrimitive* const> primitives = builtin_primitive_registry().primitives();
    for (std::size_t index = 0; index < primitives.size(); ++index) {
        const PrimitiveDescriptor& descriptor = (*primitives[index]).descriptor();
        out += index == 0U ? "\n" : ",\n";
        out += "      {\n";
        append_key(out, "        ", "name");
        append_json_string(out, descriptor.name);
        out += ",\n";
        append_key(out, "        ", "introduced");
        append_version(out, descriptor.introduced);
        out += ",\n";
        names.clear();
        for (const PartRole role : descriptor.roles) names.push_back(part_role_name(role));
        append_key(out, "        ", "roles");
        append_name_list(out, names);
        out += ",\n";
        append_key(out, "        ", "states_read");
        append_state_list(out, descriptor.states_read);
        out += ",\n";
        names.clear();
        for (const EngineInput input : descriptor.inputs_read) names.push_back(engine_input_name(input));
        append_key(out, "        ", "inputs_read");
        append_name_list(out, names);
        out += ",\n";
        append_key(out, "        ", "cost");
        out += '{';
        append_json_string(out, "per_device_pixel");
        out += ": ";
        append_unsigned(out, descriptor.cost.per_device_pixel);
        out += ", ";
        append_json_string(out, "fixed");
        out += ": ";
        append_unsigned(out, descriptor.cost.fixed);
        out += ", ";
        append_json_string(out, "preparation_per_device_pixel");
        out += ": ";
        append_unsigned(out, descriptor.cost.preparation_per_device_pixel);
        out += "},\n";
        append_key(out, "        ", "outsets");
        out += '[';
        append_number(out, descriptor.outsets.left);
        out += ", ";
        append_number(out, descriptor.outsets.top);
        out += ", ";
        append_number(out, descriptor.outsets.right);
        out += ", ";
        append_number(out, descriptor.outsets.bottom);
        out += "],\n";
        append_key(out, "        ", "parameters");
        append_parameters(out, descriptor.parameters);
        out += "\n      }";
    }
    out += primitives.empty() ? "]" : "\n    ]";
    out += ",\n";
    // Bounded expressions (E5): the budget a program is checked against at
    // load and the operations, in enum order.
    append_key(out, "    ", "expressions");
    out += '{';
    append_json_string(out, "maximum_operations");
    out += ": ";
    append_unsigned(out, maximum_expression_operations);
    out += ", ";
    append_json_string(out, "maximum_stack");
    out += ": ";
    append_unsigned(out, maximum_expression_stack);
    out += ", ";
    append_json_string(out, "operations");
    out += ": ";
    names.clear();
    for (std::size_t index = 0; index < expression_operation_count; ++index) {
        names.push_back(expression_operation_name(static_cast<ExpressionOperation>(index)));
    }
    append_name_list(out, names);
    out += "},\n";
    // Published values (E6): how many a theme may name.
    append_key(out, "    ", "published_values");
    out += '{';
    append_json_string(out, "maximum");
    out += ": ";
    append_unsigned(out, maximum_published_values);
    out += "},\n";
    // Presentational state machines (E7): the engine properties besides the
    // primitives' animated parameters, and the bounds a machine is checked
    // against at load.
    append_key(out, "    ", "transitions");
    out += '{';
    append_json_string(out, "engine_properties");
    out += ": ";
    names.clear();
    names.push_back(opacity_property);
    names.push_back(scale_property);
    names.push_back(face_property);
    names.push_back(translate_x_property);
    names.push_back(translate_y_property);
    append_name_list(out, names);
    out += ", ";
    append_json_string(out, "maximum_states");
    out += ": ";
    append_unsigned(out, maximum_presentation_states);
    out += ", ";
    append_json_string(out, "maximum_per_part");
    out += ": ";
    append_unsigned(out, maximum_part_transitions);
    out += "},\n";
    // Ambient effects (E7): the frame cap and the everyday area cap.
    append_key(out, "    ", "ambient");
    out += '{';
    append_json_string(out, "maximum_frames_per_second");
    out += ": ";
    append_number(out, maximum_ambient_frames_per_second);
    out += ", ";
    append_json_string(out, "maximum_window_fraction");
    out += ": ";
    append_number(out, maximum_ambient_window_fraction);
    out += "},\n";
    // Effect programs (design note section 7), one per line in registration
    // order. Whether the plugin verified each depends on the build, so the
    // manifest leaves it out; the ABI's effect_at reports it.
    append_key(out, "    ", "effects");
    out += '[';
    const EffectRegistry& effects = built_in_effect_registry();
    for (std::size_t index = 0; index < effects.count(); ++index) {
        const EffectEntry& entry = effects.at(index);
        const EffectDeclaration& declaration = *entry.declaration;
        out += index == 0U ? "\n      {" : ",\n      {";
        append_json_string(out, "name");
        out += ": ";
        append_json_string(out, declaration.name);
        out += ", ";
        append_json_string(out, "parent");
        out += ": ";
        append_json_string(out, entry.parent);
        out += ", ";
        append_json_string(out, "time_model");
        out += ": ";
        append_json_string(out, effect_time_model_name(declaration.time_model));
        out += ", ";
        append_json_string(out, "engine_version");
        out += ": ";
        append_unsigned(out, declaration.engine_version);
        out += ", ";
        append_json_string(out, "cost");
        out += ": {";
        append_json_string(out, "fixed");
        out += ": ";
        append_unsigned(out, declaration.cost.fixed);
        out += ", ";
        append_json_string(out, "per_device_pixel");
        out += ": ";
        append_unsigned(out, declaration.cost.per_device_pixel);
        out += "}, ";
        append_json_string(out, "setup_cost");
        out += ": ";
        append_unsigned(out, declaration.setup_cost);
        out += ", ";
        append_json_string(out, "setup_on_geometry");
        out += declaration.setup_on_geometry ? ": true, " : ": false, ";
        append_json_string(out, "state_budget");
        out += ": ";
        append_unsigned(out, declaration.state_budget);
        out += ", ";
        append_json_string(out, "frames_per_second");
        out += ": ";
        append_number(out, declaration.frames_per_second);
        out += ", ";
        append_json_string(out, "outsets");
        out += ": [";
        append_number(out, declaration.outsets.left);
        out += ", ";
        append_number(out, declaration.outsets.top);
        out += ", ";
        append_number(out, declaration.outsets.right);
        out += ", ";
        append_number(out, declaration.outsets.bottom);
        out += "], ";
        append_json_string(out, "input");
        out += ": ";
        append_json_string(out, effect_input_name(declaration.input));
        out += '}';
    }
    out += effects.count() == 0U ? "]" : "\n    ]";
    out += "\n  },\n";
}

void append_custom_state_list(std::string& out, std::span<const CustomStateDeclaration> states) {
    out += '[';
    for (std::size_t index = 0; index < states.size(); ++index) {
        if (index > 0U) out += ", ";
        append_json_string(out, states[index].name);
    }
    out += ']';
}

// One part per line: name, role, the system it reuses and its own states.
void append_parts(std::string& out, std::string_view indent,
                  std::span<const PartDeclaration> parts) {
    out += "[\n";
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const PartDeclaration& part = parts[index];
        out += indent;
        out += "  {\"name\": ";
        append_json_string(out, part.name);
        out += ", \"role\": ";
        append_json_string(out, part_role_name(part.role));
        out += ", \"composed_from\": ";
        if (part.composed_from.empty()) {
            out += "null";
        } else {
            append_json_string(out, part.composed_from);
        }
        out += ", \"states\": ";
        append_state_list(out, part.states);
        out += index + 1U < parts.size() ? "},\n" : "}\n";
    }
    out += indent;
    out += ']';
}

void append_item_systems(std::string& out, std::span<const ItemSystemDeclaration> systems) {
    out += "[";
    for (std::size_t index = 0; index < systems.size(); ++index) {
        const ItemSystemDeclaration& system = systems[index];
        out += index == 0U ? "\n" : ",\n";
        out += "        {\n";
        append_key(out, "          ", "name");
        append_json_string(out, system.name);
        out += ",\n";
        append_key(out, "          ", "parts");
        append_parts(out, "          ", system.parts);
        out += ",\n";
        append_key(out, "          ", "states");
        append_state_list(out, system.states);
        out += ",\n";
        append_key(out, "          ", "custom_states");
        append_custom_state_list(out, system.custom_states);
        out += "\n        }";
    }
    out += systems.empty() ? "]" : "\n      ]";
}

void append_properties(std::string& out, std::span<const PropertyDescriptor> properties) {
    out += "[";
    for (std::size_t index = 0; index < properties.size(); ++index) {
        const PropertyDescriptor& property = properties[index];
        out += index == 0U ? "\n" : ",\n";
        out += "        {\"name\": ";
        append_json_string(out, property.name);
        out += ", \"kind\": ";
        append_json_string(out, value_kind_name(property.kind));
        out += ", \"category\": ";
        append_json_string(out, property.category);
        out += ", \"enumeration\": ";
        if (property.enumeration == nullptr) {
            out += "null";
        } else {
            append_json_string(out, (*property.enumeration).type_name);
        }
        // The values a document may write for an enumeration.
        out += ", \"choices\": [";
        if (property.enumeration != nullptr) {
            const std::vector<PropertyEnumChoice>& choices = (*property.enumeration).choices;
            for (std::size_t choice = 0; choice < choices.size(); ++choice) {
                if (choice > 0U) out += ", ";
                append_json_string(out, choices[choice].name);
            }
        }
        out += "]";
        out += ", \"readable\": ";
        append_bool(out, property.readable);
        out += ", \"writable\": ";
        append_bool(out, property.writable);
        out += ", \"bindable\": ";
        append_bool(out, property.bindable);
        out += ", \"nullable\": ";
        append_bool(out, property.nullable);
        out += ", \"serialization\": ";
        append_json_string(out, serialization_name(property.serialization_visibility));
        out += "}";
    }
    out += properties.empty() ? "]" : "\n      ]";
}

void append_interactions(std::string& out, std::span<const InteractionDeclaration> interactions) {
    out += "[";
    for (std::size_t index = 0; index < interactions.size(); ++index) {
        const InteractionDeclaration& interaction = interactions[index];
        out += index == 0U ? "\n" : ",\n";
        out += "        {\"name\": ";
        append_json_string(out, interaction.name);
        out += ", \"payload\": ";
        append_json_string(out, interaction.payload);
        out += ", \"mutable_payload\": ";
        append_bool(out, interaction.mutable_payload);
        out += "}";
    }
    out += interactions.empty() ? "]" : "\n      ]";
}

// Keeps only the entries of `kept` whose names `other` also has, in
// `kept`'s order. A Gallery-local subclass reports its base type's anatomy
// id but may add properties or events; the type's own schema is what every
// instance shares.
void keep_common_properties(std::vector<PropertyDescriptor>& kept,
                            std::span<const PropertyDescriptor> other) {
    std::vector<PropertyDescriptor> common;
    common.reserve(kept.size());
    for (std::size_t index = 0; index < kept.size(); ++index) {
        bool shared = false;
        for (std::size_t position = 0; position < other.size(); ++position) {
            if (other[position].name == kept[index].name) shared = true;
        }
        if (shared) common.push_back(kept[index]);
    }
    kept.swap(common);
}

void keep_common_interactions(std::vector<InteractionDeclaration>& kept,
                              std::span<const InteractionDeclaration> other) {
    std::vector<InteractionDeclaration> common;
    common.reserve(kept.size());
    for (std::size_t index = 0; index < kept.size(); ++index) {
        if (find_interaction(other, kept[index].name) < other.size()) {
            common.push_back(kept[index]);
        }
    }
    kept.swap(common);
}

std::size_t catalog_position(std::string_view id) {
    for (std::size_t index = 0; index < gallery::control_catalog.size(); ++index) {
        if (gallery::control_catalog[index].id == id) return index;
    }
    return gallery::control_catalog.size();
}

// GaugeBase's constructor is protected: a subclass that adds nothing stands
// for it.
class SchemaGauge final : public GaugeBase {
public:
    explicit SchemaGauge(StableId stable_id) : GaugeBase(std::move(stable_id)) {}
};

void record_control(const Control& control, std::vector<TypeRecord>& records) {
    const ControlAnatomy& anatomy = control.anatomy();
    const std::size_t position = catalog_position(anatomy.control_id);
    // Gallery-private types publish the base anatomy ("control").
    if (position >= records.size()) return;
    TypeRecord& record = records[position];
    const std::vector<PropertyDescriptor> properties = control.property_descriptors();
    const std::vector<InteractionDeclaration> interactions = control.interactions();
    if (!record.seen) {
        record.anatomy = &anatomy;
        record.properties = properties;
        record.child_properties = control.child_property_descriptors();
        record.interactions = interactions;
        record.seen = true;
        return;
    }
    keep_common_properties(record.properties, properties);
    keep_common_interactions(record.interactions, interactions);
}

// A control with no anatomy of its own (it publishes the base anatomy,
// "control"), recorded under its registry id with that anatomy, as the C ABI
// describes it.
void record_control_as(std::string_view id, const Control& control, std::vector<TypeRecord>& records) {
    const std::size_t position = catalog_position(id);
    if (position >= records.size()) return;
    TypeRecord& record = records[position];
    const std::vector<PropertyDescriptor> properties = control.property_descriptors();
    const std::vector<InteractionDeclaration> interactions = control.interactions();
    if (!record.seen) {
        record.anatomy = &control.anatomy();
        record.properties = properties;
        record.child_properties = control.child_property_descriptors();
        record.interactions = interactions;
        record.seen = true;
        return;
    }
    keep_common_properties(record.properties, properties);
    keep_common_interactions(record.interactions, interactions);
}

void record_tree(const Control::Ptr& root, std::vector<TypeRecord>& records) {
    std::vector<Control::Ptr> pending{root};
    while (!pending.empty()) {
        const Control::Ptr control = pending.back();
        pending.pop_back();
        record_control(*control, records);
        const std::span<const Control::Ptr> children = (*control).children();
        for (std::size_t index = 0; index < children.size(); ++index) {
            pending.push_back(children[index]);
        }
    }
}

void record_component(std::string_view id, const Component& component,
                      std::vector<TypeRecord>& records) {
    const std::size_t position = catalog_position(id);
    require(position < records.size(), "every exported component is in the registry");
    TypeRecord& record = records[position];
    record.interactions = component.interactions();
    record.seen = true;
}

std::string render_schema(std::span<const TypeRecord> records) {
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
    out += "  \"types\": [";
    for (std::size_t index = 0; index < records.size(); ++index) {
        const TypeRecord& record = records[index];
        const gallery::CatalogEntry& entry = gallery::control_catalog[index];
        out += index == 0U ? "\n" : ",\n";
        out += "    {\n";
        append_key(out, "      ", "id");
        append_json_string(out, entry.id);
        out += ",\n";
        append_key(out, "      ", "type");
        append_json_string(out, entry.type);
        out += ",\n";
        append_key(out, "      ", "kind");
        if (record.anatomy != nullptr) {
            append_json_string(out, "control");
        } else if (record.seen) {
            append_json_string(out, "component");
        } else {
            append_json_string(out, "service");
        }
        if (record.anatomy != nullptr) {
            const ControlAnatomy& anatomy = *record.anatomy;
            out += ",\n";
            append_key(out, "      ", "parts");
            append_parts(out, "      ", anatomy.parts);
            out += ",\n";
            append_key(out, "      ", "states");
            append_state_list(out, anatomy.states);
            out += ",\n";
            append_key(out, "      ", "custom_states");
            append_custom_state_list(out, anatomy.custom_states);
            out += ",\n";
            append_key(out, "      ", "item_systems");
            append_item_systems(out, anatomy.item_systems);
            out += ",\n";
            append_key(out, "      ", "properties");
            append_properties(out, record.properties);
            if (!record.child_properties.empty()) {
                out += ",\n";
                append_key(out, "      ", "child_properties");
                append_properties(out, record.child_properties);
            }
        }
        out += ",\n";
        append_key(out, "      ", "interactions");
        append_interactions(out, record.interactions);
        out += "\n    }";
    }
    // The window (windows.create's object), after the registry's types as
    // the C ABI lists it: a component, written with its interactions only
    // (the C ABI's property_at lists its chrome properties).
    std::vector<InteractionDeclaration> window_interactions;
    Window::append_interactions(window_interactions);
    out += ",\n    {\n";
    append_key(out, "      ", "id");
    append_json_string(out, "window");
    out += ",\n";
    append_key(out, "      ", "type");
    append_json_string(out, "Window");
    out += ",\n";
    append_key(out, "      ", "kind");
    append_json_string(out, "component");
    out += ",\n";
    append_key(out, "      ", "interactions");
    append_interactions(out, window_interactions);
    out += "\n    }";
    out += "\n  ]\n}\n";
    return out;
}

// The committed file's text without carriage returns, which a Windows
// checkout may add.
std::string read_committed(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    const std::string raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string text;
    text.reserve(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index) {
        if (raw[index] != '\r') text += raw[index];
    }
    return text;
}

} // namespace

void test_schema_export() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    std::vector<TypeRecord> records(gallery::control_catalog.size());
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        require(gallery::select_gallery_page(window, page.id),
                "every Gallery page must be selectable");
        window.perform_layout();
        record_tree(window.root(), records);
    }
    // Registry controls the Gallery does not attach to its root tree, as in
    // the anatomy coverage test.
    const std::shared_ptr<ButtonBase> button_base =
        make_control<ButtonBase>(StableId("schema.button-base"), "Base");
    const std::shared_ptr<ScrollBar> scroll_bar =
        make_control<ScrollBar>(StableId("schema.scroll-bar"));
    const std::shared_ptr<AnchoredOverlayLayer> overlay_layer = make_control<AnchoredOverlayLayer>(
        StableId("schema.anchored-overlay-layer"), button_base, AnchoredOverlayPlacement{});
    const std::shared_ptr<RangeControl> range_control =
        make_control<RangeControl>(StableId("schema.range-control"));
    const std::shared_ptr<TaskDialog> task_dialog = make_control<TaskDialog>(StableId("schema.task-dialog"));
    record_control(*button_base, records);
    record_control(*scroll_bar, records);
    record_control(*overlay_layer, records);
    record_control(*range_control, records);
    // A dialog opens on demand, outside the root tree.
    const std::shared_ptr<FontDialog> font_dialog = make_control<FontDialog>(StableId("schema.font-dialog"));
    record_control(*font_dialog, records);
    record_control(*task_dialog, records);
    // A toast shows on demand, in an overlay outside the root tree.
    ToastContent toast_content{};
    toast_content.message = "Sample";
    const std::shared_ptr<Toast> toast = make_control<Toast>(StableId("schema.toast"), std::move(toast_content));
    record_control(*toast, records);
    // Types no Gallery page shows yet: the plots and instruments packages'
    // PlotView with its item children and Sparkline, and the ValueBar. The
    // items have no anatomy of their own: they are recorded by their ids
    // with the base anatomy, as the C ABI describes them.
    const std::shared_ptr<PlotView> plot_view = make_control<PlotView>(StableId("schema.plot-view"));
    const std::shared_ptr<PlotAxisItem> plot_axis = make_control<PlotAxisItem>(StableId("schema.plot-axis"));
    const std::shared_ptr<PlotSeriesItem> plot_series =
        make_control<PlotSeriesItem>(StableId("schema.plot-series"));
    const std::shared_ptr<PlotAnnotationItem> plot_annotation =
        make_control<PlotAnnotationItem>(StableId("schema.plot-annotation"));
    const std::shared_ptr<Sparkline> sparkline = make_control<Sparkline>(StableId("schema.sparkline"));
    const std::shared_ptr<ValueBar> value_bar = make_control<ValueBar>(StableId("schema.value-bar"));
    record_control(*plot_view, records);
    record_control_as("plot_axis_item", *plot_axis, records);
    record_control_as("plot_series_item", *plot_series, records);
    record_control_as("plot_annotation_item", *plot_annotation, records);
    record_control(*sparkline, records);
    record_control(*value_bar, records);
    // The final controls and the help system shown only inside another or
    // on demand. WizardPage and HelpBookViewer have no anatomy of their own
    // yet and GaugeBase is a base: they are recorded by their ids.
    const std::shared_ptr<SchemaGauge> gauge_base = make_control<SchemaGauge>(StableId("schema.gauge-base"));
    const std::shared_ptr<WizardPage> wizard_page = make_control<WizardPage>(StableId("schema.wizard-page"));
    const std::shared_ptr<Callout> callout = make_control<Callout>(StableId("schema.callout"));
    const std::shared_ptr<ModalLayer> modal_layer = make_control<ModalLayer>(StableId("schema.modal-layer"));
    const std::shared_ptr<HelpBookViewer> help_viewer =
        make_control<HelpBookViewer>(StableId("schema.help-viewer"));
    const std::shared_ptr<HelpBook> help_book = make_control<HelpBook>(StableId("schema.help-book"));
    record_control_as("gauge_base", *gauge_base, records);
    record_control_as("wizard_page", *wizard_page, records);
    record_control(*callout, records);
    record_control(*modal_layer, records);
    record_control_as("help_book_viewer", *help_viewer, records);
    record_control(*help_book, records);

    // Components, constructed directly.
    const std::shared_ptr<Command> command = std::make_shared<Command>();
    const std::shared_ptr<GalleryCommand> gallery_command = std::make_shared<GalleryCommand>(command);
    const std::shared_ptr<FontCommand> font_command = std::make_shared<FontCommand>(command);
    const std::shared_ptr<ColorCommand> color_command = std::make_shared<ColorCommand>(command);
    const std::shared_ptr<ContextMenu> context_menu =
        std::make_shared<ContextMenu>("schema.context-menu");
    const std::shared_ptr<ToolTip> tool_tip = std::make_shared<ToolTip>(window);
    const std::shared_ptr<RibbonToolTip> ribbon_tool_tip = std::make_shared<RibbonToolTip>(window);
    const std::shared_ptr<Timer> timer =
        std::make_shared<Timer>(window, std::chrono::milliseconds(1000));
    const std::shared_ptr<BackgroundWorker> background_worker = std::make_shared<BackgroundWorker>(window);
    const std::shared_ptr<ImageList> image_list = std::make_shared<ImageList>(window);
    const std::shared_ptr<NotifyIcon> notify_icon = std::make_shared<NotifyIcon>(window);
    const std::shared_ptr<OpenFileDialog> open_file_dialog = std::make_shared<OpenFileDialog>();
    const std::shared_ptr<SaveFileDialog> save_file_dialog = std::make_shared<SaveFileDialog>();
    const std::shared_ptr<ColorDialog> color_dialog = std::make_shared<ColorDialog>();
    const std::shared_ptr<FolderBrowserDialog> folder_browser_dialog = std::make_shared<FolderBrowserDialog>();
    const std::shared_ptr<ThreadExceptionDialog> thread_exception_dialog =
        std::make_shared<ThreadExceptionDialog>(StableId("schema.thread-exception"), ThreadExceptionReport{});
    const std::shared_ptr<ErrorProvider> error_provider = std::make_shared<ErrorProvider>(window);
    const std::shared_ptr<HelpProvider> help_provider = std::make_shared<HelpProvider>(window);
    const std::shared_ptr<SemanticFeedback> feedback = std::make_shared<SemanticFeedback>(window);
    const std::shared_ptr<BindingSource> binding_source = std::make_shared<BindingSource>(window);
    const std::shared_ptr<BindingContext> binding_context = std::make_shared<BindingContext>(window);
    const std::shared_ptr<DockWorkspace> dock_workspace = std::make_shared<DockWorkspace>("schema.workspace");
    const std::shared_ptr<Label> bound = make_control<Label>(StableId("schema.bound"));
    const std::shared_ptr<Binding> binding =
        (*bound).data_bindings().add("Text", binding_source, "Name");
    require(binding != nullptr, "a binding to an empty source is created");
    record_component("command", *command, records);
    record_component("gallery_command", *gallery_command, records);
    record_component("font_command", *font_command, records);
    record_component("color_command", *color_command, records);
    record_component("context_menu", *context_menu, records);
    record_component("tool_tip", *tool_tip, records);
    record_component("ribbon_tool_tip", *ribbon_tool_tip, records);
    record_component("timer", *timer, records);
    record_component("background_worker", *background_worker, records);
    record_component("notify_icon", *notify_icon, records);
    record_component("open_file_dialog", *open_file_dialog, records);
    record_component("save_file_dialog", *save_file_dialog, records);
    record_component("color_dialog", *color_dialog, records);
    record_component("folder_browser_dialog", *folder_browser_dialog, records);
    record_component("thread_exception_dialog", *thread_exception_dialog, records);
    record_component("image_list", *image_list, records);
    record_component("error_provider", *error_provider, records);
    record_component("help_provider", *help_provider, records);
    record_component("semantic_feedback", *feedback, records);
    record_component("binding", *binding, records);
    record_component("binding_source", *binding_source, records);
    record_component("binding_context", *binding_context, records);
    record_component("dock_workspace", *dock_workspace, records);
    const std::shared_ptr<AboutDialog> about_dialog =
        std::make_shared<AboutDialog>(StableId("schema.about"), AboutInfo{});
    const std::shared_ptr<HelpBookSystem> help_system = std::make_shared<HelpBookSystem>();
    record_component("about_dialog", *about_dialog, records);
    record_component("help_book_system", *help_system, records);

    std::size_t services = 0;
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (!records[index].seen) ++services;
    }
    require(services == 1U, "every registry type but the LiveSurface service is exported");
    const std::string schema = render_schema(records);

    const char* committed_path = std::getenv("GUI_FORMS_CONTROL_SCHEMA");
    if (committed_path == nullptr) {
        std::cout << "control schema: " << records.size()
                  << " types exported; GUI_FORMS_CONTROL_SCHEMA unset, not compared\n";
        return;
    }
    const char* write = std::getenv("GUI_FORMS_CONTROL_SCHEMA_WRITE");
    if (write != nullptr && std::string_view(write) == "1") {
        std::ofstream file(committed_path, std::ios::binary | std::ios::trunc);
        file << schema;
        require(static_cast<bool>(file), "the control schema must be writable");
        std::cout << "control schema: wrote " << committed_path << "\n";
        return;
    }
    if (read_committed(committed_path) != schema) {
        throw std::runtime_error(
            "manifests/control-schema.json is stale; run gui_forms_gallery_pages_tests with "
            "GUI_FORMS_CONTROL_SCHEMA_WRITE=1 to rewrite it");
    }
    std::cout << "control schema: " << records.size() << " types match the committed manifest\n";
}

} // namespace gallery_pages_tests
