// DockSite's drag (stage DK phase D3; owner decisions DK-24 to DK-26,
// DK-30, DK-41): the state machine, the postage stamp, the targets, the
// drag layer and the keyboard move mode. The targets' geometry and their
// plan are DockLayout's neighbours in the core (dock_drop_targets).

#include "gui_forms/controls/docking/dock_site/dock_site.hpp"
#include "dock_site_children.hpp"
#include "../../drag_gesture/drag_gesture.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// A document tab sliding on its strip tears off this far outside the
// strip's band.
constexpr double tear_off_distance = 16.0;
// The rejected cue: its side and its offset from the pointer.
constexpr double cue_side = 14.0;
constexpr double cue_offset_x = 14.0;
constexpr double cue_offset_y = 16.0;

DockShapeRegion region_of(DockDropZone zone) noexcept {
    switch (zone) {
    case DockDropZone::top: return DockShapeRegion::top;
    case DockDropZone::left: return DockShapeRegion::left;
    case DockDropZone::right: return DockShapeRegion::right;
    case DockDropZone::bottom: return DockShapeRegion::bottom;
    case DockDropZone::center: return DockShapeRegion::tab_into;
    }
    return DockShapeRegion::tab_into;
}

std::string_view zone_word(DockDropZone zone) noexcept {
    switch (zone) {
    case DockDropZone::top: return "top";
    case DockDropZone::left: return "left";
    case DockDropZone::right: return "right";
    case DockDropZone::bottom: return "bottom";
    case DockDropZone::center: return "center";
    }
    return "center";
}

constexpr std::array<DockDropZone, dock_drop_zone_count> cross_order{
    DockDropZone::top, DockDropZone::left, DockDropZone::center, DockDropZone::right, DockDropZone::bottom};
constexpr std::array<DockDropZone, 4> edge_order{DockDropZone::top, DockDropZone::left, DockDropZone::right,
                                                 DockDropZone::bottom};

// The keyboard move mode's arrows within a pane: each steps `toward` its
// side; from the opposite side (`away`) it stops at the centre first.
struct ZoneStep final {
    std::uint32_t key{};
    DockDropZone toward{DockDropZone::center};
    DockDropZone away{DockDropZone::center};
};

constexpr std::array<ZoneStep, 4> zone_steps{{
    {PhysicalKey::left, DockDropZone::left, DockDropZone::right},
    {PhysicalKey::right, DockDropZone::right, DockDropZone::left},
    {PhysicalKey::up, DockDropZone::top, DockDropZone::bottom},
    {PhysicalKey::down, DockDropZone::bottom, DockDropZone::top},
}};

Rect site_surface(const Control& site) noexcept {
    const Rect client = site.client_rectangle();
    const Rect surface{0.0, 0.0, client.width, client.height};
    return surface;
}

} // namespace

struct DockSite::CaptureCheck final {
    std::weak_ptr<Control> site{};
    void operator()() const {
        const std::shared_ptr<Control> held = site.lock();
        if (!held || !(*held).is_alive()) return;
        DockSite& owner = static_cast<DockSite&>(*held);
        owner.check_capture_after_release();
    }
};

void DockSite::create_drag_layer() {
    const std::string identity = std::string(stable_id().value()) + ".drag-layer";
    drag_layer_ = make_control<DragLayer>(StableId(identity), *this);
    add_child(drag_layer_);
}

// -- queries -------------------------------------------------------------------

std::optional<DockSubject> DockSite::drag_subject() const {
    if (drag_.phase != DockDragPhase::armed && drag_.phase != DockDragPhase::dragging) return std::nullopt;
    const DockSubject subject = drag_.subject;
    return subject;
}

std::span<const DockDropTarget> DockSite::drag_targets() const noexcept {
    if (drag_.phase != DockDragPhase::dragging) return {};
    const std::span<const DockDropTarget> targets(drag_.targets);
    return targets;
}

std::span<const DockDropPane> DockSite::drag_panes() const noexcept {
    if (drag_.phase != DockDragPhase::dragging) return {};
    const std::span<const DockDropPane> panes(drag_.panes);
    return panes;
}

std::optional<DockDropTarget> DockSite::drag_target() const {
    if (drag_.phase != DockDragPhase::dragging || !drag_.target || *drag_.target >= drag_.targets.size()) {
        return std::nullopt;
    }
    const DockDropTarget target = drag_.targets[*drag_.target];
    return target;
}

Rect DockSite::drag_stamp() const noexcept {
    if (drag_.phase != DockDragPhase::dragging && drag_.phase != DockDragPhase::settling) return {};
    return drag_.stamp;
}

void DockSite::reset_drag() noexcept {
    if (drag_.picture && !drag_.guest) {
        Window* const host = attached_window();
        // A release that fails (allocation while marking the repaint) leaves
        // the image to the window's end.
        try {
            if (host != nullptr) static_cast<void>((*host).release_control_snapshot((*drag_.picture).image));
        } catch (...) {
        }
    }
    drag_ = DragSession{};
    // The session's motion ends with it.
    stamp_motion_.finish();
    tile_fade_.finish();
}

// The stamp's image area: the stamp below its label.
Rect DockSite::drag_image_bounds() const noexcept {
    const Rect label = drag_label_bounds();
    if (drag_.stamp.empty()) return {};
    const Rect image{drag_.stamp.x, label.bottom(), drag_.stamp.width,
                     std::max(0.0, drag_.stamp.bottom() - label.bottom())};
    return image;
}

bool DockSite::hidden_by_drag(DockNodeId stack) const noexcept {
    const bool hidden = drag_.phase == DockDragPhase::dragging && !drag_.panel_only && drag_.source_stack == stack;
    return hidden;
}

bool DockSite::dragged_out(DockNodeId stack, const DockPanelId& panel) const noexcept {
    const bool out = drag_.phase == DockDragPhase::dragging && drag_.panel_only && drag_.source_stack == stack &&
                     drag_.subject.panel == panel;
    return out;
}

bool DockSite::motion_reduced() const noexcept {
    const Window* const host = attached_window();
    const bool reduced = host != nullptr && (*host).presentation_settings().reduced_motion;
    return reduced;
}

// -- starting ------------------------------------------------------------------

// A press on a caption arms a drag of the whole stack (D7-1).
void DockSite::arm_caption_drag(std::size_t stack, Point local, std::uint64_t pointer_id) {
    if (drag_.phase != DockDragPhase::idle || stack >= stacks_.size()) return;
    reset_drag();
    drag_.phase = DockDragPhase::armed;
    drag_.subject = DockSubject::of_stack(stacks_[stack].stack);
    drag_.source_stack = stacks_[stack].stack;
    drag_.press = local;
    drag_.pointer = local;
    drag_.pointer_id = pointer_id == 0U ? 1U : pointer_id;
}

