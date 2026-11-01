#pragma once

#include "gui_forms/controls/tool_strip/tool_strip.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

// The side of a ToolStripContainer a panel lines. It decides how the panel
// stacks its strips and which way they run.
enum class ToolStripPanelEdge : std::uint8_t { top, bottom, left, right };

// A band that holds ToolStrips (Windows Forms' ToolStripPanel). A top or
// bottom panel stacks horizontal strips in rows, one strip per row; a left
// or right panel stacks vertical strips in columns. The panel is as thick as
// its strips together, and has no thickness without them. The panel owns
// joined strips as its children.
class ToolStripPanel final : public Control {
public:
    explicit ToolStripPanel(StableId stable_id, ToolStripPanelEdge edge = ToolStripPanelEdge::top);

    [[nodiscard]] ToolStripPanelEdge edge() const noexcept { return edge_; }
    // Changing the edge turns the joined strips to its orientation. Throws
    // std::invalid_argument for an invalid edge, and std::logic_error when a
    // joined StatusStrip would have to turn vertical.
    void set_edge(ToolStripPanelEdge edge);
    // Adds a strip after the others and turns it to the panel's orientation.
    // Throws std::invalid_argument for a null strip or one that already has
    // a parent, and std::logic_error for a StatusStrip on a left or right
    // panel.
    void join(std::shared_ptr<ToolStrip> strip);
    // Joins a strip as join does, at `row` of the joined strips (0 first;
    // the count of joined strips last). Throws std::out_of_range for a row
    // past that count, before anything changes, and as join otherwise.
    void insert(std::shared_ptr<ToolStrip> strip, std::size_t row);
    // Removes a joined strip and returns it; null when the strip is not
    // joined to this panel.
    std::shared_ptr<ToolStrip> remove(const ToolStrip& strip);
    // The joined strips, in order.
    [[nodiscard]] std::vector<std::shared_ptr<ToolStrip>> strips() const;

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

    ToolStripPanelEdge edge_{ToolStripPanelEdge::top};
};

} // namespace gui_forms
