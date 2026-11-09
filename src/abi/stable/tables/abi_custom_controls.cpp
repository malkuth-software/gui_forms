// GF_TABLE_CUSTOM_CONTROLS: controls the caller defines over a stock base.
// See gf_abi.h and ../custom/abi_custom_control.hpp.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"

#include "../custom/abi_custom_control.hpp"
#include "../schema/abi_create_with.hpp"
#include "../schema/abi_schema.hpp"
#include "../values/abi_values.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/schema.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity create_slot{GF_TABLE_CUSTOM_CONTROLS, 0U};
constexpr SlotIdentity notify_changed_slot{GF_TABLE_CUSTOM_CONTROLS, 1U};

constexpr std::uint32_t spec_size_1_0 = 80U;
constexpr std::uint32_t callbacks_size_1_0 = 48U;
constexpr std::uint32_t property_size_1_0 = 96U;
constexpr std::uint32_t paint_target_size_1_0 = 72U;
static_assert(sizeof(gf_custom_control_spec) == spec_size_1_0);
static_assert(sizeof(gf_custom_control_callbacks) == callbacks_size_1_0);
static_assert(sizeof(gf_custom_property) == property_size_1_0);
static_assert(sizeof(gf_paint_target) == paint_target_size_1_0);

constexpr std::size_t maximum_text_bytes = 256U;
// Descriptions are prose for the property grid; longer is not useful.
constexpr std::size_t maximum_description_bytes = 4096U;

constexpr gf_custom_property_flags known_property_flags =
    GF_CUSTOM_PROPERTY_READ_ONLY | GF_CUSTOM_PROPERTY_COMBINABLE | GF_CUSTOM_PROPERTY_CHANGE_NOTIFICATIONS;

// The property kind of a gf_value_kind a custom property may declare: every
// kind but NULL, OBJECT and COLLECTION. The numbering is shared
// (value_kind_of is its inverse).
bool custom_property_kind(gf_value_kind kind, BindingValueKind& converted) noexcept {
    if (kind == GF_VALUE_KIND_NULL || kind == GF_VALUE_KIND_OBJECT || kind >= GF_VALUE_KIND_COLLECTION) {
        return false;
    }
    converted = static_cast<BindingValueKind>(kind);
    return value_kind_of(converted) == kind;
}

gf_status read_bounded_text(gf_utf8_view view, std::size_t maximum, const char* refusal, std::string& text) {
    std::string_view read;
    const gf_status status = read_text(view, create_slot, read);
    if (status != GF_STATUS_OK) {
        return status;
    }
    if (read.size() > maximum) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, refusal);
    }
    text = std::string(read);
    return GF_STATUS_OK;
}

// One caller property, read into a definition; names distinct from the
// definitions read before it.
gf_status read_property(const gf_custom_property& property, const std::vector<CustomPropertyDefinition>& earlier,
                        CustomPropertyDefinition& definition) {
    if ((property.flags & ~known_property_flags) != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "unknown gf_custom_property_flags bits are set");
    }
    std::string name;
    const gf_status named =
        read_bounded_text(property.name, maximum_text_bytes, "a property's name has 1 to 256 bytes", name);
    if (named != GF_STATUS_OK) {
        return named;
    }
    if (name.empty()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a property's name has 1 to 256 bytes");
    }
    const std::string canonical = registered_name(name);
    if (!is_registered_name(canonical)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a property's name is not a registered name");
    }
    for (const CustomPropertyDefinition& previous : earlier) {
        if (previous.descriptor.name == canonical) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a custom control's property names are distinct");
        }
    }
    BindingValueKind kind = BindingValueKind::null;
    if (!custom_property_kind(property.kind, kind)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "a custom property's kind is a gf_value_kind other than NULL, OBJECT and COLLECTION");
    }
    PropertyDescriptor descriptor{};
    descriptor.name = canonical;
    descriptor.kind = kind;
    const gf_status categorized =
        read_bounded_text(property.category, maximum_text_bytes, "a property's category has at most 256 bytes",
                          descriptor.category);
    if (categorized != GF_STATUS_OK) {
        return categorized;
    }
    const gf_status described = read_bounded_text(property.description, maximum_description_bytes,
                                                  "a property's description has at most 4096 bytes",
                                                  descriptor.description);
    if (described != GF_STATUS_OK) {
        return described;
    }
    std::optional<PropertyEnumDescriptor> enumeration;
    const gf_status enumerated =
        read_enumeration(property.enumeration, property.choices, property.choice_count,
                         (property.flags & GF_CUSTOM_PROPERTY_COMBINABLE) != 0U, create_slot, enumeration);
    if (enumerated != GF_STATUS_OK) {
        return enumerated;
    }
    if (enumeration.has_value() != (kind == BindingValueKind::enumeration)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "an ENUMERATION property names its enumeration and choices; another kind has none");
    }
    if (enumeration) {
        descriptor.enumeration = std::make_shared<const PropertyEnumDescriptor>(std::move(*enumeration));
    }
    descriptor.writable = (property.flags & GF_CUSTOM_PROPERTY_READ_ONLY) == 0U;
    if (property.default_value != nullptr) {
        BindingValue read;
        const gf_status valued = read_value(property.default_value, create_slot, read);
        if (valued != GF_STATUS_OK) {
            return valued;
        }
        const std::optional<BindingValue> converted = convert_property_value(read, descriptor);
        if (!converted) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a property's default is not a value it holds");
        }
        descriptor.default_value = *converted;
    }
    definition.descriptor = std::move(descriptor);
    definition.change_notifications = (property.flags & GF_CUSTOM_PROPERTY_CHANGE_NOTIFICATIONS) != 0U;
    return GF_STATUS_OK;
}

