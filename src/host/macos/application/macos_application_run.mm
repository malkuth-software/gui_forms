// Application-scope windowing on AppKit (stage DK phase W0, decisions 6 and
// 7): run_macos_application and the run's HostWindowing, which the window
// delegates reach through the mac_run_* functions
// (macos_window_delegates.hpp).
//
// Desktop units: AppKit screen coordinates are points (logical units) with
// the origin at the bottom left of the primary screen. Frames, the pointer
// and work areas are published top-down, measured from the top of the
// tallest screen, exactly as appkit_host_services.mm publishes monitors.
//
// The run owns every window (window, view, delegate) through a slot.
// Initial windows keep their slot until the run returns, as before; a
// runtime window that closed is released from the main queue after the
// close has unwound (dispatch_async_f), so no view is released inside one of
// its own events.

#include "macos_host_private.hpp"
#include "macos_bundled_fonts.hpp"
#include "macos_main_work.hpp"
#include "macos_values.hpp"
#include "macos_window_delegates.hpp"
#include "macos_window_requests.hpp"
#include "../view/macos_view.hpp"

namespace gui_forms::host {

class StableWindowIdEquals final {
public:
    explicit StableWindowIdEquals(std::string_view stable_id) noexcept
        : stable_id_(stable_id) {}

    bool operator()(const MacApplicationWindow& candidate) const noexcept
    {
        return candidate.stable_id == stable_id_;
    }

private:
    std::string_view stable_id_;
};

namespace {
double desktop_top() {
    double top = 0.0;
    for (NSScreen* screen in [NSScreen screens]) {
        top = std::max(top, static_cast<double>(NSMaxY(screen.frame)));
    }
    return top;
}

gui_forms::Rect desktop_rect(NSRect rect, double top) {
    const gui_forms::Rect result{rect.origin.x, top - NSMaxY(rect), rect.size.width,
                                 rect.size.height};
    return result;
}

gui_forms::HostWindowFrame native_window_frame(NSWindow* window) {
    gui_forms::HostWindowFrame frame;
    const double top = desktop_top();
    const NSRect outer = window.frame;
    const NSRect content = [window contentRectForFrameRect:outer];
    frame.content = desktop_rect(content, top);
    frame.outer = desktop_rect(outer, top);
    frame.outer_measured = true;
    return frame;
}

std::vector<std::string> drag_region_ids(MacHostOptions& options) {
    std::vector<std::string> ids;
    if (!options.window_drag_region_id.empty()) {
        ids.push_back(std::move(options.window_drag_region_id));
    }
    for (std::string& region : options.window_drag_region_ids) {
        ids.push_back(std::move(region));
    }
    return ids;
}

bool valid_drag_regions(const MacApplicationWindow& entry) {
    std::vector<std::string> ids;
    if (!entry.options.window_drag_region_id.empty()) {
        ids.push_back(entry.options.window_drag_region_id);
    }
    ids.insert(ids.end(), entry.options.window_drag_region_ids.begin(),
               entry.options.window_drag_region_ids.end());
    const bool valid = validate_window_chrome_drag_regions(*entry.model, ids).accepted();
    return valid;
}
} // namespace

struct MacRunSlot final {
    gui_forms::HostWindowId id{};
    std::string stable_id{};
    bool initial{};
    // Shown and hidden by the floating policy (HostWindowKindTraits).
    bool floating{};
    // windowWillClose: has finished running every callback.
    bool close_finished{};
    NSWindow* window{};
    // A floating window's parent (the primary window), to re-attach the child
    // relation if AppKit dropped it while the window was ordered out.
    __weak NSWindow* parent{};
    GUIFormsView* view{};
    GUIFormsWindowDelegate* delegate{};
    bool print_metrics_on_close{};
    std::function<void(std::string_view metrics_json, std::string_view host_json)> final_snapshot{};
    bool capture_requested{};
};

// Keeps a scheduled release from reaching a run that has ended.
struct MacRunAlive final {
    MacApplicationRun* run{};
};

class MacApplicationRun final : public gui_forms::HostWindowing {
public:
    static constexpr std::size_t reserved_slots = 2U * gui_forms::HostWindowing::maximum_windows;

    MacApplicationRun() : HostWindowing(macos_capabilities()) {
        slots_.reserve(reserved_slots);
        alive_ = std::make_shared<MacRunAlive>();
        (*alive_).run = this;
    }
    ~MacApplicationRun() override { (*alive_).run = nullptr; }
    MacApplicationRun(const MacApplicationRun&) = delete;
    MacApplicationRun& operator=(const MacApplicationRun&) = delete;

    struct OpenOutcome final {
        gui_forms::HostWindowAdmission admission{};
        // run_macos_application's exit code for an initial window that could
        // not be created: 2 invalid, 5 attach.
        int failure_code{};
    };

