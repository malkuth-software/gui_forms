#pragma once

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/event.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace gui_forms::detail {

// Named property-registration adapters. Construction names the exact object,
// member, value type, and conversion policy; the templates only perform that
// already-authored decision for std::function-based registration seams.
template <typename Object, typename Value>
class BindingMemberGetter final {
public:
    using Member = Value Object::*;

    BindingMemberGetter(const Object& object, Member member) noexcept
        : object_(&object), member_(member) {}

    BindingValue operator()() const {
        return BindingValue{(*object_).*member_};
    }

private:
    const Object* object_{};
    Member member_{};
};

template <typename Object, typename Value>
class BindingMethodGetter final {
public:
    using Method = Value (Object::*)() const;

    BindingMethodGetter(const Object& object, Method method) noexcept
        : object_(&object), method_(method) {}

    BindingValue operator()() const {
        return BindingValue{((*object_).*method_)()};
    }

private:
    const Object* object_{};
    Method method_{};
};

template <typename Object, typename Value>
class BindingNoexceptMethodGetter final {
public:
    using Method = Value (Object::*)() const noexcept;

    BindingNoexceptMethodGetter(const Object& object, Method method) noexcept
        : object_(&object), method_(method) {}

    BindingValue operator()() const {
        return BindingValue{((*object_).*method_)()};
    }

private:
    const Object* object_{};
    Method method_{};
};

template <typename Object, typename Value>
class ConvertedPropertySetter final {
public:
    using Method = void (Object::*)(Value);

    ConvertedPropertySetter(Object& object, Method method,
                            BindingValueKind kind, const char* error) noexcept
        : object_(&object), method_(method), kind_(kind), error_(error) {}

    void operator()(const BindingValue& value) const {
        const std::optional<BindingValue> converted =
            convert_binding_value(value, kind_);
        if (!converted) throw std::invalid_argument(error_);
        ((*object_).*method_)(std::get<Value>(*converted));
    }

private:
    Object* object_{};
    Method method_{};
    BindingValueKind kind_{};
    const char* error_{};
};

template <typename Object, typename Value>
class DirectPropertySetter final {
public:
    using Method = void (Object::*)(Value);

    DirectPropertySetter(Object& object, Method method) noexcept
        : object_(&object), method_(method) {}

    void operator()(const BindingValue& value) const {
        ((*object_).*method_)(std::get<Value>(value));
    }

private:
    Object* object_{};
    Method method_{};
};

// Reads an enumerated property through its getter as one of the
// enumeration's choices; a choice's value is the C++ enumerator's underlying
// value.
template <typename Object, typename Enumeration>
class EnumerationPropertyGetter final {
public:
    using Method = Enumeration (Object::*)() const noexcept;

    EnumerationPropertyGetter(const Object& object, Method method,
                              std::shared_ptr<const PropertyEnumDescriptor> enumeration) noexcept
        : object_(&object), method_(method), enumeration_(std::move(enumeration)) {}

    BindingValue operator()() const {
        PropertyDescriptor descriptor;
        descriptor.kind = BindingValueKind::enumeration;
        descriptor.enumeration = enumeration_;
        const std::int64_t value = static_cast<std::int64_t>(((*object_).*method_)());
        const std::optional<BindingValue> normalized =
            convert_property_value(BindingValue{value}, descriptor);
        if (!normalized) {
            throw std::logic_error("GUI.Forms retained enumerator is outside its property enumeration");
        }
        return *normalized;
    }

private:
    const Object* object_{};
    Method method_{};
    std::shared_ptr<const PropertyEnumDescriptor> enumeration_;
};

// Writes an enumerated property's choice, already validated against its
// enumeration, through the setter.
template <typename Object, typename Enumeration>
class EnumerationPropertySetter final {
public:
    using Method = void (Object::*)(Enumeration);

    EnumerationPropertySetter(Object& object, Method method) noexcept
        : object_(&object), method_(method) {}

    void operator()(const BindingValue& value) const {
        const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
        ((*object_).*method_)(static_cast<Enumeration>(choice.value));
    }

private:
    Object* object_{};
    Method method_{};
};

template <typename... EventArguments>
class PropertyChangeRelay final {
public:
    explicit PropertyChangeRelay(std::function<void()> changed)
        : changed_(std::move(changed)) {}

    void operator()(EventArguments...) const { changed_(); }

private:
    std::function<void()> changed_;
};

template <typename... EventArguments>
class EventChangeConnector final {
public:
    explicit EventChangeConnector(Event<EventArguments...>& event) noexcept
        : event_(&event) {}

    SubscriptionToken operator()(Component& owner,
                                 std::function<void()> changed) const {
        return (*event_).subscribe(
            owner,
            PropertyChangeRelay<EventArguments...>(std::move(changed)));
    }

private:
    Event<EventArguments...>* event_{};
};

} // namespace gui_forms::detail
