#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace gui_forms;

// Each test holds its fixture on the heap. main inlines every test, and
// the sanitizer build gives each inlined local its own stack slot, so
// fixtures held on the stack add up to a frame of 103 KB in main.

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class InputProbe final : public Control {
public:
    explicit InputProbe(StableId id) : Control(std::move(id)) {
        focusable = true;
        allow_drop = true;
    }

    void on_pointer(PointerEvent& event) override {
        ++pointer_events;
        if (throw_on_pointer) {
            throw std::runtime_error("intentional host callback fault");
        }
        event.handled = true;
    }

    void on_key(KeyEvent& event) override {
        ++key_events;
        event.handled = true;
    }

    void on_text_input(TextInputEvent& event) override {
        ++text_events;
        last_text = event.text_utf8;
        event.handled = true;
    }

    void on_drag(DragEvent& event) override {
        drag_actions.push_back(event.action);
        if (!event.items.empty()) {
            last_drag_items = event.items;
        }
        event.accepted_effect = drag_effect;
        event.handled = true;
    }

    std::uint64_t pointer_events{};
    std::uint64_t key_events{};
    std::uint64_t text_events{};
    std::string last_text;
    std::vector<DragAction> drag_actions;
    std::vector<DragDataItem> last_drag_items;
    DragEffect drag_effect{DragEffect::copy};
    bool throw_on_pointer{};
};

class HostLeasePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

class HostLeaseInputProbe final : public Control {
public:
    explicit HostLeaseInputProbe(StableId id) : Control(std::move(id)) {
        focusable = true;
        allow_drop = true;
    }

    void on_paint(Painter& painter, Rect damage) override {
        in_application_paint = true;
        if (inject) {
            result = inject();
            inject = {};
        }
        in_application_paint = false;
        painter.fill_rect(damage, Color::rgba(12, 24, 36));
    }

    void on_pointer(PointerEvent& event) override {
        callback_during_paint |= in_application_paint;
        ++pointer_events;
        event.handled = true;
    }

    void on_drag(DragEvent& event) override {
        callback_during_paint |= in_application_paint;
        drag_actions.push_back(event.action);
        event.accepted_effect = DragEffect::copy;
        event.handled = true;
    }

    std::function<HostDispatchResult()> inject;
    HostDispatchResult result;
    bool in_application_paint{};
    bool callback_during_paint{};
    std::uint64_t pointer_events{};
    std::vector<DragAction> drag_actions;
};

struct Fixture final {
    Fixture()
        : probe(make_control<InputProbe>(StableId("host.probe"))),
          window(probe, {100.0, 80.0}),
          host(window) {
        (*probe).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    }

    std::shared_ptr<InputProbe> probe;
    Window window;
    host::HeadlessHost host;
};

HostMonitor reference_monitor() {
    return {"headless.primary",
            {0.0, 0.0, 1920.0, 1080.0},
            {0.0, 0.0, 1920.0, 1040.0},
            1.0,
            true};
}

HostDialogRequest message_request(std::uint64_t request_id,
                                  HostMessageButtons buttons = HostMessageButtons::ok_cancel) {
    HostMessageDialogRequest message;
    message.title = "Portable dialog";
    message.message = "Renderer-free modal contract Ω";
    message.buttons = buttons;
    message.icon = HostMessageIcon::information;
    message.default_choice = buttons == HostMessageButtons::yes_no
        ? HostDialogChoice::yes : HostDialogChoice::ok;
    return {request_id, "gallery.window", std::move(message)};
}

class AppendCanonicalModalTrace final {
public:
    explicit AppendCanonicalModalTrace(std::string& trace) : trace_(trace) {}

    void operator()(const HostModalTransition& transition) const {
        trace_ += transition.entering ? "modal=enter" : "modal=leave";
        trace_ += " request=" + std::to_string(transition.request_id);
        trace_ += " depth=" + std::to_string(transition.depth) + '\n';
    }

private:
    std::string& trace_;
};

HostDialogResult handle_canonical_dialog(
    const HostDialogRequest& request, host::HeadlessHostServices& adapter) {
    if (request.request_id == 70) {
        static_cast<void>(adapter.show_dialog(message_request(71)));
        return HostDialogResult{
            {}, request.request_id,
            HostMessageDialogResult{HostDialogOutcome::cancelled,
                                    HostDialogChoice::cancel}};
    }
    return HostDialogResult{
        {}, request.request_id,
        HostMessageDialogResult{HostDialogOutcome::accepted,
                                HostDialogChoice::ok}};
}

class PlaySoundCueOffThread final {
public:
    PlaySoundCueOffThread(host::HeadlessHostServices& services,
                          HostServiceStatus& result, std::uint64_t timestamp)
        : services_(services), result_(result), timestamp_(timestamp) {}

    void operator()() const {
        result_ = services_.play_sound_cue(
            {HostSoundCue::error, 1.0, timestamp_});
    }

private:
    host::HeadlessHostServices& services_;
    HostServiceStatus& result_;
    std::uint64_t timestamp_;
};

class ShowDialogOffThread final {
public:
    ShowDialogOffThread(host::HeadlessHostServices& services,
                        HostDialogResult& result)
        : services_(services), result_(result) {}

    void operator()() const {
        result_ = services_.show_dialog(message_request(15));
    }

private:
    host::HeadlessHostServices& services_;
    HostDialogResult& result_;
};

class AppendNestedModalTrace final {
public:
    explicit AppendNestedModalTrace(std::string& transitions)
        : transitions_(transitions) {}

    void operator()(const HostModalTransition& transition) const {
        transitions_ += transition.entering ? "enter:" : "leave:";
        transitions_ += std::to_string(transition.request_id) + ":" +
            std::to_string(transition.depth) + '\n';
    }

private:
    std::string& transitions_;
};

class NestedModalHandler final {
public:
    NestedModalHandler(Fixture& fixture, HostDialogResult& nested)
        : fixture_(fixture), nested_(nested) {}

    HostDialogResult operator()(const HostDialogRequest& request,
                                host::HeadlessHostServices& adapter) const {
        if (request.request_id == 100) {
            require(adapter.snapshot().modal_depth == 1 &&
                        fixture_.host.session().snapshot().modal_depth == 1 &&
                        !fixture_.window.captured_control(),
                    "modal entry must publish depth and release owner capture first");
            PointerEvent blocked;
            blocked.action = PointerAction::move;
            blocked.position = {20.0, 20.0};
            const std::uint64_t before = (*fixture_.probe).pointer_events;
            const HostDispatchResult blocked_result =
                fixture_.host.dispatch(blocked, 2);
            require(blocked_result.accepted() && !blocked_result.handled &&
                        (*fixture_.probe).pointer_events == before,
                    "owner input must be sequenced but suppressed during a modal call");
            nested_ = adapter.show_dialog(message_request(200));
            const HostDialogResult duplicate = adapter.show_dialog(request);
            require(duplicate.status.error == HostServiceError::invalid_argument,
                    "an active dialog identity must not reenter itself");
            return HostDialogResult{
                {}, request.request_id,
                HostMessageDialogResult{HostDialogOutcome::cancelled,
                                        HostDialogChoice::cancel}};
        }
        return HostDialogResult{
            {}, request.request_id,
            HostMessageDialogResult{HostDialogOutcome::accepted,
                                    HostDialogChoice::ok}};
    }

private:
    Fixture& fixture_;
    HostDialogResult& nested_;
};

class ModalLimitHandler final {
public:
    explicit ModalLimitHandler(HostServiceError& terminal_error)
        : terminal_error_(terminal_error) {}