// A tool stack's strip does not reorder, so a tab that moved the strip's
// threshold is the docking drag's at once; a document strip keeps its live
// slide (tear_off_document_tab ends it when the pointer leaves the strip).
void DockSite::on_tab_drag(DockNodeId stack, PageDragStart& start) {
    if (drag_.phase != DockDragPhase::idle || workspace_ == nullptr || start.claimed) return;
    StackSlot* const slot = slot_for(stack);
    if (slot == nullptr || !(*slot).view) return;
    const std::shared_ptr<PageStrip>& strip = (*(*slot).view).strip();
    if (!strip || (*strip).reorder_enabled()) return;
    const DockNode* const node = (*workspace_).layout().find_node(stack);
    if (node == nullptr) return;
    const Rect absolute = absolute_bounds();
    const Point press{start.press_point.x - absolute.x, start.press_point.y - absolute.y};
    const Point pointer{start.pointer.x - absolute.x, start.pointer.y - absolute.y};
    const DockSubject subject = DockSubject::of_panel(DockPanelId(start.page.value));
    const bool panel_only = (*node).panels.size() >= 2U;
    if (begin_drag(subject, stack, panel_only, press, pointer, false, 1U)) start.claimed = true;
}

bool DockSite::tear_off_document_tab(const PointerEvent& event) {
    for (const StackSlot& slot : stacks_) {
        if (slot.collapsed || !slot.view || slot.kind != DockNodeKind::document_stack) continue;
        const std::shared_ptr<PageStrip>& strip = (*slot.view).strip();
        if (!strip) continue;
        const std::optional<PageId> dragged = (*strip).dragged_page();
        if (!dragged) continue;
        const Rect band = (*strip).absolute_bounds();
        const double distance = tear_off_distance * effective_text_scale();
        const bool outside = event.position.y < band.y - distance || event.position.y > band.bottom() + distance;
        if (!outside) return false;
        const DockNode* const node = (*workspace_).layout().find_node(slot.stack);
        if (node == nullptr) return false;
        const DockNodeId stack = slot.stack;
        const DockSubject subject = DockSubject::of_panel(DockPanelId((*dragged).value));
        const bool panel_only = (*node).panels.size() >= 2U;
        static_cast<void>((*strip).cancel_drag());
        const Rect absolute = absolute_bounds();
        const Point pointer{event.position.x - absolute.x, event.position.y - absolute.y};
        const bool started = begin_drag(subject, stack, panel_only, pointer, pointer, false, event.pointer_id);
        return started;
    }
    return false;
}

// The subject leaves its place: the next layout pass closes it up (the
// stack held out of the pass, or the panel's page out of its view). The
// model is not changed. The site takes the pointer (for a pointer drag) and
// the focus, so Escape and the move mode's keys reach it.
bool DockSite::begin_drag(const DockSubject& subject, DockNodeId source_stack, bool panel_only, Point press,
                          Point pointer, bool keyboard, std::uint64_t pointer_id) {
    if (workspace_ == nullptr || !drag_layer_) return false;
    if (drag_.phase != DockDragPhase::idle && drag_.phase != DockDragPhase::armed) return false;
    Window* const host = attached_window();
    StackSlot* const slot = slot_for(source_stack);
    // A stack behind side tabs is dragged only from its open flyout (phase
    // D5), whose rectangle is the stamp's start.
    if (host == nullptr || slot == nullptr || (*slot).bounds.empty()) return false;
    if ((*slot).collapsed && !(*slot).in_flyout) return false;
    close_position_menu();
    close_navigator();
    const DockNode* const node = (*workspace_).layout().find_node(source_stack);
    if (node == nullptr || (*node).panels.empty()) return false;
    DragSession session{};
    session.phase = DockDragPhase::dragging;
    session.subject = subject;
    session.source_stack = source_stack;
    session.panel_only = panel_only;
    session.keyboard = keyboard;
    session.pointer_id = pointer_id == 0U ? 1U : pointer_id;
    session.press = press;
    session.pointer = pointer;
    session.source = (*slot).bounds;
    session.grab = drag_gesture::grab_of(session.source, press);
    if (keyboard) session.grab = {0.5, 0.5};
    const DockPanelId shown = subject.kind == DockSubjectKind::panel ? subject.panel : (*node).panels[(*node).selected];
    session.title = (*workspace_).panel_title(shown);
    session.generation = (*workspace_).tree_generation();
    session.stamp = session.source;
    // A release where nothing accepts floats the subject (D4-1); a keyboard
    // move has no pointer to float at.
    session.may_float = !keyboard && (*workspace_).can_float(subject);
    // The panel's picture before it leaves its place: its view (tabs and
    // content; a tool stack's caption is drawn by role above it), at the
    // window's own scale so the stamp only ever shrinks it.
    if ((*slot).view) session.picture = (*host).snapshot_control((*slot).view, (*host).scale());
    reset_drag();
    drag_ = std::move(session);
    // The subject leaves an open flyout with it (the stack stays auto-hidden
    // in the model until a drop).
    if (flyout_panel_) close_flyout(DockFlyoutCause::programmatic);
    // The page leaves its view, or the whole stack the layout, at the next
    // layout pass.
    set_focusable(true);
    static_cast<void>((*host).request_focus(shared_from_this()));
    if (!keyboard) (*host).capture_pointer(shared_from_this(), drag_.pointer_id);
    (*drag_layer_).visible = true;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    (*drag_layer_).invalidate(Dirty::paint);
    announce_drag();
    // The stamp shrinks from the source's rectangle (at once under reduced
    // motion or in an occluded window).
    stamp_motion_.jump_to(0.0);
    static_cast<void>(stamp_motion_.animate_to(1.0));
    return true;
}

