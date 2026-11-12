// gf_value conversions shared by the property and event tables. See
// abi_values.hpp.

#include "abi_values.hpp"

#include "../registry/abi_registry.hpp"
#include "../tables/abi_records.hpp"

#include "../../../core/resources/fonts/bundled_font_families.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include <string>
#include <string_view>
#include <variant>

namespace gui_forms::abi1 {

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

bool record_binding_value(const BindingValue& held, gf_value& record, std::string_view& text) {
    if (std::holds_alternative<std::monostate>(held)) {
        record.kind = GF_VALUE_KIND_NULL;
    } else if (std::holds_alternative<bool>(held)) {
        record.kind = GF_VALUE_KIND_BOOLEAN;
        record.boolean = std::get<bool>(held) ? 1U : 0U;
    } else if (std::holds_alternative<std::int64_t>(held)) {
        record.kind = GF_VALUE_KIND_SIGNED;
        record.signed_integer = std::get<std::int64_t>(held);
    } else if (std::holds_alternative<std::uint64_t>(held)) {
        record.kind = GF_VALUE_KIND_UNSIGNED;
        record.unsigned_integer = std::get<std::uint64_t>(held);
    } else if (std::holds_alternative<double>(held)) {
        record.kind = GF_VALUE_KIND_NUMBER;
        record.number = std::get<double>(held);
    } else if (std::holds_alternative<std::string>(held)) {
        record.kind = GF_VALUE_KIND_TEXT;
        text = std::get<std::string>(held);
    } else if (std::holds_alternative<Point>(held)) {
        const Point& point = std::get<Point>(held);
        record.kind = GF_VALUE_KIND_POINT;
        record.point = gf_logical_point{point.x, point.y};
    } else if (std::holds_alternative<Size>(held)) {
        const Size& size = std::get<Size>(held);
        record.kind = GF_VALUE_KIND_SIZE;
        record.size = gf_logical_size{size.width, size.height};
    } else if (std::holds_alternative<Rect>(held)) {
        const Rect& rectangle = std::get<Rect>(held);
        record.kind = GF_VALUE_KIND_RECTANGLE;
        record.rectangle = gf_logical_rect{rectangle.x, rectangle.y, rectangle.width, rectangle.height};
    } else if (std::holds_alternative<Insets>(held)) {
        const Insets& insets = std::get<Insets>(held);
        record.kind = GF_VALUE_KIND_INSETS;
        record.insets = gf_logical_insets{insets.left, insets.top, insets.right, insets.bottom};
    } else if (std::holds_alternative<Color>(held)) {
        record.kind = GF_VALUE_KIND_COLOR;
        record.color = packed_color(std::get<Color>(held));
    } else if (std::holds_alternative<FontSpec>(held)) {
        const FontSpec& font = std::get<FontSpec>(held);
        record.kind = GF_VALUE_KIND_FONT;
        record.font.role = static_cast<gf_font_role>(font.role);
        record.font.weight = font.weight;
        record.font.italic = font.italic ? 1U : 0U;
        record.font.size = font.size;
        record.font.letter_spacing = font.letter_spacing;
        // The family id rides in the reserved word (since 1.2, K1).
        record.font.reserved = static_cast<std::uint32_t>(font.family.value);
    } else if (std::holds_alternative<ImageId>(held)) {
        record.kind = GF_VALUE_KIND_IMAGE;
        record.unsigned_integer = std::get<ImageId>(held).value;
    } else if (std::holds_alternative<PropertyCollectionValue>(held)) {
        const PropertyCollectionValue& collection = std::get<PropertyCollectionValue>(held);
        record.kind = GF_VALUE_KIND_COLLECTION;
        record.unsigned_integer = property_collection_items(collection).size();
    } else if (std::holds_alternative<PropertyEnumValue>(held)) {
        const PropertyEnumValue& enumeration = std::get<PropertyEnumValue>(held);
        record.kind = GF_VALUE_KIND_ENUMERATION;
        record.signed_integer = enumeration.value;
        text = enumeration.name;
    } else {
        return false;
    }
    return true;
}

namespace {

// The FontSpec a caller's gf_font holds.
gf_status read_font(const gf_font& font, SlotIdentity slot, BindingValue& converted) {
    if (font.role > GF_FONT_ROLE_MONOSPACE || font.weight < 1U || font.weight > 1000U || font.italic > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a font has a known role, a weight from 1 to 1000 and italic 0 or 1");
    }
    // reserved is the bundled family id (since 1.2, K1): zero, or a
    // numbered family fonts.family_id gives.
    const BundledFontFamily family{static_cast<std::uint8_t>(font.reserved)};
    if (font.reserved > 0xFFU || !detail::valid_bundled_family(family)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a font's family is zero or an id fonts.family_id gives");
    }
    FontSpec spec;
    spec.family = family;
    spec.role = static_cast<FontRole>(font.role);
    spec.size = font.size;
    spec.weight = static_cast<std::uint16_t>(font.weight);
    spec.italic = font.italic == 1U;
    spec.letter_spacing = font.letter_spacing;
    if (!valid_font_spec(spec)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a font's size is finite and above zero, its letter spacing from -size/4 to size");
    }
    converted = spec;
    return GF_STATUS_OK;
}

// The items of a caller's COLLECTION, each read as read_value reads a value.
gf_status read_collection(const gf_value& value, SlotIdentity slot, BindingValue& converted) {
    if (value.element_count > maximum_property_collection_items) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a collection has at most 4096 elements");
    }
    if (value.elements == nullptr && value.element_count != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a collection with elements needs its elements array");
    }
    // The elements are stepped by the first one's struct_size, not this
    // library's sizeof (N14): a caller built against another minor passes
    // its own size.
    std::uint32_t stride = 0U;
    const gf_status checked = check_array(value.elements, value.element_count, value_size_1_0, slot, stride);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    std::vector<BindingValue> items;
    items.reserve(static_cast<std::size_t>(value.element_count));
    for (std::uint64_t index = 0U; index < value.element_count; ++index) {
        const gf_value& element = element_at(value.elements, stride, index);
        if (element.struct_size >= value_size_1_0 && element.kind == GF_VALUE_KIND_COLLECTION) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a collection's element is not a collection");
        }
        BindingValue item;
        const gf_status read = read_value(&element, slot, item);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (std::holds_alternative<std::monostate>(item)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a collection's element is not null");
        }
        items.push_back(std::move(item));
    }
    BindingValueKind item_kind = BindingValueKind::text;
    if (!items.empty()) {
        item_kind = binding_value_kind(items.front());
    }
    try {
        // The caller names no item type; a property gives its own
        // (shape_collection).
        converted = make_property_collection(std::string{"value"}, item_kind, std::move(items));
    } catch (const std::invalid_argument&) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a collection's elements are of one kind");
    }
    return GF_STATUS_OK;
}

} // namespace

