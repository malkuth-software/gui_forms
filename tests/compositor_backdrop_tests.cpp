// Stage T5 phase C0: the portable compositor model, headless
// (notes:planning/stages/T5-compositor/PLAN.md §2 and §5 C0). Grant resolution
// against every state, the window's request, grant and backdrop_changed
// order, the host's compositor reports through the session, and byte-stable
// headless traces of a declared and an undeclared window through a
// compositor stopping and starting and high contrast on and off.
#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"
#include "application/headless_application.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// The state of a host that composites and offers every backdrop.
CompositorState full_state(std::uint64_t generation) {
    CompositorState state{};
    state.generation.value = generation;
    state.compositing = CompositorPresence::present;
    state.support = {WindowBackdropSupport::supported, WindowBackdropSupport::supported,
                     WindowBackdropSupport::supported, WindowBackdropSupport::supported};
    return state;
}

// An X11-like state: transparency under a compositor, no materials.
CompositorState x11_state(std::uint64_t generation, bool compositing) {
    CompositorState state{};
    state.generation.value = generation;
    state.compositing = compositing ? CompositorPresence::present : CompositorPresence::absent;
    state.support = {WindowBackdropSupport::supported,
                     compositing ? WindowBackdropSupport::supported : WindowBackdropSupport::no_compositor,
                     WindowBackdropSupport::host_unsupported, WindowBackdropSupport::host_unsupported};
    return state;
}

// Records each backdrop_changed emission.
struct GrantRecorder final {
    std::vector<WindowBackdropGrant> grants{};
    void record(const WindowBackdropGrant& grant) { grants.push_back(grant); }
};

// A handler that, on its first call, asks its window for `next`.
struct ReentrantRequester final {
    Window* window{};
    WindowBackdropRequest next{};
    std::vector<WindowBackdropGrant> seen{};
    bool requested{};
    void record(const WindowBackdropGrant& grant) {
        seen.push_back(grant);
        if (requested) return;
        requested = true;
        static_cast<void>((*window).request_backdrop(next));
    }
};

struct Fixture final {
    Fixture()
        : root(make_control<Control>(StableId("root"))), window(root, {120.0, 90.0}) {}
    std::shared_ptr<Control> root;
    Window window;
};

// The host attached to a fixture window; declared before the session binds.
struct HostedFixture final {
    explicit HostedFixture(bool declared)
        : root(make_control<Control>(StableId("root"))), window(root, {120.0, 90.0}),
          declared_now(declared ? window.declare_non_opaque() : false), host(window) {
        require(host.dispatch(HostAttachEvent{{120.0, 90.0}, 1.0}, 1).accepted(), "attach refused");
    }
    std::shared_ptr<Control> root;
    Window window;
    bool declared_now{};
    host::HeadlessHost host;
};

void test_request_validation() {
    require(window_backdrop_request_valid(WindowBackdropRequest{}), "the default request (opaque) is valid");
    require(window_backdrop_request_valid(WindowBackdropRequest::preferring(WindowBackdrop::window_material)),
            "a single material is valid");
    require(window_backdrop_request_valid(WindowBackdropRequest::with_fallbacks(
                WindowBackdrop::window_material, WindowBackdrop::transparent, WindowBackdrop::opaque)),
            "a three-entry chain ending at opaque is valid");
    require(!window_backdrop_request_valid(
                WindowBackdropRequest::with_fallback(WindowBackdrop::opaque, WindowBackdrop::transparent)),
            "opaque before the end is refused");
    require(!window_backdrop_request_valid(
                WindowBackdropRequest::with_fallback(WindowBackdrop::transparent, WindowBackdrop::transparent)),
            "a repeated entry is refused");
    WindowBackdropRequest empty{};
    empty.length = 0U;
    require(!window_backdrop_request_valid(empty), "an empty request is refused");
    WindowBackdropRequest long_request{};
    long_request.length = 4U;
    require(!window_backdrop_request_valid(long_request), "a request past three entries is refused");
    WindowBackdropRequest unknown = WindowBackdropRequest::preferring(static_cast<WindowBackdrop>(9));
    require(!window_backdrop_request_valid(unknown), "a backdrop outside the enumeration is refused");
}

