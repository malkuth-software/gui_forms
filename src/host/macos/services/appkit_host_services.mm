#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "macos_host.hpp"
#include "appkit_clipboard_image.hpp"
#include "appkit_status_items.hpp"
#include "../compositor/macos_compositor.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../../../core/failure/fail_fast.hpp"
#include "../../../render/skia/raster/skia_raster.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
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
std::optional<std::size_t> chosen_file_filter(NSPopUpButton* popup, std::size_t filter_count) {
    if (popup == nil) {
        if (filter_count == 1U) return std::size_t{0};
        return std::nullopt;
    }
    const NSInteger index = popup.indexOfSelectedItem;
    if (index < 0 || static_cast<std::size_t>(index) >= filter_count) return std::nullopt;
    const std::size_t chosen = static_cast<std::size_t>(index);
    return chosen;
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
                const NSModalResponse response = [alert runModal];
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
                NSSavePanel* panel = [NSSavePanel savePanel];
                panel.title = native_string(payload.title);
                panel.directoryURL = native_directory_url(payload.initial_directory);
                panel.nameFieldStringValue = native_string(payload.suggested_name);
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
                NSArray<NSURL*>* urls = panel.URL == nil ? @[] : @[panel.URL];
                HostPathDialogResult value = native_path_result(response, urls);
                if (value.outcome == HostDialogOutcome::accepted) {
                    value.chosen_filter = chosen_file_filter(popup, payload.filters.size());
                }
                return {{}, request.request_id, std::move(value)};
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
                const NSModalResponse response = [alert runModal];
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

    void shutdown_impl() noexcept override {
        status_items_.reset();
        for (const CursorEntry& entry : cursors_) {
            if ([NSCursor currentCursor] == entry.native) [[NSCursor arrowCursor] set];
        }
        cursors_.clear();
        pasteboard_ = nil;
    }

private:
    void forward_tray_event(const gui_forms::HostTrayIconEvent& event) { publish_tray_icon_event(event); }

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
    // The raster view of the window these services belong to; weak, so a
    // view that is gone applies nothing. Set once, by the view's init.
    __weak NSView* backdrop_view_{};
    // The window's content minimum before MinimumSize was first written.
    NSSize opened_minimum_{};
    bool opened_minimum_known_{};

    friend void gui_forms::host::detail::bind_macos_backdrop_view(HostServices& services, NSView* raster);
};

} // namespace

namespace gui_forms::host::detail {

void bind_macos_backdrop_view(HostServices& services, NSView* raster) {
    AppKitHostServices* const appkit = dynamic_cast<AppKitHostServices*>(&services);
    if (appkit == nullptr) return;
    (*appkit).backdrop_view_ = raster;
}

} // namespace gui_forms::host::detail

namespace gui_forms::host {

std::unique_ptr<HostServices> make_macos_host_services(MacHostServiceOptions options) {
    return std::make_unique<AppKitHostServices>(options);
}

} // namespace gui_forms::host
