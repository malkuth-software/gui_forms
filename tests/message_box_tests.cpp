// show_message_box (stage W): the Windows Forms MessageBox.Show facade over
// the host's message dialog. The request it sends (caption, text, buttons,
// icon and the default button counted from the first), the chosen button as
// a DialogResult (every button set), the options, a box closed without a
// choice, a host that refuses, no host, refused arguments and the wrong
// thread; help asked while the box is open (the Help button or F1), the
// owner window's handlers, then its HelpProviders in creation order.

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// What the scripted host saw and what it answers next. An answer queue that
// runs out answers with a cancelled box.
struct MessageScript final {
    std::vector<HostDialogRequest> requests{};
    std::deque<HostMessageDialogResult> answers{};
    HostServiceError error{HostServiceError::none};
    // Help the user asks for while the next box is open: true for F1, false
    // for the Help button; and whether the host's reports were raised.
    std::vector<bool> help_presses{};
    std::vector<bool> help_published{};
};

struct ScriptedMessages final {
    MessageScript* script{};

    HostDialogResult operator()(const HostDialogRequest& request, host::HeadlessHostServices& services) const {
        (*script).requests.push_back(request);
        for (const bool keyboard : (*script).help_presses) {
            HostDialogHelpEvent help{};
            help.request_id = request.request_id;
            help.keyboard_initiated = keyboard;
            (*script).help_published.push_back(services.publish_dialog_help(help));
        }
        (*script).help_presses.clear();
        HostDialogResult result;
        result.request_id = request.request_id;
        if ((*script).error != HostServiceError::none) {
            result.status.error = (*script).error;
            result.payload = HostMessageDialogResult{};
            return result;
        }
        if ((*script).answers.empty()) {
            result.payload = HostMessageDialogResult{HostDialogOutcome::cancelled, HostDialogChoice::none};
            return result;
        }
        result.payload = (*script).answers.front();
        (*script).answers.pop_front();
        return result;
    }
};

HostMessageDialogResult chose(HostDialogChoice choice) {
    const HostMessageDialogResult result{HostDialogOutcome::accepted, choice};
    return result;
}

const HostMessageDialogRequest* message_request(const HostDialogRequest& request) {
    const HostMessageDialogRequest* const result = std::get_if<HostMessageDialogRequest>(&request.payload);
    return result;
}

void without_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("message.unhosted"));
    Window window(root, {320.0, 200.0});
    const MessageBoxResult result = show_message_box(window, {.text = "Saved."});
    require(!result.shown() && result.result() == DialogResult::none && result.error() == HostServiceError::unsupported,
            "without a host the box is not shown and the result is none");
}

