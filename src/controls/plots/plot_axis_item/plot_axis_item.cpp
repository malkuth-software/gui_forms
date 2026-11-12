#include "gui_forms/controls/plot_axis_item.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

// The PlotAxisDirection and PlotAxisScale enumerations, numbered as
// declared. Static storage; any thread may read them.
std::shared_ptr<const PropertyEnumDescriptor> plot_axis_direction_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "plot_axis_direction", {{"horizontal", 0}, {"vertical", 1}}, false});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> plot_axis_scale_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "plot_axis_scale", {{"linear", 0}, {"log10", 1}, {"symlog", 2}, {"time", 3}, {"category", 4}}, false});
    return value;
}

plots::ScaleKind scale_kind(PlotAxisScale scale) noexcept {
    switch (scale) {
    case PlotAxisScale::linear:
        return plots::ScaleKind::linear;
    case PlotAxisScale::log10:
        return plots::ScaleKind::log10;
    case PlotAxisScale::symlog:
        return plots::ScaleKind::symlog;
    case PlotAxisScale::time:
        return plots::ScaleKind::time;
    case PlotAxisScale::category:
        return plots::ScaleKind::category;
    }
    return plots::ScaleKind::linear;
}

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) throw std::invalid_argument(message);
}

} // namespace

PlotAxisItem::PlotAxisItem(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = false;
    tab_stop = false;
}

plots::Axis PlotAxisItem::axis_description(plots::AxisSide side) const {
    plots::Axis axis{};
    axis.kind = scale_kind(scale.read());
    axis.range = plots::PlotRange{minimum.read(), maximum.read()};
    axis.side = side;
    axis.fit = auto_fit.read() ? plots::AxisFit::once : plots::AxisFit::none;
    const double span = follow_span.read();
    axis.follow = plots::AxisFollow{span > 0.0, span};
    return axis;
}

Size PlotAxisItem::measure(Size) {
    return Size{0.0, 0.0};
}

bool PlotAxisItem::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor PlotAxisItem::semantic_descriptor() const {
    // The plot publishes its axes in its own description.
    SemanticDescriptor descriptor;
    descriptor.exposed = false;
    return descriptor;
}

void PlotAxisItem::notify_plot() {
    revision_ = revision_ + 1U;
    const Control::Ptr owner = parent();
    if (owner) (*owner).invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
}

// -- property hooks --------------------------------------------------------------

void PlotAxisItem::prepare_write(const decltype(direction)&, PlotAxisDirection&) {
    require_mutable();
}

void PlotAxisItem::written(const decltype(direction)&) {
    notify_plot();
}

void PlotAxisItem::prepare_write(const decltype(scale)&, PlotAxisScale&) {
    require_mutable();
}

void PlotAxisItem::written(const decltype(scale)&) {
    notify_plot();
}

void PlotAxisItem::prepare_write(const decltype(minimum)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "PlotAxisItem minimum must be finite");
}

void PlotAxisItem::written(const decltype(minimum)&) {
    notify_plot();
}

void PlotAxisItem::prepare_write(const decltype(maximum)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "PlotAxisItem maximum must be finite");
}

void PlotAxisItem::written(const decltype(maximum)&) {
    notify_plot();
}

void PlotAxisItem::prepare_write(const decltype(label)&, std::string&) {
    require_mutable();
}

void PlotAxisItem::written(const decltype(label)&) {
    notify_plot();
}

void PlotAxisItem::prepare_write(const decltype(auto_fit)&, bool&) {
    require_mutable();
}

void PlotAxisItem::written(const decltype(auto_fit)&) {
    notify_plot();
}

void PlotAxisItem::prepare_write(const decltype(follow_span)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0) {
        throw std::invalid_argument("PlotAxisItem follow_span must be finite and not negative");
    }
}

void PlotAxisItem::written(const decltype(follow_span)&) {
    notify_plot();
}

