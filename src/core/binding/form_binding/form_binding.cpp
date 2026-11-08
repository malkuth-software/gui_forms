// Form bindings made at creation (stage FD, FD-6). A scalar Value link is a
// PropertyLink the bound control keeps; a field binding is a Binding in the
// control's data_bindings(). Both validate before they change anything.

#include "gui_forms/binding/form_binding/form_binding.hpp"

#include "gui_forms/binding/binding/binding.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/binding/control_bindings_collection/control_bindings_collection.hpp"
#include "gui_forms/binding/property_link/property_link.hpp"
#include "gui_forms/binding/types/binding_contract_types.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/event.hpp"

#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

// A Value's scalar as the property's BindingValue, and back; the reading
// is none when the property holds another alternative, or an integer
// outside int.
BindingValue scalar_value(const bool value) {
    const BindingValue converted{value};
    return converted;
}

BindingValue scalar_value(const int value) {
    const BindingValue converted{static_cast<std::int64_t>(value)};
    return converted;
}

BindingValue scalar_value(const double value) {
    const BindingValue converted{value};
    return converted;
}

template <typename T>
std::optional<T> scalar_reading(const BindingValue& value);

template <>
std::optional<bool> scalar_reading<bool>(const BindingValue& value) {
    const bool* flag = std::get_if<bool>(&value);
    if (flag == nullptr) return std::nullopt;
    return *flag;
}

template <>
std::optional<int> scalar_reading<int>(const BindingValue& value) {
    const std::int64_t* integer = std::get_if<std::int64_t>(&value);
    if (integer == nullptr) return std::nullopt;
    const bool in_range =
        *integer >= std::numeric_limits<int>::min() && *integer <= std::numeric_limits<int>::max();
    if (!in_range) return std::nullopt;
    return static_cast<int>(*integer);
}

template <>
std::optional<double> scalar_reading<double>(const BindingValue& value) {
    const double* number = std::get_if<double>(&value);
    if (number == nullptr) return std::nullopt;
    return *number;
}

// The property's descriptor, checked for a binding in `direction`: it
// exists, is bindable and writable, carries `kind`, and, two way, is
// readable and raises a change. Throws std::invalid_argument otherwise.
PropertyDescriptor checked_target(const Control& target, std::string_view property, BindingValueKind kind,
                                  PropertyBindingDirection direction) {
    const std::optional<PropertyDescriptor> descriptor = target.property_descriptor(property);
    if (!descriptor || !(*descriptor).bindable || !(*descriptor).writable) {
        throw std::invalid_argument("a form binding's property is not a bindable, writable property of its control");
    }
    if ((*descriptor).kind != kind) {
        throw std::invalid_argument("a form binding's source does not carry the property's kind");
    }
    const bool two_way = direction == PropertyBindingDirection::two_way;
    if (two_way && (!(*descriptor).readable || !(*descriptor).change_notifications)) {
        throw std::invalid_argument("a two-way form binding's property is not readable or raises no change");
    }
    return *descriptor;
}

template <typename T>
class FormValueLink;

// The control's change of the property, relayed to its link: a named
// target for the property-change subscription's std::function.
template <typename T>
struct FormValuePublish final {
    FormValueLink<T>* link{};

    void operator()() const;
};

// The link of one property to one Value<T>. Made by bind_form_value,
// kept by the control (adopt_property_link); its subscriptions end with
// it. Event order: the source's change sets the property (when it differs);
// two way, the property's change sets the source, whose equal-write rule
// ends the echo.
template <typename T>
class FormValueLink final : public PropertyLink {
public:
    FormValueLink(Control& target, std::string property, Value<T>& source, PropertyBindingDirection direction)
        : target_(target), property_(std::move(property)), source_(source) {
        // Bound at creation: the property takes the source's value now, and
        // the subscriptions follow in this same call.
        target_.set_property_value(property_, scalar_value(source_.get()));
        const Delegate<T> apply = Delegate<T>::template bind<FormValueLink, &FormValueLink::apply>(*this);
        source_changed_ = source_.changed().subscribe(target_, apply);
        if (direction == PropertyBindingDirection::two_way) {
            const FormValuePublish<T> publish{this};
            target_changed_ = target_.subscribe_property_changed(property_, target_, publish);
        }
    }

    // The property's current value, written to the source while the
    // source lives; a value the source's type cannot hold is not written.
    void publish() {
        if (!source_changed_.connected()) return;
        const std::optional<BindingValue> current = target_.property_value(property_);
        if (!current) return;
        const std::optional<T> reading = scalar_reading<T>(*current);
        if (!reading) return;
        // Last operation: a notification may destroy the link or control.
        source_.set(*reading);
    }

private:
    void apply(T value) {
        const std::optional<BindingValue> current = target_.property_value(property_);
        if (current) {
            const std::optional<T> reading = scalar_reading<T>(*current);
            if (reading && *reading == value) return;
        }
        // Last operation: a notification may destroy the link or control.
        target_.set_property_value(property_, scalar_value(value));
    }

    Control& target_;
    std::string property_;
    Value<T>& source_;
    SubscriptionToken source_changed_{};
    SubscriptionToken target_changed_{};
};

template <typename T>
void FormValuePublish<T>::operator()() const {
    (*link).publish();
}

template <typename T>
void bind_scalar(Control& target, std::string_view property, Value<T>& source, PropertyBindingDirection direction) {
    if (!source.is_alive()) throw std::invalid_argument("a form binding's Value is disposed");
    static_cast<void>(checked_target(target, property, form_value_kind<T>, direction));
    std::unique_ptr<FormValueLink<T>> link =
        std::make_unique<FormValueLink<T>>(target, std::string(property), source, direction);
    target.adopt_property_link(std::move(link));
}

} // namespace

void bind_form_value(Control& target, std::string_view property, Value<bool>& source,
                     PropertyBindingDirection direction) {
    bind_scalar(target, property, source, direction);
}

void bind_form_value(Control& target, std::string_view property, Value<int>& source,
                     PropertyBindingDirection direction) {
    bind_scalar(target, property, source, direction);
}

void bind_form_value(Control& target, std::string_view property, Value<double>& source,
                     PropertyBindingDirection direction) {
    bind_scalar(target, property, source, direction);
}

void bind_form_field(Control& target, std::string_view property, const std::shared_ptr<BindingSource>& source,
                     std::string field, PropertyBindingDirection direction, FormUpdateTrigger trigger) {
    if (!source || !(*source).is_alive()) throw std::invalid_argument("a form binding's BindingSource is disposed");
    const std::optional<PropertyDescriptor> descriptor = target.property_descriptor(property);
    if (!descriptor || !(*descriptor).bindable || !(*descriptor).writable) {
        throw std::invalid_argument("a form binding's property is not a bindable, writable property of its control");
    }
    BindingOptions options{};
    options.data_source_update_mode = DataSourceUpdateMode::never;
    if (direction == PropertyBindingDirection::two_way) {
        options.data_source_update_mode = trigger == FormUpdateTrigger::on_change
            ? DataSourceUpdateMode::on_property_changed
            : DataSourceUpdateMode::on_validation;
    }
    // The Binding reads the current record when it is added: bound at
    // creation.
    static_cast<void>(target.data_bindings().add(std::string(property), source, std::move(field), options));
}

} // namespace gui_forms
