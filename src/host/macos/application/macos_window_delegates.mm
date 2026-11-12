// The native window and application delegates of an AppKit run
// (macos_window_delegates.hpp).

#include "macos_window_delegates.hpp"
#include "../view/macos_view.hpp"

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsActivationObserver's native entries: each sends the entry's answer message.
void run_activation_application_did_become_active(GUIFormsActivationObserver* object, NSNotification* notification) {
    [object answerApplicationDidBecomeActive:notification];
}
void run_activation_application_did_resign_active(GUIFormsActivationObserver* object, NSNotification* notification) {
    [object answerApplicationDidResignActive:notification];
}
} // namespace

@implementation GUIFormsActivationObserver
- (instancetype)initWithRun:(gui_forms::host::MacApplicationRun*)run {
    self = [super init];
    if (self != nil) {
        _run = run;
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        [center addObserver:self selector:@selector(applicationDidBecomeActive:)
                       name:NSApplicationDidBecomeActiveNotification object:nil];
        [center addObserver:self selector:@selector(applicationDidResignActive:)
                       name:NSApplicationDidResignActiveNotification object:nil];
    }
    return self;
}
- (void)applicationDidBecomeActive:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.activation.applicationDidBecomeActive:",
        &run_activation_application_did_become_active, self, notification);
}

- (void)answerApplicationDidBecomeActive:(NSNotification*)notification {
    gui_forms::host::mac_run_report_active(_run, true);
}
- (void)applicationDidResignActive:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.activation.applicationDidResignActive:",
        &run_activation_application_did_resign_active, self, notification);
}

- (void)answerApplicationDidResignActive:(NSNotification*)notification {
    gui_forms::host::mac_run_report_active(_run, false);
}
- (void)stopObserving {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _run = nullptr;
}
@end

namespace gui_forms::host {

void finish_run(MacRunEnd& end) {
    if (end.finished || end.finish == nullptr) return;
    end.finished = true;
    (*end.finish)(end.context);
}

} // namespace gui_forms::host

namespace {
// The named targets the fail-fast boundary calls for the run's application
// callbacks (windowWillClose:, application:openURLs:).
void run_closed_handler(std::function<void()>& callback) {
    callback();
}
void run_open_files(const std::function<void(std::vector<std::string>)>& open_files,
                    std::vector<std::string>& paths) {
    open_files(std::move(paths));
}
} // namespace

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsWindowDelegate's native entries: each sends the entry's answer message.
void run_window_delegate_window_did_move(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidMove:notification];
}
void run_window_delegate_window_did_resize(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidResize:notification];
}
BOOL run_window_delegate_window_should_close(GUIFormsWindowDelegate* object, NSWindow* sender) {
    return [object answerWindowShouldClose:sender];
}
void run_window_delegate_window_did_become_key(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidBecomeKey:notification];
}
void run_window_delegate_window_did_resign_key(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidResignKey:notification];
}
void run_window_delegate_window_did_change_occlusion_state(
    GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidChangeOcclusionState:notification];
}
void run_window_delegate_window_did_miniaturize(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidMiniaturize:notification];
}
void run_window_delegate_window_did_deminiaturize(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidDeminiaturize:notification];
}
void run_window_delegate_window_did_change_backing_properties(
    GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidChangeBackingProperties:notification];
}
void run_window_delegate_window_did_enter_full_screen(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidEnterFullScreen:notification];
}
void run_window_delegate_window_did_exit_full_screen(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidExitFullScreen:notification];
}
} // namespace

@implementation GUIFormsWindowDelegate
- (instancetype)initWithView:(GUIFormsView*)view
      stopsApplicationOnClose:(BOOL)stopsApplicationOnClose
                  hideOnClose:(BOOL)hideOnClose
                 closedHandler:(std::function<void()>)closedHandler {
    self = [super init];
    if (self != nil) {
        _view = view;
        _stopsApplicationOnClose = stopsApplicationOnClose;
        _hideOnClose = hideOnClose;
        _closedHandler = std::move(closedHandler);
        _run = nullptr;
        _windowId = gui_forms::HostWindowId{};
        _closeReason = HostCloseReason::user;
    }
    return self;
}
- (void)attachRun:(gui_forms::host::MacApplicationRun*)run
           window:(gui_forms::HostWindowId)window {
    _run = run;
    _windowId = window;
}
- (void)closeWindowForApplication:(NSWindow*)window {
    _hideOnClose = NO;
    _closeReason = HostCloseReason::application;
    // NSWindow close does not ask windowShouldClose:; windowWillClose: runs
    // before it returns.
    [window close];
}
- (void)windowDidMove:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidMove:", &run_window_delegate_window_did_move, self, notification);
}

- (void)answerWindowDidMove:(NSNotification*)notification {
    gui_forms::host::mac_run_report_frame(_run, _windowId);
}
- (void)windowDidResize:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidResize:", &run_window_delegate_window_did_resize, self, notification);
}

