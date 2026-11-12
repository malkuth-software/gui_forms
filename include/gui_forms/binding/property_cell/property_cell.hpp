#pragma once

#include <cstdint>

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/binding/type_tag/type_tag.hpp"

#include <optional>
#include <type_traits>
#include <utility>

namespace gui_forms {

namespace detail {
// The process's property-write epoch (S5): advanced by every property cell
// write that stores and every collection cell edit, so that a control's kept
// layout made before a write is never read after it. Atomic: controls on
// different root threads share it.
[[nodiscard]] std::uint64_t property_write_epoch() noexcept;
void advance_property_write_epoch() noexcept;
} // namespace detail

// A property's only storage, as the single member of its capability
// (property-capabilities study, sections 2 and 15; owner decisions: a read
// is `.read()`, a write is a touch, no implicit conversion).
//
// For example, `TextCapability<Declaring>` has the one member
// `PropertyCell<std::string, TextCapability<Declaring>> text` and a static
// `assign(TextCapability& capability, std::string value)`, and `ButtonBase`
// derives from `Control` and `TextCapability<ButtonBase>`. Then
// `(*button).text = "OK"` is the write and `(*button).text.read()` the read.
// (The generated API pages join comment lines, so the example stays in code
// spans: a bare `*` there opens reStructuredText emphasis.)
//
// The write calls `Capability::assign`, which compares (an equal value does
// nothing: decision 3), stores through `replace`, touches its control and
// raises the property's change event (decision 2). The cell is the first
// and only non-static member of a standard-layout capability, so the cell
// and its capability are pointer-interconvertible: that is the one raw
// address conversion, in `capability()`, checked by `static_assert`. The
// capability reaches its control with a static_cast to its declaring class.
//
// Beside the read: `type()` (the runtime PropertyType), `as_type(types::x)`
// (a total conversion; a pair that can fail is a compile error) and
// `as_type(PropertyType)` (any conversion, as an explicit optional result).
// These three need a PropertyValueTraits entry for Value.
//
// Value: the property's stored type, copyable or movable and comparable
// with ==. Capability: the class whose only member this cell is. The cell
// has no conversion operator and no ==, so a forgotten `.read()` does not
// compile, and it cannot be copied: a property is part of its control.
template <typename Value, typename Capability>
class PropertyCell final {
public:
    PropertyCell() : value_() {}
    explicit PropertyCell(Value initial) : value_(std::move(initial)) {}
    PropertyCell(const PropertyCell&) = delete;
    PropertyCell& operator=(const PropertyCell&) = delete;

    // The write: everything else is the capability's.
    PropertyCell& operator=(Value value) {
        Capability::assign(capability(), std::move(value));
        return *this;
    }

    // The read.
    [[nodiscard]] const Value& read() const noexcept { return value_; }

    // The runtime type (PropertyValueTraits<Value>).
    [[nodiscard]] PropertyType type() const {
        PropertyType described = PropertyValueTraits<Value>::type();
        return described;
    }

    // A total conversion, named by a tag: `as_type(types::string)`. A pair
    // that can fail does not compile; use the descriptor overload.
    template <typename Target>
        requires detail::total_property_conversion<Value, Target>
    [[nodiscard]] Target as_type(TypeTag<Target>) const {
        Target converted = detail::convert_property_total<Target, Value>(value_);
        return converted;
    }

    // A conversion to a runtime type: nullopt when the value has no form of
    // that type (convert_to_property_type).
    [[nodiscard]] std::optional<BindingValue> as_type(const PropertyType& target) const {
        const BindingValue general = PropertyValueTraits<Value>::to_binding(value_);
        std::optional<BindingValue> converted = convert_to_property_type(general, target);
        return converted;
    }

private:
    friend Capability;

    // Stores `value` when it differs from the stored one. Returns whether
    // it stored. For the capability's assign only.
    bool replace(Value value) {
        if (value_ == value) return false;
        value_ = std::move(value);
        detail::advance_property_write_epoch();
        return true;
    }

    Capability& capability() noexcept {
        static_assert(std::is_standard_layout_v<Capability>,
                      "a property capability is standard layout, with its cell as its only member");
        // Defined: a standard-layout class and its first non-static member
        // are pointer-interconvertible.
        Capability& owner = *reinterpret_cast<Capability*>(this);
        return owner;
    }

    Value value_;
};

// A property whose value has another owner (study section 14, G2: an
// editing engine's text, a selection derived from item state): the
// capability's only member, with PropertyCell's spelling and no storage.
// The write calls `Capability::assign`, which writes the owner; the read
// calls `Capability::read`, which returns the owner's value by value.
//
//   template <typename Declaring>
//   struct EditedTextCapability {
//       PropertyView<std::string, EditedTextCapability<Declaring>> text;
//       static void assign(EditedTextCapability& capability, std::string value);
//       static std::string read(const EditedTextCapability& capability);
//   };
//
// Like a cell, a view has no conversion operator and no ==, and it cannot
// be copied. It is empty, and as its capability's first and only member it
// is pointer-interconvertible with the capability, as a cell is.
template <typename Value, typename Capability>
class PropertyView final {
public:
    PropertyView() = default;
    PropertyView(const PropertyView&) = delete;
    PropertyView& operator=(const PropertyView&) = delete;

    // The write: everything is the capability's.
    PropertyView& operator=(Value value) {
        Capability::assign(capability(), std::move(value));
        return *this;
    }

    // The read, through the owner.
    [[nodiscard]] Value read() const {
        Value value = Capability::read(capability());
        return value;
    }

    // The runtime type, a total conversion and a runtime conversion, as on
    // PropertyCell; each reads the owner once.
    [[nodiscard]] PropertyType type() const {
        PropertyType described = PropertyValueTraits<Value>::type();
        return described;
    }

    template <typename Target>
        requires detail::total_property_conversion<Value, Target>
    [[nodiscard]] Target as_type(TypeTag<Target>) const {
        const Value value = read();
        Target converted = detail::convert_property_total<Target, Value>(value);
        return converted;
    }

    [[nodiscard]] std::optional<BindingValue> as_type(const PropertyType& target) const {
        const Value value = read();
        const BindingValue general = PropertyValueTraits<Value>::to_binding(value);
        std::optional<BindingValue> converted = convert_to_property_type(general, target);
        return converted;
    }

private:
    Capability& capability() noexcept {
        static_assert(std::is_standard_layout_v<Capability>,
                      "a property capability is standard layout, with its view as its only member");
        // Defined: a standard-layout class and its first non-static member
        // are pointer-interconvertible.
        Capability& owner = *reinterpret_cast<Capability*>(this);
        return owner;
    }
    const Capability& capability() const noexcept {
        static_assert(std::is_standard_layout_v<Capability>,
                      "a property capability is standard layout, with its view as its only member");
        const Capability& owner = *reinterpret_cast<const Capability*>(this);
        return owner;
    }
};

} // namespace gui_forms
