#include "gui_forms/controls/tool_strip_container/tool_strip_container.hpp"

#include "../drag_gesture/drag_gesture.hpp"
#include "gui_forms/controls/status_strip/status_strip.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/events/key_commands/key_commands.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// A measured thickness, with non-finite or negative answers as zero.
double usable(double thickness) noexcept {
    if (!std::isfinite(thickness) || thickness < 0.0) return 0.0;
    return thickness;
}

// One side panel's placement for arrange: the panel, its rectangle and
// whether it is shown.
struct PanelPlacement final {
    Control::Ptr panel{};
    Rect bounds{};
    bool shown{};
};

// An empty panel's drop band reaches this far in from its edge.
constexpr double empty_band = 12.0;
// The insertion line's thickness.
constexpr double marker_thickness = 3.0;
constexpr std::array<ToolStripPanelEdge, 4> panel_edges{ToolStripPanelEdge::top, ToolStripPanelEdge::bottom,
                                                        ToolStripPanelEdge::left, ToolStripPanelEdge::right};

bool vertical_edge(ToolStripPanelEdge edge) noexcept {
    const bool vertical = edge == ToolStripPanelEdge::left || edge == ToolStripPanelEdge::right;
    return vertical;
}

bool valid_edge(ToolStripPanelEdge edge) noexcept {
    const bool valid = edge == ToolStripPanelEdge::top || edge == ToolStripPanelEdge::bottom ||
                       edge == ToolStripPanelEdge::left || edge == ToolStripPanelEdge::right;
    return valid;
}

bool is_status_strip(const ToolStrip& strip) noexcept {
    const bool status = dynamic_cast<const StatusStrip*>(&strip) != nullptr;
    return status;
}

// The band a panel accepts drops in: the panel itself, or for a thinner
// panel (an empty one has no thickness) empty_band reaching in from the
// container's edge it lines.
Rect drop_band(ToolStripPanelEdge edge, Rect panel) noexcept {
    Rect band = panel;
    if (vertical_edge(edge)) {
        if (band.width >= empty_band) return band;
        band.width = empty_band;
        if (edge == ToolStripPanelEdge::right) band.x = panel.x + panel.width - empty_band;
        return band;
    }
    if (band.height >= empty_band) return band;
    band.height = empty_band;
    if (edge == ToolStripPanelEdge::bottom) band.y = panel.y + panel.height - empty_band;
    return band;
}

// The insertion line at `line` along the band's stacking axis, kept inside
// the band.
Rect marker_at(bool vertical, Rect band, double line) noexcept {
    if (vertical) {
        const double x = std::clamp(line - marker_thickness * 0.5, band.x,
                                    std::max(band.x, band.x + band.width - marker_thickness));
        const Rect marker{x, band.y, marker_thickness, band.height};
        return marker;
    }
    const double y = std::clamp(line - marker_thickness * 0.5, band.y,
                                std::max(band.y, band.y + band.height - marker_thickness));
    const Rect marker{band.x, y, band.width, marker_thickness};
    return marker;
}

} // namespace

// The layer the drag paints on: a child of the container kept above the
// panels, covering it, transparent to hit testing, taking no focus and
// publishing nothing (its parts are the container's).
class ToolStripContainer::DragLayer final : public Control {
public:
    DragLayer(StableId stable_id, ToolStripContainer& container)
        : Control(std::move(stable_id)), container_(&container) {
        set_paint_plane(PaintPlane::control);
        set_visible(false);
    }

    void release_container() noexcept { container_ = nullptr; }

    [[nodiscard]] bool hit_test_local(Point) const override { return false; }

    void on_paint(Painter& painter, Rect) override {
        if (container_ != nullptr) (*container_).paint_drag_layer(painter);
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.exposed = false;
        descriptor.include_descendants = false;
        return descriptor;
    }

private:
    ToolStripContainer* container_{};
};

ToolStripContainer::ToolStripContainer(StableId stable_id)
    : Control(std::move(stable_id)), stamp_motion_(*this, drag_gesture::stamp_curve(), 1.0) {
    set_paint_plane(PaintPlane::control);
    stamp_motion_.set_listener(MotionListener::bind<ToolStripContainer, &ToolStripContainer::on_stamp_motion>(*this));
}

ToolStripContainer::~ToolStripContainer() {
    capture_token_.disconnect();
    static_cast<void>(focus_return_.cancel());
    static_cast<void>(capture_check_.cancel());
    if (drag_layer_) (*drag_layer_).release_container();
}

void ToolStripContainer::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    top_ = make_control<ToolStripPanel>(StableId(prefix + ".top"), ToolStripPanelEdge::top);
    bottom_ = make_control<ToolStripPanel>(StableId(prefix + ".bottom"), ToolStripPanelEdge::bottom);
    left_ = make_control<ToolStripPanel>(StableId(prefix + ".left"), ToolStripPanelEdge::left);
    right_ = make_control<ToolStripPanel>(StableId(prefix + ".right"), ToolStripPanelEdge::right);
    content_ = make_control<Panel>(StableId(prefix + ".content"));
    // The content panel comes first, so Tab reaches the content before the
    // strips.
    add_child(content_);
    add_child(top_);
    add_child(left_);
    add_child(right_);
    add_child(bottom_);
    drag_layer_ = make_control<DragLayer>(StableId(prefix + ".drag-layer"), *this);
    add_child(drag_layer_);
}