    OpenOutcome open(MacApplicationWindow entry, bool runtime);
    void report_native_frame(gui_forms::HostWindowId window);
    void close_finished(gui_forms::HostWindowId window) noexcept;
    // Releases runtime windows whose close has finished.
    void reclaim();
    [[nodiscard]] std::size_t slot_count() const noexcept { return slots_.size(); }
    [[nodiscard]] MacRunSlot& slot_at(std::size_t index) noexcept { return *slots_[index]; }

protected:
    gui_forms::HostWindowFrameResult frame_impl(gui_forms::HostWindowId window) override;
    HostServiceStatus request_frame_impl(gui_forms::HostWindowId window,
                                         gui_forms::Rect content) override;
    gui_forms::HostScreenPointResult pointer_position_impl() override;
    gui_forms::HostWindowAtResult window_at_impl(gui_forms::DesktopLogicalPoint point) override;
    HostServiceStatus bring_to_front_impl(gui_forms::HostWindowId window) override;
    HostServiceStatus destroy_window_impl(gui_forms::HostWindowId window) override;
    HostMonitorResult query_monitors_impl() override;
    void query_monitors_into_impl(HostMonitorResult& result) override;
    HostServiceStatus show_impl(gui_forms::HostWindowId window) override;
    HostServiceStatus hide_impl(gui_forms::HostWindowId window) override;

public:
    // Popup windows open during the run (HostWindowKind::popup) as
    // borderless, non-activating panels (GUIFormsPopupPanel); their alpha
    // is the transparent backdrop the popup model requests.
    [[nodiscard]] bool supports_popup_windows() const noexcept override { return true; }
    // Splash windows (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S) are
    // the same panels, ignoring the mouse and never key, at their own level.
    [[nodiscard]] bool supports_splash_windows() const noexcept override { return true; }
    // run_modal's nested loop is an AppKit modal session
    // (beginModalSessionForWindow:): AppKit refuses the other windows'
    // input while the main queue, timers and display continue.
    [[nodiscard]] bool supports_modal_windows() const noexcept override { return true; }

protected:
    HostServiceStatus run_modal_impl(gui_forms::HostWindowId window) override;

private:
    // Releases a window whose initialization failed, before it was shown.
    void abandon_open(gui_forms::HostWindowId id, NSWindow* nativeWindow, NSWindow* ownerWindow,
                      GUIFormsView* view);

    [[nodiscard]] MacRunSlot* slot(gui_forms::HostWindowId window) noexcept;
    [[nodiscard]] MacRunSlot* slot(std::string_view stable_id) noexcept;
    void schedule_reclaim();

    // Stable addresses: a window's callbacks may open another window while
    // its own slot is in use.
    std::vector<std::unique_ptr<MacRunSlot>> slots_;
    std::shared_ptr<MacRunAlive> alive_;
    bool reclaim_scheduled_{};
};

MacRunSlot* MacApplicationRun::slot(gui_forms::HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        MacRunSlot& candidate = *slots_[index];
        if (candidate.id == window && live(window)) return &candidate;
    }
    return nullptr;
}

MacRunSlot* MacApplicationRun::slot(std::string_view stable_id) noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        MacRunSlot& candidate = *slots_[index];
        if (candidate.stable_id == stable_id && live(candidate.id)) return &candidate;
    }
    return nullptr;
}

