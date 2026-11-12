#pragma once

// GUIFormsView: the NSView that hosts one GUI.Forms Window, its host
// session and its CPU raster. One class, implemented across these units:
//
//   macos_view.mm                the primary implementation: construction,
//                                attach, the host session's reports, window
//                                lifecycle, shutdown, and every native entry
//                                AppKit calls, each through the fail-fast
//                                boundary (call_failing_fast) to its answer;
//   macos_view_scheduling.mm     (Scheduling) damage collection, the display
//                                link, posted work and the wake timer;
//   macos_view_drawing.mm        (Drawing) the retained draw and its
//                                presentation, live surface reports, the
//                                invalidation overlay and frame capture;
//   macos_view_input.mm          (Input) pointer, keyboard, text input and
//                                drag destination;
//   macos_view_accessibility.mm  (Accessibility) the view's own element and
//                                the semantic queries its elements make;
//   macos_view_diagnostics.mm    (Diagnostics) metrics, host JSON and the
//                                private CPU and causal traces.
//
// Every message is sent on the main thread.

#include "../application/macos_host_private.hpp"
#include "../accessibility/macos_accessibility_element.hpp"
#include "macos_view_diagnostics.hpp"

@interface GUIFormsView : NSView <NSTextInputClient, NSDraggingDestination> {
    std::unique_ptr<Window> _model;
    std::unique_ptr<HostSession> _hostSession;
    std::unique_ptr<HostServices> _hostServices;
    // The last compositor report's generation; 0 before the first (T5).
    gui_forms::CompositorGeneration _compositorGeneration;
    gui_forms::SubscriptionToken _closeRequestSubscription;
    std::uint64_t _nextHostSequence;
    BOOL _hostAttached;
    // The double-click interval last reported, in milliseconds; 0 (as the
    // runtime zeroes ivars) before the first report.
    std::uint32_t _reportedDoubleClickMilliseconds;
    // The caret settings last reported (audit N75).
    gui_forms::CaretSettings _reportedCaret;
    SkiaRaster _raster;
    BOOL _rasterHasContent;
    DamageRegion _pendingDamage;
    // Scroll copies taken from the model and not yet applied to the raster,
    // in order (SurfaceRoot::take_scroll_copies); scratch for the take; the
    // clips of copies the current draw applied, damaged again if its frame
    // is not presented.
    std::vector<gui_forms::ScrollCopy> _pendingCopies;
    std::vector<gui_forms::ScrollCopy> _takenCopies;
    std::vector<GFRect> _appliedCopyClips;
    NSMutableAttributedString* _markedText;
    NSRange _selectedRange;
    NSTrackingArea* _trackingArea;
    dispatch_source_t _wakeSource;
    // The sRGB colour space every present tags the raster with: immutable,
    // made once per view instead of once per draw, released in dealloc.
    CGColorSpaceRef _presentColorSpace;
    std::unique_ptr<MacDisplayLinkTrace> _displayTrace;
    CADisplayLink* _displayLink;
    BOOL _hostOccluded;
    // The full-screen state the session last accepted (notifyWindowState).
    BOOL _windowStateReported;
    BOOL _reportedFullScreen;
    std::vector<LiveSurfacePresentation> _pendingLivePresentations;
    // Objective-C++ constructs these vectors empty with the view. Reuse storage
    // across ticks; immutable leases survive producer reconfiguration until
    // this pass has finished consuming their pixels.
    std::vector<gui_forms::LiveSurfaceFrame> _liveFrames;
    std::vector<GFRect> _opaqueLiveClips;
    // Per pending presentation: the index of the frame it draws (fragments of
    // one surface share one acquisition) and the part of its clip to copy.
    std::vector<std::size_t> _liveFrameSources;
    std::vector<GFRect> _liveRegions;
    // Per acquired frame (indexed like _liveFrames): its generation, kept
    // after the frames are released so that a presented pass can report
    // what it showed (LiveSurface::notify_presented); 0 for no frame.
    std::vector<std::uint64_t> _liveFrameGenerations;
    std::vector<GFRect> _uncoveredDamage;
    // Per drawRect: on a reusable raster, the pending live presentations'
    // destinations and the parts of the uncovered damage inside them.
    std::vector<GFRect> _liveDestinations;
    std::vector<GFRect> _liveExposure;
    // A presentation region was taken but its pixels never reached a
    // presented raster. The next pass copies complete clips.
    BOOL _liveRegionLost;
    // The surface epoch of the receipt the raster's front presents; 0 before
    // the first presented frame.
    std::uint64_t _frontSurfaceEpoch;
    std::uint64_t _lastSemanticGeneration;
    // The key the cached accessibility children were built under; read only
    // while _semanticAccessibilityChildren is not nil.
    MacAccessibilityTreeKey _accessibilityCacheKey;
    // Whether the last published tree held a live region: a semantic change
    // then republishes the tree at once, so that a changed announcement is
    // posted when it changes rather than when a client next reads.
    BOOL _publishesLiveRegion;
    // Whether the last published tree held a text node with text ranges: a
    // semantic change then republishes the tree at once, so that its
    // element posts a changed value or selection when it changes.
    BOOL _publishesTextRanges;
    BOOL _initialHostOccluded;
    std::uint64_t _scheduledWakeCount;
    std::uint64_t _damageCollectionCount;
    std::uint64_t _nativeDrawCount;
    std::uint64_t _displayTickCount;
    MacPaintPhase _rasterPreparePhase;
    MacPaintPhase _retainedPaintPhase;
    MacPaintPhase _rasterFinishPhase;
    MacPaintPhase _cgSetupPhase;
    MacPaintPhase _cgDrawPhase;
    MacPaintPhase _cgReleasePhase;
    MacCpuAttribution _cpuAttribution;
    std::unique_ptr<MacCausalTrace> _causalTrace;
    double _lastNativeDirtyArea;
    double _lastFrameDamageBoundsArea;
    double _lastCGDestinationArea;
    double _lastCGClipBoundsArea;
    std::size_t _lastCGSourceBytes;
    NSArray* _semanticAccessibilityChildren;
    // The published elements by accessibility_element_key, and the first
    // element for each stable identity (the tab-to-panel relation).
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>*
        _semanticAccessibilityElements;
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>*
        _semanticAccessibilityElementsByStableId;
    BOOL _transparentFullSizeContent;
    std::vector<std::string> _windowDragRegionIds;
    // A Control-click is AppKit's context-menu request, not a press: its
    // drags and release are not dispatched either.
    BOOL _controlClickInProgress;
    // Portable frame capture (capture_path host option). Completed once,
    // after the first presented frame whose dirty rect covers the view.
    std::string _frameCapturePath;
    BOOL _frameCaptureCompleted;
    BOOL _frameCaptureSaved;
    // The invalidation overlay's present-time storage (gui_forms/diagnostics/
    // invalidation_overlay), reused: the scratch frame the outlines are
    // composited in (sized on first use), the device areas they cover now,
    // those they covered at the last fade tick, and the fade timer, nil
    // while nothing shows. The timer retains a weak proxy, not the view.
    std::vector<std::byte> _overlayScratch;
    std::vector<gui_forms::DevicePixelRect> _overlayRegions;
    std::vector<gui_forms::DevicePixelRect> _overlayDrawn;
    NSTimer* _overlayTimer;
}
- (instancetype)initWithModel:(std::unique_ptr<Window>)model
       transparentFullSizeContent:(BOOL)transparentFullSizeContent
              windowDragRegionIds:(std::vector<std::string>)windowDragRegionIds;
