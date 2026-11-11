#import <AppKit/AppKit.h>
#include "gui_forms/platform/macos_host.hpp"
#include "gui_forms/gui_forms.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Audit P13: the view presents its raster as a CGImage over a data provider
// with no release callback, so the image borrows the raster's pixels. That
// is sound only while CoreGraphics never keeps the image after the draw
// returns. A PDF context keeps it until the page ends, so this test records
// the view into a PDF page, then repaints the view in another colour and
// resizes it (which replaces the raster's storage) before ending the page.
// The recorded page must show the colour at recording time; under
// AddressSanitizer a borrowed image would also read freed pixels.

namespace {

constexpr const char* window_title = "GUI.Forms recorded draw check";

class RecordedRoot final : public gui_forms::Control {
public:
    explicit RecordedRoot(gui_forms::StableId id) : Control(std::move(id)) {}
    void set_color(gui_forms::Color color) {
        color_ = color;
        invalidate(gui_forms::Dirty::paint);
    }
    void on_paint(gui_forms::Painter& painter, gui_forms::Rect) override {
        ++paints;
        painter.fill_rect(client_rectangle(), color_);
    }
    int paints{};

private:
    gui_forms::Color color_{gui_forms::Color::rgba(30, 95, 180)};
};

struct State final {
    std::shared_ptr<RecordedRoot> root{};
    std::function<void()> close{};
    std::string failure{};
    bool completed{};
    std::chrono::steady_clock::time_point deadline{};
};
struct RecordedStep final {
    std::shared_ptr<State> state{};
};

NSWindow* find_recorded_window() {
    NSWindow* window = nil;
    NSString* const title = [NSString stringWithUTF8String:window_title];
    for (NSWindow* candidate in NSApp.windows) {
        if ([candidate.title isEqualToString:title]) {
            window = candidate;
            break;
        }
    }
    return window;
}

void run_recorded_step(void* context);
void queue_recorded_step(const std::shared_ptr<State>& state, std::int64_t delay_ns) {
    std::unique_ptr<RecordedStep> retained = std::make_unique<RecordedStep>();
    (*retained).state = state;
    const dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, delay_ns);
    void* const context = retained.release();
    dispatch_after_f(deadline, dispatch_get_main_queue(), context, run_recorded_step);
}

// The centre pixel of the PDF's first page drawn into a 64 x 64 sRGB
// bitmap, as four bytes R, G, B, A; all zero when the PDF cannot be read.
std::vector<std::uint8_t> page_centre(NSData* pdf) {
    std::vector<std::uint8_t> rgba(64U * 64U * 4U, 0U);
    std::vector<std::uint8_t> centre(4U, 0U);
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)pdf);
    CGPDFDocumentRef document = CGPDFDocumentCreateWithProvider(provider);
    CGDataProviderRelease(provider);
    if (document == nullptr) return centre;
    CGPDFPageRef page = CGPDFDocumentGetPage(document, 1);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef bitmap = CGBitmapContextCreate(rgba.data(), 64, 64, 8, 64 * 4, space,
                                                static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
    CGColorSpaceRelease(space);
    if (page != nullptr && bitmap != nullptr) {
        const CGRect box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
        CGContextScaleCTM(bitmap, 64.0 / box.size.width, 64.0 / box.size.height);
        CGContextDrawPDFPage(bitmap, page);
        const std::size_t at = (32U * 64U + 32U) * 4U;
        for (std::size_t channel = 0; channel < 4U; ++channel) centre[channel] = rgba[at + channel];
    }
    if (bitmap != nullptr) CGContextRelease(bitmap);
    CGPDFDocumentRelease(document);
    return centre;
}

void record(const std::shared_ptr<State>& state) {
    NSWindow* const window = find_recorded_window();
    const bool painted = window != nil && window.isVisible && (*(*state).root).paints > 0;
    if (!painted && std::chrono::steady_clock::now() < (*state).deadline) {
        queue_recorded_step(state, 50 * NSEC_PER_MSEC);
        return;
    }
    if (!painted) {
        (*state).failure = "the first frame was not painted";
        (*state).close();
        return;
    }
    NSView* const view = window.contentView;
    const NSRect bounds = view.bounds;
    NSMutableData* const pdf = [NSMutableData data];
    CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData((__bridge CFMutableDataRef)pdf);
    CGRect media = NSRectToCGRect(bounds);
    CGContextRef recording = CGPDFContextCreate(consumer, &media, nullptr);
    CGDataConsumerRelease(consumer);
    if (recording == nullptr) {
        (*state).failure = "no PDF context";
        (*state).close();
        return;
    }
    CGPDFContextBeginPage(recording, nullptr);
    NSGraphicsContext* const context = [NSGraphicsContext graphicsContextWithCGContext:recording flipped:NO];
    [view displayRectIgnoringOpacity:bounds inContext:context];
    // The page is still open: the PDF context may hold the drawn image.
    // Repaint in red and resize, which replaces the raster's storage.
    (*(*state).root).set_color(gui_forms::Color::rgba(220, 40, 40));
    [window setContentSize:NSMakeSize(bounds.size.width + 160.0, bounds.size.height + 120.0)];
    [view display];
    CGPDFContextEndPage(recording);
    CGPDFContextClose(recording);
    CGContextRelease(recording);
    const std::vector<std::uint8_t> centre = page_centre(pdf);
    std::cout << "recorded centre rgba " << static_cast<int>(centre[0]) << ' ' << static_cast<int>(centre[1]) << ' '
              << static_cast<int>(centre[2]) << ' ' << static_cast<int>(centre[3]) << '\n';
    const bool blue = centre[3] > 240U && centre[2] > 150U && centre[0] < 80U;
    if (!blue) (*state).failure = "the recorded page does not show the colour at recording time";
    (*state).completed = true;
    (*state).close();
}

void run_recorded_step(void* const context) {
    const std::unique_ptr<RecordedStep> retained(static_cast<RecordedStep*>(context));
    const std::shared_ptr<State>& state = (*retained).state;
    try {
        record(state);
    } catch (const std::exception& error) {
        (*state).failure = error.what();
        (*state).close();
    }
}

void ready(const std::shared_ptr<State>& state, std::function<void()>, std::function<void()> close,
           std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)>,
           std::function<gui_forms::HostClipboardTextResult()>,
           std::function<gui_forms::HostServiceStatus(std::string_view)>) {
    (*state).close = std::move(close);
    (*state).deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    queue_recorded_step(state, 50 * NSEC_PER_MSEC);
}

} // namespace

int main() {
    std::shared_ptr<State> state = std::make_shared<State>();
    (*state).root = gui_forms::make_control<RecordedRoot>(gui_forms::StableId("recorded-root"));
    gui_forms::host::MacHostOptions options;
    options.title = window_title;
    options.initial_size = {320, 200};
    options.print_metrics_on_close = false;
    options.host_ready = std::bind(ready, state, std::placeholders::_1, std::placeholders::_2,
                                   std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
    const int result = gui_forms::host::run_macos(
        std::make_unique<gui_forms::Window>((*state).root, options.initial_size), std::move(options));
    if (result != 0 || !(*state).completed || !(*state).failure.empty()) {
        std::cerr << (*state).failure << '\n';
        return 1;
    }
    std::cout << "a recorded draw keeps the pixels it drew after the raster changes\n";
    return 0;
}
