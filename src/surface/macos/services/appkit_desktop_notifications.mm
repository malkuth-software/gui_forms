#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

#include <Block.h>

#include "appkit_desktop_notifications.hpp"
#include "../application/macos_main_work.hpp"
#include "../../../core/failure/fail_fast.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The notification center's delegate: shows a notification while the
// application is frontmost and hands each response to the main thread.
@interface GFNotificationCenterDelegate : NSObject <UNUserNotificationCenterDelegate>
// The bodies of this class's native entries (the delegate methods below):
// each entry calls its answer through the fail-fast boundary
// (call_failing_fast).
- (void)answerWillPresentNotification:(UNNotification*)notification
                    completionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler;
- (void)answerDidReceiveResponse:(UNNotificationResponse*)response
               completionHandler:(void (^)(void))completionHandler;
@end

namespace {

using gui_forms::HostDesktopNotificationAction;
using gui_forms::HostDesktopNotificationEvent;
using gui_forms::HostDesktopNotificationId;
using gui_forms::HostDesktopNotificationRequest;
using gui_forms::HostDesktopNotificationUrgency;
using gui_forms::HostServiceError;
using gui_forms::HostServiceStatus;
using gui_forms::host::detail::AppKitDesktopNotifications;

// The one category the host gives its notifications: it asks the center to
// report a dismissal.
NSString* const category_identifier = @"org.gui-forms.desktop-notification";

enum class Authorization : std::uint8_t { not_asked, asking, granted, refused };

// One notification the host showed, until it is answered or withdrawn.
struct CenterEntry final {
    __strong NSString* identifier;
    // The services' notifications object; it erases its entries when it
    // goes, so an entry's owner is alive.
    AppKitDesktopNotifications* owner;
    HostDesktopNotificationId id;
    // Waiting for the user's permission: added once it is granted. Nil once
    // added.
    __strong UNNotificationRequest* waiting;
};

// The process's notification center state, on the main thread only.
struct CenterState final {
    Authorization authorization{Authorization::not_asked};
    __strong GFNotificationCenterDelegate* delegate{};
    // Makes this run's identifiers unique in Notification Center.
    __strong NSString* prefix{};
    std::vector<CenterEntry> entries{};
};

// Made on first use and never destroyed: the center may answer until the
// process ends, and no exit-time destructor runs (tools/check_exit_statics.py).
CenterState& center_state() {
    static CenterState* const state = new CenterState();
    return *state;
}

// The delegate, the category and this run's identifier prefix, on the first
// show.
void prepare_center(CenterState& state) {
    if (state.delegate != nil) return;
    UNUserNotificationCenter* const center = [UNUserNotificationCenter currentNotificationCenter];
    state.delegate = [[GFNotificationCenterDelegate alloc] init];
    center.delegate = state.delegate;
    UNNotificationCategory* const category =
        [UNNotificationCategory categoryWithIdentifier:category_identifier
                                               actions:@[]
                                     intentIdentifiers:@[]
                                               options:UNNotificationCategoryOptionCustomDismissAction];
    [center setNotificationCategories:[NSSet setWithObject:category]];
    state.prefix = [NSString stringWithFormat:@"gui-forms-%@", [[NSUUID UUID] UUIDString]];
}

// -- the authorization's completion ---------------------------------------------
//
// requestAuthorizationWithOptions takes only a block. The house style has no
// block literals, so the completion is a global block built as the Clang
// Block ABI lays one out ("Block Implementation Specification"): the class
// of a global block (_NSConcreteGlobalBlock, declared in <Block.h>), its
// flags, a named invoke function and a descriptor. A global block captures
// nothing, is never copied to the heap (Block_copy returns it) and lives as
// long as the process; the answer's context is the process-wide center
// state.

struct AuthorizationBlockDescriptor final {
    unsigned long reserved;
    unsigned long size;
};

struct AuthorizationBlock final {
    void* isa;
    int flags;
    int reserved;
    void (*invoke)(AuthorizationBlock*, BOOL, NSError* __unsafe_unretained);
    const AuthorizationBlockDescriptor* descriptor;
};

// BLOCK_IS_GLOBAL in the Block ABI's flags.
constexpr int block_is_global = 1 << 28;

using AuthorizationCompletion = void (^)(BOOL, NSError*);

struct AuthorizationWork final {
    bool granted{};
};

// Adds the waiting requests once permission is granted; reports each as
// failed when it is refused. One entry at a time, found afresh: a handler
// may withdraw or show others, or destroy an owner (which erases its
// entries).
void apply_authorization(void* context) {
    const std::unique_ptr<AuthorizationWork> work(static_cast<AuthorizationWork*>(context));
    CenterState& state = center_state();
    state.authorization = (*work).granted ? Authorization::granted : Authorization::refused;
    UNUserNotificationCenter* const center = [UNUserNotificationCenter currentNotificationCenter];
    for (;;) {
        std::size_t found = state.entries.size();
        for (std::size_t index = 0U; index < state.entries.size(); ++index) {
            if (state.entries[index].waiting != nil) {
                found = index;
                break;
            }
        }
        if (found == state.entries.size()) return;
        CenterEntry& entry = state.entries[found];
        if ((*work).granted) {
            [center addNotificationRequest:entry.waiting withCompletionHandler:nil];
            entry.waiting = nil;
            continue;
        }
        AppKitDesktopNotifications* const owner = entry.owner;
        const HostDesktopNotificationEvent event{entry.id, HostDesktopNotificationAction::failed};
        state.entries.erase(state.entries.begin() + static_cast<std::ptrdiff_t>(found));
        (*owner).report(event);
    }
}

void answer_authorization(BOOL granted) {
    gui_forms::host::macos::post_main_work(new AuthorizationWork{granted != NO}, &apply_authorization);
}

// The block's invoke function: called by the notification center on a
// queue of its own.
void authorization_answered(AuthorizationBlock*, BOOL granted, NSError* __unsafe_unretained) {
    gui_forms::detail::call_failing_fast("macos.notifications.authorization_answered", &answer_authorization,
                                         granted);
}

const AuthorizationBlockDescriptor authorization_descriptor{0UL, sizeof(AuthorizationBlock)};
AuthorizationBlock authorization_block{static_cast<void*>(_NSConcreteGlobalBlock), block_is_global, 0,
                                       &authorization_answered, &authorization_descriptor};

void ask_authorization(CenterState& state) {
    if (state.authorization != Authorization::not_asked) return;
    state.authorization = Authorization::asking;
    const AuthorizationCompletion completion =
        (__bridge AuthorizationCompletion)static_cast<void*>(&authorization_block);
    [[UNUserNotificationCenter currentNotificationCenter]
        requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                      completionHandler:completion];
}

// -- responses ----------------------------------------------------------------------

// A response, carried to the main thread.
struct ResponseWork final {
    __strong NSString* identifier;
    __strong NSString* action;
};

void apply_response(NSString* identifier, NSString* action) {
    HostDesktopNotificationAction reported = HostDesktopNotificationAction::activated;
    if ([action isEqualToString:UNNotificationDefaultActionIdentifier]) {
        reported = HostDesktopNotificationAction::activated;
    } else if ([action isEqualToString:UNNotificationDismissActionIdentifier]) {
        reported = HostDesktopNotificationAction::dismissed;
    } else {
        // The host's category has no actions of its own.
        return;
    }
    CenterState& state = center_state();
    for (std::size_t index = 0U; index < state.entries.size(); ++index) {
        if (![state.entries[index].identifier isEqualToString:identifier]) continue;
        AppKitDesktopNotifications* const owner = state.entries[index].owner;
        const HostDesktopNotificationEvent event{state.entries[index].id, reported};
        state.entries.erase(state.entries.begin() + static_cast<std::ptrdiff_t>(index));
        (*owner).report(event);
        return;
    }
}

void apply_response_work(void* context) {
    const std::unique_ptr<ResponseWork> work(static_cast<ResponseWork*>(context));
    apply_response((*work).identifier, (*work).action);
}

// The named targets the fail-fast boundary (call_failing_fast) calls for
// GFNotificationCenterDelegate's native entries.
void run_will_present(GFNotificationCenterDelegate* object, UNNotification* notification,
                      void (^completion)(UNNotificationPresentationOptions)) {
    [object answerWillPresentNotification:notification completionHandler:completion];
}

void run_did_receive(GFNotificationCenterDelegate* object, UNNotificationResponse* response,
                     void (^completion)(void)) {
    [object answerDidReceiveResponse:response completionHandler:completion];
}

NSString* text_of(const std::string& text) {
    NSString* const converted = [[NSString alloc] initWithBytes:text.data()
                                                         length:text.size()
                                                       encoding:NSUTF8StringEncoding];
    return converted != nil ? converted : @"";
}

UNNotificationInterruptionLevel interruption_level(HostDesktopNotificationUrgency urgency) noexcept {
    switch (urgency) {
    case HostDesktopNotificationUrgency::low:
        return UNNotificationInterruptionLevelPassive;
    case HostDesktopNotificationUrgency::normal:
        return UNNotificationInterruptionLevelActive;
    case HostDesktopNotificationUrgency::critical:
        return UNNotificationInterruptionLevelTimeSensitive;
    }
    // HostServices refuses an urgency outside the enumeration first.
    return UNNotificationInterruptionLevelActive;
}

} // namespace