void test_refusal_order() {
    const WindowBackdropConditions declared{true, false, false};
    const CompositorState full = full_state(1U);
    require(window_backdrop_refusal(WindowBackdrop::opaque, CompositorState{}, WindowBackdropConditions{}) ==
                WindowBackdropReason::granted,
            "opaque is always granted");
    require(window_backdrop_refusal(WindowBackdrop::transparent, full, WindowBackdropConditions{}) ==
                WindowBackdropReason::not_declared,
            "an undeclared window is refused first");
    require(window_backdrop_refusal(WindowBackdrop::window_material, CompositorState{}, declared) ==
                WindowBackdropReason::host_unsupported,
            "the default state supports only opaque");
    CompositorState old_build = full;
    old_build.support[2] = WindowBackdropSupport::os_version;
    require(window_backdrop_refusal(WindowBackdrop::window_material, old_build, declared) ==
                WindowBackdropReason::os_version,
            "an OS build without the material says os_version");
    CompositorState absent = full;
    absent.compositing = CompositorPresence::absent;
    require(window_backdrop_refusal(WindowBackdrop::transparent, absent, declared) ==
                WindowBackdropReason::no_compositor,
            "no compositor refuses even a supported backdrop");
    require(window_backdrop_refusal(WindowBackdrop::transparent, x11_state(1U, false), declared) ==
                WindowBackdropReason::no_compositor,
            "the host's no_compositor support value is reported");
    CompositorState everything_off = absent;
    everything_off.transparency_enabled = false;
    require(window_backdrop_refusal(WindowBackdrop::transparent, everything_off,
                                    WindowBackdropConditions{true, true, true}) ==
                WindowBackdropReason::high_contrast,
            "high contrast wins over every host refusal");
    require(window_backdrop_refusal(WindowBackdrop::transparent, everything_off,
                                    WindowBackdropConditions{true, false, true}) ==
                WindowBackdropReason::no_compositor,
            "no compositor is checked before reduced transparency");
    require(window_backdrop_refusal(WindowBackdrop::window_material, old_build,
                                    WindowBackdropConditions{true, true, false}) ==
                WindowBackdropReason::high_contrast,
            "high contrast is checked before the OS build");
    require(window_backdrop_refusal(WindowBackdrop::window_material, CompositorState{},
                                    WindowBackdropConditions{true, true, false}) ==
                WindowBackdropReason::high_contrast,
            "high contrast is checked before the host's support");
    require(window_backdrop_refusal(WindowBackdrop::transparent, absent, WindowBackdropConditions{false, true, true}) ==
                WindowBackdropReason::not_declared,
            "an undeclared window is refused before high contrast");
    require(window_backdrop_refusal(WindowBackdrop::opaque, absent, WindowBackdropConditions{true, true, true}) ==
                WindowBackdropReason::granted,
            "opaque is granted under high contrast");
    require(window_backdrop_refusal(WindowBackdrop::transparent, full, WindowBackdropConditions{true, true, true}) ==
                WindowBackdropReason::high_contrast,
            "high contrast is checked before reduced transparency");
    require(window_backdrop_refusal(WindowBackdrop::transparent, full, WindowBackdropConditions{true, false, true}) ==
                WindowBackdropReason::reduced_transparency,
            "reduced transparency refuses");
    CompositorState effects_off = full;
    effects_off.transparency_enabled = false;
    require(window_backdrop_refusal(WindowBackdrop::transient_material, effects_off, declared) ==
                WindowBackdropReason::transparency_disabled,
            "transparency effects off refuses");
    require(window_backdrop_refusal(WindowBackdrop::transient_material, full, declared) ==
                WindowBackdropReason::granted,
            "a supported material is granted");
}

