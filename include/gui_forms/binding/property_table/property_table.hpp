#pragma once

#include "gui_forms/export/application_export.hpp"
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
class Control;

// One property of a property-owning type's constant table (property-
// capabilities study, section 4 and decision 5): what the run-time
// registry, the form loader, binding, the schema export and the C ABI read,
// once per type instead of a registration per instance. Every operation is
// a plain function taking the owner, a Component (study section 14, G7:
// controls and components alike); the operation static_casts it to its
// declaring class, which derives from Component non-virtually. A null
// operation means the descriptor's default rule applies, as for a
// PropertyRegistration without it.
struct GUI_FORMS_API PropertyEntry final {
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

// One property a container provides to its direct children (an attached
// property, study sections 10 and 15.4): a child writes it in its own
// `.form` block, and the container stores the value per child. The
// operations take the container as a Component (static_cast to the
// declaring class, as PropertyEntry's do) and the child, which must be a
// live direct child of it. A child property has no change event and no
// origin (Windows Forms raises no per-child event).
struct GUI_FORMS_API ChildPropertyEntry final {
    // The registered (snake_case) name; equal to describe().name.
    std::string_view name;
    // The inert descriptor, built once (property_table_descriptor).
    const PropertyDescriptor& (*describe)();
    // The child's current value; the descriptor's default when none was
    // written.
    BindingValue (*read)(const Component& container, const Control& child);
    // Writes a value already converted to the descriptor's kind.
    void (*write)(Component& container, const Control& child, const BindingValue& value);
    // Restores the default; null: write the descriptor's default value.
    void (*reset)(Component& container, const Control& child);
    // Whether the value is serialized; null: compare with the default.
    bool (*should_serialize)(const Component& container, const Control& child);
};

// Whether the names of `entries` are distinct. Used in a static_assert next
// to each type's table, so a duplicate fails where it is declared, naming
// the table. Entry: PropertyEntry or ChildPropertyEntry.
template <typename Entry, std::size_t Count>
[[nodiscard]] consteval bool property_names_distinct(const std::array<Entry, Count>& entries) {
    for (std::size_t first = 0U; first < Count; ++first) {
        for (std::size_t second = first + 1U; second < Count; ++second) {
            if (entries[first].name == entries[second].name) return false;
        }
    }
    return true;
}

// A declaring class's table after its base's: the base's entries first,
// then its own, so a type's slots are stable and gap-free. Entry:
// PropertyEntry or ChildPropertyEntry.
template <typename Entry, std::size_t BaseCount, std::size_t OwnCount>
[[nodiscard]] constexpr std::array<Entry, BaseCount + OwnCount> join_property_tables(
    const std::array<Entry, BaseCount>& base, const std::array<Entry, OwnCount>& own) {
    std::array<Entry, BaseCount + OwnCount> joined{};
    for (std::size_t index = 0U; index < BaseCount; ++index) joined[index] = base[index];
    for (std::size_t index = 0U; index < OwnCount; ++index) joined[BaseCount + index] = own[index];
    return joined;
}

// A derived type's refinement of base entries (study section 14, G3): the
// base's table with each entry of `overrides` replacing the base entry of
// the same name, in the base entry's slot. An override that names no base
// entry throws, which fails the constant evaluation of the table that uses
// it, so the mistake does not compile. Entry: PropertyEntry or
// ChildPropertyEntry.
template <typename Entry, std::size_t BaseCount, std::size_t OverrideCount>
[[nodiscard]] constexpr std::array<Entry, BaseCount> override_property_entries(
    const std::array<Entry, BaseCount>& base, const std::array<Entry, OverrideCount>& overrides) {
    std::array<Entry, BaseCount> result = base;
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
struct GUI_FORMS_API PropertyOperations final {
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
[[nodiscard]] GUI_FORMS_API PropertyDescriptor property_table_descriptor(PropertyDescriptor descriptor,
                                                           PropertyOperations operations);

// Table access for any property owner (study section 14, G7): the entry
// of `owner`'s table with the registered name `registered`, or null. The
// operations below apply one entry's rules (a null operation's default
// included) and check the values against its descriptor; they do not check
// the owner's thread or liveness, which their callers do (Control's member
// API) and the entries' writes do through their prepare hooks.
[[nodiscard]] GUI_FORMS_API const PropertyEntry* find_property_entry(const Component& owner, std::string_view registered);
// The converted current value; nullopt when the entry is not readable.
// Throws std::logic_error when the read violates the descriptor.
[[nodiscard]] GUI_FORMS_API std::optional<BindingValue> read_property_entry(const Component& owner, const PropertyEntry& entry);
// Converts `value` to the entry's kind and writes it. Throws
// std::logic_error for a read-only entry and std::invalid_argument for a
// value that does not convert.
GUI_FORMS_API void write_property_entry(Component& owner, const PropertyEntry& entry, const BindingValue& value);
// Restores the default; false when the entry has none and no reset.
GUI_FORMS_API bool reset_property_entry(Component& owner, const PropertyEntry& entry);
[[nodiscard]] GUI_FORMS_API bool property_entry_should_serialize(const Component& owner, const PropertyEntry& entry);
[[nodiscard]] GUI_FORMS_API PropertyValueOrigin property_entry_origin(const Component& owner, const PropertyEntry& entry);
// Connects `changed` for `subscriber`'s lifetime; an empty token when the
// entry raises no change or `changed` is empty.
[[nodiscard]] GUI_FORMS_API SubscriptionToken connect_property_entry(Component& owner, const PropertyEntry& entry,
                                                       Component& subscriber, std::function<void()> changed);

// The same operations for one child property. Callers (Control's child
// property API) check the container's thread and that `child` is a live
// direct child; the entries' writes check both again through their
// prepare hooks. The read is converted and checked against the
// descriptor, and the written value converted to its kind, as above.
[[nodiscard]] GUI_FORMS_API BindingValue read_child_property_entry(const Component& container, const Control& child,
                                                     const ChildPropertyEntry& entry);
GUI_FORMS_API void write_child_property_entry(Component& container, const Control& child, const ChildPropertyEntry& entry,
                                const BindingValue& value);
GUI_FORMS_API bool reset_child_property_entry(Component& container, const Control& child, const ChildPropertyEntry& entry);
[[nodiscard]] GUI_FORMS_API bool child_property_entry_should_serialize(const Component& container, const Control& child,
                                                         const ChildPropertyEntry& entry);

} // namespace gui_forms
