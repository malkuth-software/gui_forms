#pragma once

// The authority to join a window's help offers (B10): a HelpProvider's
// fallback joins the offers that help requests no handler took reach, and
// leaves them on disposal.
//
// Holder: HelpProvider.
//
// Stale use: the provider joins on construction, given a live window, and
// leaves through its lifetime observer, doing nothing once that has expired
// (the window and its offers are gone with it). The functions are static and
// keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

namespace gui_forms::detail {

class SurfaceHelpAccess final {
public:
    SurfaceHelpAccess() = delete;

    static void add_help_fallback(SurfaceRoot& root, WindowHelpFallback& fallback) {
        root.add_help_fallback(fallback);
    }
    static void remove_help_fallback(SurfaceRoot& root, const WindowHelpFallback& fallback) noexcept {
        root.remove_help_fallback(fallback);
    }
};

} // namespace gui_forms::detail
