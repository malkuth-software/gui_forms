#pragma once

#include <type_traits>
#include <utility>

namespace gui_forms {

// A property's only storage, as the single member of its capability
// (property-capabilities study, section 2; owner decisions: a read is
// `.type()`, a write is a touch, no implicit conversion).
//
//   template <typename Declaring>
//   struct TextCapability {
//       PropertyCell<std::string, TextCapability<Declaring>> text;
//       static void assign(TextCapability& capability, std::string value);
//   };
//   class ButtonBase : public Control, public TextCapability<ButtonBase> {};
//
//   (*button).text = "OK";                       // the write
//   std::string label = (*button).text.type();   // the read
//
// The write calls `Capability::assign`, which compares (an equal value does
// nothing: decision 3), stores through `replace`, touches its control and
// raises the property's change event (decision 2). The cell is the first
// and only non-static member of a standard-layout capability, so the cell
// and its capability are pointer-interconvertible: that is the one raw
// address conversion, in `capability()`, checked by `static_assert`. The
// capability reaches its control with a static_cast to its declaring class.
//
// Value: the property's stored type, copyable or movable and comparable
// with ==. Capability: the class whose only member this cell is. The cell
// has no conversion operator and no ==, so a forgotten `.type()` does not
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
    [[nodiscard]] const Value& type() const noexcept { return value_; }

private:
    friend Capability;

    // Stores `value` when it differs from the stored one. Returns whether
    // it stored. For the capability's assign only.
    bool replace(Value value) {
        if (value_ == value) return false;
        value_ = std::move(value);
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
    [[nodiscard]] Value type() const {
        Value value = Capability::read(capability());
        return value;
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