    HostDialogResult operator()(const HostDialogRequest& request,
                                host::HeadlessHostServices& adapter) const {
        if (request.request_id <= HostServices::maximum_nested_modal_depth) {
            const HostDialogResult child = adapter.show_dialog(
                message_request(request.request_id + 1));
            if (!child.status.accepted()) {
                terminal_error_ = child.status.error;
            }
        }
        return HostDialogResult{
            {}, request.request_id,
            HostMessageDialogResult{HostDialogOutcome::cancelled,
                                    HostDialogChoice::cancel}};
    }

private:
    HostServiceError& terminal_error_;
};

HostDialogResult shutdown_during_dialog(
    const HostDialogRequest& request, host::HeadlessHostServices& adapter) {
    adapter.shutdown();
    return HostDialogResult{
        {}, request.request_id,
        HostMessageDialogResult{HostDialogOutcome::accepted,
                                HostDialogChoice::ok}};
}

class InterruptModalObserver final {
public:
    InterruptModalObserver(HostServices& services, bool entering, bool shutdown)
        : services_(services), entering_(entering), shutdown_(shutdown) {}
    void operator()(const HostModalTransition& transition) const {
        if (transition.entering != entering_) return;
        if (shutdown_) services_.shutdown();
        else throw std::runtime_error("modal observer failure");
    }
private:
    HostServices& services_;
    bool entering_;
    bool shutdown_;
};
class CountDialogCalls final {
public:
    explicit CountDialogCalls(unsigned& count) : count_(count) {}
    HostDialogResult operator()(const HostDialogRequest& request,
                                host::HeadlessHostServices&) const {
        ++count_;
        return {{}, request.request_id,
                HostMessageDialogResult{HostDialogOutcome::accepted, HostDialogChoice::ok}};
    }
private:
    unsigned& count_;
};

class QueryMonitorsOffThread final {
public:
    QueryMonitorsOffThread(HostServices& services, HostMonitorResult& result)
        : services_(services), result_(result) {}

    void operator()() const { result_ = services_.query_monitors(); }

private:
    HostServices& services_;
    HostMonitorResult& result_;
};

class DispatchSessionOffThread final {
public:
    DispatchSessionOffThread(Fixture& fixture, HostDispatchResult& result)
        : fixture_(fixture), result_(result) {}

    void operator()() const {
        HostEvent event{1, 1, HostActivationEvent{true}};
        result_ = fixture_.host.session().dispatch(std::move(event));
    }

private:
    Fixture& fixture_;
    HostDispatchResult& result_;
};

void cancel_close(HostCloseRequest& request) { request.cancel = true; }

class InjectPointerDuringPaint final {
public:
    explicit InjectPointerDuringPaint(host::HeadlessHost& host) : host_(host) {}

    HostDispatchResult operator()() const {
        PointerEvent down;
        down.action = PointerAction::down;
        down.button = PointerButton::primary;
        down.position = {20.0, 20.0};
        down.pointer_id = 9U;
        return host_.dispatch(std::move(down), 2U);
    }

private:
    host::HeadlessHost& host_;
};

class InjectCompactedDragOvers final {
public:
    InjectCompactedDragOvers(host::HeadlessHost& host, std::uint64_t& sequence,
                             bool& every_deferred)
        : host_(host), sequence_(sequence), every_deferred_(every_deferred) {}

    HostDispatchResult operator()() const {
        HostDispatchResult last;
        for (std::uint32_t index = 0U; index < 32U; ++index) {
            DragEvent over;
            over.action = DragAction::over;
            over.session_id = 77U;
            over.position = {20.0 + static_cast<double>(index), 20.0};
            over.allowed_effects = DragEffect::copy | DragEffect::move;
            over.items.emplace_back(DragTextData{"lease-drag"});
            last = host_.dispatch(std::move(over), sequence_++);
            every_deferred_ = every_deferred_ && last.handled &&
                last.input_deferred && !last.input_capacity_rejected &&
                last.drag_effect == DragEffect::copy;
        }
        return last;
    }

private:
    host::HeadlessHost& host_;
    std::uint64_t& sequence_;
    bool& every_deferred_;
};

std::string canonical_host_trace() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    std::uint64_t time = 1'000;
    require(fixture.host.dispatch(HostAttachEvent{{320.0, 180.0}, 2.0}, time++).accepted(),
            "headless attach must be accepted");
    static_cast<void>(fixture.host.dispatch(
        HostDisplayEvent{{reference_monitor()}}, time++));
    static_cast<void>(fixture.host.dispatch(HostActivationEvent{true}, time++));
    static_cast<void>(fixture.host.dispatch(HostOcclusionEvent{true}, time++));
    static_cast<void>(fixture.host.dispatch(HostOcclusionEvent{false}, time++));
    PointerEvent pointer;
    pointer.action = PointerAction::down;
    pointer.button = PointerButton::primary;
    pointer.position = {20.0, 20.0};
    pointer.pointer_id = 1;
    static_cast<void>(fixture.host.dispatch(pointer, time++));
    KeyEvent key;
    key.action = KeyAction::down;
    key.physical_key = 36;
    static_cast<void>(fixture.host.dispatch(key, time++));
    TextInputEvent text;
    text.text_utf8 = "host-text";
    static_cast<void>(fixture.host.dispatch(std::move(text), time++));
    static_cast<void>(fixture.host.dispatch(HostCloseRequest{HostCloseReason::test, false},
                                            time++));
    static_cast<void>(fixture.host.dispatch(HostClosedEvent{HostCloseReason::test}, time++));
    static_cast<void>(fixture.host.dispatch(HostShutdownEvent{}, time++));
    return fixture.host.trace();
}

std::string canonical_service_trace() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    HostServices& services = fixture.host.services();
    const HostMonitorResult monitors = services.query_monitors();
    std::string trace = "service=monitors error=";
    trace += host_service_error_name(monitors.status.error);
    trace += " count=" + std::to_string(monitors.monitors.size());
    if (!monitors.monitors.empty()) {
        trace += " primary=" + monitors.monitors.front().id;
    }
    trace += '\n';
    const HostServiceStatus cursor = services.set_cursor(CursorKind::text);
    trace += "service=cursor error=";
    trace += host_service_error_name(cursor.error);
    trace += " value=";
    trace += cursor_kind_name(services.snapshot().cursor);
    trace += '\n';
    const HostServiceStatus capture = services.set_pointer_capture(true, 7);
    trace += "service=capture error=";
    trace += host_service_error_name(capture.error);
    trace += " pointer=7\n";
    const HostServiceStatus release = services.set_pointer_capture(false);
    trace += "service=release error=";
    trace += host_service_error_name(release.error);
    trace += '\n';
    const HostServiceStatus write = services.write_clipboard_text("service Ω");
    trace += "service=clipboard_write error=";
    trace += host_service_error_name(write.error);
    trace += '\n';
    const HostClipboardTextResult read = services.read_clipboard_text();
    trace += "service=clipboard_read error=";
    trace += host_service_error_name(read.status.error);
    trace += " generation=" + std::to_string(read.generation);
    trace += " text=" + read.text_utf8 + '\n';
    host::HeadlessHostServices& headless =
        static_cast<host::HeadlessHostServices&>(services);
    headless.queue_dialog_result(
        {{}, 40, HostMessageDialogResult{HostDialogOutcome::cancelled,
                                         HostDialogChoice::cancel}});
    const HostDialogResult dialog = services.show_dialog(message_request(40));
    trace += headless.dialog_trace();
    trace += "service=dialog error=";
    trace += host_service_error_name(dialog.status.error);
    trace += '\n';
    trace += "snapshot=" + services.snapshot().to_json() + '\n';
    return trace;
}