void test_resolution_chain() {
    const WindowBackdropConditions declared{true, false, false};
    const WindowBackdropRequest chain = WindowBackdropRequest::with_fallback(WindowBackdrop::window_material,
                                                                             WindowBackdrop::transparent);
    const WindowBackdropGrant on_x11 = resolve_window_backdrop(chain, x11_state(4U, true), declared);
    require(on_x11.requested == WindowBackdrop::window_material && on_x11.granted == WindowBackdrop::transparent &&
                on_x11.reason == WindowBackdropReason::host_unsupported && on_x11.generation.value == 4U,
            "the chain falls to transparent with the preferred entry's reason and the state's generation");
    const WindowBackdropGrant without = resolve_window_backdrop(chain, x11_state(5U, false), declared);
    require(without.granted == WindowBackdrop::opaque && without.reason == WindowBackdropReason::host_unsupported,
            "with no compositor the chain ends at opaque");
    const WindowBackdropGrant single = resolve_window_backdrop(
        WindowBackdropRequest::preferring(WindowBackdrop::window_material), x11_state(6U, true), declared);
    require(single.granted == WindowBackdrop::opaque,
            "transparent is a fallback only when listed (T5-Q5)");
    const WindowBackdropGrant full = resolve_window_backdrop(chain, full_state(7U), declared);
    require(full.granted == WindowBackdrop::window_material && full.reason == WindowBackdropReason::granted,
            "the preferred entry is granted when it can be");
    // Every backdrop against every support value, declared, with nothing
    // else refusing: granted exactly when supported.
    for (std::size_t backdrop_index = 1U; backdrop_index < window_backdrop_count; ++backdrop_index) {
        for (std::size_t support_index = 0U; support_index < 4U; ++support_index) {
            CompositorState state = full_state(1U);
            state.support[backdrop_index] = static_cast<WindowBackdropSupport>(support_index);
            const WindowBackdrop backdrop = static_cast<WindowBackdrop>(backdrop_index);
            const WindowBackdropGrant grant =
                resolve_window_backdrop(WindowBackdropRequest::preferring(backdrop), state, declared);
            const bool supported = support_index == 0U;
            require((grant.granted == backdrop) == supported, "a backdrop is granted exactly when supported");
        }
    }
}

void test_state_validation_and_json() {
    require(compositor_state_valid(CompositorState{}), "the default state is valid");
    CompositorState broken{};
    broken.support[0] = WindowBackdropSupport::host_unsupported;
    require(!compositor_state_valid(broken), "a state without opaque is invalid");
    const std::string json = full_state(3U).to_json();
    require(json == "{\"generation\":3,\"compositing\":\"present\",\"support\":{\"opaque\":\"supported\","
                    "\"transparent\":\"supported\",\"window_material\":\"supported\","
                    "\"transient_material\":\"supported\"},\"transparency_enabled\":true,"
                    "\"reduced_transparency\":false,\"high_contrast\":false,\"dark_scheme\":false}",
            "the state's JSON names every field");
    const HostCapabilities capabilities = host::headless_capabilities();
    require(capabilities.protocol_version >= 9U && host_protocol_revisions.size() >= 2U &&
                host_protocol_revisions[1].version == 9U,
            "protocol version 9 carries the compositor report");
    const std::string capability_json = capabilities.to_json();
    require(capability_json.find("\"transparent_windows\",\"window_backdrops\",\"compositor_notifications\","
                                 "\"presentation_preferences\"") != std::string::npos,
            "the headless host reports the four compositor capabilities by name");
}

