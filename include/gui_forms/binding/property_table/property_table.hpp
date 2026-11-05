#pragma once

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <span>
#include <string_view>

namespace gui_forms {

class Component;

// One property of a property-owning type's constant table (property-
// capabilities study, section 4 and decision 5): what the run-time
// registry, the form loader, binding, the schema export and the C ABI read,
// once per type instead of a registration per instance. Every operation is
// a plain function taking the owner, a Component (study section 14, G7:
// controls and components alike); the operation static_casts it to its
// declaring class, which derives from Component non-virtually. A null
// operation means the descriptor's default rule applies, as for a
// PropertyRegistration without it.
struct PropertyEntry final {
    // The registered (snake_case) name; equal to describe().name.
    std::string_view name;
    // The inert descriptor, built once per type and property on first use
    // and kept for the program's life. Its own checks are
    // Control::define_bindable_property's (property_table_descriptor).
    const PropertyDescriptor& (*describe)();
    // The current value; null when the property is not readable.
    BindingValue (*read)(const Component& owner);
    // Writes a value already converted to the descriptor's kind; null when
    // the property is not writable.
    void (*write)(Component& owner, const BindingValue& value);
    // Restores the default; null: write the descriptor's default value.
    void (*reset)(Component& owner);
    // Whether the value is serialized; null: compare with the default.
    bool (*should_serialize)(const Component& owner);
    // Where the value comes from; null: defaulted or local by the default.
    PropertyValueOrigin (*origin)(const Component& owner);
    // Connects `changed` to the property's change event for `owner`'s
    // lifetime; null when the property raises none.
    SubscriptionToken (*connect_changed)(Component& owner, Component& subscriber, std::function<void()> changed);
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

// A derived type's refinement of base entries (study section 14, G3): the
// base's table with each entry of `overrides` replacing the base entry of
// the same name, in the base entry's slot. An override that names no base
// entry throws, which fails the constant evaluation of the table that uses
// it, so the mistake does not compile.
template <std::size_t BaseCount, std::size_t OverrideCount>
[[nodiscard]] constexpr std::array<PropertyEntry, BaseCount> override_property_entries(
    const std::array<PropertyEntry, BaseCount>& base, const std::array<PropertyEntry, OverrideCount>& overrides) {
    std::array<PropertyEntry, BaseCount> result = base;
    for (std::size_t replacing = 0U; replacing < OverrideCount; ++replacing) {
        bool replaced = false;
        for (std::size_t slot = 0U; slot < BaseCount; ++slot) {
            if (result[slot].name != overrides[replacing].name) continue;
            result[slot] = overrides[replacing];
            replaced = true;
        }
        if (!replaced) throw std::logic_error("a property entry override names no base entry");
    }
    return result;
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

// Table access for any property owner (study section 14, G7): the entry
// of `owner`'s table with the registered name `registered`, or null. The
// operations below apply one entry's rules (a null operation's default
// included) and check the values against its descriptor; they do not check
// the owner's thread or liveness, which their callers do (Control's member
// API) and the entries' writes do through their prepare hooks.
[[nodiscard]] const PropertyEntry* find_property_entry(const Component& owner, std::string_view registered);
// The converted current value; nullopt when the entry is not readable.
// Throws std::logic_error when the read violates the descriptor.
[[nodiscard]] std::optional<BindingValue> read_property_entry(const Component& owner, const PropertyEntry& entry);
// Converts `value` to the entry's kind and writes it. Throws
// std::logic_error for a read-only entry and std::invalid_argument for a
// value that does not convert.
void write_property_entry(Component& owner, const PropertyEntry& entry, const BindingValue& value);
// Restores the default; false when the entry has none and no reset.
bool reset_property_entry(Component& owner, const PropertyEntry& entry);
[[nodiscard]] bool property_entry_should_serialize(const Component& owner, const PropertyEntry& entry);
[[nodiscard]] PropertyValueOrigin property_entry_origin(const Component& owner, const PropertyEntry& entry);
// Connects `changed` for `subscriber`'s lifetime; an empty token when the
// entry raises no change or `changed` is empty.
[[nodiscard]] SubscriptionToken connect_property_entry(Component& owner, const PropertyEntry& entry,
                                                       Component& subscriber, std::function<void()> changed);

} // namespace gui_forms
