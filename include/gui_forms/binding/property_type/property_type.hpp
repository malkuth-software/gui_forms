#pragma once

#include "gui_forms/binding/type_tag/type_tag.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace gui_forms {

// A property's runtime type (study section 15; numpy's dtype): the binding
// kind, whether empty is a value, and an enumeration's finite choice set.
// `(*c).visible.type()` returns it; `as_type(type)` converts to it.
struct PropertyType final {
    BindingValueKind kind{BindingValueKind::null};
    bool nullable{};
    std::shared_ptr<const PropertyEnumDescriptor> enumeration;
};

// Converts `value` to `target` with the property conversion rules
// (convert_property_value: the one conversion table). Nullopt when the
// value has no form of that type.
[[nodiscard]] std::optional<BindingValue> convert_to_property_type(const BindingValue& value,
                                                                   const PropertyType& target);

// One specialization per property value type: its PropertyType (type())
// and its BindingValue form (to_binding()). There is no primary definition,
// so a value type without an entry still works as a cell's storage but not
// with type() or as_type(). Enumerations' entries live beside their
// declaring class (their choice sets are its descriptors).
template <typename Value>
struct PropertyValueTraits;

template <>
struct PropertyValueTraits<bool> {
    static PropertyType type() { return PropertyType{BindingValueKind::boolean, false, nullptr}; }
    static BindingValue to_binding(bool value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<std::int32_t> {
    static PropertyType type() { return PropertyType{BindingValueKind::signed_integer, false, nullptr}; }
    static BindingValue to_binding(std::int32_t value) { return BindingValue{std::int64_t{value}}; }
};

template <>
struct PropertyValueTraits<std::int64_t> {
    static PropertyType type() { return PropertyType{BindingValueKind::signed_integer, false, nullptr}; }
    static BindingValue to_binding(std::int64_t value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<std::uint32_t> {
    static PropertyType type() { return PropertyType{BindingValueKind::unsigned_integer, false, nullptr}; }
    static BindingValue to_binding(std::uint32_t value) { return BindingValue{std::uint64_t{value}}; }
};

template <>
struct PropertyValueTraits<std::uint64_t> {
    static PropertyType type() { return PropertyType{BindingValueKind::unsigned_integer, false, nullptr}; }
    static BindingValue to_binding(std::uint64_t value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<float> {
    static PropertyType type() { return PropertyType{BindingValueKind::number, false, nullptr}; }
    static BindingValue to_binding(float value) { return BindingValue{static_cast<double>(value)}; }
};

template <>
struct PropertyValueTraits<double> {
    static PropertyType type() { return PropertyType{BindingValueKind::number, false, nullptr}; }
    static BindingValue to_binding(double value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<std::string> {
    static PropertyType type() { return PropertyType{BindingValueKind::text, false, nullptr}; }
    static BindingValue to_binding(const std::string& value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<Point> {
    static PropertyType type() { return PropertyType{BindingValueKind::point, false, nullptr}; }
    static BindingValue to_binding(Point value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<Size> {
    static PropertyType type() { return PropertyType{BindingValueKind::size, false, nullptr}; }
    static BindingValue to_binding(Size value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<Rect> {
    static PropertyType type() { return PropertyType{BindingValueKind::rectangle, false, nullptr}; }
    static BindingValue to_binding(Rect value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<Insets> {
    static PropertyType type() { return PropertyType{BindingValueKind::insets, false, nullptr}; }
    static BindingValue to_binding(Insets value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<Color> {
    static PropertyType type() { return PropertyType{BindingValueKind::color, false, nullptr}; }
    static BindingValue to_binding(Color value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<FontSpec> {
    static PropertyType type() { return PropertyType{BindingValueKind::font, false, nullptr}; }
    static BindingValue to_binding(const FontSpec& value) { return BindingValue{value}; }
};

template <>
struct PropertyValueTraits<ImageId> {
    static PropertyType type() { return PropertyType{BindingValueKind::image, false, nullptr}; }
    static BindingValue to_binding(ImageId value) { return BindingValue{value}; }
};

// Milliseconds travel as a signed count.
template <>
struct PropertyValueTraits<std::chrono::milliseconds> {
    static PropertyType type() { return PropertyType{BindingValueKind::signed_integer, false, nullptr}; }
    static BindingValue to_binding(std::chrono::milliseconds value) {
        return BindingValue{static_cast<std::int64_t>(value.count())};
    }
};

// An optional value is its value's type, nullable; empty is null.
template <typename Value>
struct PropertyValueTraits<std::optional<Value>> {
    static PropertyType type() {
        PropertyType described = PropertyValueTraits<Value>::type();
        described.nullable = true;
        return described;
    }
    static BindingValue to_binding(const std::optional<Value>& value) {
        if (!value) return BindingValue{};
        const BindingValue present = PropertyValueTraits<Value>::to_binding(*value);
        return present;
    }
};

namespace types {

inline constexpr TypeTag<bool> boolean{};
inline constexpr TypeTag<std::int32_t> int32{};
inline constexpr TypeTag<std::int64_t> int64{};
inline constexpr TypeTag<std::uint32_t> uint32{};
inline constexpr TypeTag<std::uint64_t> uint64{};
inline constexpr TypeTag<float> float32{};
inline constexpr TypeTag<double> float64{};
inline constexpr TypeTag<std::string> string{};
inline constexpr TypeTag<Point> point{};
inline constexpr TypeTag<Size> size{};
inline constexpr TypeTag<Rect> rect{};
inline constexpr TypeTag<Insets> insets{};
inline constexpr TypeTag<Color> color{};
inline constexpr TypeTag<FontSpec> font_spec{};
inline constexpr TypeTag<ImageId> image_id{};
inline constexpr TypeTag<std::chrono::milliseconds> milliseconds{};
inline constexpr TypeTag<BindingValue> binding_value{};
inline constexpr TypeTag<std::optional<Color>> optional_color{};

} // namespace types

namespace detail {

template <typename Value>
inline constexpr bool property_integer = std::is_same_v<Value, std::int32_t> ||
                                         std::is_same_v<Value, std::int64_t> ||
                                         std::is_same_v<Value, std::uint32_t> ||
                                         std::is_same_v<Value, std::uint64_t>;

// Whether From converts to To without a possible failure (study section
// 15): the identity, widening integers, an integer to float64, anything to
// string or to binding_value, an enumeration to int64.
template <typename From, typename To>
inline constexpr bool total_property_conversion =
    std::is_same_v<From, To> ||
    (std::is_same_v<From, std::int32_t> && std::is_same_v<To, std::int64_t>) ||
    (std::is_same_v<From, std::uint32_t> && std::is_same_v<To, std::uint64_t>) ||
    (std::is_same_v<From, std::uint32_t> && std::is_same_v<To, std::int64_t>) ||
    (property_integer<From> && std::is_same_v<To, double>) ||
    std::is_same_v<To, std::string> || std::is_same_v<To, BindingValue> ||
    (std::is_enum_v<From> && std::is_same_v<To, std::int64_t>);

// The tag conversion: the same results as the descriptor conversion of the
// pair (property_type_tests compares them), without building a
// BindingValue where a cast is the rule.
template <typename To, typename From>
[[nodiscard]] To convert_property_total(const From& value) {
    static_assert(total_property_conversion<From, To>,
                  "this conversion can fail: use as_type(PropertyType), which returns an explicit result");
    if constexpr (std::is_same_v<From, To>) {
        return value;
    } else if constexpr (std::is_same_v<To, BindingValue>) {
        BindingValue converted = PropertyValueTraits<From>::to_binding(value);
        return converted;
    } else if constexpr (std::is_same_v<To, std::string>) {
        const BindingValue general = PropertyValueTraits<From>::to_binding(value);
        std::string text = binding_value_to_string(general);
        return text;
    } else if constexpr (std::is_enum_v<From>) {
        const std::int64_t number = static_cast<std::int64_t>(value);
        return number;
    } else {
        const To widened = static_cast<To>(value);
        return widened;
    }
}

} // namespace detail

} // namespace gui_forms
