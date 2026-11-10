#pragma once

// The macOS compositor adapter (stage T5, phase C3;
// notes:planning/stages/T5-compositor/PLAN.md §1.2 and MEASUREMENTS.md C3).
// AppKit types stay here, inside src/host/macos/.

#import <AppKit/AppKit.h>

#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/host/services/host_services.hpp"

#include <cstdint>
#include <optional>

namespace gui_forms::host::detail {

// The window server always composites. `transparent` and both materials
// are supported at the macOS 14 deployment target; reduced transparency is
// the accessibility display option. Owner decision T5-Q7: "increase
// contrast" never sets `high_contrast`, so it stays false here.
// `dark_scheme` is the application's effective appearance matched against
// Dark Aqua, and `system_colours` the control accent and selected-text
// background colours in sRGB. Main thread only (NSApp, dynamic colours).
[[nodiscard]] CompositorState read_macos_compositor_state(CompositorGeneration generation);

// The view a window's raster view is shown in. Not declared non-opaque:
// the raster view itself, as before stage T5. Declared: a container whose
// first subview is a hidden behind-window NSVisualEffectView and whose
// second is the raster view, both filling it, so that a backdrop change
// never moves the raster view out of its window.
[[nodiscard]] NSView* make_macos_content_view(NSView* raster, bool non_opaque);

// Applies a granted backdrop to the window showing `raster`:
// - `opaque`: the window opaque on its background colour, the effect view
//   hidden (today's window);
// - `transparent`: the window non-opaque on a clear background, the effect
//   view hidden;
// - `window_material` and `transient_material`: as transparent, with the
//   effect view shown at underWindowBackground or popover, blending behind
//   the window and following its active state.
// Every non-opaque grant sets ignoresMouseEvents to NO explicitly: without
// it the window server sends clicks over clear pixels to the window behind
// (MEASUREMENTS.md C3; owner decision T5-Q6). A material for a window made
// without the container is backend_failure, changing nothing; so is any
// grant without a window. Opaque without a window changes nothing and is
// accepted.
[[nodiscard]] HostServiceStatus apply_macos_backdrop(NSView* raster, WindowBackdrop backdrop);

// Defined beside AppKitHostServices: the raster view whose window that
// window's services apply backdrops to. Held weakly; nil detaches it.
void bind_macos_backdrop_view(HostServices& services, NSView* raster);

} // namespace gui_forms::host::detail
