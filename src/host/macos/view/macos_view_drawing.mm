// GUIFormsView (Drawing): the retained draw AppKit asks for in drawRect:
// and its presentation, the live surfaces' presentation reports, the
// invalidation overlay and frame capture.
//
// drawRetainedRect: is the main sequence. It runs five stages in order, each
// a method below: prepareRasterForDraw: sizes the raster to the view;
// decideDrawReuse: decides what the raster still holds and what this draw
// owes (MacDrawPlan); paintFrame:started: paints the owed damage and the live
// frames into the raster; presentFrame:... copies the raster to AppKit's
// context and settles what the presentation validated; recordDrawMetrics:...
// reports what was copied (stage DP).

#include "macos_view.hpp"
#include "../application/macos_main_work.hpp"
#include "../application/macos_values.hpp"
#if defined(GUI_FORMS_HAVE_METAL_PRESENT)
#include "macos_metal_present.hpp"
#endif

// NSTimer retains its target: this proxy observes the view weakly, so the
// invalidation overlay's fade timer cannot keep a closed window's view
// alive, and a tick after the view is gone stops the timer.
@interface GUIFormsOverlayTimerTarget : NSObject {
    __weak GUIFormsView* _view;
}
- (instancetype)initWithView:(GUIFormsView*)view;
- (void)tick:(NSTimer*)sender;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerTick:(NSTimer*)sender;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsOverlayTimerTarget's native entries: each sends the entry's answer message.
void run_overlay_timer_tick(GUIFormsOverlayTimerTarget* object, NSTimer* sender) {
    [object answerTick:sender];
}
} // namespace

@implementation GUIFormsOverlayTimerTarget
- (instancetype)initWithView:(GUIFormsView*)view {
    self = [super init];
    if (self != nil) _view = view;
    return self;
}
- (void)tick:(NSTimer*)sender {
    gui_forms::detail::call_failing_fast("macos.overlay_timer.tick:", &run_overlay_timer_tick, self, sender);
}

- (void)answerTick:(NSTimer*)sender {
    GUIFormsView* view = _view;
    if (view == nil) {
        [sender invalidate];
        return;
    }
    [view invalidationOverlayTick];
}
@end

namespace gui_forms::host::macos {

// The view's extent for one draw (prepareRasterForDraw:).
struct MacDrawExtent final {
    double scale{1.0};
    GFSize logical_size{};
    // AppKit's dirty rectangle, in view coordinates.
    GFRect native_damage{};
};

// What one draw reuses and owes (decideDrawReuse:), read by the stages
// after it. The live presentations' frames, regions and the damage the draw
// paints are the view's own storage (_liveFrames, _liveRegions,
// _pendingDamage); this record carries the decisions about them.
struct MacDrawPlan final {
    MacDrawExtent extent{};
    // The raster still holds the last presentation and the window is
    // visible: AppKit's rectangles are copies outside the owed damage.
    bool reusable_raster{false};
    // Nothing owed and no live frame: the draw paints nothing and copies the
    // raster as it stands.
    bool copy_only{false};
    // Nothing retained is owed: only live frames change pixels.
    bool live_only{false};
    // A live region was narrowed to its frame's damage.
    bool narrowed_any{false};
    // A presentation region was taken from a live surface.
    bool regions_taken{false};
    // The rectangles AppKit is drawing (getRectsBeingDrawn:count:). AppKit
    // owns them; they stay valid until drawRect: returns.
    const NSRect* drawn_rects{nullptr};
    NSInteger drawn_count{0};
#if defined(GUI_FORMS_PREPARED_TEXT)
    // The receipt of the front the raster presented before this draw.
    PaintReceipt previous_receipt{};
#endif
};

// What paintFrame:started: left for presentation.
struct MacPaintedFrame final {
    // The model's receipt for the retained paint; none when nothing retained
    // was painted, the frame was not begun or the candidate was aborted.
    std::optional<PaintReceipt> receipt{};
    bool live_draws_succeeded{true};
#if defined(GUI_FORMS_PREPARED_TEXT)
    // The prepared candidate became the front.
    bool committed{false};
#endif
};

// What presentFrame:... copied (stage DP).
struct MacPresentedFrame final {
    bool presented{false};
    // Presented with its live frames committed: each surface shown has been
    // told the generation it showed.
    bool live_presented{false};
    // The CGContextDrawImage copy.
    std::uint64_t copy_nanoseconds{0};
    // From the start of drawRetainedRect: to the end of the copy.
    std::uint64_t elapsed_nanoseconds{0};
};

} // namespace gui_forms::host::macos

using gui_forms::host::macos::MacDrawExtent;
using gui_forms::host::macos::MacDrawPlan;
using gui_forms::host::macos::MacPaintedFrame;
using gui_forms::host::macos::MacPresentedFrame;

@implementation GUIFormsView (Drawing)

- (void)drawRetainedRect:(NSRect)dirtyRect {
    if (!_model) {
        return;
    }
    std::optional<MacCpuScope> cpuScope{};
    if (_cpuAttribution.enabled) cpuScope.emplace(_cpuAttribution, 0U);
    const std::chrono::steady_clock::time_point started =
        std::chrono::steady_clock::now();
    if (_causalTrace && (*_causalTrace).enabled) (*_causalTrace).record_draw_entry(started);
    const MacDrawExtent extent = [self prepareRasterForDraw:dirtyRect];
    const MacDrawPlan plan = [self decideDrawReuse:extent];
    const MacPaintedFrame painted = [self paintFrame:plan started:started];
    const MacPresentedFrame presentation = [self presentFrame:dirtyRect
                                                         plan:plan
                                                      painted:painted
                                                      started:started];
    [self recordDrawMetrics:plan painted:painted presentation:presentation];
    if (_drawTrace) {
        [self recordDrawTrace:dirtyRect
                      started:started
                 presentation:presentation.presented ? YES : NO
                     duration:presentation.elapsed_nanoseconds];
    }
    [self completeFrameCaptureIfDue:dirtyRect
                          presented:(presentation.presented && painted.receipt) ? YES : NO];
    // Do not collect the next model invalidation from inside AppKit's paint
    // transaction. setNeedsDisplayInRect: may leave the view dirty without
    // enqueueing a second draw when invoked synchronously by drawRect:, which
    // lets animation callbacks advance while the last raster remains frozen.
    // The retained deadline source owns the next poll; arm it after presenting
    // and collect damage from its main-queue callback.
    [self armWakeTimer];
    if (_cpuAttribution.enabled) {
        const std::array<double, 4> extents{
            _lastNativeDirtyArea, _lastFrameDamageBoundsArea,
            _lastCGDestinationArea, _lastCGClipBoundsArea};
        const std::uint64_t wallNs = host_now_nanoseconds();
        _cpuAttribution.record_draw(wallNs, extents, _lastCGSourceBytes);
    }
    if (_causalTrace && (*_causalTrace).enabled) {
        (*_causalTrace).record_draw_exit(gui_forms::FrameClock::now());
    }
}

