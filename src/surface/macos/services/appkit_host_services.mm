#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <objc/message.h>

#include "macos_host.hpp"
#include "appkit_clipboard_image.hpp"
#include "appkit_drag_release.hpp"
#include "appkit_status_items.hpp"
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
#include "appkit_desktop_notifications.hpp"
#endif
#include "appkit_relative_pointer.hpp"
#include "../compositor/macos_compositor.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../../../core/failure/fail_fast.hpp"
#include "../../../core/host/session/session_directory.hpp"
#include "../../../render/skia/raster/skia_raster.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

using gui_forms::DamageRegion;
using gui_forms::DragAction;
using gui_forms::DragBinaryData;
using gui_forms::DragDataItem;
using gui_forms::DragEffect;
using gui_forms::DragEvent;
using gui_forms::DragFileListData;
using gui_forms::DragLimits;
using gui_forms::DragTextData;
using gui_forms::HostActivationEvent;
using gui_forms::HostAttachEvent;
using gui_forms::HostCapability;
using gui_forms::HostCloseReason;
using gui_forms::HostCloseRequest;
using gui_forms::HostClipboardTextResult;
using gui_forms::HostClosedEvent;
using gui_forms::HostDispatchResult;
using gui_forms::HostDialogChoice;
using gui_forms::HostDialogOutcome;
using gui_forms::HostDialogRequest;
using gui_forms::HostDialogResult;
using gui_forms::HostColorDialogRequest;
using gui_forms::HostColorDialogResult;
using gui_forms::HostDisplayEvent;
using gui_forms::HostFolderDialogRequest;
using gui_forms::HostMessageButtons;
using gui_forms::HostMessageDialogRequest;
using gui_forms::HostMessageDialogResult;
using gui_forms::HostMessageIcon;
using gui_forms::HostOpenFileDialogRequest;
using gui_forms::HostPathDialogResult;
using gui_forms::HostSaveFileDialogRequest;
using gui_forms::HostEvent;
using gui_forms::HostEventPayload;
using gui_forms::HostOcclusionEvent;
using gui_forms::HostResizeEvent;
using gui_forms::HostScaleEvent;
using gui_forms::HostMonitor;
using gui_forms::HostMonitorResult;
using gui_forms::HostServiceError;
using gui_forms::HostServiceStatus;
using gui_forms::HostSoundCue;
using gui_forms::HostSoundCueRequest;
using gui_forms::HostServices;
using gui_forms::HostSession;
using gui_forms::HostShutdownEvent;
using gui_forms::KeyAction;
using gui_forms::KeyEvent;
using gui_forms::LiveSurfacePresentation;
using gui_forms::Modifier;
using gui_forms::PaintReceipt;
using GFPoint = gui_forms::Point;
using gui_forms::PointerAction;
using gui_forms::PointerButton;
using gui_forms::PointerEvent;
using gui_forms::SemanticAction;
using gui_forms::SemanticNode;
using gui_forms::SemanticRole;
using gui_forms::SemanticState;
using GFRect = gui_forms::Rect;
using GFSize = gui_forms::Size;
using gui_forms::TextInputEvent;
using gui_forms::Window;
using gui_forms::CursorKind;
using gui_forms::render::SkiaRaster;

// The target of a file panel's filter popup (the accessory view
// file_filter_popup adds): choosing a filter makes its types the panel's
// allowed types, an empty list allowing any file. The panel owns the popup
// through its accessory view and the popup only observes this switch, so
// the caller keeps the switch alive until the panel's modal run returns;
// the switch only observes the panel.
@interface GuiFormsFileFilterSwitch : NSObject
@property(nonatomic, weak) NSSavePanel* panel;
@property(nonatomic, copy) NSArray<NSArray<UTType*>*>* filterTypes;
- (void)filterChanged:(NSPopUpButton*)sender;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerFilterChanged:(NSPopUpButton*)sender;
@end

// An NSAlert's delegate for its Help button: reports the open alert's help
// request and keeps it open. The services outlive the alert's modal run.
@interface GuiFormsAlertHelp : NSObject <NSAlertDelegate>
@property(nonatomic, assign) gui_forms::HostServices* services;
@property(nonatomic, assign) std::uint64_t requestId;
- (BOOL)alertShowHelp:(NSAlert*)alert;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (BOOL)answerAlertShowHelp:(NSAlert*)alert;
@end

// The target of an alert's buttons while a model holds control
// (run_alert_modal). A click or key equivalent that reaches a button while
// the model holds is the user's press: it offers the take-back dialog and
// the alert stays open. Otherwise the button answers as NSAlert's own target
// does in a modal run, stopping the modal session with the button's tag.
// The caller keeps the target alive until the modal session ends; the
// buttons only observe it.
@interface GuiFormsHeldAlertAnswer : NSObject
- (void)buttonPressed:(NSButton*)sender;
// The body of this class's native entry: the entry calls its answer
// through the fail-fast boundary (call_failing_fast).
- (void)answerButtonPressed:(NSButton*)sender;
@end

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// The source of a drag to other programs (HostServices::start_drag): it
// offers the request's effects to every destination, inside the program or
// outside, and reports the end to the services that started it. The
// services hold it for the drag's length and detach it at shutdown or when
// their window closes, after which an end reports nothing; it holds itself
// until AppKit's session ends. Not compiled in a high-integrity build
// (cmake/HighIntegrity.cmake).
@interface GuiFormsDragSource : NSObject <NSDraggingSource>
@property(nonatomic, assign) gui_forms::HostServices* services;
@property(nonatomic, assign) std::uint64_t sessionId;
@property(nonatomic, assign) NSDragOperation allowedOperations;
// The press or move the session started from: its button's release is sent
// to the window when the session ends.
@property(nonatomic, assign) NSEventType startType;
// The source itself, from the start until the session ends: AppKit's end
// reaches a live source even after the services that started it are gone
// (detached at shutdown, so that end reports nothing).
@property(nonatomic, strong) GuiFormsDragSource* holdUntilEnd;
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context;
- (void)draggingSession:(NSDraggingSession*)session
           endedAtPoint:(NSPoint)screenPoint
              operation:(NSDragOperation)operation;
// The bodies of this class's native entries: each entry calls its answer
// through the fail-fast boundary (call_failing_fast).
- (void)answerDraggingSessionEndedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation;
@end
#endif

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GuiFormsAlertHelp's native entries: each sends the entry's answer message.
BOOL run_alert_help_alert_show_help(GuiFormsAlertHelp* object, NSAlert* alert) {
    return [object answerAlertShowHelp:alert];
}
} // namespace

@implementation GuiFormsAlertHelp
- (BOOL)alertShowHelp:(NSAlert*)alert {
    return gui_forms::detail::call_failing_fast(
        "macos.alert_help.alertShowHelp:", &run_alert_help_alert_show_help, self, alert);
}

- (BOOL)answerAlertShowHelp:(NSAlert*)alert {
    (void)alert;
    gui_forms::HostDialogHelpEvent event{};
    event.request_id = self.requestId;
    event.keyboard_initiated = false;
    static_cast<void>((*self.services).publish_dialog_help(event));
    return YES;
}
@end

namespace {
// The named target the fail-fast boundary (call_failing_fast) calls for
// GuiFormsHeldAlertAnswer's native entry: it sends the entry's answer message.
void run_held_alert_button_pressed(GuiFormsHeldAlertAnswer* object, NSButton* sender) {
    [object answerButtonPressed:sender];
}
} // namespace

@implementation GuiFormsHeldAlertAnswer
- (void)buttonPressed:(NSButton*)sender {
    gui_forms::detail::call_failing_fast(
        "macos.held_alert.buttonPressed:", &run_held_alert_button_pressed, self, sender);
}

- (void)answerButtonPressed:(NSButton*)sender {
    if (gui_forms::detail::native_input_held(true)) return;
    [NSApp stopModalWithCode:sender.tag];
}
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GuiFormsFileFilterSwitch's native entries: each sends the entry's answer message.
void run_file_filter_filter_changed(GuiFormsFileFilterSwitch* object, NSPopUpButton* sender) {
    [object answerFilterChanged:sender];
}
} // namespace

@implementation GuiFormsFileFilterSwitch
- (void)filterChanged:(NSPopUpButton*)sender {
    gui_forms::detail::call_failing_fast(
        "macos.file_filter.filterChanged:", &run_file_filter_filter_changed, self, sender);
}

- (void)answerFilterChanged:(NSPopUpButton*)sender {
    const NSInteger index = sender.indexOfSelectedItem;
    if (index < 0 || static_cast<NSUInteger>(index) >= self.filterTypes.count) {
        return;
    }
    self.panel.allowedContentTypes = self.filterTypes[static_cast<NSUInteger>(index)];
}
@end

