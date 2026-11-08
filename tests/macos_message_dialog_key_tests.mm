#import <AppKit/AppKit.h>

#include "macos_host.hpp"

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <variant>

// Keys reaching the host's message dialog. While the dialog runs, a
// synthetic key-down is posted to the application's event queue, and the
// result is recorded for each button set, declared default and key: Escape
// must end on the cancel choice, Return on the declared default choice, and
// an ordinary key must leave the dialog open. The rows where Cancel is also
// the default are the ones AppKit alone gets wrong (Escape is ignored there),
// which is why the host gives such a dialog a zero-sized button carrying
// Escape. Each alert must also be visible and key at the modal panel level
// of a [NSAlert runModal] alert, and ordered out afterwards. The keys reach
// only an active application, so ctest launches this bundle through
// cmake/run_macos_activated_test.cmake.

namespace {

using gui_forms::HostDialogChoice;
using gui_forms::HostMessageButtons;

enum class ProbeKey : std::uint8_t {
    escape,
    return_key,
    ordinary,
};

struct ProbeCase final {
    HostMessageButtons buttons;
    HostDialogChoice default_choice;
    ProbeKey key;
    HostDialogChoice expected;
};

constexpr std::array<ProbeCase, 16> probe_cases{{
    {HostMessageButtons::ok, HostDialogChoice::ok, ProbeKey::return_key, HostDialogChoice::ok},
    {HostMessageButtons::ok_cancel, HostDialogChoice::cancel, ProbeKey::escape, HostDialogChoice::cancel},
    {HostMessageButtons::ok_cancel, HostDialogChoice::cancel, ProbeKey::return_key, HostDialogChoice::cancel},
    {HostMessageButtons::ok_cancel, HostDialogChoice::cancel, ProbeKey::ordinary, HostDialogChoice::cancel},
    {HostMessageButtons::ok_cancel, HostDialogChoice::ok, ProbeKey::escape, HostDialogChoice::cancel},
    {HostMessageButtons::ok_cancel, HostDialogChoice::ok, ProbeKey::return_key, HostDialogChoice::ok},
    {HostMessageButtons::ok_cancel, HostDialogChoice::ok, ProbeKey::ordinary, HostDialogChoice::cancel},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::cancel, ProbeKey::escape, HostDialogChoice::cancel},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::cancel, ProbeKey::return_key, HostDialogChoice::cancel},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::cancel, ProbeKey::ordinary, HostDialogChoice::cancel},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::yes, ProbeKey::escape, HostDialogChoice::cancel},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::yes, ProbeKey::return_key, HostDialogChoice::yes},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::yes, ProbeKey::ordinary, HostDialogChoice::cancel},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::no, ProbeKey::escape, HostDialogChoice::cancel},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::no, ProbeKey::return_key, HostDialogChoice::no},
    {HostMessageButtons::yes_no_cancel, HostDialogChoice::no, ProbeKey::ordinary, HostDialogChoice::cancel},
}};

// One case's scheduled steps on the main queue. The test owns the record and
// keeps it alive until `chain_finished`, after the dialog has returned, so
// every step may write to it.
struct KeyProbe final {
    ProbeKey key{ProbeKey::escape};
    bool dialog_finished{};
    bool chain_finished{};
    bool modal_window_found{};
    bool application_active{};
    bool alert_key{};
    bool alert_visible{};
    NSWindowLevel alert_level{};
    NSRect alert_frame{};
    __strong NSWindow* alert_window{};
    bool open_after_ordinary_key{};
    bool closed_by_watchdog{};
    std::uint32_t polls_left{};
    // Polls spent waiting for the alert to be key and visible.
    std::uint32_t show_polls_used{};
};

constexpr std::int64_t first_key_delay_nanoseconds = 250 * NSEC_PER_MSEC;
constexpr std::int64_t ordinary_key_check_nanoseconds = 300 * NSEC_PER_MSEC;
constexpr std::int64_t watch_interval_nanoseconds = 20 * NSEC_PER_MSEC;
// About one second of polls before the watchdog ends a dialog that ignored
// its key.
constexpr std::uint32_t watch_polls = 50U;
// The window server reports a new window's occlusion state asynchronously,
// after the window has become key; on a loaded machine the first alert of a
// fresh process can still read as occluded when first_key_delay has passed.
// About three seconds of polls for the alert to be key and visible before its
// facts are recorded anyway (and fail).
constexpr std::uint32_t show_polls = 150U;

void post_after(std::int64_t delay_nanoseconds, KeyProbe* probe, dispatch_function_t function) {
    const dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, delay_nanoseconds);
    dispatch_after_f(when, dispatch_get_main_queue(), probe, function);
}

NSEvent* key_event(NSString* characters, unsigned short key_code, NSInteger window_number);