void ToolStripContainer::set_panel_visible(bool& flag, bool visible) {
    require_mutable();
    if (flag == visible) return;
    flag = visible;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ToolStripContainer::set_top_panel_visible(bool visible) {
    set_panel_visible(top_visible_, visible);
}

void ToolStripContainer::set_bottom_panel_visible(bool visible) {
    set_panel_visible(bottom_visible_, visible);
}

void ToolStripContainer::set_left_panel_visible(bool visible) {
    set_panel_visible(left_visible_, visible);
}

void ToolStripContainer::set_right_panel_visible(bool visible) {
    set_panel_visible(right_visible_, visible);
}

// The top and bottom panels take the height their strips need across the
// whole width; the left and right panels take the width theirs need over the
// height between; the content panel takes what is left.
ToolStripContainer::Layout ToolStripContainer::layout_for(Size client) {
    Layout placed;
    const double width = std::max(0.0, client.width);
    const double height = std::max(0.0, client.height);
    // Each panel is offered what the panels before it left, and takes at
    // most that.
    double top = 0.0;
    if (top_ && top_visible_) {
        const Size wanted = (*top_).measure({width, height});
        top = std::min(height, usable(wanted.height));
    }
    double bottom = 0.0;
    if (bottom_ && bottom_visible_) {
        const Size wanted = (*bottom_).measure({width, height - top});
        bottom = std::min(height - top, usable(wanted.height));
    }
    const double middle = std::max(0.0, height - top - bottom);
    double left = 0.0;
    if (left_ && left_visible_) {
        const Size wanted = (*left_).measure({width, middle});
        left = std::min(width, usable(wanted.width));
    }
    double right = 0.0;
    if (right_ && right_visible_) {
        const Size wanted = (*right_).measure({width - left, middle});
        right = std::min(width - left, usable(wanted.width));
    }
    placed.top = {0.0, 0.0, width, top};
    placed.bottom = {0.0, height - bottom, width, bottom};
    placed.left = {0.0, top, left, middle};
    placed.right = {width - right, top, right, middle};
    placed.content = {left, top, std::max(0.0, width - left - right), middle};
    return placed;
}

void ToolStripContainer::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Layout placed = layout_for({final_bounds.width, final_bounds.height});
    // Measuring the panels measures their strips, whose program code may
    // dispose this container or take a panel out of it.
    if (!is_alive()) return;
    const std::array<PanelPlacement, 4> panels{{
        {.panel = top_, .bounds = placed.top, .shown = top_visible_},
        {.panel = bottom_, .bounds = placed.bottom, .shown = bottom_visible_},
        {.panel = left_, .bounds = placed.left, .shown = left_visible_},
        {.panel = right_, .bounds = placed.right, .shown = right_visible_},
    }};
    for (const PanelPlacement& placement : panels) {
        if (!placement.panel || !is_current_layout_child(placement.panel)) continue;
        set_child_layout_collapsed(placement.panel, !placement.shown);
        if (placement.shown) set_child_layout(placement.panel, placement.bounds);
    }
    if (content_ && is_current_layout_child(content_)) set_child_layout(content_, placed.content);
    if (drag_layer_ && is_current_layout_child(drag_layer_)) {
        set_child_layout(drag_layer_, Rect{0.0, 0.0, final_bounds.width, final_bounds.height});
    }
}

// -- moving strips -------------------------------------------------------------

const std::shared_ptr<ToolStripPanel>& ToolStripContainer::panel_on(ToolStripPanelEdge edge) const noexcept {
    switch (edge) {
    case ToolStripPanelEdge::bottom:
        return bottom_;
    case ToolStripPanelEdge::left:
        return left_;
    case ToolStripPanelEdge::right:
        return right_;
    case ToolStripPanelEdge::top:
    default:
        return top_;
    }
}

bool ToolStripContainer::panel_shown(ToolStripPanelEdge edge) const noexcept {
    switch (edge) {
    case ToolStripPanelEdge::bottom:
        return bottom_visible_;
    case ToolStripPanelEdge::left:
        return left_visible_;
    case ToolStripPanelEdge::right:
        return right_visible_;
    case ToolStripPanelEdge::top:
    default:
        return top_visible_;
    }
}