namespace {

// A titled window's corner radius, read only (owner, 2026-11-24: never
// set or override the corner mask, never subclass the theme frame; WebKit
// and Firefox gave that up for its WindowServer cost on macOS 26), with
// where it came from. In order:
// - macOS 26: the content view's layout regions. The safe area with
//   vertical corner adaptation keeps content out of the bottom corners by
//   exactly their radius beyond the plain safe area (measured on 26.7: 16
//   for a plain titled window, 26 with a toolbar, matching the frame);
// - before that, with GUI_FORMS_MACOS_PRIVATE_CORNER_RADIUS:
//   -[NSWindow _effectiveCornerRadius], looked up with respondsToSelector:
//   and never linked;
// - otherwise unknown (the band draws square).
// A window in full screen is square whatever these say (the layout region
// still reports the windowed radius there); the caller checks first.
struct CornerReading final {
    double radius{};
    gui_forms::WindowCornerSource source{gui_forms::WindowCornerSource::unknown};
};

CornerReading titled_window_corner_radius([[maybe_unused]] NSWindow* window) noexcept {
    CornerReading reading{};
    // NSViewLayoutRegion is in the macOS 26 SDK; a build with an older SDK
    // (CI's macos-15 image) leaves the reading to the fallbacks below.
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    NSView* const content = window.contentView;
    if (@available(macOS 26.0, *)) {
        if (content != nil) {
            NSViewLayoutRegion* const adapted =
                [NSViewLayoutRegion safeAreaLayoutRegionWithCornerAdaptation:NSViewLayoutRegionAdaptivityAxisVertical];
            NSViewLayoutRegion* const plain =
                [NSViewLayoutRegion safeAreaLayoutRegionWithCornerAdaptation:NSViewLayoutRegionAdaptivityAxisNone];
            const CGFloat radius = [content edgeInsetsForLayoutRegion:adapted].bottom -
                                   [content edgeInsetsForLayoutRegion:plain].bottom;
            if (radius > 0.0 && std::isfinite(radius)) {
                reading.radius = static_cast<double>(radius);
                reading.source = gui_forms::WindowCornerSource::layout_region;
                return reading;
            }
        }
    }
#endif
#if defined(GUI_FORMS_MACOS_PRIVATE_CORNER_RADIUS)
    const SEL getter = NSSelectorFromString(@"_effectiveCornerRadius");
    if ([window respondsToSelector:getter]) {
        NSMethodSignature* const signature = [window methodSignatureForSelector:getter];
        const char* const returned = signature == nil ? "" : [signature methodReturnType];
        // Only a getter that returns a CGFloat is read.
        if (std::strcmp(returned, @encode(CGFloat)) == 0) {
            CGFloat (*const read)(id, SEL) = reinterpret_cast<CGFloat (*)(id, SEL)>(objc_msgSend);
            const CGFloat radius = read(window, getter);
            if (radius > 0.0 && std::isfinite(radius)) {
                reading.radius = static_cast<double>(radius);
                reading.source = gui_forms::WindowCornerSource::private_getter;
            }
        }
    }
#endif
    return reading;
}

// The logical size of GUI.Forms' message icon (stock_alert_icon_image).
constexpr CGFloat alert_icon_points = 32.0;

// `pixels` (premultiplied BGRA, top row first) as an NSImage 32 points
// square: one representation, its pixels converted to the premultiplied
// RGBA NSBitmapImageRep holds by default. Nil when AppKit cannot make it.
NSImage* alert_icon_image(const gui_forms::HostImage& pixels) {
    if (pixels.width == 0U || pixels.height == 0U || pixels.row_bytes < std::uint64_t{pixels.width} * 4U ||
        pixels.pixels.size() < pixels.row_bytes * (pixels.height - 1U) + std::uint64_t{pixels.width} * 4U) {
        return nil;
    }
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr pixelsWide:pixels.width pixelsHigh:pixels.height
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:0
        bytesPerRow:pixels.width * 4 bitsPerPixel:32];
    if (rep == nil || rep.bitmapData == nullptr) return nil;
    unsigned char* const bytes = rep.bitmapData;
    for (std::uint32_t y = 0U; y < pixels.height; ++y) {
        const std::byte* const row = pixels.pixels.data() + y * pixels.row_bytes;
        unsigned char* const out = bytes + std::size_t{y} * pixels.width * 4U;
        for (std::uint32_t x = 0U; x < pixels.width; ++x) {
            out[x * 4U] = static_cast<unsigned char>(row[x * 4U + 2U]);
            out[x * 4U + 1U] = static_cast<unsigned char>(row[x * 4U + 1U]);
            out[x * 4U + 2U] = static_cast<unsigned char>(row[x * 4U]);
            out[x * 4U + 3U] = static_cast<unsigned char>(row[x * 4U + 3U]);
        }
    }
    const NSSize size = NSMakeSize(alert_icon_points, alert_icon_points);
    rep.size = size;
    NSImage* image = [[NSImage alloc] initWithSize:size];
    [image addRepresentation:rep];
    return image;
}

NSCursor* native_cursor(CursorKind cursor) {
    switch (cursor) {
    case CursorKind::arrow: return [NSCursor arrowCursor];
    case CursorKind::text: return [NSCursor IBeamCursor];
    case CursorKind::hand: return [NSCursor pointingHandCursor];
    case CursorKind::crosshair: return [NSCursor crosshairCursor];
    case CursorKind::resize_horizontal: return [NSCursor resizeLeftRightCursor];
    case CursorKind::resize_vertical: return [NSCursor resizeUpDownCursor];
    case CursorKind::wait: return [NSCursor arrowCursor];
    case CursorKind::forbidden: return [NSCursor operationNotAllowedCursor];
    // AppKit has no public help cursor; the arrow stands in.
    case CursorKind::help: return [NSCursor arrowCursor];
    case CursorKind::resize_diagonal_down:
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
        if (@available(macOS 15.0, *)) {
            return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                                             inDirections:NSCursorFrameResizeDirectionsAll];
        }
#endif
        return [NSCursor crosshairCursor];
    case CursorKind::resize_diagonal_up:
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
        if (@available(macOS 15.0, *)) {
            return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                                             inDirections:NSCursorFrameResizeDirectionsAll];
        }
#endif
        return [NSCursor crosshairCursor];
    }
    return [NSCursor arrowCursor];
}

NSString* native_string(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

std::string utf8_string(id value) {
    NSString* string = nil;
    if ([value isKindOfClass:[NSAttributedString class]]) {
        string = [(NSAttributedString*)value string];
    } else if ([value isKindOfClass:[NSString class]]) {
        string = (NSString*)value;
    }
    if (string == nil) return {};
    const char* bytes = string.UTF8String;
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

std::uint32_t color_component_byte(CGFloat component) {
    return static_cast<std::uint32_t>(
        std::lround(std::clamp(component, 0.0, 1.0) * 255.0));
}

NSURL* native_directory_url(const std::string& path) {
    NSString* value = native_string(path);
    return value.length == 0 ? nil : [NSURL fileURLWithPath:value isDirectory:YES];
}

NSArray<UTType*>* native_allowed_types(
    const std::vector<gui_forms::HostFileDialogFilter>& filters) {
    NSMutableArray<UTType*>* types = [[NSMutableArray alloc] init];
    for (const gui_forms::HostFileDialogFilter& filter : filters) {
        for (const std::string& extension : filter.extensions) {
            std::string normalized = extension;
            while (!normalized.empty() && normalized.front() == '.') {
                normalized.erase(normalized.begin());
            }
            NSString* value = native_string(normalized);
            UTType* type = value.length == 0
                ? nil : [UTType typeWithFilenameExtension:value];
            if (type != nil && ![types containsObject:type]) {
                [types addObject:type];
            }
        }
    }
    return types;
}

// A file panel's filter chooser: with more than one filter, a labelled
// popup of the request's filters as the panel's accessory view, starting
// on `initial_filter`, and the panel's allowed types set to that filter's.
// With one filter its types are allowed and no popup is shown; with none,
// any file. The returned popup (nil without one) tells the chosen filter
// after the run; `filter_switch` receives the popup's target, which the
// caller keeps until then.
NSPopUpButton* file_filter_popup(NSSavePanel* panel,
                                 const std::vector<gui_forms::HostFileDialogFilter>& filters,
                                 std::size_t initial_filter,
                                 GuiFormsFileFilterSwitch* __strong* filter_switch) {
    *filter_switch = nil;
    if (filters.size() < 2U) {
        NSArray<UTType*>* types = native_allowed_types(filters);
        if (types.count != 0) {
            panel.allowedContentTypes = types;
        }
        return nil;
    }
    NSMutableArray<NSArray<UTType*>*>* per_filter = [[NSMutableArray alloc] init];
    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 240, 26) pullsDown:NO];
    for (const gui_forms::HostFileDialogFilter& filter : filters) {
        const std::vector<gui_forms::HostFileDialogFilter> one{filter};
        [per_filter addObject:native_allowed_types(one)];
        NSString* label = native_string(filter.label);
        [popup addItemWithTitle:label.length == 0 ? @"Files" : label];
    }
    const std::size_t start = initial_filter < filters.size() ? initial_filter : 0U;
    [popup selectItemAtIndex:static_cast<NSInteger>(start)];
    GuiFormsFileFilterSwitch* target = [[GuiFormsFileFilterSwitch alloc] init];
    target.panel = panel;
    target.filterTypes = per_filter;
    popup.target = target;
    popup.action = @selector(filterChanged:);
    NSTextField* caption = [NSTextField labelWithString:@"Format:"];
    NSStackView* row = [NSStackView stackViewWithViews:@[caption, popup]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.edgeInsets = NSEdgeInsetsMake(8, 12, 8, 12);
    panel.accessoryView = row;
    panel.allowedContentTypes = per_filter[start];
    *filter_switch = target;
    return popup;
}

// The request's filter a popup ended on, or none without a popup.
stxlib::Optional<std::size_t> chosen_file_filter(NSPopUpButton* popup, std::size_t filter_count) {
    if (popup == nil) {
        if (filter_count == 1U) return stxlib::Optional<std::size_t>::of(0U);
        return stxlib::Optional<std::size_t>::none();
    }
    const NSInteger index = popup.indexOfSelectedItem;
    if (index < 0 || static_cast<std::size_t>(index) >= filter_count) return stxlib::Optional<std::size_t>::none();
    const std::size_t chosen = static_cast<std::size_t>(index);
    return stxlib::Optional<std::size_t>::of(chosen);
}

HostPathDialogResult native_path_result(NSModalResponse response,
                                        NSArray<NSURL*>* urls) {
    HostPathDialogResult result;
    if (response != NSModalResponseOK) {
        return result;
    }
    result.outcome = HostDialogOutcome::accepted;
    for (NSURL* url in urls) {
        const std::string path = utf8_string(url.path);
        if (!path.empty()) {
            result.paths.push_back(path);
        }
    }
    return result;
}

// Test cancellation on the main queue without Objective-C blocks (owner
// decision V2-1): a named context, adopted and deleted by a named
// dispatch_function_t; the context keeps the panel or alert alive until the
// work runs, as the replaced blocks did.
struct PanelCancelWork final {
    NSSavePanel* panel;
};

void cancel_panel_on_main(void* context) {
    const std::unique_ptr<PanelCancelWork> work(static_cast<PanelCancelWork*>(context));
    [(*work).panel cancel:nil];
}

struct AlertCancelWork final {
    NSAlert* alert;
};

void cancel_alert_on_main(void* context) {
    const std::unique_ptr<AlertCancelWork> work(static_cast<AlertCancelWork*>(context));
    [(*work).alert.window orderOut:nil];
    [NSApp abortModal];
}

// Keeps Escape on the cancel choice when Cancel is also the declared
// default: the window's default button cell then gives Cancel Return as its
// key equivalent, and AppKit ignores Escape
// (tests/macos_message_dialog_key_tests.mm). A second button carries Escape
// and clicks Cancel through its target and action, in place of a block-only
// event monitor (owner decision V2-1); [alert runModal] then handles every
// key itself. Hidden views take no key equivalents, so the button is
// zero-sized rather than hidden, and it is kept out of the accessibility
// tree. It joins the content view after [alert layout], which runModal's own
// layout keeps; as the accessory view it would make the alert 16 pt taller.
// The alert's view tree owns the button; the button only observes Cancel.
void add_escape_clicks_cancel(NSAlert* alert, NSButton* cancel_button) {
    [alert layout];
    NSButton* escape = [[NSButton alloc] initWithFrame:NSZeroRect];
    escape.keyEquivalent = @"\e";
    escape.target = cancel_button;
    escape.action = @selector(performClick:);
    [escape setAccessibilityElement:NO];
    [alert.window.contentView addSubview:escape];
}

// Whether the user's `event` must be held back from the alert the host runs
// while a model holds control (session_directory.hpp, native_input_held):
// pointer, key and scroll input to the alert itself or to any window that
// does not work when modal. A panel that works when modal (the take-back
// dialog, ApplicationWindow::outside_modality) gets its input.
bool user_event(NSEvent* event) {
    switch (event.type) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp:
    case NSEventTypeOtherMouseDragged:
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
    case NSEventTypeScrollWheel: return true;
    // Every other event type (moves, enter and exit, application, system
    // and periodic events) is not the user's input to a window, and passes.
    default: return false;
    }
}

