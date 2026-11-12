// GUIFormsView (Scheduling): damage collection, the display link that
// presents live surfaces, posted work and the wake timer.

#include "macos_view.hpp"
#include "../application/macos_values.hpp"

// CADisplayLink retains its target. This proxy observes the view weakly, so a
// paused link cannot keep an otherwise closed window's view alive.
@interface GUIFormsDisplayTarget : NSObject {
    __weak GUIFormsView* _view;
}
- (instancetype)initWithView:(GUIFormsView*)view;
- (void)tick:(CADisplayLink*)sender;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerTick:(CADisplayLink*)sender;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsDisplayTarget's native entries: each sends the entry's answer message.
void run_display_target_tick(GUIFormsDisplayTarget* object, CADisplayLink* sender) {
    [object answerTick:sender];
}
} // namespace

@implementation GUIFormsDisplayTarget
- (instancetype)initWithView:(GUIFormsView*)view {
    self = [super init];
    if (self != nil) _view = view;
    return self;
}
- (void)tick:(CADisplayLink*)sender {
    gui_forms::detail::call_failing_fast("macos.display_target.tick:", &run_display_target_tick, self, sender);
}

- (void)answerTick:(CADisplayLink*)sender {
    GUIFormsView* view = _view;
    if (view == nil) {
        [sender invalidate];
        return;
    }
    [view displayLinkTick];
}
@end

@implementation GUIFormsView (Scheduling)

// Takes the scroll copies the model recorded with the damage just taken.
// The damage this view still holds moves with each copy's pixels; the copy's
// clip is asked of AppKit, which copies the raster there after the draw.
- (void)takeScrollCopies {
    _takenCopies.clear();
    (*_model).take_scroll_copies(_takenCopies);
    for (const gui_forms::ScrollCopy& copy : _takenCopies) {
        if (!_pendingDamage.empty()) {
            _pendingDamage = gui_forms::damage_after_scroll_copy(_pendingDamage, copy.clip, copy.offset);
        }
        _pendingCopies.push_back(copy);
        [self setNeedsDisplayInRect:NSMakeRect(copy.clip.x, copy.clip.y, copy.clip.width, copy.clip.height)];
    }
    _takenCopies.clear();
}

- (void)collectDamage {
    std::optional<MacCpuScope> cpuScope{};
    if (_cpuAttribution.enabled) cpuScope.emplace(_cpuAttribution, 2U);
    ++_damageCollectionCount;
    // Housekeeping: nothing is caught. The native entries that call it
    // (the wake, the display link, posted work, drawing) are the fail-fast
    // boundary.
    if (!_model) {
        return;
    }
    const gui_forms::FrameTime pollTime = std::chrono::steady_clock::now();
    const gui_forms::FramePollResult pollResult = (*_model).poll_frame_schedule(pollTime);
    if (_causalTrace && (*_causalTrace).enabled) {
        (*_causalTrace).record_poll(pollTime, pollResult.deadlines_fired, pollResult.next_wake);
    }
    const std::uint64_t semanticGeneration = (*_model).semantic_generation();
    if (semanticGeneration != _lastSemanticGeneration) {
        _lastSemanticGeneration = semanticGeneration;
        _semanticAccessibilityChildren = nil;
        _accessibilityCacheKey = MacAccessibilityTreeKey{};
        // Republishing posts a changed live region's announcement, and
        // a text node's changed value or selection, now.
        if (_publishesLiveRegion == YES || _publishesTextRanges == YES) {
            static_cast<void>([self accessibilityChildren]);
        }
        NSAccessibilityPostNotification(
            self, NSAccessibilityLayoutChangedNotification);
    }
    DamageRegion damage = (*_model).take_damage();
    const double scale =
        self.window == nil ? 1.0 : self.window.backingScaleFactor;
    [self takeScrollCopies];
    for (GFRect rect : damage.rectangles()) {
        rect = gui_forms::detail::align_damage_outward(rect, scale);
        _pendingDamage.add(rect);
        [self setNeedsDisplayInRect:
            NSMakeRect(rect.x, rect.y, rect.width, rect.height)];
    }
    [self startDisplayLinkIfNeeded];
    [self armWakeTimer];
}