std::string canonical_modal_trace() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(
        fixture.host.services());
    std::string trace;
    SubscriptionToken observation = services.modal_changed().subscribe(
        AppendCanonicalModalTrace(trace));
    services.set_dialog_handler(handle_canonical_dialog);
    static_cast<void>(services.show_dialog(message_request(70)));
    trace += services.dialog_trace();
    trace += "services=" + services.snapshot().to_json() + '\n';
    trace += "session=" + fixture.host.session().snapshot().to_json() + '\n';
    return trace;
}

void test_capabilities_and_normalized_dispatch() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    const HostCapabilities capabilities = host::headless_capabilities();
    require(capabilities.supports(HostCapability::lifecycle |
                                      HostCapability::scale_notifications |
                                      HostCapability::pointer_input |
                                      HostCapability::keyboard_input |
                                      HostCapability::text_composition |
                                      HostCapability::monitor_geometry |
                                      HostCapability::pointer_capture |
                                      HostCapability::cursor |
                                      HostCapability::clipboard |
                                      HostCapability::typed_drag_destination |
                                      HostCapability::dialogs |
                                      HostCapability::sound_cues),
            "headless host must report every implemented normalized input capability");
    require(capabilities.to_json().find("\"scale_notifications\"") !=
                std::string::npos,
            "capability report must expose readable names, not only a bit field");

    require(fixture.host.dispatch(HostAttachEvent{{640.0, 360.0}, 2.0}, 1).accepted(),
            "valid attach event must be accepted");
    require(fixture.window.client_size() == Size{640.0, 360.0} &&
                fixture.window.scale() == 2.0,
            "host attach must update retained size and scale through one transaction");

    PointerEvent pointer;
    pointer.action = PointerAction::down;
    pointer.button = PointerButton::primary;
    pointer.position = {10.0, 10.0};
    const HostDispatchResult pointer_result = fixture.host.dispatch(pointer, 2);
    require(pointer_result.handled && (*fixture.probe).pointer_events == 1,
            "normalized pointer input must reach the retained target");
    require(fixture.window.focused_control() == fixture.probe,
            "pointer-down through the host protocol must preserve focus behavior");

    KeyEvent key;
    key.physical_key = 48;
    require(fixture.host.dispatch(key, 3).handled && (*fixture.probe).key_events == 1,
            "normalized key input must reach the retained focus target");
    TextInputEvent text;
    text.text_utf8 = "é";
    require(fixture.host.dispatch(std::move(text), 4).handled &&
                (*fixture.probe).last_text == "é",
            "committed UTF-8 host text must retain bytes and handling result");
}

void test_application_callback_fault_is_contained_at_host_boundary() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    require(fixture.host.dispatch(
                HostAttachEvent{{320.0, 180.0}, 1.0}, 1).accepted(),
            "callback-fault fixture must attach before input");
    (*fixture.probe).throw_on_pointer = true;
    PointerEvent pointer;
    pointer.action = PointerAction::down;
    pointer.button = PointerButton::primary;
    pointer.position = {10.0, 10.0};
    pointer.pointer_id = 7U;
    const HostDispatchResult fault = fixture.host.dispatch(pointer, 2);
    const HostSessionSnapshot after_fault = fixture.host.session().snapshot();
    require(fault.error == HostDispatchError::callback_fault &&
                after_fault.callback_faults == 1U &&
                after_fault.last_sequence == 2U &&
                !fixture.window.captured_control(),
            "throwing input callbacks must become an observable host fault and release capture");

    (*fixture.probe).throw_on_pointer = false;
    pointer.action = PointerAction::move;
    const HostDispatchResult recovered = fixture.host.dispatch(pointer, 3);
    require(recovered.accepted() && recovered.handled &&
                fixture.host.session().snapshot().callback_faults == 1U,
            "a contained callback fault must not poison later host dispatch");
    require(std::string(host_dispatch_error_name(
                HostDispatchError::callback_fault)) == "callback_fault" &&
                after_fault.to_json().find("\"callback_faults\":1") !=
                    std::string::npos,
            "callback fault telemetry must be stable and machine readable");
}

void test_semantic_sound_cues_are_bounded_and_deterministic() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(
        fixture.host.services());
    require(fixture.window.host_services() == &services,
            "an active host session must expose its portable services to controls");

    constexpr std::uint64_t start = 1'000'000'000U;
    require(services.play_sound_cue(
                {HostSoundCue::notification, 0.8, start}).accepted() &&
            services.play_sound_cue(
                {HostSoundCue::notification, 0.8, start + 10'000'000U}).accepted() &&
            services.play_sound_cue(
                {HostSoundCue::success, 0.0, start + 70'000'000U}).accepted(),
            "valid semantic cues, coalesced bursts, and muted presentation must succeed");
    const HostServicesSnapshot snapshot = services.snapshot();
    require(snapshot.sound_requests == 3U && snapshot.sound_playbacks == 1U &&
                snapshot.sound_coalesced == 1U && snapshot.sound_muted == 1U &&
                snapshot.sound_coalescing_window_nanoseconds == 50'000'000U &&
                services.sound_trace() ==
                    "sound=notification timestamp=1000000000 gain=0.8\n",
            "headless sound accounting and adapter trace must be exact");
    require(services.play_sound_cue(
                {HostSoundCue::warning, 1.0, start - 1U}).error ==
                    HostServiceError::invalid_argument,
            "sound timestamps must remain monotonic across cue identities");

    require(services.set_sound_cue_coalescing_window(
                std::chrono::nanoseconds::zero()).accepted() &&
                services.sound_cue_coalescing_window() ==
                    std::chrono::nanoseconds::zero(),
            "host sound burst coalescing must be explicitly disableable");
    require(services.play_sound_cue(
                {HostSoundCue::notification, 0.5, start + 80'000'000U}).accepted() &&
                services.play_sound_cue(
                {HostSoundCue::notification, 0.5, start + 80'000'001U}).accepted(),
            "a zero coalescing window must preserve consecutive semantic cues");
    require(services.snapshot().sound_playbacks == 3U &&
                services.set_sound_cue_coalescing_window(
                    std::chrono::seconds(6)).error ==
                    HostServiceError::invalid_argument,
            "sound coalescing customization must enforce its declared bound");

    HostServiceStatus wrong_thread;
    std::thread worker(PlaySoundCueOffThread(
        services, wrong_thread, start + 100'000'000U));
    worker.join();
    require(wrong_thread.error == HostServiceError::wrong_thread,
            "sound cues must preserve host-service UI-thread enforcement");
}

