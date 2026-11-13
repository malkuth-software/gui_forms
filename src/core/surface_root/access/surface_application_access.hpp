#pragma once

// The authority over a root's place in an application run (B10): attaching
// the run's window state, and reading it to place native popups and
// tooltips on the desktop.
//
// Holders: detail::attach_application_window (the run), PopupStandby and
// ToolTipPresenter.
//
// Stale use: the root only observes the state (a weak reference); once the
// run ends it is expired and the native-popup helpers refuse or fall back as
// they do today. The readers are owned by the root, so they never reach a
// destroyed one. The functions are static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

#include <memory>

namespace gui_forms::detail {

class SurfaceApplicationAccess final {
public:
    SurfaceApplicationAccess() = delete;

    // `root` joins the run as a ready window with `state`.
    static void attach(SurfaceRoot& root, const std::shared_ptr<ApplicationWindowState>& state) noexcept {
        root.application_window_ = state;
    }
    // `root`'s state in its run, observed; empty outside a run.
    [[nodiscard]] static std::weak_ptr<ApplicationWindowState> application_window(const SurfaceRoot& root) noexcept {
        const std::weak_ptr<ApplicationWindowState> result = root.application_window_;
        return result;
    }
};

} // namespace gui_forms::detail
