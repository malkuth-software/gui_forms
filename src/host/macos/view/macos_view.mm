// GUIFormsView's primary implementation (macos_view.hpp): construction and
// attach, the host session's reports, window lifecycle and shutdown, and
// the native entries. Each entry AppKit calls goes through the fail-fast
// boundary (call_failing_fast) to a named target below, which sends the
// entry's answer; the answers that are not lifecycle live in the categories.

#include "macos_view.hpp"
#include "../application/macos_bundled_fonts.hpp"
#include "../application/macos_main_work.hpp"
#include "../application/macos_values.hpp"

// Main-queue work without Objective-C blocks (house style section 9, owner
// decision V2-1). Each posting allocates one named context on the heap and
// passes it to dispatch_async_f with a named dispatch_function_t; the
// function adopts the context (std::unique_ptr) and so deletes it whether or
// not its target still lives. Contexts hold their Objective-C objects as the
// blocks they replace captured them: __weak where the block re-checked a
// weak reference, strong (ARC's default) where it captured the object.

namespace {
// A view that may be gone by the time the work runs.
struct WeakViewWork final {
    __weak GUIFormsView* view;
};

void drain_posted_work_on_main(void* context) {
    const std::unique_ptr<WeakViewWork> work(static_cast<WeakViewWork*>(context));
    GUIFormsView* const view = (*work).view;
    if (view != nil) [view drainPostedWork];
}

void collect_damage_on_main(void* context) {
    const std::unique_ptr<WeakViewWork> work(static_cast<WeakViewWork*>(context));
    GUIFormsView* const view = (*work).view;
    if (view != nil) [view collectDamage];
}

// The wake timer's event handler. Its context belongs to the dispatch
// source (dispatch_set_context) and is deleted by release_weak_view_work,
// the source's finalizer, when the source is released.
void scheduled_wake_on_main(void* context) {
    WeakViewWork& work = *static_cast<WeakViewWork*>(context);
    GUIFormsView* const view = work.view;
    if (view != nil) [view scheduledWake];
}

void release_weak_view_work(void* context) {
    const std::unique_ptr<WeakViewWork> work(static_cast<WeakViewWork*>(context));
}
} // namespace

class DrainPostedWorkWake final {
public:
    explicit DrainPostedWorkWake(GUIFormsView* view) noexcept
        : view_(view) {}

    void operator()() const
    {
        WeakViewWork* const work = new WeakViewWork{view_};
        post_main_work(work, &drain_posted_work_on_main);
    }

private:
    __weak GUIFormsView* view_;
};

class CollectDamageWake final {
public:
    explicit CollectDamageWake(GUIFormsView* view) noexcept
        : view_(view) {}

    void operator()() const
    {
        WeakViewWork* const work = new WeakViewWork{view_};
        post_main_work(work, &collect_damage_on_main);
    }

private:
    __weak GUIFormsView* view_;
};