- (BOOL)initializeHost;
- (void)installCloseRequestHandler:(std::function<void(HostCloseRequest&)>)handler;
- (HostDispatchResult)dispatchHostPayload:(HostEventPayload)payload
                         timestampNanoseconds:(std::uint64_t)timestamp;
- (BOOL)requestCloseHiding:(BOOL)hiding reason:(HostCloseReason)reason;
// Whether the closing handler refuses a close for `reason`, asked without
// closing (audit P2). A detached host, or a request nothing accepted,
// refuses nothing, as in the headless host's session end.
- (BOOL)closeRefusedForReason:(HostCloseReason)reason;
- (void)notifyClosed;
- (void)notifyClosedWithReason:(HostCloseReason)reason;
- (void)notifyActivation:(BOOL)active;
- (void)notifyOcclusion:(BOOL)occluded;
- (void)notifyScaleChanged;
- (void)notifyWindowState;
- (void)notifyDisplaysChanged;
- (void)screenParametersChanged:(NSNotification*)notification;
// Stage T5: the accessibility display options changed (reduce
// transparency); the compositor state is reported again.
- (void)accessibilityDisplayOptionsChanged:(NSNotification*)notification;
- (void)systemColoursChanged:(NSNotification*)notification;
- (void)reportCompositorState;
// The view the native window shows: this view, or the backdrop container
// holding it for a window declared non-opaque (make_macos_content_view).
// Called once, when the window is made.
- (NSView*)hostContentView;
- (void)drainPostedWork;
- (void)scheduledWake;
- (void)displayLinkTick;
- (void)invalidationOverlayTick;
- (void)prepareForShutdown;
- (BOOL)acceptsWindowRequest;
- (HostDialogResult)showHostDialog:(const HostDialogRequest&)request;
- (HostClipboardTextResult)readHostClipboard;
- (HostServiceStatus)writeHostClipboard:(std::string_view)text;
- (NSRect)accessibilityFrameForSemanticBounds:(GFRect)bounds;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast). The answers that live in a category are
// declared with it.
- (void)answerAccessibilityDisplayOptionsChanged:(NSNotification*)notification;
- (void)answerSystemColoursChanged:(NSNotification*)notification;
- (void)answerViewDidChangeEffectiveAppearance;
- (void)answerScreenParametersChanged:(NSNotification*)notification;
- (void)answerViewDidMoveToWindow;
- (void)answerViewWillMoveToWindow:(NSWindow*)newWindow;
- (void)answerDealloc;
- (void)answerSetFrameSize:(NSSize)newSize;
- (void)answerUpdateTrackingAreas;
@end

