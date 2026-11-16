// The macOS direct path: IOSurfaces as a layer's contents
// (macos_iosurface_present.hpp). Main thread only.

#include "macos_iosurface_present.hpp"
#include "macos_view.hpp"

#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace gui_forms::host::macos {

namespace {

// The most surfaces kept: one shown, one the window server may still be
// reading, one to write.
constexpr std::size_t maximum_surfaces = 3U;

// 'BGRA': 32-bit pixels, bytes B, G, R, A in memory, the raster's layout.
constexpr OSType bgra_pixel_format = 0x42475241U;

} // namespace

struct MacIOSurfacePath::State final {
    struct SurfaceSlot final {
        IOSurfaceRef surface{};
        // The present that last showed this surface; 0 for never.
        std::uint64_t shown_at{};
    };

    // The owner (observing): the view holds the shim that holds this path.
    __unsafe_unretained GUIFormsView* view{};
    CGColorSpaceRef color_space{};
    // The layer over the view's own content, made with the first present;
    // strong (ARC).
    CALayer* layer{};
    std::array<SurfaceSlot, maximum_surfaces> slots{};
    std::size_t count{};
    presentation::PresentExtent extent{};
    // The slot granted and not yet presented.
    std::size_t held{};
    bool holding{};
    // The slot the layer shows.
    std::size_t shown{};
    bool showing{};
    std::uint64_t presents{};
};

namespace {

// Makes one surface of `extent` in the raster's layout, tagged with the
// colour space; null when the window server declines.
IOSurfaceRef make_surface(presentation::PresentExtent extent, CGColorSpaceRef colorSpace) {
    const std::size_t row_bytes =
        IOSurfaceAlignProperty(kIOSurfaceBytesPerRow, static_cast<std::size_t>(extent.width) * 4U);
    NSDictionary* properties = @{
        (__bridge NSString*)kIOSurfaceWidth: @(extent.width),
        (__bridge NSString*)kIOSurfaceHeight: @(extent.height),
        (__bridge NSString*)kIOSurfaceBytesPerElement: @(4U),
        (__bridge NSString*)kIOSurfaceBytesPerRow: @(row_bytes),
        (__bridge NSString*)kIOSurfacePixelFormat: @(bgra_pixel_format),
    };
    IOSurfaceRef surface = IOSurfaceCreate((__bridge CFDictionaryRef)properties);
    if (surface == nullptr) return nullptr;
    if (colorSpace != nullptr) {
        CFPropertyListRef tag = CGColorSpaceCopyPropertyList(colorSpace);
        if (tag != nullptr) {
            IOSurfaceSetValue(surface, kIOSurfaceColorSpace, tag);
            CFRelease(tag);
        }
    }
    return surface;
}

// Gives back every surface (the layer keeps its own reference to the one
// it shows until its contents change).
void free_surfaces(MacIOSurfacePath::State& state) noexcept {
    if (state.holding) {
        IOSurfaceUnlock(state.slots[state.held].surface, 0, nullptr);
        state.holding = false;
    }
    for (std::size_t index = 0U; index < state.count; ++index) {
        if (state.slots[index].surface != nullptr) CFRelease(state.slots[index].surface);
        state.slots[index] = MacIOSurfacePath::State::SurfaceSlot{};
    }
    state.count = 0U;
    state.showing = false;
}

// Clears and hides the layer, so that the view's own content (the copy
// path's) shows.
void hide_layer(MacIOSurfacePath::State& state) noexcept {
    if (state.layer == nil) return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    state.layer.contents = nil;
    state.layer.hidden = YES;
    [CATransaction commit];
}

} // namespace

MacIOSurfacePath::MacIOSurfacePath(GUIFormsView* view, CGColorSpaceRef colorSpace)
    : state_(std::make_unique<State>()) {
    (*state_).view = view;
    (*state_).color_space = colorSpace != nullptr ? CGColorSpaceRetain(colorSpace) : nullptr;
}

MacIOSurfacePath::~MacIOSurfacePath() {
    State& state = *state_;
    hide_layer(state);
    free_surfaces(state);
    if (state.layer != nil) [state.layer removeFromSuperlayer];
    state.layer = nil;
    if (state.color_space != nullptr) CGColorSpaceRelease(state.color_space);
}