namespace {
// The named targets the fail-fast boundary calls for GUIFormsView's native
// entries (call_failing_fast): each sends the entry's body message.
void run_display_link_tick(GUIFormsView* view) {
    [view answerDisplayLinkTick];
}
void run_posted_work(GUIFormsView* view) {
    [view answerDrainPostedWork];
}
void run_collect_damage(GUIFormsView* view) {
    [view collectDamage];
}
void run_draw_retained(GUIFormsView* view, NSRect dirty) {
    [view drawRetainedRect:dirty];
}
void run_view_accessibility_display_options_changed(GUIFormsView* object, NSNotification* notification) {
    [object answerAccessibilityDisplayOptionsChanged:notification];
}
void run_view_system_colours_changed(GUIFormsView* object, NSNotification* notification) {
    [object answerSystemColoursChanged:notification];
}
void run_view_view_did_change_effective_appearance(GUIFormsView* object) {
    [object answerViewDidChangeEffectiveAppearance];
}
NSRect run_view_accessibility_frame(GUIFormsView* object) {
    return [object answerAccessibilityFrame];
}
void run_view_screen_parameters_changed(GUIFormsView* object, NSNotification* notification) {
    [object answerScreenParametersChanged:notification];
}
NSString* run_view_accessibility_label(GUIFormsView* object) {
    return [object answerAccessibilityLabel];
}
NSArray* run_view_accessibility_children(GUIFormsView* object) {
    return [object answerAccessibilityChildren];
}
NSArray* run_view_accessibility_children_in_navigation_order(GUIFormsView* object) {
    return [object answerAccessibilityChildrenInNavigationOrder];
}
NSArray* run_view_accessibility_visible_children(GUIFormsView* object) {
    return [object answerAccessibilityVisibleChildren];
}
NSRect run_view_accessibility_frame_for_semantic_bounds(GUIFormsView* object, GFRect bounds) {
    return [object answerAccessibilityFrameForSemanticBounds:bounds];
}
void run_view_view_did_move_to_window(GUIFormsView* object) {
    [object answerViewDidMoveToWindow];
}
void run_view_view_will_move_to_window(GUIFormsView* object, NSWindow* newWindow) {
    [object answerViewWillMoveToWindow:newWindow];
}
void run_view_dealloc(__unsafe_unretained GUIFormsView* object) {
    [object answerDealloc];
}
void run_view_set_frame_size(GUIFormsView* object, NSSize newSize) {
    [object answerSetFrameSize:newSize];
}
void run_view_update_tracking_areas(GUIFormsView* object) {
    [object answerUpdateTrackingAreas];
}
void run_view_invalidation_overlay_tick(GUIFormsView* object) {
    [object answerInvalidationOverlayTick];
}
NSDragOperation run_view_dragging_entered(GUIFormsView* object, id<NSDraggingInfo> sender) {
    return [object answerDraggingEntered:sender];
}
NSDragOperation run_view_dragging_updated(GUIFormsView* object, id<NSDraggingInfo> sender) {
    return [object answerDraggingUpdated:sender];
}
void run_view_dragging_exited(GUIFormsView* object, id<NSDraggingInfo> sender) {
    [object answerDraggingExited:sender];
}
BOOL run_view_prepare_for_drag_operation(GUIFormsView* object, id<NSDraggingInfo> sender) {
    return [object answerPrepareForDragOperation:sender];
}
BOOL run_view_perform_drag_operation(GUIFormsView* object, id<NSDraggingInfo> sender) {
    return [object answerPerformDragOperation:sender];
}
void run_view_cursor_update(GUIFormsView* object, NSEvent* event) {
    [object answerCursorUpdate:event];
}
void run_view_mouse_exited(GUIFormsView* object, NSEvent* event) {
    [object answerMouseExited:event];
}
void run_view_mouse_down(GUIFormsView* object, NSEvent* event) {
    [object answerMouseDown:event];
}
void run_view_mouse_up(GUIFormsView* object, NSEvent* event) {
    [object answerMouseUp:event];
}
void run_view_right_mouse_down(GUIFormsView* object, NSEvent* event) {
    [object answerRightMouseDown:event];
}
NSMenu* run_view_menu_for_event(GUIFormsView* object, NSEvent* event) {
    return [object answerMenuForEvent:event];
}
void run_view_right_mouse_up(GUIFormsView* object, NSEvent* event) {
    [object answerRightMouseUp:event];
}
void run_view_other_mouse_down(GUIFormsView* object, NSEvent* event) {
    [object answerOtherMouseDown:event];
}
void run_view_other_mouse_up(GUIFormsView* object, NSEvent* event) {
    [object answerOtherMouseUp:event];
}
void run_view_mouse_moved(GUIFormsView* object, NSEvent* event) {
    [object answerMouseMoved:event];
}
void run_view_mouse_dragged(GUIFormsView* object, NSEvent* event) {
    [object answerMouseDragged:event];
}
void run_view_right_mouse_dragged(GUIFormsView* object, NSEvent* event) {
    [object answerRightMouseDragged:event];
}
void run_view_other_mouse_dragged(GUIFormsView* object, NSEvent* event) {
    [object answerOtherMouseDragged:event];
}
void run_view_scroll_wheel(GUIFormsView* object, NSEvent* event) {
    [object answerScrollWheel:event];
}
void run_view_key_down(GUIFormsView* object, NSEvent* event) {
    [object answerKeyDown:event];
}
void run_view_key_up(GUIFormsView* object, NSEvent* event) {
    [object answerKeyUp:event];
}
void run_view_flags_changed(GUIFormsView* object, NSEvent* event) {
    [object answerFlagsChanged:event];
}
void run_view_insert_text(GUIFormsView* object, id string, NSRange replacementRange) {
    [object answerInsertText:string replacementRange:replacementRange];
}
void run_view_set_marked_text(GUIFormsView* object, id string, NSRange selectedRange, NSRange replacementRange) {
    [object answerSetMarkedText:string selectedRange:selectedRange replacementRange:replacementRange];
}
void run_view_unmark_text(GUIFormsView* object) {
    [object answerUnmarkText];
}
BOOL run_view_has_marked_text(GUIFormsView* object) {
    return [object answerHasMarkedText];
}
NSRange run_view_marked_range(GUIFormsView* object) {
    return [object answerMarkedRange];
}
NSRange run_view_selected_range(GUIFormsView* object) {
    return [object answerSelectedRange];
}
NSAttributedString* run_view_attributed_substring_for_proposed_range(
    GUIFormsView* object, NSRange range, NSRangePointer actualRange) {
    return [object answerAttributedSubstringForProposedRange:range actualRange:actualRange];
}
NSArray<NSAttributedStringKey>* run_view_valid_attributes_for_marked_text(GUIFormsView* object) {
    return [object answerValidAttributesForMarkedText];
}
NSRect run_view_first_rect_for_character_range(GUIFormsView* object, NSRange range, NSRangePointer actualRange) {
    return [object answerFirstRectForCharacterRange:range actualRange:actualRange];
}
void run_view_do_command_by_selector(GUIFormsView* object, SEL selector) {
    [object answerDoCommandBySelector:selector];
}
} // namespace