// The view's extent for this draw. Clears the last draw's extents, and
// resizes the ordinary raster to the view.
- (MacDrawExtent)prepareRasterForDraw:(NSRect)dirtyRect {
    MacDrawExtent extent{};
    extent.scale = self.window == nil ? 1.0 : self.window.backingScaleFactor;
    extent.logical_size = GFSize{self.bounds.size.width, self.bounds.size.height};
    _lastNativeDirtyArea = std::max(0.0, dirtyRect.size.width) *
        std::max(0.0, dirtyRect.size.height);
    _lastFrameDamageBoundsArea = 0.0;
    _lastCGDestinationArea = 0.0;
    _lastCGClipBoundsArea = 0.0;
    _lastCGSourceBytes = 0U;
#if !defined(GUI_FORMS_PREPARED_TEXT)
    // At an unchanged scale the raster keeps its pixels where the old and
    // new extents overlap, and the window damaged what the resize exposed
    // (Window::resize); otherwise it starts clear and is painted whole.
    if (_raster.resize(extent.logical_size, extent.scale) && !_raster.carried_on_resize()) {
        _rasterHasContent = NO;
        _pendingDamage.add(GFRect{0.0, 0.0, extent.logical_size.width, extent.logical_size.height});
    }
#endif
    extent.native_damage = GFRect{dirtyRect.origin.x, dirtyRect.origin.y,
                                  dirtyRect.size.width, dirtyRect.size.height};
    return extent;
}

// Decides what the raster still holds and what this draw owes: drops scroll
// copies a raster that lost its presentation cannot apply, acquires the live
// frames and their regions (_liveFrames, _liveFrameSources, _liveRegions,
// _opaqueLiveClips), and gathers the retained damage into _pendingDamage.
- (MacDrawPlan)decideDrawReuse:(const MacDrawExtent&)extent {
    const double scale = extent.scale;
    const GFSize logicalSize = extent.logical_size;
    const GFRect nativeDamage = extent.native_damage;
    MacDrawPlan plan{};
    plan.extent = extent;
    const gui_forms::PaintLeaseSnapshot drawLease = (*_model).paint_lease_snapshot();
    gui_forms::detail::FrontReuse reuse{};
    reuse.raster_has_content = _rasterHasContent == YES;
#if defined(GUI_FORMS_PREPARED_TEXT)
    reuse.front_matches_extent = _raster.prepared_front_matches(logicalSize, scale);
    plan.previous_receipt = _raster.prepared_front_receipt();
#else
    // The ordinary raster was resized to the view above (keeping the pixels
    // it carried, if any).
    static_cast<void>(logicalSize);
    reuse.front_matches_extent = true;
#endif
    reuse.front_surface_epoch = _frontSurfaceEpoch;
    reuse.surface_epoch = drawLease.surface_epoch;
    reuse.lease_clean = drawLease.state == gui_forms::PaintLeaseState::clean;
    reuse.live_region_lost = _liveRegionLost == YES;
    // The raster still holds the last presentation, at this size and scale,
    // in the model's current surface epoch.
    const bool frontHeld = gui_forms::detail::front_held(reuse);
    const bool reusableRaster = frontHeld && _hostOccluded == NO;
    // Scroll copies move pixels of a raster that still holds the last
    // presentation; otherwise their clips are repainted.
    if (!reusableRaster && !_pendingCopies.empty()) {
        for (const gui_forms::ScrollCopy& copy : _pendingCopies) _pendingDamage.add(copy.clip);
        _pendingCopies.clear();
    }
    const bool copiesPending = !_pendingCopies.empty();
    // While hidden the model declines to paint. AppKit's rectangles then ask
    // for a copy (an exposure or a snapshot); kept as retained damage, they
    // would be repainted by the first visible frame. Live frames still
    // compose while hidden, so a pending one keeps the ordinary path.
    const bool occludedCopy = frontHeld && _hostOccluded == YES && _pendingLivePresentations.empty();
    // Copying only a presentation region needs a raster that still holds the
    // previous presentation of that placement.
    // A scroll copy can move a live surface's previous pixels: copy whole clips.
    const bool regionsUsable = reusableRaster && _liveRegionLost == NO && !copiesPending;
    _liveFrames.clear();
    _opaqueLiveClips.clear();
    _liveFrameSources.clear();
    _liveRegions.clear();
    _liveFrames.reserve(_pendingLivePresentations.size());
    _opaqueLiveClips.reserve(_pendingLivePresentations.size());
    _liveFrameSources.reserve(_pendingLivePresentations.size());
    _liveRegions.reserve(_pendingLivePresentations.size());
    bool regionsTaken{false};
    bool narrowedAny{false};
    for (std::size_t index = 0; index < _pendingLivePresentations.size(); ++index) {
        const LiveSurfacePresentation& update = _pendingLivePresentations[index];
        // Overlay fragments of one surface share one frame and its one region.
        std::size_t source = index;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (_pendingLivePresentations[earlier].surface == update.surface) {
                source = _liveFrameSources[earlier];
                break;
            }
        }
        gui_forms::LiveSurfaceFrame frame{};
        if (source == index && update.surface) {
            // Takes the frame and clears its invalid region in one step.
            frame = (*update.surface).acquire_for_presentation((__bridge const void*)self);
            if (frame) regionsTaken = true;
        }
        const gui_forms::LiveSurfaceFrame& drawn = source == index ? frame : _liveFrames[source];
        GFRect region = update.clip;
        if (regionsUsable && update.damage_limited && drawn && drawn.opaque()) {
            // Outside the region the frame equals what this raster holds.
            const GFRect changed = gui_forms::detail::live_damage_in_window(
                drawn.damage(), update.destination, drawn.width(), drawn.height(), scale);
            region = GFRect::intersection(update.clip, changed);
            narrowedAny = true;
            // The display link requested the damage it saw; a publication that
            // landed since can reach outside this flush. The raster receives
            // the copy now, so request the remainder for the next flush.
            if (!region.empty() && GFRect::intersection(nativeDamage, region) != region) {
                const GFRect remainder = gui_forms::detail::align_damage_outward(region, scale);
                [self setNeedsDisplayInRect:
                    NSMakeRect(remainder.x, remainder.y, remainder.width, remainder.height)];
            }
        }
        if (drawn && drawn.opaque()) {
            const GFRect covered = gui_forms::detail::opaque_live_clip(
                GFRect::intersection(update.clip, update.destination), scale);
            if (!covered.empty()) _opaqueLiveClips.push_back(covered);
        }
        _liveFrameSources.push_back(source);
        _liveRegions.push_back(region);
        _liveFrames.push_back(std::move(frame));
    }
    // Use the rectangles AppKit is drawing, not their bounding box. Live
    // fragments around an overlay leave only the overlay's share to retained
    // painting; a bounding box would repaint the whole surface beneath it.
    const NSRect* drawnRects = nullptr;
    NSInteger drawnCount = 0;
    [self getRectsBeingDrawn:&drawnRects count:&drawnCount];
    _uncoveredDamage.clear();
    for (NSInteger index = 0; index < drawnCount && !occludedCopy; ++index) {
        const NSRect rect = drawnRects[index];
        const GFRect dirty{rect.origin.x, rect.origin.y, rect.size.width, rect.size.height};
        if (reusableRaster) {
            gui_forms::detail::append_uncovered_live_damage(dirty, scale, _opaqueLiveClips, _uncoveredDamage);
        } else {
            _uncoveredDamage.push_back(dirty);
        }
    }
    if (drawnCount == 0 && !occludedCopy) _uncoveredDamage.push_back(nativeDamage);
    // Nothing owed and no live frame: AppKit's request is a copy of the raster
    // as it stands, and this draw prepares and paints nothing. A resize, a
    // scale change, a reallocated backing or a discarded candidate fails the
    // predicate and repaints.
    const bool copyOnly = gui_forms::detail::copy_only_draw(
        reuse, !_pendingDamage.empty() || copiesPending, !_pendingLivePresentations.empty());
    if (copyOnly) _uncoveredDamage.clear();
    if (reusableRaster && !copyOnly) {
        // Paint the gathered set, not AppKit's coalesced request: outside the
        // model's damage (already in _pendingDamage) and the live surfaces,
        // the raster holds this frame's pixels, which AppKit copies unchanged.
        _liveDestinations.clear();
        for (const LiveSurfacePresentation& update : _pendingLivePresentations) {
            _liveDestinations.push_back(update.destination);
        }
        _liveExposure.clear();
        gui_forms::detail::append_within_live_destinations(_uncoveredDamage, _liveDestinations, _liveExposure);
        bool livePending{false};
        for (const GFRect region : _liveRegions) {
            if (!region.empty()) livePending = true;
        }
        // Live frames narrowed to nothing leave nothing to paint, and empty
        // frame damage would mean the whole raster (a prepared frame clears its
        // candidate). Keep AppKit's rectangles then, as the host did before.
        if (!_pendingDamage.empty() || !_liveExposure.empty() || livePending) {
            _uncoveredDamage.swap(_liveExposure);
        }
    }
    for (const GFRect rectangle : _uncoveredDamage) _pendingDamage.add(rectangle);
    const bool liveOnly = _pendingDamage.empty();
    if (narrowedAny && !liveOnly) {
        // Retained painting can reach beneath live clips; copy those pixels too.
        const GFRect retainedBounds = _pendingDamage.bounds();
        for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
            const GFRect beneath = GFRect::intersection(
                _pendingLivePresentations[index].clip, retainedBounds);
            _liveRegions[index] = GFRect::united(_liveRegions[index], beneath);
        }
    }
    plan.reusable_raster = reusableRaster;
    plan.copy_only = copyOnly;
    plan.live_only = liveOnly;
    plan.narrowed_any = narrowedAny;
    plan.regions_taken = regionsTaken;
    plan.drawn_rects = drawnRects;
    plan.drawn_count = drawnCount;
    return plan;
}

