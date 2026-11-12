#import <AppKit/AppKit.h>

#include "prepared_text_test_support.hpp"
#include "../src/core/text/prepared/prepared_storage.hpp"
#include "gui_forms/platform/macos_host.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/prepared_text/layout/prepared_text_layout.hpp"
#include "gui_forms/prepared_text/service/prepared_text_service.hpp"
#include "gui_forms/prepared_text/types/prepared_text_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <cmath>
#include <functional>
#include <iostream>

// Existing private native diagnostics/occlusion hook, not a public API addition.
@interface NSView (PreparedHostProbe)
- (std::string)hostJSON;
- (void)collectDamage;
- (void)notifyOcclusion:(BOOL)occluded;
@end

namespace {
using namespace gui_forms;
using prepared_test::require;

class PreparedRoot final : public Control {
public:
    PreparedRoot() : Control(StableId("mac-prepared-root")) {}
    PreparedTextLayout layout{};
    Color background{255, 255, 255, 255};
    std::uint64_t paints{0};
    std::uint64_t prepared_paints{0};
protected:
    void on_paint(Painter& painter, const gui_forms::Rect bounds) override {
        ++paints;
        painter.fill_rect(bounds, background);
        if (!layout.empty()) {
            const PreparedTextPaintResult result = painter.draw_prepared_text(layout, layout.authority(), {12, 40}, {0, 0, 0, 255});
            // A stale layout (a replacement submitted, a resize) is refused with
            // core's typed refusal, which the host contains, as the Linux and
            // Windows fixtures do.
            if (result.status != PreparedTextStatus::success) {
                throw gui_forms::detail::PreparedTextPaintFailure(result.status);
            }
            ++prepared_paints;
        }
    }
};

enum class Stage { host_ready, first_ready, first_presented, replacement_ready, replacement_presented };
struct Capture final {
    NSInteger width{0};
    NSInteger height{0};
    std::vector<std::uint8_t> rgba{};
};
struct State final {
    PreparedTextService service{};
    EncodedFontLease fonts{};
    std::unique_ptr<PreparedTextSession> session{};
    std::shared_ptr<PreparedRoot> root{};
    gui_forms::Window* model{}; // Borrow only while the host run is alive.
    std::function<void()> close{};
    LayoutAuthority expected{};
    Stage stage{Stage::host_ready};
    std::chrono::steady_clock::time_point deadline{};
    std::uint64_t previous_presented{0};
    double scale{1.0};
    Capture first{};
    bool finished{false};
    bool host_closed{false};
    std::string failure{};
};
struct DeferredStep final {
    std::shared_ptr<State> state{};
};

NSWindow* find_window() {
    NSWindow* result = nil;
    NSArray<NSWindow*>* const windows = NSApp.windows;
    const NSUInteger count = windows.count;
    for (NSUInteger index = 0; index < count; ++index) {
        NSWindow* const candidate = [windows objectAtIndex:index];
        if ([candidate.title isEqualToString:@"GUI.Forms prepared native fixture"]) {
            result = candidate;
            break;
        }
    }
    return result;
}
void close_fixture(State& state) {
    // closed can clear the stored target synchronously. This local copy owns
    // the callable until this invocation has returned.
    const std::function<void()> request_close = state.close;
    if (request_close) request_close();
}
void set_stage(State& state, const Stage stage) {
    state.stage = stage;
    state.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
}
void run_step(void* const context);
void enqueue(const std::shared_ptr<State>& state) {
    std::unique_ptr<DeferredStep> owner = std::make_unique<DeferredStep>();
    (*owner).state = state;
    const std::int64_t delay_nanoseconds = 50'000'000;
    const dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, delay_nanoseconds);
    void* const context = owner.release();
    dispatch_after_f(when, dispatch_get_main_queue(), context, run_step);
}

