// GUIFormsView (Scheduling): damage collection, the display link (the
// window's frame source: it reports the display's timing, polls the frames
// paced to it and presents live surfaces), posted work and the wake timer.

#include "macos_view.hpp"
#include "../application/macos_main_work.hpp"
#include "../application/macos_values.hpp"

#include <optional>

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
// The first-frame show, posted (postFirstShow): the view may be gone by
// the time the work runs. The function adopts and deletes the context.
struct FirstShowWork final {
    __weak GUIFormsView* view;
};

void run_perform_due_first_show(GUIFormsView* view) {
    [view performDueFirstShow];
}

void perform_first_show_on_main(void* context) {
    const std::unique_ptr<FirstShowWork> work(static_cast<FirstShowWork*>(context));
    GUIFormsView* const view = (*work).view;
    if (view == nil) return;
    gui_forms::detail::call_failing_fast("macos.first_show", &run_perform_due_first_show, view);
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
    // The first-frame show's deadline rides on this view's wake
    // (armWakeTimer). Its show is posted, never performed here.
    if (_firstShow.waiting()) {
        const gui_forms::FirstFrameShowStep firstShowStep = _firstShow.poll(pollTime);
        if (firstShowStep.show) [self postFirstShow];
    }
    const gui_forms::FramePollResult pollResult = (*_model).poll_frame_schedule(pollTime);
    if (_causalTrace && (*_causalTrace).enabled) {
        (*_causalTrace).record_poll(pollTime, pollResult.deadlines_fired, pollResult.next_wake);
    }
    const std::uint64_t semanticGeneration = (*_model).semantic_generation();
    const gui_forms::Control::Ptr focusedControl = (*_model).focused_control();
    const std::uint64_t focusedRuntimeId = focusedControl ? (*focusedControl).runtime_id().value : 0U;
    const bool focusMoved = focusedRuntimeId != _lastFocusedRuntimeId;
    _lastFocusedRuntimeId = focusedRuntimeId;
    if (semanticGeneration != _lastSemanticGeneration || focusMoved) {
        _lastSemanticGeneration = semanticGeneration;
        _semanticAccessibilityChildren = nil;
        _accessibilityCacheKey = MacAccessibilityTreeKey{};
        [self postAccessibilityNotification:NSAccessibilityLayoutChangedNotification element:self];
        // Once a client has read the tree, republishing at once posts what
        // changed (postAccessibilityChanges:): keyboard focus moving
        // (FocusedUIElementChanged on the newly focused element; the
        // focused control's identity is compared as well as the semantic
        // generation, so a move that does not invalidate semantics is still
        // published), selection, value and expansion changes, a changed
        // live region's announcement and a text node's changed value or
        // selection. Before any client has read it, nothing is built.
        if ([self semanticTreePublished] == YES) {
            static_cast<void>([self accessibilityChildren]);
        }
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
    // While the first show waits, the window is off screen, where AppKit
    // displays nothing on its own: the damage just requested is drawn and
    // presented now. A present there leaves the show due; it is posted.
    if (_firstShow.waiting()) {
        [self displayAwaitingFirstShow];
        if (_firstShowDue == YES) [self postFirstShow];
    }
}

// The link runs while the model wants display-paced frames (motion, a
// running animation timer: SurfaceRoot::wants_display_frames) or has live
// presentations; a request made outside a tick reaches here through the
// schedule wake (CollectDamageWake), so a paused link is never left behind
// by new motion.
- (void)startDisplayLinkIfNeeded {
    if (!_model || self.window == nil || _hostOccluded == YES) return;
    const bool wanted = (*_model).has_live_surface_presentations() || (*_model).wants_display_frames();
    if (!wanted) return;
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

// The live-surface path is done with the link: it pauses unless the model
// still wants display-paced frames, which keep it running (it no longer
// stops itself under running motion). Either way the live surfaces wait
// as stopDisplayLink leaves them; the caller then marks them idle.
- (void)restDisplayLink {
    if (_model && (*_model).wants_display_frames()) {
        (*_model).set_live_surface_idle_waiting(false);
        return;
    }
    [self stopDisplayLink];
}

// Reports the display's timing from the link's tick (design note
// frame-source, section 3): the refresh period is this frame's interval,
// targetTimestamp - timestamp (on a variable-rate display, the rate the
// system chose), and the phase is targetTimestamp, the time the frame
// drawn now is shown. CADisplayLink times are CACurrentMediaTime seconds;
// the target's distance from now carries it over to the steady clock. A
// tick without a usable interval reports nothing.
- (void)reportDisplayTiming {
    const CFTimeInterval target = _displayLink.targetTimestamp;
    const CFTimeInterval period = target - _displayLink.timestamp;
    if (!(target > 0.0) || !(period > 0.0)) return;
    const CFTimeInterval ahead = target - CACurrentMediaTime();
    const gui_forms::FrameTime now = gui_forms::FrameClock::now();
    const gui_forms::FrameTime presentation =
        now + std::chrono::duration_cast<gui_forms::FrameInterval>(std::chrono::duration<double>(ahead));
    const gui_forms::FrameInterval refresh =
        std::chrono::duration_cast<gui_forms::FrameInterval>(std::chrono::duration<double>(period));
    const gui_forms::DisplayTiming timing =
        gui_forms::DisplayTiming::reported(gui_forms::DisplayTimingSource::display, refresh, presentation);
    // A period outside the model's bounds (a link's first tick) is
    // refused and changes nothing.
    static_cast<void>((*_model).set_display_timing(timing));
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
    // The frame source: the display's timing first, then, while frames
    // are wanted, the poll that advances them (collectDamage polls at
    // now, just past this refresh, so the frame due on it fires and is
    // drawn for targetTimestamp). The wake timer stays armed as well; a
    // poll that finds nothing due does nothing.
    [self reportDisplayTiming];
    if ((*_model).wants_display_frames()) [self collectDamage];
    // No live surface: the frames alone keep the link, which pauses on
    // the first tick that finds none wanted.
    if (!(*_model).has_live_surface_presentations()) {
        [self restDisplayLink];
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
        [self restDisplayLink];
        return;
    }
    if (_pendingLivePresentations.empty()) {
        // Publication posts CollectDamageWake, which restarts this paused link.
        // Arm after pausing, then recheck generations to avoid a lost wake.
        [self restDisplayLink];
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
        [self restDisplayLink];
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
    stxlib::Optional<gui_forms::FrameTime> wake = (*_model).next_wake();
    // A waiting first show's deadline is one more wake (first_frame_show.hpp).
    const stxlib::Optional<gui_forms::FrameTime> firstShowDeadline = _firstShow.deadline();
    if (firstShowDeadline.present() &&
        (wake.absent() || *firstShowDeadline.value() < *wake.value())) {
        wake = firstShowDeadline;
    }
    if (wake.absent()) {
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
    const std::chrono::nanoseconds remaining = std::chrono::ceil<std::chrono::nanoseconds>(*wake.value() - now);
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

- (void)setFirstFrameShowCeiling:(gui_forms::FrameInterval)ceiling {
    _firstShow.set_ceiling(ceiling);
}

// Order of events (first_frame_show.hpp): the open path has run the init
// turn and collected damage; here the model is told the window is visible
// (it paints only then), the frame is drawn and presented off screen, and
// the window is ordered front, made key and the application activated as
// the request asked. Activation and focus reach the model afterwards
// through windowDidBecomeKey:, and AppKit publishes the window to
// accessibility clients when it is on screen.
- (void)requestFirstShowTakingKey:(BOOL)takesKey
                       activating:(BOOL)activates
                           parent:(NSWindow*)parent {
    _firstShowTakesKey = takesKey;
    _firstShowActivates = activates;
    _firstShowParent = parent;
    const gui_forms::FirstFrameShowStep step = _firstShow.request_show(gui_forms::FrameClock::now());
    if (step.show) {
        _firstShowDue = YES;
        [self performDueFirstShow];
        return;
    }
    if (!_firstShow.waiting()) return;
    // The window is about to be shown: the model paints although the
    // window is off screen (initializeHost reported it occluded, and an
    // occluded model declines to paint). Unoccluding collects damage, which
    // draws the hidden window while the gate waits.
    if (_hostOccluded == YES) [self notifyOcclusion:NO];
    else [self collectDamage];
    [self displayAwaitingFirstShow];
    if (_firstShowDue == YES) {
        [self performDueFirstShow];
        return;
    }
    // No frame yet: the deadline is armed with the view's wake.
    [self armWakeTimer];
}

- (void)cancelFirstShow {
    _firstShowDue = NO;
    if (!_firstShow.cancel()) return;
    // The window stays off screen: the model is occluded again, as
    // initializeHost left it, until a show unoccludes it.
    if (_hostOccluded == NO) [self notifyOcclusion:YES];
    [self armWakeTimer];
}

- (void)noteFramePresentedForFirstShow {
    const gui_forms::FirstFrameShowStep step =
        _firstShow.report_frame_presented(gui_forms::FrameClock::now());
    if (!step.show) return;
    _firstShowDue = YES;
    // Inside the view's own display of the hidden window, its caller
    // performs the show once the draw returns; inside AppKit's display
    // cycle the show is posted, so no window is ordered from inside a draw.
    if (_firstShowDisplaying == NO) [self postFirstShow];
}

- (void)performDueFirstShow {
    if (_firstShowDue == NO) return;
    _firstShowDue = NO;
    NSWindow* const window = self.window;
    if (window == nil || _hostAttached == NO || [self acceptsWindowRequest] == NO) return;
    NSWindow* const parent = _firstShowParent;
    // addChildWindow:ordered: orders the child on screen, so the relation
    // is made only now (the open path detached it to keep the window off
    // screen), and again in case ordering out detached it.
    if (parent != nil && window.parentWindow == nil) {
        [parent addChildWindow:window ordered:NSWindowAbove];
    }
    if (_firstShowTakesKey == YES) [window makeKeyAndOrderFront:nil];
    else [window orderFront:nil];
    if (_firstShowActivates == YES) [NSApp activateIgnoringOtherApps:YES];
}

- (gui_forms::FirstFrameShowSnapshot)firstShowSnapshot {
    const gui_forms::FirstFrameShowSnapshot snapshot = _firstShow.snapshot();
    return snapshot;
}

- (void)displayAwaitingFirstShow {
    if (_firstShowDisplaying == YES || self.window == nil) return;
    _firstShowDisplaying = YES;
    [self displayIfNeeded];
    _firstShowDisplaying = NO;
}

- (void)postFirstShow {
    _firstShowDue = YES;
    FirstShowWork* const work = new FirstShowWork{self};
    post_main_work(work, &perform_first_show_on_main);
}

@end

// This unit's link anchor (macos_view.hpp): referenced from
// link_view_categories in macos_view.mm so that the archive member holding
// the Scheduling category is linked wherever GUIFormsView is.
namespace gui_forms::host::macos {
void gui_forms_macos_view_scheduling_anchor() noexcept {}
} // namespace gui_forms::host::macos
