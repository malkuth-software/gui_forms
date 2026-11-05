#pragma once

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

namespace gui_forms {

class Component;
class Control;

// One property of a control type's constant property table (property-
// capabilities study, section 4 and decision 5): what the run-time
// registry, the form loader, binding, the schema export and the C ABI read,
// once per type instead of a registration per instance. Every operation is
// a plain function taking the control; a null operation means the
// descriptor's default rule applies, as for a PropertyRegistration without
// it.
struct PropertyEntry final {
    // The registered (snake_case) name; equal to describe().name.
    std::string_view name;
    // The inert descriptor, built once per type and property on first use
    // and kept for the program's life. Its own checks are
    // Control::define_bindable_property's (property_table_descriptor).
    const PropertyDescriptor& (*describe)();
    // The current value; null when the property is not readable.
    BindingValue (*read)(const Control& control);
    // Writes a value already converted to the descriptor's kind; null when
    // the property is not writable.
    void (*write)(Control& control, const BindingValue& value);
    // Restores the default; null: write the descriptor's default value.
    void (*reset)(Control& control);
    // Whether the value is serialized; null: compare with the default.
    bool (*should_serialize)(const Control& control);
    // Where the value comes from; null: defaulted or local by the default.
    PropertyValueOrigin (*origin)(const Control& control);
    // Connects `changed` to the property's change event for `owner`'s
    // lifetime; null when the property raises none.
    SubscriptionToken (*connect_changed)(Control& control, Component& owner, std::function<void()> changed);
};

// Whether the names of `entries` are distinct. Used in a static_assert next
// to each type's table, so a duplicate fails where it is declared, naming
// the table.
[[nodiscard]] consteval bool property_names_distinct(std::span<const PropertyEntry> entries) {
    for (std::size_t first = 0U; first < entries.size(); ++first) {
        for (std::size_t second = first + 1U; second < entries.size(); ++second) {
            if (entries[first].name == entries[second].name) return false;
        }
    }
    return true;
}

// A declaring class's table after its base's: the base's entries first,
// then its own, so a type's slots are stable and gap-free.
template <std::size_t BaseCount, std::size_t OwnCount>
[[nodiscard]] constexpr std::array<PropertyEntry, BaseCount + OwnCount> join_property_tables(
    const std::array<PropertyEntry, BaseCount>& base, const std::array<PropertyEntry, OwnCount>& own) {
    std::array<PropertyEntry, BaseCount + OwnCount> joined{};
    for (std::size_t index = 0U; index < BaseCount; ++index) joined[index] = base[index];
    for (std::size_t index = 0U; index < OwnCount; ++index) joined[BaseCount + index] = own[index];
    return joined;
}

// Which operations a property has, for the descriptor checks.
struct PropertyOperations final {
    bool read{};
    bool write{};
    bool reset{};
    bool change_event{};
};

// Builds and checks a table entry's descriptor with the checks of
// Control::define_bindable_property (a registered name, kind, effects,
// enumeration, standard values, a default converting to the kind, a reader
// when readable and a writer when writable) and derives resettable and
// change_notifications from `operations`. Throws std::invalid_argument on a
// failed check. For an entry's describe().
[[nodiscard]] PropertyDescriptor property_table_descriptor(PropertyDescriptor descriptor,
                                                           PropertyOperations operations);

} // namespace gui_forms