// Begins the frame the plan owes (none for a copy-only draw), applies the
// pending scroll copies, paints the retained damage and the live frames into
// the raster and ends the frame; a prepared frame is committed or aborted.
// Records the raster prepare, retained paint and raster finish phases,
// measured from `started`.
- (MacPaintedFrame)paintFrame:(const MacDrawPlan&)plan
                      started:(std::chrono::steady_clock::time_point)started {
    const double scale = plan.extent.scale;
    const GFSize logicalSize = plan.extent.logical_size;
    const bool reusableRaster = plan.reusable_raster;
    bool liveOnly = plan.live_only;
    bool narrowedAny = plan.narrowed_any;
    MacPaintedFrame painted{};
#if defined(GUI_FORMS_PREPARED_TEXT)
    const bool imagesReady = _raster.synchronize_images((*_model).image_resources());
#else
    static_cast<void>(scale);
    static_cast<void>(logicalSize);
    static_cast<void>(reusableRaster);
    static_cast<void>(_raster.synchronize_images((*_model).image_resources()));
#endif
    DamageRegion frameDamage = _pendingDamage;
    for (const GFRect region : _liveRegions) frameDamage.add(region);
    if (narrowedAny && frameDamage.empty()) {
        // Nothing changed, but empty frame damage means the whole raster.
        for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
            _liveRegions[index] = _pendingLivePresentations[index].clip;
            frameDamage.add(_liveRegions[index]);
        }
        narrowedAny = false;
    }
    // A copy-only draw begins no frame: the front is presented as it stands,
    // with no candidate, receipt or acknowledgement.
    bool frameReady{!plan.copy_only};
#if defined(GUI_FORMS_PREPARED_TEXT)
    gui_forms::PreparedTextStatus admission = gui_forms::PreparedTextStatus::success;
    if (frameReady) {
        admission = imagesReady
            ? _raster.begin_prepared_frame(logicalSize, scale, frameDamage)
            : gui_forms::PreparedTextStatus::resource_failure;
    }
    frameReady = frameReady && admission == gui_forms::PreparedTextStatus::success;
    if (frameReady) {
        // Admission clears the candidate instead of copying the presented
        // front when damage spans the raster (resize, revocation, full live
        // damage). Then live surfaces draw complete clips and retained
        // painting fills what they leave uncovered. Keep that damage until
        // this exact candidate reaches native presentation.
        const GFRect extent{0.0, 0.0, logicalSize.width, logicalSize.height};
        if (frameDamage.bounds().contains(extent)) {
            for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
                _liveRegions[index] = _pendingLivePresentations[index].clip;
            }
            narrowedAny = false;
            _uncoveredDamage.clear();
            if (reusableRaster) {
                gui_forms::detail::append_uncovered_live_damage(extent, scale, _opaqueLiveClips, _uncoveredDamage);
            } else {
                _uncoveredDamage.push_back(extent);
            }
            for (const GFRect rectangle : _uncoveredDamage) _pendingDamage.add(rectangle);
            liveOnly = _pendingDamage.empty();
        }
    }
#else
    if (frameReady) _raster.begin_frame(frameDamage);