void test_window_without_host() {
    const std::unique_ptr<Fixture> held = std::make_unique<Fixture>();
    Fixture& fixture = *held;
    GrantRecorder recorder{};
    const SubscriptionToken token = fixture.window.backdrop_changed().subscribe(
        Delegate<const WindowBackdropGrant&>::bind<GrantRecorder, &GrantRecorder::record>(recorder));
    const WindowBackdropGrant first =
        fixture.window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent));
    require(first.granted == WindowBackdrop::opaque && first.reason == WindowBackdropReason::not_declared,
            "an undeclared window stays opaque");
    require(recorder.grants.size() == 1U, "the new request raised one event");
    require(fixture.window.declare_non_opaque(), "a window without a host may be declared");
    require(fixture.window.backdrop_grant().reason == WindowBackdropReason::host_unsupported,
            "declaring resolves again: the default state supports only opaque");
    fixture.window.set_compositor_state(full_state(1U), false);
    require(fixture.window.backdrop_grant().granted == WindowBackdrop::transparent,
            "a window without host services is granted what the state allows");
    fixture.window.set_compositor_state(full_state(2U), false);
    require(recorder.grants.size() == 3U && fixture.window.backdrop_grant().generation.value == 2U,
            "a new generation alone raises nothing");
    PresentationSettings reduced = fixture.window.presentation_settings();
    reduced.reduced_transparency = true;
    fixture.window.set_presentation_settings(reduced);
    require(fixture.window.backdrop_grant().granted == WindowBackdrop::opaque &&
                fixture.window.backdrop_grant().reason == WindowBackdropReason::reduced_transparency,
            "the application's reduced transparency withdraws the backdrop");
    bool threw = false;
    try {
        static_cast<void>(fixture.window.request_backdrop(
            WindowBackdropRequest::with_fallback(WindowBackdrop::opaque, WindowBackdrop::transparent)));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && fixture.window.backdrop_request().preferred() == WindowBackdrop::transparent,
            "an invalid request throws and changes nothing");
}

void test_reentrant_request() {
    const std::unique_ptr<Fixture> held = std::make_unique<Fixture>();
    Fixture& fixture = *held;
    require(fixture.window.declare_non_opaque(), "declaration refused");
    fixture.window.set_compositor_state(full_state(1U), false);
    ReentrantRequester requester{&fixture.window,
                                 WindowBackdropRequest::preferring(WindowBackdrop::transient_material)};
    GrantRecorder later{};
    const SubscriptionToken first = fixture.window.backdrop_changed().subscribe(
        Delegate<const WindowBackdropGrant&>::bind<ReentrantRequester, &ReentrantRequester::record>(requester));
    const SubscriptionToken second = fixture.window.backdrop_changed().subscribe(
        Delegate<const WindowBackdropGrant&>::bind<GrantRecorder, &GrantRecorder::record>(later));
    static_cast<void>(fixture.window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::window_material)));
    require(requester.seen.size() == 2U && requester.seen[0].granted == WindowBackdrop::window_material &&
                requester.seen[1].granted == WindowBackdrop::transient_material,
            "the nested request raises its own event within the first");
    require(later.grants.size() == 2U && later.grants[0].granted == WindowBackdrop::transient_material &&
                later.grants[1].granted == WindowBackdrop::window_material,
            "a later handler hears the nested change, then its own emission's grant");
    require(fixture.window.backdrop_grant().granted == WindowBackdrop::transient_material,
            "the nested request stands");
}

void test_session_reports_and_refusals() {
    const std::unique_ptr<HostedFixture> held = std::make_unique<HostedFixture>(true);
    HostedFixture& fixture = *held;
    require(fixture.declared_now && !fixture.window.declare_non_opaque(),
            "declaration is fixed once a host session is bound");
    HostCompositorEvent no_opaque{full_state(1U)};
    no_opaque.state.support[0] = WindowBackdropSupport::host_unsupported;
    require(fixture.host.dispatch(no_opaque, 2).error == HostDispatchError::invalid_payload,
            "a report without opaque is refused");
    require(fixture.host.dispatch(HostCompositorEvent{full_state(0U)}, 3).error ==
                HostDispatchError::invalid_payload,
            "generation 0 is not later than the initial state");
    require(fixture.host.dispatch(HostCompositorEvent{full_state(2U)}, 4).accepted(), "a valid report refused");
    require(fixture.host.dispatch(HostCompositorEvent{full_state(2U)}, 5).error ==
                HostDispatchError::invalid_payload,
            "a report that is not later is refused");
    require(fixture.window.compositor_state().generation.value == 2U, "the refused reports changed the state");

    host::HeadlessHostServices& services = fixture.host.headless_services();
    services.fail_next_backdrop();
    const WindowBackdropGrant failed =
        fixture.window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::window_material));
    require(failed.granted == WindowBackdrop::opaque && failed.reason == WindowBackdropReason::backend_failure &&
                services.native_backdrop() == WindowBackdrop::opaque,
            "a failed application leaves the window opaque and says backend_failure");
    const WindowBackdropGrant applied =
        fixture.window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transient_material));
    require(applied.granted == WindowBackdrop::transient_material &&
                services.native_backdrop() == WindowBackdrop::transient_material,
            "the next request is applied natively");
    require(services.backdrop_trace() == "backdrop=window_material accepted=0\n"
                                         "backdrop=transient_material accepted=1\n",
            "only changed backdrops reach the native window");

    const std::unique_ptr<HostedFixture> shut_held = std::make_unique<HostedFixture>(false);
    require((*shut_held).host.dispatch(HostShutdownEvent{}, 2).accepted(), "shutdown refused");
    require((*shut_held).host.dispatch(HostCompositorEvent{full_state(1U)}, 3).error ==
                HostDispatchError::after_shutdown,
            "a report after shutdown is refused");
}

