// The macOS Metal buffer route (macos_metal_present.hpp). Main thread only.

#include "macos_metal_present.hpp"
#include "macos_view.hpp"

#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace gui_forms::host::macos {

namespace {

// The drawables the layer cycles through, and the surfaces whose last
// present is remembered for their age (one more than the pool, for a
// pool the system replaces).
constexpr NSUInteger maximum_drawables = 3U;
constexpr std::size_t remembered_surfaces = 4U;
// The presents tracked until they are replaced on screen: more than the
// pool, so that a full pool is seen before the table is.
constexpr std::size_t tracked_presents = 8U;

} // namespace

struct MacMetalPath::State final {
    struct Shown final {
        // Observing: compared, never dereferenced.
        IOSurfaceRef surface{};
        std::uint64_t shown_at{};
    };

    __unsafe_unretained GUIFormsView* view{};
    CGColorSpaceRef color_space{};
    // Strong (ARC): the device, made at the first acquire; the layer over
    // the view's own content; the drawable granted and not yet presented.
    id<MTLDevice> device{};
    CAMetalLayer* layer{};
    id<CAMetalDrawable> drawable{};
    IOSurfaceRef locked{};
    bool unsupported{};
    // The drawables presented and not yet known to be replaced on screen,
    // oldest first (strong: kept only to read presentedTime, and dropped as
    // soon as a later one is displayed, so they return to the pool).
    std::array<id<CAMetalDrawable>, tracked_presents> presented{};
    std::size_t presented_count{};
    std::array<Shown, remembered_surfaces> shown{};
    std::size_t next_shown{};
    std::uint64_t presents{};
};

namespace {

// The last present that showed `surface`; 0 for none remembered.
std::uint64_t shown_at(const MacMetalPath::State& state, IOSurfaceRef surface) noexcept {
    for (const MacMetalPath::State::Shown& entry : state.shown) {
        if (entry.surface == surface) return entry.shown_at;
    }
    return 0U;
}

// Remembers that `surface` was shown by the latest present, replacing its
// own entry or the oldest.
void remember_shown(MacMetalPath::State& state, IOSurfaceRef surface) noexcept {
    for (MacMetalPath::State::Shown& entry : state.shown) {
        if (entry.surface == surface) {
            entry.shown_at = state.presents;
            return;
        }
    }
    state.shown[state.next_shown] = MacMetalPath::State::Shown{surface, state.presents};
    state.next_shown = (state.next_shown + 1U) % remembered_surfaces;
}

// The drawables the window server still holds (count_present_holds over
// each tracked present's presentedTime): those replaced on screen are
// forgotten first. Never waits.
std::size_t held_drawables(MacMetalPath::State& state) noexcept {
    std::array<bool, tracked_presents> displayed{};
    for (std::size_t index = 0U; index < state.presented_count; ++index) {
        // presentedTime is 0 until the drawable reaches the screen.
        displayed[index] = state.presented[index].presentedTime > 0.0;
    }
    const presentation::PresentHoldCount count =
        presentation::count_present_holds(std::span<const bool>(displayed.data(), state.presented_count));
    for (std::size_t index = 0U; index < count.held; ++index)
        state.presented[index] = state.presented[index + count.displaced];
    for (std::size_t index = count.held; index < state.presented_count; ++index) state.presented[index] = nil;
    state.presented_count = count.held;
    return count.held;
}

// Forgets every tracked present (release, destruction).
void forget_presented(MacMetalPath::State& state) noexcept {
    for (std::size_t index = 0U; index < state.presented_count; ++index) state.presented[index] = nil;
    state.presented_count = 0U;
}

// Unlocks and drops the drawable held between acquire and present.
void drop_drawable(MacMetalPath::State& state) noexcept {
    if (state.locked != nullptr) IOSurfaceUnlock(state.locked, 0, nullptr);
    state.locked = nullptr;
    state.drawable = nil;
}

void hide_layer(MacMetalPath::State& state) noexcept {
    if (state.layer == nil) return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    state.layer.hidden = YES;
    [CATransaction commit];
}

// Makes the layer beneath the compositor's sublayers; false without a view
// or a host layer.
bool make_layer(MacMetalPath::State& state) {
    GUIFormsView* const view = state.view;
    if (view == nil) return false;
    if (view.layer == nil) view.wantsLayer = YES;
    CALayer* const host = view.layer;
    if (host == nil) return false;
    CAMetalLayer* const layer = [CAMetalLayer layer];
    layer.device = state.device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    // CPU-writable drawables: their textures are backed by IOSurfaces.
    layer.framebufferOnly = NO;
    layer.maximumDrawableCount = maximum_drawables;
    // A backstop only: acquire never calls nextDrawable while the pool is
    // held (held_drawables), so it does not wait for a drawable.
    layer.allowsNextDrawableTimeout = YES;
    layer.presentsWithTransaction = NO;
    layer.anchorPoint = CGPointZero;
    layer.contentsGravity = kCAGravityTopLeft;
    if (state.color_space != nullptr) layer.colorspace = state.color_space;
    layer.hidden = YES;
    [host insertSublayer:layer atIndex:0];
    state.layer = layer;
    return true;
}

} // namespace