NSEvent* key_down_event(ProbeKey key, NSInteger window_number) {
    switch (key) {
    case ProbeKey::escape: return key_event(@"\e", 53U, window_number);
    case ProbeKey::return_key: return key_event(@"\r", 36U, window_number);
    case ProbeKey::ordinary: return key_event(@"a", 0U, window_number);
    }
    // Fallback for a value outside ProbeKey: Escape, which ends any dialog.
    return key_event(@"\e", 53U, window_number);
}

NSEvent* key_event(NSString* characters, unsigned short key_code, NSInteger window_number) {
    const NSTimeInterval timestamp = [NSProcessInfo processInfo].systemUptime;
    NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                      location:NSZeroPoint
                                 modifierFlags:0
                                     timestamp:timestamp
                                  windowNumber:window_number
                                       context:nil
                                    characters:characters
                    charactersIgnoringModifiers:characters
                                     isARepeat:NO
                                       keyCode:key_code];
    return event;
}

void post_key(ProbeKey key, NSWindow* window) {
    NSEvent* event = key_down_event(key, window.windowNumber);
    [NSApp postEvent:event atStart:NO];
}

// Polls until the dialog returns; after `polls_left` polls it ends the
// dialog the way the host's test cancellation does.
void watch_dialog(void* context) {
    KeyProbe& probe = *static_cast<KeyProbe*>(context);
    if (probe.dialog_finished) {
        probe.chain_finished = true;
        return;
    }
    if (probe.polls_left == 0U) {
        NSWindow* window = [NSApp modalWindow];
        [window orderOut:nil];
        [NSApp abortModal];
        probe.closed_by_watchdog = true;
        probe.chain_finished = true;
        return;
    }
    --probe.polls_left;
    post_after(watch_interval_nanoseconds, &probe, &watch_dialog);
}

// After an ordinary key: record whether the dialog is still open, then
// close it with Escape.
void check_open_then_escape(void* context) {
    KeyProbe& probe = *static_cast<KeyProbe*>(context);
    NSWindow* window = [NSApp modalWindow];
    probe.open_after_ordinary_key = !probe.dialog_finished && window != nil;
    if (window != nil) {
        post_key(ProbeKey::escape, window);
    }
    probe.polls_left = watch_polls;
    post_after(watch_interval_nanoseconds, &probe, &watch_dialog);
}

void post_probe_key(void* context) {
    KeyProbe& probe = *static_cast<KeyProbe*>(context);
    NSWindow* window = [NSApp modalWindow];
    probe.modal_window_found = window != nil;
    probe.application_active = NSApp.isActive;
    probe.alert_key = window != nil && window.isKeyWindow;
    if (window != nil) {
        const NSWindowOcclusionState occlusion = window.occlusionState;
        probe.alert_visible = (occlusion & NSWindowOcclusionStateVisible) != 0;
        probe.alert_level = window.level;
        probe.alert_frame = window.frame;
        probe.alert_window = window;
        post_key(probe.key, window);
    }
    if (probe.key == ProbeKey::ordinary) {
        post_after(ordinary_key_check_nanoseconds, &probe, &check_open_then_escape);
        return;
    }
    probe.polls_left = watch_polls;
    post_after(watch_interval_nanoseconds, &probe, &watch_dialog);
}

bool alert_shown(NSWindow* window) {
    if (window == nil || !window.isKeyWindow) {
        return false;
    }
    const NSWindowOcclusionState occlusion = window.occlusionState;
    return (occlusion & NSWindowOcclusionStateVisible) != 0;
}

// Waits until the modal alert is key and visible, then records its facts and
// posts the key; after `polls_left` polls it records whatever it sees.
void wait_for_alert(void* context) {
    KeyProbe& probe = *static_cast<KeyProbe*>(context);
    if (!alert_shown([NSApp modalWindow]) && probe.polls_left > 0U) {
        --probe.polls_left;
        ++probe.show_polls_used;
        post_after(watch_interval_nanoseconds, &probe, &wait_for_alert);
        return;
    }
    post_probe_key(context);
}

// Runs the main run loop until the probe's last step has run, so no step
// outlives its record. There is deliberately no turn limit: the chain always
// ends (its polls are bounded and the dialog has returned), and leaving early
// would let a pending step write to a destroyed record; ctest's timeout
// bounds the run.
void drain_probe(const KeyProbe& probe) {
    while (!probe.chain_finished) {
        NSDate* until = [NSDate dateWithTimeIntervalSinceNow:0.01];
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:until];
    }
}

const char* buttons_name(HostMessageButtons buttons) {
    switch (buttons) {
    case HostMessageButtons::ok: return "ok";
    case HostMessageButtons::ok_cancel: return "ok_cancel";
    case HostMessageButtons::yes_no: return "yes_no";
    case HostMessageButtons::yes_no_cancel: return "yes_no_cancel";
    case HostMessageButtons::retry_cancel: return "retry_cancel";
    // The probe names only the buttons and choices it exercises.
    default: return "?";
    }
}

