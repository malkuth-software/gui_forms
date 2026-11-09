#pragma once

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/splitter/splitter_capabilities/splitter_capabilities.hpp"

#include <memory>
#include <optional>
#include <span>

namespace gui_forms {

class SplitterDrag;

// A move of a Splitter by the user. `pointer` is the pointer in the parent's
// coordinates. In splitter_moving, `split_position` is the target's proposed
// extent, which a handler may change (it is clamped again to the limits); in
// splitter_moved it is the extent the target took.
struct SplitterMoveEvent final {
    Point pointer{};
    double split_position{};
};

// A bar docked between two docked siblings that resizes the one before it
// (Windows Forms' Splitter, kept as a migration facade over the dock layout;
// owner decision 2 of 2026-10-25). New code uses SplitContainer.
//
// The splitter docks at an edge (left by default) and its target is the
// visible sibling docked at the same edge whose far side touches the
// splitter's near side, as in Windows Forms: for a left splitter, the
// left-docked sibling whose right edge is the splitter's left edge. Dragging
// the bar sets the target's requested width (left and right) or height (top
// and bottom), live, between min_size and the extent that leaves min_extra
// for the rest of the parent's dock area. A splitter docked none or fill, or
// one with no target, does nothing. The bar is painted by role (a face
// plate) and takes no focus, as in Windows Forms.
//
// The drag runs on the window's drag engine (roadmap 2.1, the study's step
// M1) as a projection along the axis: the press starts it at once, each move
// previews the extent live (splitter_moving), the release commits it
// (splitter_moved). Escape, a lost capture and the window's deactivation
// cancel and put the target back at its extent before the drag, as Windows
// Forms' Escape does. The begin_keyboard_drag rule action moves it from the
// keyboard: the arrows along the axis step by 10 (Ctrl 100, Shift 1), Enter
// commits, Escape cancels.
class Splitter final : public Control, public SplitterCapabilities<Splitter> {
public:
    explicit Splitter(StableId stable_id);
    ~Splitter() override;

    // The target's extent along the splitter's axis, or none without a
    // target (Windows Forms' SplitPosition, -1 there).
    [[nodiscard]] std::optional<double> split_position() const;
    // Resizes the target to `extent`, clamped to the limits; does nothing
    // without a target. Throws std::invalid_argument for a non-finite value.
    void set_split_position(double extent);
    // The sibling the splitter resizes, or null.
    [[nodiscard]] Control::Ptr target() const;

    [[nodiscard]] Event<SplitterMoveEvent&>& splitter_moving() noexcept { return splitter_moving_; }
    [[nodiscard]] Event<const SplitterMoveEvent&>& splitter_moved() noexcept { return splitter_moved_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_pointer(PointerEvent& event) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    bool begin_keyboard_drag() override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

    // Stage T1 anatomy, as dot members.
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
    };
    struct States final {
        bool normal{};
        bool hot{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    friend class SplitterDrag;

    // The limits a split position is clamped to, for the current target.
    struct SplitLimits final {
        double minimum{};
        double maximum{};
    };

    [[nodiscard]] bool horizontal_axis() const noexcept;
    [[nodiscard]] double axis_of(Point point) const noexcept;
    [[nodiscard]] double extent_of(const Control& control) const noexcept;
    [[nodiscard]] SplitLimits limits(const Control& target) const;
    [[nodiscard]] double clamped(double extent, const Control& target) const;
    void apply(Control& target, double extent);
    // Records the target and its extent before a drag; false without one.
    bool prepare_drag();

    bool hot_{};
    bool dragging_{};
    // Whether a preview changed the target's extent (a cancel restores it).
    bool previewed_{};
    // The target's extent when the drag started, and the target.
    double drag_start_extent_{};
    Control::WeakPtr drag_target_{};
    // The drag engine's client for this splitter (splitter.cpp).
    std::unique_ptr<SplitterDrag> drag_{};
    Event<SplitterMoveEvent&> splitter_moving_{};
    Event<const SplitterMoveEvent&> splitter_moved_{};
    // property_codemod begin Splitter (tools/property_families/splitter.json; generated, do not edit)
private:
    friend struct SplitterMinSizeCapability<Splitter>;
    friend struct SplitterMinExtraCapability<Splitter>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(min_size)& cell, double& incoming);
    void written(const decltype(min_size)& cell);
    void prepare_write(const decltype(min_extra)& cell, double& incoming);
    void written(const decltype(min_extra)& cell);
    // property_codemod end Splitter
};

} // namespace gui_forms