// Damage collection, the display link, posted work and the wake timer
// (macos_view_scheduling.mm).
@interface GUIFormsView (Scheduling)
- (void)collectDamage;
- (void)takeScrollCopies;
- (void)armWakeTimer;
- (void)startDisplayLinkIfNeeded;
- (void)stopDisplayLink;
- (void)setDisplayTraceEnabled:(BOOL)enabled;
- (MacDisplayLinkTrace)displayTraceSnapshot;
- (void)answerDisplayLinkTick;
- (void)answerDrainPostedWork;
@end

// The retained draw and its presentation, the invalidation overlay and frame
// capture (macos_view_drawing.mm).
@interface GUIFormsView (Drawing)
- (void)installFrameCapturePath:(std::string)path;
- (BOOL)frameCaptureSaved;
- (void)completeFrameCaptureIfDue:(NSRect)dirtyRect presented:(BOOL)presented;
- (void)drawRetainedRect:(NSRect)dirtyRect;
- (void)armInvalidationOverlayTimer;
- (void)stopInvalidationOverlayTimer;
- (void)answerInvalidationOverlayTick;
@end

// Pointer, keyboard, text input and drag destination (macos_view_input.mm).
@interface GUIFormsView (Input)
- (DragEvent)dragEventFor:(id<NSDraggingInfo>)sender action:(DragAction)action;
// macOS's own keyboard context request (owner's ruling, 2026-11-08): the
// accessibility show-menu action (NSAccessibilityShowMenuAction, VoiceOver
// VO+Shift+M) on an element with no menu of its own, reported to the
// window as a keyboard HostContextRequestEvent after the element's control
// took focus. Returns whether a control handled it.
- (BOOL)dispatchKeyboardContextRequest;
- (NSDragOperation)answerDraggingEntered:(id<NSDraggingInfo>)sender;
- (NSDragOperation)answerDraggingUpdated:(id<NSDraggingInfo>)sender;
- (void)answerDraggingExited:(id<NSDraggingInfo>)sender;
- (BOOL)answerPrepareForDragOperation:(id<NSDraggingInfo>)sender;
- (BOOL)answerPerformDragOperation:(id<NSDraggingInfo>)sender;
- (void)answerCursorUpdate:(NSEvent*)event;
- (void)answerMouseExited:(NSEvent*)event;
- (void)answerMouseDown:(NSEvent*)event;
- (void)answerMouseUp:(NSEvent*)event;
- (void)answerRightMouseDown:(NSEvent*)event;
- (NSMenu*)answerMenuForEvent:(NSEvent*)event;
- (void)answerRightMouseUp:(NSEvent*)event;
- (void)answerOtherMouseDown:(NSEvent*)event;
- (void)answerOtherMouseUp:(NSEvent*)event;
- (void)answerMouseMoved:(NSEvent*)event;
- (void)answerMouseDragged:(NSEvent*)event;
- (void)answerRightMouseDragged:(NSEvent*)event;
- (void)answerOtherMouseDragged:(NSEvent*)event;
- (void)answerScrollWheel:(NSEvent*)event;
- (void)answerKeyDown:(NSEvent*)event;
- (void)answerKeyUp:(NSEvent*)event;
- (void)answerFlagsChanged:(NSEvent*)event;
- (void)answerInsertText:(id)string replacementRange:(NSRange)replacementRange;
- (void)answerSetMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange;
- (void)answerUnmarkText;
- (BOOL)answerHasMarkedText;
- (NSRange)answerMarkedRange;
- (NSRange)answerSelectedRange;
- (nullable NSAttributedString*)answerAttributedSubstringForProposedRange:(NSRange)range
        actualRange:(nullable NSRangePointer)actualRange;
