#pragma once

// The authority of a control that forwards focus and keys to another tree
// (B10; the sub-surface design note, section 4): how the newest focus change
// came about, and the bubbling of a key the other tree gave back unhandled.
//
// Holder: SubSurfaceHost.
//
// Stale use: the host reaches its root through attached_surface_root(),
// null once it is detached, and then does nothing; resume_unhandled_key
// itself answers false, doing nothing, when `from` is no longer eligible.
// The functions are static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

namespace gui_forms::detail {

class SurfaceForwardingAccess final {
public:
    SurfaceForwardingAccess() = delete;

    using FocusTraversal = SurfaceRoot::FocusTraversal;

    // Through Tab, Shift+Tab or anything else (none).
    [[nodiscard]] static FocusTraversal last_focus_traversal(const SurfaceRoot& root) noexcept {
        const FocusTraversal result = root.last_focus_traversal_;
        return result;
    }
    // Bubbles a key `from` forwarded and got back unhandled
    // (SurfaceRoot::resume_unhandled_key). Root thread only.
    static bool resume_unhandled_key(SurfaceRoot& root, const Control::Ptr& from, KeyEvent event) {
        const bool result = root.resume_unhandled_key(from, event);
        return result;
    }
};

} // namespace gui_forms::detail
