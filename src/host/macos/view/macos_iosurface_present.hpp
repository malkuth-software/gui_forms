#pragma once

// The macOS direct path (docs/user/presentation.md): IOSurfaces shown as the
// contents of a layer over the view. IOSurface is shared memory the window
// server can composite without a copy; it may be resident in GPU memory.
// GUI.Forms treats it as a blind buffer: IOSurfaceLock for CPU writing,
// CPU pixels written, IOSurfaceUnlock, and the surface set as a CALayer's
// contents. No Metal, OpenGL or other GPU API is called, linked or probed;
// only IOSurface.framework and QuartzCore's CALayer and CATransaction.
//
// Up to three surfaces are kept. A surface the window server still uses
// (IOSurfaceIsInUse), and the one the layer shows, are never written: a
// free one is taken, a new one made while fewer than three exist, and the
// path refuses as busy when all are in use. A surface that cannot be made
// refuses as resources; one that cannot be locked is lost; a frame above
// 64 MiB refuses as extent, as the other platforms' paths cap it.
//
// Main thread only, as every message to the view. AppKit, QuartzCore and
// IOSurface types stay in the .mm (State).

#include "macos_presentation.hpp"

#include <CoreGraphics/CoreGraphics.h>

#include <memory>
#include <span>

@class GUIFormsView;

namespace gui_forms::host::macos {

class MacIOSurfacePath final : public MacDirectPath {
public:
    // `view` (observing) owns the shim that owns this path; `colorSpace` is
    // the view's present colour space, retained here.
    MacIOSurfacePath(GUIFormsView* view, CGColorSpaceRef colorSpace);
    ~MacIOSurfacePath() override;

    presentation::DirectAcquire acquire(presentation::PresentExtent extent) override;
    presentation::PresentOutcome present(std::span<const presentation::PresentRect> damage) override;
    void release() noexcept override;

    void detach_view() noexcept override;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace gui_forms::host::macos
