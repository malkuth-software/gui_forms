#pragma once

// The authority of the handles a root gives out to close their own grant
// (B10): an overlay attachment closes its overlay, an accelerator attachment
// its accelerator, an update scope leaves its scope.
//
// Holders: OverlayAttachment, AcceleratorAttachment and UpdateScope.
//
// Stale use: an attachment forgets its root when it is revoked, and the
// root's teardown revokes every attachment (release_tree), so a closed or
// revoked attachment never calls in. An update scope forgets its root when
// it closes or is abandoned; the root outlives every open scope (the scope
// contract, update_scope.hpp). The functions are static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

namespace gui_forms::detail {

class SurfaceHandleAccess final {
public:
    SurfaceHandleAccess() = delete;

    static void close_overlay(SurfaceRoot& root, OverlayAttachment& overlay) noexcept { root.close_overlay(overlay); }
    static void close_accelerator(SurfaceRoot& root, AcceleratorAttachment& accelerator) noexcept {
        root.close_accelerator(accelerator);
    }
    static void leave_update_scope(SurfaceRoot& root) { root.leave_update_scope(); }
};

} // namespace gui_forms::detail
