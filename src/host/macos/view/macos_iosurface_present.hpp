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
// refuses as resources; one that cannot be locked is lost.
//
// Main thread only, as every message to the view. AppKit, QuartzCore and
// IOSurface types stay in the .mm (State).

#include "../../../core/host/presentation/presentation_shim.hpp"

#include <CoreGraphics/CoreGraphics.h>

#include <memory>
#include <span>

@class GUIFormsView;

namespace gui_forms::host::macos {

class MacIOSurfacePath final : public presentation::PresentationDirectPath {
public:
    // `view` (observing) owns the shim that owns this path; `colorSpace` is
    // the view's present colour space, retained here.
    MacIOSurfacePath(GUIFormsView* view, CGColorSpaceRef colorSpace);
    ~MacIOSurfacePath() override;

    presentation::DirectAcquire acquire(presentation::PresentExtent extent) override;
    presentation::PresentOutcome present(std::span<const presentation::PresentRect> damage) override;
    void release() noexcept override;

    // Forgets the view (its dealloc): the layer is still removed and the
    // surfaces freed, and no message is sent to the view after this.
    void detach_view() noexcept;

    // Whether the layer shows the window's pixels opaquely (set from the
    // window before each present).
    bool opaque{true};
    // Points per device pixel's reciprocal: the view's backing scale.
    double scale{1.0};

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace gui_forms::host::macos
