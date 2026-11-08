#pragma once

#include "gui_forms/binding/child_property_view/child_property_view.hpp"
#include "gui_forms/binding/property_cell/property_cell.hpp"
#include "gui_forms/binding/property_table/property_table.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"

#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace gui_forms {

// The table operations of a property held in a capability's cell or view
// (study sections 14 and 15.2): plain functions a PropertyEntry points at,
// one instantiation per declaring class and property. The family codemod
// (tools/property_codemod.py) writes the entries. `Member` is a pointer to
// the cell or view member of the capability, named through the declaring
// class (`&ButtonBase::text_alignment`), after its type `MemberPointer`
// (`decltype(&ButtonBase::text_alignment)`); `Declaring` derives from the
// capability, so the owner reaches the cell with `.*`. Values travel
// through PropertyValueTraits, and a write is the cell's own assignment, so
// the capability's prepare and written hooks run as for any write.

template <typename MemberPointer>
struct CellMemberValue;

template <typename Value, typename Capability, typename Holder>
struct CellMemberValue<PropertyCell<Value, Capability> Holder::*> {
    using Type = Value;
};

template <typename Value, typename Capability, typename Holder>
struct CellMemberValue<PropertyView<Value, Capability> Holder::*> {
    using Type = Value;
};

// The value type of a cell, view or child view, named by its type (with
// or without a reference: `decltype((*control).text)`).
template <typename Member>
struct AssignedMemberValue;

template <typename Value, typename Capability>
struct AssignedMemberValue<PropertyCell<Value, Capability>> {
    using Type = Value;
};

template <typename Value, typename Capability>
struct AssignedMemberValue<PropertyView<Value, Capability>> {
    using Type = Value;
};

template <typename Value, typename Capability>
struct AssignedMemberValue<ChildPropertyView<Value, Capability>> {
    using Type = Value;
};

// `value` as the value of the cell, view or child view `Member` (its
// declared type, as `decltype((*control).text)` spells it): converted to the
// member's PropertyType with the property conversion rules, then through
// PropertyValueTraits, as write_cell_entry converts a table write. Generated
// forms assign a property with it (CLOSURE P-6), as in
// `(*made.ok).text = gui_forms::assigned_property_value<decltype((*made.ok).text)>(gui_forms::BindingValue{std::string("OK")});`.
//
// Throws std::invalid_argument when `value` has no form of the member's
// type, or what the traits throw for a value outside a narrower type.
template <typename Member>
[[nodiscard]] typename AssignedMemberValue<std::remove_cvref_t<Member>>::Type assigned_property_value(
    const BindingValue& value) {
    using Value = typename AssignedMemberValue<std::remove_cvref_t<Member>>::Type;
    const PropertyType target = PropertyValueTraits<Value>::type();
    const std::optional<BindingValue> converted = convert_to_property_type(value, target);
    if (!converted) throw std::invalid_argument("GUI.Forms: the value has no form of the property's type");
    Value typed = PropertyValueTraits<Value>::from_binding(*converted);
    return typed;
}

// The current value in its BindingValue form.
template <typename Declaring, typename MemberPointer, MemberPointer Member>
[[nodiscard]] BindingValue read_cell_entry(const Component& owner) {
    using Value = typename CellMemberValue<MemberPointer>::Type;
    const Declaring& declaring = static_cast<const Declaring&>(owner);
    const Value current = (declaring.*Member).read();
    BindingValue general = PropertyValueTraits<Value>::to_binding(current);
    return general;
}

// Writes a value write_property_entry has already converted to the
// descriptor's kind.
template <typename Declaring, typename MemberPointer, MemberPointer Member>
void write_cell_entry(Component& owner, const BindingValue& value) {
    using Value = typename CellMemberValue<MemberPointer>::Type;
    Declaring& declaring = static_cast<Declaring&>(owner);
    Value typed = PropertyValueTraits<Value>::from_binding(value);
    (declaring.*Member) = std::move(typed);
}

// Connects `changed` to the event field `EventMember` of the declaring
// class for `subscriber`'s lifetime.
template <typename Declaring, typename EventPointer, EventPointer EventMember>
[[nodiscard]] SubscriptionToken connect_event_entry(Component& owner, Component& subscriber,
                                                    std::function<void()> changed) {
    Declaring& declaring = static_cast<Declaring&>(owner);
    const detail::EventChangeConnector connector(declaring.*EventMember);
    SubscriptionToken token = connector(subscriber, std::move(changed));
    return token;
}

// A declaring-class member function as an entry's read
// (`BindingValue f() const`), where the table read is not the cell's value.
template <typename Declaring, typename MemberPointer, MemberPointer Read>
[[nodiscard]] BindingValue read_member_entry(const Component& owner) {
    const Declaring& declaring = static_cast<const Declaring&>(owner);
    BindingValue current = (declaring.*Read)();
    return current;
}

// A declaring-class member function as an entry's write
// (`void f(const BindingValue&)`).
template <typename Declaring, typename MemberPointer, MemberPointer Write>
void write_member_entry(Component& owner, const BindingValue& value) {
    Declaring& declaring = static_cast<Declaring&>(owner);
    (declaring.*Write)(value);
}

// A declaring-class member function as an entry's reset.
template <typename Declaring, typename MemberPointer, MemberPointer Reset>
void reset_member_entry(Component& owner) {
    Declaring& declaring = static_cast<Declaring&>(owner);
    (declaring.*Reset)();
}

// A declaring-class member function as an entry's should_serialize.
template <typename Declaring, typename MemberPointer, MemberPointer ShouldSerialize>
[[nodiscard]] bool should_serialize_member_entry(const Component& owner) {
    const Declaring& declaring = static_cast<const Declaring&>(owner);
    const bool serialize = (declaring.*ShouldSerialize)();
    return serialize;
}

// A declaring-class member function as an entry's origin
// (`PropertyValueOrigin f() const noexcept`).
template <typename Declaring, typename MemberPointer, MemberPointer Origin>
[[nodiscard]] PropertyValueOrigin origin_member_entry(const Component& owner) {
    const Declaring& declaring = static_cast<const Declaring&>(owner);
    const PropertyValueOrigin origin = (declaring.*Origin)();
    return origin;
}

} // namespace gui_forms
