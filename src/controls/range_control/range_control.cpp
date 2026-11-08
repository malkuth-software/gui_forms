#include "gui_forms/controls/range_control/range_control.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"

#include "range_control_rendering.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace range_control_detail;

void RangeControl::bind(Value<double>& model) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    std::unique_ptr<detail::ScalarBinding<RangeControl, double>> binding =
        std::make_unique<detail::ScalarBinding<RangeControl, double>>(
            *this, model, value_changed_, &RangeControl::value, &RangeControl::set_value,
            &RangeControl::validate_bound_value);
    // Retire the old model before the initial model-wins publication.
    value_binding_ = std::move(binding);
    (*value_binding_).synchronize();
}

void RangeControl::validate_bound_value(const double value) const {
    if (value < minimum_ || value > maximum_) {
        throw std::out_of_range("bound value is outside the control range");
    }
}

void RangeControl::unbind() noexcept { value_binding_.reset(); }

RangeControl::RangeControl(StableId stable_id)
    : Control(std::move(stable_id)) {
    // The user changes it, so a form binding writes it back by default.
    PropertyDescriptor value_descriptor{"value", BindingValueKind::number, "Behavior",
                             "Current value within the retained range.", BindingValue{0.0},
                             Dirty::paint | Dirty::semantics};
    value_descriptor.binding_direction = PropertyBindingDirection::two_way;
    define_bindable_property({
        std::move(value_descriptor),
        detail::BindingMemberGetter<RangeControl, double>(
            *this, &RangeControl::value_),
        detail::ConvertedPropertySetter<RangeControl, double>(
            *this, &RangeControl::set_value, BindingValueKind::number,
            "RangeControl.Value binding requires a number"),
        detail::EventChangeConnector<double>(value_changed_), {}, {}});

    PropertyDescriptor minimum;
    minimum.name = "minimum";
    minimum.kind = BindingValueKind::number;
    minimum.category = "Behavior";
    minimum.description = "Lower bound of the range; below the maximum.";
    minimum.default_value = BindingValue{0.0};
    minimum.invalidation_effects = Dirty::paint | Dirty::semantics;
    minimum.bindable = false;
    define_bindable_property({
        std::move(minimum),
        detail::BindingNoexceptMethodGetter<RangeControl, double>(*this, &RangeControl::minimum),
        detail::ConvertedPropertySetter<RangeControl, double>(
            *this, &RangeControl::set_minimum, BindingValueKind::number,
            "RangeControl.Minimum requires a number"),
        detail::EventChangeConnector<double, double>(range_changed_), {}, {}});

    PropertyDescriptor maximum;
    maximum.name = "maximum";
    maximum.kind = BindingValueKind::number;
    maximum.category = "Behavior";
    maximum.description = "Upper bound of the range; above the minimum.";
    maximum.default_value = BindingValue{100.0};
    maximum.invalidation_effects = Dirty::paint | Dirty::semantics;
    maximum.bindable = false;
    define_bindable_property({
        std::move(maximum),
        detail::BindingNoexceptMethodGetter<RangeControl, double>(*this, &RangeControl::maximum),
        detail::ConvertedPropertySetter<RangeControl, double>(
            *this, &RangeControl::set_maximum, BindingValueKind::number,
            "RangeControl.Maximum requires a number"),
        detail::EventChangeConnector<double, double>(range_changed_), {}, {}});
}

double RangeControl::normalized_value() const noexcept {
    return (value_ - minimum_) / (maximum_ - minimum_);
}

void RangeControl::set_range(double minimum_value, double maximum_value) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    require_finite(minimum_value, "range minimum must be finite");
    require_finite(maximum_value, "range maximum must be finite");
    if (minimum_value >= maximum_value) {
        throw std::invalid_argument("range maximum must be greater than minimum");
    }
    if (minimum_ == minimum_value && maximum_ == maximum_value) {
        return;
    }
    const double old_value = value_;
    const double next_value = std::clamp(value_, minimum_value, maximum_value);
    if (value_binding_) (*value_binding_).validate_update(next_value);
    if (!is_alive()) return;
    minimum_ = minimum_value;
    maximum_ = maximum_value;
    value_ = next_value;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(range_changed_, minimum_, maximum_);
    if (is_alive() && old_value != value_) {
        publish_change(value_changed_, value_);
    }
}