void test_typed_dialog_requests_and_results() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(
        fixture.host.services());

    services.queue_dialog_result(
        {{}, 10, HostMessageDialogResult{HostDialogOutcome::accepted,
                                         HostDialogChoice::yes}});
    const HostDialogResult message = services.show_dialog(
        message_request(10, HostMessageButtons::yes_no));
    require(message.status.accepted() &&
                std::get<HostMessageDialogResult>(message.payload).choice ==
                    HostDialogChoice::yes,
            "typed message dialog must preserve its accepted choice");

    HostOpenFileDialogRequest open;
    open.title = "Open recording";
    open.initial_directory = "/fixtures";
    open.suggested_name = "capture.wav";
    open.filters = {{"Recordings", {"wav", "iq"}}};
    open.allow_multiple = true;
    services.queue_dialog_result(
        {{}, 11, HostPathDialogResult{HostDialogOutcome::accepted,
                                      {"/fixtures/a.wav", "/fixtures/b.iq"}}});
    const HostDialogResult opened = services.show_dialog(
        {11, "gallery.window", std::move(open)});
    require(opened.status.accepted() &&
                std::get<HostPathDialogResult>(opened.payload).paths.size() == 2,
            "open-file result must preserve bounded multiple selections");

    HostSaveFileDialogRequest save;
    save.title = "Save profile";
    save.initial_directory = "/profiles";
    save.suggested_name = "receiver";
    save.default_extension = "json";
    save.filters = {{"Profiles", {"json"}}};
    services.queue_dialog_result(
        {{}, 12, HostPathDialogResult{HostDialogOutcome::cancelled, {}}});
    const HostDialogResult saved = services.show_dialog(
        {12, "gallery.window", std::move(save)});
    require(saved.status.accepted() &&
                std::get<HostPathDialogResult>(saved.payload).outcome ==
                    HostDialogOutcome::cancelled &&
                std::get<HostPathDialogResult>(saved.payload).paths.empty(),
            "cancelled save must carry no value for a facade to commit");

    services.queue_dialog_result(
        {{}, 13, HostPathDialogResult{HostDialogOutcome::accepted,
                                      {"/fixtures"}}});
    const HostDialogResult folder = services.show_dialog(
        {13, "gallery.window", HostFolderDialogRequest{"Select folder", "/"}});
    require(folder.status.accepted() &&
                std::get<HostPathDialogResult>(folder.payload).paths.front() ==
                    "/fixtures",
            "folder result must retain its selected UTF-8 path");

    services.queue_dialog_result(
        {{}, 14, HostColorDialogResult{HostDialogOutcome::accepted, 0x3366CCFFU}});
    const HostDialogResult color = services.show_dialog(
        {14, "gallery.window",
         HostColorDialogRequest{"Select accent", 0x112233FFU, false}});
    require(color.status.accepted() &&
                std::get<HostColorDialogResult>(color.payload).rgba == 0x3366CCFFU,
            "color result must retain portable RGBA bytes");

    HostDialogResult wrong_thread;
    std::thread worker(ShowDialogOffThread(services, wrong_thread));
    worker.join();
    require(wrong_thread.status.error == HostServiceError::wrong_thread,
            "dialog calls must reject the wrong thread without entering modal state");

    HostDialogRequest invalid = message_request(0);
    require(services.show_dialog(invalid).status.error ==
                HostServiceError::invalid_argument,
            "zero dialog identity must be rejected before adapter entry");
    invalid = message_request(16);
    std::get<HostMessageDialogRequest>(invalid.payload).message =
        std::string{"\xF0\x28\x8C\x28", 4};
    require(services.show_dialog(invalid).status.error ==
                HostServiceError::invalid_argument,
            "malformed dialog UTF-8 must be rejected before adapter entry");

    services.queue_dialog_result(
        {{}, 17, HostPathDialogResult{HostDialogOutcome::accepted, {"/wrong"}}});
    require(services.show_dialog(message_request(17)).status.error ==
                HostServiceError::backend_failure,
            "an adapter result whose type does not match its request must fail closed");
}

DragEvent drag_event(DragAction action, std::uint64_t session_id, Point position) {
    DragEvent event;
    event.action = action;
    event.session_id = session_id;
    event.position = position;
    event.allowed_effects = DragEffect::copy | DragEffect::move;
    event.items = {
        DragTextData{"receiver Ω"},
        DragFileListData{{"/captures/a.iq", "/captures/b.wav"}},
        DragBinaryData{"application/x-gui-forms-probe", {0x00, 0x7f, 0xff}},
    };
    return event;
}

void test_typed_drag_destination_routing_and_bounds() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    require(fixture.host.dispatch(HostAttachEvent{{100.0, 80.0}, 1.0}, 0).accepted(),
            "drag fixture must attach before host input");
    require(fixture.host.dispatch(drag_event(DragAction::enter, 90, {10.0, 10.0}), 1)
                .drag_effect == DragEffect::copy,
            "drag enter must return the target's allowed portable effect");
    require(fixture.host.dispatch(drag_event(DragAction::over, 90, {20.0, 10.0}), 2)
                .drag_effect == DragEffect::copy,
            "drag over must route to the retained opt-in target");
    const HostDispatchResult drop = fixture.host.dispatch(
        drag_event(DragAction::drop, 90, {20.0, 10.0}), 3);
    require(drop.handled && drop.drag_effect == DragEffect::copy &&
                (*fixture.probe).drag_actions ==
                    std::vector<DragAction>{DragAction::enter, DragAction::over,
                                            DragAction::drop} &&
                (*fixture.probe).last_drag_items.size() == 3 &&
                std::get<DragTextData>((*fixture.probe).last_drag_items[0]).text_utf8 ==
                    "receiver Ω" &&
                std::get<DragFileListData>((*fixture.probe).last_drag_items[1])
                    .paths_utf8.size() == 2 &&
                std::get<DragBinaryData>((*fixture.probe).last_drag_items[2]).bytes.back() ==
                    0xff,
            "typed drop must preserve variant order, UTF-8, file lists, and opaque bytes");
    require(fixture.host.session().snapshot().drag_events == 3 &&
                fixture.host.session().snapshot().drag_drops == 1,
            "drag accounting must distinguish sequenced events from completed drops");

    DragEvent invalid = drag_event(DragAction::enter, 0, {10.0, 10.0});
    require(fixture.host.dispatch(std::move(invalid), 4).error ==
                HostDispatchError::invalid_payload,
            "zero drag identity must be rejected before retained routing");
    invalid = drag_event(DragAction::enter, 91, {10.0, 10.0});
    std::get<DragTextData>(invalid.items[0]).text_utf8 =
        std::string{"\xF0\x28\x8C\x28", 4};
    require(fixture.host.dispatch(std::move(invalid), 5).error ==
                HostDispatchError::invalid_payload,
            "malformed drag UTF-8 must fail at the host boundary");
    invalid = drag_event(DragAction::enter, 92, {10.0, 10.0});
    std::get<DragBinaryData>(invalid.items[2]).bytes.resize(
        DragLimits::maximum_total_bytes);
    require(fixture.host.dispatch(std::move(invalid), 6).error ==
                HostDispatchError::invalid_payload,
            "aggregate drag payloads beyond 16 MiB must fail before callbacks");

    std::shared_ptr<gui_forms::Control> root = make_control<Control>(StableId("drag.root"));
    (*root).set_requested_bounds({0.0, 0.0, 100.0, 40.0});
    std::shared_ptr<InputProbe> left =
        make_control<InputProbe>(StableId("drag.left"));
    std::shared_ptr<InputProbe> right =
        make_control<InputProbe>(StableId("drag.right"));
    (*left).set_requested_bounds({0.0, 0.0, 50.0, 40.0});
    (*right).set_requested_bounds({50.0, 0.0, 50.0, 40.0});
    (*root).add_child(left);
    (*root).add_child(right);
    const std::unique_ptr<Window> held_transition_window = std::make_unique<Window>(root, Size{100.0, 40.0});
    Window& transition_window = *held_transition_window;
    host::HeadlessHost transition_host(transition_window);
    require(transition_host.dispatch(HostAttachEvent{{100.0, 40.0}, 1.0}, 0)
                .accepted(),
            "transition drag fixture must attach before host input");
    static_cast<void>(transition_host.dispatch(
        drag_event(DragAction::enter, 93, {10.0, 10.0}), 1));
    static_cast<void>(transition_host.dispatch(
        drag_event(DragAction::over, 93, {60.0, 10.0}), 2));
    require((*left).drag_actions ==
                std::vector<DragAction>{DragAction::enter, DragAction::leave} &&
                (*right).drag_actions ==
                std::vector<DragAction>{DragAction::enter, DragAction::over},
            "target crossing must deterministically leave old, enter new, then deliver over");
    (*right).enabled = false;
    (*right).enabled = true;
    static_cast<void>(transition_host.dispatch(
        drag_event(DragAction::leave, 93, {60.0, 10.0}), 3));
    require((*right).drag_actions.size() == 2,
            "eligibility revocation must clear drag ownership without a stale callback");
}