std::optional<ToolStripContainer::StripPlace> ToolStripContainer::place_of(const ToolStrip& strip) const {
    for (const ToolStripPanelEdge edge : panel_edges) {
        const std::shared_ptr<ToolStripPanel>& panel = panel_on(edge);
        if (!panel) continue;
        const std::vector<std::vector<std::shared_ptr<ToolStrip>>> joined = (*panel).rows();
        for (std::size_t row = 0U; row < joined.size(); ++row) {
            for (std::size_t position = 0U; position < joined[row].size(); ++position) {
                if (joined[row][position].get() != &strip) continue;
                const StripPlace place{edge, row, position, joined[row].size() == 1U};
                return place;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::vector<std::shared_ptr<ToolStrip>>> ToolStripContainer::rows_without(ToolStripPanelEdge edge,
                                                                                      const ToolStrip& strip) const {
    std::vector<std::vector<std::shared_ptr<ToolStrip>>> result;
    const std::shared_ptr<ToolStripPanel>& panel = panel_on(edge);
    if (!panel) return result;
    for (const std::vector<std::shared_ptr<ToolStrip>>& row : (*panel).rows()) {
        std::vector<std::shared_ptr<ToolStrip>> kept;
        kept.reserve(row.size());
        for (const std::shared_ptr<ToolStrip>& member : row) {
            if (member.get() != &strip) kept.push_back(member);
        }
        if (!kept.empty()) result.push_back(std::move(kept));
    }
    return result;
}

bool ToolStripContainer::admits(const ToolStrip& strip, const ToolStripDropTarget& target) const {
    if (!valid_edge(target.edge) || !panel_on(target.edge)) return false;
    if (vertical_edge(target.edge) && is_status_strip(strip)) return false;
    const std::vector<std::vector<std::shared_ptr<ToolStrip>>> rows = rows_without(target.edge, strip);
    if (!target.joins_row) return target.row <= rows.size();
    const bool admitted = target.row < rows.size() && target.position <= rows[target.row].size();
    return admitted;
}

// A strip alone in its row is in place for a new row at that row; a strip
// sharing a row is in place joining that row at its position (the rows
// without it keep that row).
bool ToolStripContainer::same_place(StripPlace place, const ToolStripDropTarget& target) noexcept {
    if (place.edge != target.edge || place.row != target.row) return false;
    if (place.alone) return !target.joins_row;
    const bool same = target.joins_row && target.position == place.position;
    return same;
}

bool ToolStripContainer::move_checked(const ToolStrip& strip, const ToolStripDropTarget& target) {
    require_mutable();
    const std::optional<StripPlace> place = place_of(strip);
    if (!place || !admits(strip, target)) return false;
    // The strip, owned: the panel's own pointer to it.
    const std::shared_ptr<ToolStrip> held =
        (*panel_on((*place).edge)).rows()[(*place).row][(*place).position];
    const Control::Ptr keep = shared_from_this();
    static_cast<void>(cancel_drag());
    if (same_place(*place, target)) return true;
    move_to(held, *place, target);
    return true;
}

bool ToolStripContainer::move_strip(const ToolStrip& strip, ToolStripPanelEdge edge, std::size_t row) {
    ToolStripDropTarget target{};
    target.edge = edge;
    target.row = row;
    const bool moved = move_checked(strip, target);
    return moved;
}

bool ToolStripContainer::move_strip_into_row(const ToolStrip& strip, ToolStripPanelEdge edge, std::size_t row,
                                             std::size_t position) {
    ToolStripDropTarget target{};
    target.edge = edge;
    target.row = row;
    target.joins_row = true;
    target.position = position;
    const bool moved = move_checked(strip, target);
    return moved;
}

// The target is checked: the strip is joined at `from`, and `to` admits it.
void ToolStripContainer::move_to(const std::shared_ptr<ToolStrip>& strip, StripPlace from,
                                 const ToolStripDropTarget& to) {
    std::shared_ptr<ToolStrip> removed = (*panel_on(from.edge)).remove(*strip);
    ToolStripPanel& panel = *panel_on(to.edge);
    if (to.joins_row) panel.join_row(std::move(removed), to.row, to.position);
    else panel.insert(std::move(removed), to.row);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const std::optional<StripPlace> now = place_of(*strip);
    ToolStripMove moved{};
    moved.strip_id = std::string((*strip).stable_id().value());
    moved.from = from.edge;
    moved.from_row = from.row;
    moved.from_position = from.position;
    moved.to = to.edge;
    moved.to_row = now ? (*now).row : to.row;
    moved.to_position = now ? (*now).position : to.position;
    strip_moved_.emit(moved);
}

// -- dragging strips -----------------------------------------------------------

Rect ToolStripContainer::local_bounds_of(const Control& control) const noexcept {
    const Rect mine = absolute_bounds();
    const Rect theirs = control.absolute_bounds();
    const Rect local{theirs.x - mine.x, theirs.y - mine.y, theirs.width, theirs.height};
    return local;
}

Point ToolStripContainer::local_point(Point window_point) const noexcept {
    const Rect mine = absolute_bounds();
    const Point local{window_point.x - mine.x, window_point.y - mine.y};
    return local;
}

std::shared_ptr<ToolStrip> ToolStripContainer::grip_strip_at(Point window_point) const {
    for (const ToolStripPanelEdge edge : panel_edges) {
        const std::shared_ptr<ToolStripPanel>& panel = panel_on(edge);
        if (!panel || !panel_shown(edge)) continue;
        for (const std::shared_ptr<ToolStrip>& strip : (*panel).strips()) {
            if (!(*strip).visible() || !(*strip).grip_visible()) continue;
            const Rect grip = (*strip).parts().grip.bounds;
            if (grip.empty()) continue;
            const Rect placed = (*strip).absolute_bounds();
            const Rect window_grip{placed.x + grip.x, placed.y + grip.y, grip.width, grip.height};
            if (window_grip.contains(window_point)) return strip;
        }
    }
    return nullptr;
}

// The first shown panel whose band holds the point, in the order top,
// bottom, left, right (the top and bottom panels own the corners). Its rows
// without the dragged strip are taken in order across the panel: the outer
// quarters of a row's thickness put the strip in a new row before or after
// it, the middle half joins it, before the first of its strips whose middle
// the point is not past. Rows with no shown strip count as passed while the
// point is past the row before them. Without shown rows the line lies on
// the band's container edge.
std::optional<ToolStripDropTarget> ToolStripContainer::target_at(Point local) const {
    const std::shared_ptr<ToolStrip> dragged = drag_.strip.lock();
    if (!dragged) return std::nullopt;
    const bool status = is_status_strip(*dragged);
    for (const ToolStripPanelEdge edge : panel_edges) {
        const std::shared_ptr<ToolStripPanel>& panel = panel_on(edge);
        if (!panel || !panel_shown(edge)) continue;
        const bool vertical = vertical_edge(edge);
        if (vertical && status) continue;
        const Rect band = drop_band(edge, local_bounds_of(*panel));
        if (!band.contains(local)) continue;
        // `across` runs across the rows, `along` along them.
        const double across = vertical ? local.x : local.y;
        const double along = vertical ? local.y : local.x;
        double line = vertical ? band.x : band.y;
        if (edge == ToolStripPanelEdge::bottom) line = band.y + band.height;
        if (edge == ToolStripPanelEdge::right) line = band.x + band.width;
        ToolStripDropTarget target{};
        target.edge = edge;
        const std::vector<std::vector<std::shared_ptr<ToolStrip>>> rows = rows_without(edge, *dragged);
        std::size_t row = 0U;
        for (; row < rows.size(); ++row) {
            // The row's span across, from its shown strips.
            double start = 0.0;
            double end = 0.0;
            bool shown = false;
            for (const std::shared_ptr<ToolStrip>& member : rows[row]) {
                if (!(*member).visible()) continue;
                const Rect bounds = local_bounds_of(*member);
                const double member_start = vertical ? bounds.x : bounds.y;
                const double member_end = member_start + (vertical ? bounds.width : bounds.height);
                start = shown ? std::min(start, member_start) : member_start;
                end = shown ? std::max(end, member_end) : member_end;
                shown = true;
            }
            if (!shown) continue;
            const double thickness = end - start;
            if (across < start + thickness * 0.25) {
                line = start;
                break;
            }
            if (across <= start + thickness * 0.75) {
                // Joins this row, before the first strip whose middle the
                // point is not past.
                std::size_t position = 0U;
                double between = vertical ? band.y : band.x;
                for (const std::shared_ptr<ToolStrip>& member : rows[row]) {
                    if (!(*member).visible()) {
                        ++position;
                        continue;
                    }
                    const Rect bounds = local_bounds_of(*member);
                    const double member_start = vertical ? bounds.y : bounds.x;
                    const double member_length = vertical ? bounds.height : bounds.width;
                    if (along < member_start + member_length * 0.5) {
                        between = member_start;
                        break;
                    }
                    ++position;
                    between = member_start + member_length;
                }
                const double half = marker_thickness * 0.5;
                target.row = row;
                target.joins_row = true;
                target.position = position;
                target.marker = vertical ? Rect{start, between - half, thickness, marker_thickness}
                                         : Rect{between - half, start, marker_thickness, thickness};
                return target;
            }
            line = end;
        }
        target.row = row;
        target.marker = marker_at(vertical, band, line);
        return target;
    }
    return std::nullopt;
}

Rect ToolStripContainer::drag_stamp() const noexcept {
    if (drag_.phase != ToolStripDragPhase::dragging && drag_.phase != ToolStripDragPhase::settling) return {};
    return drag_.stamp;
}

void ToolStripContainer::set_drag_threshold(double threshold) {
    require_mutable();
    if (!drag_gesture::valid_threshold(threshold)) {
        throw std::out_of_range("ToolStripContainer drag threshold must lie in 1 to 64");
    }
    drag_threshold_ = threshold;
}

Rect ToolStripContainer::stamp_now() const {
    const Rect stamp = drag_gesture::stamp_at(drag_.source, drag_.pointer, drag_.grab, stamp_motion_.current(),
                                              effective_text_scale());
    return stamp;
}

void ToolStripContainer::invalidate_drag_paint() {
    if (!drag_layer_) return;
    if (!drag_.stamp.empty()) (*drag_layer_).invalidate(drag_.stamp);
    if (drag_.target) (*drag_layer_).invalidate((*drag_.target).marker);
}

// A press on a grip arms the drag and captures the pointer, so the moves
// and the release come here.
void ToolStripContainer::arm_drag(const std::shared_ptr<ToolStrip>& strip, Point local, std::uint64_t pointer_id) {
    Window* const host = attached_window();
    const std::optional<StripPlace> place = place_of(*strip);
    if (host == nullptr || !place) return;
    reset_drag();
    DragSession session{};
    session.phase = ToolStripDragPhase::armed;
    session.strip = strip;
    session.from = (*place).edge;
    session.from_row = (*place).row;
    session.pointer_id = pointer_id;
    session.press = local;
    session.pointer = local;
    drag_ = std::move(session);
    (*host).capture_pointer(shared_from_this(), pointer_id);
    // Subscribed after taking the capture, whose own change is not a loss.
    capture_token_ = (*host).pointer_capture_changed().subscribe(
        *this,
        Delegate<const PointerCaptureChange&>::bind<ToolStripContainer, &ToolStripContainer::on_capture_changed>(*this));
}

// The armed drag starts: the strip's picture is taken, the container takes
// the focus (so Escape and Ctrl reach it), and the stamp starts shrinking
// from the strip's rectangle.
void ToolStripContainer::begin_drag() {
    Window* const host = attached_window();
    const std::shared_ptr<ToolStrip> strip = drag_.strip.lock();
    if (host == nullptr || !strip || !drag_layer_) {
        reset_drag();
        return;
    }
    drag_.phase = ToolStripDragPhase::dragging;
    drag_.source = local_bounds_of(*strip);
    drag_.grab = drag_gesture::grab_of(drag_.source, drag_.press);
    drag_.stamp = drag_.source;
    drag_.picture = (*host).snapshot_control(strip, (*host).scale());
    drag_.focus_before = (*host).focused_control();
    drag_.focusable_before = focusable();
    set_focusable(true);
    static_cast<void>((*host).request_focus(shared_from_this()));
    (*drag_layer_).set_visible(true);
    stamp_motion_.jump_to(0.0);
    static_cast<void>(stamp_motion_.animate_to(1.0));
    invalidate(Dirty::paint | Dirty::semantics);
}

void ToolStripContainer::update_drag(Point local, Modifier modifiers) {
    if (drag_.phase == ToolStripDragPhase::armed) {
        drag_.pointer = local;
        if (!drag_gesture::past_threshold(drag_.press, local, drag_threshold_)) return;
        begin_drag();
        if (drag_.phase != ToolStripDragPhase::dragging) return;
    }
    if (drag_.phase != ToolStripDragPhase::dragging) return;
    invalidate_drag_paint();
    drag_.pointer = local;
    drag_.suppressed = drag_gesture::suppresses_targets(modifiers);
    drag_.stamp = stamp_now();
    drag_.target.reset();
    if (!drag_.suppressed) drag_.target = target_at(local);
    invalidate_drag_paint();
}

struct ToolStripContainer::FocusReturn final {
    std::weak_ptr<Control> container{};
    std::weak_ptr<Control> target{};
    void operator()() const {
        const std::shared_ptr<Control> held = container.lock();
        const std::shared_ptr<Control> focus = target.lock();
        if (!held || !(*held).is_alive() || !focus || !(*focus).attached()) return;
        Window* const host = (*held).attached_window();
        if (host != nullptr) static_cast<void>((*host).request_focus(focus));
    }
};

// A strip just moved is not eligible for focus until the layout pass that
// ends the event's dispatch places it: a refused request is tried again on
// the dispatcher's next turn.
void ToolStripContainer::return_focus() noexcept {
    try {
        Window* const host = attached_window();
        const Control::Ptr before = drag_.focus_before.lock();
        // Focusability first: making the container unfocusable lets go of
        // the focus in its subtree.
        set_focusable(drag_.focusable_before);
        bool returned = false;
        if (host != nullptr && before && (*before).attached()) returned = (*host).request_focus(before);
        if (!returned && host != nullptr && before && (*host).dispatcher_snapshot().accepting) {
            static_cast<void>(focus_return_.cancel());
            focus_return_ = (*host).begin_invoke(shared_from_this(), FocusReturn{weak_from_this(), before});
        }
    } catch (...) {
    }
}

void ToolStripContainer::end_drag(bool drop) {
    if (drag_.phase == ToolStripDragPhase::armed) {
        reset_drag();
        return;
    }
    if (drag_.phase != ToolStripDragPhase::dragging) return;
    const Control::Ptr keep = shared_from_this();
    static_cast<void>(capture_check_.cancel());
    const std::shared_ptr<ToolStrip> strip = drag_.strip.lock();
    const std::optional<ToolStripDropTarget> target = drop ? drag_.target : std::nullopt;
    invalidate_drag_paint();
    drag_.target.reset();
    drag_.phase = ToolStripDragPhase::settling;
    drag_.settle_from = drag_.stamp;
    capture_token_.disconnect();
    Window* const host = attached_window();
    if (host != nullptr && (*host).captured_control().get() == this) (*host).release_pointer();
    // The target is checked again: the program may have moved the strip,
    // or hidden a panel, while it was dragged.
    if (target && strip) {
        const std::optional<StripPlace> place = place_of(*strip);
        const bool admitted = place && panel_shown((*target).edge) && admits(*strip, *target);
        if (admitted && !same_place(*place, *target)) move_to(strip, *place, *target);
        if (!is_alive()) return;
    }
    // After the move, which takes the strip (perhaps holding the focus
    // before the drag) out of the tree and back.
    return_focus();
    // The stamp settles into the strip's place (at once under reduced
    // motion or in an occluded window).
    stamp_motion_.jump_to(0.0);
    if (!strip || stamp_motion_.animate_to(1.0) != MotionStart::started) finish_settling();
}

void ToolStripContainer::finish_settling() noexcept {
    reset_drag();
    try {
        if (is_alive()) invalidate(Dirty::paint | Dirty::semantics);
    } catch (...) {
    }
}

void ToolStripContainer::release_picture(Window& host) noexcept {
    if (!drag_.picture) return;
    // A release that fails (allocation while marking the repaint) leaves
    // the image to the window's end.
    try {
        static_cast<void>(host.release_control_snapshot((*drag_.picture).image));
    } catch (...) {
    }
    drag_.picture.reset();
}

void ToolStripContainer::reset_drag() noexcept {
    capture_token_.disconnect();
    static_cast<void>(capture_check_.cancel());
    Window* const host = attached_window();
    if (host != nullptr) {
        release_picture(*host);
        try {
            if ((*host).captured_control().get() == this) (*host).release_pointer();
        } catch (...) {
        }
    }
    try {
        invalidate_drag_paint();
        if (drag_layer_) (*drag_layer_).set_visible(false);
    } catch (...) {
    }
    drag_ = DragSession{};
    stamp_motion_.finish();
}

bool ToolStripContainer::cancel_drag() {
    if (drag_.phase == ToolStripDragPhase::armed) {
        reset_drag();
        return true;
    }
    if (drag_.phase != ToolStripDragPhase::dragging) return false;
    end_drag(false);
    return true;
}

// While dragging, the stamp follows the shrink; while settling, it moves
// from where the drag ended into the strip's place as the layout pass left
// it, and the session ends when it gets there.
void ToolStripContainer::on_stamp_motion(const MotionStep& step) {
    if (drag_.phase == ToolStripDragPhase::dragging) {
        invalidate_drag_paint();
        drag_.stamp = stamp_now();
        invalidate_drag_paint();
        return;
    }
    if (drag_.phase != ToolStripDragPhase::settling) return;
    const std::shared_ptr<ToolStrip> strip = drag_.strip.lock();
    const Rect to = strip ? local_bounds_of(*strip) : drag_.settle_from;
    invalidate_drag_paint();
    drag_.stamp = drag_gesture::interpolate(drag_.settle_from, to, step.current);
    invalidate_drag_paint();
    if (step.settled) finish_settling();
}

struct ToolStripContainer::CaptureCheck final {
    std::weak_ptr<Control> container{};
    void operator()() const {
        const std::shared_ptr<Control> held = container.lock();
        if (!held || !(*held).is_alive()) return;
        ToolStripContainer& owner = static_cast<ToolStripContainer&>(*held);
        owner.check_capture_after_release();
    }
};

void ToolStripContainer::on_capture_changed(const PointerCaptureChange& change) {
    if (drag_.phase != ToolStripDragPhase::armed && drag_.phase != ToolStripDragPhase::dragging) return;
    if (change.captured && change.control_id == runtime_id()) return;
    if (change.captured) {
        // Another control took the capture.
        if (drag_.phase == ToolStripDragPhase::armed) reset_drag();
        else end_drag(false);
        return;
    }
    Window* const host = attached_window();
    if (host == nullptr) return;
    static_cast<void>(capture_check_.cancel());
    // A window whose dispatcher no longer runs has no next turn: decide now.
    if (!(*host).dispatcher_snapshot().accepting) {
        check_capture_after_release();
        return;
    }
    capture_check_ = (*host).begin_invoke(shared_from_this(), CaptureCheck{weak_from_this()});
}

void ToolStripContainer::check_capture_after_release() {
    if (drag_.phase != ToolStripDragPhase::armed && drag_.phase != ToolStripDragPhase::dragging) return;
    const Window* const host = attached_window();
    if (host != nullptr && (*host).captured_control().get() == this) return;
    if (drag_.phase == ToolStripDragPhase::armed) reset_drag();
    else end_drag(false);
}

void ToolStripContainer::on_pointer_preview(PointerEvent& event) {
    if (event.action != PointerAction::down || event.button != PointerButton::primary) return;
    // Presses wait while a drag settles.
    if (drag_.phase == ToolStripDragPhase::settling) {
        event.handled = true;
        return;
    }
    if (drag_.phase != ToolStripDragPhase::idle) return;
    const std::shared_ptr<ToolStrip> strip = grip_strip_at(event.position);
    if (!strip) return;
    const std::uint64_t pointer = event.pointer_id == 0U ? 1U : event.pointer_id;
    arm_drag(strip, local_point(event.position), pointer);
    if (drag_.phase == ToolStripDragPhase::armed) event.handled = true;
}

void ToolStripContainer::on_pointer(PointerEvent& event) {
    if (drag_.phase != ToolStripDragPhase::armed && drag_.phase != ToolStripDragPhase::dragging) return;
    // The keyboard move takes no pointer input.
    if (drag_.keyboard) return;
    const Control::Ptr keep = shared_from_this();
    const Point local = local_point(event.position);
    if (event.action == PointerAction::move) {
        update_drag(local, event.modifiers);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::up && event.button == PointerButton::primary) {
        if (drag_.phase == ToolStripDragPhase::armed) {
            reset_drag();
        } else {
            update_drag(local, event.modifiers);
            end_drag(true);
        }
        event.handled = true;
    }
}

// Escape cancels; Ctrl pressed or let go updates the suppression.
bool ToolStripContainer::handle_drag_key(KeyEvent& event) {
    if (event.handled) return false;
    if (drag_.phase == ToolStripDragPhase::armed && drag_gesture::cancels(event)) {
        reset_drag();
        event.handled = true;
        return true;
    }
    if (drag_.phase != ToolStripDragPhase::dragging) return false;
    const Control::Ptr keep = shared_from_this();
    if (drag_.keyboard) {
        // Every key is the keyboard move's.
        event.handled = true;
        if (event.action != KeyAction::down) return true;
        const std::uint32_t key = event.physical_key;
        const bool vertical = vertical_edge(drag_.key_edge);
        const std::uint32_t across_back = vertical ? PhysicalKey::left : PhysicalKey::up;
        const std::uint32_t across_forward = vertical ? PhysicalKey::right : PhysicalKey::down;
        const std::uint32_t along_back = vertical ? PhysicalKey::up : PhysicalKey::left;
        const std::uint32_t along_forward = vertical ? PhysicalKey::down : PhysicalKey::right;
        if (key == PhysicalKey::escape) end_drag(false);
        else if (key == PhysicalKey::enter) end_drag(true);
        else if (key == PhysicalKey::tab) step_keyboard_panel(has_modifier(event.modifiers, Modifier::shift) ? -1 : 1);
        else if (key == across_back) step_keyboard(-1, 0);
        else if (key == across_forward) step_keyboard(1, 0);
        else if (key == along_back) step_keyboard(0, -1);
        else if (key == along_forward) step_keyboard(0, 1);
        return true;
    }
    if (drag_gesture::suppresses_targets(event.modifiers) != drag_.suppressed) {
        update_drag(drag_.pointer, event.modifiers);
    }
    if (drag_gesture::cancels(event)) {
        end_drag(false);
        event.handled = true;
        return true;
    }
    return false;
}

void ToolStripContainer::on_key_preview(KeyEvent& event) {
    if (handle_drag_key(event)) return;
    if (event.handled || event.action != KeyAction::down || drag_.phase != ToolStripDragPhase::idle) return;
    if (!effective_key_map().matches(KeyCommand::move_panel, event)) return;
    const std::shared_ptr<ToolStrip> strip = focused_strip();
    if (strip && begin_keyboard_move(*strip)) event.handled = true;
}

// -- the keyboard move ---------------------------------------------------------

std::shared_ptr<ToolStrip> ToolStripContainer::focused_strip() const {
    const Window* const host = attached_window();
    if (host == nullptr) return nullptr;
    Control::Ptr control = (*host).focused_control();
    while (control) {
        std::shared_ptr<ToolStrip> strip = std::dynamic_pointer_cast<ToolStrip>(control);
        if (strip && place_of(*strip)) return strip;
        control = (*control).parent();
    }
    return nullptr;
}

bool ToolStripContainer::begin_keyboard_move(const ToolStrip& strip) {
    require_mutable();
    if (drag_.phase != ToolStripDragPhase::idle) return false;
    const std::optional<StripPlace> place = place_of(strip);
    if (attached_window() == nullptr || !place || !panel_shown((*place).edge)) return false;
    const std::shared_ptr<ToolStrip> held =
        (*panel_on((*place).edge)).rows()[(*place).row][(*place).position];
    reset_drag();
    DragSession session{};
    session.phase = ToolStripDragPhase::armed;
    session.strip = held;
    session.from = (*place).edge;
    session.from_row = (*place).row;
    session.keyboard = true;
    // Grabbed at its middle, so the stamp centres on the chosen place.
    const Rect bounds = local_bounds_of(strip);
    session.press = {bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    session.pointer = session.press;
    drag_ = std::move(session);
    begin_drag();
    if (drag_.phase != ToolStripDragPhase::dragging) return false;
    // Its own place: a new row where it is alone, or its position in the
    // row it shares.
    drag_.key_edge = (*place).edge;
    drag_.key_stop = (*place).alone ? (*place).row * 2U : (*place).row * 2U + 1U;
    drag_.key_position = (*place).position;
    show_keyboard_target();
    return true;
}

std::optional<ToolStripContainer::RowSpan> ToolStripContainer::row_span(
    const std::vector<std::shared_ptr<ToolStrip>>& row, bool vertical) const {
    std::optional<RowSpan> span{};
    for (const std::shared_ptr<ToolStrip>& member : row) {
        if (!(*member).visible()) continue;
        const Rect bounds = local_bounds_of(*member);
        const double start = vertical ? bounds.x : bounds.y;
        const double end = start + (vertical ? bounds.width : bounds.height);
        if (!span) span = RowSpan{start, end};
        else span = RowSpan{std::min((*span).start, start), std::max((*span).end, end)};
    }
    return span;
}

Rect ToolStripContainer::marker_for(const ToolStripDropTarget& target) const {
    const std::shared_ptr<ToolStrip> dragged = drag_.strip.lock();
    const std::shared_ptr<ToolStripPanel>& panel = panel_on(target.edge);
    if (!dragged || !panel) return {};
    const bool vertical = vertical_edge(target.edge);
    const Rect band = drop_band(target.edge, local_bounds_of(*panel));
    const std::vector<std::vector<std::shared_ptr<ToolStrip>>> rows = rows_without(target.edge, *dragged);
    // Without shown rows the line lies on the band's container edge.
    double line = vertical ? band.x : band.y;
    if (target.edge == ToolStripPanelEdge::bottom) line = band.y + band.height;
    if (target.edge == ToolStripPanelEdge::right) line = band.x + band.width;
    if (!target.joins_row) {
        std::optional<RowSpan> span{};
        if (target.row < rows.size()) span = row_span(rows[target.row], vertical);
        if (span) line = (*span).start;
        else if (target.row > 0U && target.row <= rows.size()) {
            const std::optional<RowSpan> before = row_span(rows[target.row - 1U], vertical);
            if (before) line = (*before).end;
        }
        return marker_at(vertical, band, line);
    }
    if (target.row >= rows.size()) return marker_at(vertical, band, line);
    const std::optional<RowSpan> span = row_span(rows[target.row], vertical);
    if (!span) return marker_at(vertical, band, line);
    // Between the strip before the position and the one at it.
    const std::vector<std::shared_ptr<ToolStrip>>& members = rows[target.row];
    double between = vertical ? band.y : band.x;
    for (std::size_t index = 0U; index < members.size() && index <= target.position; ++index) {
        if (!(*members[index]).visible()) continue;
        const Rect bounds = local_bounds_of(*members[index]);
        const double start = vertical ? bounds.y : bounds.x;
        const double length = vertical ? bounds.height : bounds.width;
        between = index == target.position ? start : start + length;
    }
    const double half = marker_thickness * 0.5;
    const double thickness = (*span).end - (*span).start;
    const Rect marker = vertical ? Rect{(*span).start, between - half, thickness, marker_thickness}
                                 : Rect{between - half, (*span).start, marker_thickness, thickness};
    return marker;
}

std::optional<ToolStripDropTarget> ToolStripContainer::keyboard_target() const {
    const std::shared_ptr<ToolStrip> dragged = drag_.strip.lock();
    if (!dragged) return std::nullopt;
    ToolStripDropTarget target{};
    target.edge = drag_.key_edge;
    target.row = drag_.key_stop / 2U;
    target.joins_row = drag_.key_stop % 2U == 1U;
    target.position = target.joins_row ? drag_.key_position : 0U;
    if (!panel_shown(target.edge) || !admits(*dragged, target)) return std::nullopt;
    target.marker = marker_for(target);
    return target;
}

// The stamp stands over the chosen place's line.
void ToolStripContainer::show_keyboard_target() {
    invalidate_drag_paint();
    drag_.target = keyboard_target();
    if (drag_.target) {
        const Rect marker = (*drag_.target).marker;
        drag_.pointer = {marker.x + marker.width * 0.5, marker.y + marker.height * 0.5};
    }
    drag_.stamp = stamp_now();
    invalidate_drag_paint();
}

// `across` steps the stop through the rows (a new row, then joining that
// row, in turn); `along` steps the position in a joined row. Both stop at
// the ends.
void ToolStripContainer::step_keyboard(int across, int along) {
    const std::shared_ptr<ToolStrip> dragged = drag_.strip.lock();
    if (!dragged) return;
    const std::vector<std::vector<std::shared_ptr<ToolStrip>>> rows = rows_without(drag_.key_edge, *dragged);
    const std::size_t last_stop = rows.size() * 2U;
    if (across < 0 && drag_.key_stop > 0U) --drag_.key_stop;
    if (across > 0 && drag_.key_stop < last_stop) ++drag_.key_stop;
    const bool joins = drag_.key_stop % 2U == 1U;
    if (joins) {
        const std::size_t members = rows[drag_.key_stop / 2U].size();
        if (along < 0 && drag_.key_position > 0U) --drag_.key_position;
        if (along > 0 && drag_.key_position < members) ++drag_.key_position;
        drag_.key_position = std::min(drag_.key_position, members);
    }
    show_keyboard_target();
}

// The next or previous shown panel (in the order top, bottom, left, right,
// wrapping) that admits the strip, at its first stop.
void ToolStripContainer::step_keyboard_panel(int step) {
    const std::shared_ptr<ToolStrip> dragged = drag_.strip.lock();
    if (!dragged) return;
    std::size_t index = 0U;
    for (std::size_t probe = 0U; probe < panel_edges.size(); ++probe) {
        if (panel_edges[probe] == drag_.key_edge) index = probe;
    }
    for (std::size_t tried = 0U; tried < panel_edges.size(); ++tried) {
        index = step < 0 ? (index + panel_edges.size() - 1U) % panel_edges.size() : (index + 1U) % panel_edges.size();
        const ToolStripPanelEdge edge = panel_edges[index];
        if (!panel_on(edge) || !panel_shown(edge)) continue;
        if (vertical_edge(edge) && is_status_strip(*dragged)) continue;
        drag_.key_edge = edge;
        drag_.key_stop = 0U;
        drag_.key_position = 0U;
        break;
    }
    show_keyboard_target();
}

void ToolStripContainer::on_key(KeyEvent& event) {
    static_cast<void>(handle_drag_key(event));
}

// In paint order: the insertion line (the drop tint, T3-1), then the stamp
// (a card face over the strip's picture, or over a well where there is none).
void ToolStripContainer::paint_drag_layer(Painter& painter) {
    if (drag_.phase != ToolStripDragPhase::dragging && drag_.phase != ToolStripDragPhase::settling) return;
    PartPaint part{};
    if (drag_.target) {
        part.family = ControlVisualRole::panel;
        part.role = PartRole::drop;
        part.bounds = (*drag_.target).marker;
        part.states.insert(AnatomyState::drop_accepted);
        part.accent = AccentIndex{0U};
        paint_part(painter, *this, part);
        part.accent.reset();
        part.states = {};
    }
    if (drag_.stamp.empty()) return;
    part.family = ControlVisualRole::card;
    part.role = PartRole::face;
    part.bounds = drag_.stamp;
    paint_part(painter, *this, part);
    if (drag_.picture) {
        part.role = PartRole::image;
        part.image = (*drag_.picture).image;
    } else {
        part.role = PartRole::well;
    }
    paint_part(painter, *this, part);
}

void ToolStripContainer::on_detaching_from_window(Window& former_window) noexcept {
    capture_token_.disconnect();
    static_cast<void>(capture_check_.cancel());
    // The stamp's picture is the former window's image.
    release_picture(former_window);
    drag_ = DragSession{};
    stamp_motion_.finish();
    try {
        if (drag_layer_) (*drag_layer_).set_visible(false);
    } catch (...) {
    }
}

void ToolStripContainer::on_dispose() noexcept {
    static_cast<void>(focus_return_.cancel());
    reset_drag();
    Control::on_dispose();
}

SemanticDescriptor ToolStripContainer::semantic_descriptor() const {
    // A layout frame: the strips and the content describe themselves.
    SemanticDescriptor descriptor = Control::semantic_descriptor();
    descriptor.role = SemanticRole::group;
    descriptor.exposed = !accessible_name().empty();
    return descriptor;
}

namespace {

constexpr std::array<AnatomyState, 1> drop_marker_states{AnatomyState::drop_accepted};

constexpr std::array<PartField<ToolStripContainer::Parts>, 9> tool_strip_container_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ToolStripContainer::Parts::region},
    {.declaration = {.name = "top_panel", .role = PartRole::face, .composed_from = "tool_strip_panel"},
     .member = &ToolStripContainer::Parts::top_panel},
    {.declaration = {.name = "bottom_panel", .role = PartRole::face, .composed_from = "tool_strip_panel"},
     .member = &ToolStripContainer::Parts::bottom_panel},
    {.declaration = {.name = "left_panel", .role = PartRole::face, .composed_from = "tool_strip_panel"},
     .member = &ToolStripContainer::Parts::left_panel},
    {.declaration = {.name = "right_panel", .role = PartRole::face, .composed_from = "tool_strip_panel"},
     .member = &ToolStripContainer::Parts::right_panel},
    {.declaration = {.name = "content_panel", .role = PartRole::region, .composed_from = "panel"},
     .member = &ToolStripContainer::Parts::content_panel},
    {.declaration = {.name = "drag_stamp", .role = PartRole::face, .composed_from = {}},
     .member = &ToolStripContainer::Parts::drag_stamp},
    {.declaration = {.name = "drag_stamp_image", .role = PartRole::image, .composed_from = {}},
     .member = &ToolStripContainer::Parts::drag_stamp_image},
    {.declaration = {.name = "drop_marker", .role = PartRole::drop, .composed_from = {}, .states = drop_marker_states},
     .member = &ToolStripContainer::Parts::drop_marker},
}};

constexpr std::array<PartDeclaration, 9> tool_strip_container_parts =
    part_declarations(tool_strip_container_part_fields);
constexpr std::array<AnatomyState, 5> tool_strip_container_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ControlAnatomy tool_strip_container_anatomy{
    .control_id = "tool_strip_container",
    .parts = tool_strip_container_parts,
    .states = tool_strip_container_states,
    .custom_states = {},
    .item_systems = {},
};

// A panel's published geometry: where it was placed, while it is shown.
PartGeometry panel_geometry(const Control::Ptr& panel, bool shown) {
    if (!panel || !shown) return PartGeometry{};
    return PartGeometry::from_bounds((*panel).committed_arranged_bounds());
}

} // namespace

const ControlAnatomy& ToolStripContainer::anatomy() const noexcept {
    return tool_strip_container_anatomy;
}

void ToolStripContainer::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.top_panel = panel_geometry(top_, top_visible_);
    record.bottom_panel = panel_geometry(bottom_, bottom_visible_);
    record.left_panel = panel_geometry(left_, left_visible_);
    record.right_panel = panel_geometry(right_, right_visible_);
    record.content_panel = panel_geometry(content_, true);
    const Rect stamp = drag_stamp();
    if (!stamp.empty()) {
        record.drag_stamp = PartGeometry::from_bounds(stamp);
        if (drag_.picture) record.drag_stamp_image = PartGeometry::from_bounds(stamp);
    }
    if (drag_.target) record.drop_marker = PartGeometry::from_bounds((*drag_.target).marker);
    write_parts(anatomy(), tool_strip_container_part_fields, record, geometry);
}

namespace {

// The events ToolStripContainer adds to those of Control.
constexpr std::array<InteractionDeclaration, 1> container_interactions{{
    {.name = "strip_moved", .payload = "ToolStripMove", .mutable_payload = false},
}};

} // namespace

void ToolStripContainer::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), container_interactions.begin(), container_interactions.end());
}

ToolStripContainer::Parts ToolStripContainer::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), tool_strip_container_part_fields, published.parts(), record);
    return record;
}

} // namespace gui_forms
