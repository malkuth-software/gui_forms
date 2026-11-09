#include "macos_compositor.hpp"

// The backdrop container: a plain view that only holds the effect view and
// the raster view (make_macos_content_view).
@interface GUIFormsBackdropContainer : NSView
@end

@implementation GUIFormsBackdropContainer
@end

namespace gui_forms::host::detail {
namespace {

// The container holding `raster`, or nil when the window was made without
// one.
GUIFormsBackdropContainer* container_of(NSView* raster) {
    NSView* const parent = raster.superview;
    if (![parent isKindOfClass:[GUIFormsBackdropContainer class]]) return nil;
    GUIFormsBackdropContainer* const container = static_cast<GUIFormsBackdropContainer*>(parent);
    return container;
}

NSVisualEffectView* effect_of(GUIFormsBackdropContainer* container) {
    if (container == nil || container.subviews.count == 0U) return nil;
    NSView* const first = container.subviews[0];
    if (![first isKindOfClass:[NSVisualEffectView class]]) return nil;
    NSVisualEffectView* const effect = static_cast<NSVisualEffectView*>(first);
    return effect;
}

// The materials the portable purposes map to (PLAN.md §2.2). The bench
// run of the C3 probe confirms them; CI's runner has reduce transparency
// on and shows only AppKit's opaque fallback.
NSVisualEffectMaterial material_for(WindowBackdrop backdrop) noexcept {
    if (backdrop == WindowBackdrop::transient_material) return NSVisualEffectMaterialPopover;
    return NSVisualEffectMaterialUnderWindowBackground;
}

// A colour channel in [0, 1] as a byte, rounded; out-of-range values are
// clamped (extended sRGB can exceed the unit range).
std::uint8_t channel_byte(CGFloat channel) noexcept {
    const double clamped = channel < 0.0 ? 0.0 : (channel > 1.0 ? 1.0 : static_cast<double>(channel));
    const std::uint8_t byte = static_cast<std::uint8_t>(clamped * 255.0 + 0.5);
    return byte;
}

// A system colour as sRGB, resolved against the current drawing appearance
// (on the main thread, outside a draw, the application's effective
// appearance); none when AppKit cannot convert it.
std::optional<Color> srgb_colour(NSColor* colour) {
    NSColor* const converted = colour == nil ? nil : [colour colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (converted == nil) return std::nullopt;
    CGFloat red = 0.0;
    CGFloat green = 0.0;
    CGFloat blue = 0.0;
    CGFloat alpha = 0.0;
    [converted getRed:&red green:&green blue:&blue alpha:&alpha];
    const Color result = Color::rgba(channel_byte(red), channel_byte(green), channel_byte(blue), channel_byte(alpha));
    return result;
}

} // namespace

CompositorState read_macos_compositor_state(CompositorGeneration generation) {
    CompositorState state{};
    state.generation = generation;
    state.compositing = CompositorPresence::present;
    state.support[static_cast<std::size_t>(WindowBackdrop::transparent)] = WindowBackdropSupport::supported;
    state.support[static_cast<std::size_t>(WindowBackdrop::window_material)] = WindowBackdropSupport::supported;
    state.support[static_cast<std::size_t>(WindowBackdrop::transient_material)] = WindowBackdropSupport::supported;
    // macOS has no separate transparency-effects switch.
    state.transparency_enabled = true;
    NSWorkspace* const workspace = NSWorkspace.sharedWorkspace;
    state.reduced_transparency = workspace.accessibilityDisplayShouldReduceTransparency == YES;
    state.high_contrast = false;
    // T4 theme variants: the application's effective appearance (the
    // system's Light or Dark unless the application set its own), matched
    // against Aqua and Dark Aqua; the accent and text-selection colours as
    // the system reports them now.
    NSApplication* const application = NSApp;
    NSAppearance* const appearance = application == nil ? nil : application.effectiveAppearance;
    if (appearance != nil) {
        NSArray<NSAppearanceName>* const names = @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ];
        const NSAppearanceName matched = [appearance bestMatchFromAppearancesWithNames:names];
        state.dark_scheme = matched != nil && [matched isEqualToString:NSAppearanceNameDarkAqua];
    }
    state.system_colours.accent = srgb_colour(NSColor.controlAccentColor);
    state.system_colours.highlight = srgb_colour(NSColor.selectedTextBackgroundColor);
    return state;
}

NSView* make_macos_content_view(NSView* raster, bool non_opaque) {
    if (!non_opaque) return raster;
    const NSRect bounds = NSMakeRect(0.0, 0.0, raster.frame.size.width, raster.frame.size.height);
    GUIFormsBackdropContainer* const container = [[GUIFormsBackdropContainer alloc] initWithFrame:bounds];
    NSVisualEffectView* const effect = [[NSVisualEffectView alloc] initWithFrame:bounds];
    effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.state = NSVisualEffectStateFollowsWindowActiveState;
    effect.material = NSVisualEffectMaterialUnderWindowBackground;
    effect.hidden = YES;
    raster.frame = bounds;
    raster.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [container addSubview:effect];
    [container addSubview:raster];
    return container;
}

HostServiceStatus apply_macos_backdrop(NSView* raster, WindowBackdrop backdrop) {
    NSWindow* const window = raster == nil ? nil : raster.window;
    if (window == nil) {
        if (backdrop == WindowBackdrop::opaque) return {};
        return {HostServiceError::backend_failure};
    }
    NSVisualEffectView* const effect = effect_of(container_of(raster));
    const bool material = backdrop == WindowBackdrop::window_material ||
                          backdrop == WindowBackdrop::transient_material;
    // Validated before anything changes.
    if (material && effect == nil) return {HostServiceError::backend_failure};
    if (backdrop == WindowBackdrop::opaque) {
        if (effect != nil) effect.hidden = YES;
        window.backgroundColor = NSColor.windowBackgroundColor;
        window.opaque = YES;
        // The raster presents opaque pixels again (the view reads the
        // window's opacity when it draws).
        raster.needsDisplay = YES;
        [window invalidateShadow];
        return {};
    }
    window.opaque = NO;
    window.backgroundColor = NSColor.clearColor;
    window.ignoresMouseEvents = NO;
    raster.needsDisplay = YES;
    if (!material) {
        if (effect != nil) effect.hidden = YES;
        [window invalidateShadow];
        return {};
    }
    effect.material = material_for(backdrop);
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.state = NSVisualEffectStateFollowsWindowActiveState;
    effect.hidden = NO;
    // The shadow follows the window's alpha, which has just changed.
    [window invalidateShadow];
    return {};
}

} // namespace gui_forms::host::detail