#endif
    // The scroll copies move the begun frame's pixels before its damage is
    // painted. A frame not begun repaints their clips later; a copy the
    // raster refuses (it cannot, after the model checked the alignment)
    // drops this frame and repaints the clips in the next.
    _appliedCopyClips.clear();
    if (!_pendingCopies.empty()) {
        bool applied = frameReady;
        for (const gui_forms::ScrollCopy& copy : _pendingCopies) {
            if (applied) applied = _raster.scroll_copy(copy.clip, copy.offset);
            _appliedCopyClips.push_back(copy.clip);
        }
        _pendingCopies.clear();
        if (!applied) {
            for (const GFRect clip : _appliedCopyClips) _pendingDamage.add(clip);
            if (frameReady) {
                _raster.end_frame();
                frameReady = false;
                for (const GFRect clip : _appliedCopyClips) {
                    [self setNeedsDisplayInRect:NSMakeRect(clip.x, clip.y, clip.width, clip.height)];
                }
            }
            _appliedCopyClips.clear();
        }
    }
    _lastFrameDamageBoundsArea = frameDamage.bounds().area();
    const std::chrono::steady_clock::time_point preparedAt =
        std::chrono::steady_clock::now();
    _rasterPreparePhase.record(started, preparedAt);
    std::optional<PaintReceipt> receipt{};
    if (frameReady && !_pendingDamage.empty()) {
        const std::chrono::steady_clock::time_point paintStarted =
            std::chrono::steady_clock::now();
        // Clip to the damaged rectangles, not their bounds: overlays in
        // opposite corners cost their own areas, not the strip between them.
        _raster.save();
        _raster.clip_damage(_pendingDamage);
#if defined(GUI_FORMS_PREPARED_TEXT)
        // A control's exception is not caught here (the fail-fast boundary
        // ends the process); core's typed prepared-text refusal (a stale or
        // cancelled layout) is an expected alternative: no receipt, so the
        // candidate is aborted below and the presented front stays.
        try {
            receipt = (*_model).paint(_raster, _pendingDamage.rectangles());
        } catch (const gui_forms::detail::PreparedTextPaintFailure&) {
            receipt.reset();
        }
#else
        receipt = (*_model).paint(_raster, _pendingDamage.rectangles());
#endif
        _raster.restore();
        const std::chrono::steady_clock::time_point paintEnded =
            std::chrono::steady_clock::now();
        _retainedPaintPhase.record(paintStarted, paintEnded);
    }
    const std::chrono::steady_clock::time_point finishStarted =
        std::chrono::steady_clock::now();
    bool liveDrawsSucceeded{true};
    if (frameReady) {
        for (std::size_t index = 0; index < _pendingLivePresentations.size(); ++index) {
            const LiveSurfacePresentation& update = _pendingLivePresentations[index];
            // Reconfiguration can retire the last frame before a replacement
            // is published. Its clip granted no opaque coverage above.
            const gui_forms::LiveSurfaceFrame& frame = _liveFrames[_liveFrameSources[index]];
            if (!frame) continue;
            // Empty: this fragment's region holds no published change.
            if (_liveRegions[index].empty()) continue;
            _raster.save();
            _raster.clip_rect(_liveRegions[index]);
            liveDrawsSucceeded = _raster.draw_live_surface_frame(
                frame, update.destination, 1.0);
            _raster.restore();
            if (!liveDrawsSucceeded) break;
            (*_model).note_live_surface_presented(update, _liveRegions[index]);
        }
        _raster.end_frame();
    }
#if defined(GUI_FORMS_PREPARED_TEXT)
    bool committed{false};
    if (frameReady && liveDrawsSucceeded && (receipt || liveOnly)) {
        // Live-only pixels retain the same coherent retained-tree revision.
        // Do not manufacture or acknowledge another model paint transaction.
        const PaintReceipt publicationReceipt = receipt ? *receipt : plan.previous_receipt;
        const gui_forms::PreparedTextStatus publication = _raster.commit_prepared_frame(publicationReceipt);
        committed = publication == gui_forms::PreparedTextStatus::success;
    }
    if (!committed) {
        _raster.abort_prepared_frame();
        receipt.reset();
    }
    painted.committed = committed;
#else
    static_cast<void>(liveOnly);
#endif
    // A frame that moved pixels and was not presented repaints the clips.
    if (!receipt) {
        for (const GFRect clip : _appliedCopyClips) _pendingDamage.add(clip);
    }
    _appliedCopyClips.clear();
    _liveFrameGenerations.clear();
    _liveFrameGenerations.reserve(_liveFrames.size());
    for (const gui_forms::LiveSurfaceFrame& frame : _liveFrames) {
        _liveFrameGenerations.push_back(frame ? frame.generation() : 0U);
    }
    _liveFrames.clear();
    const std::chrono::steady_clock::time_point finishEnded =
        std::chrono::steady_clock::now();
    _rasterFinishPhase.record(finishStarted, finishEnded);
    painted.receipt = receipt;
    painted.live_draws_succeeded = liveDrawsSucceeded;
    return painted;
}