MacApplicationRun::OpenOutcome MacApplicationRun::open(MacApplicationWindow entry, bool runtime) {
    OpenOutcome outcome;
    if (!entry.model || entry.stable_id.empty() || !valid_drag_regions(entry)) {
        outcome.admission.status.error = HostServiceError::invalid_argument;
        outcome.failure_code = 2;
        return outcome;
    }
    gui_forms::HostWindowId owner_id{};
    NSWindow* ownerWindow = nil;
    if (!entry.owner_id.empty()) {
        MacRunSlot* owner = slot(std::string_view(entry.owner_id));
        if (owner == nullptr) {
            outcome.admission.status.error = HostServiceError::invalid_argument;
            outcome.failure_code = 2;
            return outcome;
        }
        owner_id = (*owner).id;
        ownerWindow = (*owner).window;
    }
    const std::optional<gui_forms::HostPopupPlacement> popup = entry.popup;
    const gui_forms::HostWindowKind kind =
        gui_forms::host_window_kind(owner_id.valid(), entry.tool_window, entry.floating, popup.has_value());
    const bool floating = gui_forms::host_window_kind_traits(kind).floating;
    gui_forms::HostWindowDescription description;
    description.stable_id = entry.stable_id;
    description.owner = owner_id;
    description.kind = kind;
    if (popup.has_value()) description.transient = (*popup).kind;
    outcome.admission = admit(description);
    if (!outcome.admission.status.accepted()) {
        outcome.failure_code = 2;
        return outcome;
    }
    const gui_forms::HostWindowId id = outcome.admission.window;

    const GFSize initialSize = entry.options.capture_size.has_value()
        ? *entry.options.capture_size : entry.options.initial_size;
    const NSRect frame = NSMakeRect(0.0, 0.0, initialSize.width, initialSize.height);
    const bool transparentFullSizeContent = entry.options.titlebar_presentation ==
        MacTitlebarPresentation::transparent_full_size_content;
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
        NSWindowStyleMaskResizable;
    // A floating document is an ordinary titled window that minimizes with
    // its main window, not on its own; a floating tool is a utility panel.
    const bool panel = entry.tool_window || kind == gui_forms::HostWindowKind::floating_tool;
    if (kind != gui_forms::HostWindowKind::floating_document) {
        if (!panel && entry.options.minimizable) style |= NSWindowStyleMaskMiniaturizable;
        else style |= NSWindowStyleMaskUtilityWindow;
    }
    if (transparentFullSizeContent) style |= NSWindowStyleMaskFullSizeContentView;
    NSWindow* nativeWindow = nil;
    if (popup.has_value()) {
        // A native popup (owner, 2026-11-04): undecorated, never
        // activating the application, kept above its owner as a child
        // window; only a focus-taking popup becomes key. Its shadow is
        // drawn by the program in its own pixels, not by AppKit.
        const bool splash = gui_forms::host_transient_is_splash((*popup).kind);
        const BOOL takesFocus =
            !splash && (*popup).focus == gui_forms::HostPopupFocus::takes_focus ? YES : NO;
        GUIFormsPopupPanel* popupPanel = [[GUIFormsPopupPanel alloc] initWithContentRect:frame
                                                                              takesFocus:takesFocus];
        [popupPanel setHidesOnDeactivate:NO];
        [popupPanel setFloatingPanel:NO];
        [popupPanel setBecomesKeyOnlyIfNeeded:YES];
        [popupPanel setHasShadow:NO];
        if (splash) {
            // A Splash takes no input (owner, 2026-11-08): clicks pass to the
            // windows beneath it. A tooltip stands above the program's
            // popups and menus, as the host tooltip did; a splashscreen
            // floats above the program's windows.
            [popupPanel setIgnoresMouseEvents:YES];
            const NSWindowLevel level = (*popup).kind == gui_forms::HostTransientKind::tool_tip
                ? NSStatusWindowLevel : NSFloatingWindowLevel;
            [popupPanel setLevel:level];
        }
        nativeWindow = popupPanel;
    } else if (panel) {
        nativeWindow = [[NSPanel alloc] initWithContentRect:frame styleMask:style
                                                    backing:NSBackingStoreBuffered defer:NO];
    } else {
        nativeWindow = [[NSWindow alloc] initWithContentRect:frame styleMask:style
                                                     backing:NSBackingStoreBuffered defer:NO];
    }
    [nativeWindow setReleasedWhenClosed:NO];
    if (floating) {
        if (kind == gui_forms::HostWindowKind::floating_tool) {
            // NSPanel hides on deactivation by default; the portable policy
            // hides floating tools itself (decision 1), and an ordinary
            // window level keeps it above its main window only.
            NSPanel* floatingPanel = static_cast<NSPanel*>(nativeWindow);
            [floatingPanel setHidesOnDeactivate:NO];
            [floatingPanel setFloatingPanel:NO];
        }
        // Joins the main window's full-screen space (decision 1). Unverified
        // on a machine: that an auxiliary child window follows its parent
        // into the space; see the pull request.
        [nativeWindow setCollectionBehavior:([nativeWindow collectionBehavior] |
                                             NSWindowCollectionBehaviorFullScreenAuxiliary)];
    }
    std::vector<std::string> windowDragRegionIds = drag_region_ids(entry.options);
    GUIFormsView* view = [[GUIFormsView alloc] initWithModel:std::move(entry.model)
                                  transparentFullSizeContent:transparentFullSizeContent
                                         windowDragRegionIds:std::move(windowDragRegionIds)];
    [view installCloseRequestHandler:std::move(entry.options.close_request)];
    if (!entry.options.capture_path.empty()) {
        [view installFrameCapturePath:entry.options.capture_path];
    }
    const BOOL primary = kind == gui_forms::HostWindowKind::primary ? YES : NO;
    GUIFormsWindowDelegate* delegate =
        [[GUIFormsWindowDelegate alloc] initWithView:view
                             stopsApplicationOnClose:primary
                                         hideOnClose:entry.options.hide_on_close
                                        closedHandler:std::move(entry.options.closed)];
    [delegate attachRun:this window:id];
    [nativeWindow setDelegate:delegate];
    [nativeWindow setTitle:native_string(entry.options.title)];
    if (transparentFullSizeContent) {
        [nativeWindow setTitlebarAppearsTransparent:YES];
        [nativeWindow setTitleVisibility:NSWindowTitleHidden];
    }
    // A popup's size is the program's bounds, below any window minimum.
    const NSSize minimumSize = popup.has_value()
        ? NSMakeSize(0.0, 0.0)
        : NSMakeSize(entry.options.minimum_size.width, entry.options.minimum_size.height);
    [nativeWindow setContentMinSize:minimumSize];
    [nativeWindow setContentView:[view hostContentView]];

    std::unique_ptr<MacRunSlot> created = std::make_unique<MacRunSlot>();
    (*created).id = id;
    (*created).stable_id = entry.stable_id;
    (*created).initial = !runtime;
    (*created).floating = floating;
    (*created).parent = ownerWindow;
    (*created).window = nativeWindow;
    (*created).view = view;
    (*created).delegate = delegate;
    (*created).print_metrics_on_close = entry.options.print_metrics_on_close;
    (*created).final_snapshot = entry.options.final_snapshot;
    (*created).capture_requested = !entry.options.capture_path.empty();
    // Published before initialization: the ready callback may already ask
    // for this window's frame.
    slots_.push_back(std::move(created));

    if (popup.has_value()) {
        if (ownerWindow != nil) [ownerWindow addChildWindow:nativeWindow ordered:NSWindowAbove];
        if (!entry.options.initially_visible) [nativeWindow orderOut:nil];
        // Placed where the program put it, before it is shown.
        const gui_forms::Rect content{(*popup).origin.x, (*popup).origin.y, initialSize.width,
                                      initialSize.height};
        if (!request_frame_impl(id, content).accepted()) {
            abandon_open(id, nativeWindow, ownerWindow, view);
            outcome.admission.status.error = HostServiceError::backend_failure;
            outcome.admission.window = {};
            outcome.failure_code = 5;
            return outcome;
        }
    } else if (ownerWindow == nil) {
        [nativeWindow center];
    } else {
        [ownerWindow addChildWindow:nativeWindow ordered:NSWindowAbove];
        // A floating window is created hidden; the policy shows it.
        if (!entry.options.initially_visible || floating) {
            // addChildWindow:ordered: may order a previously hidden child
            // onscreen. Preserve the portable initial-visibility contract
            // after establishing native ownership.
            [nativeWindow orderOut:nil];
        }
        NSScreen* screen = ownerWindow.screen ?: NSScreen.mainScreen;
        NSRect placed = nativeWindow.frame;
        const NSRect ownerFrame = ownerWindow.frame;
        const NSRect work = screen.visibleFrame;
        placed.origin.x = std::clamp(NSMaxX(ownerFrame) + 10.0, NSMinX(work),
                                     NSMaxX(work) - placed.size.width);
        placed.origin.y = std::clamp(NSMaxY(ownerFrame) - placed.size.height, NSMinY(work),
                                     NSMaxY(work) - placed.size.height);
        [nativeWindow setFrame:placed display:NO];
    }
    // A Splash is never a drop target: it takes no input.
    if (popup.has_value() && gui_forms::host_transient_is_splash((*popup).kind)) [view unregisterDraggedTypes];

    if ([view initializeHost] == NO) {
        abandon_open(id, nativeWindow, ownerWindow, view);
        outcome.admission.status.error = HostServiceError::backend_failure;
        outcome.admission.window = {};
        outcome.failure_code = 5;
        return outcome;
    }
    if (entry.options.host_ready) {
        const std::function<void()> dispatchPending = entry.options.dispatch_pending;
        entry.options.host_ready(DispatchAndCollectWake(view, dispatchPending),
                                 RequestNativeClose(nativeWindow), ShowNativeDialog(view),
                                 ReadNativeClipboard(view), WriteNativeClipboard(view));
    }
    if (entry.options.full_screen_ready) {
        entry.options.full_screen_ready(RequestNativeFullScreen(nativeWindow));
    }
    if (entry.options.title_ready) {
        entry.options.title_ready(RequestNativeTitle(nativeWindow, view));
    }
    if (entry.options.client_size_ready) {
        entry.options.client_size_ready(RequestNativeClientSize(nativeWindow, view));
    }
    if (entry.options.visibility_ready) {
        entry.options.visibility_ready(RequestNativeShow(nativeWindow), RequestNativeHide(nativeWindow));
    }
    [view drainPostedWork];
    if (entry.options.dispatch_pending) entry.options.dispatch_pending();
    [view collectDamage];
    report_native_frame(id);
    window_created(id, entry.options.initially_visible);
    if (entry.options.initially_visible && !floating) {
        const bool takesKey = primary == YES ||
            (popup.has_value() && (*popup).focus == gui_forms::HostPopupFocus::takes_focus);
        if (takesKey) [nativeWindow makeKeyAndOrderFront:nil];
        else [nativeWindow orderFront:nil];
    }
    if (entry.options.close_after_launch_for_testing) {
        post_perform_close(nativeWindow);
    }
    return outcome;
}