Capture capture_occluded(State& state, NSView* const view) {
    [view notifyOcclusion:YES];
    const PaintLeaseSnapshot before = (*state.model).paint_lease_snapshot();
    const std::uint64_t paints = (*state.root).paints;
    require((*state.model).occluded(), "occlusion hook did not reach model");
    Capture result{};
    @autoreleasepool {
        NSBitmapImageRep* const bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        require(bitmap != nil, "AppKit bitmap allocation");
        // This explicit exposure occurs only after an independently observed
        // successful native frame; it cannot manufacture first-paint readiness.
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
        result.width = bitmap.pixelsWide;
        result.height = bitmap.pixelsHigh;
        require(result.width > 0 && result.height > 0 && result.width <= 2048 && result.height <= 2048,
            "bounded snapshot extent");
        const std::size_t count = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height);
        require(count <= 1024U * 1024U, "snapshot pixel cap");
        result.rgba.resize(count * 4U);
        NSColorSpace* const color_space = NSColorSpace.sRGBColorSpace;
        for (NSInteger row = 0; row < result.height; ++row) {
            const std::size_t offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(result.width) * 4U;
            for (NSInteger column = 0; column < result.width; ++column) {
                NSColor* const color = [[bitmap colorAtX:column y:row] colorUsingColorSpace:color_space];
                require(color != nil, "snapshot color conversion");
                const std::array<double, 4> components{static_cast<double>(color.redComponent),
                    static_cast<double>(color.greenComponent), static_cast<double>(color.blueComponent),
                    static_cast<double>(color.alphaComponent)};
                const std::size_t pixel = offset + static_cast<std::size_t>(column) * 4U;
                for (std::size_t channel = 0; channel < components.size(); ++channel) {
                    require(std::isfinite(components[channel]), "finite snapshot color");
                    const double bounded = std::clamp(components[channel], 0.0, 1.0);
                    const long value = std::lround(bounded * 255.0);
                    result.rgba[pixel + channel] = static_cast<std::uint8_t>(value);
                }
            }
        }
    }
    const PaintLeaseSnapshot after = (*state.model).paint_lease_snapshot();
    require((*state.root).paints == paints, "occluded exposure reran application painting");
    require(after.presented_revision == before.presented_revision &&
        after.presentation_receipts_accepted == before.presentation_receipts_accepted,
        "null-receipt exposure invented acknowledgement");
    [view notifyOcclusion:NO];
    return result;
}
bool same_capture(const Capture& first, const Capture& second) {
    const bool equal = first.width == second.width && first.height == second.height && first.rgba == second.rgba;
    return equal;
}
void require_ink(const Capture& capture) {
    std::size_t dark = 0;
    std::size_t opaque = 0;
    for (std::size_t offset = 0; offset < capture.rgba.size(); offset += 4U) {
        if (capture.rgba[offset + 3U] >= 250) {
            ++opaque;
            if (capture.rgba[offset] < 100 && capture.rgba[offset + 1U] < 100 && capture.rgba[offset + 2U] < 100) ++dark;
        }
    }
    require(dark >= 10 && opaque > dark * 4U, "opaque prepared glyph ink missing from native exposure");
}
void submit(State& state, NSWindow* const window, const std::string_view text) {
    state.scale = static_cast<double>(window.backingScaleFactor);
    require(std::isfinite(state.scale) && state.scale >= 0.5 && state.scale <= 4.0, "actual backing scale supported");
    const PreparedTextKey key = prepared_test::make_key(state.service, state.fonts, text, 20.0, state.scale);
    PreparedTextStatus status = (*state.session).desire(key, state.expected);
    require(status == PreparedTextStatus::success, "native fixture desire");
    PrepareInput input{};
    status = prepared_test::make_input(state.service, key, text, input);
    require(status == PreparedTextStatus::success, "native fixture input");
    status = (*state.session).submit(state.expected, input);
    require(status == PreparedTextStatus::success && input.empty(), "native fixture async submit");
}
bool adopt_ready(State& state, NSView* const view) {
    const PreparedTextSessionSnapshot ready = (*state.session).inspect_ready();
    if (ready.slot != PreparedTextSlot::ready) return false;
    require(ready.completion == PreparedTextStatus::success, "native fixture worker preparation");
    const PreparedTextStatus adopted = (*state.session).adopt_ready(state.expected, (*state.root).layout);
    require(adopted == PreparedTextStatus::success, "native fixture adoption");
    (*state.root).invalidate(Dirty::paint);
    [view collectDamage];
    return true;
}

