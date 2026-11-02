// GF_TABLE_PROPERTIES: properties by schema name. See gf_abi.h.

#include "abi_tables.hpp"

#include "../registry/abi_registry.hpp"
#include "../values/abi_values.hpp"

#include "gui_forms/binding.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity get_slot{GF_TABLE_PROPERTIES, 0U};
constexpr SlotIdentity set_slot{GF_TABLE_PROPERTIES, 1U};
constexpr SlotIdentity reset_slot{GF_TABLE_PROPERTIES, 2U};
constexpr SlotIdentity element_at_slot{GF_TABLE_PROPERTIES, 3U};

// The live control behind object and the property name, on the UI thread.
gf_status enter_property(gf_object object, gf_utf8_view name, SlotIdentity slot, Control::Ptr& control,
                         std::string_view& property) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status found = registry().find_object(object, slot, control);
    if (found != GF_STATUS_OK) {
        return found;
    }
    if (!(*control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the object has been disposed");
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

gf_status GF_CALL properties_get(gf_object object, gf_utf8_view name, gf_value* value, char* text_buffer,
                                 std::uint64_t text_capacity, std::uint64_t* text_required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(get_slot.table_id, get_slot.slot_index);
        const gf_status checked = check_value_output(value, text_buffer, text_capacity, text_required, get_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        Control::Ptr control;
        std::string_view property;
        const gf_status entered = enter_property(object, name, get_slot, control, property);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::optional<BindingValue> current = (*control).property_value(property);
        if (!current) {
            return fail(GF_STATUS_NOT_FOUND, get_slot, "the control has no readable property of that name");
        }
        const gf_status written =
            write_value_output(*current, value, text_buffer, text_capacity, text_required, get_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(get_slot);
    }
}

gf_status GF_CALL properties_set(gf_object object, gf_utf8_view name, const gf_value* value) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_slot.table_id, set_slot.slot_index);
        Control::Ptr control;
        std::string_view property;
        const gf_status entered = enter_property(object, name, set_slot, control, property);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        BindingValue converted;
        const gf_status read = read_value(value, set_slot, converted);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const std::optional<PropertyDescriptor> descriptor = (*control).property_descriptor(property);
        if (!descriptor) {
            return fail(GF_STATUS_NOT_FOUND, set_slot, "the control has no property of that name");
        }
        // A caller's collection takes the property's item type and kind.
        try {
            shape_collection(converted, (*descriptor).default_value);
        } catch (const std::invalid_argument&) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_slot,
                        "an element does not convert to the property collection's item kind");
        }
        (*control).set_property_value(property, std::move(converted));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_slot);
    }
}

gf_status GF_CALL properties_reset(gf_object object, gf_utf8_view name) noexcept {
    try {
        const ShadowFrameScope shadow_frame(reset_slot.table_id, reset_slot.slot_index);
        Control::Ptr control;
        std::string_view property;
        const gf_status entered = enter_property(object, name, reset_slot, control, property);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (!(*control).property_descriptor(property)) {
            return fail(GF_STATUS_NOT_FOUND, reset_slot, "the control has no property of that name");
        }
        if (!(*control).reset_property(property)) {
            return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, reset_slot, "the property has no reset path");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(reset_slot);
    }
}

gf_status GF_CALL properties_element_at(gf_object object, gf_utf8_view name, std::uint64_t index, gf_value* value,
                                        char* text_buffer, std::uint64_t text_capacity,
                                        std::uint64_t* text_required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(element_at_slot.table_id, element_at_slot.slot_index);
        const gf_status checked =
            check_value_output(value, text_buffer, text_capacity, text_required, element_at_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        Control::Ptr control;
        std::string_view property;
        const gf_status entered = enter_property(object, name, element_at_slot, control, property);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::optional<BindingValue> current = (*control).property_value(property);
        if (!current) {
            return fail(GF_STATUS_NOT_FOUND, element_at_slot, "the control has no readable property of that name");
        }
        if (!std::holds_alternative<PropertyCollectionValue>(*current)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, element_at_slot, "the property is not a collection");
        }
        const std::span<const BindingValue> items =
            property_collection_items(std::get<PropertyCollectionValue>(*current));
        if (index >= items.size()) {
            return fail(GF_STATUS_NOT_FOUND, element_at_slot, "the index is not below the collection's count");
        }
        const BindingValue& item = items[static_cast<std::size_t>(index)];
        const gf_status written =
            write_value_output(item, value, text_buffer, text_capacity, text_required, element_at_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(element_at_slot);
    }
}

constexpr gf_properties_table table{
    .struct_size = sizeof(gf_properties_table),
    .abi_minor = GF_ABI_MINOR,
    .get = &properties_get,
    .set = &properties_set,
    .reset = &properties_reset,
    .element_at = &properties_element_at,
};

} // namespace

const gf_properties_table& properties_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
