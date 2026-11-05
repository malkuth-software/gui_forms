#pragma once

#include "gui_forms/component.hpp"
#include "gui_forms/binding/value/binding_value_kind/binding_value_kind.hpp"
#include "gui_forms/binding/value/property_collection_value/property_collection_value.hpp"
#include "gui_forms/binding/value/property_object_value/property_object_value.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms {

struct PropertyEnumValue final {
    std::string type_name;
    std::string name;
    std::int64_t value{};
    friend bool operator==(const PropertyEnumValue& left,
                           const PropertyEnumValue& right) noexcept(noexcept(
        left.type_name == right.type_name && left.name == right.name &&
        left.value == right.value)) {
        return left.type_name == right.type_name && left.name == right.name &&
               left.value == right.value;
    }
};

struct PropertyObjectMember;
struct PropertyObjectData;
struct PropertyCollectionData;
struct PropertyEnumDescriptor;

using BindingValue = std::variant<std::monostate, bool, std::int64_t,
                                  std::uint64_t, double, std::string,
                                  Point, Size, Rect, Insets, Color, FontSpec,
                                  ImageId, PropertyEnumValue,
                                  PropertyObjectValue, PropertyCollectionValue>;

struct PropertyObjectMember final {
    std::string name;
    std::string description;
    BindingValue value;
    bool writable{true};
    // Optional schema is required when a member's current value is null and
    // may also pin conversion/editor services independently of its owner.
    std::optional<BindingValueKind> declared_kind;
    bool nullable{};
    std::shared_ptr<const PropertyEnumDescriptor> enumeration;
    std::vector<BindingValue> standard_values;
    bool standard_values_exclusive{};
    std::string converter_name;
    std::string editor_name;
    friend bool operator==(const PropertyObjectMember& left,
                           const PropertyObjectMember& right) noexcept(noexcept(
        left.name == right.name && left.description == right.description &&
        left.value == right.value && left.writable == right.writable &&
        left.declared_kind == right.declared_kind &&
        left.nullable == right.nullable &&
        left.enumeration == right.enumeration &&
        left.standard_values == right.standard_values &&
        left.standard_values_exclusive == right.standard_values_exclusive &&
        left.converter_name == right.converter_name &&
        left.editor_name == right.editor_name)) {
        return left.name == right.name &&
               left.description == right.description &&
               left.value == right.value && left.writable == right.writable &&
               left.declared_kind == right.declared_kind &&
               left.nullable == right.nullable &&
               left.enumeration == right.enumeration &&
               left.standard_values == right.standard_values &&
               left.standard_values_exclusive ==
                   right.standard_values_exclusive &&
               left.converter_name == right.converter_name &&
               left.editor_name == right.editor_name;
    }
};

struct PropertyObjectData final {
    std::string type_name;
    std::vector<PropertyObjectMember> members;
};

struct PropertyCollectionData final {
    std::string item_type_name;
    BindingValueKind item_kind{BindingValueKind::text};
    std::vector<BindingValue> items;
};

inline constexpr std::size_t maximum_property_value_depth = 8U;
inline constexpr std::size_t maximum_property_object_members = 256U;
inline constexpr std::size_t maximum_property_collection_items = 4096U;
inline constexpr std::size_t maximum_property_value_nodes = 8192U;

[[nodiscard]] PropertyObjectValue make_property_object(
    std::string type_name, std::vector<PropertyObjectMember> members);
[[nodiscard]] PropertyCollectionValue make_property_collection(
    std::string item_type_name, BindingValueKind item_kind,
    std::vector<BindingValue> items);
[[nodiscard]] std::span<const BindingValue> property_collection_items(
    const PropertyCollectionValue& value) noexcept;
[[nodiscard]] bool valid_property_value_tree(const BindingValue& value) noexcept;

[[nodiscard]] BindingValueKind binding_value_kind(
    const BindingValue& value) noexcept;
[[nodiscard]] std::string binding_value_to_string(const BindingValue& value);
[[nodiscard]] std::optional<bool> binding_value_to_bool(
    const BindingValue& value) noexcept;
[[nodiscard]] std::optional<std::int64_t> binding_value_to_signed(
    const BindingValue& value) noexcept;
[[nodiscard]] std::optional<std::uint64_t> binding_value_to_unsigned(
    const BindingValue& value) noexcept;
[[nodiscard]] std::optional<double> binding_value_to_number(
    const BindingValue& value) noexcept;
[[nodiscard]] std::optional<BindingValue> convert_binding_value(
    const BindingValue& value, BindingValueKind target_kind);

enum class PropertySerializationVisibility : std::uint8_t {
    hidden,
    visible,
    content,
};

// The current value's authorship is distinct from ShouldSerialize. An
// inherited or ambient value can differ from a default without becoming a
// local assignment.
enum class PropertyValueOrigin : std::uint8_t {
    defaulted,
    local,
    inherited,
    ambient,
    computed,
};

[[nodiscard]] std::string_view property_value_origin_name(
    PropertyValueOrigin origin) noexcept;

struct PropertyEnumChoice final {
    std::string name;
    std::int64_t value{};
    friend bool operator==(const PropertyEnumChoice& left,
                           const PropertyEnumChoice& right) noexcept(noexcept(
        left.name == right.name && left.value == right.value)) {
        return left.name == right.name && left.value == right.value;
    }
};