// The canonical trace of one window through a compositor stopping and
// starting, and through the host's high contrast on and off.
std::string run_canonical_trace(bool declared) {
    const std::unique_ptr<HostedFixture> held = std::make_unique<HostedFixture>(declared);
    HostedFixture& fixture = *held;
    GrantRecorder recorder{};
    const SubscriptionToken token = fixture.window.backdrop_changed().subscribe(
        Delegate<const WindowBackdropGrant&>::bind<GrantRecorder, &GrantRecorder::record>(recorder));
    require(fixture.host.dispatch(HostCompositorEvent{x11_state(1U, true)}, 10).accepted(), "report 1 refused");
    static_cast<void>(fixture.window.request_backdrop(
        WindowBackdropRequest::preferring(WindowBackdrop::transparent)));
    require(fixture.host.dispatch(HostCompositorEvent{x11_state(2U, false)}, 20).accepted(), "report 2 refused");
    require(fixture.host.dispatch(HostCompositorEvent{x11_state(3U, true)}, 30).accepted(), "report 3 refused");
    CompositorState contrast = x11_state(4U, true);
    contrast.high_contrast = true;
    require(fixture.host.dispatch(HostCompositorEvent{contrast}, 40).accepted(), "report 4 refused");
    require(fixture.window.presentation_settings().high_contrast,
            "the host's high contrast reaches the presentation settings");
    require(fixture.host.dispatch(HostCompositorEvent{x11_state(5U, true)}, 50).accepted(), "report 5 refused");
    require(!fixture.window.presentation_settings().high_contrast, "the host's high contrast off reaches them too");
    std::string trace = fixture.host.trace();
    for (const WindowBackdropGrant& grant : recorder.grants) {
        trace += "backdrop_changed requested=";
        trace += window_backdrop_name(grant.requested);
        trace += " granted=";
        trace += window_backdrop_name(grant.granted);
        trace += " reason=";
        trace += window_backdrop_reason_name(grant.reason);
        trace += " generation=" + std::to_string(grant.generation.value) + "\n";
    }
    return trace;
}

void test_canonical_traces() {
    const std::string declared = run_canonical_trace(true);
    require(declared == run_canonical_trace(true), "the declared window's trace is byte-stable");
    const std::string undeclared = run_canonical_trace(false);
    require(undeclared == run_canonical_trace(false), "the undeclared window's trace is byte-stable");
    const char* const declared_changes =
        "backdrop_changed requested=transparent granted=transparent reason=granted generation=1\n"
        "backdrop_changed requested=transparent granted=opaque reason=no_compositor generation=2\n"
        "backdrop_changed requested=transparent granted=transparent reason=granted generation=3\n"
        "backdrop_changed requested=transparent granted=opaque reason=high_contrast generation=4\n"
        "backdrop_changed requested=transparent granted=transparent reason=granted generation=5\n";
    require(declared.find(declared_changes) != std::string::npos,
            "the declared window follows the compositor and high contrast");
    require(declared.find("event=compositor accepted=1") != std::string::npos &&
                declared.find("compositor_generation=2 compositing=absent backdrop=opaque reason=no_compositor "
                              "native_backdrop=opaque") != std::string::npos,
            "the trace records the stopped compositor's grant and the native backdrop");
    const char* const undeclared_changes =
        "backdrop_changed requested=transparent granted=opaque reason=not_declared generation=1\n";
    require(undeclared.size() > 0U && undeclared.find(undeclared_changes) != std::string::npos &&
                undeclared.find("granted=transparent") == std::string::npos,
            "the undeclared window stays opaque through every change");
    std::cout << declared << undeclared;
}