namespace gui_forms::host::macos {
// Pulls in the category units' archive members (see the link anchors in
// macos_view.hpp): without these references a program that links the host
// archive directly gets GUIFormsView without its categories.
void link_view_categories() noexcept {
    gui_forms_macos_view_scheduling_anchor();
    gui_forms_macos_view_drawing_anchor();
    gui_forms_macos_view_input_anchor();
    gui_forms_macos_view_accessibility_anchor();
    gui_forms_macos_view_diagnostics_anchor();
}
} // namespace gui_forms::host::macos

@implementation GUIFormsView

- (instancetype)initWithModel:(std::unique_ptr<Window>)model
       transparentFullSizeContent:(BOOL)transparentFullSizeContent
              windowDragRegionIds:(std::vector<std::string>)windowDragRegionIds {
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, 980.0, 680.0)];
    if (self != nil) {
        _model = std::move(model);
        // The availability record (stage DP): AppKit clips each copy to the
        // rectangles being drawn, so the host presents a rectangle set.
        gui_forms::PresentationDeclaration presentation;
        presentation.contract_version = gui_forms::paint_contract_version;
        presentation.extensions.partial_present = true;
        (*_model).declare_presentation(presentation);
        _hostServices = gui_forms::host::make_macos_host_services();
        gui_forms::host::detail::bind_macos_backdrop_view(*_hostServices, self);
        _compositorGeneration = gui_forms::CompositorGeneration{};
        _hostSession = std::make_unique<HostSession>(
            *_model, gui_forms::host::macos_capabilities(), _hostServices.get());
        _nextHostSequence = 1;
        _hostAttached = NO;
        _lastSemanticGeneration = 0;
        _accessibilityCacheKey = MacAccessibilityTreeKey{};
        _initialHostOccluded = YES;
        _scheduledWakeCount = 0;
        _damageCollectionCount = 0;
        _nativeDrawCount = 0;
        _displayTickCount = 0;
        _rasterPreparePhase = {};
        _retainedPaintPhase = {};
        _rasterFinishPhase = {};
        _cgSetupPhase = {};
        _cgDrawPhase = {};
        _cgReleasePhase = {};
        _cpuAttribution = {};
        _causalTrace = nullptr;
        _lastNativeDirtyArea = 0.0;
        _lastFrameDamageBoundsArea = 0.0;
        _lastCGDestinationArea = 0.0;
        _lastCGClipBoundsArea = 0.0;
        _lastCGSourceBytes = 0U;
        _transparentFullSizeContent = transparentFullSizeContent;
        _windowDragRegionIds = std::move(windowDragRegionIds);
        _semanticAccessibilityChildren = nil;
        _publishesLiveRegion = NO;
        _publishesTextRanges = NO;
        _semanticAccessibilityElements = [[NSMutableDictionary alloc] init];
        _semanticAccessibilityElementsByStableId = [[NSMutableDictionary alloc] init];
        _markedText = [[NSMutableAttributedString alloc] init];
        _selectedRange = NSMakeRange(NSNotFound, 0);
        _displayLink = nil;
        _hostOccluded = NO;
        _presentColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        _wakeSource = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        // The source owns its context; its finalizer deletes it.
        WeakViewWork* const wakeWork = new WeakViewWork{self};
        dispatch_set_context(_wakeSource, wakeWork);
        dispatch_set_finalizer_f(_wakeSource, &release_weak_view_work);
        dispatch_source_set_event_handler_f(_wakeSource, &scheduled_wake_on_main);
        dispatch_resume(_wakeSource);
        dispatch_source_set_timer(_wakeSource, DISPATCH_TIME_FOREVER,
                                  DISPATCH_TIME_FOREVER, 0);
        (*_model).set_dispatch_wake_handler(DrainPostedWorkWake(self));
        (*_model).set_paint_wake_handler(CollectDamageWake(self));
        // The raster keeps its pixels between draws, so a scroll's pixels
        // can be moved instead of repainted (drawRetainedRect:).
        (*_model).set_scroll_copies_supported(true);
        (*_model).set_live_surface_idle_wake_handler(CollectDamageWake(self));
        [self setWantsLayer:NO];
        [self registerForDraggedTypes:@[
            NSPasteboardTypeString, NSPasteboardTypeFileURL, @"public.data"]];
        const bool fonts_ready = _raster.register_bundled_fonts(bundle_font_set());
        (*_model).set_text_metrics_provider(fonts_ready ? &_raster : nullptr);
        (*_model).set_framebuffer_painter(fonts_ready ? &_raster : nullptr);
        (*_model).metrics().set_renderer(
            fonts_ready
                ? "Skia CPU m152 · HarfBuzz 14.6.0 · FreeType 2.14.2 · bundled fonts (optional Unicode fallback)"
                : "Skia CPU m152 · incomplete bundled font pack",
            true);
        [[NSNotificationCenter defaultCenter]
            addObserver:self
               selector:@selector(screenParametersChanged:)
                   name:NSApplicationDidChangeScreenParametersNotification
                 object:nil];
        // Posted on the workspace's own centre, never the default one.
        [NSWorkspace.sharedWorkspace.notificationCenter
            addObserver:self
               selector:@selector(accessibilityDisplayOptionsChanged:)
                   name:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                 object:nil];
        // The accent and highlight colours (System Settings, Appearance).
        [[NSNotificationCenter defaultCenter]
            addObserver:self
               selector:@selector(systemColoursChanged:)
                   name:NSSystemColorsDidChangeNotification
                 object:nil];
    }
    return self;
}

