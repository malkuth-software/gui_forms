// The visible-area guarantee's checks (stage VA, owner decisions of
// 2026-11-22; notes planning/stages/VA-visible-area/STUDY.md, section 4).
//
// After each layout flush the window compares every viewport owner the
// flush arranged (and each one's parent) with its viewport, and the root
// with the root slot, and keeps one record per overflow: what overflowed,
// why, and whether the owner's answer is carried out. The checks read
// committed geometry only and lay nothing out.
//
// A contain or cover is carried out here, as the owner's content frame
// (Control::content_frame) or, for the root, the window's root frame: one
// uniform scale and a translation the walks place the owner's children
// through (sections 5 and 9). Layout never reads a frame, so a frame
// changes nothing the check measures, and the check is stable.

#include "gui_forms/surface_root/surface_root.hpp"

#include "../../control/control/control_tree_walk.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <stxlib/optional.hpp>

namespace gui_forms {
namespace {

// How far content may reach past its viewport before it counts as an
// overflow, in logical units: layout's rounding, not content.
constexpr double overflow_tolerance = 0.01;

bool reaches_past(Rect content, Rect viewport) noexcept {
    const bool left = content.x < viewport.x - overflow_tolerance;
    const bool top = content.y < viewport.y - overflow_tolerance;
    const bool right = content.x + content.width > viewport.x + viewport.width + overflow_tolerance;
    const bool bottom = content.y + content.height > viewport.y + viewport.height + overflow_tolerance;
    const bool past = left || top || right || bottom;
    return past;
}

Rect union_of(Rect first, Rect second) noexcept {
    const double left = std::min(first.x, second.x);
    const double top = std::min(first.y, second.y);
    const double right = std::max(first.x + first.width, second.x + second.width);
    const double bottom = std::max(first.y + first.height, second.y + second.height);
    const Rect joined{left, top, right - left, bottom - top};
    return joined;
}

// The answer an owner gives, whether it is carried out, and the frame
// that carries out a contain or cover (the identity for any other answer).
struct OwnerAnswer final {
    OverflowAnswer answer{OverflowAnswer::contain};
    bool answered{};
    double effective_scale{1.0};
    ScaleFrame frame{};
};

// The owner's fit with its floor (decision 5): media is bounded by its own
// minimum_scale; controls also by the presentation settings' floor. The
// maximum is raised to the floor where it lay below it, so the fit stays
// valid.
ViewportFit floored_fit(const Control& owner, double content_floor) noexcept {
    ViewportFit fit = owner.viewport_fit.read();
    if (!owner.shows_media_content()) fit.minimum_scale = std::max(fit.minimum_scale, content_floor);
    fit.maximum_scale = std::max(fit.maximum_scale, fit.minimum_scale);
    return fit;
}

// The answer of an owner whose `content` (its own coordinates) overflows
// `viewport`. An owner that scrolls its content (when it `may_scroll`)
// answers by scrolling; one that scales its content itself
// (applied_viewport_scale) answers a contain or cover while it does;
// otherwise the window carries out its viewport_fit: a clip as it is, a
// contain or cover through a frame whose scale keeps within the fit's
// bounds on the effective scale (`outer_scale` is the scale above the
// owner), and a scroll not at all. A contain below its floor takes
// below_minimum at the floor: a clip there is answered, a scroll is not.
OwnerAnswer answer_of(const Control& owner, bool may_scroll, Rect content, Rect viewport, double outer_scale,
                      double content_floor) noexcept {
    OwnerAnswer result{};
    if (may_scroll && owner.content_scrolls()) {
        result.answer = OverflowAnswer::scroll;
        result.answered = true;
        return result;
    }
    result.answer = owner.viewport_fit.read().answer;
    const stxlib::Optional<double> applied = owner.applied_viewport_scale();
    if (applied.present()) {
        result.effective_scale = *applied.value();
        result.answered = result.answer != OverflowAnswer::scroll;
        return result;
    }
    switch (result.answer) {
    case OverflowAnswer::scroll:
        // An owner that does not scroll cannot carry a scroll out.
        result.answered = false;
        break;
    case OverflowAnswer::clip:
        result.answered = true;
        break;
    case OverflowAnswer::contain:
    case OverflowAnswer::cover: {
        const ViewportFit fit = fit_within_outer_scale(floored_fit(owner, content_floor), outer_scale);
        const ViewportPlacement placement =
            place_viewport_content(Size{content.width, content.height}, viewport, fit);
        result.frame = placement_frame(content, placement);
        result.effective_scale = placement.scale;
        result.answered = placement.applied != OverflowAnswer::scroll;
        break;
    }
    default:
        // Not an answer of the enumeration (viewport_fit refuses one):
        // unanswered.
        result.answered = false;
        break;
    }
    return result;
}

} // namespace

std::size_t SurfaceRoot::unanswered_overflow_count() const noexcept {
    std::size_t count = 0U;
    for (const OverflowRecord& record : overflow_records_) {
        if (!record.answered) ++count;
    }
    return count;
}

Control::Ptr SurfaceRoot::overflow_owner(const OverflowRecord& record) const {
    const Control::Ptr owner = find_runtime_control(record.owner);
    return owner;
}

void SurfaceRoot::queue_viewport_check(Control& owner) {
    if (owner.viewport_check_pass_ == viewport_check_sequence_) return;
    // Listed before it is marked, so a list that cannot grow leaves the
    // owner to be queued again by a later arrangement in this flush.
    const stxlib::OperationStatus listed = viewport_checks_.push(owner.weak_from_this());
    if (listed.failed()) return;
    owner.viewport_check_pass_ = viewport_check_sequence_;
}

void SurfaceRoot::check_viewports() {
    // The root first: its frame is the outermost scale every owner's floor
    // is measured against.
    if (root_ && (*root_).viewport_check_pass_ == viewport_check_sequence_) check_root_slot();
    // By index, the count read each step: a frame that changes lists the
    // framed owners below it again (requeue_framed_descendants). The list's
    // storage stays the window's for the next flush.
    for (stxlib::usize index = 0U; index < viewport_checks_.count(); ++index) {
        const Control::Ptr owner = viewport_checks_[index].lock();
        if (!owner || !(*owner).is_alive() || (*owner).surface_root_ != this) continue;
        check_viewport_owner(owner);
    }
    viewport_checks_.clear();
    prune_overflow_records();
}

void SurfaceRoot::requeue_framed_descendants(Control& owner) {
    // In fixed storage (the depth rule); listing changes no child list.
    detail::SubtreeWalk walk(owner);
    for (Control* current = walk.next(); current != nullptr; current = walk.next()) {
        if (current == &owner || is_identity_frame((*current).content_frame_)) continue;
        // Listed whatever its pass: its floor and bounds are measured
        // against the scale above it, which has just changed. A list that
        // cannot grow leaves its frame until its next arrangement.
        const stxlib::OperationStatus listed = viewport_checks_.push((*current).weak_from_this());
        static_cast<void>(listed);
    }
}

void SurfaceRoot::check_viewport_owner(const Control::Ptr& owner) {
    const Control& control = *owner;
    const bool shown = control.visible.read() && !control.layout_collapsed_;
    const Rect viewport = control.child_viewport_rectangle();
    const Size viewport_extent{viewport.width, viewport.height};
    // Where the children were laid out: the viewport, or the design
    // rectangle of an owner that contains its design (Control::
    // content_layout_rectangle). Children beyond it overflow; the design
    // itself is checked against the viewport below.
    const Rect laid_out = control.content_layout_rectangle();
    // The children: their arranged bounds, in the owner's coordinates, as
    // the window places them (placed_bounds). Visual outsets (shadows,
    // focus rings) are decoration and do not count.
    bool children_overflow = false;
    bool placed_child = false;
    bool any_child = false;
    Rect content{};
    if (shown) {
        for (const Control::Ptr& child : control.children_) {
            // A child takes part in its parent's content while it is shown,
            // alive and laid out in this window.
            if (!child || !(*child).is_alive() || !(*child).visible.read() || (*child).surface_root_ != this ||
                (*child).layout_collapsed_) {
                continue;
            }
            const Rect bounds = (*child).committed_arranged_bounds();
            content = any_child ? union_of(content, bounds) : bounds;
            any_child = true;
            if (!reaches_past(bounds, laid_out)) continue;
            children_overflow = true;
            // A child the dock and anchor engine placed was placed by its
            // position; one a layout container placed did not fit after
            // reflow.
            if ((*child).arranged_by_dock_layout_) placed_child = true;
        }
    }
    // The design size: content designed larger than the viewport, laid out
    // at the viewport's origin (Control::arrange lays the children of a
    // containing owner out at it).
    const Size design = control.design_size.read();
    const bool has_design = design.width > 0.0 && design.height > 0.0;
    const bool design_overflow = shown && has_design &&
                                 (design.width > viewport.width + overflow_tolerance ||
                                  design.height > viewport.height + overflow_tolerance);

    // One answer for both kinds: the content is everything that must show,
    // the viewport itself, the children and the design rectangle.
    OwnerAnswer answer{};
    if (children_overflow || design_overflow) {
        Rect whole = viewport;
        if (children_overflow) whole = union_of(whole, content);
        if (design_overflow) whole = union_of(whole, Rect{viewport.x, viewport.y, design.width, design.height});
        const double outer_scale = local_frame_of(control).scale;
        answer = answer_of(control, true, whole, viewport, outer_scale, presentation_settings_.minimum_content_scale);
    }
    set_content_frame(owner, answer.frame);

    OverflowRecord children_record{};
    if (children_overflow) {
        children_record.owner = control.runtime_id();
        children_record.cause = placed_child ? OverflowCause::placed_child_outside_viewport
                                             : OverflowCause::minimum_content_exceeds_viewport;
        children_record.content_extent = Size{content.width, content.height};
        children_record.viewport_extent = viewport_extent;
        children_record.answer = answer.answer;
        children_record.effective_scale = answer.effective_scale;
        children_record.answered = answer.answered;
    }
    update_overflow_record(owner, OverflowScope::children, children_overflow, children_record);

    OverflowRecord design_record{};
    if (design_overflow) {
        design_record.owner = control.runtime_id();
        design_record.cause = OverflowCause::design_size_exceeds_viewport;
        design_record.content_extent = design;
        design_record.viewport_extent = viewport_extent;
        design_record.answer = answer.answer;
        design_record.effective_scale = answer.effective_scale;
        design_record.answered = answer.answered;
    }
    update_overflow_record(owner, OverflowScope::design, design_overflow, design_record);
}

void SurfaceRoot::check_root_slot() {
    const Control::Ptr root = root_;
    const Rect slot = root_slot();
    const Rect arranged = (*root).committed_arranged_bounds();
    const bool shown = (*root).visible.read() && !(*root).layout_collapsed_;
    // The root is arranged at the slot; it is larger only where its minimum
    // size exceeds the slot (Control::arrange keeps the minimum), or where
    // it is laid out at its design size (root_layout_rectangle), and then
    // its far edges lie outside the client area.
    const bool overflowing = shown && (arranged.width > slot.width + overflow_tolerance ||
                                       arranged.height > slot.height + overflow_tolerance);
    // The root cannot scroll itself within its slot: only its own
    // viewport_fit answers, a contain or cover through the window's root
    // frame (the owner's game case: a root taller than a small window is
    // shown whole).
    OwnerAnswer answer{};
    if (overflowing) {
        answer = answer_of(*root, false, arranged, slot, 1.0, presentation_settings_.minimum_content_scale);
    }
    set_root_frame(answer.frame);
    OverflowRecord record{};
    if (overflowing) {
        const Size design = (*root).design_size.read();
        const bool designed = design.width > 0.0 && design.height > 0.0;
        record.owner = (*root).runtime_id();
        record.cause = designed ? OverflowCause::design_size_exceeds_viewport
                                : OverflowCause::minimum_content_exceeds_viewport;
        record.content_extent = Size{arranged.width, arranged.height};
        record.viewport_extent = Size{slot.width, slot.height};
        record.answer = answer.answer;
        record.effective_scale = answer.effective_scale;
        record.answered = answer.answered;
    }
    update_overflow_record(root, OverflowScope::root_slot, overflowing, record);
}

void SurfaceRoot::set_content_frame(const Control::Ptr& owner, const ScaleFrame& frame) {
    if ((*owner).content_frame_ == frame) return;
    // Where the children were shown, then where they will be: both inside
    // the owner's child viewport, which is all they paint in. Each child is
    // recorded again at its new scale when it paints (recorded_frame_scale_)
    // and its semantic bounds move.
    const DamageCauseScope cause(*this, InvalidationCause::layout, owner.get());
    add_subtree_damage(owner);
    (*owner).content_frame_ = frame;
    settle_subtree(*owner, Dirty::paint | Dirty::hit_test | Dirty::semantics | Dirty::accessibility);
    ++semantic_generation_;
    requeue_framed_descendants(*owner);
}

void SurfaceRoot::set_root_frame(const ScaleFrame& frame) {
    if (root_frame_ == frame || !root_) return;
    const DamageCauseScope cause(*this, InvalidationCause::layout, root_.get());
    add_subtree_damage(root_);
    root_frame_ = frame;
    settle_subtree(*root_, Dirty::paint | Dirty::hit_test | Dirty::semantics | Dirty::accessibility);
    ++semantic_generation_;
    requeue_framed_descendants(*root_);
}

void SurfaceRoot::update_overflow_record(const Control::Ptr& owner, OverflowScope scope, bool overflowing,
                                         const OverflowRecord& record) {
    const RuntimeId identity = (*owner).runtime_id();
    stxlib::usize found = overflow_records_.count();
    for (stxlib::usize index = 0U; index < overflow_records_.count(); ++index) {
        if (overflow_records_[index].owner == identity && overflow_record_keys_[index].scope == scope) {
            found = index;
            break;
        }
    }
    const bool known = found < overflow_records_.count();
    if (!overflowing) {
        if (known) remove_overflow_record(found);
        return;
    }
    bool changed = true;
    if (known) {
        const OverflowRecord& kept = overflow_records_[found];
        changed = kept.answer != record.answer || kept.answered != record.answered || kept.cause != record.cause ||
                  kept.effective_scale != record.effective_scale;
        // Replaced in place: the record keeps its position, the order in
        // which overflows appeared.
        overflow_records_[found] = record;
    } else {
        if (overflow_records_.count() >= maximum_overflow_records) {
            ++dropped_overflow_records_;
            return;
        }
        // Both lists grow before either holds the new record, so a failed
        // growth leaves them in step; the pushes then fit their capacity.
        const stxlib::OperationStatus records_reserved = overflow_records_.reserve(overflow_records_.count() + 1U);
        if (records_reserved.failed()) {
            ++dropped_overflow_records_;
            return;
        }
        const stxlib::OperationStatus keys_reserved = overflow_record_keys_.reserve(overflow_record_keys_.count() + 1U);
        if (keys_reserved.failed()) {
            ++dropped_overflow_records_;
            return;
        }
        const stxlib::OperationStatus record_pushed = overflow_records_.push_copy(record);
        const stxlib::OperationStatus key_pushed =
            overflow_record_keys_.push(OverflowRecordKey{(*owner).weak_from_this(), scope});
        static_cast<void>(record_pushed);
        static_cast<void>(key_pushed);
    }
    if (!changed) return;
    metrics_.record_overflow(record.answer, record.answered);
    try {
        LayoutDiagnostic diagnostic{};
        diagnostic.code = LayoutDiagnosticCode::content_overflow;
        diagnostic.severity = record.answered ? LayoutDiagnosticSeverity::warning : LayoutDiagnosticSeverity::error;
        diagnostic.child = std::string((*owner).stable_id().value());
        record_layout_diagnostic(std::move(diagnostic));
    } catch (const std::bad_alloc&) {
        // Counted above; only the diagnostic is lost.
        ++dropped_layout_diagnostics_;
    }
}

void SurfaceRoot::fill_overlay_overflows() {
    overflow_outline_scratch_.clear();
    for (stxlib::usize index = 0U; index < overflow_records_.count(); ++index) {
        const OverflowRecord& record = overflow_records_[index];
        const bool clipped = record.answered && record.answer == OverflowAnswer::clip;
        if (record.answered && !clipped) continue;
        const Control::Ptr owner = overflow_record_keys_[index].owner.lock();
        if (!owner || (*owner).surface_root_ != this) continue;
        OverflowOutline outline{};
        outline.kind = clipped ? OverflowOutlineKind::clipped : OverflowOutlineKind::unanswered;
        outline.bounds = overflow_record_keys_[index].scope == OverflowScope::root_slot
                             ? root_slot()
                             : absolute_child_viewport_of(*owner);
        // An outline the scratch cannot hold is not shown this paint.
        const stxlib::OperationStatus kept = overflow_outline_scratch_.push_copy(outline);
        if (kept.failed() || overflow_outline_scratch_.count() >= maximum_overflow_outlines) break;
    }
    (*invalidation_overlay_).set_overflow_outlines(overflow_outline_scratch_.view());
}

void SurfaceRoot::remove_overflow_record(std::size_t index) noexcept {
    overflow_records_.erase(index);
    overflow_record_keys_.erase(index);
}

void SurfaceRoot::prune_overflow_records() noexcept {
    // Back to front, so an erased record moves none still to be looked at.
    stxlib::usize index = overflow_records_.count();
    while (index > 0U) {
        --index;
        const Control::Ptr owner = overflow_record_keys_[index].owner.lock();
        const bool attached = owner && (*owner).is_alive() && (*owner).surface_root_ == this;
        if (!attached) remove_overflow_record(index);
    }
}

} // namespace gui_forms