struct ReadyGrant final {
    std::vector<std::string>* opened{};
    void operator()(Window& window, ApplicationWindowHandle) const {
        static_cast<void>(window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::window_material)));
        (*opened).push_back(window_backdrop_name(window.backdrop_grant().granted));
    }
};

ApplicationWindow make_entry(const char* id, const char* owner, bool non_opaque, std::vector<std::string>* opened) {
    ApplicationWindow entry{};
    entry.stable_id = id;
    entry.owner_id = owner;
    const std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
    entry.model = std::make_unique<Window>(root, Size{200.0, 160.0});
    entry.options.title = id;
    entry.options.initial_size = {200.0, 160.0};
    entry.options.minimum_size = {100.0, 80.0};
    entry.options.non_opaque = non_opaque;
    entry.options.ready = ReadyGrant{opened};
    return entry;
}

void test_headless_application() {
    std::vector<std::string> opened{};
    std::vector<ApplicationWindow> entries{};
    entries.push_back(make_entry("main", "", true, &opened));
    entries.push_back(make_entry("plain", "main", false, &opened));
    const std::unique_ptr<host::HeadlessApplication> application =
        std::make_unique<host::HeadlessApplication>(std::move(entries));
    host::HeadlessApplication& desk = *application;
    const HostWindowId main_id = desk.id("main");
    const HostWindowId plain_id = desk.id("plain");
    require((*desk.model(main_id)).non_opaque_declared() && !(*desk.model(plain_id)).non_opaque_declared(),
            "ApplicationWindowOptions::non_opaque declares the window");
    desk.simulate_compositor_state(full_state(0U));
    require((*desk.model(main_id)).backdrop_grant().granted == WindowBackdrop::window_material &&
                (*desk.model(plain_id)).backdrop_grant().reason == WindowBackdropReason::not_declared,
            "every open window receives the report");
    require(desk.compositor_state().generation.value == 1U, "the simulated generation starts at 1");
    ApplicationWindow later = make_entry("later", "main", true, &opened);
    require(desk.windows().open_window(std::move(later)).status.accepted(), "runtime open refused");
    const HostWindowId later_id = desk.id("later");
    require((*desk.model(later_id)).compositor_state().generation.value == 1U &&
                (*desk.model(later_id)).backdrop_grant().granted == WindowBackdrop::window_material,
            "a window opened later receives the current state after attach");
    require(opened.size() == 3U && opened[0] == "opaque" && opened[1] == "opaque" &&
                opened[2] == "window_material",
            "ready callbacks see the state reported so far: none for the first windows, the report for the later one");
    desk.simulate_compositor_state(x11_state(0U, false));
    require((*desk.model(main_id)).backdrop_grant().reason == WindowBackdropReason::host_unsupported &&
                (*desk.model(later_id)).backdrop_grant().granted == WindowBackdrop::opaque,
            "a later report reaches every window");
    require(desk.windowing().trace().find("compositor generation=2 compositing=absent") != std::string::npos,
            "the windowing trace records the report");
}

} // namespace

int main() {
    try {
        test_request_validation();
        test_refusal_order();
        test_resolution_chain();
        test_state_validation_and_json();
        test_window_without_host();
        test_reentrant_request();
        test_session_reports_and_refusals();
        test_canonical_traces();
        test_headless_application();
        std::cout << "gui_forms_compositor_backdrop_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_compositor_backdrop_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
