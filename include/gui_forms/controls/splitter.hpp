#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/events/drag_events/drag_events.hpp"
#include "gui_forms/drag/drag_tracker/drag_tracker.hpp"
#include "gui_forms/controls/splitter_capabilities.hpp"

#include <array>
#include <optional>
#include <span>

namespace gui_forms {

// A move of a Splitter by the user. `pointer` is the pointer in the parent's
// coordinates. In splitter_moving, `split_position` is the target's proposed
// extent, which a handler may change (it is clamped again to the limits); in
// splitter_moved it is the extent the target took.
struct GUI_FORMS_API SplitterMoveEvent final {
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
// The drag runs on the public drag client (DragTracker, a value drag along
// the axis, over the window's drag engine; roadmap 2.1, the study's step M1): the press starts it at once, each move
// previews the extent live (splitter_moving), the release commits it
// (splitter_moved). Escape, a lost capture and the window's deactivation
// cancel and put the target back at its extent before the drag, as Windows
// Forms' Escape does. The begin_keyboard_drag rule action moves it from the
// keyboard: the arrows along the axis step by 10 (Ctrl 100, Shift 1), Enter
// commits, Escape cancels. The engine raises the typed drag interactions
// (drag_started, drag_dropped, drag_ended; a splitter has no candidates, so
// no drag_target_changed) after the splitter's own events.
class GUI_FORMS_API Splitter final : public Control, public SplitterCapabilities<Splitter> {
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
    [[nodiscard]] Event<const DragInteraction&>& drag_started() noexcept { return drag_interactions_.started; }
    [[nodiscard]] Event<const DragInteraction&>& drag_target_changed() noexcept {
        return drag_interactions_.target_changed;
    }
    [[nodiscard]] Event<const DragInteraction&>& drag_dropped() noexcept { return drag_interactions_.dropped; }
    [[nodiscard]] Event<const DragInteraction&>& drag_ended() noexcept { return drag_interactions_.ended; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    bool begin_keyboard_drag() override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

    // Stage T1 anatomy, as dot members.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
    };
    struct GUI_FORMS_API States final {
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
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_enter() override;
    void on_mouse_leave() override;

    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;

private:

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
    // The drag tracker's callbacks (splitter.cpp): the value plan, the live
    // preview, the announcement and the commit or restore.
    void plan_drag(DragPlan& plan);
    void preview_drag(const DragTrackerFrame& frame);
    void announce_drag(DragAnnouncementRequest& request);
    void finish_drag(const DragTrackerFrame& frame);
    [[nodiscard]] Point pointer_in_parent(Point window_point) const;

    bool hot_{};
    bool dragging_{};
    // Whether a preview changed the target's extent (a cancel restores it).
    bool previewed_{};
    // The target's extent when the drag started, and the target.
    double drag_start_extent_{};
    Control::WeakPtr drag_target_{};
    Event<SplitterMoveEvent&> splitter_moving_{};
    Event<const SplitterMoveEvent&> splitter_moved_{};
    // The typed drag interactions, raised by the drag engine.
    DragInteractionEvents drag_interactions_{};
    // The announcement's text, valid while the tracker copies it.
    std::array<char, 96> announcement_text_{};
    // The drag client; it raises drag_interactions_ and borrows this
    // splitter, which outlives it (members are destroyed first).
    DragTracker drag_;
    // property_codemod begin Splitter (tools/property_families/splitter.json; generated, do not edit)
private:
    void set_hot_from_pointer(bool inside);
    void drag_pointer(MouseEventArgs& arguments);
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