void MacApplicationRun::abandon_open(gui_forms::HostWindowId id, NSWindow* nativeWindow,
                                     NSWindow* ownerWindow, GUIFormsView* view) {
    [view prepareForShutdown];
    if (ownerWindow != nil) [ownerWindow removeChildWindow:nativeWindow];
    [nativeWindow orderOut:nil];
    [nativeWindow setDelegate:nil];
    [nativeWindow setContentView:nil];
    retire(id);
    slots_.pop_back();
}

void MacApplicationRun::report_native_frame(gui_forms::HostWindowId window) {
    MacRunSlot* found = slot(window);
    if (found == nullptr || (*found).window == nil) return;
    report_frame(window, native_window_frame((*found).window));
}

void MacApplicationRun::close_finished(gui_forms::HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        MacRunSlot& candidate = *slots_[index];
        if (candidate.id != window) continue;
        candidate.close_finished = true;
        if (!candidate.initial) schedule_reclaim();
        break;
    }
    // A nested modal loop waits for the next event; a close that came from
    // the main queue or a timer brings none, so one is posted for the loop
    // to wake on and check its window again.
    if (modal_depth() > 0U) {
        NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                           location:NSZeroPoint
                                      modifierFlags:0
                                          timestamp:0.0
                                       windowNumber:0
                                            context:nil
                                            subtype:0
                                              data1:0
                                              data2:0];
        if (wake != nil) [NSApp postEvent:wake atStart:NO];
    }
}