- (BOOL)initializeHost {
    if (_hostAttached == YES) return YES;
    const NSRect bounds = self.bounds;
    const double scale = self.window == nil ? 1.0 : self.window.backingScaleFactor;
    const HostDispatchResult result = [self dispatchHostPayload:
        HostAttachEvent{GFSize{std::max(1.0, bounds.size.width),
                               std::max(1.0, bounds.size.height)}, scale}
                               timestampNanoseconds:host_now_nanoseconds()];
    if (!result.accepted()) return NO;
    _hostAttached = YES;
    [self reportInputSettings];
    // Initial ordering can precede attachment, when occlusion notifications
    // cannot enter the host session. Establish native state before scheduling.
    NSWindow* nativeWindow = self.window;
    const bool nativeVisible = nativeWindow != nil && nativeWindow.isVisible &&
        (nativeWindow.occlusionState & NSWindowOcclusionStateVisible) != 0;
    _initialHostOccluded = nativeVisible ? NO : YES;
    [self notifyOcclusion:_initialHostOccluded];
    [self notifyDisplaysChanged];
    [self reportCompositorState];
    [self notifyWindowState];
    [self collectDamage];
    [self startDisplayLinkIfNeeded];
    return YES;
}

- (NSView*)hostContentView {
    const bool nonOpaque = _model != nullptr && (*_model).non_opaque_declared();
    NSView* const content = gui_forms::host::detail::make_macos_content_view(self, nonOpaque);
    return content;
}

- (void)accessibilityDisplayOptionsChanged:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityDisplayOptionsChanged:",
        &run_view_accessibility_display_options_changed, self, notification);
}

- (void)answerAccessibilityDisplayOptionsChanged:(NSNotification*)notification {
    (void)notification;
    [self reportCompositorState];
    [self collectDamage];
}

- (void)systemColoursChanged:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.view.systemColoursChanged:", &run_view_system_colours_changed, self, notification);
}

- (void)answerSystemColoursChanged:(NSNotification*)notification {
    (void)notification;
    [self reportCompositorState];
    [self collectDamage];
}

// The system's Light or Dark reaches the view as a change of its effective
// appearance (the host sets no appearance of its own, so the view follows
// the application's): the state is read again, dark_scheme with it.
- (void)viewDidChangeEffectiveAppearance {
    gui_forms::detail::call_failing_fast(
        "macos.view.viewDidChangeEffectiveAppearance", &run_view_view_did_change_effective_appearance, self);
}

- (void)answerViewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    [self reportCompositorState];
    [self collectDamage];
}

// Reports the window server's state to the window (stage T5, C3): after
// attach, and whenever the accessibility display options, the system
// colours or the effective appearance (Light or Dark) change. A reading
// that differs from the window's last report only by its generation is not
// sent.
- (void)reportCompositorState {
    if (_hostAttached == NO || _model == nullptr) return;
    const gui_forms::CompositorGeneration next{_compositorGeneration.value + 1U};
    const gui_forms::CompositorState state = gui_forms::host::detail::read_macos_compositor_state(next);
    gui_forms::CompositorState unchanged = state;
    unchanged.generation = (*_model).compositor_state().generation;
    if (_compositorGeneration.value != 0U && unchanged == (*_model).compositor_state()) return;
    _compositorGeneration = next;
    static_cast<void>([self dispatchHostPayload:gui_forms::HostCompositorEvent{state}
                           timestampNanoseconds:host_now_nanoseconds()]);
}