bool press_event(NSEvent* event) {
    const NSEventType type = event.type;
    return type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown ||
        type == NSEventTypeOtherMouseDown || (type == NSEventTypeKeyDown && !event.isARepeat);
}

bool reaches_outside_modality(NSEvent* event, NSWindow* alert_window) {
    NSWindow* target = event.window;
    const bool key = event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp;
    if (key) target = NSApp.keyWindow;
    if (target == nil || target == alert_window) return false;
    return [target isKindOfClass:[NSPanel class]] && static_cast<NSPanel*>(target).worksWhenModal;
}

// Runs `alert` modally. While a model holds control, the user's input to it
// is held back and a press offers the take-back dialog, which works over the
// alert; once the user has taken control back the alert answers the user as
// usual, and it stays open throughout (the take-back leaves it in place).
// The loop is the alert's own modal session: it takes each event, holds the
// user's, and otherwise passes it on only to the alert, a panel that works
// when modal, or the application itself, as runModal does. With no model
// holding control it is [alert runModal].
//
// [NSApp runModalSession:] also takes and sends any events waiting in the
// queue, past the loop's hold: input that arrives while it runs (a click the
// window server delivers then, or one posted by main-queue work it runs)
// reaches the alert's buttons directly. So the buttons answer through
// GuiFormsHeldAlertAnswer, which applies the same hold at the button: such a
// click offers the take-back dialog instead of answering the alert.
NSModalResponse run_alert_modal(NSAlert* alert) {
    if (!gui_forms::detail::native_input_held(false)) {
        const NSModalResponse plain = [alert runModal];
        return plain;
    }
    [alert layout];
    // Kept to the end of the scope: the buttons only observe it.
    NS_VALID_UNTIL_END_OF_SCOPE GuiFormsHeldAlertAnswer* const answer = [[GuiFormsHeldAlertAnswer alloc] init];
    for (NSButton* button in alert.buttons) {
        button.target = answer;
        button.action = @selector(buttonPressed:);
    }
    NSWindow* const window = alert.window;
    [window center];
    NSModalSession session = [NSApp beginModalSessionForWindow:window];
    NSModalResponse response = NSModalResponseContinue;
    while (response == NSModalResponseContinue) {
        @autoreleasepool {
            NSDate* const until = [NSDate dateWithTimeIntervalSinceNow:0.05];
            NSEvent* const event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until
                                                         inMode:NSModalPanelRunLoopMode dequeue:YES];
            if (event != nil) {
                const bool input = user_event(event);
                const bool outside = input && reaches_outside_modality(event, window);
                if (input && !outside && gui_forms::detail::native_input_held(press_event(event))) {
                    // Held: the press offered the take-back dialog.
                } else if (!input || outside || event.window == window ||
                           event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp) {
                    // Keys go to the key window, which is the alert or a
                    // panel that works when modal.
                    [NSApp sendEvent:event];
                } else if (press_event(event)) {
                    // A press on another window while the alert is up, as
                    // runModal answers it.
                    NSBeep();
                }
            }
            response = [NSApp runModalSession:session];
        }
    }
    [NSApp endModalSession:session];
    [window orderOut:nil];
    return response;
}

void schedule_test_panel_cancel(NSSavePanel* panel, bool enabled) {
    if (!enabled) {
        return;
    }
    PanelCancelWork* const work = new PanelCancelWork{panel};
    dispatch_async_f(dispatch_get_main_queue(), work, &cancel_panel_on_main);
}

void schedule_test_alert_cancel(NSAlert* alert, bool enabled) {
    if (!enabled) {
        return;
    }
    AlertCancelWork* const work = new AlertCancelWork{alert};
    dispatch_async_f(dispatch_get_main_queue(), work, &cancel_alert_on_main);
}

// A save panel's chosen file that does not exist yet: with CheckFileExists
// the user is told to choose an existing one (false); with CreatePrompt the
// user is asked whether to create it (true for Yes). The panel is shown
// again after false, with the name kept.
bool accept_missing_file(NSURL* url, bool must_exist, bool cancel_for_testing) {
    NSAlert* alert = [[NSAlert alloc] init];
    NSString* const name = url.lastPathComponent != nil ? url.lastPathComponent : @"";
    if (must_exist) {
        alert.alertStyle = NSAlertStyleWarning;
        alert.messageText = [NSString stringWithFormat:@"%@ does not exist.", name];
        alert.informativeText = @"Choose an existing file.";
        [alert addButtonWithTitle:@"OK"];
        schedule_test_alert_cancel(alert, cancel_for_testing);
        static_cast<void>([alert runModal]);
        return false;
    }
    alert.messageText = [NSString stringWithFormat:@"%@ does not exist. Create it?", name];
    [alert addButtonWithTitle:@"Yes"];
    [alert addButtonWithTitle:@"No"];
    schedule_test_alert_cancel(alert, cancel_for_testing);
    const NSModalResponse response = [alert runModal];
    const bool accepted = response == NSAlertFirstButtonReturn;
    return accepted;
}

class AppKitHostServices final : public HostServices {
public:
    explicit AppKitHostServices(const gui_forms::host::MacHostServiceOptions& options)
        : HostServices(gui_forms::host::macos_capabilities()),
          cancel_dialogs_for_testing_(options.cancel_dialogs_for_testing) {
        if (options.clipboard_name.empty()) {
            pasteboard_ = [NSPasteboard generalPasteboard];
        } else {
            NSString* name = [[NSString alloc]
                initWithBytes:options.clipboard_name.data()
                       length:options.clipboard_name.size()
                     encoding:NSUTF8StringEncoding];
            pasteboard_ = name == nil ? nil : [NSPasteboard pasteboardWithName:name];
        }
    }
    ~AppKitHostServices() override {
        shutdown();
    }

protected:
    HostMonitorResult query_monitors_impl() override {
        HostMonitorResult result;
        NSArray<NSScreen*>* screens = [NSScreen screens];
        NSScreen* primary = screens.firstObject;
        double desktop_top = 0.0;
        for (NSScreen* screen in screens) {
            desktop_top = std::max(desktop_top, NSMaxY(screen.frame));
        }
        std::size_t index = 0;
        for (NSScreen* screen in screens) {
            const NSRect frame = screen.frame;
            const NSRect visible = screen.visibleFrame;
            NSNumber* screen_number = screen.deviceDescription[@"NSScreenNumber"];
            std::string identifier = "appkit.screen." + std::to_string(index);
            if (screen_number != nil) {
                identifier = "appkit.display." +
                    std::to_string(screen_number.unsignedIntValue);
            }
            result.monitors.push_back({
                std::move(identifier),
                {frame.origin.x, desktop_top - NSMaxY(frame),
                 frame.size.width, frame.size.height},
                {visible.origin.x, desktop_top - NSMaxY(visible),
                 visible.size.width, visible.size.height},
                screen.backingScaleFactor,
                screen == primary});
            ++index;
        }
        if (result.monitors.empty()) {
            result.status.error = HostServiceError::backend_failure;
        }
        return result;
    }