HostServiceStatus MacApplicationRun::run_modal_impl(gui_forms::HostWindowId window) {
    MacRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    NSWindow* modalWindow = (*found).window;
    const std::optional<gui_forms::HostWindowId> owner_id = owner(window);
    const gui_forms::HostWindowId main_window = primary();
    // AppKit's modal session refuses key and mouse events to the
    // application's other windows; posted work, damage collection and
    // close requests (post_main_work), timers and display continue in
    // NSModalPanelRunLoopMode, a common mode.
    NSModalSession session = [NSApp beginModalSessionForWindow:modalWindow];
    while (live(window) && (!main_window.valid() || live(main_window))) {
        const NSModalResponse response = [NSApp runModalSession:session];
        if (response != NSModalResponseContinue) break;
        if (!live(window)) break;
        // Waits for the next event without taking it (runModalSession takes
        // it on the next pass), servicing the run loop's sources meanwhile.
        static_cast<void>([NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:[NSDate distantFuture]
                                                inMode:NSModalPanelRunLoopMode
                                               dequeue:NO]);
    }
    [NSApp endModalSession:session];
    // The owner becomes key again with its raster view first responder; its
    // activation gives its last focused control focus again
    // (SurfaceRoot::restore_focus).
    MacRunSlot* const owner_slot = owner_id.has_value() ? slot(*owner_id) : nullptr;
    if (owner_slot != nullptr && (*owner_slot).window.visible) {
        [(*owner_slot).window makeKeyAndOrderFront:nil];
        [(*owner_slot).window makeFirstResponder:(*owner_slot).view];
    }
    // Releases that waited for the nested loop to end (reclaim).
    schedule_reclaim();
    return {};
}

namespace {
// dispatch_function_t for schedule_reclaim: owns and deletes its context.
void reclaim_mac_run(void* context) {
    std::weak_ptr<MacRunAlive>* alive = static_cast<std::weak_ptr<MacRunAlive>*>(context);
    const std::shared_ptr<MacRunAlive> state = (*alive).lock();
    delete alive;
    if (state && (*state).run != nullptr) (*(*state).run).reclaim();
}
} // namespace

void MacApplicationRun::schedule_reclaim() {
    if (reclaim_scheduled_) return;
    reclaim_scheduled_ = true;
    // Released by reclaim_mac_run on the main thread (post_main_work),
    // whether or not the run still lives.
    std::weak_ptr<MacRunAlive>* context = new std::weak_ptr<MacRunAlive>(alive_);
    post_main_work(context, &reclaim_mac_run);
}

void MacApplicationRun::reclaim() {
    reclaim_scheduled_ = false;
    // Inside a nested modal loop some window's event or callback is on the
    // stack below it, so nothing is released: run_modal_impl schedules the
    // release again when its loop ends.
    if (modal_depth() > 0U) return;
    for (std::size_t index = slots_.size(); index > 0U; --index) {
        MacRunSlot& candidate = *slots_[index - 1U];
        if (candidate.initial || !candidate.close_finished) continue;
        const std::string metrics = [candidate.view metricsJSON];
        const std::string host = [candidate.view hostJSON];
        if (candidate.print_metrics_on_close) {
            std::fprintf(stdout, "{\"stable_id\":\"%s\",\"window\":%s,\"host\":%s}\n",
                         candidate.stable_id.c_str(), metrics.c_str(), host.c_str());
            std::fflush(stdout);
        }
        if (candidate.final_snapshot) candidate.final_snapshot(metrics, host);
        [candidate.view prepareForShutdown];
        [candidate.window setDelegate:nil];
        [candidate.window setContentView:nil];
        slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
    }
}

