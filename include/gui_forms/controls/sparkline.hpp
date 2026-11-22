#pragma once
// Sparkline: the instruments package's word-sized trend line (owner,
// 2026-11-03; IMGUI_IMPLOT_STUDY B.4, the plots package's first client). It
// keeps the newest `capacity` values in a ring and draws them as one line
// across its bounds, the newest at the right, scaled to their own minimum
// and maximum, with a dot on the newest value and, with `fill`, the band
// under the line shaded. Drawn through the plots primitives
// (plots::SeriesMarkPainter: the envelope decimation keeps any capacity to
// about four points per device column). Read-only: no input, no focus.
// paint_sparkline draws the same marks from any values, without the control.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/plots/mark_painters/mark_painters.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/controls/sparkline_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include <stxlib/optional.hpp>

namespace gui_forms {

// The look of one sparkline drawn by paint_sparkline.
struct GUI_FORMS_API SparklineStyle final {
    // The line's colour; the band and the newest dot take it too.
    Color line{};
    // The line's width in local units (finite and positive).
    double line_width{1.0};
    // Whether the band under the line is shaded, and its alpha.
    bool fill{};
    std::uint8_t band_alpha{72U};
    // The dot on the newest finite value; 0 draws none.
    double newest_marker_size{4.0};
};

// The storage paint_sparkline reuses from paint to paint: one per drawn
// sparkline (a grid cell keeps one per column, reused across rows). It
// grows when a wider plot is drawn and never shrinks.
struct GUI_FORMS_API SparklineWorkspace final {
    plots::SeriesMarkPainter marks{};
    std::size_t reserved_columns{};
};

// Draws `values` (oldest first; NaN is a gap) the way Sparkline draws its
// ring, inside `plot` (local units, clipped to it), so that a grid cell, a
// list item or an owner-drawn control can draw a word-sized trend without
// a Sparkline control (the D3 demonstrator's I5). The x axis spans `slots`
// positions with the newest value at the right; `slots` is at least 2 and
// at least values.size(), otherwise nothing is drawn. The y axis is the
// values' finite extent (a flat run sits in the middle). `scale` is the
// paint scale (device pixels per local unit, finite and positive). Draws
// nothing when no value is finite or the plot is empty. Allocates only
// when the workspace grows; the painter is saved and restored around it.
GUI_FORMS_API void paint_sparkline(Painter& painter, Rect plot, plots::SeriesSpans<double> values,
                                   std::size_t slots, double scale, const SparklineStyle& style,
                                   SparklineWorkspace& workspace);

// ic: gui_forms::Sparkline: generated from include/gui_forms/controls/sparkline.ic.toml by tools/ic_declarations.py;
// ic: edit the declaration, not these lines. Thread: ui, unless a call says otherwise.
class GUI_FORMS_API Sparkline final : public Control, public SparklineCapabilities<Sparkline> {
public:
    explicit Sparkline(StableId stable_id);

    // Appends values, dropping the oldest past the capacity. NaN is a gap
    // in the line; infinities throw std::invalid_argument, changing nothing.
    void push(double value);
    void push_values(std::span<const double> values);
    // Removes every value.
    void clear_values();
    // The live values, oldest first.
    [[nodiscard]] plots::SeriesSpans<double> values() const noexcept { return values_.read(); }
    // The newest finite value, if any.
    [[nodiscard]] stxlib::Optional<double> latest() const noexcept;
    // The live values' finite extent (invalid when there are none).
    [[nodiscard]] plots::PlotRange extent() const noexcept;
    // The plot the line is drawn in, in local coordinates.
    [[nodiscard]] Rect plot_bounds() const noexcept;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry plot;
        PartGeometry series;
        PartGeometry marker;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // Replaces the ring with one of `capacity`, keeping the newest values.
    void resize_ring(std::size_t capacity);

    plots::SeriesColumn<double> values_{};
    SparklineWorkspace workspace_{};
    // property_codemod begin Sparkline (tools/property_families/sparkline.json; generated, do not edit)
public:
    // Sparkline's property table: Control's entries, then its own.
    static constexpr std::size_t sparkline_property_count = Control::control_property_count + 2U;
    [[nodiscard]] static const std::array<PropertyEntry, sparkline_property_count>& sparkline_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct SparklineCapacityCapability<Sparkline>;
    friend struct SparklineFillCapability<Sparkline>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    // ic: Requires incoming >= 2 and incoming <= 100000, else failure invalid_argument, unchanged.
    void prepare_write(const decltype(capacity)& cell, std::uint32_t& incoming);
    void written(const decltype(capacity)& cell);
    void prepare_write(const decltype(fill)& cell, bool& incoming);
    void written(const decltype(fill)& cell);
    // property_codemod end Sparkline
};

} // namespace gui_forms