void test_nested_modal_order_owner_suppression_and_limit() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    require(fixture.host.dispatch(HostAttachEvent{{100.0, 80.0}, 1.0}, 0).accepted(),
            "modal fixture must attach before host input");
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(
        fixture.host.services());
    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = {10.0, 10.0};
    down.pointer_id = 31;
    require(fixture.host.dispatch(down, 1).handled &&
                fixture.window.captured_control() == fixture.probe,
            "modal fixture must begin with focused captured owner input");

    std::string transitions;
    SubscriptionToken observation = services.modal_changed().subscribe(
        AppendNestedModalTrace(transitions));
    HostDialogResult nested;
    services.set_dialog_handler(NestedModalHandler(fixture, nested));
    const HostDialogResult outer = services.show_dialog(message_request(100));
    require(outer.status.accepted() && nested.status.accepted() &&
                transitions == "enter:100:1\nenter:200:2\nleave:200:1\nleave:100:0\n",
            "nested modal transitions must use one deterministic stack order");
    const HostServicesSnapshot service_snapshot = services.snapshot();
    const HostSessionSnapshot session_snapshot = fixture.host.session().snapshot();
    require(service_snapshot.dialog_requests == 2 &&
                service_snapshot.dialog_completions == 2 &&
                service_snapshot.dialog_cancellations == 1 &&
                service_snapshot.maximum_modal_depth == 2 &&
                service_snapshot.modal_depth == 0 &&
                session_snapshot.modal_transitions == 4 &&
                session_snapshot.modal_input_suppressions == 1 &&
                session_snapshot.modal_depth == 0 &&
                fixture.window.focused_control() == fixture.probe,
            "modal exit must restore owner eligibility without losing focus");
    observation.disconnect();

    const std::unique_ptr<Fixture> held_limit_fixture = std::make_unique<Fixture>();

    Fixture& limit_fixture = *held_limit_fixture;
    host::HeadlessHostServices& limit_services =
        static_cast<host::HeadlessHostServices&>(limit_fixture.host.services());
    HostServiceError terminal_error = HostServiceError::none;
    limit_services.set_dialog_handler(ModalLimitHandler(terminal_error));
    require(limit_services.show_dialog(message_request(1)).status.accepted() &&
                terminal_error == HostServiceError::modal_limit &&
                limit_services.snapshot().maximum_modal_depth ==
                    HostServices::maximum_nested_modal_depth &&
                limit_services.snapshot().modal_depth == 0,
            "nested modal depth must fail closed at the portable bound and unwind");

    const std::unique_ptr<Fixture> held_shutdown_fixture = std::make_unique<Fixture>();

    Fixture& shutdown_fixture = *held_shutdown_fixture;
    host::HeadlessHostServices& shutdown_services =
        static_cast<host::HeadlessHostServices&>(shutdown_fixture.host.services());
    shutdown_services.set_dialog_handler(shutdown_during_dialog);
    const HostDialogResult interrupted =
        shutdown_services.show_dialog(message_request(300));
    require(interrupted.status.error == HostServiceError::after_shutdown &&
                shutdown_services.snapshot().shutdown &&
                shutdown_services.snapshot().modal_depth == 0,
            "shutdown during a modal adapter call must unwind and reject its stale result");
}

void test_modal_observer_failure_and_shutdown_unwind() {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
        Fixture& fixture = *held_fixture;
        host::HeadlessHostServices& services =
            static_cast<host::HeadlessHostServices&>(fixture.host.services());
        unsigned calls{};
        services.set_dialog_handler(CountDialogCalls(calls));
        SubscriptionToken observation = services.modal_changed().subscribe(
            InterruptModalObserver(services, scenario != 1, scenario == 2));
        const HostDialogResult result = services.show_dialog(message_request(301));
        require(result.status.error == (scenario == 2 ? HostServiceError::after_shutdown
                                                     : HostServiceError::backend_failure) &&
                    result.request_id == 301 && services.snapshot().modal_depth == 0 &&
                    fixture.host.session().snapshot().modal_depth == 0 &&
                    calls == (scenario == 1 ? 1U : 0U),
                "observer failure or shutdown must unwind modal state and skip stale backend calls");
        observation.disconnect();
        if (scenario != 2) {
            require(services.show_dialog(message_request(301)).status.accepted() &&
                        services.snapshot().modal_depth == 0,
                    "observer failure must leave the request ID and owner usable");
        }
    }
}

void test_display_capture_and_occlusion_synchronization() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    require(fixture.host.dispatch(HostAttachEvent{{640.0, 360.0}, 1.0}, 1).accepted(),
            "host synchronization fixture must attach");
    require(fixture.host.dispatch(
                HostDisplayEvent{{reference_monitor()}}, 2).accepted(),
            "valid display topology must enter the sequenced host trace");
    HostDisplayEvent invalid_display;
    require(fixture.host.dispatch(std::move(invalid_display), 3).error ==
                HostDispatchError::invalid_geometry,
            "empty display topology must be rejected before snapshot mutation");
    const HostSessionSnapshot display_snapshot = fixture.host.session().snapshot();
    require(display_snapshot.display_changes == 1 &&
                display_snapshot.monitor_count == 1,
            "accepted display topology must update structured session state once");

    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = {10.0, 10.0};
    down.pointer_id = 7;
    require(fixture.host.dispatch(down, 4).handled &&
                fixture.window.captured_control() == fixture.probe &&
                fixture.host.services().snapshot().pointer_captured &&
                fixture.host.services().snapshot().captured_pointer_id == 7,
            "retained pointer-down capture must synchronize to the host service");
    PointerEvent up = down;
    up.action = PointerAction::up;
    require(fixture.host.dispatch(up, 5).handled &&
                !fixture.window.captured_control() &&
                !fixture.host.services().snapshot().pointer_captured &&
                fixture.host.services().snapshot().pointer_capture_updates == 2,
            "physical release must synchronously release retained and host capture");

    down.pointer_id = 8;
    require(fixture.host.dispatch(down, 6).handled &&
                fixture.host.services().snapshot().pointer_captured,
            "a second retained capture must synchronize before revocation");
    (*fixture.probe).enabled = false;
    require(!fixture.window.captured_control() &&
                !fixture.host.services().snapshot().pointer_captured &&
                fixture.host.services().snapshot().pointer_capture_updates == 4 &&
                fixture.window.metrics_snapshot().capture_revocations == 1,
            "eligibility revocation must release retained and host capture once");
    (*fixture.probe).enabled = true;

    const FrameTime base{};
    FrameRequestToken active = fixture.window.schedule_paint(
        fixture.probe, base + std::chrono::nanoseconds(10));
    require(fixture.host.dispatch(HostOcclusionEvent{true}, 7).accepted() &&
                fixture.window.occluded() && !fixture.window.next_wake().has_value() &&
                active.connected(),
            "host occlusion must pause rather than revoke retained paint work");
    require(fixture.host.dispatch(HostOcclusionEvent{false}, 20).accepted() &&
                !fixture.window.occluded() &&
                fixture.window.next_wake() == base + std::chrono::nanoseconds(10),
            "host resume must expose the overdue paint deadline once");
    const FramePollResult resumed =
        fixture.window.poll_frame_schedule(base + std::chrono::nanoseconds(20));
    require(resumed.deadlines_fired == 1 && !active.connected(),
            "resumed host work must deliver the held deadline as one paint");
}

