#pragma once

// Form bindings made at creation (stage FD, FD-6; owner rulings R1, R2 and
// R4 of 2026-11-07). A form document binds a property in its own slot,
// `value: bind(interval_);`, and the generated code or the loader calls one
// of these while it builds the form: the property takes the source's value
// at its place in document order, and the subscription is made in the same
// call, so there is no default-then-overwrite and no separate connect step.
// The control keeps what each call made and ends it when it is destroyed.
//
// - A scalar Value: bind_form_value. One way, the property follows the
//   Value; two way, a change of the property (the user's) is also written
//   to the Value. The Value must outlive the control, or be disposed first:
//   a disposed Value is no longer written.
// - A field of a BindingSource's records: bind_form_field, through the
//   control's data_bindings() (Binding), with the direction and the
//   update trigger mapped onto its DataSourceUpdateMode.
// - A Command: the button's own bind(Command&), called directly.
//
// Every call is UI-thread only and validates before it changes anything:
// an unknown or unbindable property, a kind the source cannot carry, a two
// way binding of a property that raises no change, or a disposed source
// throws std::invalid_argument and leaves the control as it was.

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/binding/value/binding_value_kind/binding_value_kind.hpp"
#include "gui_forms/value.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace gui_forms {

class BindingSource;
class Control;

// When a two-way binding of a text field writes the control's text back
// (owner ruling FD-6 R1): when the control validates (`on_validate`, the
// default, WinForms' DataSourceUpdateMode::OnValidation), or on every
// change (`on_change`).
enum class FormUpdateTrigger : std::uint8_t {
    on_validate,
    on_change,
};

// The kind of property a Value<T> carries: boolean for bool, a signed
// integer for int, a number for double. Other Value types are not bound.
template <typename T>
inline constexpr BindingValueKind form_value_kind = BindingValueKind::null;
template <>
inline constexpr BindingValueKind form_value_kind<bool> = BindingValueKind::boolean;
template <>
inline constexpr BindingValueKind form_value_kind<int> = BindingValueKind::signed_integer;
template <>
inline constexpr BindingValueKind form_value_kind<double> = BindingValueKind::number;

// Binds `property` of `target` to `source`. See the header comment.
void bind_form_value(Control& target, std::string_view property, Value<bool>& source,
                     PropertyBindingDirection direction);
void bind_form_value(Control& target, std::string_view property, Value<int>& source,
                     PropertyBindingDirection direction);
void bind_form_value(Control& target, std::string_view property, Value<double>& source,
                     PropertyBindingDirection direction);

// The generated code's spelling: `Kind` is the bound property's kind as
// the form schema recorded it, so a Value whose type does not carry that
// kind does not compile.
template <BindingValueKind Kind, typename T>
void bind_form_value(Control& target, std::string_view property, Value<T>& source,
                     PropertyBindingDirection direction) {
    static_assert(form_value_kind<T> != BindingValueKind::null,
                  "a form binds a Value<bool>, Value<int> or Value<double>");
    static_assert(form_value_kind<T> == Kind, "the Value's type does not carry the bound property's kind");
    bind_form_value(target, property, source, direction);
}

// Binds `property` of `target` to the field `field` of `source`'s current
// record. `trigger` matters only for a two-way binding.
void bind_form_field(Control& target, std::string_view property, const std::shared_ptr<BindingSource>& source,
                     std::string field, PropertyBindingDirection direction, FormUpdateTrigger trigger);

} // namespace gui_forms