- (void)installCloseRequestHandler:(std::function<void(HostCloseRequest&)>)handler {
    _closeRequestSubscription.disconnect();
    if (_hostSession != nullptr && handler) {
        _closeRequestSubscription = (*_hostSession).closing().subscribe(std::move(handler));
    }
}

- (HostDispatchResult)dispatchHostPayload:(HostEventPayload)payload
                         timestampNanoseconds:(std::uint64_t)timestamp {
    if (_hostSession == nullptr) {
        HostDispatchResult result;
        result.error = gui_forms::HostDispatchError::after_shutdown;
        return result;
    }
    HostEvent event;
    event.sequence = _nextHostSequence++;
    event.timestamp_nanoseconds = timestamp;
    event.payload = std::move(payload);
    return (*_hostSession).dispatch(std::move(event));
}

- (BOOL)requestCloseHiding:(BOOL)hiding reason:(HostCloseReason)reason {
    if (_hostAttached == NO) return NO;
    const HostDispatchResult result = [self dispatchHostPayload:
        HostCloseRequest{reason, false, hiding == YES}
                                           timestampNanoseconds:host_now_nanoseconds()];
    return result.accepted() && result.close_allowed;
}

- (BOOL)closeRefusedForReason:(HostCloseReason)reason {
    if (_hostAttached == NO) return NO;
    const HostDispatchResult result = [self dispatchHostPayload:HostCloseRequest{reason, false, false}
                                           timestampNanoseconds:host_now_nanoseconds()];
    const BOOL refused = result.accepted() && !result.close_allowed ? YES : NO;
    return refused;
}

- (void)notifyClosed {
    [self notifyClosedWithReason:HostCloseReason::user];
}
- (void)notifyClosedWithReason:(HostCloseReason)reason {
    if (_hostAttached == NO) return;
    static_cast<void>([self dispatchHostPayload:HostClosedEvent{reason}
                               timestampNanoseconds:host_now_nanoseconds()]);
}

// NSEvent.doubleClickInterval, reported after attach and again on each
// activation if it moved: AppKit posts no notification when the user changes
// it, and the user changes it in System Settings, outside this application.
- (void)reportInputSettings {
    if (_hostAttached == NO) return;
    const NSTimeInterval seconds = NSEvent.doubleClickInterval;
    const double milliseconds = std::isfinite(seconds) ? std::round(seconds * 1000.0) : 0.0;
    const double bounded = std::clamp(milliseconds, 1.0,
                                      static_cast<double>(gui_forms::maximum_double_click_milliseconds));
    const std::uint32_t interval = static_cast<std::uint32_t>(bounded);
    const gui_forms::CaretSettings caret = [self caretSettings];
    if (interval == _reportedDoubleClickMilliseconds && caret == _reportedCaret) return;
    gui_forms::HostInputSettingsEvent input{interval, gui_forms::SettingOrigin::host};
    input.caret = caret;
    const HostDispatchResult reported = [self dispatchHostPayload:input
                                             timestampNanoseconds:host_now_nanoseconds()];
    if (reported.accepted()) {
        _reportedDoubleClickMilliseconds = interval;
        _reportedCaret = caret;
    }
}

// The caret's blink phase (audit N75): the NSTextInsertionPointBlinkPeriodOn
// user default, in milliseconds, where set; a period above the maximum is
// the usual way to stop blinking and reports a caret that never blinks.
// Without the default, GUI.Forms' default with origin default_value.
- (gui_forms::CaretSettings)caretSettings {
    gui_forms::CaretSettings caret{};
    id stored = [NSUserDefaults.standardUserDefaults objectForKey:@"NSTextInsertionPointBlinkPeriodOn"];
    if (![stored isKindOfClass:NSNumber.class]) return caret;
    const double period = [(NSNumber*)stored doubleValue];
    if (!std::isfinite(period) || period <= 0.0) return caret;
    caret.origin = gui_forms::SettingOrigin::host;
    if (period > static_cast<double>(gui_forms::maximum_caret_blink_milliseconds)) {
        caret.blink_milliseconds = 0U;
        return caret;
    }
    const double rounded = std::max(1.0, std::round(period));
    caret.blink_milliseconds = static_cast<std::uint32_t>(rounded);
    return caret;
}

- (void)notifyActivation:(BOOL)active {
    if (_hostAttached == NO) return;
    if (active == YES) [self reportInputSettings];
    static_cast<void>([self dispatchHostPayload:HostActivationEvent{active == YES}
                               timestampNanoseconds:host_now_nanoseconds()]);
    [self collectDamage];
    if (active == YES) [self setNeedsDisplay:YES];
}