gui_forms::HostWindowFrameResult MacApplicationRun::frame_impl(gui_forms::HostWindowId window) {
    gui_forms::HostWindowFrameResult result;
    MacRunSlot* found = slot(window);
    if (found == nullptr) {
        result.status.error = HostServiceError::after_shutdown;
        return result;
    }
    result.frame = native_window_frame((*found).window);
    return result;
}

HostServiceStatus MacApplicationRun::request_frame_impl(gui_forms::HostWindowId window,
                                                        gui_forms::Rect content) {
    MacRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    NSWindow* nativeWindow = (*found).window;
    const NSSize minimum = [nativeWindow contentMinSize];
    if (content.width < static_cast<double>(minimum.width) ||
        content.height < static_cast<double>(minimum.height)) {
        return {HostServiceError::invalid_argument};
    }
    const bool fullScreen = ([nativeWindow styleMask] & NSWindowStyleMaskFullScreen) != 0;
    if (fullScreen || [nativeWindow isMiniaturized] || [nativeWindow isZoomed]) {
        return {HostServiceError::unsupported};
    }
    const double top = desktop_top();
    const NSRect contentRect = NSMakeRect(content.x, top - content.y - content.height,
                                          content.width, content.height);
    const NSRect frameRect = [nativeWindow frameRectForContentRect:contentRect];
    // windowDidMove: and windowDidResize: report the new frame.
    [nativeWindow setFrame:frameRect display:YES];
    return {};
}

gui_forms::HostScreenPointResult MacApplicationRun::pointer_position_impl() {
    gui_forms::HostScreenPointResult result;
    const NSPoint location = [NSEvent mouseLocation];
    result.position = {location.x, desktop_top() - location.y};
    return result;
}

gui_forms::HostWindowAtResult MacApplicationRun::window_at_impl(gui_forms::DesktopLogicalPoint point) {
    gui_forms::HostWindowAtResult result;
    const NSPoint location = NSMakePoint(point.x, desktop_top() - point.y);
    // The frontmost window of any application at the point (0: no window to
    // look below).
    const NSInteger number = [NSWindow windowNumberAtPoint:location belowWindowWithWindowNumber:0];
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        MacRunSlot& candidate = *slots_[index];
        if (!live(candidate.id) || candidate.window == nil) continue;
        if (candidate.window.windowNumber != number) continue;
        result.window = candidate.id;
        break;
    }
    return result;
}

HostServiceStatus MacApplicationRun::bring_to_front_impl(gui_forms::HostWindowId window) {
    MacRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    NSWindow* target = (*found).window;
    // orderedWindows: the application's windows from front to back.
    NSWindow* topmost = nil;
    for (NSWindow* candidate in [NSApp orderedWindows]) {
        for (std::size_t index = 0U; index < slots_.size(); ++index) {
            if ((*slots_[index]).window == candidate && live((*slots_[index]).id)) {
                topmost = candidate;
                break;
            }
        }
        if (topmost != nil) break;
    }
    if (topmost == nil || topmost == target) return {};
    // Relative ordering keeps the window below other applications' windows;
    // AppKit keeps child windows above their parents.
    [target orderWindow:NSWindowAbove relativeTo:topmost.windowNumber];
    return {};
}

HostServiceStatus MacApplicationRun::destroy_window_impl(gui_forms::HostWindowId window) {
    MacRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    NSWindow* nativeWindow = (*found).window;
    NSWindow* parent = nativeWindow.parentWindow;
    if (parent != nil) [parent removeChildWindow:nativeWindow];
    // windowWillClose: publishes the closed event and retires the identity
    // before this returns.
    [(*found).delegate closeWindowForApplication:nativeWindow];
    return {};
}

HostServiceStatus MacApplicationRun::show_impl(gui_forms::HostWindowId window) {
    MacRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    NSWindow* nativeWindow = (*found).window;
    NSWindow* parentWindow = (*found).parent;
    // Re-attach the child relation in case ordering out detached it.
    if (parentWindow != nil && nativeWindow.parentWindow == nil) {
        [parentWindow addChildWindow:nativeWindow ordered:NSWindowAbove];
    }
    // orderFront: shows without making the window key.
    [nativeWindow orderFront:nil];
    return {};
}

HostServiceStatus MacApplicationRun::hide_impl(gui_forms::HostWindowId window) {
    MacRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    [(*found).window orderOut:nil];
    return {};
}

HostMonitorResult MacApplicationRun::query_monitors_impl() {
    HostMonitorResult result;
    query_monitors_into_impl(result);
    return result;
}

