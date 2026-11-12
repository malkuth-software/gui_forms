#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/controls/plot_values.hpp"
#include "gui_forms/plots/axis/axis.hpp"
#include "gui_forms/controls/plot_axis_item_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace gui_forms {

// One axis of a PlotView (IMGUI_IMPLOT_STUDY B.4), declared as a child of
// the plot in a form or in C++: its direction, scale, initial range, title,
// fitting and following. The first horizontal axis is drawn below the plot
// and a second above it; the first vertical axis left of the plot and a
// second right of it; further ones are ignored (plots::
// maximum_axes_per_orientation bounds the model, the layout draws two). A
// plot without a horizontal or a vertical axis item uses a linear one over
// [0, 1].
//
// An item draws nothing, takes no input, no focus and no space, and
// publishes no semantic node: the plot reads it. Changing a property tells
// the owning plot, which lays out and repaints; it takes the item's range
// again only until a gesture moves its view (PlotView::reset_view).
class PlotAxisItem final : public Control, public PlotAxisItemCapabilities<PlotAxisItem> {
public:
    explicit PlotAxisItem(StableId stable_id);

    // The axis as the plots primitives describe it, on `side`.
    [[nodiscard]] plots::Axis axis_description(plots::AxisSide side) const;
    // Bumped by every property write: the owning plot compares it with the
    // revision it last took the item's range at.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] Size measure(Size available) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

private:
    // Bumps the revision and invalidates the owning plot.
    void notify_plot();

    std::uint64_t revision_{};
    // property_codemod begin PlotAxisItem (tools/property_families/plot_axis_item.json; generated, do not edit)
public:
    // PlotAxisItem's property table: Control's entries, then its own.
    static constexpr std::size_t plot_axis_item_property_count = Control::control_property_count + 7U;
    [[nodiscard]] static const std::array<PropertyEntry, plot_axis_item_property_count>& plot_axis_item_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct PlotAxisItemDirectionCapability<PlotAxisItem>;
    friend struct PlotAxisItemScaleCapability<PlotAxisItem>;
    friend struct PlotAxisItemMinimumCapability<PlotAxisItem>;
    friend struct PlotAxisItemMaximumCapability<PlotAxisItem>;
    friend struct PlotAxisItemLabelCapability<PlotAxisItem>;
    friend struct PlotAxisItemAutoFitCapability<PlotAxisItem>;
    friend struct PlotAxisItemFollowSpanCapability<PlotAxisItem>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(direction)& cell, PlotAxisDirection& incoming);
    void written(const decltype(direction)& cell);
    void prepare_write(const decltype(scale)& cell, PlotAxisScale& incoming);
    void written(const decltype(scale)& cell);
    void prepare_write(const decltype(minimum)& cell, double& incoming);
    void written(const decltype(minimum)& cell);
    void prepare_write(const decltype(maximum)& cell, double& incoming);
    void written(const decltype(maximum)& cell);
    void prepare_write(const decltype(label)& cell, std::string& incoming);
    void written(const decltype(label)& cell);
    void prepare_write(const decltype(auto_fit)& cell, bool& incoming);
    void written(const decltype(auto_fit)& cell);
    void prepare_write(const decltype(follow_span)& cell, double& incoming);
    void written(const decltype(follow_span)& cell);
    // property_codemod end PlotAxisItem
};

} // namespace gui_forms