// Copies the raster (through the invalidation overlay's scratch frame while
// outlines show) into AppKit's context, then settles what the presentation
// validated: the live surfaces shown learn their generations, the overlay's
// fade timer is armed, presented live presentations are released and a
// taken region is validated or marked lost. Records the CG setup, draw and
// release phases.
- (MacPresentedFrame)presentFrame:(NSRect)dirtyRect
                             plan:(const MacDrawPlan&)plan
                          painted:(const MacPaintedFrame&)painted
                          started:(std::chrono::steady_clock::time_point)started {
    const GFSize logicalSize = plan.extent.logical_size;
    const std::optional<PaintReceipt>& receipt = painted.receipt;
    const bool liveDrawsSucceeded = painted.live_draws_succeeded;
    MacPresentedFrame presentation{};
    if (receipt) {
        _rasterHasContent = YES;
        _frontSurfaceEpoch = (*receipt).surface_epoch;
    }
    GFSize presentationSize = logicalSize;
#if defined(GUI_FORMS_PREPARED_TEXT)
    _rasterHasContent = _raster.prepared_front_receipt() ? YES : NO;
    if (_rasterHasContent == YES) {
        const double frontScale = _raster.prepared_front_scale();
        presentationSize = {
            static_cast<double>(_raster.pixel_width()) / frontScale,
            static_cast<double>(_raster.pixel_height()) / frontScale};
    }
#endif
    bool presented = false;
    std::uint64_t copyNanoseconds = 0U;
    const void* pixels = _raster.pixels();
    // The invalidation overlay at present time: the dirty rectangle's pixels
    // are copied into a scratch frame and the outlines composited there, and
    // that frame is drawn instead of the raster. AppKit clips the draw to the
    // dirty rectangles, so the scratch frame's pixels outside them are never
    // shown. The raster is never drawn on, so the overlay damages nothing.
    // Off, this is one relaxed load.
    gui_forms::InvalidationOverlay* const overlay = (*_model).invalidation_overlay();
    if (overlay != nullptr && pixels != nullptr && _rasterHasContent == YES && presentationSize.width > 0.0) {
        const std::size_t frameBytes = _raster.byte_size();
        const std::int32_t frameWidth = static_cast<std::int32_t>(_raster.pixel_width());
        const std::int32_t frameHeight = static_cast<std::int32_t>(_raster.pixel_height());
        const std::size_t rowBytes = _raster.row_bytes();
        const double frameScale = static_cast<double>(frameWidth) / presentationSize.width;
        if (_overlayScratch.size() != frameBytes) _overlayScratch.assign(frameBytes, std::byte{0});
        // The dirty rectangle in device pixels, outward (the view is flipped,
        // so its rows are the raster's).
        gui_forms::DevicePixelRect box{};
        box.left = std::clamp<std::int32_t>(
            static_cast<std::int32_t>(std::floor(dirtyRect.origin.x * frameScale)), 0, frameWidth);
        box.top = std::clamp<std::int32_t>(
            static_cast<std::int32_t>(std::floor(dirtyRect.origin.y * frameScale)), 0, frameHeight);
        box.right = std::clamp<std::int32_t>(
            static_cast<std::int32_t>(std::ceil((dirtyRect.origin.x + dirtyRect.size.width) * frameScale)),
            box.left, frameWidth);
        box.bottom = std::clamp<std::int32_t>(
            static_cast<std::int32_t>(std::ceil((dirtyRect.origin.y + dirtyRect.size.height) * frameScale)),
            box.top, frameHeight);
        const std::byte* const source = static_cast<const std::byte*>(pixels);
        if (_presenter && (*_presenter).direct_available()) {
            // The direct path writes from the frame outside the dirty
            // rectangle too (an older buffer's missing damage): the scratch
            // frame is the whole raster while the overlay is on.
            std::memcpy(_overlayScratch.data(), source, frameBytes);
        } else {
            const std::size_t rowOffset = static_cast<std::size_t>(box.left) * 4U;
            const std::size_t rowLength = static_cast<std::size_t>(box.right - box.left) * 4U;
            for (std::int32_t y = box.top; y < box.bottom; ++y) {
                const std::size_t offset = static_cast<std::size_t>(y) * rowBytes + rowOffset;
                std::memcpy(_overlayScratch.data() + offset, source + offset, rowLength);
            }
        }
        const gui_forms::InvalidationClock::time_point now = gui_forms::InvalidationClock::now();
        (*overlay).expire(now);
        gui_forms::InvalidationOverlayTarget target{};
        target.pixels = std::span<std::byte>(_overlayScratch);
        target.width = static_cast<std::uint32_t>(frameWidth);
        target.height = static_cast<std::uint32_t>(frameHeight);
        target.row_bytes = rowBytes;
        (*overlay).compose(target, frameScale, now, box);
        pixels = _overlayScratch.data();
    }
    // Native exposure/snapshot callbacks can precede unocclusion. The model
    // intentionally declines painting then; the last complete raster is still
    // valid and must be copied into AppKit's newly supplied backing context.
    // The copy goes through the presentation shim; its copy path is the
    // CoreGraphics draw below (copyFramePixels:), which AppKit clips to the
    // rectangles being drawn, so the whole raster is named as the damage.
    // The direct path (macos_iosurface_present.hpp) writes only the damage
    // into its surface, so while it is usable the damage is the rectangles
    // AppKit is drawing, in device pixels, outward.
    if (_rasterHasContent == YES && pixels != nullptr && liveDrawsSucceeded) {
        gui_forms::host::presentation::PresentationShim& presenter = [self presenter];
        gui_forms::host::presentation::PresentFrame frame{};
        frame.source.pixels = static_cast<const std::byte*>(pixels);
        frame.source.row_bytes = _raster.row_bytes();
        frame.source.extent = gui_forms::host::presentation::PresentExtent{_raster.pixel_width(),
                                                                            _raster.pixel_height()};
        _presentRects.clear();
        if (presenter.direct_available() && presentationSize.width > 0.0) {
            [self appendDrawnDeviceRects:frame.source.extent
                                   scale:static_cast<double>(_raster.pixel_width()) / presentationSize.width];
            if (_directPath != nullptr) {
                (*_directPath).opaque = self.window == nil || self.window.opaque == YES;
                (*_directPath).scale = static_cast<double>(_raster.pixel_width()) / presentationSize.width;
            }
        } else {
            _presentRects.push_back(gui_forms::host::presentation::present_extent_rect(frame.source.extent));
        }
        frame.damage = _presentRects;
        (*_copyPath).presentation_width = presentationSize.width;
        (*_copyPath).presentation_height = presentationSize.height;
        (*_copyPath).copy_nanoseconds = 0U;
        const gui_forms::host::presentation::PresentReport report = presenter.present(frame);
        presented = report.outcome == gui_forms::host::presentation::PresentOutcome::presented;
        copyNanoseconds = (*_copyPath).copy_nanoseconds;
    }
    const std::chrono::duration<long long, std::ratio<1, 1000000000>> elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started);
#if defined(GUI_FORMS_PREPARED_TEXT)
    const bool livePresented = presented && painted.committed;
#else
    const bool livePresented = presented;
#endif
    // Each surface shown learns the generation it showed and the display
    // timing of this pass (threaded sub-surfaces design note sections 7 and
    // 8), once per pass, before the presentations are released.
    if (livePresented) [self reportLivePresentations];
    // Outlines recorded by this draw may reach outside its dirty rectangles
    // (labels, the legend, the counters card): the fade timer's first tick
    // presents them. An overlay just turned off leaves its last areas to
    // present from the raster. The counters card alone arms nothing, so a
    // tick's own copy-only draw does not arm the next.
    if ((overlay != nullptr && !(*overlay).empty()) || (overlay == nullptr && !_overlayDrawn.empty())) {
        [self armInvalidationOverlayTimer];
    }
    if (presented) {
#if defined(GUI_FORMS_PREPARED_TEXT)
        if (painted.committed) _pendingLivePresentations.clear();
#else
        _pendingLivePresentations.clear();
#endif
    }
    // A taken region is validated only by a presented raster, as EndPaint
    // validates an update region. Otherwise the next pass copies everything.
    if (plan.regions_taken) _liveRegionLost = livePresented ? NO : YES;
    presentation.presented = presented;
    presentation.live_presented = livePresented;
    presentation.copy_nanoseconds = copyNanoseconds;
    presentation.elapsed_nanoseconds = static_cast<std::uint64_t>(elapsed.count());
    return presentation;
}