// property_codemod begin PlotAxisItem (tools/property_families/plot_axis_item.json; generated, do not edit)

namespace {

PropertyDescriptor plot_axis_item_direction_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "direction";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Layout";
    descriptor.description = "Which way the axis runs.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"plot_axis_direction", "horizontal", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = plot_axis_direction_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_axis_item_direction() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_axis_item_direction_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_axis_item_scale_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "scale";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "How the axis maps values.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"plot_axis_scale", "linear", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = plot_axis_scale_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_axis_item_scale() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_axis_item_scale_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_axis_item_minimum_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "minimum";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "The smallest value shown.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_axis_item_minimum() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_axis_item_minimum_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_axis_item_maximum_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "maximum";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "The largest value shown.";
    descriptor.default_value = BindingValue{1.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_axis_item_maximum() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_axis_item_maximum_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_axis_item_label_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "label";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The axis title.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_axis_item_label() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_axis_item_label_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_axis_item_auto_fit_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "auto_fit";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the axis fits the data.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_axis_item_auto_fit() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_axis_item_auto_fit_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_axis_item_follow_span_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "follow_span";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "The span a following axis shows before the newest x; 0 for none.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_axis_item_follow_span() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_axis_item_follow_span_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<PlotAxisDirection>::type() {
    return PropertyType{BindingValueKind::enumeration, false, plot_axis_direction_enum()};
}

BindingValue PropertyValueTraits<PlotAxisDirection>::to_binding(PlotAxisDirection value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = plot_axis_direction_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

PlotAxisDirection PropertyValueTraits<PlotAxisDirection>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<PlotAxisDirection>(choice.value);
}

PropertyType PropertyValueTraits<PlotAxisScale>::type() {
    return PropertyType{BindingValueKind::enumeration, false, plot_axis_scale_enum()};
}

BindingValue PropertyValueTraits<PlotAxisScale>::to_binding(PlotAxisScale value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = plot_axis_scale_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

PlotAxisScale PropertyValueTraits<PlotAxisScale>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<PlotAxisScale>(choice.value);
}

const std::array<PropertyEntry, PlotAxisItem::plot_axis_item_property_count>& PlotAxisItem::plot_axis_item_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 7U> own{{
        {"direction", &describe_plot_axis_item_direction,
         &read_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::direction), &PlotAxisItem::direction>,
         &write_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::direction), &PlotAxisItem::direction>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"scale", &describe_plot_axis_item_scale,
         &read_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::scale), &PlotAxisItem::scale>,
         &write_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::scale), &PlotAxisItem::scale>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"minimum", &describe_plot_axis_item_minimum,
         &read_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::minimum), &PlotAxisItem::minimum>,
         &write_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::minimum), &PlotAxisItem::minimum>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"maximum", &describe_plot_axis_item_maximum,
         &read_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::maximum), &PlotAxisItem::maximum>,
         &write_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::maximum), &PlotAxisItem::maximum>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"label", &describe_plot_axis_item_label,
         &read_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::label), &PlotAxisItem::label>,
         &write_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::label), &PlotAxisItem::label>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"auto_fit", &describe_plot_axis_item_auto_fit,
         &read_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::auto_fit), &PlotAxisItem::auto_fit>,
         &write_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::auto_fit), &PlotAxisItem::auto_fit>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"follow_span", &describe_plot_axis_item_follow_span,
         &read_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::follow_span), &PlotAxisItem::follow_span>,
         &write_cell_entry<PlotAxisItem, decltype(&PlotAxisItem::follow_span), &PlotAxisItem::follow_span>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "PlotAxisItem's property table names a property twice");
    static const std::array<PropertyEntry, plot_axis_item_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> PlotAxisItem::property_table() const noexcept {
    const std::array<PropertyEntry, plot_axis_item_property_count>& entries = plot_axis_item_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end PlotAxisItem

} // namespace gui_forms
