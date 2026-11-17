#import <AppKit/AppKit.h>

#include "appkit_status_items.hpp"
#include "appkit_clipboard_image.hpp"
#include "../../../core/failure/fail_fast.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The target of one status item's button. It holds the item's portable
// identity and borrows the owner's sink; the owner clears `sink` before it
// releases the item, so a late action finds nothing to call.
@interface GFStatusItemTarget : NSObject
// The icon's HostTrayIconId value (an Objective-C property holds the plain
// integer).
@property(nonatomic) std::uint64_t iconIdentity;
@property(nonatomic) gui_forms::host::detail::AppKitStatusItems::Sink* sink;
- (void)buttonActivated:(id)sender;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerButtonActivated:(id)sender;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GFStatusItemTarget's native entries: each sends the entry's answer message.
void run_status_item_button_activated(GFStatusItemTarget* object, id sender) {
    [object answerButtonActivated:sender];
}
} // namespace

@implementation GFStatusItemTarget
- (void)buttonActivated:(id)sender {
    gui_forms::detail::call_failing_fast(
        "macos.status_item.buttonActivated:", &run_status_item_button_activated, self, sender);
}

- (void)answerButtonActivated:(id)sender {
    static_cast<void>(sender);
    gui_forms::host::detail::AppKitStatusItems::Sink* const sink = self.sink;
    if (sink == nullptr) return;
    NSEvent* const event = [NSApp currentEvent];
    gui_forms::HostTrayIconEvent published{};
    published.icon_id = gui_forms::HostTrayIconId{self.iconIdentity};
    const bool secondary = event.type == NSEventTypeRightMouseUp ||
                           (event.modifierFlags & NSEventModifierFlagControl) != 0;
    if (secondary) {
        published.action = gui_forms::HostTrayIconAction::context;
    } else if (event.clickCount == 2) {
        published.action = gui_forms::HostTrayIconAction::double_click;
    } else {
        published.action = gui_forms::HostTrayIconAction::click;
    }
    (*sink)(published);
}
@end

// Records the row a native menu's user chose.
@interface GFStatusMenuTarget : NSObject
@property(nonatomic) NSInteger chosenTag;
- (void)rowChosen:(id)sender;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerRowChosen:(id)sender;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GFStatusMenuTarget's native entries: each sends the entry's answer message.
void run_status_menu_row_chosen(GFStatusMenuTarget* object, id sender) {
    [object answerRowChosen:sender];
}
} // namespace

@implementation GFStatusMenuTarget
- (void)rowChosen:(id)sender {
    gui_forms::detail::call_failing_fast("macos.status_menu.rowChosen:", &run_status_menu_row_chosen, self, sender);
}

- (void)answerRowChosen:(id)sender {
    self.chosenTag = [(NSMenuItem*)sender tag];
}
@end

namespace gui_forms::host::detail {
namespace {

// The status bar's icon height in points (the menu bar's thickness less its
// margins).
constexpr CGFloat status_icon_points = 18.0;

NSString* native_text(const std::string& text) {
    NSString* value = [[NSString alloc] initWithBytes:text.data()
                                               length:text.size()
                                             encoding:NSUTF8StringEncoding];
    return value == nil ? @"" : value;
}

// An NSImage from the image nearest the status bar's size in pixels.
NSImage* status_image(const HostTrayIconRequest& request) {
    const CGFloat scale = NSScreen.mainScreen != nil ? NSScreen.mainScreen.backingScaleFactor : 1.0;
    const std::uint32_t wanted = static_cast<std::uint32_t>(std::lround(status_icon_points * scale));
    const HostImage& image = request.images[select_tray_icon_image(request.images, wanted)];
    // A copy of the premultiplied BGRA8 pixels, as they are.
    NSBitmapImageRep* representation = host_image_representation(image.view());
    if (representation == nil) return nil;
    // The image keeps its pixels and shows at the status bar's point size.
    const CGFloat side = status_icon_points;
    const CGFloat aspect = image.height == 0U ? 1.0 : static_cast<CGFloat>(image.width) / image.height;
    NSImage* result = [[NSImage alloc] initWithSize:NSMakeSize(side * aspect, side)];
    [result addRepresentation:representation];
    return result;
}

} // namespace

struct AppKitStatusItems::Impl final {
    struct Entry final {
        HostTrayIconId id{};
        __strong NSStatusItem* item{};
        __strong GFStatusItemTarget* target{};
    };