// The copy path's draw: the raster's pixels (or the overlay's scratch frame
// in the same layout) as a CGImage drawn into the current context.
- (BOOL)copyFramePixels:(const void*)pixels
       presentationSize:(GFSize)presentationSize
        copyNanoseconds:(std::uint64_t&)copyNanoseconds {
    BOOL drawn = NO;
    const std::chrono::steady_clock::time_point setupStarted =
        std::chrono::steady_clock::now();
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, pixels, _raster.byte_size(), nullptr);
    CGColorSpaceRef colorSpace = _presentColorSpace;
    // An opaque window presents opaque pixels, as the other hosts do: the
    // frame's alpha reaches the window server only while a non-opaque
    // backdrop is granted (stage T5).
    const bool windowOpaque = self.window == nil || self.window.opaque == YES;
    const CGImageAlphaInfo alphaInfo =
        windowOpaque ? kCGImageAlphaNoneSkipFirst : kCGImageAlphaPremultipliedFirst;
    // The raster is premultiplied BGRA8 (SK2): read as 32-bit
    // little-endian words with alpha first, the window server's own
    // order, so the copy into the backing store needs no swizzle.
    const CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(
        static_cast<CGBitmapInfo>(alphaInfo) |
        static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little));
    CGImageRef image = CGImageCreate(
        _raster.pixel_width(), _raster.pixel_height(), 8, 32,
        _raster.row_bytes(), colorSpace, bitmapInfo, provider,
        nullptr, false, kCGRenderingIntentDefault);
    const std::chrono::steady_clock::time_point setupEnded =
        std::chrono::steady_clock::now();
    _cgSetupPhase.record(setupStarted, setupEnded);
    if (image != nullptr) {
        CGContextRef context = NSGraphicsContext.currentContext.CGContext;
        const CGRect clipBounds = CGContextGetClipBoundingBox(context);
        _lastCGClipBoundsArea = std::max(0.0, clipBounds.size.width) *
            std::max(0.0, clipBounds.size.height);
        _lastCGDestinationArea = presentationSize.width * presentationSize.height;
        _lastCGSourceBytes = _raster.byte_size();
        const std::chrono::steady_clock::time_point drawStarted =
            std::chrono::steady_clock::now();
        CGContextSaveGState(context);
        CGContextSetBlendMode(context, kCGBlendModeCopy);
        CGContextTranslateCTM(context, 0.0, presentationSize.height);
        CGContextScaleCTM(context, 1.0, -1.0);
        CGContextDrawImage(context,
                           CGRectMake(0.0, 0.0, presentationSize.width, presentationSize.height),
                           image);
        CGContextRestoreGState(context);
        const std::chrono::steady_clock::time_point drawEnded =
            std::chrono::steady_clock::now();
        _cgDrawPhase.record(drawStarted, drawEnded);
        copyNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(drawEnded - drawStarted).count());
        drawn = YES;
    }
    const std::chrono::steady_clock::time_point releaseStarted =
        std::chrono::steady_clock::now();
    if (image != nullptr) CGImageRelease(image);
    CGDataProviderRelease(provider);
    const std::chrono::steady_clock::time_point releaseEnded =
        std::chrono::steady_clock::now();
    _cgReleasePhase.record(releaseStarted, releaseEnded);
    return drawn;
}

// The shim: the copy path always, and a direct path where the presentation
// policy turns it on (GUI_FORMS_PRESENT=direct): the Metal route where it
// is built and the policy also names "metal", the IOSurface path
// otherwise. The shim owns the direct path; _directPath observes it to set
// its opacity and scale.
- (gui_forms::host::presentation::PresentationShim&)presenter {
    if (_presenter) return *_presenter;
    _copyPath = std::make_unique<gui_forms::host::macos::MacViewCopyPath>(self);
    std::unique_ptr<gui_forms::host::presentation::PresentationDirectPath> direct{};
    _directPath = nullptr;
    const gui_forms::host::presentation::PresentPolicy policy =
        gui_forms::host::presentation::present_policy_from_environment();
#if defined(GUI_FORMS_HAVE_METAL_PRESENT)
    if (policy.direct && gui_forms::host::presentation::present_policy_names("metal")) {
        std::unique_ptr<gui_forms::host::macos::MacMetalPath> drawables =
            std::make_unique<gui_forms::host::macos::MacMetalPath>(self, _presentColorSpace);
        _directPath = drawables.get();
        direct = std::move(drawables);
    }
#endif
    if (policy.direct && !direct) {
        std::unique_ptr<gui_forms::host::macos::MacIOSurfacePath> surfaces =
            std::make_unique<gui_forms::host::macos::MacIOSurfacePath>(self, _presentColorSpace);
        _directPath = surfaces.get();
        direct = std::move(surfaces);
    }
    _presenter = std::make_unique<gui_forms::host::presentation::PresentationShim>(
        _copyPath.get(), std::move(direct), nullptr);
    return *_presenter;
}

// The rectangles AppKit is drawing (inside drawRect:), in device pixels at
// `scale` device pixels per point, rounded outward and clipped to `extent`,
// appended to _presentRects. The view is flipped, so its rows are the
// raster's. None drawn names the whole extent.
- (void)appendDrawnDeviceRects:(gui_forms::host::presentation::PresentExtent)extent scale:(double)scale {
    const NSRect* drawnRects = nullptr;
    NSInteger drawnCount = 0;
    [self getRectsBeingDrawn:&drawnRects count:&drawnCount];
    const gui_forms::host::presentation::PresentRect bounds =
        gui_forms::host::presentation::present_extent_rect(extent);
    for (NSInteger index = 0; index < drawnCount; ++index) {
        const NSRect rect = drawnRects[index];
        const double left = std::floor(rect.origin.x * scale);
        const double top = std::floor(rect.origin.y * scale);
        const double right = std::ceil((rect.origin.x + rect.size.width) * scale);
        const double bottom = std::ceil((rect.origin.y + rect.size.height) * scale);
        const double limit = static_cast<double>(gui_forms::host::presentation::maximum_present_side);
        gui_forms::host::presentation::PresentRect device{};
        device.left = static_cast<std::int32_t>(std::clamp(left, 0.0, limit));
        device.top = static_cast<std::int32_t>(std::clamp(top, 0.0, limit));
        device.right = static_cast<std::int32_t>(std::clamp(right, 0.0, limit));
        device.bottom = static_cast<std::int32_t>(std::clamp(bottom, 0.0, limit));
        const gui_forms::host::presentation::PresentRect clipped =
            gui_forms::host::presentation::present_rect_intersection(device, bounds);
        if (!clipped.empty()) _presentRects.push_back(clipped);
    }
    if (drawnCount == 0) _presentRects.push_back(bounds);
}

// Asks for the whole view to be drawn again: the direct path gave its
// surfaces back and the backing store beneath them is stale. Sent by
// MacIOSurfacePath::release after the current draw.
- (void)markPresentationDirty {
    [self setNeedsDisplay:YES];
}