void test_environment_services_are_bounded_and_deterministic() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    HostServices& services = fixture.host.services();
    const HostMonitorResult monitors = services.query_monitors();
    require(monitors.status.accepted() && monitors.monitors.size() == 1 &&
                monitors.monitors.front().id == "headless.primary" &&
                monitors.monitors.front().work_area == Rect{0.0, 0.0, 1920.0, 1040.0} &&
                monitors.monitors.front().primary,
            "headless monitor query must return the stable reference desktop");

    require(services.set_cursor(CursorKind::hand).accepted() &&
                services.snapshot().cursor == CursorKind::hand,
            "cursor service must retain the last accepted portable cursor");
    require(services.set_pointer_capture(true, 0).error ==
                HostServiceError::invalid_argument,
            "host capture must reject the reserved zero pointer identity");
    require(services.write_clipboard_text("M3b Ω clipboard").accepted(),
            "headless clipboard must accept bounded UTF-8 text");
    const HostClipboardTextResult clipboard = services.read_clipboard_text();
    require(clipboard.status.accepted() && clipboard.has_text &&
                clipboard.text_utf8 == "M3b Ω clipboard" && clipboard.generation == 1,
            "headless clipboard read must reproduce text and generation");

    const std::string invalid_utf8{"\xF0\x28\x8C\x28", 4};
    require(services.write_clipboard_text(invalid_utf8).error ==
                HostServiceError::invalid_utf8,
            "clipboard must reject malformed UTF-8 before reaching the adapter");
    const std::string too_large(HostServices::maximum_clipboard_text_bytes + 1U, 'x');
    require(services.write_clipboard_text(too_large).error ==
                HostServiceError::too_large,
            "clipboard must reject text beyond the declared host-service bound");

    HostMonitorResult wrong_thread;
    std::thread worker(QueryMonitorsOffThread(services, wrong_thread));
    worker.join();
    require(wrong_thread.status.error == HostServiceError::wrong_thread,
            "host services must reject calls outside the owning UI thread");

    const HostServicesSnapshot before_shutdown = services.snapshot();
    require(before_shutdown.monitor_queries == 1 &&
                before_shutdown.cursor_updates == 1 &&
                before_shutdown.clipboard_writes == 1 &&
                before_shutdown.clipboard_reads == 1 &&
                before_shutdown.rejected_requests == 3,
            "service accounting must reject malformed work without mutating state on a wrong thread");
    services.shutdown();
    require(services.read_clipboard_text().status.error ==
                HostServiceError::after_shutdown,
            "service requests after shutdown must be rejected deterministically");
    require(services.snapshot().to_json().find("\"shutdown\":true") !=
                std::string::npos,
            "service snapshot must expose shutdown state");

    require(canonical_service_trace() == canonical_service_trace(),
            "headless service traces must be byte-identical across fresh adapters");
}

void test_sequence_geometry_and_shutdown_guards() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    HostDispatchResult wrong_thread;
    std::thread worker(DispatchSessionOffThread(fixture, wrong_thread));
    worker.join();
    require(wrong_thread.error == HostDispatchError::wrong_thread,
            "host session must reject delivery outside its owning UI thread");

    HostEvent zero{0, 1, HostActivationEvent{true}};
    require(fixture.host.session().dispatch(std::move(zero)).error ==
                HostDispatchError::sequence_zero,
            "zero host sequence must be rejected");

    HostEvent first{4, 2, HostAttachEvent{{320.0, 180.0}, 1.0}};
    require(fixture.host.session().dispatch(std::move(first)).accepted(),
            "first explicit monotonic host sequence must be accepted");
    HostEvent repeated{4, 3, HostActivationEvent{true}};
    require(fixture.host.session().dispatch(std::move(repeated)).error ==
                HostDispatchError::non_monotonic_sequence,
            "replayed host sequence must be rejected");
    HostEvent invalid{5, 4, HostResizeEvent{{-1.0, 10.0}}};
    require(fixture.host.session().dispatch(std::move(invalid)).error ==
                HostDispatchError::invalid_geometry,
            "invalid host geometry must be rejected before mutating the window");

    FrameRequestToken active = fixture.window.schedule_paint(
        fixture.probe, FrameTime{} + std::chrono::milliseconds(10));
    fixture.window.capture_pointer(fixture.probe, 11);
    require(fixture.host.services().snapshot().pointer_captured,
            "explicit retained capture must reach host services before shutdown");
    HostEvent shutdown{6, 5, HostShutdownEvent{}};
    require(fixture.host.session().dispatch(std::move(shutdown)).accepted() &&
                !active.connected() &&
                !fixture.host.services().snapshot().pointer_captured,
            "host shutdown must synchronously revoke scheduled work and capture");
    HostEvent after{7, 6, HostActivationEvent{true}};
    require(fixture.host.session().dispatch(std::move(after)).error ==
                HostDispatchError::after_shutdown,
            "events after shutdown must be rejected deterministically");
}

// Counts the presentation_changed notifications a window raises.
struct PresentationChangeCounter final {
    unsigned* count{};
    void operator()(const PresentationSettings&) const { *count = *count + 1U; }
};

