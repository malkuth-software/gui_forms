#pragma once

// The macOS host's presentation paths (docs/user/presentation.md): the copy
// path, which is the view's CoreGraphics draw of the raster into AppKit's
// backing store exactly as before the shim, and the direct path
// (macos_iosurface_present.hpp), IOSurfaces shown as a layer's contents.
// AppKit, CoreGraphics and IOSurface types stay in src/host/macos/.
//
// Main thread only, as every message to the view.

#include "../../../core/host/presentation/presentation_shim.hpp"

#include <cstdint>
#include <span>

@class GUIFormsView;

namespace gui_forms::host::macos {

// The copy path: copy() asks the view to draw the frame into the current
// AppKit graphics context (inside drawRect:). AppKit clips the draw to the
// rectangles being drawn, so the rectangles the shim passes only decide
// whether a draw happens. AppKit's backing store cannot be moved, so the
// path never scrolls. Owned by its view, declared before the shim that
// borrows it.
class MacViewCopyPath final : public presentation::PresentationCopyPath {
public:
    explicit MacViewCopyPath(GUIFormsView* view) noexcept : view_(view) {}

    [[nodiscard]] bool scrolls() const noexcept override { return false; }
    bool scroll(const presentation::PresentScrollPlan& plan) override;
    presentation::PresentOutcome copy(const presentation::PresentSourcePixels& source,
                                      std::span<const presentation::PresentRect> rects) override;

    // Set by each present before it runs the shim: the size, in points,
    // the raster is drawn at. Written by copy(): the draw's duration.
    double presentation_width{};
    double presentation_height{};
    std::uint64_t copy_nanoseconds{};

private:
    // The owner (observing): the view holds this path and outlives it.
    __unsafe_unretained GUIFormsView* view_;
};

} // namespace gui_forms::host::macos
