#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/tool_strip_panel/tool_strip_panel.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// The phases of a strip drag: `armed` from a press on a strip's grip until
// the pointer has moved the drag threshold; `dragging` while the stamp
// follows the pointer; `settling` while the stamp moves into the strip's
// place (its new one, or its old one when nothing moved).
enum class ToolStripDragPhase : std::uint8_t { idle, armed, dragging, settling };

// Where a dragged strip would go in the panel on `edge`, its rows counted
// as they are without the dragged strip: a new row at `row` (0 before the
// first, the row count after the last), or, when `joins_row`, the existing
// row `row` at `position` among its strips. `marker` is the insertion line
// (between rows, or across a row between its strips), in the container's
// coordinates.
struct ToolStripDropTarget final {
    ToolStripPanelEdge edge{ToolStripPanelEdge::top};
    std::size_t row{};
    bool joins_row{};
    std::size_t position{};
    Rect marker{};

    [[nodiscard]] bool operator==(const ToolStripDropTarget& other) const noexcept {
        const bool same = edge == other.edge && row == other.row && joins_row == other.joins_row &&
                          position == other.position && marker == other.marker;
        return same;
    }
};

// A strip that changed place, raised after the move (by a drag, move_strip
// or move_strip_into_row): its edge, row and position in the row before,
// and after (rows counted in the panel as it is after the move).
struct ToolStripMove final {
    std::string strip_id{};
    ToolStripPanelEdge from{ToolStripPanelEdge::top};
    std::size_t from_row{};
    std::size_t from_position{};
    ToolStripPanelEdge to{ToolStripPanelEdge::top};
    std::size_t to_row{};
    std::size_t to_position{};
};