- (void)answerWindowDidResize:(NSNotification*)notification {
    gui_forms::host::mac_run_report_frame(_run, _windowId);
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowShouldClose:", &run_window_delegate_window_should_close, self, sender);
}

- (BOOL)answerWindowShouldClose:(NSWindow*)sender {
    if (_view != nil && ![_view requestCloseHiding:_hideOnClose reason:HostCloseReason::user]) return NO;
    if (_hideOnClose) {
        [sender orderOut:nil];
        gui_forms::host::mac_run_hidden_on_close(_run, _windowId);
        return NO;
    }
    return YES;
}
- (void)prepareForApplicationShutdown {
    _hideOnClose = NO;
}
- (void)windowWillClose:(NSNotification*)notification {
    [_view notifyClosedWithReason:_closeReason];
    // Retired before the closed callback, so the callback may open a
    // replacement under the same stable id.
    gui_forms::host::mac_run_retire(_run, _windowId);
    if (_closedHandler) {
        std::function<void()> callback = std::move(_closedHandler);
        _closedHandler = {};
        // Application code (audit P1): through the fail-fast boundary.
        gui_forms::detail::call_failing_fast("macos.window_will_close", &run_closed_handler, callback);
    }
    gui_forms::host::mac_run_close_finished(_run, _windowId);
    if (!_stopsApplicationOnClose) return;
    [NSApp stop:nil];
    NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:wake atStart:NO];
}
- (void)windowDidBecomeKey:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidBecomeKey:", &run_window_delegate_window_did_become_key, self, notification);
}

- (void)answerWindowDidBecomeKey:(NSNotification*)notification {
    [_view notifyActivation:YES];
}
- (void)windowDidResignKey:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidResignKey:", &run_window_delegate_window_did_resign_key, self, notification);
}

- (void)answerWindowDidResignKey:(NSNotification*)notification {
    [_view notifyActivation:NO];
}
- (void)windowDidChangeOcclusionState:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidChangeOcclusionState:",
        &run_window_delegate_window_did_change_occlusion_state, self, notification);
}

- (void)answerWindowDidChangeOcclusionState:(NSNotification*)notification {
    NSWindow* window = notification.object;
    const BOOL visible = (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
    [_view notifyOcclusion:!visible];
}
- (void)windowDidMiniaturize:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidMiniaturize:", &run_window_delegate_window_did_miniaturize, self, notification);
}

- (void)answerWindowDidMiniaturize:(NSNotification*)notification {
    gui_forms::host::mac_run_report_minimized(_run, _windowId, true);
}
- (void)windowDidDeminiaturize:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidDeminiaturize:",
        &run_window_delegate_window_did_deminiaturize, self, notification);
}

- (void)answerWindowDidDeminiaturize:(NSNotification*)notification {
    [_view setNeedsDisplay:YES];
    gui_forms::host::mac_run_report_minimized(_run, _windowId, false);
}
- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidChangeBackingProperties:",
        &run_window_delegate_window_did_change_backing_properties, self, notification);
}

- (void)answerWindowDidChangeBackingProperties:(NSNotification*)notification {
    [_view notifyScaleChanged];
}
- (void)windowDidEnterFullScreen:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidEnterFullScreen:",
        &run_window_delegate_window_did_enter_full_screen, self, notification);
}

- (void)answerWindowDidEnterFullScreen:(NSNotification*)notification {
    [_view notifyWindowState];
}
- (void)windowDidExitFullScreen:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidExitFullScreen:",
        &run_window_delegate_window_did_exit_full_screen, self, notification);
}

- (void)answerWindowDidExitFullScreen:(NSNotification*)notification {
    [_view notifyWindowState];
}
- (gui_forms::host::MacTerminationClose)closeForTermination:(NSWindow*)window
                                                     reason:(HostCloseReason)reason {
    if (!_stopsApplicationOnClose) return gui_forms::host::MacTerminationClose::not_ending;
    // Asked directly rather than through performClose:, which beeps and does
    // nothing for a window without a close button, or windowShouldClose:,
    // which would report a user close.
    if (_view != nil && ![_view requestCloseHiding:NO reason:reason]) {
        return gui_forms::host::MacTerminationClose::refused;
    }
    _hideOnClose = NO;
    _closeReason = reason;
    // NSWindow close does not ask windowShouldClose:; windowWillClose:
    // reports _closeReason before it returns.
    [window close];
    return gui_forms::host::MacTerminationClose::closed;
}
- (BOOL)refusesTermination:(HostCloseReason)reason {
    if (_stopsApplicationOnClose || _view == nil) return NO;
    const BOOL refused = [_view closeRefusedForReason:reason];
    return refused;
}
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsTerminationDelegate's native entries: each sends the entry's answer message.
NSApplicationTerminateReply run_termination_application_should_terminate(
    GUIFormsTerminationDelegate* object, NSApplication* sender) {
    return [object answerApplicationShouldTerminate:sender];
}
} // namespace

