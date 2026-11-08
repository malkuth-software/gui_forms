#include "gui_forms/binding/property_table/property_table.hpp"

#include "gui_forms/component/component/component.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

const PropertyEntry* find_property_entry(const Component& owner, std::string_view registered) {
    const std::span<const PropertyEntry> table = owner.property_table();
    for (const PropertyEntry& entry : table) {
        if (entry.name == registered) return &entry;
    }
    return nullptr;
}

std::optional<BindingValue> read_property_entry(const Component& owner, const PropertyEntry& entry) {
    const PropertyDescriptor& descriptor = entry.describe();
    if (!descriptor.readable || entry.read == nullptr) return std::nullopt;
    const BindingValue value = entry.read(owner);
    std::optional<BindingValue> normalized = convert_property_value(value, descriptor);
    if (!normalized || !valid_property_value_tree(*normalized)) {
        throw std::logic_error("GUI.Forms property getter violated its declared schema: " + descriptor.name);
    }
    return normalized;
}

void write_property_entry(Component& owner, const PropertyEntry& entry, const BindingValue& value) {
    const PropertyDescriptor& descriptor = entry.describe();
    if (!descriptor.writable || entry.write == nullptr) {
        throw std::logic_error("GUI.Forms property is read-only: " + descriptor.name);
    }
    const std::optional<BindingValue> converted = convert_property_value(value, descriptor);
    if (!converted) {
        throw std::invalid_argument("GUI.Forms property value cannot convert to its declared kind: " +
                                    descriptor.name);
    }
    entry.write(owner, *converted);
}

bool reset_property_entry(Component& owner, const PropertyEntry& entry) {
    if (entry.reset != nullptr) {
        entry.reset(owner);
        return true;
    }
    const PropertyDescriptor& descriptor = entry.describe();
    if (!descriptor.writable || entry.write == nullptr || !descriptor.default_value) return false;
    entry.write(owner, *descriptor.default_value);
    return true;
}

bool property_entry_should_serialize(const Component& owner, const PropertyEntry& entry) {
    const PropertyDescriptor& descriptor = entry.describe();
    if (descriptor.serialization_visibility == PropertySerializationVisibility::hidden) return false;
    if (entry.should_serialize != nullptr) return entry.should_serialize(owner);
    if (!descriptor.readable || entry.read == nullptr) return false;
    if (!descriptor.default_value) return true;
    const bool differs = entry.read(owner) != *descriptor.default_value;
    return differs;
}

PropertyValueOrigin property_entry_origin(const Component& owner, const PropertyEntry& entry) {
    if (entry.origin != nullptr) return entry.origin(owner);
    const PropertyDescriptor& descriptor = entry.describe();
    if (!descriptor.readable || entry.read == nullptr || !descriptor.default_value) {
        return PropertyValueOrigin::computed;
    }
    const bool defaulted = entry.read(owner) == *descriptor.default_value;
    const PropertyValueOrigin origin = defaulted ? PropertyValueOrigin::defaulted : PropertyValueOrigin::local;
    return origin;
}

SubscriptionToken connect_property_entry(Component& owner, const PropertyEntry& entry, Component& subscriber,
                                         std::function<void()> changed) {
    if (entry.connect_changed == nullptr || !changed) return SubscriptionToken{};
    SubscriptionToken token = entry.connect_changed(owner, subscriber, std::move(changed));
    return token;
}

BindingValue read_child_property_entry(const Component& container, const Control& child,
                                       const ChildPropertyEntry& entry) {
    const PropertyDescriptor& descriptor = entry.describe();
    const BindingValue value = entry.read(container, child);
    std::optional<BindingValue> normalized = convert_property_value(value, descriptor);
    if (!normalized || !valid_property_value_tree(*normalized)) {
        throw std::logic_error("GUI.Forms child property getter violated its declared schema: " + descriptor.name);
    }
    BindingValue result = std::move(*normalized);
    return result;
}

void write_child_property_entry(Component& container, const Control& child, const ChildPropertyEntry& entry,
                                const BindingValue& value) {
    const PropertyDescriptor& descriptor = entry.describe();
    if (!descriptor.writable || entry.write == nullptr) {
        throw std::logic_error("GUI.Forms child property is read-only: " + descriptor.name);
    }
    const std::optional<BindingValue> converted = convert_property_value(value, descriptor);
    if (!converted) {
        throw std::invalid_argument("GUI.Forms child property value cannot convert to its declared kind: " +
                                    descriptor.name);
    }
    entry.write(container, child, *converted);
}

bool reset_child_property_entry(Component& container, const Control& child, const ChildPropertyEntry& entry) {
    if (entry.reset != nullptr) {
        entry.reset(container, child);
        return true;
    }
    const PropertyDescriptor& descriptor = entry.describe();
    if (!descriptor.writable || entry.write == nullptr || !descriptor.default_value) return false;
    entry.write(container, child, *descriptor.default_value);
    return true;
}

bool child_property_entry_should_serialize(const Component& container, const Control& child,
                                           const ChildPropertyEntry& entry) {
    const PropertyDescriptor& descriptor = entry.describe();
    if (descriptor.serialization_visibility == PropertySerializationVisibility::hidden) return false;
    if (entry.should_serialize != nullptr) return entry.should_serialize(container, child);
    if (!descriptor.default_value) return true;
    const bool differs = entry.read(container, child) != *descriptor.default_value;
    return differs;
}

} // namespace gui_forms
