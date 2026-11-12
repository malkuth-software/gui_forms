#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/plot_values.hpp"
#include "gui_forms/controls/plot_annotation_item_capabilities.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace gui_forms {

// A text label of a PlotView anchored at a data point (IMGUI_IMPLOT_STUDY
// B.3, annotations), declared as a child of the plot: placed beside its
// anchor, clear of the annotations before it and inside the plot
// (plots::layout_annotations). Plain text (owner ruling 7a). The item
// draws nothing itself, takes no input, no focus and no space, and
// publishes no semantic node; the plot reads it.
class GUI_FORMS_API PlotAnnotationItem final : public Control, public PlotAnnotationItemCapabilities<PlotAnnotationItem> {
public:
    explicit PlotAnnotationItem(StableId stable_id);

    [[nodiscard]] Size measure(Size available) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

private:
    // Invalidates the owning plot.
    void notify_plot();
    // property_codemod begin PlotAnnotationItem (tools/property_families/plot_annotation_item.json; generated, do not edit)
public:
    // PlotAnnotationItem's property table: Control's entries, then its own.
    static constexpr std::size_t plot_annotation_item_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, plot_annotation_item_property_count>& plot_annotation_item_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct PlotAnnotationItemTextCapability<PlotAnnotationItem>;
    friend struct PlotAnnotationItemXCapability<PlotAnnotationItem>;
    friend struct PlotAnnotationItemYCapability<PlotAnnotationItem>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(x)& cell, double& incoming);
    void written(const decltype(x)& cell);
    void prepare_write(const decltype(y)& cell, double& incoming);
    void written(const decltype(y)& cell);
    // property_codemod end PlotAnnotationItem
};

} // namespace gui_forms
