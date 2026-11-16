#pragma once

// Relative pointer mode on macOS (capabilities relative_pointer,
// pointer_confinement, pointer_warp; gui_forms/events/relative_pointer).
// AppKitHostServices applies the mode: locked disassociates the pointer
// from the mouse (CGAssociateMouseAndMouseCursorPosition(false)) and hides
// it; confined hides it on request and is kept inside its rectangle by the
// view, which warps it back (AppKit has no native confinement); every mode
// turns AppKit's mouse coalescing off while it lasts, so each native
// sample arrives. The view reads the state here on each mouse event: it
// sends one HostRelativeMotionEvent per moved or dragged event (deltaX,
// deltaY: AppKit's accelerated motion, in points, y down), and in confined
// and locked modes no absolute move. macOS gives no unaccelerated deltas
// without IOHID, so raw_pointer_motion is not reported.
//
// Main thread only, as AppKit.

#include "gui_forms/host/services/host_services.hpp"

namespace gui_forms::host::detail {

// active: the services put the pointer in `mode`. confine: the confined
// rectangle in the view's (window logical) units.
struct MacRelativePointerState final {
    bool active{};
    RelativePointerMode mode{RelativePointerMode::free};
    Rect confine{};
};

// Defined beside AppKitHostServices; inactive for other services.
[[nodiscard]] MacRelativePointerState macos_relative_pointer_state(const HostServices& services) noexcept;

} // namespace gui_forms::host::detail