void RangeControl::set_minimum(double minimum_value) {
    set_range(minimum_value, maximum_);
}

void RangeControl::set_maximum(double maximum_value) {
    set_range(minimum_, maximum_value);
}

void RangeControl::set_value(double value) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    require_finite(value, "range value must be finite");
    if (value < minimum_ || value > maximum_) {
        throw std::out_of_range("range value is outside minimum and maximum");
    }
    if (value_ == value) {
        return;
    }
    if (value_binding_) (*value_binding_).validate_update(value);
    if (!is_alive()) return;
    value_ = value;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(value_changed_, value_);
}

void RangeControl::set_small_change(double change) {
    require_mutable();
    require_finite(change, "small change must be finite");
    if (change <= 0.0) {
        throw std::invalid_argument("small change must be positive");
    }
    if (small_change_ == change) {
        return;
    }
    small_change_ = change;
    invalidate(Dirty::semantics);
}

void RangeControl::set_large_change(double change) {
    require_mutable();
    require_finite(change, "large change must be finite");
    if (change <= 0.0) {
        throw std::invalid_argument("large change must be positive");
    }
    if (large_change_ == change) {
        return;
    }
    large_change_ = change;
    invalidate(Dirty::semantics);
}

void RangeControl::set_orientation(Orientation orientation) {
    require_mutable();
    if (orientation_ == orientation) {
        return;
    }
    orientation_ = orientation;
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void RangeControl::set_style(BasicControlStyle style) {
    require_mutable();
    if (style_ == style) {
        return;
    }
    style_ = std::move(style);
    invalidate(Dirty::paint | Dirty::semantics);
}

void RangeControl::increment(double delta) {
    require_mutable();
    require_finite(delta, "range increment must be finite");
    set_value(std::clamp(value_ + delta, minimum_, maximum_));
}

Rect RangeControl::local_bounds() const noexcept {
    const Rect arranged = committed_arranged_bounds();
    return {0.0, 0.0, arranged.width, arranged.height};
}

bool RangeControl::set_value_from_input(double value, RangeAction action) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    require_finite(value, "input range value must be finite");
    const double next = std::clamp(value, minimum_, maximum_);
    if (next == value_) {
        return false;
    }
    if (value_binding_) (*value_binding_).validate_update(next);
    if (!is_alive()) return false;
    const RangeScrollEvent event{value_, next, action};
    value_ = next;
    invalidate(Dirty::paint | Dirty::semantics);
    scroll_.emit(event);
    if (is_alive()) {
        publish_change(value_changed_, value_);
    }
    return true;
}


namespace {

constexpr std::array<PartField<RangeControl::Parts>, 2> range_control_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &RangeControl::Parts::region},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &RangeControl::Parts::focus},
}};

constexpr std::array<StateField<RangeControl::States>, 5> range_control_state_fields{{
    {.state = AnatomyState::normal, .member = &RangeControl::States::normal},
    {.state = AnatomyState::enabled, .member = &RangeControl::States::enabled},
    {.state = AnatomyState::disabled, .member = &RangeControl::States::disabled},
    {.state = AnatomyState::focused, .member = &RangeControl::States::focused},
    {.state = AnatomyState::focus_visible, .member = &RangeControl::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 2> range_control_parts =
    part_declarations(range_control_part_fields);
constexpr std::array<AnatomyState, 5> range_control_states =
    state_declarations(range_control_state_fields);

constexpr ControlAnatomy range_control_anatomy{
    .control_id = "range_control",
    .parts = range_control_parts,
    .states = range_control_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& RangeControl::anatomy() const noexcept {
    return range_control_anatomy;
}

void RangeControl::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), range_control_part_fields, record, geometry);
}

RangeControl::Parts RangeControl::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), range_control_part_fields, published.parts(), record);
    return record;
}

RangeControl::States RangeControl::states() const {
    States record;
    read_states(range_control_state_fields, anatomy_states(), record);
    return record;
}

namespace {

// The events RangeControl adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 3> range_control_interactions{{
    {.name = "range_changed", .payload = "double, double", .mutable_payload = false},
    {.name = "value_changed", .payload = "double", .mutable_payload = false},
    {.name = "scroll", .payload = "RangeScrollEvent", .mutable_payload = false},
}};

} // namespace

void RangeControl::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), range_control_interactions.begin(), range_control_interactions.end());
}

} // namespace gui_forms