- (void)startDisplayLinkIfNeeded {
    if (!_model || self.window == nil || _hostOccluded == YES ||
        !(*_model).has_live_surface_presentations()) return;
    if (_displayLink == nil) {
        GUIFormsDisplayTarget* target = [[GUIFormsDisplayTarget alloc] initWithView:self];
        _displayLink = [self displayLinkWithTarget:target selector:@selector(tick:)];
        [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    }
    (*_model).set_live_surface_idle_waiting(false);
    _displayLink.paused = NO;
}

- (void)stopDisplayLink {
    _displayLink.paused = YES;
    if (_model) (*_model).set_live_surface_idle_waiting(false);
}

- (void)setDisplayTraceEnabled:(BOOL)enabled {
    _displayTrace.reset();
    if (enabled) _displayTrace = std::make_unique<MacDisplayLinkTrace>();
}

- (MacDisplayLinkTrace)displayTraceSnapshot {
    if (_displayTrace) return *_displayTrace;
    return MacDisplayLinkTrace{};
}

- (void)answerDisplayLinkTick {
    if (_displayTrace) {
        MacDisplayLinkTrace& trace = *_displayTrace;
        if (trace.count < trace.timestamps.size()) {
            const std::chrono::steady_clock::duration time =
                std::chrono::steady_clock::now().time_since_epoch();
            const std::chrono::nanoseconds nanoseconds =
                std::chrono::duration_cast<std::chrono::nanoseconds>(time);
            trace.timestamps[trace.count] = static_cast<std::uint64_t>(nanoseconds.count());
            ++trace.count;
        } else trace.overflow = true;
    }
    ++_displayTickCount;
    if (!_model || self.window == nil || _hostOccluded == YES) {
        [self stopDisplayLink];
        return;
    }
    // A failed batch needs a fresh composition, including unchanged surfaces.
    // Reusing cached clips could cover a newly opened overlay or hidden control.
    const bool retry = !_pendingLivePresentations.empty();
    bool publicationChanged = false;
    std::vector<LiveSurfacePresentation> updates =
        (*_model).take_live_surface_presentations(retry, &publicationChanged);
    _pendingLivePresentations = std::move(updates);
    if (!(*_model).has_live_surface_presentations()) {
        [self stopDisplayLink];
        return;
    }
    if (_pendingLivePresentations.empty()) {
        // Publication posts CollectDamageWake, which restarts this paused link.
        // Arm after pausing, then recheck generations to avoid a lost wake.
        [self stopDisplayLink];
        (*_model).set_live_surface_idle_waiting(true);
        return;
    }
    const double scale = self.window.backingScaleFactor;
    // When the raster still holds the previous presentation, ask AppKit to
    // redraw only where the surface actually changed. Redrawing the whole
    // clip left every overlay inside it as uncovered damage, so a floating
    // control was retained-painted on each publication however far away
    // the change was. drawRetainedRect: re-requests any part of the copied
    // region the flush did not cover, so a narrower request never leaves
    // stale pixels presented.
    const bool rasterReusable = _rasterHasContent == YES && _liveRegionLost == NO &&
        (*_model).paint_lease_snapshot().state == gui_forms::PaintLeaseState::clean;
    for (const LiveSurfacePresentation& update : _pendingLivePresentations) {
        GFRect request = update.clip;
        if (rasterReusable && update.damage_limited && update.surface) {
            const gui_forms::LiveSurfaceFrame latest = (*update.surface).acquire_latest();
            if (latest && latest.opaque() && !latest.damage().empty()) {
                const GFRect changed = gui_forms::detail::live_damage_in_window(
                    latest.damage(), update.destination, latest.width(), latest.height(), scale);
                const GFRect narrowed = GFRect::intersection(update.clip, changed);
                if (!narrowed.empty()) request = narrowed;
            }
        }
        const GFRect aligned = gui_forms::detail::align_damage_outward(request, scale);
        [self setNeedsDisplayInRect:
            NSMakeRect(aligned.x, aligned.y, aligned.width, aligned.height)];
    }
    // Where a new frame changed pixels beneath an overlay, the model now
    // holds that share as damage (take_live_surface_presentations). Take
    // it into this flush so the overlay is retained-painted over the new
    // frame together with the live copy, not one wake later.
    const DamageRegion beneathOverlays = (*_model).take_damage();
    [self takeScrollCopies];
    for (GFRect rect : beneathOverlays.rectangles()) {
        rect = gui_forms::detail::align_damage_outward(rect, scale);
        _pendingDamage.add(rect);
        [self setNeedsDisplayInRect:
            NSMakeRect(rect.x, rect.y, rect.width, rect.height)];
    }
    // AppKit calls on the main run loop, following this view's screen. Keep
    // its default variable refresh policy; sample only the newest frame.
    [self displayIfNeeded];
    if (!publicationChanged && _pendingLivePresentations.empty()) {
        // Unchanged surfaces may still need one composition for an overlay
        // edit. Once presented, a static overlay must not keep us ticking.
        [self stopDisplayLink];
        (*_model).set_live_surface_idle_waiting(true);
    }
}

- (void)answerDrainPostedWork {
    if (!_model) return;
    static_cast<void>((*_model).drain_posted_work());
    [self collectDamage];
}

- (void)armWakeTimer {
    if (!_model || _wakeSource == nil) {
        return;
    }
    const std::optional<gui_forms::FrameTime> wake = (*_model).next_wake();
    if (!wake) {
        if (_causalTrace && (*_causalTrace).enabled) {
            (*_causalTrace).record_arm(gui_forms::FrameClock::now(), wake, 0);
        }
        dispatch_source_set_timer(_wakeSource, DISPATCH_TIME_FOREVER,
                                  DISPATCH_TIME_FOREVER, 0);
        return;
    }
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    // Nanosecond precision, never rounded down (audit N107): a deadline
    // less than a millisecond away used to fire early and be armed again.
    // A deadline already past fires at once.
    const std::chrono::nanoseconds remaining = std::chrono::ceil<std::chrono::nanoseconds>(*wake - now);
    const std::chrono::nanoseconds::rep nanoseconds = std::max<std::chrono::nanoseconds::rep>(remaining.count(), 0);
    dispatch_source_set_timer(
        _wakeSource,
        dispatch_time(DISPATCH_TIME_NOW, static_cast<std::int64_t>(nanoseconds)),
        DISPATCH_TIME_FOREVER,
        static_cast<std::uint64_t>(std::chrono::microseconds(250).count() * 1000));
    if (_causalTrace && (*_causalTrace).enabled) {
        (*_causalTrace).record_arm(now, wake, static_cast<std::int64_t>(nanoseconds));
    }
}

@end

// This unit's link anchor (macos_view.hpp): referenced from
// link_view_categories in macos_view.mm so that the archive member holding
// the Scheduling category is linked wherever GUIFormsView is.
namespace gui_forms::host::macos {
void gui_forms_macos_view_scheduling_anchor() noexcept {}
} // namespace gui_forms::host::macos
