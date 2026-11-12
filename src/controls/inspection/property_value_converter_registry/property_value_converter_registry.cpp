#include "gui_forms/inspection/property_value_converter_registry/property_value_converter_registry.hpp"

#include "gui_forms/controls/color_value_editor.hpp"
#include "gui_forms/text.hpp"
#include "../property_service_utilities.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {

using detail::invariant_number_text;
using detail::localize_number;
using detail::property_service_name;
using detail::require_conversion_context;

namespace {

std::string format_invariant_value(const BindingValue& value,
                                   const PropertyDescriptor&) {
    const bool* boolean = std::get_if<bool>(&value);
    return boolean ? std::string(*boolean ? "True" : "False")
                   : binding_value_to_string(value);
}

std::optional<BindingValue> parse_invariant_value(
    std::string_view text, const BindingValue&,
    const PropertyDescriptor& descriptor) {
    return convert_property_value(BindingValue{std::string(text)}, descriptor);
}

std::string format_invariant_value_with_context(
    const BindingValue& value, const PropertyDescriptor& descriptor,
    const PropertyConversionContext& context) {
    const bool* boolean = std::get_if<bool>(&value);
    if (boolean) return std::string(*boolean ? "True" : "False");
    std::string formatted = binding_value_to_string(value);
    return descriptor.kind == BindingValueKind::signed_integer ||
            descriptor.kind == BindingValueKind::unsigned_integer ||
            descriptor.kind == BindingValueKind::number
        ? localize_number(std::move(formatted), context)
        : formatted;
}

std::optional<BindingValue> parse_invariant_value_with_context(
    std::string_view text, const BindingValue&,
    const PropertyDescriptor& descriptor,
    const PropertyConversionContext& context) {
    if (descriptor.kind == BindingValueKind::signed_integer ||
        descriptor.kind == BindingValueKind::unsigned_integer ||
        descriptor.kind == BindingValueKind::number) {
        const std::optional<std::string> normalized =
            invariant_number_text(text, context);
        return normalized
            ? convert_property_value(BindingValue{*normalized}, descriptor)
            : std::optional<BindingValue>{};
    }
    return convert_property_value(BindingValue{std::string(text)}, descriptor);
}

std::string format_color_hex(const BindingValue& value,
                             const PropertyDescriptor&) {
    const Color* color = std::get_if<Color>(&value);
    return color ? ColorValueEditor::format_value(*color) : std::string{};
}

std::optional<BindingValue> parse_color_hex(
    std::string_view text, const BindingValue&, const PropertyDescriptor&) {
    const std::optional<Color> color = ColorValueEditor::parse_value(text);
    return color ? std::optional<BindingValue>{BindingValue{*color}}
                 : std::optional<BindingValue>{};
}

} // namespace

bool PropertyValueConverterRegistry::register_converter(
    std::string name, PropertyValueConverter converter) {
    const std::string canonical = property_service_name(name);
    if ((!converter.format && !converter.format_with_context) ||
        (!converter.parse && !converter.parse_with_context)) {
        throw std::invalid_argument(
            "Property converters require format and parse callbacks");
    }
    return converters_.emplace(canonical, std::move(converter)).second;
}

bool PropertyValueConverterRegistry::unregister_converter(
    std::string_view name) {
    const std::string canonical = property_service_name(name);
    const bool removed = converters_.erase(canonical) != 0U;
    if (removed) {
        KindMap::iterator item = kind_mappings_.begin();
        while (item != kind_mappings_.end()) {
            if ((*item).second == canonical) {
                item = kind_mappings_.erase(item);
            } else {
                ++item;
            }
        }
    }
    return removed;
}

void PropertyValueConverterRegistry::map_kind(
    BindingValueKind kind, std::string converter_name) {
    const std::string canonical = property_service_name(converter_name);
    if (!converters_.contains(canonical)) {
        throw std::invalid_argument(
            "Property converter kind mapping requires a registered converter");
    }
    kind_mappings_.insert_or_assign(kind, canonical);
}

void PropertyValueConverterRegistry::clear_kind(BindingValueKind kind) {
    kind_mappings_.erase(kind);
}

