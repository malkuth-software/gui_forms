#include "gui_forms/controls/plots/plot_annotation_item/plot_annotation_item.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

PlotAnnotationItem::PlotAnnotationItem(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = false;
    tab_stop = false;
}

Size PlotAnnotationItem::measure(Size) {
    return Size{0.0, 0.0};
}

bool PlotAnnotationItem::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor PlotAnnotationItem::semantic_descriptor() const {
    // The plot reads its annotations into its own description.
    SemanticDescriptor descriptor;
    descriptor.exposed = false;
    return descriptor;
}

void PlotAnnotationItem::notify_plot() {
    const Control::Ptr owner = parent();
    if (owner) (*owner).invalidate(Dirty::paint | Dirty::semantics);
}

// -- property hooks --------------------------------------------------------------

void PlotAnnotationItem::prepare_write(const decltype(text)&, std::string&) {
    require_mutable();
}

void PlotAnnotationItem::written(const decltype(text)&) {
    notify_plot();
}

void PlotAnnotationItem::prepare_write(const decltype(x)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming)) throw std::invalid_argument("PlotAnnotationItem x must be finite");
}

void PlotAnnotationItem::written(const decltype(x)&) {
    notify_plot();
}

void PlotAnnotationItem::prepare_write(const decltype(y)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming)) throw std::invalid_argument("PlotAnnotationItem y must be finite");
}

void PlotAnnotationItem::written(const decltype(y)&) {
    notify_plot();
}

// property_codemod begin PlotAnnotationItem (tools/property_families/plot_annotation_item.json; generated, do not edit)

namespace {

PropertyDescriptor plot_annotation_item_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The annotation's words.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_annotation_item_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_annotation_item_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_annotation_item_x_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "x";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Layout";
    descriptor.description = "The anchor's x.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_annotation_item_x() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_annotation_item_x_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor plot_annotation_item_y_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "y";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Layout";
    descriptor.description = "The anchor's y.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_plot_annotation_item_y() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        plot_annotation_item_y_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, PlotAnnotationItem::plot_annotation_item_property_count>& PlotAnnotationItem::plot_annotation_item_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"text", &describe_plot_annotation_item_text,
         &read_cell_entry<PlotAnnotationItem, decltype(&PlotAnnotationItem::text), &PlotAnnotationItem::text>,
         &write_cell_entry<PlotAnnotationItem, decltype(&PlotAnnotationItem::text), &PlotAnnotationItem::text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"x", &describe_plot_annotation_item_x,
         &read_cell_entry<PlotAnnotationItem, decltype(&PlotAnnotationItem::x), &PlotAnnotationItem::x>,
         &write_cell_entry<PlotAnnotationItem, decltype(&PlotAnnotationItem::x), &PlotAnnotationItem::x>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"y", &describe_plot_annotation_item_y,
         &read_cell_entry<PlotAnnotationItem, decltype(&PlotAnnotationItem::y), &PlotAnnotationItem::y>,
         &write_cell_entry<PlotAnnotationItem, decltype(&PlotAnnotationItem::y), &PlotAnnotationItem::y>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "PlotAnnotationItem's property table names a property twice");
    static const std::array<PropertyEntry, plot_annotation_item_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> PlotAnnotationItem::property_table() const noexcept {
    const std::array<PropertyEntry, plot_annotation_item_property_count>& entries = plot_annotation_item_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end PlotAnnotationItem

} // namespace gui_forms