void shape_collection(BindingValue& converted, const std::optional<BindingValue>& shape) {
    if (!std::holds_alternative<PropertyCollectionValue>(converted) || !shape ||
        !std::holds_alternative<PropertyCollectionValue>(*shape)) {
        return;
    }
    const PropertyCollectionValue& model = std::get<PropertyCollectionValue>(*shape);
    const std::span<const BindingValue> read = property_collection_items(std::get<PropertyCollectionValue>(converted));
    std::vector<BindingValue> items(read.begin(), read.end());
    converted = make_property_collection(std::string(model.item_type_name()), model.item_kind(), std::move(items));
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
        return read_font((*value).font, slot, converted);
    case GF_VALUE_KIND_IMAGE:
        converted = ImageId{(*value).unsigned_integer};
        return GF_STATUS_OK;
    case GF_VALUE_KIND_COLLECTION:
        return read_collection(*value, slot, converted);
    case GF_VALUE_KIND_OBJECT:
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot, "an object value has no gf_value form in this library yet");
    default:
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot, "this library does not know that gf_value kind");
    }
}

void write_value_fields(const gf_value& complete, gf_value& destination) noexcept {
    const std::size_t skipped = sizeof(std::uint32_t);
    unsigned char* target = reinterpret_cast<unsigned char*>(&destination);
    const unsigned char* source = reinterpret_cast<const unsigned char*>(&complete);
    std::memcpy(target + skipped, source + skipped, value_size_1_0 - skipped);
}

gf_status check_value_output(const gf_value* value, const char* text_buffer, std::uint64_t text_capacity,
                       const std::uint64_t* text_required, SlotIdentity slot) {
    if (value == nullptr || (*value).struct_size < value_size_1_0 || text_required == nullptr ||
        (text_buffer == nullptr && text_capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a value needs a gf_value of at least the 1.0 size, a text size output, and a text "
                    "buffer unless its capacity is zero");
    }
    return GF_STATUS_OK;
}

gf_status write_value_output(const BindingValue& held, gf_value* value, char* text_buffer, std::uint64_t text_capacity,
                      std::uint64_t* text_required, SlotIdentity slot) {
    // The complete 1.0 record; only its kind's fields are set.
    gf_value complete{};
    complete.struct_size = value_size_1_0;
    std::string_view text;
    if (!record_binding_value(held, complete, text)) {
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot,
                    "the property's value kind has no gf_value form in this library");
    }
    *text_required = text.size();
    if (text_capacity < text.size()) {
        return fail(GF_STATUS_BUFFER_TOO_SMALL, slot, "the text buffer is smaller than the property's text");
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
}

} // namespace gui_forms::abi1