    Sink sink{};
    std::vector<Entry> entries{};

    Entry* find(HostTrayIconId icon_id) noexcept {
        for (Entry& entry : entries) {
            if (entry.id == icon_id) return &entry;
        }
        return nullptr;
    }

    static void release(Entry& entry) noexcept {
        entry.target.sink = nullptr;
        if (entry.item != nil) [NSStatusBar.systemStatusBar removeStatusItem:entry.item];
        entry.item = nil;
        entry.target = nil;
    }
};

AppKitStatusItems::AppKitStatusItems(Sink sink) : impl_(std::make_unique<Impl>()) {
    (*impl_).sink = sink;
}

AppKitStatusItems::~AppKitStatusItems() {
    for (Impl::Entry& entry : (*impl_).entries) Impl::release(entry);
    (*impl_).entries.clear();
}

HostServiceStatus AppKitStatusItems::add(HostTrayIconId icon_id, const HostTrayIconRequest& request) {
    NSImage* image = status_image(request);
    if (image == nil) return {HostServiceError::backend_failure};
    NSStatusItem* item = [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
    if (item == nil || item.button == nil) return {HostServiceError::backend_failure};
    GFStatusItemTarget* target = [[GFStatusItemTarget alloc] init];
    target.iconIdentity = icon_id.value;
    target.sink = &(*impl_).sink;
    item.button.image = image;
    item.button.toolTip = native_text(request.tooltip);
    item.button.target = target;
    item.button.action = @selector(buttonActivated:);
    [item.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
    Impl::Entry entry;
    entry.id = icon_id;
    entry.item = item;
    entry.target = target;
    (*impl_).entries.push_back(entry);
    return {};
}

HostServiceStatus AppKitStatusItems::update(HostTrayIconId icon_id, const HostTrayIconRequest& request) {
    Impl::Entry* const entry = (*impl_).find(icon_id);
    if (entry == nullptr) return {HostServiceError::backend_failure};
    NSImage* image = status_image(request);
    if (image == nil) return {HostServiceError::backend_failure};
    (*entry).item.button.image = image;
    (*entry).item.button.toolTip = native_text(request.tooltip);
    return {};
}

void AppKitStatusItems::remove(HostTrayIconId icon_id) noexcept {
    std::vector<Impl::Entry>& entries = (*impl_).entries;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].id != icon_id) continue;
        Impl::release(entries[index]);
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

HostTrayMenuResult AppKitStatusItems::show_menu(HostTrayIconId icon_id, const HostTrayMenuRequest& request) {
    HostTrayMenuResult result;
    Impl::Entry* const entry = (*impl_).find(icon_id);
    if (entry == nullptr || (*entry).item.button == nil) {
        result.status = {HostServiceError::backend_failure};
        return result;
    }
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    menu.autoenablesItems = NO;
    GFStatusMenuTarget* target = [[GFStatusMenuTarget alloc] init];
    target.chosenTag = 0;
    for (std::size_t index = 0; index < request.items.size(); ++index) {
        const HostTrayMenuItem& row = request.items[index];
        if (row.separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:native_text(row.text)
                                                      action:@selector(rowChosen:)
                                               keyEquivalent:@""];
        item.target = target;
        // Tags start at 1: 0 means no row was chosen.
        item.tag = static_cast<NSInteger>(index + 1U);
        item.enabled = row.enabled ? YES : NO;
        item.state = row.checked ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    NSStatusBarButton* button = (*entry).item.button;
    [menu popUpMenuPositioningItem:nil
                        atLocation:NSMakePoint(0.0, NSHeight(button.bounds) + 4.0)
                            inView:button];
    const NSInteger chosen = target.chosenTag;
    if (chosen > 0 && static_cast<std::size_t>(chosen) <= request.items.size()) {
        result.chosen_id = request.items[static_cast<std::size_t>(chosen) - 1U].id;
    }
    return result;
}

} // namespace gui_forms::host::detail