MacMetalPath::MacMetalPath(GUIFormsView* view, CGColorSpaceRef colorSpace) : state_(std::make_unique<State>()) {
    (*state_).view = view;
    (*state_).color_space = colorSpace != nullptr ? CGColorSpaceRetain(colorSpace) : nullptr;
}

MacMetalPath::~MacMetalPath() {
    State& state = *state_;
    drop_drawable(state);
    forget_presented(state);
    hide_layer(state);
    if (state.layer != nil) [state.layer removeFromSuperlayer];
    state.layer = nil;
    state.device = nil;
    if (state.color_space != nullptr) CGColorSpaceRelease(state.color_space);
}

presentation::DirectAcquire MacMetalPath::acquire(presentation::PresentExtent extent) {
    State& state = *state_;
    if (state.unsupported) return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
    if (state.drawable != nil) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
    if (state.device == nil) state.device = MTLCreateSystemDefaultDevice();
    if (state.device == nil) {
        state.unsupported = true;
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
    }
    if (state.layer == nil && !make_layer(state))
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::resources);
    const CGSize size = CGSizeMake(static_cast<CGFloat>(extent.width), static_cast<CGFloat>(extent.height));
    if (!CGSizeEqualToSize(state.layer.drawableSize, size)) state.layer.drawableSize = size;
    // No drawable ready is a refusal for this frame, never a wait: while
    // every drawable of the pool is presented and not yet replaced on
    // screen, nextDrawable would block, so it is not called. The frame is
    // presented by the copy path with the same pixels.
    if (held_drawables(state) >= maximum_drawables || state.presented_count == tracked_presents)
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
    id<CAMetalDrawable> drawable = [state.layer nextDrawable];
    if (drawable == nil) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
    IOSurfaceRef surface = drawable.texture.iosurface;
    if (surface == nullptr) {
        state.unsupported = true;
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
    }
    if (IOSurfaceGetWidth(surface) < extent.width || IOSurfaceGetHeight(surface) < extent.height)
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::extent);
    std::uint32_t seed = 0U;
    if (IOSurfaceLock(surface, 0, &seed) != kIOReturnSuccess)
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::lost);
    void* const base = IOSurfaceGetBaseAddress(surface);
    const std::size_t row_bytes = IOSurfaceGetBytesPerRow(surface);
    if (base == nullptr || row_bytes < static_cast<std::size_t>(extent.width) * 4U) {
        IOSurfaceUnlock(surface, 0, nullptr);
        return presentation::DirectAcquire::refused(presentation::DirectRefusal::lost);
    }
    state.drawable = drawable;
    state.locked = surface;
    presentation::DirectBuffer buffer{};
    buffer.pixels = static_cast<std::byte*>(base);
    buffer.row_bytes = row_bytes;
    buffer.extent = extent;
    const std::uint64_t last = shown_at(state, surface);
    buffer.age = last == 0U ? 0U : static_cast<std::uint32_t>(state.presents - last + 1U);
    return presentation::DirectAcquire::granted(buffer);
}

presentation::PresentOutcome MacMetalPath::present(std::span<const presentation::PresentRect> damage) {
    // The drawable is presented whole; it takes no damage rectangles.
    static_cast<void>(damage);
    State& state = *state_;
    if (state.drawable == nil) return presentation::PresentOutcome::failed;
    IOSurfaceRef surface = state.locked;
    state.locked = nullptr;
    if (IOSurfaceUnlock(surface, 0, nullptr) != kIOReturnSuccess) {
        state.drawable = nil;
        return presentation::PresentOutcome::lost;
    }
    GUIFormsView* const view = state.view;
    if (view == nil) {
        state.drawable = nil;
        return presentation::PresentOutcome::failed;
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    state.layer.hidden = NO;
    state.layer.opaque = opaque ? YES : NO;
    state.layer.contentsScale = scale;
    state.layer.frame = NSRectToCGRect(view.bounds);
    [CATransaction commit];
    // Presentation only: no command buffer is made or committed.
    [state.drawable present];
    state.presented[state.presented_count] = state.drawable;
    ++state.presented_count;
    state.drawable = nil;
    ++state.presents;
    remember_shown(state, surface);
    return presentation::PresentOutcome::presented;
}

void MacMetalPath::detach_view() noexcept { (*state_).view = nil; }

void MacMetalPath::release() noexcept {
    State& state = *state_;
    drop_drawable(state);
    forget_presented(state);
    hide_layer(state);
    state.shown = {};
    state.next_shown = 0U;
    // As the IOSurface path: the backing store beneath the layer is drawn
    // whole on the next pass.
    if (state.view != nil)
        [state.view performSelector:@selector(markPresentationDirty) withObject:nil afterDelay:0.0];
}

} // namespace gui_forms::host::macos