struct ShowFromOtherThread final {
    Window* window{};
    bool* refused{};
    void operator()() const {
        try {
            static_cast<void>(show_message_box(*window, {.text = "Elsewhere"}));
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void shown_through_host() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("message.root"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    MessageScript script;
    services.set_dialog_handler(ScriptedMessages{&script});

    script.answers.push_back(chose(HostDialogChoice::no));
    const MessageBoxResult answered = show_message_box(window, {.text = "Discard the changes?",
                                                                .caption = "Evidence",
                                                                .buttons = HostMessageButtons::yes_no_cancel,
                                                                .icon = HostMessageIcon::question,
                                                                .default_button = MessageBoxDefaultButton::button2});
    require(answered.shown() && answered.result() == DialogResult::no && answered.error() == HostServiceError::none, "the chosen button is the result");
    require(script.requests.size() == 1U && script.requests[0].owner_id == "message.root" &&
                script.requests[0].request_id != 0U,
            "the request names the window's root and a fresh identity");
    const HostMessageDialogRequest* const sent = message_request(script.requests[0]);
    require(sent != nullptr && (*sent).title == "Evidence" && (*sent).message == "Discard the changes?" &&
                (*sent).buttons == HostMessageButtons::yes_no_cancel && (*sent).icon == HostMessageIcon::question &&
                (*sent).default_choice == HostDialogChoice::no,
            "the caption, text, buttons, icon and second button as default reach the host");

    script.answers.push_back(chose(HostDialogChoice::yes));
    require(show_message_box(window, {.text = "Overwrite?",
                                      .buttons = HostMessageButtons::yes_no,
                                      .default_button = MessageBoxDefaultButton::button3})
                    .result() == DialogResult::yes &&
                (*message_request(script.requests[1])).default_choice == HostDialogChoice::yes,
            "a default past the last button is the first button");

    script.answers.push_back(chose(HostDialogChoice::retry));
    require(show_message_box(window, {.text = "Disk busy", .buttons = HostMessageButtons::retry_cancel,
                                      .icon = HostMessageIcon::error})
                    .result() == DialogResult::retry &&
                (*message_request(script.requests[2])).default_choice == HostDialogChoice::retry,
            "retry is the result of Retry");

    script.answers.push_back(chose(HostDialogChoice::ignore));
    require(show_message_box(window, {.text = "Read error", .buttons = HostMessageButtons::abort_retry_ignore,
                                      .default_button = MessageBoxDefaultButton::button2})
                    .result() == DialogResult::ignore &&
                (*message_request(script.requests[3])).default_choice == HostDialogChoice::retry,
            "Abort, Retry, Ignore: the second button is Retry, and Ignore is the result of Ignore");
    script.answers.push_back(chose(HostDialogChoice::abort));
    require(show_message_box(window, {.text = "Read error", .buttons = HostMessageButtons::abort_retry_ignore})
                    .result() == DialogResult::abort,
            "abort is the result of Abort");
    script.answers.push_back(chose(HostDialogChoice::try_again));
    require(show_message_box(window, {.text = "Sign in failed", .buttons = HostMessageButtons::cancel_try_continue,
                                      .default_button = MessageBoxDefaultButton::button3})
                    .result() == DialogResult::try_again &&
                (*message_request(script.requests[5])).default_choice == HostDialogChoice::continue_,
            "Cancel, Try Again, Continue: the third button is Continue, and try_again is the result of Try Again");
    script.answers.push_back(chose(HostDialogChoice::continue_));
    require(show_message_box(window, {.text = "Sign in failed", .buttons = HostMessageButtons::cancel_try_continue,
                                      .default_button = MessageBoxDefaultButton::button4})
                    .result() == DialogResult::continue_ &&
                (*message_request(script.requests[6])).default_choice == HostDialogChoice::cancel,
            "continue_ is the result of Continue; a fourth button without one is the first");
    require(show_message_box(window, {.text = "Read error", .buttons = HostMessageButtons::abort_retry_ignore})
                    .result() == DialogResult::none,
            "an Abort, Retry, Ignore box the host closed without a choice returns none");
    require(show_message_box(window, {.text = "Sign in failed", .buttons = HostMessageButtons::cancel_try_continue})
                    .result() == DialogResult::cancel,
            "a Cancel, Try Again, Continue box closed without a choice returns cancel");
    static_cast<void>(show_message_box(window, {.text = "Gauche", .options = MessageBoxOptions::right_align}));
    const HostMessageDialogRequest* const right = message_request(script.requests.back());
    require((*right).right_align && !(*right).rtl_reading, "right_align reaches the host");
    static_cast<void>(show_message_box(
        window, {.text = "Droite", .options = MessageBoxOptions::right_align | MessageBoxOptions::rtl_reading}));
    const HostMessageDialogRequest* const both = message_request(script.requests.back());
    require((*both).right_align && (*both).rtl_reading, "both options reach the host");
    static_cast<void>(show_message_box(window, {.text = "Plain"}));
    const HostMessageDialogRequest* const plain = message_request(script.requests.back());
    require(!(*plain).right_align && !(*plain).rtl_reading, "no options reach the host as none");

    require(show_message_box(window, {.text = "Done."}).result() == DialogResult::ok,
            "an OK box closed without a choice returns ok");
    require(show_message_box(window, {.text = "Go on?", .buttons = HostMessageButtons::ok_cancel}).result() ==
                DialogResult::cancel,
            "a box with Cancel closed without a choice returns cancel");
    require(show_message_box(window, {.text = "Keep?", .buttons = HostMessageButtons::yes_no}).result() ==
                DialogResult::none,
            "a Yes/No box the host closed without a choice returns none");

    script.error = HostServiceError::backend_failure;
    const MessageBoxResult refused = show_message_box(window, {.text = "Busy"});
    require(!refused.shown() && refused.result() == DialogResult::none && refused.error() == HostServiceError::backend_failure,
            "a host that refuses leaves none and says why");
    script.error = HostServiceError::none;

    bool refused_none = false;
    try {
        static_cast<void>(MessageBoxResult::refused(HostServiceError::none));
    } catch (const std::invalid_argument&) {
        refused_none = true;
    }
    require(refused_none, "a refusal names an error");
    require(MessageBoxResult::closed(DialogResult::yes).shown() &&
                MessageBoxResult::closed(DialogResult::yes).error() == HostServiceError::none,
            "a closed box has no refusal");

    const std::size_t sent_before = script.requests.size();
    bool threw = false;
    try {
        static_cast<void>(show_message_box(window, {.text = std::string("bad\0text", 8U)}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "text with NUL is refused");
    threw = false;
    try {
        static_cast<void>(show_message_box(window, {.text = "x", .caption = std::string(1U, '\xff')}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a caption that is not UTF-8 is refused");
    threw = false;
    try {
        static_cast<void>(show_message_box(window, {.text = "x", .buttons = static_cast<HostMessageButtons>(9U)}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && script.requests.size() == sent_before, "unknown buttons are refused before the host is asked");
    threw = false;
    try {
        static_cast<void>(show_message_box(window, {.text = "x", .options = static_cast<MessageBoxOptions>(0x20000U)}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && script.requests.size() == sent_before, "an option GUI.Forms does not admit is refused");

    bool wrong_thread = false;
    std::thread other(ShowFromOtherThread{&window, &wrong_thread});
    other.join();
    require(wrong_thread && script.requests.size() == sent_before, "showing off the UI thread is refused");
}

// A help handler: records each request it sees and handles it or not.
struct RecordHelp final {
    std::vector<HelpRequestEvent>* seen{};
    bool* handles{};

    void operator()(HelpRequestEvent& request) const {
        (*seen).push_back(request);
        if (*handles) request.handled = true;
    }
};

// A window help handler that disposes a provider.
struct DisposeProvider final {
    HelpProvider* provider{};

    void operator()(HelpRequestEvent&) const { (*provider).dispose(); }
};

struct ThrowingHelp final {
    void operator()(HelpRequestEvent&) const { throw std::runtime_error("help handler failed"); }
};

void help_requests() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("message.help"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    MessageScript script;
    services.set_dialog_handler(ScriptedMessages{&script});

    // The Help button is shown only when asked for; it counts after the
    // set's buttons.
    script.answers.push_back(chose(HostDialogChoice::yes));
    static_cast<void>(show_message_box(window, {.text = "Save?",
                                                .buttons = HostMessageButtons::yes_no_cancel,
                                                .default_button = MessageBoxDefaultButton::button4,
                                                .display_help_button = true}));
    require((*message_request(script.requests[0])).help_button &&
                (*message_request(script.requests[0])).default_choice == HostDialogChoice::help,
            "the fourth button of a three-button box with Help is Help");
    static_cast<void>(show_message_box(window, {.text = "Go on?",
                                                .buttons = HostMessageButtons::ok_cancel,
                                                .default_button = MessageBoxDefaultButton::button3,
                                                .display_help_button = true}));
    require((*message_request(script.requests[1])).default_choice == HostDialogChoice::help,
            "the third button of a two-button box with Help is Help");
    static_cast<void>(show_message_box(window, {.text = "Plain"}));
    require(!(*message_request(script.requests[2])).help_button, "no button set shows Help unasked");

    std::vector<HelpRequestEvent> window_seen;
    bool window_handles = false;
    SubscriptionToken window_token = window.help_requested().subscribe(RecordHelp{&window_seen, &window_handles});
    HelpProvider first(window);
    HelpProvider second(window);
    HelpProvider third(window);
    first.help_namespace = "first.chm";
    second.help_namespace = "second.chm";
    second.set_help_string(root, "Root help");
    std::vector<HelpRequestEvent> first_seen;
    std::vector<HelpRequestEvent> second_seen;
    std::vector<HelpRequestEvent> third_seen;
    bool first_handles = false;
    bool second_handles = true;
    bool third_handles = true;
    SubscriptionToken first_token = first.help_requested().subscribe(RecordHelp{&first_seen, &first_handles});
    SubscriptionToken second_token = second.help_requested().subscribe(RecordHelp{&second_seen, &second_handles});
    SubscriptionToken third_token = third.help_requested().subscribe(RecordHelp{&third_seen, &third_handles});

    // The Help button: the window first, then its providers in creation
    // order until one handles it; the box stays open and returns its choice.
    script.help_presses = {false};
    script.answers.push_back(chose(HostDialogChoice::no));
    const MessageBoxResult helped = show_message_box(
        window, {.text = "Save?", .buttons = HostMessageButtons::yes_no_cancel, .display_help_button = true});
    require(helped.result() == DialogResult::no && script.help_published.size() == 1U && script.help_published[0],
            "help while the box is open leaves it open; the box returns its choice");
    require(window_seen.size() == 1U && window_seen[0].target_stable_id == "message.help" &&
                !window_seen[0].keyboard_initiated && window_seen[0].help_namespace.empty(),
            "the owner window's help_requested names its root");
    require(first_seen.size() == 1U && first_seen[0].help_namespace == "first.chm" && second_seen.size() == 1U &&
                second_seen[0].help_namespace == "second.chm" && second_seen[0].help_string == "Root help" &&
                third_seen.empty(),
            "unhandled, the providers are offered it in creation order until one handles it");

    // F1 without a Help button; a window handler that handles it stops it.
    window_handles = true;
    script.help_presses = {true};
    static_cast<void>(show_message_box(window, {.text = "Done."}));
    require(window_seen.size() == 2U && window_seen[1].keyboard_initiated && first_seen.size() == 1U &&
                second_seen.size() == 1U,
            "F1 asks for help without the button; a handled request reaches no provider");
    window_handles = false;

    // A provider a handler disposes is not offered the request.
    SubscriptionToken dispose_token = window.help_requested().subscribe(DisposeProvider{&first});
    script.help_presses = {false};
    static_cast<void>(show_message_box(window, {.text = "Again", .display_help_button = true}));
    require(first_seen.size() == 1U && second_seen.size() == 2U, "a provider disposed meanwhile is skipped");
    dispose_token.disconnect();

    require(window.request_help(true) && second_seen.size() == 3U, "request_help raises the same request");
    HostDialogHelpEvent closed{};
    closed.request_id = script.requests.back().request_id;
    require(!services.publish_dialog_help(closed), "help for a box that has closed is dropped");

    // A handler's exception is thrown once the box has closed.
    SubscriptionToken throwing = window.help_requested().subscribe(ThrowingHelp{});
    script.help_presses = {false};
    const std::size_t asked = script.requests.size();
    bool threw = false;
    try {
        static_cast<void>(show_message_box(window, {.text = "Broken", .display_help_button = true}));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw && script.requests.size() == asked + 1U, "a help handler's exception is thrown after the box closes");
}

} // namespace

int main() {
    without_host();
    shown_through_host();
    help_requests();
    std::cout << "show_message_box: requests, results, dismissal, refusals, no host and threads passed\n";
    return 0;
}
