#pragma once

#include "gui_forms/controls/tool_strip.hpp"
#include "gui_forms/controls/tool_strip_panel_values.hpp"
#include "gui_forms/controls/tool_strip_panel_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

// A band that holds ToolStrips (Windows Forms' ToolStripPanel). A top or
// bottom panel stacks horizontal strips in rows; a left or right panel
// stacks vertical strips in columns, which this class also calls rows. A row
// holds one strip or several side by side: each strip but the last takes its
// content_length() (no more than the row has left), and the last takes the
// rest, so a strip alone fills its row. A row is as thick as its thickest
// strip, the panel as its rows together (none without strips). The panel
// owns joined strips as its children, in row order.
class ToolStripPanel final : public Control, public ToolStripPanelCapabilities<ToolStripPanel> {
public:
    explicit ToolStripPanel(StableId stable_id, ToolStripPanelEdge initial_edge = ToolStripPanelEdge::top);

    // Adds a strip in a new row after the others and turns it to the panel's
    // orientation.
    // Throws std::invalid_argument for a null strip or one that already has
    // a parent, and std::logic_error for a StatusStrip on a left or right
    // panel.
    void join(std::shared_ptr<ToolStrip> strip);
    // Joins a strip as join does, in a new row at `row` (0 first; the row
    // count last). Throws std::out_of_range for a row past the row count,
    // before anything changes, and as join otherwise.
    void insert(std::shared_ptr<ToolStrip> strip, std::size_t row);
    // Joins a strip as join does, into the existing row `row` at `position`
    // among its strips (0 first; their count last). Throws std::out_of_range
    // for a row or position past those, before anything changes, and as
    // join otherwise.
    void join_row(std::shared_ptr<ToolStrip> strip, std::size_t row, std::size_t position);
    // Removes a joined strip and returns it; null when the strip is not
    // joined to this panel. A row it leaves empty goes; the strip after it
    // in its row starts the row when it started it.
    std::shared_ptr<ToolStrip> remove(const ToolStrip& strip);
    // The joined strips, in order (row by row).
    [[nodiscard]] std::vector<std::shared_ptr<ToolStrip>> strips() const;
    // The joined strips by row, each row in order along the panel.
    [[nodiscard]] std::vector<std::vector<std::shared_ptr<ToolStrip>>> rows() const;
    [[nodiscard]] std::size_t row_count() const;

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members; its states are Control's.
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    [[nodiscard]] bool vertical() const noexcept;
    [[nodiscard]] bool continues_row(const ToolStrip& strip) const noexcept;
    // Joins `strip` (validated) just before `before` in the children, or
    // last without one; `continues` records whether it continues the row of
    // the strip before it.
    void place(std::shared_ptr<ToolStrip> strip, const std::shared_ptr<ToolStrip>& before, bool continues);

    // The joined strips that continue the row of the strip before them;
    // every other strip starts a row.
    std::vector<RuntimeId> continuing_{};
    // property_codemod begin ToolStripPanel (tools/property_families/tool_strip_panel.json; generated, do not edit)
private:
    friend struct ToolStripPanelEdgePropertyCapability<ToolStripPanel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(edge)& cell, ToolStripPanelEdge& incoming);
    void written(const decltype(edge)& cell);
    // property_codemod end ToolStripPanel
};

} // namespace gui_forms
