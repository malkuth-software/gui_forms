// The macOS splashscreen (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S):
// a borderless non-activating panel at the floating level that ignores the
// mouse, centred on the main screen, shown before Application::create. Its
// view keeps one CGImage of the last frame, painted by paint_splashscreen
// into a CPU raster with the bundle's fonts.

#include "macos_host_private.hpp"
#include "macos_bundled_fonts.hpp"

#include <pthread.h>

@interface GUIFormsSplashView : NSView
// The frame to draw (retained by the view), or null.
- (void)setFrameImage:(CGImageRef)image;
// The bodies of this class's native entries (the AppKit overrides below):
// each entry calls its answer through the fail-fast boundary
// (call_failing_fast).
- (void)answerDrawRect:(NSRect)dirtyRect;
- (void)answerDealloc;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsSplashView's native entries: each sends the entry's answer message.
void run_splash_view_draw_rect(GUIFormsSplashView* object, NSRect dirtyRect) {
    [object answerDrawRect:dirtyRect];
}
void run_splash_view_dealloc(__unsafe_unretained GUIFormsSplashView* object) {
    [object answerDealloc];
}
} // namespace

@implementation GUIFormsSplashView {
    CGImageRef _frameImage;
}

- (BOOL)isOpaque {
    return YES;
}

- (void)setFrameImage:(CGImageRef)image {
    if (image != nullptr) CGImageRetain(image);
    if (_frameImage != nullptr) CGImageRelease(_frameImage);
    _frameImage = image;
}

- (void)drawRect:(NSRect)dirtyRect {
    gui_forms::detail::call_failing_fast("macos.splash_view.drawRect:", &run_splash_view_draw_rect, self, dirtyRect);
}

- (void)answerDrawRect:(NSRect)dirtyRect {
    static_cast<void>(dirtyRect);
    if (_frameImage == nullptr) return;
    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    CGContextDrawImage(context, NSRectToCGRect(self.bounds), _frameImage);
}

- (void)dealloc {
    gui_forms::detail::call_failing_fast("macos.splash_view.dealloc", &run_splash_view_dealloc, self);
}

- (void)answerDealloc {
    if (_frameImage != nullptr) CGImageRelease(_frameImage);
    _frameImage = nullptr;
}

@end

namespace gui_forms::host {

namespace {

class MacSplashscreenSurface final : public gui_forms::detail::SplashscreenSurface {
public:
    MacSplashscreenSurface(NSPanel* panel, GUIFormsSplashView* view, double scale) noexcept
        : panel_(panel), view_(view), scale_(scale), color_space_(CGColorSpaceCreateWithName(kCGColorSpaceSRGB)) {}

    ~MacSplashscreenSurface() override {
        [view_ setFrameImage:nullptr];
        [panel_ orderOut:nil];
        [panel_ close];
        if (color_space_ != nullptr) CGColorSpaceRelease(color_space_);
    }

    MacSplashscreenSurface(const MacSplashscreenSurface&) = delete;
    MacSplashscreenSurface& operator=(const MacSplashscreenSurface&) = delete;

    [[nodiscard]] bool fonts_ready() { return raster_.register_bundled_fonts(bundle_font_set()); }

    bool present(const gui_forms::detail::SplashscreenFrame& frame) override {
        if (color_space_ == nullptr) return false;
        if (frame.images != nullptr && !raster_.synchronize_images(*frame.images)) return false;
        static_cast<void>(raster_.resize(frame.size, scale_));
        DamageRegion whole;
        whole.add(Rect{0.0, 0.0, frame.size.width, frame.size.height});
        raster_.begin_frame(whole);
        gui_forms::detail::paint_splashscreen(raster_, frame);
        raster_.end_frame();
        const void* pixels = raster_.pixels();
        if (pixels == nullptr) return false;
        // The view draws the image after this returns: the provider copies
        // nothing, so the image is made from a copy of the frame.
        CFDataRef bytes = CFDataCreate(nullptr, static_cast<const UInt8*>(pixels),
                                       static_cast<CFIndex>(raster_.byte_size()));
        if (bytes == nullptr) return false;
        CGDataProviderRef provider = CGDataProviderCreateWithCFData(bytes);
        CFRelease(bytes);
        if (provider == nullptr) return false;
        const CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(
            static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst) |
            static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little));
        CGImageRef image = CGImageCreate(raster_.pixel_width(), raster_.pixel_height(), 8, 32, raster_.row_bytes(),
                                         color_space_, bitmapInfo, provider, nullptr, false,
                                         kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);
        if (image == nullptr) return false;
        [view_ setFrameImage:image];
        CGImageRelease(image);
        // No run loop runs yet: draw and flush now.
        [view_ setNeedsDisplay:YES];
        [panel_ display];
        return true;
    }

private:
    NSPanel* panel_;
    GUIFormsSplashView* view_;
    double scale_{1.0};
    CGColorSpaceRef color_space_{};
    render::SkiaRaster raster_{};
};

} // namespace

std::unique_ptr<gui_forms::detail::SplashscreenSurface> open_platform_splashscreen(const Size size) {
    std::unique_ptr<gui_forms::detail::SplashscreenSurface> result;
    if (pthread_main_np() == 0) return result;
    [NSApplication sharedApplication];
    NSScreen* screen = [NSScreen mainScreen];
    if (screen == nil) return result;
    const NSRect visible = screen.visibleFrame;
    const NSRect frame = NSMakeRect(NSMidX(visible) - size.width / 2.0, NSMidY(visible) - size.height / 2.0,
                                    size.width, size.height);
    NSPanel* panel = [[NSPanel alloc] initWithContentRect:frame
                                                styleMask:NSWindowStyleMaskBorderless |
                                                          NSWindowStyleMaskNonactivatingPanel
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    panel.releasedWhenClosed = NO;
    panel.opaque = YES;
    panel.hasShadow = YES;
    panel.level = NSFloatingWindowLevel;
    panel.ignoresMouseEvents = YES;
    panel.hidesOnDeactivate = NO;
    GUIFormsSplashView* view = [[GUIFormsSplashView alloc] initWithFrame:NSMakeRect(0.0, 0.0, size.width, size.height)];
    panel.contentView = view;
    const double scale = screen.backingScaleFactor > 0.0 ? screen.backingScaleFactor : 1.0;
    std::unique_ptr<MacSplashscreenSurface> surface = std::make_unique<MacSplashscreenSurface>(panel, view, scale);
    if (!(*surface).fonts_ready()) return result;
    [panel orderFrontRegardless];
    result = std::move(surface);
    return result;
}

} // namespace gui_forms::host