void test_input_settings_report_and_validation() {
    require(host_protocol_revisions.size() == 5U && host_protocol_revisions[1].version == 9U &&
                host_protocol_revisions[2].version == 10U && host_protocol_revisions[3].version == 11U &&
                host_protocol_revisions[4].version == 12U && HostCapabilities::current_protocol_version == 12U,
            "the compositor report is version 9, the input settings 10, the mouse conventions 11 and the "
            "window state 12");
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    const PresentationSettings initial = fixture.window.presentation_settings();
    require(initial.double_click_milliseconds == default_double_click_milliseconds &&
                initial.double_click_milliseconds == 400U &&
                initial.double_click_origin == SettingOrigin::default_value,
            "before any report the window holds the stated default, as a default");
    HostEvent early{1, 1, HostInputSettingsEvent{650U, SettingOrigin::host}};
    require(fixture.host.session().dispatch(std::move(early)).error == HostDispatchError::invalid_lifecycle,
            "input settings before attach are a lifecycle error");
    HostEvent attach{2, 2, HostAttachEvent{{320.0, 180.0}, 1.0}};
    require(fixture.host.session().dispatch(std::move(attach)).accepted(), "attach accepted");

    // The application's own settings, set before the host reports.
    PresentationSettings own = fixture.window.presentation_settings();
    own.text_scale = 1.5;
    own.sound_enabled = false;
    fixture.window.set_presentation_settings(own);
    unsigned changes = 0U;
    const std::shared_ptr<Component> owner = std::make_shared<Component>();
    const SubscriptionToken token =
        fixture.window.presentation_changed().subscribe(*owner, PresentationChangeCounter{&changes});

    HostEvent reported{3, 3, HostInputSettingsEvent{650U, SettingOrigin::host}};
    require(fixture.host.session().dispatch(std::move(reported)).accepted(), "a valid report is accepted");
    const PresentationSettings& after = fixture.window.presentation_settings();
    require(after.double_click_milliseconds == 650U && after.double_click_origin == SettingOrigin::host,
            "the report reaches the window's presentation settings");
    require(after.text_scale == 1.5 && !after.sound_enabled, "the report leaves the application's settings");
    require(changes == 1U, "one report raises presentation_changed once");

    HostEvent same{4, 4, HostInputSettingsEvent{650U, SettingOrigin::host}};
    require(fixture.host.session().dispatch(std::move(same)).accepted() && changes == 1U,
            "an unchanged report raises nothing");

    HostEvent zero{5, 5, HostInputSettingsEvent{0U, SettingOrigin::host}};
    require(fixture.host.session().dispatch(std::move(zero)).error == HostDispatchError::invalid_payload,
            "a zero interval is refused");
    HostEvent too_long{6, 6, HostInputSettingsEvent{maximum_double_click_milliseconds + 1U, SettingOrigin::host}};
    require(fixture.host.session().dispatch(std::move(too_long)).error == HostDispatchError::invalid_payload,
            "an interval above the maximum is refused");
    HostEvent bad_origin{7, 7, HostInputSettingsEvent{650U, static_cast<SettingOrigin>(7U)}};
    require(fixture.host.session().dispatch(std::move(bad_origin)).error == HostDispatchError::invalid_payload,
            "an origin outside the enumeration is refused");
    require(fixture.window.presentation_settings().double_click_milliseconds == 650U && changes == 1U,
            "a refused report changes nothing");

    HostEvent longest{8, 8, HostInputSettingsEvent{maximum_double_click_milliseconds, SettingOrigin::host}};
    require(fixture.host.session().dispatch(std::move(longest)).accepted() &&
                fixture.window.presentation_settings().double_click_milliseconds == 5000U,
            "the maximum itself is accepted");
    HostEvent fallback{9, 9, HostInputSettingsEvent{default_double_click_milliseconds, SettingOrigin::default_value}};
    require(fixture.host.session().dispatch(std::move(fallback)).accepted() &&
                fixture.window.presentation_settings().double_click_origin == SettingOrigin::default_value,
            "a host without the setting reports the default as a default");

    // The application's later changes keep the host's report.
    fixture.window.set_text_scale(1.25);
    require(fixture.window.presentation_settings().double_click_origin == SettingOrigin::default_value &&
                fixture.window.presentation_settings().double_click_milliseconds == 400U,
            "a text scale change keeps the reported interval");
    PresentationSettings invalid = fixture.window.presentation_settings();
    invalid.double_click_milliseconds = 0U;
    bool threw = false;
    try {
        fixture.window.set_presentation_settings(invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && fixture.window.presentation_settings().double_click_milliseconds == 400U,
            "an application's zero interval throws and changes nothing");
    require(host_event_name(HostEventPayload{HostInputSettingsEvent{}}) == std::string("input_settings"),
            "the event is named input_settings");

    // The pointer settings travel with the report and are validated.
    HostInputSettingsEvent with_pointer{650U, SettingOrigin::host, PointerSettings{}};
    with_pointer.pointer.drag_width = 8.0;
    with_pointer.pointer.drag_height = 6.0;
    with_pointer.pointer.drag_origin = SettingOrigin::host;
    with_pointer.pointer.native_hover = true;
    HostEvent pointer_report{10, 10, with_pointer};
    require(fixture.host.session().dispatch(std::move(pointer_report)).accepted() &&
                fixture.window.presentation_settings().pointer == with_pointer.pointer,
            "the drag and hover settings reach the window");
    HostInputSettingsEvent negative_drag = with_pointer;
    negative_drag.pointer.drag_width = -1.0;
    HostEvent negative{11, 11, negative_drag};
    require(fixture.host.session().dispatch(std::move(negative)).error == HostDispatchError::invalid_payload &&
                fixture.window.presentation_settings().pointer.drag_width == 8.0,
            "a negative drag size is refused, changing nothing");
    HostEvent unfinite{12, 12, HostContextRequestEvent{ContextRequestOrigin::pointer,
                                                       {std::numeric_limits<double>::infinity(), 0.0}}};
    require(fixture.host.session().dispatch(std::move(unfinite)).error == HostDispatchError::invalid_payload,
            "a context request at no finite position is refused");
    HostEvent keyboard{13, 13, HostContextRequestEvent{ContextRequestOrigin::keyboard, {}}};
    require(fixture.host.session().dispatch(std::move(keyboard)).accepted(),
            "a keyboard context request is accepted");
    HostEvent hover{14, 14, HostHoverEvent{{10.0, 10.0}}};
    require(fixture.host.session().dispatch(std::move(hover)).accepted(), "a hover report is accepted");
    require(host_event_name(HostEventPayload{HostContextRequestEvent{}}) == std::string("context_request") &&
                host_event_name(HostEventPayload{HostHoverEvent{}}) == std::string("hover"),
            "the events are named context_request and hover");
}

void test_lifecycle_transition_guards() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;

    require(fixture.host.dispatch(HostActivationEvent{true}, 1).error ==
                HostDispatchError::invalid_lifecycle,
            "activation before attach must be rejected");
    require(fixture.host.session().snapshot().phase == HostLifecyclePhase::constructed,
            "rejected pre-attach input must not advance lifecycle state");

    require(fixture.host.dispatch(HostAttachEvent{{320.0, 180.0}, 1.0}, 2).accepted(),
            "first attach must enter the attached phase");
    require(fixture.host.session().snapshot().phase == HostLifecyclePhase::attached,
            "accepted attach must publish the attached phase");
    require(fixture.host.dispatch(HostAttachEvent{{320.0, 180.0}, 1.0}, 3).error ==
                HostDispatchError::invalid_lifecycle,
            "duplicate attach must be rejected");

    SubscriptionToken cancel =
        fixture.host.session().closing().subscribe(cancel_close);
    require(!fixture.host.dispatch(
                 HostCloseRequest{HostCloseReason::user, false}, 4).close_allowed &&
                fixture.host.session().snapshot().phase == HostLifecyclePhase::attached,
            "a cancelled close must return to the attached phase");
    cancel.disconnect();

    require(fixture.host.dispatch(
                HostCloseRequest{HostCloseReason::user, false}, 5).close_allowed &&
                fixture.host.session().snapshot().phase ==
                    HostLifecyclePhase::close_authorized,
            "an allowed close must enter close-authorized");
    PointerEvent input;
    input.action = PointerAction::move;
    input.position = {10.0, 10.0};
    require(fixture.host.dispatch(input, 6).error ==
                HostDispatchError::invalid_lifecycle,
            "input after close authorization must not reach retained controls");
    require(fixture.host.dispatch(HostActivationEvent{false}, 7).accepted() &&
                !fixture.host.session().snapshot().active,
            "native deactivation may complete during authorized close");
    require(fixture.host.dispatch(HostActivationEvent{true}, 8).error ==
                HostDispatchError::invalid_lifecycle,
            "authorized close must not permit a late activation to reopen state");
    require(fixture.host.dispatch(HostOcclusionEvent{true}, 9).accepted() &&
                fixture.host.session().snapshot().occluded,
            "native occlusion may complete during authorized close");
    require(fixture.host.dispatch(
                HostClosedEvent{HostCloseReason::user}, 10).accepted() &&
                fixture.host.session().snapshot().phase == HostLifecyclePhase::closed &&
                !fixture.host.session().snapshot().attached,
            "closed must detach the portable host session");
    require(fixture.host.dispatch(HostResizeEvent{{640.0, 360.0}}, 11).error ==
                HostDispatchError::invalid_lifecycle,
            "nonterminal events after closed must be rejected");
    require(fixture.host.dispatch(HostShutdownEvent{}, 12).accepted() &&
                fixture.host.session().snapshot().phase == HostLifecyclePhase::shutdown,
            "shutdown must be the terminal lifecycle phase");
}

