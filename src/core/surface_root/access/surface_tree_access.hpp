#pragma once

// The authority of the tree's node over the root it is attached to (B10):
// attaching, detaching, disposing and forgetting subtrees; the dirty marks
// and the settle and layout reads behind them; the eligibility, availability
// and tooltip-text notices; the containing-block registrations; the opaque
// cover note; the thread check and the lifecycle-notification guard; and the
// focus moves a command activation makes.
//
// Holder: Control.
//
// Stale use: a control reaches its root only through its own surface_root_,
// which the root sets at attach and clears at detach (or forgets in
// forget_destroyed_subtree), so a control never calls into a root it is not
// attached to; every call site checks the pointer first. The functions are
// static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

#include <span>
#include <string_view>

namespace gui_forms::detail {

class SurfaceTreeAccess final {
public:
    SurfaceTreeAccess() = delete;

    // std::logic_error naming `operation` off the root's thread.
    static void require_root_thread(SurfaceRoot& root, std::string_view operation) {
        root.require_root_thread(operation);
    }
    // Whether the root is telling controls of an attach or a detach, when the
    // tree may not change.
    [[nodiscard]] static bool in_lifecycle_notification(const SurfaceRoot& root) noexcept {
        const bool result = root.in_lifecycle_notification_;
        return result;
    }

    static void validate_subtree_registration(const SurfaceRoot& root, const Control& control) {
        root.validate_subtree_registration(control);
    }
    static void attach_subtree(SurfaceRoot& root, const Control::Ptr& control, const Control::WeakPtr& parent) {
        root.attach_subtree(control, parent);
    }
    static void detach_subtree(SurfaceRoot& root, const Control::Ptr& control) { root.detach_subtree(control); }
    static void dispose_subtree(SurfaceRoot& root, const Control::Ptr& control) noexcept {
        root.dispose_subtree(control);
    }
    // The backstop for a control destroyed while still attached (~Control).
    static void forget_destroyed_subtree(SurfaceRoot& root, Control& control) noexcept {
        root.forget_destroyed_subtree(control);
    }
    static void refresh_ancestor_summaries(SurfaceRoot& root, Control& first) noexcept {
        root.refresh_ancestor_summaries(first);
    }

    static void mark_dirty(SurfaceRoot& root, Control& control, Dirty dirty) { root.mark_dirty(control, dirty); }
    static void mark_paint_dirty(SurfaceRoot& root, Control& control, Rect local_damage) {
        root.mark_paint_dirty(control, local_damage);
    }
    static void mark_subtree_dirty(SurfaceRoot& root, Control& control, Dirty dirty) {
        root.mark_subtree_dirty(control, dirty);
    }
    static void mark_child_layout_slot(SurfaceRoot& root, Control& control) { root.mark_child_layout_slot(control); }
    // SurfaceRoot::refuse_arrange_outside_slot: Control::arrange and
    // arrange_self ask it first and change nothing when it refuses.
    [[nodiscard]] static bool refuse_arrange_outside_slot(SurfaceRoot& root, const Control& control) noexcept {
        const bool refused = root.refuse_arrange_outside_slot(control);
        return refused;
    }
    static void change_paint_plane(SurfaceRoot& root, Control& control, PaintPlane plane) {
        root.change_paint_plane(control, plane);
    }
    static void note_content_gather(SurfaceRoot& root, Control& control) { root.note_content_gather(control); }
    static void note_opaque_cover(SurfaceRoot& root, const Control& control, const Painter& painter,
                                  Rect local) noexcept {
        root.note_opaque_cover(control, painter, local);
    }
    static void list_layout_deferred(SurfaceRoot& root, Control& control) noexcept {
        root.list_layout_deferred(control);
    }
    static void settle_dirty(SurfaceRoot& root) { root.settle_dirty(); }
    static void ensure_layout(SurfaceRoot& root, bool read_barrier) { root.ensure_layout(read_barrier); }
    [[nodiscard]] static Rect absolute_bounds_of(const SurfaceRoot& root, const Control& control) {
        const Rect result = root.absolute_bounds_of(control);
        return result;
    }

    static void note_containing_block(SurfaceRoot& root, const Control& child, const Control& ancestor) {
        root.note_containing_block(child, ancestor);
    }
    static void set_containing_block_dependencies(SurfaceRoot& root, Control& dependent,
                                                  std::span<const RuntimeId> watched) {
        root.set_containing_block_dependencies(dependent, watched);
    }
    static void mark_containing_block_dependents(SurfaceRoot& root, Control& watched) {
        root.mark_containing_block_dependents(watched);
    }

    static void on_eligibility_changed(SurfaceRoot& root, const Control::Ptr& control) {
        root.on_eligibility_changed(control);
    }
    static void on_eligibility_restored(SurfaceRoot& root, const Control::Ptr& control) {
        root.on_eligibility_restored(control);
    }
    static void on_enabled_withdrawn(SurfaceRoot& root, const Control::Ptr& control) {
        root.on_enabled_withdrawn(control);
    }
    static void on_hit_test_transparency_changed(SurfaceRoot& root, const Control::Ptr& control) {
        root.on_hit_test_transparency_changed(control);
    }
    static void publish_control_availability(SurfaceRoot& root, Control& control) {
        root.publish_control_availability(control);
    }
    static void tool_tip_text_changed(SurfaceRoot& root, Control& control) { root.tool_tip_text_changed(control); }

    [[nodiscard]] static bool validate_command_activation(SurfaceRoot& root, const Control::Ptr& destination) {
        const bool result = root.validate_command_activation(destination);
        return result;
    }
    [[nodiscard]] static bool move_focus_after(SurfaceRoot& root, const Control::Ptr& origin) {
        const bool result = root.move_focus_after(origin);
        return result;
    }
};

} // namespace gui_forms::detail
