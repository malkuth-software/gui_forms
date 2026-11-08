#pragma once

// A container setting written as an ordered list of text items (study 10,
// section 6: a table's column and row styles, a track panel's track specs):
// a collection of "String" text values, each item one entry in the
// setting's own grammar. Internal to the controls library.

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/binding/value/property_collection_value/property_collection_value.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::text_collection_property {

// The property's value holding `items`, in order.
[[nodiscard]] inline BindingValue value_of(std::span<const std::string> items) {
    std::vector<BindingValue> values{};
    values.reserve(items.size());
    for (const std::string& item : items) values.emplace_back(item);
    BindingValue value{make_property_collection("String", BindingValueKind::text, std::move(values))};
    return value;
}

// The items `value` holds. Throws std::invalid_argument, naming `property`
// ("TableLayoutPanelCompat.column_styles"), when it is not a collection of
// text.
[[nodiscard]] inline std::vector<std::string> items_of(const BindingValue& value, std::string_view property) {
    const PropertyCollectionValue* collection = std::get_if<PropertyCollectionValue>(&value);
    if (collection == nullptr || !*collection || (*collection).item_kind() != BindingValueKind::text) {
        throw std::invalid_argument(std::string(property) + " requires a homogeneous text collection");
    }
    const std::span<const BindingValue> values = property_collection_items(*collection);
    std::vector<std::string> items{};
    items.reserve(values.size());
    for (const BindingValue& item : values) items.push_back(std::get<std::string>(item));
    return items;
}

// The words of one item, split at spaces; empty words are skipped. The
// words borrow `item`.
[[nodiscard]] inline std::vector<std::string_view> words_of(std::string_view item) {
    std::vector<std::string_view> words{};
    std::size_t position = 0U;
    while (position < item.size()) {
        while (position < item.size() && item[position] == ' ') ++position;
        const std::size_t start = position;
        while (position < item.size() && item[position] != ' ') ++position;
        if (position > start) words.push_back(item.substr(start, position - start));
    }
    return words;
}
std::vector<std::string_view> words_of(std::string&& item) = delete;

} // namespace gui_forms::text_collection_property