- (NSArray<NSAttributedStringKey>*)answerValidAttributesForMarkedText;
- (NSRect)answerFirstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange;
- (void)answerDoCommandBySelector:(SEL)selector;
@end

// The view's own accessibility element and the semantic queries its
// published elements make (macos_view_accessibility.mm).
@interface GUIFormsView (Accessibility)
- (NSRect)answerAccessibilityFrame;
- (NSString*)answerAccessibilityLabel;
- (NSArray*)answerAccessibilityChildren;
- (NSArray*)answerAccessibilityChildrenInNavigationOrder;
- (NSArray*)answerAccessibilityVisibleChildren;
- (NSRect)answerAccessibilityFrameForSemanticBounds:(GFRect)bounds;
// Performs `action` on a published node named as the shared routing API
// names it: the runtime identity of its control and, for a virtual child,
// the child's stable identity (empty for the control's own node). A virtual
// child with no exposed control above it (control 0) is named by its stable
// identity alone.
- (BOOL)performSemanticAction:(SemanticAction)action
                      control:(std::uint64_t)control
                 virtualChild:(const std::string&)virtualChild
                     stableId:(const std::string&)stableId
                        value:(NSString*)value;
- (id)semanticAccessibilityElementForStableId:(NSString*)stableId;
// The text queries of owner decision A-T1 for the control `control`
// (Window::semantic_text_layout and Window::set_semantic_text_selection).
// Each returns NO when the window has gone or the control refuses.
- (BOOL)readSemanticTextLayout:(std::uint64_t)control layout:(SemanticTextLayout&)layout;
- (BOOL)selectSemanticText:(std::uint64_t)control selection:(SemanticTextSelection)selection;
// The window's accessibility tree key now (read_accessibility_tree_key);
// NO when the window has gone.
- (BOOL)readAccessibilityTreeKey:(MacAccessibilityTreeKey&)key;
// A point in screen coordinates (AppKit's, origin at the bottom left) in
// the view's semantic coordinates.
- (NSPoint)semanticPointForScreenPoint:(NSPoint)point;
@end

// The metrics and host JSON a run prints and snapshots, and the private CPU
// attribution and causal wake trace (macos_view_diagnostics.mm).
@interface GUIFormsView (Diagnostics)
- (std::string)metricsJSON;
- (std::string)hostJSON;
- (BOOL)setCpuAttributionEnabled:(const BOOL)enabled;
- (std::array<std::uint64_t, 13>)cpuAttributionSnapshot;
- (MacCausalSnapshot)cpuCausalTraceSnapshot;
@end