- (void)notifyOcclusion:(BOOL)occluded {
    if (_hostAttached == NO) return;
    static_cast<void>([self dispatchHostPayload:HostOcclusionEvent{occluded == YES}
                               timestampNanoseconds:host_now_nanoseconds()]);
    _hostOccluded = occluded;
    if (occluded == YES) {
        [self stopDisplayLink];
        [self armWakeTimer];
    } else {
        // What changed while hidden is in _pendingDamage (an occluded draw
        // keeps it, since the model declines to paint) or still in the model.
        // The raster holds the last presentation everywhere else.
        [self collectDamage];
        // AppKit may consume a display request while our model is still hidden.
        // Rearm native presentation even when no new model damage was produced;
        // with nothing owed, that draw copies the raster and paints nothing.
        [self setNeedsDisplay:YES];
        [self startDisplayLinkIfNeeded];
    }
}

- (void)notifyScaleChanged {
    if (_hostAttached == NO) return;
    const double scale = self.window == nil ? 1.0 : self.window.backingScaleFactor;
    static_cast<void>([self dispatchHostPayload:HostScaleEvent{scale}
                               timestampNanoseconds:host_now_nanoseconds()]);
    [self collectDamage];
}

// Reports the native window's full-screen state (HostWindowStateEvent;
// owner, 2026-11-08: the window outer band) after attach and when AppKit
// enters or leaves full screen. macOS reports no corners (no
// window_corners), so the session's corner request changes nothing here.
// A refused report is tried again on the next change.
- (void)notifyWindowState {
    if (_hostAttached == NO || self.window == nil) return;
    const BOOL fullScreen = ([self.window styleMask] & NSWindowStyleMaskFullScreen) != 0 ? YES : NO;
    if (_windowStateReported == YES && _reportedFullScreen == fullScreen) return;
    const HostDispatchResult reported = [self dispatchHostPayload:gui_forms::HostWindowStateEvent{fullScreen == YES}
                                              timestampNanoseconds:host_now_nanoseconds()];
    if (!reported.accepted()) return;
    _windowStateReported = YES;
    _reportedFullScreen = fullScreen;
    [self collectDamage];
}

- (NSRect)accessibilityFrame {
    return gui_forms::detail::call_failing_fast("macos.view.accessibilityFrame", &run_view_accessibility_frame, self);
}

- (void)notifyDisplaysChanged {
    if (_hostAttached == NO || _hostServices == nullptr) {
        return;
    }
    HostMonitorResult monitors = (*_hostServices).query_monitors();
    if (!monitors.status.accepted()) {
        return;
    }
    static_cast<void>([self dispatchHostPayload:
        HostDisplayEvent{std::move(monitors.monitors)}
                               timestampNanoseconds:host_now_nanoseconds()]);
}

- (void)screenParametersChanged:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.view.screenParametersChanged:", &run_view_screen_parameters_changed, self, notification);
}

- (void)answerScreenParametersChanged:(NSNotification*)notification {
    [self notifyDisplaysChanged];
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    return YES;
}

- (BOOL)resignFirstResponder {
    return YES;
}

- (BOOL)isAccessibilityElement { return YES; }
- (NSString*)accessibilityRole { return NSAccessibilityGroupRole; }
- (NSString*)accessibilityLabel {
    return gui_forms::detail::call_failing_fast("macos.view.accessibilityLabel", &run_view_accessibility_label, self);
}

- (NSArray*)accessibilityChildren {
    return gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityChildren", &run_view_accessibility_children, self);
}

- (NSArray*)accessibilityChildrenInNavigationOrder {
    return gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityChildrenInNavigationOrder",
        &run_view_accessibility_children_in_navigation_order, self);
}

- (NSArray*)accessibilityVisibleChildren {
    return gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityVisibleChildren", &run_view_accessibility_visible_children, self);
}

- (NSRect)accessibilityFrameForSemanticBounds:(GFRect)bounds {
    return gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityFrameForSemanticBounds:",
        &run_view_accessibility_frame_for_semantic_bounds, self, bounds);
}

- (void)viewDidMoveToWindow {
    gui_forms::detail::call_failing_fast("macos.view.viewDidMoveToWindow", &run_view_view_did_move_to_window, self);
}

- (void)answerViewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [self notifyScaleChanged];
    [self notifyDisplaysChanged];
    [self collectDamage];
    [self startDisplayLinkIfNeeded];
}

- (void)viewWillMoveToWindow:(NSWindow*)newWindow {
    gui_forms::detail::call_failing_fast(
        "macos.view.viewWillMoveToWindow:", &run_view_view_will_move_to_window, self, newWindow);
}

- (void)answerViewWillMoveToWindow:(NSWindow*)newWindow {
    if (newWindow == nil && _wakeSource != nil) {
        dispatch_source_set_timer(_wakeSource, DISPATCH_TIME_FOREVER,
                                  DISPATCH_TIME_FOREVER, 0);
    }
    if (newWindow == nil) {
        [_displayLink invalidate];
        _displayLink = nil;
        [self stopInvalidationOverlayTimer];
    }
    [super viewWillMoveToWindow:newWindow];
}

