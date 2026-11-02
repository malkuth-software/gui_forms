// GF_TABLE_PROPERTIES: properties by schema name. See gf_abi.h.

#include "abi_tables.hpp"

#include "../registry/abi_registry.hpp"

#include "gui_forms/binding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity get_slot{GF_TABLE_PROPERTIES, 0U};
constexpr SlotIdentity set_slot{GF_TABLE_PROPERTIES, 1U};
constexpr SlotIdentity reset_slot{GF_TABLE_PROPERTIES, 2U};

// The 1.0 size of gf_value: a caller passes at least this much.
constexpr std::uint32_t value_size_1_0 = 152U;
static_assert(sizeof(gf_value) == value_size_1_0);

// A colour as 0xRRGGBBAA, and back.
std::uint32_t packed_color(Color color) noexcept {
    const std::uint32_t packed = (static_cast<std::uint32_t>(color.red) << 24U) |
                                 (static_cast<std::uint32_t>(color.green) << 16U) |
                                 (static_cast<std::uint32_t>(color.blue) << 8U) |
                                 static_cast<std::uint32_t>(color.alpha);
    return packed;
}

Color unpacked_color(std::uint32_t packed) noexcept {
    Color color;
    color.red = static_cast<std::uint8_t>((packed >> 24U) & 0xFFU);
    color.green = static_cast<std::uint8_t>((packed >> 16U) & 0xFFU);
    color.blue = static_cast<std::uint8_t>((packed >> 8U) & 0xFFU);
    color.alpha = static_cast<std::uint8_t>(packed & 0xFFU);
    return color;
}

bool finite_all(double first, double second, double third, double fourth) noexcept {
    const bool finite = std::isfinite(first) && std::isfinite(second) && std::isfinite(third) &&
                        std::isfinite(fourth);
    return finite;
}

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
        if (value == nullptr || (*value).struct_size < value_size_1_0 || text_required == nullptr ||
            (text_buffer == nullptr && text_capacity != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, get_slot,
                        "get needs a gf_value of at least the 1.0 size, a text size output, and a text "
                        "buffer unless its capacity is zero");
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
        // The complete 1.0 record; only its kind's fields are set.
        gf_value complete{};
        complete.struct_size = value_size_1_0;
        std::string_view text;
        const BindingValue& held = *current;
        if (std::holds_alternative<std::monostate>(held)) {
            complete.kind = GF_VALUE_KIND_NULL;
        } else if (std::holds_alternative<bool>(held)) {
            complete.kind = GF_VALUE_KIND_BOOLEAN;
            complete.boolean = std::get<bool>(held) ? 1U : 0U;
        } else if (std::holds_alternative<std::int64_t>(held)) {
            complete.kind = GF_VALUE_KIND_SIGNED;
            complete.signed_integer = std::get<std::int64_t>(held);
        } else if (std::holds_alternative<std::uint64_t>(held)) {
            complete.kind = GF_VALUE_KIND_UNSIGNED;
            complete.unsigned_integer = std::get<std::uint64_t>(held);
        } else if (std::holds_alternative<double>(held)) {
            complete.kind = GF_VALUE_KIND_NUMBER;
            complete.number = std::get<double>(held);
        } else if (std::holds_alternative<std::string>(held)) {
            complete.kind = GF_VALUE_KIND_TEXT;
            text = std::get<std::string>(held);
        } else if (std::holds_alternative<Point>(held)) {
            const Point& point = std::get<Point>(held);
            complete.kind = GF_VALUE_KIND_POINT;
            complete.point = gf_logical_point{point.x, point.y};
        } else if (std::holds_alternative<Size>(held)) {
            const Size& size = std::get<Size>(held);
            complete.kind = GF_VALUE_KIND_SIZE;
            complete.size = gf_logical_size{size.width, size.height};
        } else if (std::holds_alternative<Rect>(held)) {
            const Rect& rectangle = std::get<Rect>(held);
            complete.kind = GF_VALUE_KIND_RECTANGLE;
            complete.rectangle = gf_logical_rect{rectangle.x, rectangle.y, rectangle.width, rectangle.height};
        } else if (std::holds_alternative<Insets>(held)) {
            const Insets& insets = std::get<Insets>(held);
            complete.kind = GF_VALUE_KIND_INSETS;
            complete.insets = gf_logical_insets{insets.left, insets.top, insets.right, insets.bottom};
        } else if (std::holds_alternative<Color>(held)) {
            complete.kind = GF_VALUE_KIND_COLOR;
            complete.color = packed_color(std::get<Color>(held));
        } else if (std::holds_alternative<PropertyEnumValue>(held)) {
            const PropertyEnumValue& enumeration = std::get<PropertyEnumValue>(held);
            complete.kind = GF_VALUE_KIND_ENUMERATION;
            complete.signed_integer = enumeration.value;
            text = enumeration.name;
        } else {
            return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, get_slot,
                        "the property's value kind has no gf_value form in this library");
        }
        *text_required = text.size();
        if (text_capacity < text.size()) {
            return fail(GF_STATUS_BUFFER_TOO_SMALL, get_slot, "the text buffer is smaller than the property's text");
        }
        if (!text.empty()) {
            std::memcpy(text_buffer, text.data(), text.size());
            complete.text.data = text_buffer;
            complete.text.size = text.size();
        }
        // Writes the 1.0 fields after struct_size; a larger caller record
        // keeps its later bytes, which this library does not know.
        const std::size_t skipped = sizeof(std::uint32_t);
        unsigned char* destination = reinterpret_cast<unsigned char*>(value);
        const unsigned char* source = reinterpret_cast<const unsigned char*>(&complete);
        std::memcpy(destination + skipped, source + skipped, value_size_1_0 - skipped);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(get_slot);
    }
}