// What this flush copied to the screen (stage DP). AppKit clips the copy
// to the rectangles being drawn; the live frames' regions are reported as
// live and the rest as retained, and the duration is split between them
// by area. A surface's clip arrives as fragments around overlays: each
// surface frame is one live frame, its fragments its rectangles. An
// acknowledged retained report clears the damage the frame painted.
- (void)recordDrawMetrics:(const MacDrawPlan&)plan
                  painted:(const MacPaintedFrame&)painted
             presentation:(const MacPresentedFrame&)presentation {
    const double scale = plan.extent.scale;
    const GFRect nativeDamage = plan.extent.native_damage;
    const NSRect* const drawnRects = plan.drawn_rects;
    const NSInteger drawnCount = plan.drawn_count;
    const std::optional<PaintReceipt>& receipt = painted.receipt;
    const bool presented = presentation.presented;
    const bool livePresented = presentation.live_presented;
    const std::uint64_t copyNanoseconds = presentation.copy_nanoseconds;
    gui_forms::PresentedArea retainedCopied;
    std::uint64_t liveFramesCopied = 0U;
    double liveArea = 0.0;
    double drawnArea = 0.0;
    const double devicePixelsPerPoint = scale * scale;
    if (presented) {
        for (NSInteger index = 0; index < drawnCount; ++index) {
            drawnArea += drawnRects[index].size.width * drawnRects[index].size.height;
        }
        if (drawnCount == 0) drawnArea = nativeDamage.width * nativeDamage.height;
        if (livePresented) {
            for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
                if (_liveRegions[index].empty()) continue;
                liveArea += _liveRegions[index].width * _liveRegions[index].height;
                bool firstOfFrame = true;
                for (std::size_t earlier = 0; earlier < index; ++earlier) {
                    if (_liveFrameSources[earlier] == _liveFrameSources[index] &&
                        !_liveRegions[earlier].empty()) {
                        firstOfFrame = false;
                    }
                }
                if (firstOfFrame) ++liveFramesCopied;
            }
        }
        const double retainedArea = std::max(0.0, drawnArea - liveArea);
        retainedCopied.device_pixels =
            static_cast<std::uint64_t>(std::llround(retainedArea * devicePixelsPerPoint));
        retainedCopied.rectangles =
            drawnCount > 0 ? static_cast<std::uint64_t>(drawnCount) : 1U;
        // The window records the copy time with the retained report; a
        // live-only flush reports it with its first live frame.
        if (receipt) retainedCopied.copy_nanoseconds = copyNanoseconds;
    }
    std::uint64_t unreportedCopy = receipt ? 0U : copyNanoseconds;
    const std::uint64_t elapsedNanoseconds = presentation.elapsed_nanoseconds;
    const double copiedArea = std::max(drawnArea, liveArea);
    const std::uint64_t liveElapsed = copiedArea <= 0.0 ? 0U :
        static_cast<std::uint64_t>(static_cast<double>(elapsedNanoseconds) * liveArea / copiedArea);
    if (presented) {
        if (receipt &&
            (*_model).notify_presented(*receipt, elapsedNanoseconds - liveElapsed,
                                       retainedCopied)) {
            _pendingDamage.clear();
        }
    }
    if (liveFramesCopied != 0U) {
        const std::uint64_t liveDuration =
            (receipt ? liveElapsed : elapsedNanoseconds) / liveFramesCopied;
        for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
            // One report per surface frame, at its first fragment.
            if (_liveFrameSources[index] != index) continue;
            gui_forms::PresentedArea liveCopied;
            for (std::size_t fragment = index; fragment < _liveRegions.size(); ++fragment) {
                if (_liveFrameSources[fragment] != index || _liveRegions[fragment].empty()) continue;
                const GFRect region = _liveRegions[fragment];
                liveCopied.device_pixels += static_cast<std::uint64_t>(
                    std::llround(region.width * region.height * devicePixelsPerPoint));
                ++liveCopied.rectangles;
            }
            if (liveCopied.rectangles == 0U) continue;
            liveCopied.copy_nanoseconds = unreportedCopy;
            unreportedCopy = 0U;
            (*_model).notify_live_presented(liveDuration, liveCopied);
        }
    }
}

- (void)setDrawTraceEnabled:(BOOL)enabled
           captureDirectory:(std::string)directory
                     stride:(std::size_t)stride
                      limit:(std::size_t)limit {
    _drawTrace.reset();
    if (enabled == NO) return;
    _drawTrace = std::make_unique<MacDrawTrace>();
    MacDrawTrace& trace = *_drawTrace;
    trace.capture_directory = std::move(directory);
    trace.capture_stride = stride == 0U ? 1U : stride;
    trace.capture_limit = limit;
}

- (BOOL)readDrawTrace:(std::vector<MacDrawRecord>&)records {
    records.clear();
    if (!_drawTrace) return NO;
    const MacDrawTrace& trace = *_drawTrace;
    records.reserve(trace.count);
    for (std::size_t index = 0U; index < trace.count; ++index) records.push_back(trace.records[index]);
    return trace.overflow ? YES : NO;
}

// Appends one record to the enabled draw trace and, for a presented frame
// the capture stride selects, writes the front raster as a PNG.
- (void)recordDrawTrace:(NSRect)dirtyRect
                started:(std::chrono::steady_clock::time_point)started
           presentation:(BOOL)presented
               duration:(std::uint64_t)durationNanoseconds {
    MacDrawTrace& trace = *_drawTrace;
    if (trace.count < trace.records.size()) {
        MacDrawRecord& record = trace.records[trace.count];
        const std::chrono::nanoseconds since = std::chrono::duration_cast<std::chrono::nanoseconds>(
            started.time_since_epoch());
        record.started_ns = static_cast<std::uint64_t>(since.count());
        record.duration_ns = durationNanoseconds;
        record.width = self.bounds.size.width;
        record.height = self.bounds.size.height;
        record.dirty_x = dirtyRect.origin.x;
        record.dirty_y = dirtyRect.origin.y;
        record.dirty_width = dirtyRect.size.width;
        record.dirty_height = dirtyRect.size.height;
        record.presented = presented == YES;
        record.live_resize = self.inLiveResize == YES;
        ++trace.count;
    } else {
        trace.overflow = true;
    }
    if (presented == NO) return;
    ++trace.presented;
    if (trace.capture_directory.empty() || trace.captured >= trace.capture_limit) return;
    if ((trace.presented - 1U) % trace.capture_stride != 0U) return;
    char name[32]{};
    std::snprintf(name, sizeof name, "/frame_%04zu.png", trace.captured);
    const std::string path = trace.capture_directory + name;
    const std::vector<std::byte> encoded = _raster.encode_png();
    if (gui_forms::host::detail::complete_frame_capture(path, encoded)) ++trace.captured;
}

