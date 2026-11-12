#pragma once
// The text rules shared by the node graph's operations and its documents:
// what a node key and a property name may be. The graph refuses what a
// document could not hold, so every graph saves to a document load accepts.

#include "gui_forms/controls/node_graph.hpp"

#include <array>
#include <string_view>

namespace gui_forms::node_graph_text {

// ASCII letters, digits, '_', '.' and '-'.
[[nodiscard]] inline bool key_byte(char byte) noexcept {
    const bool letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
    const bool digit = byte >= '0' && byte <= '9';
    const bool mark = byte == '_' || byte == '.' || byte == '-';
    return letter || digit || mark;
}

[[nodiscard]] inline bool valid_key(std::string_view key) noexcept {
    if (key.empty() || key.size() > maximum_graph_key_bytes) return false;
    for (const char byte : key) {
        if (!key_byte(byte)) return false;
    }
    return true;
}

// The node fields a document writes by name; a property may not take one.
inline constexpr std::array<std::string_view, 4> field_words{"kind", "title", "bounds", "port"};

[[nodiscard]] inline bool valid_property_name(std::string_view name) noexcept {
    if (!valid_key(name)) return false;
    const char first = name.front();
    const bool letter = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
    if (!letter) return false;
    for (const std::string_view word : field_words) {
        if (name == word) return false;
    }
    return true;
}

} // namespace gui_forms::node_graph_text