const char* choice_name(HostDialogChoice choice) {
    switch (choice) {
    case HostDialogChoice::none: return "none";
    case HostDialogChoice::ok: return "ok";
    case HostDialogChoice::cancel: return "cancel";
    case HostDialogChoice::yes: return "yes";
    case HostDialogChoice::no: return "no";
    case HostDialogChoice::retry: return "retry";
    // The probe names only the buttons and choices it exercises.
    default: return "?";
    }
}

const char* key_name(ProbeKey key) {
    switch (key) {
    case ProbeKey::escape: return "escape";
    case ProbeKey::return_key: return "return";
    case ProbeKey::ordinary: return "a";
    }
    return "?";
}

// The first case has no cancel choice, so the host adds nothing to its
// [NSAlert runModal] alert; its window level is the reference for the rest.
struct AlertWindowFacts final {
    bool recorded{};
    NSWindowLevel level{};
};

bool run_probe_case(gui_forms::HostServices& services, const ProbeCase& probe_case,
                    std::uint64_t request_id, AlertWindowFacts& reference) {
    gui_forms::HostMessageDialogRequest request;
    request.title = "GUI.Forms key probe";
    request.message = "Synthetic key delivery";
    request.buttons = probe_case.buttons;
    request.default_choice = probe_case.default_choice;
    KeyProbe probe;
    probe.key = probe_case.key;
    probe.polls_left = show_polls;
    post_after(first_key_delay_nanoseconds, &probe, &wait_for_alert);
    const gui_forms::HostDialogResult result =
        services.show_dialog({request_id, "test.window", std::move(request)});
    probe.dialog_finished = true;
    drain_probe(probe);
    HostDialogChoice choice = HostDialogChoice::none;
    if (std::holds_alternative<gui_forms::HostMessageDialogResult>(result.payload)) {
        choice = std::get<gui_forms::HostMessageDialogResult>(result.payload).choice;
    }
    const bool ordered_out = probe.alert_window != nil && !probe.alert_window.isVisible;
    probe.alert_window = nil;
    if (!reference.recorded) {
        reference.recorded = true;
        reference.level = probe.alert_level;
    }
    bool passed = result.status.accepted() && probe.chain_finished &&
        probe.modal_window_found && !probe.closed_by_watchdog &&
        choice == probe_case.expected && probe.application_active &&
        probe.alert_key && probe.alert_visible && ordered_out &&
        probe.alert_level == reference.level;
    if (probe_case.key == ProbeKey::ordinary && !probe.open_after_ordinary_key) {
        passed = false;
    }
    std::cout << "probe buttons=" << buttons_name(probe_case.buttons)
              << " default=" << choice_name(probe_case.default_choice)
              << " key=" << key_name(probe_case.key)
              << " result=" << choice_name(choice)
              << " expected=" << choice_name(probe_case.expected)
              << " open_after_key=" << probe.open_after_ordinary_key
              << " watchdog=" << probe.closed_by_watchdog
              << " active=" << probe.application_active
              << " alert_key=" << probe.alert_key
              << " visible=" << probe.alert_visible
              << " show_polls=" << probe.show_polls_used
              << " level=" << probe.alert_level
              << " frame=" << probe.alert_frame.origin.x << ',' << probe.alert_frame.origin.y
              << ',' << probe.alert_frame.size.width << ',' << probe.alert_frame.size.height
              << " ordered_out=" << ordered_out
              << (passed ? " PASS" : " FAIL") << '\n';
    return passed;
}

} // namespace

int main() {
    int failures = 0;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        [NSApp activate];
        gui_forms::host::MacHostServiceOptions options;
        std::unique_ptr<gui_forms::HostServices> services =
            gui_forms::host::make_macos_host_services(options);
        if (!services) {
            return 2;
        }
        std::uint64_t request_id = 1U;
        std::uint64_t expected_cancellations = 0U;
        AlertWindowFacts reference;
        for (const ProbeCase& probe_case : probe_cases) {
            if (!run_probe_case(*services, probe_case, request_id, reference)) {
                ++failures;
            }
            if (probe_case.expected == HostDialogChoice::cancel) {
                ++expected_cancellations;
            }
            ++request_id;
        }
        const gui_forms::HostServicesSnapshot snapshot = (*services).snapshot();
        std::cout << "snapshot=" << snapshot.to_json() << '\n';
        if (snapshot.dialog_requests != probe_cases.size() ||
            snapshot.dialog_completions != probe_cases.size() ||
            snapshot.dialog_cancellations != expected_cancellations ||
            snapshot.maximum_modal_depth != 1U) {
            ++failures;
        }
        (*services).shutdown();
    }
    std::cout << "failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