// Four ToolStripPanels around a content panel (Windows Forms'
// ToolStripContainer). The top and bottom panels span the container's width
// and are as tall as their strips; the left and right panels fill the height
// between them and are as wide as their strips; the content panel takes the
// rest. Put strips in a panel with ToolStripPanel::join and the window's
// controls in content_panel().
//
// Strips are dragged by their grips (owner decision DK-36, K2-1), with the
// docking drag's mechanics (src/controls/drag_gesture): a press on a grip
// that moves the drag threshold (4 units) drags the strip; its stamp, the
// strip's picture shrinking to a miniature at the pointer, follows the
// pointer, and the shown panel under the pointer marks the row the strip
// would take: between rows for a new row, or across a row, between its
// strips, to join it (the middle half of a row's thickness joins it). A
// release there moves the strip, which turns to that panel's orientation. Strips never leave the container, enter dock stacks or
// float: a release elsewhere, with Ctrl held, after Escape, or when the
// pointer capture is lost leaves the strip where it was. A StatusStrip has
// no left or right target. An empty panel's band reaches a few units in
// from its edge, so a strip can be dropped on it.
class ToolStripContainer final : public Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit ToolStripContainer(StableId stable_id);
    ~ToolStripContainer() override;
    ToolStripContainer(const ToolStripContainer&) = delete;
    ToolStripContainer& operator=(const ToolStripContainer&) = delete;
    void initialize_control_tree();

    // The panels are child controls named <stable_id>.top, .bottom, .left,
    // .right and .content.
    [[nodiscard]] const std::shared_ptr<ToolStripPanel>& top_panel() const noexcept { return top_; }
    [[nodiscard]] const std::shared_ptr<ToolStripPanel>& bottom_panel() const noexcept { return bottom_; }
    [[nodiscard]] const std::shared_ptr<ToolStripPanel>& left_panel() const noexcept { return left_; }
    [[nodiscard]] const std::shared_ptr<ToolStripPanel>& right_panel() const noexcept { return right_; }
    [[nodiscard]] const std::shared_ptr<Panel>& content_panel() const noexcept { return content_; }

    // A hidden panel takes no room and shows none of its strips.
    [[nodiscard]] bool top_panel_visible() const noexcept { return top_visible_; }
    void set_top_panel_visible(bool visible);
    [[nodiscard]] bool bottom_panel_visible() const noexcept { return bottom_visible_; }
    void set_bottom_panel_visible(bool visible);
    [[nodiscard]] bool left_panel_visible() const noexcept { return left_visible_; }
    void set_left_panel_visible(bool visible);
    [[nodiscard]] bool right_panel_visible() const noexcept { return right_visible_; }
    void set_right_panel_visible(bool visible);

    // Moves a strip joined to one of the four panels into a new row at `row`
    // of the panel on `edge` (rows counted without the strip), as a drop
    // would, and raises strip_moved. True when the strip is there afterwards
    // (also when it already was, which raises nothing); false, changing
    // nothing, for a strip not joined here, an invalid edge, a row past the
    // end, or a StatusStrip sent to the left or right. A drag under way is
    // cancelled first.
    bool move_strip(const ToolStrip& strip, ToolStripPanelEdge edge, std::size_t row);
    // As move_strip, into the existing row `row` at `position` among its
    // strips; false also for a position past the row's strips.
    bool move_strip_into_row(const ToolStrip& strip, ToolStripPanelEdge edge, std::size_t row,
                             std::size_t position);
    [[nodiscard]] Event<const ToolStripMove&>& strip_moved() noexcept { return strip_moved_; }

    [[nodiscard]] ToolStripDragPhase drag_phase() const noexcept { return drag_.phase; }
    // The strip being dragged (armed, dragging or settling), or null.
    [[nodiscard]] std::shared_ptr<ToolStrip> dragged_strip() const { return drag_.strip.lock(); }
    // Where a release would put the strip now, or none.
    [[nodiscard]] std::optional<ToolStripDropTarget> drag_target() const { return drag_.target; }
    // The stamp's rectangle in the container's coordinates while dragging
    // or settling; empty otherwise.
    [[nodiscard]] Rect drag_stamp() const noexcept;
    // Whether Ctrl is suppressing the targets.
    [[nodiscard]] bool drag_suppressed() const noexcept { return drag_.suppressed; }
    // How far the pointer moves from a grip press before it drags (4 units;
    // a host may set 1 to 64). Throws std::out_of_range outside that.
    [[nodiscard]] double drag_threshold() const noexcept { return drag_threshold_; }
    void set_drag_threshold(double threshold);
    // Ends an armed drag, or a drag under way as Escape does. False when
    // there is none.
    bool cancel_drag();
    // Moves a strip joined here by keys (owner decision DK-59): the host key
    // map's move_panel command (Ctrl+Alt+Shift+M by default), pressed with
    // the focus in a strip, starts it, as for a docking panel. The stamp
    // stands over the chosen place, starting at the strip's own: the arrows
    // across the panel's rows step between rows (a new row, then joining
    // that row, in turn), the arrows along them between positions in a
    // joined row, Tab and Shift+Tab to the next or previous shown panel
    // that admits the strip; Enter drops, Escape cancels, and the focus
    // goes back to where it was. False, starting nothing, while a drag is
    // under way or for a strip not joined to a shown panel here.
    bool begin_keyboard_move(const ToolStrip& strip);
    [[nodiscard]] bool keyboard_moving() const noexcept {
        return drag_.phase == ToolStripDragPhase::dragging && drag_.keyboard;
    }

    void arrange(Rect final_bounds) override;
    void on_pointer_preview(PointerEvent& event) override;
    void on_pointer(PointerEvent& event) override;
    // Takes the move_panel command with the focus in a strip, and the
    // drag's keys.
    void on_key_preview(KeyEvent& event) override;
    void on_key(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members; its states are Control's.
    struct Parts final {
        PartGeometry region;
        PartGeometry top_panel;
        PartGeometry bottom_panel;
        PartGeometry left_panel;
        PartGeometry right_panel;
        PartGeometry content_panel;
        // While dragging or settling: the stamp and its picture; while a
        // target is chosen, its insertion line.
        PartGeometry drag_stamp;
        PartGeometry drag_stamp_image;
        PartGeometry drop_marker;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_detaching_from_window(Window& former_window) noexcept override;
    void on_dispose() noexcept override;

private:
    class DragLayer;
    // The drag session, replaced whole. `from` and `from_row` are where the
    // strip was when it was pressed; `source` its rectangle then and `grab`
    // where the press held it (fractions of `source`), in the container's
    // coordinates. `settle_from` is the stamp where the drag ended. The
    // picture is the strip's snapshot (none where the window cannot take
    // one), released by reset_drag. `focus_before` gets the focus back.
    struct DragSession final {
        ToolStripDragPhase phase{ToolStripDragPhase::idle};
        std::weak_ptr<ToolStrip> strip{};
        ToolStripPanelEdge from{ToolStripPanelEdge::top};
        std::size_t from_row{};
        std::uint64_t pointer_id{1U};
        Point press{};
        Point pointer{};
        Point grab{};
        Rect source{};
        bool suppressed{};
        std::optional<ToolStripDropTarget> target{};
        Rect stamp{};
        Rect settle_from{};
        std::optional<ControlSnapshot> picture{};
        std::weak_ptr<Control> focus_before{};
        bool focusable_before{};
        // The keyboard move: the chosen panel, its stop (2r: a new row at
        // r; 2r + 1: joining row r, rows counted without the strip) and the
        // position in a joined row.
        bool keyboard{};
        ToolStripPanelEdge key_edge{ToolStripPanelEdge::top};
        std::size_t key_stop{};
        std::size_t key_position{};
    };
    // A row's span across the panel, from its shown strips.
    struct RowSpan final {
        double start{};
        double end{};
    };
    // A strip's place among the four panels: its row and its position in
    // the row, and whether it is alone there.
    struct StripPlace final {
        ToolStripPanelEdge edge{ToolStripPanelEdge::top};
        std::size_t row{};
        std::size_t position{};
        bool alone{true};
    };

    // Where the panels sit for one client size.
    struct Layout final {
        Rect top{};
        Rect bottom{};
        Rect left{};
        Rect right{};
        Rect content{};
    };
    [[nodiscard]] Layout layout_for(Size client);
    void set_panel_visible(bool& flag, bool visible);
    [[nodiscard]] const std::shared_ptr<ToolStripPanel>& panel_on(ToolStripPanelEdge edge) const noexcept;
    [[nodiscard]] bool panel_shown(ToolStripPanelEdge edge) const noexcept;
    [[nodiscard]] std::optional<StripPlace> place_of(const ToolStrip& strip) const;
    // A rectangle of a descendant, in this container's coordinates.
    [[nodiscard]] Rect local_bounds_of(const Control& control) const noexcept;
    [[nodiscard]] Point local_point(Point window_point) const noexcept;
    // The strip whose grip is under a window point, or null.
    [[nodiscard]] std::shared_ptr<ToolStrip> grip_strip_at(Point window_point) const;
    [[nodiscard]] std::optional<ToolStripDropTarget> target_at(Point local) const;
    // The rows of the panel on `edge` as they are without `strip` (rows it
    // would leave empty dropped).
    [[nodiscard]] std::vector<std::vector<std::shared_ptr<ToolStrip>>> rows_without(ToolStripPanelEdge edge,
                                                                                   const ToolStrip& strip) const;
    // Whether `target` admits `strip` (a shown panel's edge, a StatusStrip
    // never left or right, a row and position in range).
    [[nodiscard]] bool admits(const ToolStrip& strip, const ToolStripDropTarget& target) const;
    // Whether `target` is where the strip already is.
    [[nodiscard]] static bool same_place(StripPlace place, const ToolStripDropTarget& target) noexcept;
    // Validates, cancels a drag, and moves unless the strip is in place.
    bool move_checked(const ToolStrip& strip, const ToolStripDropTarget& target);
    // Moves without cancelling a drag or checking the target again.
    void move_to(const std::shared_ptr<ToolStrip>& strip, StripPlace from, const ToolStripDropTarget& to);
    void arm_drag(const std::shared_ptr<ToolStrip>& strip, Point local, std::uint64_t pointer_id);
    void begin_drag();
    void update_drag(Point local, Modifier modifiers);
    // Ends a drag under way: a drop moves the strip to the target there is;
    // anything else leaves it. Then the stamp settles into the strip's place.
    void end_drag(bool drop);
    void finish_settling() noexcept;
    void reset_drag() noexcept;
    void release_picture(Window& host) noexcept;
    [[nodiscard]] Rect stamp_now() const;
    // Marks the stamp and the insertion line for repainting where they are.
    void invalidate_drag_paint();
    bool handle_drag_key(KeyEvent& event);
    struct FocusReturn;
    void return_focus() noexcept;
    [[nodiscard]] std::optional<RowSpan> row_span(const std::vector<std::shared_ptr<ToolStrip>>& row,
                                                  bool vertical) const;
    // The insertion line of a target for the dragged strip.
    [[nodiscard]] Rect marker_for(const ToolStripDropTarget& target) const;
    // The keyboard move's chosen target, or none when it admits nothing.
    [[nodiscard]] std::optional<ToolStripDropTarget> keyboard_target() const;
    void show_keyboard_target();
    void step_keyboard(int across, int along);
    void step_keyboard_panel(int step);
    // The strip joined here that holds the window's focus, or null.
    [[nodiscard]] std::shared_ptr<ToolStrip> focused_strip() const;
    void on_stamp_motion(const MotionStep& step);
    // The window lets go of the capture before it dispatches a primary
    // release, so a release of the capture is checked a turn later: the
    // drag ends as lost only when the capture did not come back and the
    // release did not end it.
    void on_capture_changed(const PointerCaptureChange& change);
    struct CaptureCheck;
    void check_capture_after_release();
    void paint_drag_layer(Painter& painter);

    std::shared_ptr<ToolStripPanel> top_{};
    std::shared_ptr<ToolStripPanel> bottom_{};
    std::shared_ptr<ToolStripPanel> left_{};
    std::shared_ptr<ToolStripPanel> right_{};
    std::shared_ptr<Panel> content_{};
    bool top_visible_{true};
    bool bottom_visible_{true};
    bool left_visible_{true};
    bool right_visible_{true};
    Event<const ToolStripMove&> strip_moved_{};
    // The drag: its session, the layer that paints it above the panels (a
    // child kept last), the stamp's motion (0 to 1 on the shared drag
    // curve: shrinking while dragging, settling after) and the capture
    // subscription that ends it, held while a drag is armed or under way.
    DragSession drag_{};
    std::shared_ptr<DragLayer> drag_layer_{};
    MotionValue stamp_motion_;
    SubscriptionToken capture_token_{};
    DispatchOperation capture_check_{};
    // A focus return the dispatcher's next turn tries again (return_focus).
    DispatchOperation focus_return_{};
    // drag_gesture::default_threshold, kept here so the public header needs
    // no private one.
    double drag_threshold_{4.0};
};

} // namespace gui_forms