void test_close_cancellation_and_closed_cleanup() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    static_cast<void>(fixture.host.dispatch(HostAttachEvent{{320.0, 180.0}, 1.0}, 1));
    SubscriptionToken cancellation =
        fixture.host.session().closing().subscribe(cancel_close);
    const HostDispatchResult cancelled = fixture.host.dispatch(
        HostCloseRequest{HostCloseReason::user, false}, 2);
    require(!cancelled.close_allowed,
            "host close request must expose synchronous cancellation");
    cancellation.disconnect();
    const HostDispatchResult allowed = fixture.host.dispatch(
        HostCloseRequest{HostCloseReason::user, false}, 3);
    require(allowed.close_allowed,
            "disconnecting close policy must restore default allow behavior");

    FrameRequestToken active = fixture.window.schedule_paint(
        fixture.probe, FrameTime{} + std::chrono::milliseconds(10));
    static_cast<void>(fixture.host.dispatch(HostClosedEvent{HostCloseReason::user}, 4));
    const HostSessionSnapshot snapshot = fixture.host.session().snapshot();
    require(snapshot.close_requests == 2 && snapshot.close_cancellations == 1 &&
                snapshot.closed && !active.connected(),
            "closed lifecycle must account cancellation and revoke scheduled work");
}

void test_headless_trace_is_byte_deterministic() {
    const std::string first = canonical_host_trace();
    const std::string second = canonical_host_trace();
    require(first == second, "host event replay trace must be byte-identical");
    require(first.find("event=attach accepted=1") != std::string::npos &&
                first.find("event=shutdown") != std::string::npos,
            "host trace must expose lifecycle endpoints");
    require(canonical_modal_trace() == canonical_modal_trace(),
            "nested modal trace must be byte-identical across fresh adapters");
}

void test_headless_host_observes_model_originated_paint_wakes() {
    const std::unique_ptr<Fixture> held_fixture = std::make_unique<Fixture>();
    Fixture& fixture = *held_fixture;
    require(fixture.host.consume_paint_wake(),
            "headless host must observe initial retained Window dirtiness");
    static_cast<void>(fixture.window.take_damage());
    (*fixture.probe).invalidate(Dirty::paint);
    require(fixture.host.paint_wake_pending() &&
                fixture.host.consume_paint_wake(),
            "host paint wake must not depend on dispatching input through that Window");
}

void test_normalized_host_input_reports_paint_lease_deferral() {
    std::shared_ptr<HostLeaseInputProbe> probe =
        make_control<HostLeaseInputProbe>(StableId("host.paint.input"));
    (*probe).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(probe, Size{100.0, 80.0});
    Window& window = *held_window;
    host::HeadlessHost host(window);
    require(host.dispatch(
                HostAttachEvent{{100.0, 80.0}, 1.0}, 1U).accepted(),
            "lease-input fixture must attach before normalized input");

    (*probe).inject = InjectPointerDuringPaint(host);
    HostLeasePainter painter;
    const DamageRegion damage = window.take_damage();
    window.paint(painter, damage.bounds());

    require((*probe).result.accepted() && (*probe).result.handled &&
                (*probe).result.input_deferred &&
                !(*probe).result.input_capacity_rejected &&
                (*probe).pointer_events == 0U && !(*probe).callback_during_paint &&
                host.dispatcher_wake_pending(),
            "normalized host input must report accepted deferral without entering paint");
    const DispatchDrainResult drain = host.pump_dispatcher();
    require(drain.invoked == 1U && drain.remaining == 0U &&
                (*probe).pointer_events == 1U && !(*probe).callback_during_paint &&
                host.trace().find("event=pointer accepted=1 handled=1 "
                                  "input_deferred=1 input_rejected=0") !=
                    std::string::npos,
            "headless host must deliver deferred input on one later dispatcher turn");
}

void test_drag_over_reuses_effect_and_compacts_while_painting() {
    std::shared_ptr<HostLeaseInputProbe> probe =
        make_control<HostLeaseInputProbe>(StableId("host.paint.drag"));
    (*probe).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(probe, Size{100.0, 80.0});
    Window& window = *held_window;
    host::HeadlessHost host(window);
    require(host.dispatch(
                HostAttachEvent{{100.0, 80.0}, 1.0}, 1U).accepted(),
            "lease-drag fixture must attach before normalized input");

    DragEvent initial;
    initial.action = DragAction::over;
    initial.session_id = 77U;
    initial.position = {20.0, 20.0};
    initial.allowed_effects = DragEffect::copy | DragEffect::move;
    initial.items.emplace_back(DragTextData{"lease-drag"});
    const HostDispatchResult negotiated =
        host.dispatch(std::move(initial), 2U);
    require(negotiated.handled &&
                negotiated.drag_effect == DragEffect::copy &&
                !negotiated.input_deferred,
            "an ordinary drag-over must establish the session's last valid effect");
    (*probe).drag_actions.clear();
    (*probe).invalidate(invalidation::paint_only);
    std::uint64_t sequence = 3U;
    bool every_deferred = true;
    (*probe).inject =
        InjectCompactedDragOvers(host, sequence, every_deferred);

    HostLeasePainter painter;
    const DamageRegion damage = window.take_damage();
    window.paint(painter, damage.bounds());
    const DeferredInputSnapshot pending = window.deferred_input_snapshot();
    require(every_deferred && (*probe).drag_actions.empty() &&
                !(*probe).callback_during_paint && pending.pending == 1U &&
                pending.coalesced_drag_overs == 31U &&
                pending.drain_queued,
            "paint-time drag-over must reuse the last effect and compact obsolete positions");
    const DispatchDrainResult drain = host.pump_dispatcher();
    require(drain.invoked == 1U && drain.remaining == 0U &&
                (*probe).drag_actions == std::vector<DragAction>{DragAction::over} &&
                !(*probe).callback_during_paint &&
                window.deferred_input_snapshot().pending == 0U,
            "one latest drag-over must deliver outside paint on the posted host turn");
    window.cancel_drag();
}

} // namespace

int main() {
    try {
        test_capabilities_and_normalized_dispatch();
        test_application_callback_fault_is_contained_at_host_boundary();
        test_semantic_sound_cues_are_bounded_and_deterministic();
        test_typed_dialog_requests_and_results();
        test_typed_drag_destination_routing_and_bounds();
        test_nested_modal_order_owner_suppression_and_limit();
        test_modal_observer_failure_and_shutdown_unwind();
        test_display_capture_and_occlusion_synchronization();
        test_environment_services_are_bounded_and_deterministic();
        test_sequence_geometry_and_shutdown_guards();
        test_input_settings_report_and_validation();
        test_lifecycle_transition_guards();
        test_close_cancellation_and_closed_cleanup();
        test_headless_trace_is_byte_deterministic();
        test_headless_host_observes_model_originated_paint_wakes();
        test_normalized_host_input_reports_paint_lease_deferral();
        test_drag_over_reuses_effect_and_compacts_while_painting();
        std::cout << "headless_capabilities="
                  << host::headless_capabilities().to_json() << '\n';
        std::cout << canonical_host_trace();
        std::cout << canonical_service_trace();
        std::cout << canonical_modal_trace();
        std::cout << "gui_forms_host_protocol_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_host_protocol_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
