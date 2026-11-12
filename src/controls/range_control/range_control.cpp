#include "gui_forms/controls/range_control.hpp"

#include "range_control_rendering.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

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
            *this, model, value_changed_, &RangeControl::bound_value, &RangeControl::write_bound_value,
            &RangeControl::validate_bound_value);
    // Retire the old model before the initial model-wins publication.
    value_binding_ = std::move(binding);
    (*value_binding_).synchronize();
}

void RangeControl::validate_bound_value(const double candidate) const {
    if (candidate < minimum_ || candidate > maximum_) {
        throw std::out_of_range("bound value is outside the control range");
    }
}

void RangeControl::unbind() noexcept { value_binding_.reset(); }

RangeControl::RangeControl(StableId stable_id)
    : Control(std::move(stable_id)) {}

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
        on_value_changed();
    }
}

void RangeControl::write_bound_value(double incoming) {
    value = incoming;
}

// The value's write, in set_value's order: validate, let a bound Value
// refuse, store, invalidate, then raise value_changed.
void RangeControl::write_view(const decltype(value)&, double incoming) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    require_finite(incoming, "range value must be finite");
    if (incoming < minimum_ || incoming > maximum_) {
        throw std::out_of_range("range value is outside minimum and maximum");
    }
    if (value_ == incoming) {
        return;
    }
    if (value_binding_) (*value_binding_).validate_update(incoming);
    if (!is_alive()) return;
    value_ = incoming;
    invalidate(Dirty::paint | Dirty::semantics);
    on_value_changed();
}

double RangeControl::read_view(const decltype(value)&) const {
    return value_;
}

void RangeControl::write_view(const decltype(minimum)&, double incoming) {
    set_range(incoming, maximum_);
}

double RangeControl::read_view(const decltype(minimum)&) const {
    return minimum_;
}

void RangeControl::write_view(const decltype(maximum)&, double incoming) {
    set_range(minimum_, incoming);
}

double RangeControl::read_view(const decltype(maximum)&) const {
    return maximum_;
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
    value = std::clamp(value_ + delta, minimum_, maximum_);
}

Rect RangeControl::local_bounds() const noexcept {
    const Rect arranged = committed_arranged_bounds();
    return {0.0, 0.0, arranged.width, arranged.height};
}

bool RangeControl::set_value_from_input(double requested, RangeAction action) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    require_finite(requested, "input range value must be finite");
    const double next = std::clamp(requested, minimum_, maximum_);
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
        on_value_changed();
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

// property_codemod begin RangeControl (tools/property_families/range_control.json; generated, do not edit)

namespace {

PropertyDescriptor range_control_value_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "value";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "Current value within the retained range.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.binding_direction = PropertyBindingDirection::two_way;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_range_control_value() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        range_control_value_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor range_control_minimum_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "minimum";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "Lower bound of the range; below the maximum.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_range_control_minimum() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        range_control_minimum_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor range_control_maximum_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "maximum";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "Upper bound of the range; above the minimum.";
    descriptor.default_value = BindingValue{100.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_range_control_maximum() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        range_control_maximum_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, RangeControl::range_control_property_count>& RangeControl::range_control_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"value", &describe_range_control_value,
         &read_cell_entry<RangeControl, decltype(&RangeControl::value), &RangeControl::value>,
         &write_cell_entry<RangeControl, decltype(&RangeControl::value), &RangeControl::value>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<RangeControl, decltype(&RangeControl::value_changed_), &RangeControl::value_changed_>},
        {"minimum", &describe_range_control_minimum,
         &read_cell_entry<RangeControl, decltype(&RangeControl::minimum), &RangeControl::minimum>,
         &write_cell_entry<RangeControl, decltype(&RangeControl::minimum), &RangeControl::minimum>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<RangeControl, decltype(&RangeControl::range_changed_), &RangeControl::range_changed_>},
        {"maximum", &describe_range_control_maximum,
         &read_cell_entry<RangeControl, decltype(&RangeControl::maximum), &RangeControl::maximum>,
         &write_cell_entry<RangeControl, decltype(&RangeControl::maximum), &RangeControl::maximum>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<RangeControl, decltype(&RangeControl::range_changed_), &RangeControl::range_changed_>},
    }};
    static_assert(property_names_distinct(own), "RangeControl's property table names a property twice");
    static const std::array<PropertyEntry, range_control_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> RangeControl::property_table() const noexcept {
    const std::array<PropertyEntry, range_control_property_count>& entries = range_control_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void RangeControl::on_value_changed() {
    publish_change(value_changed_, value.read());
}

void RangeControl::prepare_write(const decltype(small_change)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "small change must be finite");
    if (incoming <= 0.0) throw std::invalid_argument("small change must be positive");
}

void RangeControl::written(const decltype(small_change)&) {
    invalidate(Dirty::semantics);
}

void RangeControl::prepare_write(const decltype(large_change)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "large change must be finite");
    if (incoming <= 0.0) throw std::invalid_argument("large change must be positive");
}

void RangeControl::written(const decltype(large_change)&) {
    invalidate(Dirty::semantics);
}

void RangeControl::prepare_write(const decltype(orientation)&, Orientation&) {
    require_mutable();
}

void RangeControl::written(const decltype(orientation)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end RangeControl

} // namespace gui_forms