- (void)dealloc {
    gui_forms::detail::call_failing_fast("macos.view.dealloc", &run_view_dealloc, self);
}

- (void)answerDealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
    [self stopDisplayLink];
    [_displayLink invalidate];
    _displayLink = nil;
    [self stopInvalidationOverlayTimer];
    if (_wakeSource != nil) {
        dispatch_source_cancel(_wakeSource);
        _wakeSource = nil;
    }
    if (_presentColorSpace != nullptr) {
        CGColorSpaceRelease(_presentColorSpace);
        _presentColorSpace = nullptr;
    }
    if (_hostSession != nullptr) {
        (*_hostSession).shutdown();
        _hostSession.reset();
    }
    if (_hostServices != nullptr) {
        (*_hostServices).shutdown();
        _hostServices.reset();
    }
    if (_model != nullptr) {
        (*_model).set_text_metrics_provider(nullptr);
        (*_model).set_framebuffer_painter(nullptr);
    }
    _model.reset();
}

- (HostDialogResult)showHostDialog:(const HostDialogRequest&)request {
    if (_hostServices == nullptr) {
        return {{HostServiceError::after_shutdown}, request.request_id,
                HostPathDialogResult{}};
    }
    return (*_hostServices).show_dialog(request);
}

- (HostClipboardTextResult)readHostClipboard {
    if (_hostServices == nullptr) {
        return {{HostServiceError::after_shutdown}, {}, 0, false};
    }
    return (*_hostServices).read_clipboard_text();
}

- (HostServiceStatus)writeHostClipboard:(std::string_view)text {
    if (_hostServices == nullptr) return {HostServiceError::after_shutdown};
    return (*_hostServices).write_clipboard_text(text);
}

- (void)setFrameSize:(NSSize)newSize {
    gui_forms::detail::call_failing_fast("macos.view.setFrameSize:", &run_view_set_frame_size, self, newSize);
}

- (void)answerSetFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (_hostSession) {
        static_cast<void>([self dispatchHostPayload:
            HostResizeEvent{GFSize{newSize.width, newSize.height}}
                                   timestampNanoseconds:host_now_nanoseconds()]);
        [self collectDamage];
    }
}

- (void)updateTrackingAreas {
    gui_forms::detail::call_failing_fast("macos.view.updateTrackingAreas", &run_view_update_tracking_areas, self);
}

- (void)answerUpdateTrackingAreas {
    if (_trackingArea != nil) {
        [self removeTrackingArea:_trackingArea];
    }
    const NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited |
        NSTrackingMouseMoved | NSTrackingCursorUpdate | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    _trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                 options:options
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
    [super updateTrackingAreas];
}

- (void)invalidationOverlayTick {
    gui_forms::detail::call_failing_fast(
        "macos.view.invalidationOverlayTick", &run_view_invalidation_overlay_tick, self);
}

- (void)displayLinkTick {
    gui_forms::detail::call_failing_fast("macos.display_link", &run_display_link_tick, self);
}

- (void)drainPostedWork {
    gui_forms::detail::call_failing_fast("macos.posted_work", &run_posted_work, self);
}

- (void)scheduledWake {
    std::optional<MacCausalWakeScope> causalScope{};
    if (_causalTrace && (*_causalTrace).enabled) causalScope.emplace(*_causalTrace);
    ++_scheduledWakeCount;
    // A libdispatch source's handler: the fail-fast boundary. On the 64-bit
    // runtime it catches an Objective-C exception as well as a C++ one.
    gui_forms::detail::call_failing_fast("macos.scheduled_wake", &run_collect_damage, self);
}

- (BOOL)acceptsWindowRequest {
    if (_hostSession == nullptr) return NO;
    const gui_forms::HostLifecyclePhase phase = (*_hostSession).snapshot().phase;
    const bool live = phase == gui_forms::HostLifecyclePhase::attached ||
                      phase == gui_forms::HostLifecyclePhase::close_authorized;
    const BOOL result = live ? YES : NO;
    return result;
}
- (void)prepareForShutdown {
    [self stopDisplayLink];
    // No compositor report reaches a session that is shutting down.
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:NSSystemColorsDidChangeNotification object:nil];
    if (_wakeSource != nil) {
        dispatch_source_set_timer(_wakeSource, DISPATCH_TIME_FOREVER,
                                  DISPATCH_TIME_FOREVER, 0);
    }
    if (_model) {
        (*_model).set_live_surface_idle_wake_handler({});
        (*_model).set_paint_wake_handler({});
    }
    // The session's shutdown releases capture while the dispatcher still
    // accepts the work controls post about it, then shuts the dispatcher
    // down; without a session the dispatcher is shut down here.
    if (_hostSession) {
        static_cast<void>([self dispatchHostPayload:HostShutdownEvent{}
                                   timestampNanoseconds:host_now_nanoseconds()]);
    }
    if (_model) (*_model).shutdown_dispatcher();
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.view.draggingEntered:", &run_view_dragging_entered, self, sender);
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.view.draggingUpdated:", &run_view_dragging_updated, self, sender);
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
    gui_forms::detail::call_failing_fast("macos.view.draggingExited:", &run_view_dragging_exited, self, sender);
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.view.prepareForDragOperation:", &run_view_prepare_for_drag_operation, self, sender);
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.view.performDragOperation:", &run_view_perform_drag_operation, self, sender);
}