@implementation GFNotificationCenterDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    static_cast<void>(center);
    gui_forms::detail::call_failing_fast("macos.notifications.willPresentNotification", &run_will_present, self,
                                         notification, completionHandler);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler {
    static_cast<void>(center);
    gui_forms::detail::call_failing_fast("macos.notifications.didReceiveNotificationResponse", &run_did_receive,
                                         self, response, completionHandler);
}

// A notification arriving while the application is frontmost shows as it
// would behind it.
- (void)answerWillPresentNotification:(UNNotification*)notification
                    completionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    static_cast<void>(notification);
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList |
                      UNNotificationPresentationOptionSound);
}

- (void)answerDidReceiveResponse:(UNNotificationResponse*)response
               completionHandler:(void (^)(void))completionHandler {
    NSString* const identifier = response.notification.request.identifier;
    NSString* const action = response.actionIdentifier;
    if ([NSThread isMainThread]) {
        apply_response(identifier, action);
    } else {
        gui_forms::host::macos::post_main_work(new ResponseWork{identifier, action}, &apply_response_work);
    }
    completionHandler();
}
@end

namespace gui_forms::host::detail {

bool AppKitDesktopNotifications::available() noexcept {
    NSBundle* const bundle = [NSBundle mainBundle];
    const bool available = bundle.bundleIdentifier != nil && [bundle.bundlePath hasSuffix:@".app"];
    return available;
}

AppKitDesktopNotifications::AppKitDesktopNotifications(Sink sink) : sink_(sink) {}

AppKitDesktopNotifications::~AppKitDesktopNotifications() {
    std::vector<CenterEntry>& entries = center_state().entries;
    std::size_t index = 0U;
    while (index < entries.size()) {
        if (entries[index].owner == this) {
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }
}

void AppKitDesktopNotifications::report(const HostDesktopNotificationEvent& event) {
    sink_(event);
}

HostServiceStatus AppKitDesktopNotifications::show(HostDesktopNotificationId notification_id,
                                                   const HostDesktopNotificationRequest& request) {
    CenterState& state = center_state();
    // The user turned the application's notifications off: nothing would
    // show.
    if (state.authorization == Authorization::refused) return {HostServiceError::backend_failure};
    prepare_center(state);
    UNMutableNotificationContent* const content = [[UNMutableNotificationContent alloc] init];
    content.title = text_of(request.title);
    content.body = text_of(request.text);
    content.sound = request.silent ? nil : [UNNotificationSound defaultSound];
    content.categoryIdentifier = category_identifier;
    content.interruptionLevel = interruption_level(request.urgency);
    NSString* const identifier =
        [NSString stringWithFormat:@"%@-%llu", state.prefix, static_cast<unsigned long long>(notification_id.value)];
    UNNotificationRequest* const native = [UNNotificationRequest requestWithIdentifier:identifier
                                                                               content:content
                                                                               trigger:nil];
    CenterEntry entry{identifier, this, notification_id, nil};
    if (state.authorization == Authorization::granted) {
        [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:native
                                                               withCompletionHandler:nil];
    } else {
        entry.waiting = native;
    }
    state.entries.push_back(entry);
    ask_authorization(state);
    return {};
}

void AppKitDesktopNotifications::withdraw(HostDesktopNotificationId notification_id) noexcept {
    std::vector<CenterEntry>& entries = center_state().entries;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        if (entries[index].owner != this || entries[index].id != notification_id) continue;
        NSString* const identifier = entries[index].identifier;
        const bool added = entries[index].waiting == nil;
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
        if (added) {
            UNUserNotificationCenter* const center = [UNUserNotificationCenter currentNotificationCenter];
            [center removePendingNotificationRequestsWithIdentifiers:@[ identifier ]];
            [center removeDeliveredNotificationsWithIdentifiers:@[ identifier ]];
        }
        return;
    }
}

} // namespace gui_forms::host::detail
