#pragma once

// The authority to bind a component to a root's lifetime (B10, SurfaceRoot's
// named capabilities): an observing reference to the root's lifetime record,
// and for a background worker a share of the root's dispatcher queue state.
//
// Holders: Timer, AnimationTimer, BackgroundWorker, BindingSource,
// BindingContext, ImageList and Application. Each takes what it needs while
// the root is alive (a constructor given the window, or a walk of live
// windows) and keeps only the observing reference.
//
// Stale use: the lifetime record is owned by the root and released with it,
// so a holder that locks its observer after the root is gone gets null and
// rejects the use as it does today (its "after Window shutdown" errors, or a
// timer that stops). The dispatcher state is shared and outlives the root;
// once the root shuts its dispatcher down it refuses posts by name
// (DispatcherState::accepting false). No capability object exists: the
// functions are static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

#include <memory>

namespace gui_forms::detail {

class SurfaceLifetimeAccess final {
public:
    SurfaceLifetimeAccess() = delete;

    // An observing reference to `root`'s lifetime record.
    [[nodiscard]] static std::weak_ptr<WindowLifetime> observe(const SurfaceRoot& root) noexcept {
        const std::weak_ptr<WindowLifetime> result = root.lifetime_;
        return result;
    }
    // Whether `lifetime` is `root`'s record (false for null).
    [[nodiscard]] static bool is_lifetime_of(const SurfaceRoot& root,
                                             const std::shared_ptr<WindowLifetime>& lifetime) noexcept {
        const bool result = lifetime != nullptr && lifetime == root.lifetime_;
        return result;
    }
    // A share of `root`'s dispatcher queue state, for posting completions
    // from a worker thread; posts after shutdown are refused by the state.
    [[nodiscard]] static std::shared_ptr<DispatcherState> share_dispatcher(const SurfaceRoot& root) noexcept {
        const std::shared_ptr<DispatcherState> result = root.dispatcher_state_;
        return result;
    }
};

} // namespace gui_forms::detail