    // The granted backdrop on this window (stage T5, C3).
    HostServiceStatus apply_window_backdrop_impl(gui_forms::WindowBackdrop backdrop) override {
        NSView* const raster = backdrop_view_;
        const HostServiceStatus applied = gui_forms::host::detail::apply_macos_backdrop(raster, backdrop);
        return applied;
    }

    // Corners: AppKit has no public corner API and never reshapes a window,
    // so only what the window is can be reported. A full-screen window and a
    // borderless one have square corners. A titled window is rounded by the
    // radius its release draws (titled_window_corner_radius), and its
    // client area's top corners meet the title bar, so they stay square
    // (WindowCornerReport::top_square); a window whose content runs under a
    // transparent title bar is rounded at all four. `complied` says whether
    // the window is as asked: `system` always, `square` where the corners
    // are square, `round` where they are rounded.
    gui_forms::HostWindowCornersResult apply_window_corners_impl(
        gui_forms::WindowCornerPreference preference) override {
        gui_forms::HostWindowCornersResult result{};
        NSView* const view = backdrop_view_;
        NSWindow* const window = view == nil ? nil : view.window;
        const bool asked_system = preference == gui_forms::WindowCornerPreference::system;
        const bool asked_square = preference == gui_forms::WindowCornerPreference::square;
        const bool asked_round = preference == gui_forms::WindowCornerPreference::round;
        if (window == nil) {
            result.report.complied = asked_system;
            return result;
        }
        const NSWindowStyleMask mask = [window styleMask];
        const bool full_screen = (mask & NSWindowStyleMaskFullScreen) != 0;
        const bool borderless = (mask & NSWindowStyleMaskTitled) == 0;
        if (full_screen || borderless) {
            result.report.shape = gui_forms::WindowCornerShape::square;
            result.report.source = gui_forms::WindowCornerSource::platform;
            result.report.complied = asked_system || asked_square;
            return result;
        }
        const CornerReading reading = titled_window_corner_radius(window);
        if (reading.source == gui_forms::WindowCornerSource::unknown) {
            // No source says how round the window is: unknown, drawn square
            // (ACTIVE_BORDER_STUDY ruling 4).
            result.report.complied = asked_system;
            return result;
        }
        result.report.shape = gui_forms::WindowCornerShape::rounded;
        result.report.radius = reading.radius;
        result.report.source = reading.source;
        result.report.top_square = (mask & NSWindowStyleMaskFullSizeContentView) == 0;
        result.report.complied = asked_system || asked_round;
        return result;
    }

    // Window's chrome properties on this window. macOS has no per-window
    // icon (the Dock shows the application's) and no taskbar button, so
    // Icon and ShowInTaskbar are unsupported. A tool window frame is the
    // ordinary frame: the utility style belongs to panels only.
    gui_forms::WindowChromeFields supported_window_chrome() const noexcept override {
        const std::uint32_t unsupported = static_cast<std::uint32_t>(gui_forms::WindowChromeField::icon) |
                                          static_cast<std::uint32_t>(gui_forms::WindowChromeField::show_in_taskbar);
        const gui_forms::WindowChromeFields supported{gui_forms::WindowChromeFields::all_bits & ~unsupported};
        return supported;
    }

    gui_forms::HostWindowChromeResult apply_window_chrome_impl(const gui_forms::WindowChrome& chrome,
                                                               gui_forms::WindowChromeFields fields) override {
        using gui_forms::WindowChromeField;
        gui_forms::HostWindowChromeResult result{};
        NSView* const view = backdrop_view_;
        NSWindow* const window = view == nil ? nil : view.window;
        if (window == nil) {
            // No window yet (or no longer): nothing is applied.
            return result;
        }
        if (fields.contains(WindowChromeField::text)) {
            NSString* title = [[NSString alloc] initWithBytes:chrome.text.data()
                                                       length:chrome.text.size()
                                                     encoding:NSUTF8StringEncoding];
            if (title != nil) {
                window.title = title;
                result.applied.add(WindowChromeField::text);
            }
        }
        const std::uint32_t frame_bits = static_cast<std::uint32_t>(WindowChromeField::form_border_style) |
                                         static_cast<std::uint32_t>(WindowChromeField::minimize_box) |
                                         static_cast<std::uint32_t>(WindowChromeField::maximize_box) |
                                         static_cast<std::uint32_t>(WindowChromeField::control_box);
        if ((fields.bits & frame_bits) != 0U) {
            apply_frame(window, chrome, fields);
            result.applied.bits = result.applied.bits | (fields.bits & frame_bits);
        }
        if (fields.contains(WindowChromeField::top_most)) {
            window.level = chrome.top_most ? NSFloatingWindowLevel : NSNormalWindowLevel;
            result.applied.add(WindowChromeField::top_most);
        }
        if (fields.contains(WindowChromeField::opacity)) {
            window.alphaValue = static_cast<CGFloat>(chrome.opacity);
            result.applied.add(WindowChromeField::opacity);
        }
        if (fields.contains(WindowChromeField::minimum_size)) {
            // A side of zero goes back to the minimum the window opened with.
            if (!opened_minimum_known_) {
                opened_minimum_ = window.contentMinSize;
                opened_minimum_known_ = true;
            }
            const CGFloat width = chrome.minimum_size.width > 0.0
                ? static_cast<CGFloat>(chrome.minimum_size.width) : opened_minimum_.width;
            const CGFloat height = chrome.minimum_size.height > 0.0
                ? static_cast<CGFloat>(chrome.minimum_size.height) : opened_minimum_.height;
            window.contentMinSize = NSMakeSize(width, height);
            result.applied.add(WindowChromeField::minimum_size);
        }
        if (fields.contains(WindowChromeField::start_position) && !window.visible) {
            apply_start_position(window, chrome.start_position);
            result.applied.add(WindowChromeField::start_position);
        }
        if (fields.contains(WindowChromeField::window_state)) {
            apply_window_state(window, chrome.window_state);
            result.applied.add(WindowChromeField::window_state);
        }
        return result;
    }

    HostServiceStatus set_cursor_impl(CursorKind cursor) override {
        [native_cursor(cursor) set];
        return {};
    }

    HostServiceStatus set_custom_cursor_impl(const gui_forms::CursorImagesPtr& images,
                                              const gui_forms::CursorImage& selected) override {
        for (const CursorEntry& entry : cursors_) {
            if (entry.images == images && entry.scale == selected.scale) {
                [entry.native set];
                return {};
            }
        }
        const gui_forms::CursorImage& first = (*images).images().front();
        const NSSize size = NSMakeSize(first.width / first.scale, first.height / first.scale);
        NSImage* image = [[NSImage alloc] initWithSize:size];
        for (const gui_forms::CursorImage& pixels : (*images).images()) {
            NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
                initWithBitmapDataPlanes:nullptr pixelsWide:pixels.width pixelsHigh:pixels.height
                bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                bytesPerRow:pixels.width * 4 bitsPerPixel:32];
            if (rep == nil || rep.bitmapData == nullptr) return {HostServiceError::backend_failure};
            unsigned char* bytes = rep.bitmapData;
            for (std::size_t i = 0; i < pixels.rgba.size(); ++i) {
                const gui_forms::Color color = pixels.rgba[i];
                bytes[i * 4] = color.red; bytes[i * 4 + 1] = color.green;
                bytes[i * 4 + 2] = color.blue; bytes[i * 4 + 3] = color.alpha;
            }
            rep.size = size;
            [image addRepresentation:rep];
        }
        NSCursor* cursor = [[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(
            (*images).normalized_x() * size.width, (*images).normalized_y() * size.height)];
        if (cursor == nil) return {HostServiceError::backend_failure};
        [cursor set];
        if (cursors_.size() >= 32) cursors_.erase(cursors_.begin());
        cursors_.push_back({images, selected.scale, cursor});
        return {};
    }

    HostServiceStatus set_pointer_capture_impl(bool, std::uint64_t) override {
        // AppKit owns mouse-drag delivery from mouseDown through mouseUp. This
        // acknowledgment keeps that native sequence synchronized with the
        // retained capture owner; it does not install a global event tap.
        return {};
    }

    HostServiceStatus set_relative_pointer_impl(const gui_forms::HostRelativePointerRequest& request) override {
        if (relative_.active) release_relative_pointer();
        if (!request.active) {
            return {};
        }
        coalescing_before_ = [NSEvent isMouseCoalescingEnabled];
        [NSEvent setMouseCoalescingEnabled:NO];
        if (request.mode == gui_forms::RelativePointerMode::locked) {
            const CGError disassociated = CGAssociateMouseAndMouseCursorPosition(false);
            if (disassociated != kCGErrorSuccess) {
                [NSEvent setMouseCoalescingEnabled:coalescing_before_];
                return {HostServiceError::backend_failure};
            }
        }
        if (request.hide_cursor || request.mode == gui_forms::RelativePointerMode::locked) {
            [NSCursor hide];
            relative_cursor_hidden_ = true;
        }
        relative_.active = true;
        relative_.mode = request.mode;
        relative_.confine = request.confine;
        return {};
    }

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    // Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).

