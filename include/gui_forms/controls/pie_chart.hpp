#pragma once
// PieChart: the plots package's round chart (Windows Forms' Chart with a Pie
// or Doughnut series): one slice per value, each a share of the total, with
// slice labels and a legend. A doughnut is a pie with a hole: doughnut_radius
// is the hole's radius as a percentage of the pie's (Windows Forms'
// DoughnutRadius). Slices are records kept by stable ID, not child controls.
//
// Slices run clockwise from start_angle (degrees clockwise from the top), in
// the order given; right to left they run counter-clockwise, mirrored about
// the vertical. A slice's colour is its own or, when it has none, the theme's
// series colour for its position (plot_series_color), so the chart follows
// the theme as PlotView does. The slices are application content, drawn as
// PlotView draws its marks; the plot field, the legend frame, its entries,
// the swatches' wells and the focus cue go through the theme's parts.
//
// Input: a click selects the slice under it (on the pie or its legend entry)
// and a double click activates it; the arrows move the selection to the next
// or previous slice, Home and End to the first and last; Enter activates.
// Selection is one slice, kept by ID across set_slices. Semantics: a list
// whose items are the slices, each named by its label with its value and
// share as the value; select and press act on them.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/pie_chart_values.hpp"
#include "gui_forms/controls/pie_chart_capabilities.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

#include <stxlib/optional.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

// One slice. `stable_id` is unique within the chart and names the slice in
// the published anatomy and semantics. `value` is finite and not negative; a
// zero slice is kept, listed in the legend and drawn as nothing. `color`, when
// present, is drawn as given (it is data); otherwise the theme's series
// colour for the slice's position.
struct GUI_FORMS_API PieSlice final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(label) std::string label{};
    double value{};
    stxlib::Optional<Color> color{};
};

// A change of the selected slice, after it happened.
struct GUI_FORMS_API PieSliceSelection final {
    stxlib::Optional<std::string> previous_id{};
    stxlib::Optional<std::string> current_id{};

    PieSliceSelection() noexcept {}
    // Spelt out because stxlib::Optional of a string is move-only (the
    // string's copy may throw), while Control::publish_change keeps a copy
    // of the payload for a selection changed during BeginInit/EndInit.
    PieSliceSelection(const PieSliceSelection& other) {
        if (other.previous_id.present()) previous_id.set(*other.previous_id.value());
        if (other.current_id.present()) current_id.set(*other.current_id.value());
    }
    PieSliceSelection(PieSliceSelection&& other) noexcept
        : previous_id(std::move(other.previous_id)), current_id(std::move(other.current_id)) {}
    PieSliceSelection& operator=(const PieSliceSelection& other) {
        if (this == &other) return *this;
        PieSliceSelection copy(other);
        previous_id = std::move(copy.previous_id);
        current_id = std::move(copy.current_id);
        return *this;
    }
    PieSliceSelection& operator=(PieSliceSelection&& other) noexcept {
        previous_id = std::move(other.previous_id);
        current_id = std::move(other.current_id);
        return *this;
    }
};

// A slice activated by a double click, Enter or the semantic press.
struct GUI_FORMS_API PieSliceActivation final {
    std::string slice_id{};
};

class GUI_FORMS_API PieChart final : public Control, public PieChartCapabilities<PieChart> {
public:
    // Development bounds, not measured capacity.
    static constexpr std::size_t maximum_slices = 4096U;
    static constexpr double maximum_doughnut_radius = 90.0;

    explicit PieChart(StableId stable_id);

    // -- slices --------------------------------------------------------------------
    [[nodiscard]] BorrowedSpan<PieSlice> slices() const& noexcept { return detail::lend_view(slices_); }
    BorrowedSpan<PieSlice> slices() const&& = delete;
    // Replaces the slices. Throws std::invalid_argument for an empty or
    // duplicate ID or one that breaks the StableId segment rule, invalid
    // UTF-8, a value that is negative or not finite, or more than
    // maximum_slices slices; the old slices then stay. The selection stays
    // on its ID when it survives, and selection_changed is raised when it
    // does not.
    void set_slices(std::vector<PieSlice> slices);
    // Sets one slice's value. Throws std::out_of_range for an unknown slice
    // and std::invalid_argument for a value that is negative or not finite.
    void set_slice_value(std::string_view slice_id, double value);
    // The sum of the values.
    [[nodiscard]] double total() const noexcept { return total_; }
    // A slice's share of the total, from 0 to 1 (0 while the total is 0).
    // Throws std::out_of_range for a slice past the end.
    [[nodiscard]] double share(std::size_t slice) const;
    // The position of the slice with this ID, or none.
    [[nodiscard]] stxlib::Optional<std::size_t> index_of(std::string_view slice_id) const;