void advance(State& state) {
    NSWindow* const window = find_window();
    if (state.stage == Stage::host_ready) {
        if (window == nil || !window.isVisible || (*state.root).paints == 0) return;
        const PaintLeaseSnapshot lease = (*state.model).paint_lease_snapshot();
        if (lease.presented_revision == 0) return;
        const MetricsSnapshot metrics = (*state.model).metrics_snapshot();
        require(metrics.renderer_name.find("bundled fonts (optional Unicode fallback)") != std::string::npos,
            "native host bundled fonts not ready");
        state.previous_presented = lease.presented_revision;
        submit(state, window, "Prepared a\xCC\x81 office");
        set_stage(state, Stage::first_ready);
        return;
    }
    require(window != nil && window.contentView != nil, "native fixture window disappeared");
    NSView* const view = window.contentView;
    require(static_cast<double>(window.backingScaleFactor) == state.scale, "backing scale changed during bounded fixture");
    if (state.stage == Stage::first_ready || state.stage == Stage::replacement_ready) {
        const bool ready = adopt_ready(state, view);
        if (!ready) return;
        const Stage next = state.stage == Stage::first_ready ? Stage::first_presented : Stage::replacement_presented;
        set_stage(state, next);
        return;
    }
    const PaintLeaseSnapshot lease = (*state.model).paint_lease_snapshot();
    if (state.stage == Stage::first_presented) {
        if (lease.presented_revision <= state.previous_presented || (*state.root).prepared_paints == 0) return;
        state.first = capture_occluded(state, view);
        require_ink(state.first);
        state.previous_presented = lease.presented_revision;
        // A throwing paint is no longer contained (owner, 2026-11-11: it
        // ends the process at the fail-fast boundary), so the fixture goes
        // straight to the replacement.
        (*state.root).background = {230, 255, 230, 255};
        [window setContentSize:NSMakeSize(360, 180)];
        submit(state, window, "Replacement prepared glyphs");
        set_stage(state, Stage::replacement_ready);
        return;
    }
    if (state.stage == Stage::replacement_presented) {
        if (lease.presented_revision <= state.previous_presented || view.bounds.size.width != 360 || view.bounds.size.height != 180) return;
        const Capture recovered = capture_occluded(state, view);
        require_ink(recovered);
        require(!same_capture(state.first, recovered), "replacement/resize did not change native image");
        require(recovered.width > state.first.width && recovered.height > state.first.height, "resized exposure kept old extent");
        std::cout << "observed_backing_scale=" << state.scale << " prepared_paints=" << (*state.root).prepared_paints
                  << " presented_revision=" << lease.presented_revision << '\n';
        state.finished = true;
        close_fixture(state);
    }
}
void run_step(void* const context) {
    const std::unique_ptr<DeferredStep> owner(static_cast<DeferredStep*>(context));
    const std::shared_ptr<State> state = (*owner).state;
    if ((*state).host_closed || (*state).finished) return;
    @try {
        try {
            require(std::chrono::steady_clock::now() < (*state).deadline, "bounded native prepared stage timed out");
            advance(*state);
            if (!(*state).finished && !(*state).host_closed) enqueue(state);
        } catch (const std::exception& error) {
            (*state).failure = error.what();
            (*state).finished = true;
            close_fixture(*state);
        }
    } @catch (NSException* exception) {
        NSString* const reason = exception.reason == nil ? @"unknown AppKit exception" : exception.reason;
        const char* const message = reason.UTF8String;
        (*state).failure = message == nullptr ? "AppKit exception" : message;
        (*state).finished = true;
        close_fixture(*state);
    }
}
struct Closed final {
    std::shared_ptr<State> state{};
    void operator()() const {
        (*state).host_closed = true;
        (*state).model = nullptr;
        (*state).close = {};
    }
};
struct Ready final {
    std::shared_ptr<State> state{};
    void operator()(std::function<void()>, std::function<void()> close,
        std::function<HostDialogResult(const HostDialogRequest&)>,
        std::function<HostClipboardTextResult()>, std::function<HostServiceStatus(std::string_view)>) const {
        (*state).close = std::move(close);
        set_stage(*state, Stage::host_ready);
        enqueue(state);
    }
};
}

int main(const int argc, char** const argv) {
    try {
        require(argc == 2, "encoded font directory argument");
        const std::vector<std::byte> bytes = prepared_test::read_font(argv[1]);
        const std::shared_ptr<State> state = std::make_shared<State>();
        (*state).fonts = prepared_test::make_bank((*state).service, bytes);
        const PreparedTextStatus opened = (*state).service.open_session((*state).fonts, nullptr, (*state).session);
        require(opened == PreparedTextStatus::success, "Mac prepared session");
        (*state).root = std::make_shared<PreparedRoot>();
        gui_forms::host::MacHostOptions options{};
        options.title = "GUI.Forms prepared native fixture";
        options.initial_size = {280, 120};
        options.minimum_size = {100, 80};
        options.print_metrics_on_close = false;
        options.host_ready = Ready{state};
        options.closed = Closed{state};
        std::unique_ptr<gui_forms::Window> model = std::make_unique<gui_forms::Window>((*state).root, options.initial_size);
        (*state).model = model.get();
        const int result = gui_forms::host::run_macos(std::move(model), std::move(options));
        (*state).host_closed = true;
        (*state).model = nullptr;
        (*state).close = {};
        (*(*state).session).begin_close();
        (*(*state).session).join_and_release();
        require(result == 0 && (*state).finished && (*state).failure.empty(),
            (*state).failure.empty() ? "native fixture did not complete" : (*state).failure.c_str());
        std::cout << "Mac prepared host: ink, callback rollback, resize recovery and null-receipt exposure passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