// The BindingValue a caller's gf_value holds.
gf_status read_value(const gf_value* value, SlotIdentity slot, BindingValue& converted) {
    if (value == nullptr || (*value).struct_size < value_size_1_0) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a gf_value needs at least the 1.0 size");
    }
    switch ((*value).kind) {
    case GF_VALUE_KIND_NULL:
        converted = std::monostate{};
        return GF_STATUS_OK;
    case GF_VALUE_KIND_BOOLEAN:
        if ((*value).boolean > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a boolean is 0 or 1");
        }
        converted = (*value).boolean == 1U;
        return GF_STATUS_OK;
    case GF_VALUE_KIND_SIGNED:
        converted = (*value).signed_integer;
        return GF_STATUS_OK;
    case GF_VALUE_KIND_UNSIGNED:
        converted = (*value).unsigned_integer;
        return GF_STATUS_OK;
    case GF_VALUE_KIND_NUMBER:
        if (!std::isfinite((*value).number)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a number is finite");
        }
        converted = (*value).number;
        return GF_STATUS_OK;
    case GF_VALUE_KIND_TEXT: {
        std::string_view text;
        const gf_status read = read_text((*value).text, slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        converted = std::string(text);
        return GF_STATUS_OK;
    }
    case GF_VALUE_KIND_POINT: {
        const gf_logical_point& point = (*value).point;
        if (!finite_all(point.x, point.y, 0.0, 0.0)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a point is finite");
        }
        converted = Point{point.x, point.y};
        return GF_STATUS_OK;
    }
    case GF_VALUE_KIND_SIZE: {
        const gf_logical_size& size = (*value).size;
        if (!finite_all(size.width, size.height, 0.0, 0.0)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a size is finite");
        }
        converted = Size{size.width, size.height};
        return GF_STATUS_OK;
    }
    case GF_VALUE_KIND_RECTANGLE: {
        const gf_logical_rect& rectangle = (*value).rectangle;
        if (!finite_all(rectangle.x, rectangle.y, rectangle.width, rectangle.height)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a rectangle is finite");
        }
        converted = Rect{rectangle.x, rectangle.y, rectangle.width, rectangle.height};
        return GF_STATUS_OK;
    }
    case GF_VALUE_KIND_INSETS: {
        const gf_logical_insets& insets = (*value).insets;
        if (!finite_all(insets.left, insets.top, insets.right, insets.bottom)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "insets are finite");
        }
        converted = Insets{insets.left, insets.top, insets.right, insets.bottom};
        return GF_STATUS_OK;
    }
    case GF_VALUE_KIND_COLOR:
        converted = unpacked_color((*value).color);
        return GF_STATUS_OK;
    case GF_VALUE_KIND_ENUMERATION:
        // The property's own enumeration names the value when it is set.
        converted = (*value).signed_integer;
        return GF_STATUS_OK;
    case GF_VALUE_KIND_FONT:
    case GF_VALUE_KIND_IMAGE:
    case GF_VALUE_KIND_OBJECT:
    case GF_VALUE_KIND_COLLECTION:
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot, "that gf_value kind has no form in this library yet");
    default:
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot, "this library does not know that gf_value kind");
    }
}

gf_status GF_CALL properties_set(gf_object object, gf_utf8_view name, const gf_value* value) noexcept {
    try {
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
        if (!(*control).property_descriptor(property)) {
            return fail(GF_STATUS_NOT_FOUND, set_slot, "the control has no property of that name");
        }
        (*control).set_property_value(property, std::move(converted));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_slot);
    }
}

gf_status GF_CALL properties_reset(gf_object object, gf_utf8_view name) noexcept {
    try {
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

constexpr gf_properties_table table{
    .struct_size = sizeof(gf_properties_table),
    .abi_minor = GF_ABI_MINOR,
    .get = &properties_get,
    .set = &properties_set,
    .reset = &properties_reset,
};

} // namespace

const gf_properties_table& properties_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
