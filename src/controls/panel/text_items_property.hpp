#pragma once

// The `items` property of a control that holds an ordered list of text
// items (ListBox, CheckedListBox, DomainUpDown; owner decision FD6-4): a
// collection of "String" text values, as ComboBox's `items` is. Internal to
// the controls library.

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/binding/value/property_collection_value/property_collection_value.hpp"
#include "gui_forms/dirty.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::text_items_property {

// The descriptor: a content-serialized, non-bindable collection whose
// default is empty; a change remeasures, repaints and republishes semantics.
[[nodiscard]] inline PropertyDescriptor descriptor(std::string description) {
    PropertyDescriptor items{};
    items.name = "items";
    items.kind = BindingValueKind::collection;
    items.category = "Data";
    items.description = std::move(description);
    items.default_value = BindingValue{make_property_collection("String", BindingValueKind::text, {})};
    items.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    items.serialization_visibility = PropertySerializationVisibility::content;
    items.bindable = false;
    return items;
}

// The property's value of `items`.
[[nodiscard]] inline BindingValue value_of(std::span<const std::string> items) {
    std::vector<BindingValue> values{};
    values.reserve(items.size());
    for (const std::string& item : items) values.emplace_back(item);
    BindingValue value{make_property_collection("String", BindingValueKind::text, std::move(values))};
    return value;
}

// The items `value` holds. Throws std::invalid_argument, naming `owner`,
// when it is not a collection of text.
[[nodiscard]] inline std::vector<std::string> items_of(const BindingValue& value, std::string_view owner) {
    const PropertyCollectionValue* collection = std::get_if<PropertyCollectionValue>(&value);
    if (collection == nullptr || !*collection || (*collection).item_kind() != BindingValueKind::text) {
        throw std::invalid_argument(std::string(owner) + ".Items requires a homogeneous text collection");
    }
    const std::span<const BindingValue> values = property_collection_items(*collection);
    std::vector<std::string> items{};
    items.reserve(values.size());
    for (const BindingValue& item : values) items.push_back(std::get<std::string>(item));
    return items;
}

} // namespace gui_forms::text_items_property
