#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/host/session/host_session.hpp"
#include "../src/core/application/application_state.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace gui_forms;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
ApplicationWindow make_window(std::string id, std::string owner = {}) {
    ApplicationWindow entry{};
    entry.stable_id = std::move(id);
    entry.owner_id = std::move(owner);
    const std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
    entry.model = std::make_unique<Window>(root, Size{320.0, 240.0});
    return entry;
}
struct ValidateOnWorker final {
    const std::vector<ApplicationWindow>* windows{};
    ApplicationResult* result{};
    void operator()() const { *result = Application::validate(*windows); }
};
struct RequestOnWorker final {
    ApplicationWindowHandle handle{};
    HostServiceStatus* result{};
    void operator()() const { *result = handle.request_close(); }
};
struct CountRequest final {
    unsigned* count{};
    void operator()() const { ++*count; }
};
struct TitleRequest final {
    std::string* observed{};
    unsigned* calls{};
    HostServiceStatus operator()(const std::string_view value) const {
        (*observed).assign(value);
        ++*calls;
        return {};
    }
};
struct TitleOnWorker final {
    ApplicationWindowHandle handle{};
    HostServiceStatus* result{};
    void operator()() const { *result = handle.set_title("worker"); }
};
struct RefuseTitle final {
    HostServiceStatus operator()(std::string_view) const { return {HostServiceError::unsupported}; }
};
struct ThrowTitle final {
    HostServiceStatus operator()(std::string_view) const { throw std::runtime_error("title adapter failure"); }
};
void titles() {
    const std::shared_ptr<detail::ApplicationWindowState> state = std::make_shared<detail::ApplicationWindowState>();
    const ApplicationWindowHandle handle = detail::ApplicationHandleAccess::make(state);
    require(handle.set_title("early").error == HostServiceError::backend_failure, "unready title accepted");
    (*state).ready = true;
    require(handle.set_title("missing").error == HostServiceError::unsupported, "missing title adapter accepted");
    std::string observed{};
    unsigned calls = 0;
    (*state).set_title = TitleRequest{&observed, &calls};
    const std::string title = "notes \xe6\x97\xa5 \xf0\x9f\x98\x80 - Editor";
    require(handle.set_title(title).accepted() && observed == title, "Unicode title changed");
    require(handle.set_title(std::string_view("bad\0title", 9)).error == HostServiceError::invalid_argument,
        "embedded title NUL accepted");
    require(handle.set_title("\xc0\xaf").error == HostServiceError::invalid_argument, "malformed title accepted");
    require(handle.set_title(std::string(65537, 'x')).error == HostServiceError::invalid_argument, "oversized title accepted");
    require(calls == 1 && observed == title, "invalid title reached native adapter");
    HostServiceStatus worker{};
    std::thread thread(TitleOnWorker{handle, &worker});
    thread.join();
    require(worker.error == HostServiceError::wrong_thread && calls == 1, "foreign-thread title reached adapter");
    require(handle.set_title(std::string(65536, 'x')).accepted() && observed.size() == 65536U,
        "maximum length title refused");
    require(handle.set_title("").accepted() && observed.empty(), "empty title refused");
    (*state).set_title = RefuseTitle{};
    require(handle.set_title("refused").error == HostServiceError::unsupported, "native refusal lost");
    (*state).set_title = ThrowTitle{};
    require(handle.set_title("throw").error == HostServiceError::backend_failure, "native exception escaped");
    (*state).closed = true;
    require(handle.set_title("closed").error == HostServiceError::after_shutdown && calls == 3, "closed title reached adapter");
    require(ApplicationWindowHandle{}.set_title("expired").error == HostServiceError::after_shutdown, "empty title handle callable");
}
struct SizeRequest final {
    Size* observed{};
    unsigned* calls{};
    HostServiceStatus operator()(const Size value) const {
        *observed = value;
        ++*calls;
        return {};
    }
};
struct SizeOnWorker final {
    ApplicationWindowHandle handle{};
    HostServiceStatus* result{};
    void operator()() const { *result = handle.request_client_size({400.0, 300.0}); }
};
struct ThrowSize final {
    HostServiceStatus operator()(Size) const { throw std::runtime_error("size adapter failure"); }
};
void client_sizes() {
    const std::shared_ptr<detail::ApplicationWindowState> state = std::make_shared<detail::ApplicationWindowState>();
    const ApplicationWindowHandle handle = detail::ApplicationHandleAccess::make(state);
    require(handle.request_client_size({400.0, 300.0}).error == HostServiceError::backend_failure,
        "unready size request accepted");
    (*state).ready = true;
    require(handle.request_client_size({400.0, 300.0}).error == HostServiceError::unsupported,
        "missing size adapter accepted");
    Size observed{};
    unsigned calls = 0;
    (*state).request_client_size = SizeRequest{&observed, &calls};
    require(handle.request_client_size({400.0, 300.0}).accepted() && observed == Size{400.0, 300.0},
        "size request changed in transit");
    const double infinity = std::numeric_limits<double>::infinity();
    require(handle.request_client_size({0.5, 300.0}).error == HostServiceError::invalid_argument,
        "sub-unit width accepted");
    require(handle.request_client_size({400.0, 32769.0}).error == HostServiceError::invalid_argument,
        "oversized height accepted");
    require(handle.request_client_size({infinity, 300.0}).error == HostServiceError::invalid_argument,
        "infinite width accepted");
    require(calls == 1, "invalid size reached native adapter");
    HostServiceStatus worker{};
    std::thread thread(SizeOnWorker{handle, &worker});
    thread.join();
    require(worker.error == HostServiceError::wrong_thread && calls == 1, "foreign-thread size reached adapter");
    (*state).request_client_size = ThrowSize{};
    require(handle.request_client_size({400.0, 300.0}).error == HostServiceError::backend_failure,
        "native size exception escaped");
    (*state).closed = true;
    require(handle.request_client_size({400.0, 300.0}).error == HostServiceError::after_shutdown,
        "closed size request reached adapter");
    require(ApplicationWindowHandle{}.request_client_size({400.0, 300.0}).error == HostServiceError::after_shutdown,
        "empty size handle callable");
}
struct ChangeHidePolicy final {
    void operator()(HostCloseRequest& request) const { request.hide_on_accept = false; }
};
struct CallbackLifetime final {
    bool& destroyed;
    explicit CallbackLifetime(bool& observed) : destroyed(observed) {}
    ~CallbackLifetime() { destroyed = true; }
    CallbackLifetime(const CallbackLifetime&) = delete;
    CallbackLifetime& operator=(const CallbackLifetime&) = delete;
};
struct CloseAndContinue final {
    std::weak_ptr<detail::ApplicationWindowState> state{};
    std::shared_ptr<CallbackLifetime> lifetime{};
    bool* destroyed{};
    bool* completed{};

