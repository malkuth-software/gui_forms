#pragma once

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/binding/type_tag/type_tag.hpp"

#include <optional>
#include <type_traits>
#include <utility>

namespace gui_forms {

class Control;

// A property a container provides to its direct children (an attached
// property: study sections 10 and 15.4), as the single member of its
// capability. The container owns the values, one per child, keyed by the
// child's runtime identity; the view has no storage. Subscripting by a
// child yields a borrow of that child's value for one expression:
//
//   template <typename Declaring>
//   struct FlowBreakCapability {
//       ChildPropertyView<bool, FlowBreakCapability<Declaring>> flow_break;
//       static void assign(FlowBreakCapability& capability, const Control& child, bool value);
//       static bool read(const FlowBreakCapability& capability, const Control& child);
//   };
//
// For example, `(*flow).flow_break[*button] = true` is the write and
// `(*flow).flow_break[*button].read()` the read.
//
// The write calls `Capability::assign`, which checks the container's thread
// and that `child` is a live direct child, compares with the child's value
// (an equal value does nothing), stores in the container and touches the
// container. The read calls `Capability::read`, which refuses a child that
// is not a live direct child and answers the default for a child with no
// value written. A borrow cannot be copied, kept or written twice: its
// write is rvalue-qualified and returns nothing. Like a cell, neither the
// view nor a borrow has a conversion operator or ==, so a forgotten
// `.read()` does not compile.
template <typename Value, typename Capability>
class ChildPropertyView final {
public:
    // One child's value, writable: borrows the capability and the child.
    class Borrow final {
    public:
        Borrow(const Borrow&) = delete;
        Borrow& operator=(const Borrow&) = delete;

        // The write.
        void operator=(Value value) && { Capability::assign(capability_, child_, std::move(value)); }

        // The read.
        [[nodiscard]] Value read() const {
            Value value = Capability::read(std::as_const(capability_), child_);
            return value;
        }

        [[nodiscard]] PropertyType type() const {
            PropertyType described = PropertyValueTraits<Value>::type();
            return described;
        }

        // A total conversion, named by a tag (as on PropertyCell).
        template <typename Target>
            requires detail::total_property_conversion<Value, Target>
        [[nodiscard]] Target as_type(TypeTag<Target>) const {
            const Value value = read();
            Target converted = detail::convert_property_total<Target, Value>(value);
            return converted;
        }

        // A conversion to a runtime type: nullopt when the value has no
        // form of that type.
        [[nodiscard]] std::optional<BindingValue> as_type(const PropertyType& target) const {
            const Value value = read();
            const BindingValue general = PropertyValueTraits<Value>::to_binding(value);
            std::optional<BindingValue> converted = convert_to_property_type(general, target);
            return converted;
        }

    private:
        friend class ChildPropertyView;

        Borrow(Capability& capability, const Control& child) noexcept : capability_(capability), child_(child) {}

        Capability& capability_;
        const Control& child_;
    };

    // One child's value through a const container: the reads only.
    class Reading final {
    public:
        Reading(const Reading&) = delete;
        Reading& operator=(const Reading&) = delete;

        [[nodiscard]] Value read() const {
            Value value = Capability::read(capability_, child_);
            return value;
        }

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
        friend class ChildPropertyView;

        Reading(const Capability& capability, const Control& child) noexcept
            : capability_(capability), child_(child) {}

        const Capability& capability_;
        const Control& child_;
    };

    ChildPropertyView() = default;
    ChildPropertyView(const ChildPropertyView&) = delete;
    ChildPropertyView& operator=(const ChildPropertyView&) = delete;

    // The borrow of `child`'s value; valid for the expression that names
    // it, while the container and the child live. A borrow can be neither
    // copied nor moved, so it is returned as a prvalue (guaranteed elision).
    [[nodiscard]] Borrow operator[](const Control& child) noexcept { return Borrow(capability(), child); }
    [[nodiscard]] Reading operator[](const Control& child) const noexcept { return Reading(capability(), child); }

private:
    Capability& capability() noexcept {
        static_assert(std::is_standard_layout_v<Capability>,
                      "a child property capability is standard layout, with its view as its only member");
        // Defined: a standard-layout class and its first non-static member
        // are pointer-interconvertible.
        Capability& owner = *reinterpret_cast<Capability*>(this);
        return owner;
    }
    const Capability& capability() const noexcept {
        static_assert(std::is_standard_layout_v<Capability>,
                      "a child property capability is standard layout, with its view as its only member");
        const Capability& owner = *reinterpret_cast<const Capability*>(this);
        return owner;
    }
};

} // namespace gui_forms