std::optional<std::string> PropertyValueConverterRegistry::converter_for(
    BindingValueKind kind) const {
    const KindMap::const_iterator found = kind_mappings_.find(kind);
    return found == kind_mappings_.end()
        ? std::optional<std::string>{}
        : std::optional<std::string>{(*found).second};
}

const PropertyValueConverter* PropertyValueConverterRegistry::find(
    std::string_view name) const noexcept {
    if (name.empty() || !validate_utf8(name).valid()) return nullptr;
    const ConverterMap::const_iterator found =
        converters_.find(canonical_binding_name(name));
    return found == converters_.end() ? nullptr : &(*found).second;
}

std::string PropertyValueConverterRegistry::format(
    const BindingValue& value, const PropertyDescriptor& descriptor) const {
    if (binding_value_kind(value) == BindingValueKind::null) return "(none)";
    std::string service = descriptor.converter_name;
    if (service.empty()) {
        const std::optional<std::string> mapped = converter_for(descriptor.kind);
        if (mapped) service = *mapped;
    }
    const PropertyValueConverter* converter = find(service);
    std::string result = converter
        ? ((*converter).format_with_context
               ? (*converter).format_with_context(value, descriptor, context_)
               : (*converter).format(value, descriptor))
        : binding_value_to_string(value);
    if (result.size() > 64U * 1024U || !validate_utf8(result).valid()) {
        throw std::invalid_argument(
            "Property converter produced invalid or unbounded display text");
    }
    return result;
}

std::optional<BindingValue> PropertyValueConverterRegistry::parse(
    std::string_view text, const BindingValue& current,
    const PropertyDescriptor& descriptor) const {
    if (text.size() > 64U * 1024U || !validate_utf8(text).valid()) return {};
    if (descriptor.nullable && (text.empty() || text == "(none)" ||
                                text == "(None)")) {
        return BindingValue{std::monostate{}};
    }
    for (const BindingValue& standard : descriptor.standard_values) {
        if (format(standard, descriptor) == text) return standard;
    }
    if (descriptor.standard_values_exclusive) return {};
    std::string service = descriptor.converter_name;
    if (service.empty()) {
        const std::optional<std::string> mapped = converter_for(descriptor.kind);
        if (mapped) service = *mapped;
    }
    const PropertyValueConverter* converter = find(service);
    if (!converter) return {};
    const std::optional<BindingValue> result = (*converter).parse_with_context
        ? (*converter).parse_with_context(text, current, descriptor, context_)
        : (*converter).parse(text, current, descriptor);
    const std::optional<BindingValue> converted = result
        ? convert_property_value(*result, descriptor)
        : std::optional<BindingValue>{};
    return converted && valid_property_value_tree(*converted)
        ? converted : std::optional<BindingValue>{};
}

void PropertyValueConverterRegistry::set_context(
    PropertyConversionContext context) {
    require_conversion_context(context);
    context_ = std::move(context);
}

std::shared_ptr<PropertyValueConverterRegistry>
PropertyValueConverterRegistry::create_default() {
    std::shared_ptr<gui_forms::PropertyValueConverterRegistry> result = std::make_shared<PropertyValueConverterRegistry>();
    PropertyValueConverter invariant;
    invariant.format = format_invariant_value;
    invariant.parse = parse_invariant_value;
    invariant.format_with_context = format_invariant_value_with_context;
    invariant.parse_with_context = parse_invariant_value_with_context;
    static_cast<void>((*result).register_converter("invariant", invariant));
    for (const BindingValueKind kind : {
             BindingValueKind::boolean, BindingValueKind::signed_integer,
             BindingValueKind::unsigned_integer, BindingValueKind::number,
             BindingValueKind::text, BindingValueKind::enumeration}) {
        (*result).map_kind(kind, "invariant");
    }
    PropertyValueConverter color_hex;
    color_hex.format = format_color_hex;
    color_hex.parse = parse_color_hex;
    static_cast<void>((*result).register_converter("color-hex", color_hex));
    (*result).map_kind(BindingValueKind::color, "color-hex");
    return result;
}

} // namespace gui_forms