    // The address goes to Launch Services, which opens it in the user's
    // browser or mail program and returns at once (HostServices::open_url
    // validated it: an http, https or mailto address).
    HostServiceStatus open_url_impl(std::string_view url_utf8) override {
        NSString* const text = [[NSString alloc] initWithBytes:url_utf8.data()
                                                        length:url_utf8.size()
                                                      encoding:NSUTF8StringEncoding];
        NSURL* const address = text != nil ? [NSURL URLWithString:text] : nil;
        if (address == nil) {
            return {HostServiceError::invalid_argument};
        }
        const BOOL opened = [NSWorkspace.sharedWorkspace openURL:address];
        if (!opened) {
            return {HostServiceError::backend_failure};
        }
        return {};
    }

    // A drag to other programs: one dragging item per file, one for the
    // text, one for each media-typed item, started from the pointer event
    // AppKit is delivering (the press or the move start_drag is called
    // from) through the window's view. AppKit then runs the drag from the
    // run loop; GuiFormsDragSource reports its end.
    HostServiceStatus start_drag_impl(std::uint64_t session_id,
                                      const gui_forms::HostDragSourceRequest& request) override {
        if (drag_without_session_) {
            // macos_start_drag_for_testing: the source without a session.
            drag_without_session_ = false;
            drag_source_ = make_drag_source(session_id, request.allowed_effects, NSEventTypeLeftMouseDragged);
            return {};
        }
        NSView* const view = backdrop_view_;
        NSEvent* const event = [NSApp currentEvent];
        if (view == nil || view.window == nil || event == nil || event.window != view.window ||
            !drag_start_event(event.type)) {
            return {HostServiceError::backend_failure};
        }
        NSMutableArray<NSDraggingItem*>* const items = [NSMutableArray array];
        const NSPoint pointer = [view convertPoint:event.locationInWindow fromView:nil];
        for (const DragDataItem& item : request.items) {
            if (const DragFileListData* files = std::get_if<DragFileListData>(&item)) {
                for (const std::string& path_utf8 : (*files).paths_utf8) {
                    NSString* const path = native_string(path_utf8);
                    if (path == nil) return {HostServiceError::invalid_argument};
                    NSURL* const url = [NSURL fileURLWithPath:path];
                    NSImage* const icon = [NSWorkspace.sharedWorkspace iconForFile:path];
                    [items addObject:dragging_item(url, icon, pointer, items.count)];
                }
            } else if (const DragTextData* text = std::get_if<DragTextData>(&item)) {
                NSString* const string = native_string((*text).text_utf8);
                if (string == nil) return {HostServiceError::invalid_argument};
                NSImage* const icon = [NSWorkspace.sharedWorkspace iconForContentType:UTTypePlainText];
                [items addObject:dragging_item(string, icon, pointer, items.count)];
            } else if (const DragBinaryData* binary = std::get_if<DragBinaryData>(&item)) {
                NSPasteboardType const type = pasteboard_type((*binary).media_type);
                if (type == nil) return {HostServiceError::invalid_argument};
                NSData* const data = [NSData dataWithBytes:(*binary).bytes.data() length:(*binary).bytes.size()];
                NSPasteboardItem* const written = [[NSPasteboardItem alloc] init];
                if (![written setData:data forType:type]) return {HostServiceError::backend_failure};
                UTType* const content = [UTType typeWithIdentifier:type];
                NSImage* const icon = [NSWorkspace.sharedWorkspace
                    iconForContentType:content != nil ? content : UTTypeData];
                [items addObject:dragging_item(written, icon, pointer, items.count)];
            }
        }
        if (items.count == 0) return {HostServiceError::invalid_argument};
        GuiFormsDragSource* const source = make_drag_source(session_id, request.allowed_effects, event.type);
        NSDraggingSession* const session = [view beginDraggingSessionWithItems:items event:event source:source];
        if (session == nil) {
            source.services = nullptr;
            source.holdUntilEnd = nil;
            return {HostServiceError::backend_failure};
        }
        session.animatesToStartingPositionsOnCancelOrFail = YES;
        drag_source_ = source;
        return {};
    }
#endif

    HostServiceStatus warp_pointer_impl(gui_forms::Point position) override {
        NSView* const view = backdrop_view_;
        NSWindow* const window = view.window;
        NSScreen* const primary = NSScreen.screens.firstObject;
        if (view == nil || window == nil || primary == nil) {
            return {HostServiceError::backend_failure};
        }
        // View (flipped, window logical units) to window, to screen (y up
        // from the primary screen's bottom), to Quartz's global space (y
        // down from the primary screen's top).
        const NSPoint in_window = [view convertPoint:NSMakePoint(position.x, position.y) toView:nil];
        const NSPoint on_screen = [window convertPointToScreen:in_window];
        const CGPoint global = CGPointMake(on_screen.x, NSMaxY(primary.frame) - on_screen.y);
        const CGError warped = CGWarpMouseCursorPosition(global);
        // A warp suppresses local mouse events for a moment; associating
        // again (or keeping a lock's disassociation) ends that at once.
        const bool locked = relative_.active && relative_.mode == gui_forms::RelativePointerMode::locked;
        static_cast<void>(CGAssociateMouseAndMouseCursorPosition(locked ? false : true));
        if (warped != kCGErrorSuccess) {
            return {HostServiceError::backend_failure};
        }
        return {};
    }

#if !defined(GUI_FORMS_HIGH_INTEGRITY) || defined(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    HostClipboardTextResult read_clipboard_text_impl() override {
        HostClipboardTextResult result;
        if (pasteboard_ == nil) {
            result.status.error = HostServiceError::backend_failure;
            return result;
        }
        result.generation = static_cast<std::uint64_t>(pasteboard_.changeCount);
        NSString* value = [pasteboard_ stringForType:NSPasteboardTypeString];
        if (value == nil) {
            return result;
        }
        NSData* encoded = [value dataUsingEncoding:NSUTF8StringEncoding];
        if (encoded == nil) {
            result.status.error = HostServiceError::backend_failure;
            return result;
        }
        if (encoded.length != 0) {
            result.text_utf8.assign(static_cast<const char*>(encoded.bytes), encoded.length);
        }
        result.has_text = true;
        return result;
    }

    HostServiceStatus write_clipboard_text_impl(std::string_view text_utf8) override {
        if (pasteboard_ == nil) {
            return {HostServiceError::backend_failure};
        }
        NSString* value = [[NSString alloc] initWithBytes:text_utf8.data()
                                                   length:text_utf8.size()
                                                 encoding:NSUTF8StringEncoding];
        if (value == nil) {
            return {HostServiceError::invalid_utf8};
        }
        [pasteboard_ clearContents];
        return [pasteboard_ setString:value forType:NSPasteboardTypeString]
                   ? HostServiceStatus{}
                   : HostServiceStatus{HostServiceError::backend_failure};
    }