// Writes the screens into `result`'s records in place: a repeated query on
// an unchanged set of screens allocates nothing (a warm Splash or Popup
// placement queries on every show).
void MacApplicationRun::query_monitors_into_impl(HostMonitorResult& result) {
    result.status = {};
    const double top = desktop_top();
    NSArray<NSScreen*>* screens = [NSScreen screens];
    NSScreen* primaryScreen = screens.firstObject;
    std::size_t count = 0U;
    for (NSScreen* screen in screens) {
        if (count == result.monitors.size()) result.monitors.emplace_back();
        HostMonitor& monitor = result.monitors[count];
        NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
        char identifier[48]{};
        const int length = number != nil
            ? std::snprintf(identifier, sizeof(identifier), "appkit.display.%u", number.unsignedIntValue)
            : std::snprintf(identifier, sizeof(identifier), "appkit.screen.%zu", count);
        monitor.id.assign(identifier, length > 0 ? static_cast<std::size_t>(length) : 0U);
        monitor.frame = desktop_rect(screen.frame, top);
        monitor.work_area = desktop_rect(screen.visibleFrame, top);
        monitor.scale = screen.backingScaleFactor;
        monitor.primary = screen == primaryScreen;
        ++count;
    }
    result.monitors.resize(count);
    if (result.monitors.empty()) result.status.error = HostServiceError::backend_failure;
}

void mac_run_report_frame(MacApplicationRun* run, gui_forms::HostWindowId window) {
    if (run == nullptr) return;
    (*run).report_native_frame(window);
}