presentation::DirectAcquire MacIOSurfacePath::acquire(presentation::PresentExtent extent) {
    State& state = *state_;
    if (state.holding) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
    // A new size makes new surfaces; the layer shows the old one until the
    // first present at the new size.
    if (extent != state.extent) {
        free_surfaces(state);
        state.extent = extent;
    }
    std::size_t chosen = state.count;
    for (std::size_t index = 0U; index < state.count; ++index) {
        if (state.showing && index == state.shown) continue;
        if (IOSurfaceIsInUse(state.slots[index].surface)) continue;
        chosen = index;
        break;
    }
    if (chosen == state.count) {
        if (state.count == maximum_surfaces)
            return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
        IOSurfaceRef made = make_surface(extent, state.color_space);
        if (made == nullptr) return presentation::DirectAcquire::refused(presentation::DirectRefusal::resources);
        state.slots[state.count] = State::SurfaceSlot{made, 0U};
        ++state.count;
    }
    IOSurfaceRef surface = state.slots[chosen].surface;
    std::uint32_t seed = 0U;
    // Options 0: a read-write lock; the CPU writes the surface.
    if (IOSurfaceLock(surface, 0, &seed) != kIOReturnSuccess)
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::lost);
    void* const base = IOSurfaceGetBaseAddress(surface);
    const std::size_t row_bytes = IOSurfaceGetBytesPerRow(surface);
    if (base == nullptr || row_bytes < static_cast<std::size_t>(extent.width) * 4U) {
        IOSurfaceUnlock(surface, 0, nullptr);
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::lost);
    }
    state.held = chosen;
    state.holding = true;
    presentation::DirectBuffer buffer{};
    buffer.pixels = static_cast<std::byte*>(base);
    buffer.row_bytes = row_bytes;
    buffer.extent = extent;
    const std::uint64_t shown_at = state.slots[chosen].shown_at;
    buffer.age = shown_at == 0U ? 0U : static_cast<std::uint32_t>(state.presents - shown_at + 1U);
    return presentation::DirectAcquire::granted(buffer);
}

presentation::PresentOutcome MacIOSurfacePath::present(std::span<const presentation::PresentRect> damage) {
    // The window server recomposites the whole layer when its contents
    // change; it takes no damage rectangles.
    static_cast<void>(damage);
    State& state = *state_;
    if (!state.holding) return presentation::PresentOutcome::failed;
    IOSurfaceRef surface = state.slots[state.held].surface;
    state.holding = false;
    if (IOSurfaceUnlock(surface, 0, nullptr) != kIOReturnSuccess) return presentation::PresentOutcome::lost;
    GUIFormsView* const view = state.view;
    if (view == nil) return presentation::PresentOutcome::failed;
    if (state.layer == nil) {
        if (view.layer == nil) view.wantsLayer = YES;
        CALayer* const host = view.layer;
        if (host == nil) return presentation::PresentOutcome::failed;
        state.layer = [CALayer layer];
        state.layer.anchorPoint = CGPointZero;
        // One surface pixel per device pixel from the top left; a surface
        // smaller than the view (a resize not yet painted) leaves the rest
        // to the view's own content beneath.
        state.layer.contentsGravity = kCAGravityTopLeft;
        // Beneath every other sublayer (the compositor's), above the view's
        // own backing content only.
        [host insertSublayer:state.layer atIndex:0];
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    state.layer.hidden = NO;
    state.layer.opaque = opaque ? YES : NO;
    state.layer.contentsScale = scale;
    state.layer.frame = NSRectToCGRect(view.bounds);
    state.layer.contents = (__bridge id)surface;
    [CATransaction commit];
    ++state.presents;
    state.slots[state.held].shown_at = state.presents;
    state.shown = state.held;
    state.showing = true;
    return presentation::PresentOutcome::presented;
}

void MacIOSurfacePath::detach_view() noexcept { (*state_).view = nil; }

void MacIOSurfacePath::release() noexcept {
    State& state = *state_;
    hide_layer(state);
    free_surfaces(state);
    // The copy path draws only inside the rectangles AppKit asks for, and
    // the backing store beneath the layer was not drawn while it showed:
    // the whole view is drawn again on the next pass (not from inside this
    // draw, where a new mark can be lost).
    if (state.view != nil)
        [state.view performSelector:@selector(markPresentationDirty) withObject:nil afterDelay:0.0];
}

} // namespace gui_forms::host::macos