#endif

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    gui_forms::HostClipboardFilesResult read_clipboard_files_impl() override {
        gui_forms::HostClipboardFilesResult result;
        if (pasteboard_ == nil) {
            result.status.error = HostServiceError::backend_failure;
            return result;
        }
        result.generation = static_cast<std::uint64_t>(pasteboard_.changeCount);
        NSArray<NSURL*>* urls = [pasteboard_ readObjectsForClasses:@[NSURL.class]
            options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
        if (urls.count > maximum_dialog_paths) {
            result.status.error = HostServiceError::too_large;
            return result;
        }
        for (NSURL* url in urls) {
            if (url.isFileURL && url.path != nil) {
                result.paths_utf8.push_back(utf8_string(url.path));
            }
        }
        if (static_cast<std::uint64_t>(pasteboard_.changeCount) != result.generation) {
            result.status.error = HostServiceError::backend_failure;
        }
        return result;
    }

    gui_forms::HostClipboardImageResult read_clipboard_image_impl() override {
        return gui_forms::host::detail::read_appkit_clipboard_image(pasteboard_);
    }

    HostServiceStatus write_clipboard_image_impl(gui_forms::HostImageView image) override {
        return gui_forms::host::detail::write_appkit_clipboard_image(pasteboard_, image);
    }
#endif

    HostDialogResult show_dialog_impl(const HostDialogRequest& request) override {
        if (const HostMessageDialogRequest* payload_pointer =
                std::get_if<HostMessageDialogRequest>(&request.payload)) {
                const HostMessageDialogRequest& payload = *payload_pointer;
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = native_string(payload.title);
                alert.informativeText = native_string(payload.message);
                switch (payload.icon) {
                case HostMessageIcon::warning:
                case HostMessageIcon::question:
                    alert.alertStyle = NSAlertStyleWarning;
                    break;
                case HostMessageIcon::error:
                    alert.alertStyle = NSAlertStyleCritical;
                    break;
                case HostMessageIcon::none:
                case HostMessageIcon::information:
                default:
                    // Fallback: a value outside HostMessageIcon shows as information.
                    alert.alertStyle = NSAlertStyleInformational;
                    break;
                }
                // GUI.Forms' own icon for the message (the stock set) in place
                // of the application icon AppKit shows by default.
                if (payload.icon_image) {
                    NSImage* icon = alert_icon_image(*payload.icon_image);
                    if (icon != nil) alert.icon = icon;
                }
                std::vector<std::pair<NSString*, HostDialogChoice>> buttons;
                switch (payload.buttons) {
                case HostMessageButtons::ok:
                default:
                    // Fallback: a value outside HostMessageButtons offers OK alone.
                    buttons = {{@"OK", HostDialogChoice::ok}};
                    break;
                case HostMessageButtons::ok_cancel:
                    buttons = {{@"OK", HostDialogChoice::ok},
                               {@"Cancel", HostDialogChoice::cancel}};
                    break;
                case HostMessageButtons::yes_no:
                    buttons = {{@"Yes", HostDialogChoice::yes},
                               {@"No", HostDialogChoice::no}};
                    break;
                case HostMessageButtons::yes_no_cancel:
                    buttons = {{@"Yes", HostDialogChoice::yes},
                               {@"No", HostDialogChoice::no},
                               {@"Cancel", HostDialogChoice::cancel}};
                    break;
                case HostMessageButtons::retry_cancel:
                    buttons = {{@"Retry", HostDialogChoice::retry},
                               {@"Cancel", HostDialogChoice::cancel}};
                    break;
                case HostMessageButtons::abort_retry_ignore:
                    buttons = {{@"Abort", HostDialogChoice::abort},
                               {@"Retry", HostDialogChoice::retry},
                               {@"Ignore", HostDialogChoice::ignore}};
                    break;
                case HostMessageButtons::cancel_try_continue:
                    buttons = {{@"Cancel", HostDialogChoice::cancel},
                               {@"Try Again", HostDialogChoice::try_again},
                               {@"Continue", HostDialogChoice::continue_}};
                    break;
                }
                // right_align and rtl_reading: an NSAlert is laid out in the
                // system's own direction, so the box shows as without them.
                // Help: the alert's own help button (macOS has no F1 help);
                // it cannot be the default button.
                // Held to the end of the run: the alert's delegate is weak.
                __attribute__((objc_precise_lifetime)) GuiFormsAlertHelp* help = nil;
                if (payload.help_button) {
                    help = [[GuiFormsAlertHelp alloc] init];
                    help.services = this;
                    help.requestId = request.request_id;
                    alert.showsHelp = YES;
                    alert.delegate = help;
                }
                NSButton* defaultButton = nil;
                NSButton* cancelButton = nil;
                for (std::size_t index = 0U; index < buttons.size(); ++index) {
                    const std::pair<NSString*, HostDialogChoice>& button_choice =
                        buttons[index];
                    NSString* const title = button_choice.first;
                    const HostDialogChoice choice = button_choice.second;
                    NSButton* button = [alert addButtonWithTitle:title];
                    // Keep cancellation and default activation independent:
                    // Escape must always reach an admitted cancel choice, while
                    // the window's default cell preserves Return even when
                    // Cancel itself is the declared default.
                    button.keyEquivalent = choice == HostDialogChoice::cancel
                        ? @"\e" : @"";
                    if (choice == payload.default_choice) defaultButton = button;
                    if (choice == HostDialogChoice::cancel) cancelButton = button;
                }
                if (defaultButton != nil) {
                    alert.window.defaultButtonCell = defaultButton.cell;
                }
                if (cancelButton != nil && defaultButton == cancelButton) {
                    add_escape_clicks_cancel(alert, cancelButton);
                }
                schedule_test_alert_cancel(alert, cancel_dialogs_for_testing_);
                const NSModalResponse response = run_alert_modal(alert);
                HostMessageDialogResult value;
                const NSInteger index = response - NSAlertFirstButtonReturn;
                if (index >= 0 && static_cast<std::size_t>(index) < buttons.size()) {
                    value.choice = buttons[static_cast<std::size_t>(index)].second;
                    value.outcome = value.choice == HostDialogChoice::cancel
                        ? HostDialogOutcome::cancelled
                        : HostDialogOutcome::accepted;
                }
                return {{}, request.request_id, value};
        }
        if (const HostOpenFileDialogRequest* payload_pointer =
                std::get_if<HostOpenFileDialogRequest>(&request.payload)) {
                const HostOpenFileDialogRequest& payload = *payload_pointer;
                // NSOpenPanel lets the user choose only existing files in
                // existing folders: check_file_exists and check_path_exists
                // false are the platform difference the request documents.
                NSOpenPanel* panel = [NSOpenPanel openPanel];
                panel.title = native_string(payload.title);
                panel.directoryURL = native_directory_url(payload.initial_directory);
                panel.nameFieldStringValue = native_string(payload.suggested_name);
                panel.canChooseFiles = YES;
                panel.canChooseDirectories = NO;
                panel.allowsMultipleSelection = payload.allow_multiple;
                GuiFormsFileFilterSwitch* filter_switch = nil;
                NSPopUpButton* popup = file_filter_popup(
                    panel, payload.filters, payload.initial_filter, &filter_switch);
                if (popup != nil) {
                    panel.accessoryViewDisclosed = YES;
                }
                schedule_test_panel_cancel(panel, cancel_dialogs_for_testing_);
                const NSModalResponse response = [panel runModal];
                // The switch was kept for the run; it stops observing now.
                filter_switch.panel = nil;
                HostPathDialogResult value = native_path_result(response, panel.URLs);
                if (value.outcome == HostDialogOutcome::accepted) {
                    value.chosen_filter = chosen_file_filter(popup, payload.filters.size());
                }
                return {{}, request.request_id, std::move(value)};
        }
        if (const HostSaveFileDialogRequest* payload_pointer =
                std::get_if<HostSaveFileDialogRequest>(&request.payload)) {
                const HostSaveFileDialogRequest& payload = *payload_pointer;
                // NSSavePanel always requires an existing folder
                // (check_path_exists) and asks before replacing a file.
                // CheckFileExists and CreatePrompt are applied after the
                // panel closes: a refused name shows a new panel in the
                // folder chosen, with the name kept.
                NSURL* directory = native_directory_url(payload.initial_directory);
                NSString* name = native_string(payload.suggested_name);
                for (;;) {
                    NSSavePanel* panel = [NSSavePanel savePanel];
                    panel.title = native_string(payload.title);
                    panel.directoryURL = directory;
                    panel.nameFieldStringValue = name;
                    GuiFormsFileFilterSwitch* filter_switch = nil;
                    NSPopUpButton* popup = nil;
                    if (payload.filters.empty() && !payload.default_extension.empty()) {
                        gui_forms::HostFileDialogFilter fallback;
                        fallback.extensions.push_back(payload.default_extension);
                        NSArray<UTType*>* types = native_allowed_types({fallback});
                        if (types.count != 0) {
                            panel.allowedContentTypes = types;
                        }
                    } else {
                        popup = file_filter_popup(panel, payload.filters, payload.initial_filter, &filter_switch);
                    }
                    schedule_test_panel_cancel(panel, cancel_dialogs_for_testing_);
                    const NSModalResponse response = [panel runModal];
                    // The switch was kept for the run; it stops observing now.
                    filter_switch.panel = nil;
                    NSURL* const chosen = panel.URL;
                    NSArray<NSURL*>* urls = chosen == nil ? @[] : @[chosen];
                    HostPathDialogResult value = native_path_result(response, urls);
                    const bool checked = payload.check_file_exists || payload.create_prompt;
                    if (value.outcome == HostDialogOutcome::accepted && checked && chosen != nil &&
                        ![[NSFileManager defaultManager] fileExistsAtPath:chosen.path] &&
                        !accept_missing_file(chosen, payload.check_file_exists, cancel_dialogs_for_testing_)) {
                        directory = chosen.URLByDeletingLastPathComponent;
                        name = chosen.lastPathComponent != nil ? chosen.lastPathComponent : @"";
                        continue;
                    }
                    if (value.outcome == HostDialogOutcome::accepted) {
                        value.chosen_filter = chosen_file_filter(popup, payload.filters.size());
                    }
                    return {{}, request.request_id, std::move(value)};
                }
        }
        if (const HostFolderDialogRequest* payload_pointer =
                std::get_if<HostFolderDialogRequest>(&request.payload)) {
                const HostFolderDialogRequest& payload = *payload_pointer;
                NSOpenPanel* panel = [NSOpenPanel openPanel];
                panel.title = native_string(payload.title);
                panel.directoryURL = native_directory_url(payload.initial_directory);
                panel.canChooseFiles = NO;
                panel.canChooseDirectories = YES;
                panel.allowsMultipleSelection = NO;
                schedule_test_panel_cancel(panel, cancel_dialogs_for_testing_);
                return {{}, request.request_id,
                        native_path_result([panel runModal], panel.URLs)};
        }
        if (const HostColorDialogRequest* payload_pointer =
                std::get_if<HostColorDialogRequest>(&request.payload)) {
                const HostColorDialogRequest& payload = *payload_pointer;
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = native_string(payload.title);
                NSColorWell* well = [[NSColorWell alloc]
                    initWithFrame:NSMakeRect(0.0, 0.0, 180.0, 28.0)];
                const CGFloat red = static_cast<CGFloat>((payload.initial_rgba >> 24U) & 0xFFU) / 255.0;
                const CGFloat green = static_cast<CGFloat>((payload.initial_rgba >> 16U) & 0xFFU) / 255.0;
                const CGFloat blue = static_cast<CGFloat>((payload.initial_rgba >> 8U) & 0xFFU) / 255.0;
                const CGFloat alpha = payload.allow_alpha
                    ? static_cast<CGFloat>(payload.initial_rgba & 0xFFU) / 255.0 : 1.0;
                well.color = [NSColor colorWithSRGBRed:red green:green blue:blue alpha:alpha];
                alert.accessoryView = well;
                [alert addButtonWithTitle:@"OK"];
                [alert addButtonWithTitle:@"Cancel"];
                schedule_test_alert_cancel(alert, cancel_dialogs_for_testing_);
                const NSModalResponse response = run_alert_modal(alert);
                HostColorDialogResult value;
                if (response == NSAlertFirstButtonReturn) {
                    NSColor* color = [well.color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
                    value.outcome = HostDialogOutcome::accepted;
                    value.rgba =
                        (color_component_byte(color.redComponent) << 24U) |
                        (color_component_byte(color.greenComponent) << 16U) |
                        (color_component_byte(color.blueComponent) << 8U) |
                        color_component_byte(
                            payload.allow_alpha ? color.alphaComponent : 1.0);
                }
                return {{}, request.request_id, value};
        }
        return {{HostServiceError::unsupported}, request.request_id,
                HostMessageDialogResult{}};
    }

    HostServiceStatus play_sound_cue_impl(
        const HostSoundCueRequest& request) override {
        NSString* name = @"Ping";
        switch (request.cue) {
        case HostSoundCue::notification: name = @"Ping"; break;
        case HostSoundCue::success: name = @"Glass"; break;
        case HostSoundCue::warning: name = @"Funk"; break;
        case HostSoundCue::error: name = @"Basso"; break;
        case HostSoundCue::operation_complete: name = @"Pop"; break;
        default:
            // Fallback: a value outside HostSoundCue plays the notification cue.
            name = @"Ping";
            break;
        }
        NSSound* sound = [NSSound soundNamed:name];
        if (sound == nil) {
            NSBeep();
            return {};
        }
        sound.volume = static_cast<float>(request.gain);
        return [sound play] ? HostServiceStatus{}
                            : HostServiceStatus{HostServiceError::backend_failure};
    }

    // Notification-area icons: NSStatusItems in the menu bar, created with the
    // first icon. Notification text is not offered (see AppKitStatusItems).
    HostServiceStatus add_tray_icon_impl(gui_forms::HostTrayIconId icon_id,
                                         const gui_forms::HostTrayIconRequest& request) override {
        if (!status_items_) {
            status_items_ = std::make_unique<gui_forms::host::detail::AppKitStatusItems>(
                gui_forms::Delegate<const gui_forms::HostTrayIconEvent&>::bind<
                    AppKitHostServices, &AppKitHostServices::forward_tray_event>(*this));
        }
        return (*status_items_).add(icon_id, request);
    }
    HostServiceStatus update_tray_icon_impl(gui_forms::HostTrayIconId icon_id,
                                            const gui_forms::HostTrayIconRequest& request) override {
        if (!status_items_) return {HostServiceError::backend_failure};
        return (*status_items_).update(icon_id, request);
    }
    void remove_tray_icon_impl(gui_forms::HostTrayIconId icon_id) noexcept override {
        if (status_items_) (*status_items_).remove(icon_id);
    }
    gui_forms::HostTrayMenuResult show_tray_menu_impl(gui_forms::HostTrayIconId icon_id,
                                                      const gui_forms::HostTrayMenuRequest& request) override {
        if (!status_items_) {
            gui_forms::HostTrayMenuResult refused;
            refused.status = {HostServiceError::backend_failure};
            return refused;
        }
        return (*status_items_).show_menu(icon_id, request);
    }

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    // Desktop notifications through UNUserNotificationCenter, created with
    // the first (AppKitDesktopNotifications).
    HostServiceStatus show_desktop_notification_impl(
        gui_forms::HostDesktopNotificationId notification_id,
        const gui_forms::HostDesktopNotificationRequest& request) override {
        if (!notifications_) {
            notifications_ = std::make_unique<gui_forms::host::detail::AppKitDesktopNotifications>(
                gui_forms::Delegate<const gui_forms::HostDesktopNotificationEvent&>::bind<
                    AppKitHostServices, &AppKitHostServices::forward_notification_event>(*this));
        }
        return (*notifications_).show(notification_id, request);
    }
    void withdraw_desktop_notification_impl(gui_forms::HostDesktopNotificationId notification_id) noexcept override {
        if (notifications_) (*notifications_).withdraw(notification_id);
    }
#endif

    void shutdown_impl() noexcept override {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
        // A drag still running ends without a report (HostServices::shutdown).
        drag_source_.services = nullptr;
        drag_source_ = nil;
#endif
        status_items_.reset();
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
        notifications_.reset();
#endif
        for (const CursorEntry& entry : cursors_) {
            if ([NSCursor currentCursor] == entry.native) [[NSCursor arrowCursor] set];
        }
        cursors_.clear();
        pasteboard_ = nil;
    }

public:
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    // The window closed (macos_drag_source_window_closed): a running drag
    // ends now, reported cancelled, and is detached, so AppKit's later end
    // reports nothing and sends no release to the closed window's view.
    void source_window_closed() {
        if (drag_source_ == nil) return;
        const std::uint64_t session_id = drag_source_.sessionId;
        drag_source_.services = nullptr;
        drag_source_ = nil;
        publish_drag_source_end(gui_forms::HostDragSourceEnd{session_id, gui_forms::DragEffect::none});
    }
    // GuiFormsDragSource's report of the drag's end, at `screen_point`: the
    // end is reported, then the window receives the release of the button
    // the drag started with, which the dragging session consumed.
    void finish_drag(std::uint64_t session_id, gui_forms::DragEffect performed_effect, NSPoint screen_point,
                     NSEventType start_type) {
        if (drag_source_ != nil && drag_source_.sessionId == session_id) {
            drag_source_.services = nullptr;
            drag_source_ = nil;
        }
        publish_drag_source_end(gui_forms::HostDragSourceEnd{session_id, performed_effect});
        NSView* const view = backdrop_view_;
        if (view != nil) gui_forms::host::detail::deliver_drag_release(view, start_type, screen_point);
    }
#endif

private:
    void forward_tray_event(const gui_forms::HostTrayIconEvent& event) { publish_tray_icon_event(event); }
    void forward_notification_event(const gui_forms::HostDesktopNotificationEvent& event) {
        publish_desktop_notification_event(event);
    }

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    // Whether AppKit can start a drag from an event of `type`: a press or a
    // move with a button held.
    // NSEventType is AppKit's open set of event kinds: a test of the six
    // kinds, not a switch over them all.
    static bool drag_start_event(NSEventType type) noexcept {
        return type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseDragged ||
               type == NSEventTypeRightMouseDown || type == NSEventTypeRightMouseDragged ||
               type == NSEventTypeOtherMouseDown || type == NSEventTypeOtherMouseDragged;
    }

    static NSDragOperation native_drag_operations(gui_forms::DragEffect effects) noexcept {
        NSDragOperation operations = NSDragOperationNone;
        if (gui_forms::has_drag_effect(effects, gui_forms::DragEffect::copy)) operations |= NSDragOperationCopy;
        if (gui_forms::has_drag_effect(effects, gui_forms::DragEffect::move)) operations |= NSDragOperationMove;
        if (gui_forms::has_drag_effect(effects, gui_forms::DragEffect::link)) operations |= NSDragOperationLink;
        return operations;
    }

    // The pasteboard type a media type names: PNG and TIFF by their AppKit
    // types, another MIME type by the type identifier it maps to, and a name
    // without a slash as a type identifier already (what an inbound drag
    // reports). Nil when none applies.
    static NSPasteboardType pasteboard_type(std::string_view media_type) {
        if (media_type == "image/png") return NSPasteboardTypePNG;
        if (media_type == "image/tiff") return NSPasteboardTypeTIFF;
        NSString* const name = native_string(media_type);
        if (name == nil) return nil;
        if (media_type.find('/') == std::string_view::npos) return name;
        UTType* const type = [UTType typeWithMIMEType:name];
        return type == nil ? nil : type.identifier;
    }

    // A dragging item for `writer`, drawn as `icon` (32 points), each item
    // stepped 4 points from the last so a group shows as a stack.
    static NSDraggingItem* dragging_item(id<NSPasteboardWriting> writer, NSImage* icon, NSPoint pointer,
                                         NSUInteger index) {
        NSDraggingItem* const item = [[NSDraggingItem alloc] initWithPasteboardWriter:writer];
        const CGFloat step = 4.0 * static_cast<CGFloat>(std::min<NSUInteger>(index, 8U));
        const NSRect frame = NSMakeRect(pointer.x - 16.0 + step, pointer.y - 16.0 + step, 32.0, 32.0);
        [item setDraggingFrame:frame contents:icon];
        return item;
    }
#endif

    // Gives the pointer back: associated with the mouse, shown, and
    // AppKit's coalescing as it was. Cannot fail in a way that keeps it.
    void release_relative_pointer() noexcept {
        if (relative_.mode == gui_forms::RelativePointerMode::locked) {
            static_cast<void>(CGAssociateMouseAndMouseCursorPosition(true));
        }
        if (relative_cursor_hidden_) {
            [NSCursor unhide];
            relative_cursor_hidden_ = false;
        }
        [NSEvent setMouseCoalescingEnabled:coalescing_before_];
        relative_ = {};
    }

    // Rewrites the style bits the named frame fields govern and keeps the
    // others: titled, resizable, closable (the control box) and
    // miniaturizable; the zoom button is enabled for the maximize box.
    static void apply_frame(NSWindow* window, const gui_forms::WindowChrome& chrome,
                            gui_forms::WindowChromeFields fields) {
        using gui_forms::WindowChromeField;
        NSWindowStyleMask mask = window.styleMask;
        if (fields.contains(WindowChromeField::form_border_style)) {
            const bool none = chrome.form_border_style == gui_forms::FormBorderStyle::none;
            const bool sizable = gui_forms::form_border_style_is_sizable(chrome.form_border_style);
            mask = none ? (mask & ~NSWindowStyleMaskTitled) : (mask | NSWindowStyleMaskTitled);
            mask = sizable ? (mask | NSWindowStyleMaskResizable) : (mask & ~NSWindowStyleMaskResizable);
        }
        if (fields.contains(WindowChromeField::control_box)) {
            mask = chrome.control_box ? (mask | NSWindowStyleMaskClosable) : (mask & ~NSWindowStyleMaskClosable);
        }
        if (fields.contains(WindowChromeField::minimize_box) || fields.contains(WindowChromeField::control_box)) {
            const bool minimize = chrome.control_box && chrome.minimize_box;
            mask = minimize ? (mask | NSWindowStyleMaskMiniaturizable) : (mask & ~NSWindowStyleMaskMiniaturizable);
        }
        window.styleMask = mask;
        if (fields.contains(WindowChromeField::maximize_box) || fields.contains(WindowChromeField::control_box)) {
            NSButton* const zoom = [window standardWindowButton:NSWindowZoomButton];
            if (zoom != nil) zoom.enabled = chrome.control_box && chrome.maximize_box;
        }
    }

    // Centres the window before it is first shown: over its parent window
    // for center_parent when it has one, else on its screen. The other
    // positions keep where the window was made.
    static void apply_start_position(NSWindow* window, gui_forms::FormStartPosition position) {
        if (position == gui_forms::FormStartPosition::center_parent && window.parentWindow != nil) {
            const NSRect around = window.parentWindow.frame;
            const NSRect frame = window.frame;
            const NSPoint origin = NSMakePoint(NSMidX(around) - frame.size.width / 2.0,
                                               NSMidY(around) - frame.size.height / 2.0);
            [window setFrameOrigin:origin];
            return;
        }
        if (position == gui_forms::FormStartPosition::center_screen ||
            position == gui_forms::FormStartPosition::center_parent) {
            [window center];
        }
    }

    // Normal, minimized (miniaturized) or maximized (zoomed). The request
    // is applied as it comes, also to a window not yet shown.
    static void apply_window_state(NSWindow* window, gui_forms::FormWindowState state) {
        if (state == gui_forms::FormWindowState::minimized) {
            if (!window.miniaturized) [window miniaturize:nil];
            return;
        }
        if (window.miniaturized) [window deminiaturize:nil];
        const bool zoom = state == gui_forms::FormWindowState::maximized;
        if (window.zoomed != zoom) [window zoom:nil];
    }

    struct CursorEntry final {
        gui_forms::CursorImagesPtr images;
        double scale;
        __strong NSCursor* native;
    };
    std::vector<CursorEntry> cursors_;
    __strong NSPasteboard* pasteboard_{};
    bool cancel_dialogs_for_testing_{};
    std::unique_ptr<gui_forms::host::detail::AppKitStatusItems> status_items_;
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    std::unique_ptr<gui_forms::host::detail::AppKitDesktopNotifications> notifications_;
#endif
    // The raster view of the window these services belong to; weak, so a
    // view that is gone applies nothing. Set once, by the view's init.
    __weak NSView* backdrop_view_{};
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    // The running drag's source, held for the drag's length (nil when no
    // drag runs).
    __strong GuiFormsDragSource* drag_source_{};
    // The next start_drag makes its source without an AppKit session
    // (macos_start_drag_for_testing).
    bool drag_without_session_{};

    // A drag's source, reporting to these services and holding itself until
    // AppKit's session ends.
    GuiFormsDragSource* make_drag_source(std::uint64_t session_id, gui_forms::DragEffect allowed,
                                         NSEventType start_type) {
        GuiFormsDragSource* const source = [[GuiFormsDragSource alloc] init];
        source.services = this;
        source.sessionId = session_id;
        source.allowedOperations = native_drag_operations(allowed);
        source.startType = start_type;
        source.holdUntilEnd = source;
        return source;
    }
#endif
    // Relative pointer mode as applied (appkit_relative_pointer.hpp), the
    // NSCursor hide it balances, and AppKit's coalescing before it.
    gui_forms::host::detail::MacRelativePointerState relative_{};
    bool relative_cursor_hidden_{};
    bool coalescing_before_{true};
    // The window's content minimum before MinimumSize was first written.
    NSSize opened_minimum_{};
    bool opened_minimum_known_{};

    friend void gui_forms::host::detail::bind_macos_backdrop_view(HostServices& services, NSView* raster);
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
    friend id<NSDraggingSource> gui_forms::host::detail::macos_start_drag_for_testing(
        HostServices& services, const gui_forms::HostDragSourceRequest& request);
#endif
    friend gui_forms::host::detail::MacRelativePointerState
    gui_forms::host::detail::macos_relative_pointer_state(const HostServices& services) noexcept;
};

} // namespace

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GuiFormsDragSource's native entries.
NSDragOperation run_drag_source_mask(GuiFormsDragSource* object) {
    return object.allowedOperations;
}

