// The AppKit host's entry points: the host's capabilities and run_macos, the
// single-window run. The application-scope run (run_macos_application) is
// macos_application_run.mm; the view, its input, drawing and accessibility
// are under ../view and ../accessibility (macos_view.hpp lists the units).

#include "macos_host_private.hpp"
#include "macos_main_work.hpp"
#include "macos_window_delegates.hpp"
#include "macos_window_requests.hpp"
#include "../view/macos_view.hpp"

namespace gui_forms::host {

// The macOS convention for the named keyboard commands (owner decision
// DK-30, interview G5-1): Cmd+W closes a document; every other command keeps
// the PC convention's keys, Ctrl among them (Ctrl+Tab, Ctrl+PageDown).
static KeyMap macos_key_map() {
    KeyMap map = pc_key_map();
    map.set_binding(KeyCommand::close_document, {{PhysicalKey::w, Modifier::meta}, {}});
    return map;
}

// accessibility: NSAccessibility publication of the semantic tree,
// including editable text attributes, verified by
// gui_forms_macos_accessibility_tests and the AXUIElement client (stage A).
HostCapabilities macos_capabilities() {
    return {HostCapabilities::current_protocol_version,
            "appkit-macos",
            HostCapability::lifecycle |
                HostCapability::accessibility |
                HostCapability::frame_capture |
                HostCapability::scale_notifications |
                HostCapability::occlusion |
                HostCapability::scheduled_wake |
                HostCapability::pointer_input |
                HostCapability::keyboard_input |
                HostCapability::text_composition |
                HostCapability::monitor_geometry |
                HostCapability::pointer_capture |
                HostCapability::cursor |
                HostCapability::clipboard |
                HostCapability::clipboard_images |
                HostCapability::typed_drag_destination |
                HostCapability::dialogs |
                HostCapability::sound_cues |
                HostCapability::notification_area |
                HostCapability::notification_area_menus |
                HostCapability::runtime_windows |
                HostCapability::window_frames |
                HostCapability::screen_pointer |
                HostCapability::window_at_point |
                HostCapability::window_ordering |
                HostCapability::transparent_windows |
                HostCapability::window_backdrops |
                HostCapability::compositor_notifications |
                HostCapability::presentation_preferences |
                HostCapability::transient_surfaces |
                HostCapability::window_state_reports,
            macos_key_map()};
}

namespace {
// run_macos's test close: up to `attempts` performClose: while visible.
struct RepeatedCloseWork final {
    NSWindow* window;
    std::uint32_t attempts;
};

void repeated_close_on_main(void* context) {
    const std::unique_ptr<RepeatedCloseWork> work(static_cast<RepeatedCloseWork*>(context));
    for (std::uint32_t attempt = 0; attempt < (*work).attempts && (*work).window.isVisible; ++attempt) {
        [(*work).window performClose:nil];
    }
}
} // namespace

namespace {

// A single-window run's end (MacRunEnd's context): borrowed from run_macos,
// which outlives every use.
struct SingleWindowRunEnd final {
    GUIFormsView* view{};
    NSWindow* window{};
    MacHostOptions* options{};
    int* exit_code{};
};

void end_single_window_run(void* context) {
    SingleWindowRunEnd& end = *static_cast<SingleWindowRunEnd*>(context);
    MacHostOptions& options = *end.options;
    [NSApp setDelegate:nil];
    const std::string metrics = [end.view metricsJSON];
    const std::string host = [end.view hostJSON];
    if (options.print_metrics_on_close) {
        std::fprintf(stdout, "{\"window\":%s,\"host\":%s}\n",
                     metrics.c_str(), host.c_str());
        std::fflush(stdout);
    }
    if (options.final_snapshot) {
        options.final_snapshot(metrics, host);
    }
    // Portable contract: a requested capture that was never saved (no
    // covering frame before close, or an encode/write failure) fails the
    // process.
    if (!options.capture_path.empty() && [end.view frameCaptureSaved] == NO) {
        *end.exit_code = 6;
    }
    [end.view prepareForShutdown];
    [end.window setDelegate:nil];
    [end.window setContentView:nil];
}

} // namespace

int run_macos(std::unique_ptr<Window> model, MacHostOptions options) {
    if (!model || !options.initially_visible || options.hide_on_close) {
        return 2;
    }
    std::vector<std::string> windowDragRegionIds;
    if (!options.window_drag_region_id.empty()) {
        windowDragRegionIds.push_back(options.window_drag_region_id);
    }
    windowDragRegionIds.insert(windowDragRegionIds.end(),
                               options.window_drag_region_ids.begin(),
                               options.window_drag_region_ids.end());
    if (!validate_window_chrome_drag_regions(
            *model, windowDragRegionIds).accepted()) {
        return 2;
    }
    int exitCode = 0;
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        detail::install_application_menu();

        const Size initialSize = options.capture_size.has_value()
            ? *options.capture_size : options.initial_size;
        const NSRect frame = NSMakeRect(0.0, 0.0,
                                        initialSize.width,
                                        initialSize.height);
        const bool transparentFullSizeContent =
            options.titlebar_presentation ==
            MacTitlebarPresentation::transparent_full_size_content;
        NSWindowStyleMask style = NSWindowStyleMaskTitled |
            NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
            NSWindowStyleMaskResizable;
        if (transparentFullSizeContent) {
            style |= NSWindowStyleMaskFullSizeContentView;
        }
        NSWindow* nativeWindow = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:style
                        backing:NSBackingStoreBuffered
                          defer:NO];
        // ARC owns this local for the complete run loop. AppKit's historical
        // self-release-on-close policy would otherwise release the same window
        // a second time when the autorelease pool drains.
        [nativeWindow setReleasedWhenClosed:NO];
        GUIFormsView* view = [[GUIFormsView alloc]
            initWithModel:std::move(model)
            transparentFullSizeContent:transparentFullSizeContent
            windowDragRegionIds:std::move(windowDragRegionIds)];
        [view installCloseRequestHandler:std::move(options.close_request)];
        if (!options.capture_path.empty()) {
            [view installFrameCapturePath:options.capture_path];
        }
        GUIFormsWindowDelegate* delegate =
            [[GUIFormsWindowDelegate alloc] initWithView:view
                                 stopsApplicationOnClose:YES
                                             hideOnClose:NO
                                            closedHandler:std::move(options.closed)];
        [nativeWindow setDelegate:delegate];
        [nativeWindow setTitle:[NSString stringWithUTF8String:options.title.c_str()]];
        if (transparentFullSizeContent) {
            [nativeWindow setTitlebarAppearsTransparent:YES];
            [nativeWindow setTitleVisibility:NSWindowTitleHidden];
        }
        [nativeWindow setContentMinSize:NSMakeSize(options.minimum_size.width,
                                            options.minimum_size.height)];
        [nativeWindow setContentView:[view hostContentView]];
        [nativeWindow center];
        if ([view initializeHost] == NO) {
            [view prepareForShutdown];
            [nativeWindow setDelegate:nil];
            [nativeWindow setContentView:nil];
            return 5;
        }
        if (options.host_ready) {
            const std::function<void()> dispatchPending = options.dispatch_pending;
            options.host_ready(
                DispatchAndCollectWake(view, dispatchPending),
                RequestNativeClose(nativeWindow),
                ShowNativeDialog(view),
                ReadNativeClipboard(view),
                WriteNativeClipboard(view));
        }
        if (options.full_screen_ready) options.full_screen_ready(RequestNativeFullScreen(nativeWindow));
        if (options.title_ready) options.title_ready(RequestNativeTitle(nativeWindow, view));
        if (options.client_size_ready)
            options.client_size_ready(RequestNativeClientSize(nativeWindow, view));
        if (options.visibility_ready) {
            options.visibility_ready(RequestNativeShow(nativeWindow),
                                     RequestNativeHide(nativeWindow));
        }
        // Match every host: one FIFO initialization turn runs after portable
        // attach/service publication and before first visible presentation.
        [view drainPostedWork];
        if (options.dispatch_pending) options.dispatch_pending();
        [view collectDamage];
        [nativeWindow makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];
        if (options.close_after_launch_for_testing) {
            const std::uint32_t closeAttempts =
                std::max<std::uint32_t>(1, options.close_attempts_for_testing);
            RepeatedCloseWork* const work = new RepeatedCloseWork{nativeWindow, closeAttempts};
            post_main_work(work, &repeated_close_on_main);
        }
        SingleWindowRunEnd ending{view, nativeWindow, &options, &exitCode};
        MacRunEnd end{&end_single_window_run, &ending, false};
        // NSApp holds its delegate weakly; this local keeps it to the end,
        // and end_single_window_run clears it.
        GUIFormsTerminationDelegate* termination = [[GUIFormsTerminationDelegate alloc] initWithEnd:&end];
        [application setDelegate:termination];
        [application run];
        finish_run(end);
    }
    return exitCode;
}

} // namespace gui_forms::host
