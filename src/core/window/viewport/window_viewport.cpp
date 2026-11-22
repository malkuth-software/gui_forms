// The visible-area guarantee's checks (stage VA, owner decisions of
// 2026-11-22; notes planning/stages/VA-visible-area/STUDY.md, section 4).
//
// After each layout flush the window compares every viewport owner the
// flush arranged (and each one's parent) with its viewport, and the root
// with the root slot, and keeps one record per overflow: what overflowed,
// why, and whether the owner's answer is carried out. The checks read
// committed geometry only and lay nothing out.

#include "gui_forms/surface_root/surface_root.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>

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

// The answer an owner gives and whether it is carried out: an owner that
// scrolls its content answers by scrolling; otherwise its viewport_fit's
// answer, carried out for clip always and for contain and cover while the
// owner applies a scale.
struct OwnerAnswer final {
    OverflowAnswer answer{OverflowAnswer::contain};
    bool answered{};
    double effective_scale{1.0};
};

OwnerAnswer answer_of(const Control& owner, bool may_scroll) noexcept {
    OwnerAnswer result{};
    const stxlib::Optional<double> applied = owner.applied_viewport_scale();
    if (applied.present()) result.effective_scale = *applied.value();
    if (may_scroll && owner.content_scrolls()) {
        result.answer = OverflowAnswer::scroll;
        result.answered = true;
        return result;
    }
    result.answer = owner.viewport_fit.read().answer;
    switch (result.answer) {
    case OverflowAnswer::scroll:
        // An owner that does not scroll cannot carry a scroll out.
        result.answered = false;
        break;
    case OverflowAnswer::clip:
        result.answered = true;
        break;
    case OverflowAnswer::contain:
    case OverflowAnswer::cover:
        result.answered = applied.present();
        break;
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
    // By index: the list's storage stays the window's for the next flush.
    for (stxlib::usize index = 0U; index < viewport_checks_.count(); ++index) {
        const Control::Ptr owner = viewport_checks_[index].lock();
        if (!owner || !(*owner).is_alive() || (*owner).surface_root_ != this) continue;
        check_viewport_owner(owner);
    }
    viewport_checks_.clear();
    if (root_ && (*root_).viewport_check_pass_ == viewport_check_sequence_) check_root_slot();
    prune_overflow_records();
}

void SurfaceRoot::check_viewport_owner(const Control::Ptr& owner) {
    const Control& control = *owner;
    const bool shown = control.visible.read() && !control.layout_collapsed_;
    const Rect viewport = control.child_viewport_rectangle();
    const Size viewport_extent{viewport.width, viewport.height};
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
            if (!reaches_past(bounds, viewport)) continue;
            children_overflow = true;
            // A child the dock and anchor engine placed was placed by its
            // position; one a layout container placed did not fit after
            // reflow.
            if ((*child).arranged_by_dock_layout_) placed_child = true;
        }
    }
    OverflowRecord children_record{};
    if (children_overflow) {
        const OwnerAnswer answer = answer_of(control, true);
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

    // The design size: content designed larger than the viewport.
    const Size design = control.design_size.read();
    const bool has_design = design.width > 0.0 && design.height > 0.0;
    const bool design_overflow = shown && has_design &&
                                 (design.width > viewport.width + overflow_tolerance ||
                                  design.height > viewport.height + overflow_tolerance);
    OverflowRecord design_record{};
    if (design_overflow) {
        const OwnerAnswer answer = answer_of(control, true);
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
    // size exceeds the slot (Control::arrange keeps the minimum), and then
    // its far edges lie outside the client area.
    const bool overflowing = shown && (arranged.width > slot.width + overflow_tolerance ||
                                       arranged.height > slot.height + overflow_tolerance);
    OverflowRecord record{};
    if (overflowing) {
        // The root cannot scroll itself: only its own viewport_fit answers,
        // and a contain or cover of the root by the window is not carried
        // out yet (the in-tree scale frame, VA2).
        const OwnerAnswer answer = answer_of(*root, false);
        record.owner = (*root).runtime_id();
        record.cause = OverflowCause::minimum_content_exceeds_viewport;
        record.content_extent = Size{arranged.width, arranged.height};
        record.viewport_extent = Size{slot.width, slot.height};
        record.answer = answer.answer;
        record.effective_scale = answer.effective_scale;
        record.answered = answer.answered;
    }
    update_overflow_record(root, OverflowScope::root_slot, overflowing, record);
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