bool DockSite::begin_keyboard_move(const DockPanelId& panel) {
    require_mutable();
    if (workspace_ == nullptr || drag_.phase != DockDragPhase::idle) return false;
    const DockPanelLocation location = (*workspace_).layout().locate(panel);
    if (location.placement != DockPlacementKind::docked && location.placement != DockPlacementKind::floating) {
        return false;
    }
    const StackSlot* const slot = slot_for(location.stack);
    const DockNode* const node = (*workspace_).layout().find_node(location.stack);
    if (slot == nullptr || node == nullptr || (*slot).collapsed) return false;
    const Rect bounds = (*slot).bounds;
    const Point centre{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    const bool panel_only = (*node).panels.size() >= 2U;
    const bool started = begin_drag(DockSubject::of_panel(panel), location.stack, panel_only, centre, centre, true, 1U);
    return started;
}

// The panes of the layout as it stands with the subject's place closed up,
// in tree order, and the targets over them (DockLayout::check decides each).
// A pointer drag keeps its target at the pointer; the keyboard keeps the
// same target, or the first.
void DockSite::plan_drag_targets() {
    if (workspace_ == nullptr) return;
    std::optional<DockDropTarget> previous = drag_target();
    drag_.panes.clear();
    bool document_stack_shown = false;
    for (const StackSlot& slot : stacks_) {
        if (slot.collapsed || slot.bounds.empty()) continue;
        if (slot.kind == DockNodeKind::document_stack) document_stack_shown = true;
        DockDropPane pane{slot.stack, slot.bounds, DockNodeId{}, Rect{}};
        if (slot.kind == DockNodeKind::document_stack && !floating()) {
            pane.area = (*workspace_).layout().document_area();
            pane.area_bounds = document_area_;
        }
        drag_.panes.push_back(pane);
    }
    if (!document_stack_shown && !document_area_.empty()) {
        drag_.panes.push_back(DockDropPane{(*workspace_).layout().document_area(), document_area_, DockNodeId{}, Rect{}});
    }
    const DockNodeId own = drag_.panel_only ? drag_.source_stack : DockNodeId{};
    const DockDropEdges edges = floating() ? DockDropEdges::none : DockDropEdges::global;
    plan_dock_drop_targets((*workspace_).layout(), drag_.subject, own, site_surface(*this), drag_.panes,
                           drag_.targets, edges);
    if (drag_.keyboard) {
        std::optional<std::size_t> kept{};
        for (std::size_t index = 0; index < drag_.targets.size() && previous; ++index) {
            const DockDropTarget& target = drag_.targets[index];
            if (target.kind == (*previous).kind && target.pane == (*previous).pane && target.zone == (*previous).zone) {
                kept = index;
            }
        }
        if (!kept && !drag_.targets.empty()) kept = 0U;
        std::optional<std::size_t> pane{};
        if (kept && drag_.targets[*kept].kind == DockDropTargetKind::pane) {
            for (std::size_t index = 0; index < drag_.panes.size(); ++index) {
                if (drag_.panes[index].node == drag_.targets[*kept].pane) pane = index;
            }
        }
        select_drag_target(kept, pane);
        return;
    }
    update_drag_pointer(drag_.pointer, drag_.suppressed ? Modifier::control : Modifier::none);
}

// -- following -----------------------------------------------------------------

void DockSite::update_drag_pointer(Point local, Modifier modifiers) {
    if (drag_.phase == DockDragPhase::armed) {
        drag_.pointer = local;
        if (!drag_gesture::past_threshold(drag_.press, local, drag_threshold.read())) return;
        const DockSubject subject = drag_.subject;
        const DockNodeId stack = drag_.source_stack;
        const Point press = drag_.press;
        const std::uint64_t pointer_id = drag_.pointer_id;
        reset_drag();
        // The caption of a floating window's only tool stack moves the window
        // itself (phase D4, decision 12).
        if (subject.kind == DockSubjectKind::stack && moves_window_by_caption() && begin_window_move(pointer_id)) {
            return;
        }
        static_cast<void>(begin_drag(subject, stack, false, press, local, false, pointer_id));
        return;
    }
    if (drag_.phase != DockDragPhase::dragging || drag_.keyboard) return;
    const Rect before = drag_paint_extent();
    drag_.pointer = local;
    drag_.suppressed = drag_gesture::suppresses_targets(modifiers);
    drag_.stamp = stamp_at();
    const DockDropEdges edges = floating() ? DockDropEdges::none : DockDropEdges::global;
    const DockDropHit hit = hit_dock_drop(local, site_surface(*this), drag_.panes, effective_text_scale(), edges);
    drag_.over_site = hit.over_site;
    std::optional<std::size_t> target{};
    std::optional<std::size_t> pane{};
    if (!drag_.suppressed) {
        target = find_dock_drop_target(drag_.targets, drag_.panes, hit);
        pane = hit.edge ? std::nullopt : hit.pane;
    }
    // Across windows the topmost site under the desktop pointer shows the
    // targets; this one shows none while that is another (phase D4).
    std::optional<Point> desktop{};
    if (!drag_.guest && workspace_ != nullptr && (*workspace_).windows_attached()) desktop = to_desktop(local);
    DockSite* under = this;
    if (desktop) under = remote_site(*desktop, nullptr);
    if (under != this) {
        target.reset();
        pane.reset();
        drag_.over_site = false;
    }
    select_drag_target(target, pane);
    if (desktop) route_drag(*desktop, modifiers, nullptr);
    if (drag_layer_) {
        (*drag_layer_).invalidate(before);
        (*drag_layer_).invalidate(drag_paint_extent());
    }
}

void DockSite::select_drag_target(std::optional<std::size_t> target, std::optional<std::size_t> hovered_pane) {
    const bool pane_changed = drag_.hovered_pane != hovered_pane;
    const bool target_changed = drag_.target != target;
    drag_.target = target;
    if (pane_changed) {
        drag_.hovered_pane = hovered_pane;
        // A newly hovered pane's cross fades in (at once under reduced
        // motion or in an occluded window).
        if (hovered_pane) {
            tile_fade_.jump_to(0.0);
            static_cast<void>(tile_fade_.animate_to(1.0));
        } else {
            tile_fade_.finish();
        }
    }
    if (drag_.keyboard) {
        // The keyboard's stamp stands over the target's area.
        const std::optional<DockDropTarget> chosen = drag_target();
        const Rect area = chosen ? (*chosen).highlight : site_surface(*this);
        drag_.pointer = {area.x + area.width * 0.5, area.y + area.height * 0.5};
        drag_.over_site = true;
        drag_.stamp = stamp_at();
    }
    if (pane_changed || target_changed) {
        announce_drag();
        invalidate(Dirty::paint | Dirty::semantics);
        if (drag_layer_) (*drag_layer_).invalidate(Dirty::paint);
    }
}

// The stamp (drag_gesture.hpp) where stamp_motion_'s shrink stands now (1
// at once under reduced motion, where it jumps to its end).
Rect DockSite::stamp_at() const {
    const Rect stamp = drag_gesture::stamp_at(drag_.source, drag_.pointer, drag_.grab, stamp_motion_.current(),
                                              effective_text_scale());
    return stamp;
}

// Everything the layer draws that moves with the pointer: the stamp, the
// tinted area, the hovered pane's cross and the cue.
Rect DockSite::drag_paint_extent() const {
    Rect extent = drag_.stamp;
    const std::optional<DockDropTarget> target = drag_target();
    if (target) extent = Rect::united(extent, (*target).highlight);
    if (drag_.hovered_pane && *drag_.hovered_pane < drag_.panes.size()) {
        extent = Rect::united(extent, drag_.panes[*drag_.hovered_pane].bounds);
    }
    const Rect cue = drop_cue_bounds();
    if (!cue.empty()) extent = Rect::united(extent, cue);
    return extent;
}

// -- ending --------------------------------------------------------------------

// The end of a drag that left the armed phase. A drop on a target applies
// its edit as one user transaction (refused only if the layout changed
// since the targets were planned, which ends the drag first); anything else
// leaves the layout as it was. Either way the subject's place is restored to
// the layout pass, the stamp settles into the panel's rectangle there, and
// the panel becomes active again.
void DockSite::end_drag(DockDragEnd end) {
    if (drag_.phase == DockDragPhase::armed) {
        reset_drag();
        return;
    }
    if (drag_.phase != DockDragPhase::dragging || workspace_ == nullptr || drag_.guest) return;
    const Control::Ptr keep = shared_from_this();
    // How the drag ends is settled step by step below; its outcome is built
    // once, from these locals, just before it is emitted.
    DockDragEnd ending = end;
    const DockSubject subject = drag_.subject;
    bool across_windows = false;
    // A target in another window is that site's; it stops showing the drag
    // before the drop's transaction.
    DockSite* const guest = remote_guest();
    std::optional<DockDropTarget> target = drag_target();
    std::string described{};
    if (guest != nullptr) {
        target = (*guest).drag_target();
        if (target) described = (*guest).describe_target(*target);
        (*guest).leave_guest_drag();
        drag_.remote.reset();
        across_windows = target.has_value();
    } else if (target) {
        described = describe_target(*target);
    }
    if (end == DockDragEnd::dropped && !target) ending = DockDragEnd::rejected;
    if (ending == DockDragEnd::dropped && drag_.generation != (*workspace_).tree_generation()) {
        ending = DockDragEnd::layout_changed;
    }
    // Released where nothing accepts: it floats at the pointer when it may
    // (D4-1).
    std::optional<Rect> float_frame{};
    if (ending == DockDragEnd::rejected && end == DockDragEnd::dropped && drag_.may_float) {
        float_frame = float_frame_at(drag_.pointer);
    }
    const DockNodeId source_stack = drag_.source_stack;
    const bool panel_only = drag_.panel_only;
    const bool keyboard = drag_.keyboard;
    DockPanelId panel = drag_.subject.panel;
    if (drag_.subject.kind == DockSubjectKind::stack) {
        const DockNode* const node = (*workspace_).layout().find_node(drag_.subject.stack);
        if (node != nullptr && !(*node).panels.empty()) panel = (*node).panels[(*node).selected];
    }
    // Settling first: the subject's place returns to the layout pass and a
    // drop's transaction is not taken for the program's.
    drag_.phase = DockDragPhase::settling;
    drag_.settle_panel = panel;
    drag_.settle_found = false;
    drag_.settle_from = drag_.stamp;
    drag_.hovered_pane.reset();
    tile_fade_.finish();
    Window* const host = attached_window();
    if (host != nullptr && !keyboard && (*host).captured_control().get() == this) (*host).release_pointer();
    DockTransactionResult result{};
    if (ending == DockDragEnd::dropped) {
        DockTransaction transaction{};
        transaction.origin = DockEditOrigin::user;
        transaction.edits.push_back((*target).edit);
        result = (*workspace_).apply(transaction);
        if (!result.applied) ending = DockDragEnd::rejected;
    } else if (float_frame) {
        result = (*workspace_).float_subject(subject, *float_frame);
        if (result.applied) ending = DockDragEnd::floated;
    }
    if (!is_alive() || workspace_ == nullptr) return;
    if (ending != DockDragEnd::dropped && ending != DockDragEnd::floated && panel_only) {
        StackSlot* const slot = slot_for(source_stack);
        const DockNode* const node = (*workspace_).layout().find_node(source_stack);
        if (slot != nullptr && node != nullptr) sync_pages(*slot, *node);
    }
    if (ending == DockDragEnd::dropped) {
        drag_.announcement = drag_.title + ": " + described + ", done";
    } else if (ending == DockDragEnd::floated) {
        drag_.announcement = drag_.title + ": floating in a new window";
    } else {
        drag_.announcement = drag_.title + ": back in its place";
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if (drag_layer_) (*drag_layer_).invalidate(Dirty::paint);
    const DockActivationCause cause = keyboard ? DockActivationCause::keyboard : DockActivationCause::pointer;
    static_cast<void>((*workspace_).activate_panel(panel, cause));
    if (!is_alive()) return;
    // Only a drop keeps its target and only a float its frame; every other
    // end reports the subject alone.
    DockDragOutcome outcome = DockDragOutcome::ended(ending, subject);
    if (ending == DockDragEnd::dropped) {
        outcome = DockDragOutcome::dropped(subject, *target, result, across_windows);
    } else if (ending == DockDragEnd::floated) {
        outcome = DockDragOutcome::floated(subject, result, *float_frame);
    }
    drag_ended_.emit(outcome);
}

// At the layout pass after the end: the stamp grows from where the drag
// ended into the panel's rectangle (reduced motion: at once).
void DockSite::place_settle_target() {
    if (drag_.settle_found || workspace_ == nullptr) return;
    const DockPanelLocation location = (*workspace_).layout().locate(drag_.settle_panel);
    const StackSlot* const slot = slot_for(location.stack);
    if (slot == nullptr || (*slot).collapsed || (*slot).bounds.empty() || motion_reduced()) {
        finish_settling();
        return;
    }
    drag_.settle_found = true;
    drag_.settle_to = (*slot).bounds;
    drag_.stamp = drag_.settle_from;
    stamp_motion_.jump_to(0.0);
    // An occluded window (or a window that just turned reduced motion on)
    // shows no settling: the stamp is gone at once.
    if (stamp_motion_.animate_to(1.0) != MotionStart::started) finish_settling();
}

void DockSite::finish_settling() noexcept {
    reset_drag();
    try {
        if (drag_layer_) (*drag_layer_).visible = false;
        if (is_alive()) invalidate(Dirty::paint | Dirty::semantics);
    } catch (...) {
    }
}

bool DockSite::cancel_drag() {
    if (drag_.phase == DockDragPhase::armed) {
        reset_drag();
        return true;
    }
    if (drag_.phase != DockDragPhase::dragging) return false;
    end_drag(DockDragEnd::escaped);
    return true;
}

void DockSite::on_capture_changed(const PointerCaptureChange& change) {
    if (window_move_.active && !(change.captured && change.control_id == runtime_id())) {
        end_window_move(false);
        return;
    }
    if (drag_.phase != DockDragPhase::dragging || drag_.keyboard || drag_.guest) return;
    if (change.captured && change.control_id == runtime_id()) return;
    if (change.captured) {
        end_drag(DockDragEnd::capture_lost);
        return;
    }
    Window* const host = attached_window();
    if (host == nullptr) return;
    static_cast<void>(capture_check_.cancel());
    // The check waits a turn, in case the capture comes back at once. A
    // window whose dispatcher no longer runs has no next turn: decide now.
    if (!(*host).dispatcher_snapshot().accepting) {
        check_capture_after_release();
        return;
    }
    capture_check_ = (*host).begin_invoke(shared_from_this(), CaptureCheck{weak_from_this()});
}

void DockSite::check_capture_after_release() {
    if (drag_.phase != DockDragPhase::dragging || drag_.keyboard || drag_.guest) return;
    const Window* const host = attached_window();
    if (host != nullptr && (*host).captured_control().get() == this) return;
    end_drag(DockDragEnd::capture_lost);
}

void DockSite::on_window_active(bool active) {
    if (active) {
        // The window became key: the panel focus is in there is the active
        // one again (focus did not move inside the window, phase D4).
        const std::optional<DockPanelId> panel = focused_panel();
        if (panel && workspace_ != nullptr) (*workspace_).note_active(*panel, DockActivationCause::focus);
        return;
    }
    if (drag_.guest || window_move_.active) return;
    if (drag_.phase == DockDragPhase::armed) reset_drag();
    if (drag_.phase == DockDragPhase::dragging) end_drag(DockDragEnd::deactivated);
}

// -- motion --------------------------------------------------------------------

// A step of the stamp's shrink or settling, or of the tiles' fade, on the
// window's motion clock. While dragging, the stamp follows its progress and
// the tiles stop publishing `expanding` when their fade settles; while
// settling, the stamp grows into its place and the session ends when it
// gets there. The layer repaints where the stamp was and is.
void DockSite::on_drag_motion(const MotionStep& step) {
    if (drag_.phase == DockDragPhase::dragging) {
        const Rect before = drag_paint_extent();
        drag_.stamp = stamp_at();
        if (step.value == &tile_fade_ && step.settled) invalidate(Dirty::paint);
        if (drag_layer_) {
            (*drag_layer_).invalidate(before);
            (*drag_layer_).invalidate(drag_paint_extent());
        }
        return;
    }
    if (drag_.phase != DockDragPhase::settling || !drag_.settle_found || step.value != &stamp_motion_) return;
    const Rect before = drag_.stamp;
    drag_.stamp = drag_gesture::interpolate(drag_.settle_from, drag_.settle_to, step.current);
    if (drag_layer_) {
        (*drag_layer_).invalidate(before);
        (*drag_layer_).invalidate(drag_.stamp);
    }
    if (step.settled) finish_settling();
}

// -- tiles ---------------------------------------------------------------------

std::size_t DockSite::tile_count() const noexcept {
    if (drag_.phase != DockDragPhase::dragging) return 0U;
    // A floating site has no window-edge targets.
    std::size_t count = floating() ? 0U : edge_order.size();
    if (drag_.hovered_pane && *drag_.hovered_pane < drag_.panes.size()) count += dock_drop_zone_count;
    return count;
}

Rect DockSite::tile_glyph_bounds(Rect tile) noexcept {
    const double inset = std::floor(tile.width * 0.2);
    const Rect glyph{tile.x + inset, tile.y + inset, std::max(0.0, tile.width - inset * 2.0),
                     std::max(0.0, tile.height - inset * 2.0)};
    return glyph;
}

// The hovered pane's cross first (in DockDropZone order), then the edges.
DockSite::TileSlot DockSite::tile_at(std::size_t tile) const {
    TileSlot slot{};
    const double scale = effective_text_scale();
    std::size_t index = tile;
    const bool cross = drag_.hovered_pane && *drag_.hovered_pane < drag_.panes.size();
    const std::optional<DockDropTarget> chosen = drag_target();
    if (cross && index < dock_drop_zone_count) {
        const DockDropPane& pane = drag_.panes[*drag_.hovered_pane];
        slot.kind = DockDropTargetKind::pane;
        slot.pane = pane.node;
        slot.zone = cross_order[index];
        slot.bounds = dock_cross_tiles(pane.bounds, scale)[static_cast<std::size_t>(slot.zone)];
    } else {
        if (cross) index -= dock_drop_zone_count;
        slot.kind = DockDropTargetKind::edge;
        slot.zone = edge_order[std::min(index, edge_order.size() - 1U)];
        slot.bounds = dock_edge_tiles(site_surface(*this), scale)[static_cast<std::size_t>(slot.zone)];
    }
    slot.refused = true;
    for (const DockDropTarget& target : drag_.targets) {
        const bool same = target.kind == slot.kind && target.zone == slot.zone &&
                          (slot.kind == DockDropTargetKind::edge || target.pane == slot.pane);
        if (same) slot.refused = false;
    }
    slot.lit = chosen && (*chosen).kind == slot.kind && (*chosen).zone == slot.zone &&
               (slot.kind == DockDropTargetKind::edge || (*chosen).pane == slot.pane);
    return slot;
}

AnatomyStateSet DockSite::tile_states(const TileSlot& tile) const {
    AnatomyStateSet states{};
    if (tile.lit) states.insert(AnatomyState::selected);
    if (tile.refused) states.insert(AnatomyState::disabled);
    if (tile.kind == DockDropTargetKind::pane && tiles_fading()) states.insert(AnatomyState::expanding);
    return states;
}

std::string DockSite::tile_identity(const TileSlot& tile) const {
    std::string identity(stable_id().value());
    if (tile.kind == DockDropTargetKind::edge) {
        identity += ".tile.edge.";
    } else {
        identity += ".tile." + std::to_string(tile.pane.value) + ".";
    }
    identity += std::string(zone_word(tile.zone));
    return identity;
}

// -- painting ------------------------------------------------------------------

Rect DockSite::drag_label_bounds() const noexcept {
    const Rect stamp = drag_.stamp;
    if (stamp.empty() || drag_.source.height <= 0.0) return {};
    const double ratio = stamp.height / drag_.source.height;
    const double height = std::min(stamp.height, std::max(4.0, caption_height() * ratio));
    const Rect label{stamp.x, stamp.y, stamp.width, height};
    return label;
}

// The cue under the pointer while it is inside the site with nothing to
// accept a drop (D4-1), or with docking suppressed.
Rect DockSite::drop_cue_bounds() const noexcept {
    // Shown only where a release snaps back: a subject that may float floats
    // there instead (D4-1).
    if (drag_.phase != DockDragPhase::dragging || drag_.keyboard || !drag_.over_site || drag_.target ||
        drag_.may_float) {
        return {};
    }
    const double scale = effective_text_scale();
    const Rect cue{drag_.pointer.x + cue_offset_x * scale, drag_.pointer.y + cue_offset_y * scale,
                   cue_side * scale, cue_side * scale};
    return cue;
}

// In paint order: the tinted area, the stamp (a card face with the
// subject's caption strip and label over the panel's picture, or over its
// content well when there is none), the tiles (the hovered
// pane's cross, then the edges), the cue. The stamp's text shrinks with it.
void DockSite::paint_drag_layer(Painter& painter) {
    if (drag_.phase != DockDragPhase::dragging && drag_.phase != DockDragPhase::settling) return;
    PartPaint part{};
    const std::optional<DockDropTarget> target = drag_target();
    if (target) {
        part.family = ControlVisualRole::panel;
        part.role = PartRole::drop;
        part.bounds = (*target).highlight;
        part.states.insert(AnatomyState::drop_accepted);
        // The translucent blue tint with its edge (T3-1) is the theme's;
        // today's accent ramp gives the tint.
        part.accent = AccentIndex{0U};
        paint_part(painter, *this, part);
        part.accent.reset();
        part.states = {};
    }
    if (!drag_.stamp.empty() && !drag_.stamp_hidden) {
        part.family = ControlVisualRole::card;
        part.role = PartRole::face;
        part.bounds = drag_.stamp;
        paint_part(painter, *this, part);
        const Rect label = drag_label_bounds();
        part.family = ControlVisualRole::panel;
        part.states.insert(AnatomyState::current);
        part.role = PartRole::face;
        part.bounds = label;
        paint_part(painter, *this, part);
        part.states = {};
        FontSpec font = caption_font();
        if (drag_.source.height > 0.0) font.size = std::max(1.0, font.size * drag_.stamp.height / drag_.source.height);
        part.font = font;
        part.role = PartRole::label;
        part.text = drag_.title;
        paint_part(painter, *this, part);
        part.text = {};
        part.bounds = drag_image_bounds();
        if (drag_.picture) {
            // The panel's picture, scaled with the stamp: content, drawn as
            // given (the image role, T2 GIVEN H1-1).
            part.role = PartRole::image;
            part.image = (*drag_.picture).image;
            paint_part(painter, *this, part);
            part.image = {};
        } else {
            // Without a picture the content well stands in (headless, or a
            // host that cannot snapshot).
            part.role = PartRole::well;
            paint_part(painter, *this, part);
        }
    }
    const std::size_t tiles = tile_count();
    for (std::size_t index = 0; index < tiles; ++index) {
        const TileSlot tile = tile_at(index);
        // The quiet tiles' look (dark plates, the shape in blue, reduced
        // strength) is the theme's (T2-4); the engine's built-in rules draw
        // the pill plate, the gold plate while lit (E8). The fade is the
        // engine's (T2 GIVEN
        // C2-1): keyed by the tile's identity, a tile entering `expanding`
        // fades in by the built-in rules.
        const std::string identity = tile_identity(tile);
        part.family = ControlVisualRole::button;
        part.states = tile_states(tile);
        part.role = PartRole::actuator;
        part.bounds = tile.bounds;
        part.key = PartKey{"tile", identity, {}};
        paint_part(painter, *this, part);
        part.role = PartRole::glyph;
        part.mark = PartMark::dock_shape;
        part.dock_region = region_of(tile.zone);
        part.bounds = tile_glyph_bounds(tile.bounds);
        part.key = PartKey{"tile_glyph", identity, {}};
        paint_part(painter, *this, part);
        part.key.reset();
        part.mark = PartMark::none;
    }
    const Rect cue = drop_cue_bounds();
    if (!cue.empty()) {
        part.family = ControlVisualRole::button;
        part.states = {};
        part.states.insert(AnatomyState::drop_rejected);
        part.role = PartRole::indicator;
        part.mark = PartMark::close;
        part.bounds = cue;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
}

// -- announcements -------------------------------------------------------------

std::string DockSite::describe_target(const DockDropTarget& target) const {
    if (target.kind == DockDropTargetKind::edge) {
        const std::string words = "dock along the window's " + std::string(zone_word(target.zone)) + " edge";
        return words;
    }
    std::string pane = "the document area";
    const StackSlot* const slot = slot_for(target.pane);
    if (slot != nullptr && (*slot).kind == DockNodeKind::tool_stack) pane = (*slot).title;
    if (slot != nullptr && (*slot).kind == DockNodeKind::document_stack) pane = "the documents";
    if (target.zone == DockDropZone::center) {
        const std::string words = "tab into " + pane;
        return words;
    }
    const std::string words = "dock " + std::string(zone_word(target.zone)) + " of " + pane;
    return words;
}

// "<title>: <what a drop does now>", as the move mode's live text and the
// site's semantic description while the drag runs.
void DockSite::announce_drag() {
    std::string text = drag_.title + ": ";
    const std::optional<DockDropTarget> target = drag_target();
    if (drag_.suppressed) {
        text += "docking suppressed";
    } else if (target) {
        text += describe_target(*target);
    } else {
        text += "no target here";
    }
    if (text == drag_.announcement) return;
    drag_.announcement = std::move(text);
    invalidate(Dirty::semantics);
}

// -- keys ----------------------------------------------------------------------

// Escape cancels any drag; Ctrl pressed or let go updates suppression for a
// pointer drag. In the keyboard move mode every key is the mode's: the
// arrows step within the current pane, Tab and Shift+Tab between panes and
// edges, Enter commits.
bool DockSite::handle_drag_key(KeyEvent& event) {
    if (window_move_.active && drag_gesture::cancels(event)) {
        end_window_move(true);
        event.handled = true;
        return true;
    }
    if (drag_.guest) return false;
    if (drag_.phase == DockDragPhase::armed && drag_gesture::cancels(event)) {
        reset_drag();
        event.handled = true;
        return true;
    }
    if (drag_.phase != DockDragPhase::dragging) return false;
    const Control::Ptr keep = shared_from_this();
    if (!drag_.keyboard) {
        const bool suppressed = drag_gesture::suppresses_targets(event.modifiers);
        if (suppressed != drag_.suppressed) {
            update_drag_pointer(drag_.pointer, event.modifiers);
        }
        if (drag_gesture::cancels(event)) {
            end_drag(DockDragEnd::escaped);
            event.handled = true;
            return true;
        }
        return false;
    }
    event.handled = true;
    if (event.action != KeyAction::down) return true;
    const std::uint32_t key = event.physical_key;
    if (key == PhysicalKey::escape) {
        end_drag(DockDragEnd::escaped);
    } else if (key == PhysicalKey::enter) {
        end_drag(DockDragEnd::dropped);
    } else if (key == PhysicalKey::tab) {
        step_keyboard_target(has_modifier(event.modifiers, Modifier::shift) ? -1 : 1);
    } else if (key == PhysicalKey::left || key == PhysicalKey::right || key == PhysicalKey::up ||
               key == PhysicalKey::down) {
        step_keyboard_zone(key);
    }
    return true;
}

// Tab's stops: each pane (on its centre when that is a target, else its
// first target), then each edge. Steps cyclically by `direction`.
void DockSite::step_keyboard_target(int direction) {
    if (drag_.targets.empty()) return;
    std::vector<std::size_t> stops{};
    stops.reserve(drag_.targets.size());
    for (std::size_t index = 0; index < drag_.targets.size(); ++index) {
        const DockDropTarget& target = drag_.targets[index];
        if (target.kind == DockDropTargetKind::edge) {
            stops.push_back(index);
            continue;
        }
        const bool first_of_pane = index == 0U || drag_.targets[index - 1U].kind != DockDropTargetKind::pane ||
                                   !(drag_.targets[index - 1U].pane == target.pane);
        if (first_of_pane) stops.push_back(index);
        if (target.zone == DockDropZone::center && !stops.empty() && drag_.targets[stops.back()].pane == target.pane &&
            drag_.targets[stops.back()].kind == DockDropTargetKind::pane) {
            stops.back() = index;
        }
    }
    std::size_t current = 0U;
    const std::optional<std::size_t> at = drag_.target;
    for (std::size_t stop = 0; stop < stops.size() && at; ++stop) {
        const DockDropTarget& held = drag_.targets[*at];
        const DockDropTarget& candidate = drag_.targets[stops[stop]];
        const bool same_pane = held.kind == DockDropTargetKind::pane && candidate.kind == DockDropTargetKind::pane &&
                               held.pane == candidate.pane;
        if (same_pane || stops[stop] == *at) current = stop;
    }
    const std::size_t count = stops.size();
    const std::size_t next = direction >= 0 ? (current + 1U) % count : (current + count - 1U) % count;
    const std::size_t chosen = stops[next];
    std::optional<std::size_t> pane{};
    if (drag_.targets[chosen].kind == DockDropTargetKind::pane) {
        for (std::size_t index = 0; index < drag_.panes.size(); ++index) {
            if (drag_.panes[index].node == drag_.targets[chosen].pane) pane = index;
        }
    }
    select_drag_target(chosen, pane);
}

// Within the current pane: Left and Right along left, centre, right; Up and
// Down along top, centre, bottom (zone_steps); the wanted zones are tried in
// order, so a refused centre is passed over.
void DockSite::step_keyboard_zone(std::uint32_t key) {
    const std::optional<DockDropTarget> current = drag_target();
    if (!current || (*current).kind != DockDropTargetKind::pane) return;
    const DockDropZone zone = (*current).zone;
    std::array<DockDropZone, 2> wanted{zone, zone};
    std::size_t count = 0U;
    for (const ZoneStep& step : zone_steps) {
        if (step.key != key) continue;
        if (zone == step.away) {
            wanted = {DockDropZone::center, step.toward};
            count = 2U;
        } else if (zone != step.toward) {
            wanted[0] = step.toward;
            count = 1U;
        }
        break;
    }
    for (std::size_t choice = 0; choice < count; ++choice) {
        for (std::size_t index = 0; index < drag_.targets.size(); ++index) {
            const DockDropTarget& target = drag_.targets[index];
            if (target.kind != DockDropTargetKind::pane || !(target.pane == (*current).pane)) continue;
            if (target.zone != wanted[choice]) continue;
            select_drag_target(index, drag_.hovered_pane);
            return;
        }
    }
}

// -- across windows (phase D4) -------------------------------------------------

std::uint64_t DockSite::now_nanoseconds() noexcept {
    const std::chrono::nanoseconds since =
        std::chrono::duration_cast<std::chrono::nanoseconds>(FrameClock::now().time_since_epoch());
    const std::uint64_t value = static_cast<std::uint64_t>(since.count());
    return value;
}

std::optional<Point> DockSite::to_desktop(Point local) const {
    if (workspace_ == nullptr) return std::nullopt;
    const std::optional<Point> origin = (*workspace_).desktop_origin(*this);
    if (!origin) return std::nullopt;
    const Point desktop{(*origin).x + local.x, (*origin).y + local.y};
    return desktop;
}

DockSite* DockSite::remote_site(Point desktop, const DockSite* excluded) const {
    if (workspace_ == nullptr) return nullptr;
    DockSite* const site = (*workspace_).site_at(desktop, excluded);
    return site;
}

DockSite* DockSite::remote_guest() const noexcept {
    const std::shared_ptr<Control> held = drag_.remote.lock();
    if (!held || !(*held).is_alive()) return nullptr;
    DockSite* const site = static_cast<DockSite*>(held.get());
    return site;
}

// Starts showing a drag of another site, the pointer at `local` here, or
// follows it. A site busy with a drag of its own shows nothing.
void DockSite::show_guest_drag(const DragSession& source, const MotionValue* source_motion, Point local,
                               Modifier modifiers) {
    if (workspace_ == nullptr || !drag_layer_ || !is_alive()) return;
    if (drag_.guest && drag_.phase == DockDragPhase::dragging) {
        update_drag_pointer(local, modifiers);
        return;
    }
    if (drag_.phase != DockDragPhase::idle || window_move_.active) return;
    DragSession session{};
    session.phase = DockDragPhase::dragging;
    session.guest = true;
    session.subject = source.subject;
    session.source_stack = source.source_stack;
    session.panel_only = source.panel_only;
    session.grab = source.grab;
    session.pointer = local;
    // The source's size, standing at the pointer: the stamp shrinks from
    // there.
    const double width = source.source.width;
    const double height = source.source.height;
    session.source = {local.x - source.grab.x * width, local.y - source.grab.y * height, width, height};
    session.title = source.title;
    session.generation = (*workspace_).tree_generation();
    session.picture = source.picture;
    session.stamp_hidden = source.stamp_hidden;
    session.may_float = source.may_float;
    session.suppressed = has_modifier(modifiers, Modifier::control);
    drag_ = std::move(session);
    // The guest's stamp shrinks in step with the source's (from where that
    // stands now), or from the start when the source has no stamp motion.
    if (source_motion != nullptr) {
        static_cast<void>(stamp_motion_.adopt(*source_motion));
    } else {
        stamp_motion_.jump_to(0.0);
        static_cast<void>(stamp_motion_.animate_to(1.0));
    }
    drag_.stamp = stamp_at();
    (*drag_layer_).visible = true;
    plan_drag_targets();
    invalidate(Dirty::paint | Dirty::semantics);
    (*drag_layer_).invalidate(Dirty::paint);
}

void DockSite::leave_guest_drag() noexcept {
    if (!drag_.guest) return;
    reset_drag();
    try {
        if (drag_layer_) (*drag_layer_).visible = false;
        if (is_alive()) invalidate(Dirty::paint | Dirty::semantics);
    } catch (...) {
    }
}

// The site under the desktop pointer shows this drag; the one that showed
// it before, if another, stops.
void DockSite::route_drag(Point desktop, Modifier modifiers, const DockSite* excluded) {
    DockSite* const under = remote_site(desktop, excluded);
    DockSite* const previous = remote_guest();
    if (previous != nullptr && previous != under) (*previous).leave_guest_drag();
    if (under == nullptr || under == this) {
        drag_.remote.reset();
        return;
    }
    const std::optional<Point> origin = (*workspace_).desktop_origin(*under);
    if (!origin) {
        drag_.remote.reset();
        return;
    }
    drag_.remote = (*under).weak_from_this();
    const Point local{desktop.x - (*origin).x, desktop.y - (*origin).y};
    (*under).show_guest_drag(drag_, &stamp_motion_, local, modifiers);
}

std::optional<Rect> DockSite::float_frame_at(Point local) const {
    const std::optional<Point> desktop = to_desktop(local);
    if (!desktop) return std::nullopt;
    const double width = std::max(1.0, drag_.source.width);
    const double height = std::max(1.0, drag_.source.height);
    const Rect frame{(*desktop).x - drag_.grab.x * width, (*desktop).y - drag_.grab.y * height, width, height};
    return frame;
}

// -- moving a floating window by its caption (phase D4) ------------------------

bool DockSite::moves_window_by_caption() const {
    if (!floating() || workspace_ == nullptr || !(*workspace_).windows_attached()) return false;
    if (stacks_.size() != 1U || stacks_[0].kind != DockNodeKind::tool_stack) return false;
    const ApplicationWindowHandle handle = (*workspace_).window_of(*this);
    if (!handle.active()) return false;
    const bool capture =
        WindowFrameDrag::mode_for(handle.windows().capabilities()) == WindowFrameDragMode::framework_capture;
    return capture;
}

bool DockSite::begin_window_move(std::uint64_t pointer_id) {
    Window* const host = attached_window();
    if (host == nullptr || workspace_ == nullptr || stacks_.empty()) return false;
    const ApplicationWindowHandle handle = (*workspace_).window_of(*this);
    const HostServiceStatus begun = window_move_.drag.begin(handle, now_nanoseconds());
    if (!begun.accepted()) return false;
    window_move_.active = true;
    window_move_.subject = DockSubject::of_stack(stacks_[0].stack);
    window_move_.pointer_id = pointer_id == 0U ? 1U : pointer_id;
    window_move_.remote.reset();
    (*host).capture_pointer(shared_from_this(), window_move_.pointer_id);
    return true;
}

// The window follows the desktop pointer; the site under the pointer, below
// the window, shows the targets a release there would take.
void DockSite::continue_window_move(Modifier modifiers) {
    if (!window_move_.active || workspace_ == nullptr) return;
    static_cast<void>(window_move_.drag.move(now_nanoseconds()));
    const ApplicationWindowHandle handle = (*workspace_).window_of(*this);
    const HostScreenPointResult pointer = handle.windows().pointer_position();
    const std::shared_ptr<Control> held = window_move_.remote.lock();
    DockSite* const previous = held ? static_cast<DockSite*>(held.get()) : nullptr;
    // The docking controls keep desktop positions as Point in logical units.
    const Point desktop{pointer.position.x, pointer.position.y};
    DockSite* const under = pointer.status.accepted() ? remote_site(desktop, this) : nullptr;
    if (previous != nullptr && previous != under) (*previous).leave_guest_drag();
    if (under == nullptr || stacks_.empty()) {
        window_move_.remote.reset();
        return;
    }
    const std::optional<Point> origin = (*workspace_).desktop_origin(*under);
    if (!origin) return;
    DragSession source{};
    source.subject = window_move_.subject;
    source.source_stack = window_move_.subject.stack;
    source.title = stacks_[0].title;
    source.source = stacks_[0].bounds;
    source.grab = {0.5, 0.0};
    source.stamp_hidden = true;
    window_move_.remote = (*under).weak_from_this();
    const Point local{pointer.position.x - (*origin).x, pointer.position.y - (*origin).y};
    (*under).show_guest_drag(source, nullptr, local, modifiers);
}

void DockSite::leave_window_move_guest() noexcept {
    const std::shared_ptr<Control> held = window_move_.remote.lock();
    window_move_.remote.reset();
    if (held && (*held).is_alive()) static_cast<DockSite&>(*held).leave_guest_drag();
}

// A release over a target docks the window's stack there (the window then
// closes with the transaction); elsewhere the window stays where it was
// moved, or, cancelled, goes back.
void DockSite::end_window_move(bool cancelled) {
    if (!window_move_.active) return;
    const Control::Ptr keep = shared_from_this();
    const std::shared_ptr<Control> held = window_move_.remote.lock();
    std::optional<DockDropTarget> target{};
    if (held && (*held).is_alive() && !cancelled) target = static_cast<DockSite&>(*held).drag_target();
    leave_window_move_guest();
    window_move_.active = false;
    Window* const host = attached_window();
    if (host != nullptr && (*host).captured_control().get() == this) (*host).release_pointer();
    if (cancelled) {
        static_cast<void>(window_move_.drag.cancel(now_nanoseconds()));
    } else {
        window_move_.drag.end(now_nanoseconds());
    }
    if (!target || workspace_ == nullptr) return;
    const DockSubject subject = window_move_.subject;
    DockPanelId panel{};
    const DockNode* const node = (*workspace_).layout().find_node(window_move_.subject.stack);
    if (node != nullptr && !(*node).panels.empty()) panel = (*node).panels[(*node).selected];
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::user;
    transaction.edits.push_back((*target).edit);
    const DockTransactionResult result = (*workspace_).apply(transaction);
    if (workspace_ == nullptr) return;
    if (result.applied) static_cast<void>((*workspace_).activate_panel(panel, DockActivationCause::pointer));
    if (!is_alive()) return;
    // A window moved onto another site's target is always across windows.
    DockDragOutcome outcome = DockDragOutcome::ended(DockDragEnd::rejected, subject);
    if (result.applied) outcome = DockDragOutcome::dropped(subject, *target, result, true);
    drag_ended_.emit(outcome);
}

} // namespace gui_forms