    // -- selection -------------------------------------------------------------------
    // The selected slice's ID, or none. The view is borrowed until the
    // slices change.
    [[nodiscard]] stxlib::Optional<std::string_view> selected_id() const noexcept;
    // Selects a slice, or none, and raises selection_changed when it
    // changes. Throws std::out_of_range for an unknown slice.
    void set_selected_id(stxlib::Optional<std::string_view> slice_id);
    // The slice under a point in local coordinates (on the pie or its legend
    // entry), if any.
    [[nodiscard]] stxlib::Optional<std::size_t> slice_at(Point local) const;

    [[nodiscard]] Event<const PieSliceSelection&>& selection_changed() noexcept { return selection_changed_; }
    [[nodiscard]] Event<const PieSliceActivation&>& slice_activated() noexcept { return slice_activated_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry plot;
        PartGeometry legend;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The "slice" item system: one slice, its identity the slice's. `slice`
    // bounds the slice's wedge (its centre angle's box, not the wedge
    // itself: slice_at hit tests the wedge); `slice_label` its share's text;
    // the legend entry is `legend_swatch` (a well framing the colour) and
    // `legend_label`.
    struct GUI_FORMS_API SliceParts final {
        PartGeometry slice;
        PartGeometry slice_label;
        PartGeometry legend_swatch;
        PartGeometry legend_label;
    };
    struct GUI_FORMS_API SliceStates final {
        bool hot{};
        bool selected{};
        bool focus_visible{};
        bool disabled{};
    };
    struct GUI_FORMS_API Slice final {
        std::size_t index{};
        std::string stable_id;
        SliceParts parts;
        SliceStates states;
    };
    // Every slice, in the order given.
    [[nodiscard]] std::vector<Slice> realized_slices() const;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    // Where everything sits for the current bounds, from one pass. Angles
    // are radians clockwise from the top, in reading order (mirrored where
    // drawn and hit when right to left).
    struct Layout final {
        Rect plot{};
        Rect legend{};
        Point centre{};
        double radius{};
        double inner_radius{};
        double row_height{};
        double swatch{};
        bool mirrored{};
    };
    [[nodiscard]] Layout layout_for(Rect client) const;
    // The slice's start and sweep, in radians; a zero slice sweeps 0.
    [[nodiscard]] double slice_start(std::size_t slice) const noexcept;
    [[nodiscard]] double slice_sweep(std::size_t slice) const noexcept;
    // The point at `radius` from the centre at reading-order angle `angle`.
    [[nodiscard]] Point polar(const Layout& placed, double angle, double radius) const noexcept;
    [[nodiscard]] SliceParts slice_parts(const Layout& placed, std::size_t slice) const;
    [[nodiscard]] AnatomyStateSet slice_states(std::size_t slice) const;
    // The selected slice's legend entry, or the plot without one.
    [[nodiscard]] Rect focus_bounds(const Layout& placed) const;
    [[nodiscard]] Color slice_color(std::size_t slice) const;
    // Fills wedge_ with the slice's outline, the arcs at about one segment
    // per three device pixels.
    void build_wedge(const Layout& placed, std::size_t slice, double scale);
    void paint_slice(Painter& painter, const Layout& placed, std::size_t slice, double scale);
    void choose_slice(std::size_t slice);
    void activate_slice(std::size_t slice);
    void recompute_total() noexcept;

    detail::LendingVector<PieSlice> slices_{};
    double total_{};
    stxlib::Optional<std::size_t> selected_{};
    stxlib::Optional<std::size_t> hot_{};
    bool focused_{};
    // The private state, defined in pie_chart.cpp.
    struct State;
    [[nodiscard]] static stxlib::Unique<State> make_state();
    stxlib::Unique<State> state_{make_state()};
    Event<const PieSliceSelection&> selection_changed_{};
    Event<const PieSliceActivation&> slice_activated_{};
    // property_codemod begin PieChart (tools/property_families/pie_chart.json; generated, do not edit)
public:
    // PieChart's property table: Control's entries, then its own.
    static constexpr std::size_t pie_chart_property_count = Control::control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, pie_chart_property_count>& pie_chart_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event (the class is final: nothing overrides it in
    // C++). Only the properties with a Windows Forms On<Property>Changed
    // have one.
    void on_font_changed();

private:
    friend struct PieChartDoughnutRadiusCapability<PieChart>;
    friend struct PieChartStartAngleCapability<PieChart>;
    friend struct PieChartLabelStyleCapability<PieChart>;
    friend struct PieChartLegendVisibleCapability<PieChart>;
    friend struct FontCapability<PieChart>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(doughnut_radius)& cell, double& incoming);
    void written(const decltype(doughnut_radius)& cell);
    void prepare_write(const decltype(start_angle)& cell, double& incoming);
    void written(const decltype(start_angle)& cell);
    void prepare_write(const decltype(label_style)& cell, PieChartLabelStyle& incoming);
    void written(const decltype(label_style)& cell);
    void prepare_write(const decltype(legend_visible)& cell, bool& incoming);
    void written(const decltype(legend_visible)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    // property_codemod end PieChart
};

} // namespace gui_forms
