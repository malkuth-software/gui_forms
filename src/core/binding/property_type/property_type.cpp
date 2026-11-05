#include "gui_forms/binding/property_type/property_type.hpp"

namespace gui_forms {

// The runtime conversion is the descriptor conversion: a PropertyDescriptor
// carrying only the type's kind, nullability and choice set, so a cell's
// as_type(PropertyType) and a registered property's write agree by
// construction.
std::optional<BindingValue> convert_to_property_type(const BindingValue& value,
                                                     const PropertyType& target) {
    PropertyDescriptor descriptor;
    descriptor.kind = target.kind;
    descriptor.nullable = target.nullable;
    descriptor.enumeration = target.enumeration;
    std::optional<BindingValue> converted = convert_property_value(value, descriptor);
    return converted;
}

} // namespace gui_forms