// The spec, validated whole before anything is made.
gf_status read_spec(const gf_custom_control_spec& spec, std::vector<CustomPropertyDefinition>& definitions) {
    if (spec.base != GF_CUSTOM_BASE_SCROLLABLE_CONTROL && spec.base != GF_CUSTOM_BASE_USER_CONTROL) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "the base is not a gf_custom_base");
    }
    if ((spec.flags & ~static_cast<gf_custom_control_flags>(GF_CUSTOM_CONTROL_FOCUSABLE)) != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "unknown gf_custom_control_flags bits are set");
    }
    const gf_custom_control_callbacks& callbacks = spec.callbacks;
    if (callbacks.struct_size < callbacks_size_1_0 || callbacks.reserved != 0U || callbacks.paint == nullptr ||
        callbacks.get_property == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "the callbacks need struct_size of at least 48, reserved zero, paint and get_property");
    }
    if (spec.property_count > GF_CUSTOM_MAXIMUM_PROPERTIES ||
        (spec.property_count != 0U && (spec.properties == nullptr || spec.property_size < property_size_1_0))) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "at most 256 properties, of property_size at least 96 bytes when there are any");
    }
    definitions.reserve(static_cast<std::size_t>(spec.property_count));
    bool writable = false;
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(spec.properties);
    for (std::uint64_t index = 0U; index < spec.property_count; ++index) {
        // property_size is the caller's stride: a later minor's record is
        // longer, and this library reads its 1.0 prefix.
        const gf_custom_property* property = reinterpret_cast<const gf_custom_property*>(cursor);
        CustomPropertyDefinition definition{};
        const gf_status read = read_property(*property, definitions, definition);
        if (read != GF_STATUS_OK) {
            return read;
        }
        writable = writable || definition.descriptor.writable;
        definitions.push_back(std::move(definition));
        cursor += spec.property_size;
    }
    if (writable && callbacks.set_property == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a writable property needs set_property");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL custom_controls_create(const gf_custom_control_spec* spec, gf_utf8_view stable_id,
                                         gf_object* object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(create_slot.table_id, create_slot.slot_index);
        if (spec == nullptr || object == nullptr || (*spec).struct_size < spec_size_1_0) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                        "create needs a gf_custom_control_spec of at least the 1.0 size and an object output");
        }
        std::string_view id_text;
        const gf_status read_id = read_text(stable_id, create_slot, id_text);
        if (read_id != GF_STATUS_OK) {
            return read_id;
        }
        if (!valid_stable_id(id_text)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, stable_id_rule);
        }
        std::vector<CustomPropertyDefinition> definitions;
        const gf_status read = read_spec(*spec, definitions);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_status entered = registry().enter_ui(create_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        // The control copies the callbacks at their 1.0 size; until it
        // exists, a failure calls none of them, release among them.
        gf_custom_control_callbacks callbacks = (*spec).callbacks;
        callbacks.struct_size = callbacks_size_1_0;
        const bool focusable = ((*spec).flags & GF_CUSTOM_CONTROL_FOCUSABLE) != 0U;
        Component::Ptr made;
        if ((*spec).base == GF_CUSTOM_BASE_USER_CONTROL) {
            made = std::make_shared<CustomControl<UserControl>>(StableId(std::string(id_text)), callbacks,
                                                                definitions, focusable);
        } else {
            made = std::make_shared<CustomControl<ScrollableControl>>(StableId(std::string(id_text)), callbacks,
                                                                      definitions, focusable);
        }
        *object = registry().add_reference(made);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(create_slot);
    }
}

gf_status GF_CALL custom_controls_notify_changed(gf_object object, std::uint64_t index) noexcept {
    try {
        const ShadowFrameScope shadow_frame(notify_changed_slot.table_id, notify_changed_slot.slot_index);
        Control::Ptr control;
        const gf_status entered = enter_live_control(object, notify_changed_slot, control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (index > GF_CUSTOM_MAXIMUM_PROPERTIES) {
            return fail(GF_STATUS_INVALID_ARGUMENT, notify_changed_slot, "the index is past the properties");
        }
        const CustomNotifyResult result = notify_custom_changed(*control, static_cast<std::size_t>(index));
        if (result == CustomNotifyResult::not_custom) {
            return fail(GF_STATUS_WRONG_HANDLE_KIND, notify_changed_slot, "the control is not a custom control");
        }
        if (result == CustomNotifyResult::not_notifying) {
            return fail(GF_STATUS_INVALID_ARGUMENT, notify_changed_slot,
                        "the property does not exist or was declared without change notifications");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(notify_changed_slot);
    }
}

constexpr gf_custom_controls_table table{
    .struct_size = sizeof(gf_custom_controls_table),
    .abi_minor = GF_ABI_MINOR,
    .create = &custom_controls_create,
    .notify_changed = &custom_controls_notify_changed,
};

} // namespace

const gf_custom_controls_table& custom_controls_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