void mac_run_retire(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept {
    if (run == nullptr) return;
    (*run).retire(window);
}

void mac_run_close_finished(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept {
    if (run == nullptr) return;
    (*run).close_finished(window);
}

void mac_run_report_minimized(MacApplicationRun* run, gui_forms::HostWindowId window,
                              bool minimized) {
    if (run == nullptr) return;
    (*run).report_minimized(window, minimized);
}

void mac_run_hidden_on_close(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept {
    if (run == nullptr) return;
    (*run).report_hidden_on_close(window);
}

void mac_run_report_active(MacApplicationRun* run, bool active) {
    if (run == nullptr) return;
    (*run).report_application_active(active);
}

// The run's open operation. Only the main thread reaches the run, and only
// while it lives.
struct MacRunOpenState final {
    MacApplicationRun* run{};
};

struct MacRunOpen final {
    std::weak_ptr<MacRunOpenState> owner{};
    gui_forms::HostWindowAdmission operator()(MacApplicationWindow entry) const {
        gui_forms::HostWindowAdmission refused;
        const std::shared_ptr<MacRunOpenState> state = owner.lock();
        if (!state || (*state).run == nullptr) {
            refused.status.error = HostServiceError::after_shutdown;
            return refused;
        }
        if (![NSThread isMainThread]) {
            refused.status.error = HostServiceError::wrong_thread;
            return refused;
        }
        const MacApplicationRun::OpenOutcome outcome = (*(*state).run).open(std::move(entry), true);
        return outcome.admission;
    }
};

namespace {

// A multi-window run's end (MacRunEnd's context): borrowed from
// run_macos_application, which outlives every use.
struct ApplicationRunEnd final {
    MacApplicationRun* run{};
    MacRunOpenState* open_state{};
    GUIFormsActivationObserver* activation{};
    MacApplicationOptions* application{};
    bool windowing_published{};
    int* exit_code{};
};

void end_application_run(void* context) {
    ApplicationRunEnd& end = *static_cast<ApplicationRunEnd*>(context);
    MacApplicationRun& run = *end.run;
    [NSApp setDelegate:nil];
    // The program's handles stop reaching the windowing first, so a
    // closed callback during shutdown cannot open another window.
    (*end.open_state).run = nullptr;
    [end.activation stopObserving];
    if (end.windowing_published && (*end.application).windowing_ended) (*end.application).windowing_ended();
    run.shutdown();

    // Closing the primary terminates the application session. Close any
    // surviving owned/tool windows while their independent host sessions
    // and delegates are still intact, so each root receives one real
    // HostClosedEvent and its callback observes the actual close point.
    for (std::size_t index = 0U; index < run.slot_count(); ++index) {
        [run.slot_at(index).delegate prepareForApplicationShutdown];
    }
    for (std::size_t index = 0U; index < run.slot_count(); ++index) {
        NSWindow* nativeWindow = run.slot_at(index).window;
        if (nativeWindow.isVisible || !nativeWindow.isReleasedWhenClosed) {
            [nativeWindow close];
        }
    }

    for (std::size_t index = 0U; index < run.slot_count(); ++index) {
        MacRunSlot& slot = run.slot_at(index);
        const std::string metrics = [slot.view metricsJSON];
        const std::string host = [slot.view hostJSON];
        if (slot.print_metrics_on_close) {
            std::fprintf(stdout, "{\"stable_id\":\"%s\",\"window\":%s,\"host\":%s}\n",
                         slot.stable_id.c_str(), metrics.c_str(), host.c_str());
        }
        if (slot.final_snapshot) slot.final_snapshot(metrics, host);
        if (slot.capture_requested && [slot.view frameCaptureSaved] == NO) {
            *end.exit_code = 6;
        }
        [slot.view prepareForShutdown];
        [slot.window setDelegate:nil];
        [slot.window setContentView:nil];
    }
    std::fflush(stdout);
}

} // namespace

int run_macos_application(std::vector<MacApplicationWindow> windows,
                          MacApplicationOptions application) {
    if (windows.empty() || windows.size() > gui_forms::HostWindowing::maximum_windows) return 2;
    // The run holds the bundled font set, so windows opened and closed during it share one
    // copy of each face.
    const std::shared_ptr<gui_forms::detail::BundledFontSet> run_fonts = bundle_font_set();
    // The run's Splash baker (Part S, phase 2), after the fonts it paints with and
    // before every window, so it outlives each tooltip that reads it.
    const gui_forms::render::SkiaSplashBakeRun splash_baking(run_fonts);
    int exitCode = 0;
    std::unordered_set<std::string> identities;
    std::size_t primary_count{};
    for (const MacApplicationWindow& entry : windows) {
        if (!entry.model || entry.stable_id.empty() ||
            !identities.insert(entry.stable_id).second ||
            (!entry.owner_id.empty() && entry.owner_id == entry.stable_id)) {
            return 2;
        }
        if (entry.owner_id.empty() && !entry.tool_window) {
            if (!entry.options.initially_visible || entry.options.hide_on_close) {
                return 2;
            }
            ++primary_count;
        }
    }
    for (const MacApplicationWindow& entry : windows) {
        if (!entry.owner_id.empty() && !identities.contains(entry.owner_id)) return 2;
        if (!valid_drag_regions(entry)) return 2;
        std::string_view owner = entry.owner_id;
        for (std::size_t depth = 0; !owner.empty(); ++depth) {
            if (depth >= windows.size()) return 2;
            const std::vector<MacApplicationWindow>::iterator parent =
                std::find_if(windows.begin(), windows.end(), StableWindowIdEquals(owner));
            owner = (*parent).owner_id;
        }
    }
    if (primary_count != 1U) return 2;

    @autoreleasepool {
        NSApplication* nativeApplication = [NSApplication sharedApplication];
        [nativeApplication setActivationPolicy:NSApplicationActivationPolicyRegular];
        detail::install_application_menu();

        MacApplicationRun run;
        GUIFormsActivationObserver* activation = [[GUIFormsActivationObserver alloc] initWithRun:&run];
        const std::shared_ptr<MacRunOpenState> openState = std::make_shared<MacRunOpenState>();
        (*openState).run = &run;
        const bool windowingPublished = static_cast<bool>(application.windowing_ready);
        if (windowingPublished) application.windowing_ready(run, MacRunOpen{openState});

        // Owners first; validation above excluded cycles and unknown owners.
        std::vector<bool> opened(windows.size(), false);
        std::size_t remaining = windows.size();
        int failure = 0;
        while (remaining > 0U && failure == 0) {
            for (std::size_t index = 0U; index < windows.size() && failure == 0; ++index) {
                if (opened[index]) continue;
                if (!windows[index].owner_id.empty() &&
                    !run.find(windows[index].owner_id).has_value()) continue;
                const MacApplicationRun::OpenOutcome outcome =
                    run.open(std::move(windows[index]), false);
                if (!outcome.admission.status.accepted()) {
                    failure = outcome.failure_code == 0 ? 2 : outcome.failure_code;
                    break;
                }
                opened[index] = true;
                --remaining;
            }
        }
        if (failure == 0) {
            ApplicationRunEnd ending{&run, openState.get(), activation, &application, windowingPublished, &exitCode};
            MacRunEnd end{&end_application_run, &ending, false};
            // NSApp holds its delegate weakly; this local keeps it to the
            // end, and end_application_run clears it.
            GUIFormsTerminationDelegate* termination =
                [[GUIFormsTerminationDelegate alloc] initWithEnd:&end openFiles:&application.open_files];
            [nativeApplication setDelegate:termination];
            run.mark_started();
            [nativeApplication activateIgnoringOtherApps:YES];
            [nativeApplication run];
            finish_run(end);
        } else {
            // The program's handles stop reaching the windowing first, so a
            // closed callback during shutdown cannot open another window.
            (*openState).run = nullptr;
            [activation stopObserving];
            if (windowingPublished && application.windowing_ended) application.windowing_ended();
            run.shutdown();
            // As before: a failed start releases the created windows without
            // running their close notifications.
            for (std::size_t index = 0U; index < run.slot_count(); ++index) {
                MacRunSlot& slot = run.slot_at(index);
                [slot.view prepareForShutdown];
                [slot.window orderOut:nil];
                [slot.window setDelegate:nil];
                [slot.window setContentView:nil];
            }
            exitCode = failure;
        }
    }
    return exitCode;
}

} // namespace gui_forms::host