- (void)drawRect:(NSRect)dirtyRect {
    ++_nativeDrawCount;
    gui_forms::detail::call_failing_fast("macos.draw", &run_draw_retained, self, dirtyRect);
}

- (void)cursorUpdate:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.cursorUpdate:", &run_view_cursor_update, self, event);
}

- (void)mouseExited:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseExited:", &run_view_mouse_exited, self, event);
}

- (void)mouseDown:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseDown:", &run_view_mouse_down, self, event);
}

- (void)mouseUp:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseUp:", &run_view_mouse_up, self, event);
}

- (void)rightMouseDown:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.rightMouseDown:", &run_view_right_mouse_down, self, event);
}

- (NSMenu*)menuForEvent:(NSEvent*)event {
    return gui_forms::detail::call_failing_fast("macos.view.menuForEvent:", &run_view_menu_for_event, self, event);
}

- (void)rightMouseUp:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.rightMouseUp:", &run_view_right_mouse_up, self, event);
}

- (void)otherMouseDown:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.otherMouseDown:", &run_view_other_mouse_down, self, event);
}

- (void)otherMouseUp:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.otherMouseUp:", &run_view_other_mouse_up, self, event);
}

- (void)mouseMoved:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseMoved:", &run_view_mouse_moved, self, event);
}

- (void)mouseDragged:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseDragged:", &run_view_mouse_dragged, self, event);
}

- (void)rightMouseDragged:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.rightMouseDragged:", &run_view_right_mouse_dragged, self, event);
}

- (void)otherMouseDragged:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.otherMouseDragged:", &run_view_other_mouse_dragged, self, event);
}

- (void)scrollWheel:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.scrollWheel:", &run_view_scroll_wheel, self, event);
}

- (void)keyDown:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.keyDown:", &run_view_key_down, self, event);
}

- (void)keyUp:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.keyUp:", &run_view_key_up, self, event);
}

- (void)flagsChanged:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.flagsChanged:", &run_view_flags_changed, self, event);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    gui_forms::detail::call_failing_fast(
        "macos.view.insertText:replacementRange:", &run_view_insert_text, self, string, replacementRange);
}

- (void)setMarkedText:(id)string
         selectedRange:(NSRange)selectedRange
       replacementRange:(NSRange)replacementRange {
    gui_forms::detail::call_failing_fast(
        "macos.view.setMarkedText:selectedRange:replacementRange:",
        &run_view_set_marked_text, self, string, selectedRange, replacementRange);
}

- (void)unmarkText {
    gui_forms::detail::call_failing_fast("macos.view.unmarkText", &run_view_unmark_text, self);
}

- (BOOL)hasMarkedText {
    return gui_forms::detail::call_failing_fast("macos.view.hasMarkedText", &run_view_has_marked_text, self);
}

- (NSRange)markedRange {
    return gui_forms::detail::call_failing_fast("macos.view.markedRange", &run_view_marked_range, self);
}

- (NSRange)selectedRange {
    return gui_forms::detail::call_failing_fast("macos.view.selectedRange", &run_view_selected_range, self);
}

- (nullable NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                                        actualRange:(nullable NSRangePointer)actualRange {
    return gui_forms::detail::call_failing_fast(
        "macos.view.attributedSubstringForProposedRange:actualRange:",
        &run_view_attributed_substring_for_proposed_range, self, range, actualRange);
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    return gui_forms::detail::call_failing_fast(
        "macos.view.validAttributesForMarkedText", &run_view_valid_attributes_for_marked_text, self);
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                          actualRange:(nullable NSRangePointer)actualRange {
    return gui_forms::detail::call_failing_fast(
        "macos.view.firstRectForCharacterRange:actualRange:",
        &run_view_first_rect_for_character_range, self, range, actualRange);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return NSNotFound;
}

- (void)doCommandBySelector:(SEL)selector {
    gui_forms::detail::call_failing_fast(
        "macos.view.doCommandBySelector:", &run_view_do_command_by_selector, self, selector);
}

@end
