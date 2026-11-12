#pragma once

// The native window and application delegates of an AppKit run
// (macos_window_delegates.mm): the window delegate that reports moves,
// keys, occlusion and closes; the activation observer; the application
// delegate that handles quit requests and opened files; and the popup
// panel.

#include "macos_host_private.hpp"

@class GUIFormsView;

namespace gui_forms::host {
// Application-scope windowing (macos_application_run.mm). The delegate
// reaches the run only through these, while the run lives.
class MacApplicationRun;
void mac_run_report_frame(MacApplicationRun* run, gui_forms::HostWindowId window);
void mac_run_retire(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept;
void mac_run_close_finished(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept;
void mac_run_report_minimized(MacApplicationRun* run, gui_forms::HostWindowId window,
                              bool minimized);
void mac_run_hidden_on_close(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept;
void mac_run_report_active(MacApplicationRun* run, bool active);
} // namespace gui_forms::host

// Reports application activation to the run's floating policy
// (NSApplicationDidBecomeActive and DidResignActive). Registered for the run
// and removed before the run ends.
@interface GUIFormsActivationObserver : NSObject {
    gui_forms::host::MacApplicationRun* _run;
}
- (instancetype)initWithRun:(gui_forms::host::MacApplicationRun*)run;
- (void)applicationDidBecomeActive:(NSNotification*)notification;
- (void)applicationDidResignActive:(NSNotification*)notification;
- (void)stopObserving;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerApplicationDidBecomeActive:(NSNotification*)notification;
- (void)answerApplicationDidResignActive:(NSNotification*)notification;
@end

namespace gui_forms::host {

// What a quit request did to one window (closeForTermination:).
enum class MacTerminationClose : std::uint8_t {
    // The window does not end the run; the request is for another window.
    not_ending,
    // The closing handler refused; the run goes on.
    refused,
    // The window has closed, so the run is over.
    closed,
};

// The end of a run: what it does once the window that ends it has closed
// (close metrics, final snapshots, the capture check, shutdown). It runs
// once: after [NSApp run] returns or, for a quit request, from the
// application delegate before AppKit ends the process.
struct MacRunEnd final {
    void (*finish)(void* context){};
    void* context{};
    bool finished{};
};

void finish_run(MacRunEnd& end);

} // namespace gui_forms::host

@interface GUIFormsWindowDelegate : NSObject <NSWindowDelegate> {
    __weak GUIFormsView* _view;
    BOOL _stopsApplicationOnClose;
    BOOL _hideOnClose;
    std::function<void()> _closedHandler;
    // The run this window belongs to (observing; the run owns the window and
    // this delegate) and the window's identity in it. Null for run_macos.
    gui_forms::host::MacApplicationRun* _run;
    gui_forms::HostWindowId _windowId;
    // The reason the closed event reports: user for every native close,
    // application when the run destroys the window, quit or session_end
    // when a quit request closes it (closeForTermination:reason:).
    HostCloseReason _closeReason;
}
- (instancetype)initWithView:(GUIFormsView*)view
      stopsApplicationOnClose:(BOOL)stopsApplicationOnClose
                  hideOnClose:(BOOL)hideOnClose
                 closedHandler:(std::function<void()>)closedHandler;
- (void)prepareForApplicationShutdown;
- (void)attachRun:(gui_forms::host::MacApplicationRun*)run
           window:(gui_forms::HostWindowId)window;
// Closes without asking the closing handler (HostWindowing::destroy_window).
- (void)closeWindowForApplication:(NSWindow*)window;
// A quit request (Cmd-Q, the Dock, a quit Apple event, a log-out): for the
// window that ends the run, asks the closing handler with reason (quit, or
// session_end for a log-out, restart or shut-down) and closes the window
// when it agrees. A hide-on-close window closes rather than hides.
- (gui_forms::host::MacTerminationClose)closeForTermination:(NSWindow*)window
                                                     reason:(HostCloseReason)reason;
// Before a quit request closes the run-ending window, every other window's
// closing handler is asked with the same reason, without closing it (audit
// P2), so a secondary window with unsaved work can cancel a quit or a
// log-out, as each window can on Windows (WM_QUERYENDSESSION) and in the
// headless host. NO for the run-ending window, which closeForTermination:
// asks.
- (BOOL)refusesTermination:(HostCloseReason)reason;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerWindowDidMove:(NSNotification*)notification;
- (void)answerWindowDidResize:(NSNotification*)notification;
- (BOOL)answerWindowShouldClose:(NSWindow*)sender;
- (void)answerWindowDidBecomeKey:(NSNotification*)notification;
- (void)answerWindowDidResignKey:(NSNotification*)notification;
- (void)answerWindowDidChangeOcclusionState:(NSNotification*)notification;
- (void)answerWindowDidMiniaturize:(NSNotification*)notification;
- (void)answerWindowDidDeminiaturize:(NSNotification*)notification;
- (void)answerWindowDidChangeBackingProperties:(NSNotification*)notification;
- (void)answerWindowDidEnterFullScreen:(NSNotification*)notification;
- (void)answerWindowDidExitFullScreen:(NSNotification*)notification;
@end

// The application delegate for a run. AppKit's terminate: ends the process
// before [NSApp run] returns, which would skip everything a run does after
// its window closes. A quit request therefore closes the window that ends
// the run, as its close button does, then ends the run in place (the run's
// MacRunEnd) and lets the termination proceed, so a log-out or restart goes
// on. Every other window's closing handler is asked first (audit P2). When
// a closing handler refuses, the termination is cancelled, as an
// application with unsaved work cancels it. With no such window, the
// termination proceeds. The work is synchronous, so the reply is given at
// once rather than deferred with NSTerminateLater.
// After a quit, the process ends inside terminate: with status 0: the run's
// own exit code and the program's code after the run do not apply.
//
// It also takes the system's requests to open files (application:openURLs:,
// which AppKit sends instead of application:openFiles: when implemented) and
// hands their paths to the run's open_files callback.
@interface GUIFormsTerminationDelegate : NSObject <NSApplicationDelegate> {
    // Observing: the run function owns it and clears this delegate before
    // it returns.
    gui_forms::host::MacRunEnd* _end;
    // Observing: the run's MacApplicationOptions::open_files, which outlives
    // the delegate's time as NSApp's delegate; null for a run without one.
    const std::function<void(std::vector<std::string>)>* _openFiles;
}
- (instancetype)initWithEnd:(gui_forms::host::MacRunEnd*)end;
- (instancetype)initWithEnd:(gui_forms::host::MacRunEnd*)end
                  openFiles:(const std::function<void(std::vector<std::string>)>*)openFiles;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (NSApplicationTerminateReply)answerApplicationShouldTerminate:(NSApplication*)sender;
@end

// A native popup window (HostWindowKind::popup, macos_application_run.mm):
// a borderless, non-activating panel. A borderless window cannot become key
// by default; a focus-taking popup (HostPopupFocus::takes_focus) may, so its
// own keyboard input reaches it, while a passive popup never does and its
// owner keeps the keyboard.
@interface GUIFormsPopupPanel : NSPanel {
    BOOL _takesFocus;
}
- (instancetype)initWithContentRect:(NSRect)contentRect takesFocus:(BOOL)takesFocus;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (BOOL)answerCanBecomeKeyWindow;
@end