struct PropertyEnumDescriptor final {
    std::string type_name;
    std::vector<PropertyEnumChoice> choices;
    bool flags{};
    friend bool operator==(const PropertyEnumDescriptor& left,
                           const PropertyEnumDescriptor& right) noexcept(noexcept(
        left.type_name == right.type_name && left.choices == right.choices &&
        left.flags == right.flags)) {
        return left.type_name == right.type_name &&
               left.choices == right.choices && left.flags == right.flags;
    }
};

inline constexpr std::size_t maximum_property_enum_choices = 256U;
inline constexpr std::size_t maximum_property_enum_text_bytes = 256U;
inline constexpr std::size_t maximum_property_standard_values = 256U;

[[nodiscard]] bool valid_property_enum_descriptor(
    const PropertyEnumDescriptor& descriptor) noexcept;

// Renderer- and language-neutral metadata for one retained property. This is
// deliberately a value snapshot: inspection/DML/designer consumers never gain
// access to a control's executable callbacks.
struct PropertyDescriptor final {
    PropertyDescriptor() = default;
    PropertyDescriptor(std::string authored_name, BindingValueKind value_kind,
                       std::string authored_category,
                       std::string authored_description,
                       std::optional<BindingValue> authored_default,
                       Dirty authored_effects,
                       bool authored_subtree_effect = false)
        : name(std::move(authored_name)), kind(value_kind),
          category(std::move(authored_category)),
          description(std::move(authored_description)),
          default_value(std::move(authored_default)),
          invalidation_effects(authored_effects),
          invalidates_subtree(authored_subtree_effect) {}

    std::string name;
    BindingValueKind kind{BindingValueKind::text};
    std::string category{"Behavior"};
    std::string description;
    std::optional<BindingValue> default_value;
    Dirty invalidation_effects{Dirty::none};
    bool invalidates_subtree{};
    PropertySerializationVisibility serialization_visibility{
        PropertySerializationVisibility::visible};
    bool readable{true};
    bool writable{true};
    bool browsable{true};
    bool bindable{true};
    bool resettable{};
    bool change_notifications{};
    // `kind` is the non-null payload kind. A nullable property may hold the
    // ordinary BindingValue monostate without weakening that declared schema.
    bool nullable{};
    // Whether the value is the control's content although its kind is a
    // presentation kind (colour, font): a typography specimen's font is
    // what it shows. A form document may set such a property; it refuses
    // other colour and font properties (form_presentation_property) and
    // leaves them to the project theme (owner ruling of 2026-11-04).
    bool content_value{};
    std::shared_ptr<const PropertyEnumDescriptor> enumeration;
    // Static standard values are inert typed snapshots. Dynamic/contextual
    // providers remain named services rather than executable descriptor data.
    std::vector<BindingValue> standard_values;
    bool standard_values_exclusive{};
    std::string standard_values_provider_name;
    // Stable, language-neutral service names. Descriptors remain inert: the
    // executable converter/editor factories live in an inspection service
    // registry owned by the consumer (for example PropertyGrid), never in the
    // property snapshot itself.
    std::string converter_name;
    std::string editor_name;
};

// Descriptor-aware conversion is the property/DML seam. In particular, enum
// names and flags require their declared finite choice set; the scalar helper
// above intentionally has no authority to guess an enum type.
[[nodiscard]] std::optional<BindingValue> convert_property_value(
    const BindingValue& value, const PropertyDescriptor& descriptor);

// A custom retained control registers these executable definitions once
// during construction. Binding never uses C++ RTTI, native handles, or managed
// reflection: name resolution yields explicit typed get/set/change behavior.
// Public inspection receives only the PropertyDescriptor above.
struct PropertyRegistration final {
    using Getter = std::function<BindingValue()>;
    using Setter = std::function<void(const BindingValue&)>;
    using ChangeConnector = std::function<SubscriptionToken(
        Component&, std::function<void()>)>;
    using Resetter = std::function<void()>;
    using ShouldSerialize = std::function<bool()>;
    using OriginProvider = std::function<PropertyValueOrigin()>;

    PropertyRegistration() = default;
    PropertyRegistration(PropertyDescriptor authored_descriptor,
                         Getter authored_get,
                         Setter authored_set,
                         ChangeConnector authored_change = {},
                         Resetter authored_reset = {},
                         ShouldSerialize authored_should_serialize = {},
                         OriginProvider authored_origin = {})
        : descriptor(std::move(authored_descriptor)),
          get(std::move(authored_get)), set(std::move(authored_set)),
          connect_changed(std::move(authored_change)),
          reset(std::move(authored_reset)),
          should_serialize(std::move(authored_should_serialize)),
          origin(std::move(authored_origin)) {}

    PropertyDescriptor descriptor;
    Getter get;
    Setter set;
    ChangeConnector connect_changed;
    Resetter reset;
    ShouldSerialize should_serialize;
    OriginProvider origin;
};

// Compatibility spelling retained for 0.x custom controls. New code should
// use PropertyRegistration; this alias does not expose callbacks through the
// Control inspection API.
using BindableProperty = PropertyRegistration;

// Data members, service names and categories compare ignoring case.
[[nodiscard]] std::string canonical_binding_name(std::string_view name);

// Property names are registered names (gui_forms/schema): one stored
// snake_case spelling, found by any of its three spellings through
// registered_name().

} // namespace gui_forms