void run_drag_source_ended(GuiFormsDragSource* object, NSPoint screen_point, NSDragOperation operation) {
    [object answerDraggingSessionEndedAtPoint:screen_point operation:operation];
}

// The one portable effect a destination performed, or none.
gui_forms::DragEffect performed_drag_effect(NSDragOperation operation) noexcept {
    if (operation == NSDragOperationCopy) return gui_forms::DragEffect::copy;
    if (operation == NSDragOperationMove) return gui_forms::DragEffect::move;
    if (operation == NSDragOperationLink) return gui_forms::DragEffect::link;
    return gui_forms::DragEffect::none;
}
} // namespace

@implementation GuiFormsDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    (void)session;
    (void)context;
    return gui_forms::detail::call_failing_fast(
        "macos.drag_source.draggingSession:sourceOperationMaskForDraggingContext:", &run_drag_source_mask, self);
}

- (void)draggingSession:(NSDraggingSession*)session
           endedAtPoint:(NSPoint)screenPoint
              operation:(NSDragOperation)operation {
    (void)session;
    // Kept alive past dropping its own hold below.
    GuiFormsDragSource* const kept = self;
    gui_forms::detail::call_failing_fast(
        "macos.drag_source.draggingSession:endedAtPoint:operation:", &run_drag_source_ended, kept, screenPoint,
        operation);
    kept.holdUntilEnd = nil;
}