    void operator()() const {
        // Cache observations before revoking the source callback. This fixture
        // detects premature destruction without accessing an expired functor.
        bool& was_destroyed = *destroyed;
        bool& did_complete = *completed;
        const std::shared_ptr<detail::ApplicationWindowState> active = state.lock();
        require(static_cast<bool>(active), "callback state expired");
        (*active).closed = true;
        (*active).request_close = {};
        require(!was_destroyed, "active callable destroyed during its own close");
        did_complete = true;
    }
};
void synchronous_close_lifetime() {
    bool destroyed = false;
    bool completed = false;
    const std::shared_ptr<detail::ApplicationWindowState> state =
        std::make_shared<detail::ApplicationWindowState>();
    (*state).ready = true;
    std::shared_ptr<CallbackLifetime> lifetime = std::make_shared<CallbackLifetime>(destroyed);
    (*state).request_close = CloseAndContinue{state, lifetime, &destroyed, &completed};
    lifetime.reset();
    const ApplicationWindowHandle handle = detail::ApplicationHandleAccess::make(state);
    const HostServiceStatus result = handle.request_close();
    require(result.accepted() && completed && destroyed && !handle.active(),
            "synchronous close did not retain and then release the active callable");
}
// The minimum client size (Windows Forms' MinimumSize): none by default,
// so a small window is accepted; a client size below a set minimum is
// refused, naming the reason and both sizes; a negative minimum is
// refused as such. Restores windows[0]'s sizes.
void minimum_sizes(std::vector<ApplicationWindow>& windows) {
    require(ApplicationWindowOptions{}.minimum_size == Size{}, "the default minimum is none");
    windows[0].options.initial_size = {120.0, 100.0};
    require(Application::validate(windows).accepted(), "a window under 150 x 150 without a minimum is accepted");
    windows[0].options.minimum_size = {150.0, 150.0};
    const ApplicationResult below = Application::validate(windows);
    require(below.error == ApplicationError::invalid_argument && below.window_index == 0U &&
                below.refusal == ApplicationWindowRefusal::below_minimum_size &&
                below.minimum_size == Size{150.0, 150.0} && below.initial_size == Size{120.0, 100.0},
            "a client size below the minimum names the reason and both sizes");
    require(!application_window_refusal_text(below.refusal).empty() &&
                application_window_refusal_text(ApplicationWindowRefusal::none).empty(),
            "a refusal has a sentence, none has none");
    windows[0].options.minimum_size = {100.0, 0.0};
    require(Application::validate(windows).accepted(), "a minimum on one side, under the client size, is accepted");
    windows[0].options.minimum_size = {-1.0, 0.0};
    require(Application::validate(windows).refusal == ApplicationWindowRefusal::invalid_minimum_size,
            "a negative minimum is named");
    windows[0].options.minimum_size = {};
    windows[0].options.initial_size = ApplicationWindowOptions{}.initial_size;
}
void validation() {
    std::vector<ApplicationWindow> windows{};
    require(!Application::validate(windows).accepted(), "empty application accepted");
    require(Application::validate(windows).refusal == ApplicationWindowRefusal::window_count,
            "an empty list names the window count");
    windows.push_back(make_window("main"));
    windows.push_back(make_window("tools", "main"));
    windows[1].options.initially_visible = false;
    windows[1].options.hide_on_close = true;
    windows[1].tool_window = true;
    require(Application::validate(windows).accepted(), "valid owned window rejected");
    windows[1].owner_id = "missing";
    require(Application::validate(windows).window_index == 1U, "missing owner not identified");
    require(Application::validate(windows).refusal == ApplicationWindowRefusal::invalid_owner,
            "a missing owner names the owner");
    windows[1].owner_id = "main";
    windows[0].owner_id = "tools";
    require(!Application::validate(windows).accepted(), "owner cycle accepted");
    windows[0].owner_id.clear();
    // Owned by "main" and named "main", the entry would own itself, refused
    // before any duplicate is looked for: the duplicate has no owner.
    windows[1].owner_id.clear();
    windows[1].stable_id = "main";
    require(!Application::validate(windows).accepted(), "duplicate ID accepted");
    require(Application::validate(windows).refusal == ApplicationWindowRefusal::duplicate_stable_id,
            "a duplicate names the stable id");
    windows[1].stable_id = "tools";
    windows[1].owner_id = "main";
    windows[0].options.title.assign("bad\0title", 9U);
    require(!Application::validate(windows).accepted(), "embedded NUL accepted");
    windows[0].options.title = "\xc0\xaf";
    require(!Application::validate(windows).accepted(), "invalid UTF-8 accepted");
    windows[0].options.title = "Application";
    windows[0].options.initial_size.width = std::numeric_limits<double>::infinity();
    require(!Application::validate(windows).accepted(), "infinite geometry accepted");
    windows[0].options.initial_size.width = 320;
    windows[0].options.hide_on_close = true;
    require(!Application::validate(windows).accepted(), "hidden primary accepted");
    require(Application::validate(windows).refusal == ApplicationWindowRefusal::hidden_primary,
            "a hidden primary is named");
    windows[0].options.hide_on_close = false;
    minimum_sizes(windows);
    ApplicationResult worker{};
    std::thread thread(ValidateOnWorker{&windows, &worker});
    thread.join();
    require(worker.error == ApplicationError::wrong_thread, "foreign model thread accepted");
}
void handles() {
    const std::shared_ptr<detail::ApplicationWindowState> state = std::make_shared<detail::ApplicationWindowState>();
    const ApplicationWindowHandle handle = detail::ApplicationHandleAccess::make(state);
    unsigned closes = 0U, full_screens = 0U;
    (*state).request_close = CountRequest{&closes};
    const HostServiceStatus unready = handle.request_close();
    require(!handle.active() && unready.error == HostServiceError::backend_failure, "unready handle callable");
    (*state).ready = true;
    const HostServiceStatus close_requested = handle.request_close();
    require(handle.active() && close_requested.accepted() && closes == 1U, "live handle did not dispatch");
    const HostServiceStatus unsupported = handle.toggle_full_screen();
    require(unsupported.error == HostServiceError::unsupported, "missing full screen host must be explicit");
    (*state).toggle_full_screen = CountRequest{&full_screens};
    const HostServiceStatus toggled = handle.toggle_full_screen();
    require(toggled.accepted() && full_screens == 1U, "full screen request did not dispatch");
    const HostServiceStatus hidden = handle.hide();
    require(hidden.error == HostServiceError::invalid_argument, "primary could hide");
    HostServiceStatus worker{};
    std::thread thread(RequestOnWorker{handle, &worker});
    thread.join();
    require(worker.error == HostServiceError::wrong_thread && closes == 1U, "foreign thread reached native request");
    (*state).closed = true;
    const HostServiceStatus closed_request = handle.request_close();
    require(!handle.active() && closed_request.error == HostServiceError::after_shutdown, "closed handle callable");
    const HostServiceStatus closed_toggle = handle.toggle_full_screen();
    require(closed_toggle.error == HostServiceError::after_shutdown && full_screens == 1U, "closed handle toggled full screen");
    const ApplicationWindowHandle empty{};
    const HostServiceStatus empty_shown = empty.show();
    require(!empty.active() && empty_shown.error == HostServiceError::after_shutdown, "empty handle callable");
}
void reusable_hide() {
    const std::shared_ptr<Control> root = make_control<Control>(StableId("reusable.root"));
    Window window(root, {320, 240});
    HostSession session(window, {HostCapabilities::current_protocol_version, "test", HostCapability::lifecycle});
    HostEvent event{};
    event.sequence = 1U;
    event.payload = HostAttachEvent{{320, 240}, 1.0};
    const HostDispatchResult attached = session.dispatch(event);
    require(attached.accepted(), "attach failed");
    SubscriptionToken token = session.closing().subscribe(ChangeHidePolicy{});
    for (unsigned index = 0U; index < 3U; ++index) {
        ++event.sequence;
        event.payload = HostCloseRequest{HostCloseReason::user, false, true};
        const HostDispatchResult hidden = session.dispatch(event);
        require(hidden.accepted() && hidden.close_allowed && session.snapshot().phase == HostLifecyclePhase::attached,
                "hide authorization terminated the attached session");
        ++event.sequence;
        event.payload = HostActivationEvent{true};
        const HostDispatchResult activated = session.dispatch(event);
        require(activated.accepted(), "reopened session rejected activation");
    }
    ++event.sequence;
    event.payload = HostCloseRequest{HostCloseReason::user, false};
    const HostDispatchResult closed = session.dispatch(event);
    require(closed.close_allowed && session.snapshot().phase == HostLifecyclePhase::close_authorized,
            "ordinary close did not authorize termination");
}
}
int main() {
    titles();
    client_sizes();
    validation();
    handles();
    reusable_hide();
    synchronous_close_lifetime();
    std::cout << "application validation, weak handles, affinity, and reusable hide passed\n";
}