@implementation GUIFormsTerminationDelegate
- (instancetype)initWithEnd:(gui_forms::host::MacRunEnd*)end {
    self = [self initWithEnd:end openFiles:nullptr];
    return self;
}
- (instancetype)initWithEnd:(gui_forms::host::MacRunEnd*)end
                  openFiles:(const std::function<void(std::vector<std::string>)>*)openFiles {
    self = [super init];
    if (self != nil) {
        _end = end;
        _openFiles = openFiles;
    }
    return self;
}
- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
    static_cast<void>(application);
    if (_openFiles == nullptr || !(*_openFiles)) return;
    std::vector<std::string> paths{};
    paths.reserve(static_cast<std::size_t>([urls count]));
    for (NSURL* url in urls) {
        if (![url isFileURL]) continue;
        // The file system's own spelling of the path, in UTF-8 on macOS.
        const char* path = [url fileSystemRepresentation];
        if (path == nullptr) continue;
        paths.emplace_back(path);
    }
    if (paths.empty()) return;
    // Application code (audit P1): through the fail-fast boundary.
    gui_forms::detail::call_failing_fast("macos.open_urls", &run_open_files, *_openFiles, paths);
}
// The reason of the quit request being handled: session_end when the quit
// Apple event says the session is ending (log out, restart, shut down, with
// or without the system's confirmation dialog), otherwise quit (Cmd-Q, the
// Dock, another application's plain quit event, or no Apple event at all).
static HostCloseReason termination_close_reason() {
    NSAppleEventDescriptor* event = [[NSAppleEventManager sharedAppleEventManager] currentAppleEvent];
    if (event == nil) return HostCloseReason::quit;
    NSAppleEventDescriptor* reason = [event attributeDescriptorForKeyword:kAEQuitReason];
    if (reason == nil) return HostCloseReason::quit;
    // The reason is an enumerated value; some senders type it as a type code.
    OSType code = [reason enumCodeValue];
    if (code == 0) code = [reason typeCodeValue];
    switch (code) {
        case kAELogOut:
        case kAEReallyLogOut:
        case kAEShowRestartDialog:
        case kAERestart:
        case kAEShowShutdownDialog:
        case kAEShutDown:
            return HostCloseReason::session_end;
        default:
            return HostCloseReason::quit;
    }
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.termination.applicationShouldTerminate:", &run_termination_application_should_terminate, self, sender);
}

- (NSApplicationTerminateReply)answerApplicationShouldTerminate:(NSApplication*)sender {
    const HostCloseReason reason = termination_close_reason();
    // First every window that does not end the run is asked, in NSApp's
    // window order, and the first refusal cancels the termination (audit
    // P2). A copy is walked: a closing handler may open or close windows.
    NSArray<NSWindow*>* asked = [NSArray arrayWithArray:[sender windows]];
    for (NSWindow* window in asked) {
        const id delegate = [window delegate];
        if (![delegate isKindOfClass:[GUIFormsWindowDelegate class]]) continue;
        GUIFormsWindowDelegate* windowDelegate = static_cast<GUIFormsWindowDelegate*>(delegate);
        if ([windowDelegate refusesTermination:reason]) return NSTerminateCancel;
    }
    for (NSWindow* window in [sender windows]) {
        const id delegate = [window delegate];
        if (![delegate isKindOfClass:[GUIFormsWindowDelegate class]]) continue;
        GUIFormsWindowDelegate* windowDelegate = static_cast<GUIFormsWindowDelegate*>(delegate);
        const gui_forms::host::MacTerminationClose outcome =
            [windowDelegate closeForTermination:window reason:reason];
        if (outcome == gui_forms::host::MacTerminationClose::not_ending) continue;
        if (outcome == gui_forms::host::MacTerminationClose::refused) return NSTerminateCancel;
        if (_end != nullptr) gui_forms::host::finish_run(*_end);
        return NSTerminateNow;
    }
    return NSTerminateNow;
}
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsPopupPanel's native entries: each sends the entry's answer message.
BOOL run_popup_panel_can_become_key_window(GUIFormsPopupPanel* object) {
    return [object answerCanBecomeKeyWindow];
}
} // namespace

@implementation GUIFormsPopupPanel
- (instancetype)initWithContentRect:(NSRect)contentRect takesFocus:(BOOL)takesFocus {
    // A Splash (no input) is made with takesFocus NO and then told to
    // ignore the mouse (MacApplicationRun::open).
    const NSWindowStyleMask style = NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel;
    self = [super initWithContentRect:contentRect styleMask:style backing:NSBackingStoreBuffered defer:NO];
    if (self != nil) _takesFocus = takesFocus;
    return self;
}
- (BOOL)canBecomeKeyWindow {
    return gui_forms::detail::call_failing_fast(
        "macos.popup_panel.canBecomeKeyWindow", &run_popup_panel_can_become_key_window, self);
}

- (BOOL)answerCanBecomeKeyWindow {
    return _takesFocus;
}
- (BOOL)canBecomeMainWindow {
    return NO;
}
@end
