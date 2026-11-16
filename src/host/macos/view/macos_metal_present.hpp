#pragma once

// The macOS Metal buffer route (docs/user/presentation.md; host capability
// metal_drawables): CAMetalLayer drawables, written by the CPU through the
// IOSurface behind each drawable's texture and presented with the
// drawable's own present. Metal is used only to obtain and present the
// buffer: the system default device for the layer, nextDrawable, the
// texture's iosurface, and -[CAMetalDrawable present]. No command buffer,
// encoder, shader, pipeline or other GPU work. Built only with
// GUI_FORMS_ENABLE_METAL_PRESENT, and chosen only when GUI_FORMS_PRESENT
// names both "direct" and "metal".
//
// Refusals: no Metal device, or a drawable without an IOSurface (a
// framebuffer-only or compressed texture), is unsupported for the window's
// life; no drawable ready (nextDrawable returned nil) is busy; a drawable
// smaller than the frame is extent; a surface that cannot be locked is
// lost. The drawable's IOSurface identity gives the buffer's age.
//
// Main thread only, as every message to the view. Metal, AppKit,
// QuartzCore and IOSurface types stay in the .mm (State).

#include "macos_presentation.hpp"

#include <CoreGraphics/CoreGraphics.h>

#include <memory>
#include <span>

@class GUIFormsView;

namespace gui_forms::host::macos {

class MacMetalPath final : public MacDirectPath {
public:
    // `view` (observing) owns the shim that owns this path; `colorSpace` is
    // the view's present colour space, retained here.
    MacMetalPath(GUIFormsView* view, CGColorSpaceRef colorSpace);
    ~MacMetalPath() override;

    presentation::DirectAcquire acquire(presentation::PresentExtent extent) override;
    presentation::PresentOutcome present(std::span<const presentation::PresentRect> damage) override;
    void release() noexcept override;
    void detach_view() noexcept override;

    struct State;

private:
    std::unique_ptr<State> state_;
};

} // namespace gui_forms::host::macos