- (void)answerDraggingSessionEndedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation {
    AppKitHostServices* const services = dynamic_cast<AppKitHostServices*>(self.services);
    if (services == nullptr) return;
    (*services).finish_drag(self.sessionId, performed_drag_effect(operation), screenPoint, self.startType);
}
@end
#endif

namespace gui_forms::host::detail {

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
NSEvent* drag_release_event(NSWindow* window, NSEventType start_type, NSPoint screen_point) {
    if (window == nil) return nil;
    // The release of the button that started the drag; any other kind of
    // event started none (NSEventType is AppKit's open set, so a chain, not
    // a switch over every kind).
    NSEventType release = NSEventTypeLeftMouseUp;
    if (start_type == NSEventTypeLeftMouseDown || start_type == NSEventTypeLeftMouseDragged) {
        release = NSEventTypeLeftMouseUp;
    } else if (start_type == NSEventTypeRightMouseDown || start_type == NSEventTypeRightMouseDragged) {
        release = NSEventTypeRightMouseUp;
    } else if (start_type == NSEventTypeOtherMouseDown || start_type == NSEventTypeOtherMouseDragged) {
        release = NSEventTypeOtherMouseUp;
    } else {
        return nil;
    }
    const NSPoint location = [window convertPointFromScreen:screen_point];
    NSEvent* const event = [NSEvent mouseEventWithType:release
                                              location:location
                                         modifierFlags:NSEvent.modifierFlags
                                             timestamp:NSProcessInfo.processInfo.systemUptime
                                          windowNumber:window.windowNumber
                                               context:nil
                                           eventNumber:0
                                            clickCount:1
                                              pressure:0.0];
    return event;
}

void deliver_drag_release(NSView* view, NSEventType start_type, NSPoint screen_point) {
    NSEvent* const event = view == nil ? nil : drag_release_event(view.window, start_type, screen_point);
    if (event == nil) return;
    if (event.type == NSEventTypeRightMouseUp) {
        [view rightMouseUp:event];
    } else if (event.type == NSEventTypeOtherMouseUp) {
        [view otherMouseUp:event];
    } else {
        [view mouseUp:event];
    }
}
#endif

#if !defined(GUI_FORMS_HIGH_INTEGRITY)
void macos_drag_source_window_closed(HostServices& services) {
    AppKitHostServices* const appkit = dynamic_cast<AppKitHostServices*>(&services);
    if (appkit == nullptr) return;
    (*appkit).source_window_closed();
}

id<NSDraggingSource> macos_start_drag_for_testing(HostServices& services, const HostDragSourceRequest& request) {
    AppKitHostServices* const appkit = dynamic_cast<AppKitHostServices*>(&services);
    if (appkit == nullptr) return nil;
    (*appkit).drag_without_session_ = true;
    const HostDragSourceResult started = services.start_drag(request);
    (*appkit).drag_without_session_ = false;
    if (!started.status.accepted()) return nil;
    return (*appkit).drag_source_;
}
#endif

void bind_macos_backdrop_view(HostServices& services, NSView* raster) {
    AppKitHostServices* const appkit = dynamic_cast<AppKitHostServices*>(&services);
    if (appkit == nullptr) return;
    (*appkit).backdrop_view_ = raster;
}

MacRelativePointerState macos_relative_pointer_state(const HostServices& services) noexcept {
    const AppKitHostServices* const appkit = dynamic_cast<const AppKitHostServices*>(&services);
    if (appkit == nullptr) return {};
    return (*appkit).relative_;
}

} // namespace gui_forms::host::detail

namespace gui_forms::host {

std::unique_ptr<HostServices> make_macos_host_services(MacHostServiceOptions options) {
    return std::make_unique<AppKitHostServices>(options);
}

} // namespace gui_forms::host
