#pragma once

// The composite control's internal access (cp34, phase 2): what the
// control's translation units share that takes the definition's internal
// types, which the public header cannot name. Internal to
// src/controls/composite_control.

#include "composite_evaluator.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace gui_forms {

class CompositeInstanceAccess final {
public:
    // Fires every rule of `trigger` on `child` (empty: the composite) and,
    // for `changed`, on `property`, in document order.
    static void fire_trigger(CompositeControl& control, control_language::CompositeTrigger trigger,
                             std::string_view child, std::optional<std::size_t> property, double wheel_notches);
    // Runs the action calls of `calls` (a command's `run`, a drag's
    // `preview`): set, toggle, step, run, raise and focus.
    static void run_calls(CompositeControl& control, const control_language::CompositeExpression& calls,
                          CompositeFiring firing);
    // Runs rule actions [first, + count).
    static void run_actions(CompositeControl& control, std::size_t first, std::size_t count, CompositeFiring firing);
    // A value an expression gave, as property `index` holds it; none when
    // it does not convert.
    [[nodiscard]] static std::optional<BindingValue> property_value_of(const CompositeControl& control,
                                                                       std::size_t index, const CompositeValue& value);
    // A registered property's read and write (the registration's
    // getter and setter): the write throws std::invalid_argument when the
    // value is refused, changing nothing.
    [[nodiscard]] static BindingValue read(const CompositeControl& control, std::size_t index);
    static void write(CompositeControl& control, std::size_t index, const BindingValue& value);
    // One step of a held part's repetition.
    static void repeat(CompositeControl& control);
    // A child's property changed: two-way binding `binding` writes back,
    // and rule `rule` (a `*-changed(@child)` rule) fires.
    static void child_changed(CompositeControl& control, std::size_t binding);
    static void child_property_fired(CompositeControl& control, std::size_t rule);
};

} // namespace gui_forms