- (void)installFrameCapturePath:(std::string)path {
    _frameCapturePath = std::move(path);
    _frameCaptureCompleted = NO;
    _frameCaptureSaved = NO;
}
- (BOOL)frameCaptureSaved {
    return _frameCaptureSaved;
}
- (void)completeFrameCaptureIfDue:(NSRect)dirtyRect presented:(BOOL)presented {
    if (_frameCaptureCompleted || _frameCapturePath.empty() || presented == NO ||
        !NSContainsRect(dirtyRect, self.bounds) || NSIsEmptyRect(self.bounds)) {
        return;
    }
    _frameCaptureCompleted = YES;
    // The front raster is the presented framebuffer: premultiplied BGRA8 that
    // CGContextDrawImage copied above. Encode it through the private Skia
    // adapter seam and report through the shared portable contract.
    const std::vector<std::byte> encoded = _raster.encode_png();
    _frameCaptureSaved = gui_forms::host::detail::complete_frame_capture(
        _frameCapturePath, encoded) ? YES : NO;
    post_perform_close(self.window);
}
// The display timing of the display link's last tick, in FrameClock time:
// its vertical blank and refresh period. Empty (source none) before the
// link first ticked or while it has no link.
- (gui_forms::LiveDisplayTiming)liveDisplayTiming {
    gui_forms::LiveDisplayTiming timing{};
    if (_displayLink == nil) return timing;
    const CFTimeInterval vblank = _displayLink.timestamp;
    const CFTimeInterval period = _displayLink.duration;
    if (vblank <= 0.0 || period <= 0.0) return timing;
    // CADisplayLink times are CACurrentMediaTime seconds; carry the age of
    // the vertical blank over to the steady clock.
    const CFTimeInterval age = std::max(0.0, CACurrentMediaTime() - vblank);
    const gui_forms::FrameTime now = gui_forms::FrameClock::now();
    timing.refresh_period = std::chrono::duration_cast<gui_forms::FrameInterval>(
        std::chrono::duration<double>(period));
    timing.last_vblank = now - std::chrono::duration_cast<gui_forms::FrameInterval>(
        std::chrono::duration<double>(age));
    timing.source = gui_forms::LiveTimingSource::display_phased;
    return timing;
}

// Reports each surface this pass presented to its LiveSurface, once per
// surface (its fragments share one frame), with the generation shown. Reads
// _pendingLivePresentations, _liveFrameSources, _liveRegions and
// _liveFrameGenerations as the pass left them.
- (void)reportLivePresentations {
    const gui_forms::LiveDisplayTiming timing = [self liveDisplayTiming];
    for (std::size_t index = 0; index < _pendingLivePresentations.size(); ++index) {
        if (index >= _liveRegions.size() || index >= _liveFrameSources.size()) break;
        if (_liveRegions[index].empty() || !_pendingLivePresentations[index].surface) continue;
        const std::size_t source = _liveFrameSources[index];
        if (source >= _liveFrameGenerations.size() || _liveFrameGenerations[source] == 0U) continue;
        bool first = true;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (_liveFrameSources[earlier] == source && !_liveRegions[earlier].empty()) first = false;
        }
        if (!first) continue;
        (*_pendingLivePresentations[index].surface).notify_presented(_liveFrameGenerations[source], timing);
    }
}

// The invalidation overlay's fade (invalidation_overlay.hpp): while outlines
// show, every tick asks AppKit to draw the areas they cover now and those
// they covered at the last tick. With nothing owed, such a draw is a copy of
// the raster (copy_only_draw), composited at present time, so the overlay
// causes no paint of its own; a faded outline leaves the raster's pixels.
// The timer stops once nothing shows. UI thread.
- (void)armInvalidationOverlayTimer {
    if (_overlayTimer != nil || self.window == nil) return;
    GUIFormsOverlayTimerTarget* proxy = [[GUIFormsOverlayTimerTarget alloc] initWithView:self];
    _overlayTimer = [NSTimer timerWithTimeInterval:(1.0 / 30.0)
                                            target:proxy
                                          selector:@selector(tick:)
                                          userInfo:nil
                                           repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_overlayTimer forMode:NSRunLoopCommonModes];
}

- (void)stopInvalidationOverlayTimer {
    if (_overlayTimer == nil) return;
    [_overlayTimer invalidate];
    _overlayTimer = nil;
}

- (void)answerInvalidationOverlayTick {
    if (!_model) {
        [self stopInvalidationOverlayTimer];
        return;
    }
    const double scale = self.window == nil ? 1.0 : self.window.backingScaleFactor;
    const double width = std::max(0.0, self.bounds.size.width) * scale;
    const double height = std::max(0.0, self.bounds.size.height) * scale;
    _overlayRegions.clear();
    gui_forms::InvalidationOverlay* const overlay = (*_model).invalidation_overlay();
    if (overlay != nullptr) {
        (*overlay).expire(gui_forms::InvalidationClock::now());
        (*overlay).fill_regions(scale, static_cast<std::uint32_t>(std::ceil(width)),
                                static_cast<std::uint32_t>(std::ceil(height)), _overlayRegions);
    }
    for (const gui_forms::DevicePixelRect& region : _overlayRegions) {
        [self setNeedsDisplayInRect:NSMakeRect(region.left / scale, region.top / scale,
                                               (region.right - region.left) / scale,
                                               (region.bottom - region.top) / scale)];
    }
    for (const gui_forms::DevicePixelRect& region : _overlayDrawn) {
        [self setNeedsDisplayInRect:NSMakeRect(region.left / scale, region.top / scale,
                                               (region.right - region.left) / scale,
                                               (region.bottom - region.top) / scale)];
    }
    _overlayDrawn.swap(_overlayRegions);
    // With no outline left nothing fades: the areas drawn at the last tick
    // were asked for above, and the counters card (the diagnostic overlay)
    // is presented again with the next paint, so the ticks stop.
    if (_overlayDrawn.empty() || (overlay != nullptr && (*overlay).empty())) [self stopInvalidationOverlayTimer];
}

@end

// This unit's link anchor (macos_view.hpp): referenced from
// link_view_categories in macos_view.mm so that the archive member holding
// the Drawing category is linked wherever GUIFormsView is.
namespace gui_forms::host::macos {
void gui_forms_macos_view_drawing_anchor() noexcept {}
} // namespace gui_forms::host::macos

namespace gui_forms::host::macos {

bool MacViewCopyPath::scroll(const presentation::PresentScrollPlan& plan) {
    static_cast<void>(plan);
    return false;
}

presentation::PresentOutcome MacViewCopyPath::copy(const presentation::PresentSourcePixels& source,
                                                   std::span<const presentation::PresentRect> rects) {
    static_cast<void>(rects);
    const GFSize size{presentation_width, presentation_height};
    const BOOL drawn = [view_ copyFramePixels:source.pixels presentationSize:size copyNanoseconds:copy_nanoseconds];
    if (drawn == YES) return presentation::PresentOutcome::presented;
    return presentation::PresentOutcome::failed;
}

} // namespace gui_forms::host::macos
