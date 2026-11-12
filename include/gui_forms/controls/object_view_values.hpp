#pragma once

#include <cstdint>
#include <string>

namespace gui_forms {

// ObjectView's value types that its property capabilities name, apart from
// the control.

enum class ObjectViewMode : std::uint8_t {
    icons,
    details,
};

enum class ObjectSelectionMode : std::uint8_t {
    single,
    multiple,
};

struct ObjectColumnId final {
    std::string value{};
    friend bool operator==(const ObjectColumnId& left, const ObjectColumnId& right) {
        const bool equal = left.value == right.value;
        return equal;
    }
};

enum class ObjectSortDirection : std::uint8_t { ascending, descending };

struct ObjectDetailsSort final {
    ObjectColumnId column{}; // Empty means no accepted sort indicator.
    ObjectSortDirection direction{ObjectSortDirection::ascending};
};

} // namespace gui_forms
