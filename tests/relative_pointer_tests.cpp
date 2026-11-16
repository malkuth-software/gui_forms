// Relative pointer mode, headless (cp34; gui_forms/events/relative_pointer).
// Every host sample is raised once, in order, never merged, with its
// timestamp and fractions; confined and locked modes take buttons and the
// wheel as samples (no mouse_down, no click); free mode routes on beside
// the samples; the shell's release regions suspend and resume the mode and
// the region's own control works while suspended; Escape, focus loss,
// deactivation, a focus scope, detaching, the until_release press and a
// second requester end it, each once; the capability report and the
// fallbacks of a window with no host. Also the pointer audit's fields:
// PointerEvent and MouseEventArgs timestamps and scroll phases.
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/events/relative_pointer/relative_pointer.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::abs(left - right) < 1e-9;
    return result;
}

// What the game control and the shell's capsule hear.
struct Recorder final {
    std::vector<RelativePointerEventArgs> samples{};
    std::vector<RelativePointerEnd> ends{};
    std::size_t mouse_downs{};
    std::size_t mouse_moves{};
    std::size_t key_downs{};
    std::size_t capsule_clicks{};
    MouseEventArgs last_mouse{};

    void sample(RelativePointerEventArgs& arguments) { samples.push_back(arguments); }
    void ended(const RelativePointerEnded& ended) { ends.push_back(ended.reason); }
    void mouse_down(const MouseEventArgs& arguments) {
        ++mouse_downs;
        last_mouse = arguments;
    }
    void mouse_move(const MouseEventArgs& arguments) {
        ++mouse_moves;
        last_mouse = arguments;
    }
    void mouse_wheel(const MouseEventArgs& arguments) { last_mouse = arguments; }
    void key_down(KeyEventArgs&) { ++key_downs; }
    void capsule_clicked(ButtonBase&) { ++capsule_clicks; }
};

// A 400 by 300 window: the shell's capsule (a button) at the top left,
// {0, 0, 80, 40}, and the game below it, {0, 40, 400, 260}, focusable.
struct Scene final {
    Scene() {
        root = make_control<Panel>(StableId("relative.root"));
        game = make_control<Control>(StableId("relative.game"));
        (*game).focusable = true;
        (*game).set_requested_bounds({0.0, 40.0, 400.0, 260.0});
        capsule = make_control<Button>(StableId("relative.capsule"), "Shell");
        (*capsule).set_requested_bounds({0.0, 0.0, 80.0, 40.0});
        other = make_control<Control>(StableId("relative.other"));
        (*other).focusable = true;
        (*other).set_requested_bounds({300.0, 0.0, 100.0, 40.0});
        (*root).add_child(capsule);
        (*root).add_child(game);
        (*root).add_child(other);
        window = std::make_unique<Window>(root, Size{400.0, 300.0});
        (*window).perform_layout();
        tokens.push_back((*game).relative_pointer().subscribe(
            Delegate<RelativePointerEventArgs&>::bind<Recorder, &Recorder::sample>(record)));
        tokens.push_back((*game).relative_pointer_ended().subscribe(
            Delegate<const RelativePointerEnded&>::bind<Recorder, &Recorder::ended>(record)));
        tokens.push_back((*game).mouse_down().subscribe(
            Delegate<const MouseEventArgs&>::bind<Recorder, &Recorder::mouse_down>(record)));
        tokens.push_back((*game).mouse_move().subscribe(
            Delegate<const MouseEventArgs&>::bind<Recorder, &Recorder::mouse_move>(record)));
        tokens.push_back((*game).mouse_wheel().subscribe(
            Delegate<const MouseEventArgs&>::bind<Recorder, &Recorder::mouse_wheel>(record)));
        tokens.push_back((*game).key_down().subscribe(
            Delegate<KeyEventArgs&>::bind<Recorder, &Recorder::key_down>(record)));
        tokens.push_back((*capsule).clicked().subscribe(
            Delegate<ButtonBase&>::bind<Recorder, &Recorder::capsule_clicked>(record)));
        const std::array<Rect, 1> regions{{{0.0, 0.0, 80.0, 40.0}}};
        require((*window).set_pointer_release_regions(regions), "the capsule's region is accepted");
    }

    Recorder record{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Control> game;
    std::shared_ptr<Button> capsule;
    std::shared_ptr<Control> other;
    std::unique_ptr<Window> window;
    std::vector<SubscriptionToken> tokens{};
};

// The scene with the reference host attached (every relative capability).
struct HostedScene final {
    HostedScene() : host(*scene.window) {
        require(host.dispatch(HostAttachEvent{{400.0, 300.0}, 1.0}, next()).accepted(), "attach accepted");
    }
    std::uint64_t next() {
        time = time + 1000U;
        return time;
    }
    HostDispatchResult motion(Point delta, std::uint64_t own_time = 0U) {
        HostRelativeMotionEvent event{};
        event.delta = delta;
        event.unaccelerated = true;
        event.timestamp_nanoseconds = own_time;
        const HostDispatchResult result = host.dispatch(event, next());
        return result;
    }
    HostDispatchResult motion_at(Point delta, Point position) {
        HostRelativeMotionEvent event{};
        event.delta = delta;
        event.position.set(position);
        const HostDispatchResult result = host.dispatch(event, next());
        return result;
    }
    HostDispatchResult pointer(PointerAction action, Point position, PointerButton button = PointerButton::none) {
        PointerEvent event{};
        event.action = action;
        event.position = position;
        event.button = button;
        event.pointer_id = 1;
        const HostDispatchResult result = host.dispatch(event, next());
        return result;
    }
    HostDispatchResult key(KeyAction action, std::uint32_t physical) {
        KeyEvent event{};
        event.action = action;
        event.physical_key = physical;
        const HostDispatchResult result = host.dispatch(event, next());
        return result;
    }
    const HostRelativePointerRequest& native() { return host.headless_services().relative_pointer_request(); }

    Scene scene{};
    host::HeadlessHost host;
    std::uint64_t time{1'000'000U};
};

RelativePointerRequest asking(RelativePointerMode mode) {
    RelativePointerRequest request{};
    request.mode = mode;
    request.raw_motion = true;
    return request;
}

// -- the capability report ----------------------------------------------------

void capability_report() {
    HostedScene hosted;
    const RelativePointerSupport support = (*hosted.scene.window).relative_pointer_support();
    require(support.relative_motion && support.raw_motion && support.lock && support.confine && support.warp,
            "the reference host reports every relative capability");
    const std::string json = hosted.host.services().snapshot().capabilities.to_json();
    require(json.find("\"relative_pointer\"") != std::string::npos &&
                json.find("\"raw_pointer_motion\"") != std::string::npos &&
                json.find("\"pointer_confinement\"") != std::string::npos &&
                json.find("\"pointer_warp\"") != std::string::npos,
            "the capability report names the four relative capabilities");
    require(HostCapabilities::current_protocol_version == 15U, "relative pointer mode is host protocol 15");

    // A window with no host derives deltas from positions and nothing else.
    Scene bare;
    const RelativePointerSupport none = (*bare.window).relative_pointer_support();
    require(!none.relative_motion && !none.raw_motion && !none.lock && !none.confine && !none.warp,
            "a window with no host reports no relative capability");
    const RelativePointerGrant grant = (*bare.game).request_relative_pointer(asking(RelativePointerMode::locked));
    require(grant.result == RelativePointerResult::degraded && grant.mode == RelativePointerMode::free &&
                !grant.host_relative && !grant.raw && !grant.cursor_hidden,
            "locked falls back to free with derived deltas, and says so");
    PointerEvent move{};
    move.action = PointerAction::move;
    move.position = {10.0, 50.0};
    static_cast<void>((*bare.window).dispatch_pointer(move));
    move.position = {13.25, 54.5};
    static_cast<void>((*bare.window).dispatch_pointer(move));
    require(bare.record.samples.size() == 1U && near(bare.record.samples[0].delta.x, 3.25) &&
                near(bare.record.samples[0].delta.y, 4.5),
            "a derived delta is the difference of two positions, fractions kept");
    require(bare.record.mouse_moves == 2U, "free mode routes the moves as usual");
    require(bare.record.samples[0].timestamp_nanoseconds != 0U, "a derived sample carries the move's stamp");
}

// -- delivery ------------------------------------------------------------------

void every_sample_in_order() {
    HostedScene hosted;
    const RelativePointerGrant grant =
        (*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::locked));
    require(grant.result == RelativePointerResult::granted && grant.mode == RelativePointerMode::locked &&
                grant.raw && grant.cursor_hidden && grant.host_relative,
            "locked is granted as asked on the reference host");
    require(hosted.native().active && hosted.native().mode == RelativePointerMode::locked &&
                hosted.native().hide_cursor && hosted.native().raw_motion,
            "the host is asked for the locked, hidden, raw pointer");
    const std::size_t count = 240U;
    std::vector<std::uint64_t> stamps{};
    stamps.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        // Tiny alternating deltas: a lock never reaches the capsule.
        const double sign = index % 2U == 0U ? 1.0 : -1.0;
        const Point delta{sign * 0.125 * static_cast<double>(index % 7U), -sign * 0.0625};
        require(hosted.motion(delta).handled, "each sample is taken");
        stamps.push_back(hosted.time);
    }
    require(hosted.scene.record.samples.size() == count, "every host sample is raised once: none merged");
    for (std::size_t index = 0; index < count; ++index) {
        const RelativePointerEventArgs& sample = hosted.scene.record.samples[index];
        const double sign = index % 2U == 0U ? 1.0 : -1.0;
        require(sample.kind == RelativePointerSampleKind::motion, "a motion sample");
        require(near(sample.delta.x, sign * 0.125 * static_cast<double>(index % 7U)) &&
                    near(sample.delta.y, -sign * 0.0625),
                "each delta arrives in host order with its fractions");
        require(sample.timestamp_nanoseconds == stamps[index], "an unstamped sample carries the host event's time");
        require(sample.unaccelerated && sample.mode == RelativePointerMode::locked, "raw, in locked mode");
    }
    require(hosted.motion({1.0, 0.0}, 777U).handled && hosted.scene.record.samples.back().timestamp_nanoseconds == 777U,
            "a sample's own timestamp is kept");
    require(hosted.host.session().snapshot().relative_motion_events == count + 1U,
            "the session counts every sample");
    const RelativePointerSnapshot snapshot = (*hosted.scene.window).relative_pointer_snapshot();
    require(snapshot.active && snapshot.samples == count + 1U && snapshot.grants == 1U, "the snapshot counts them");

    // Not finite: refused at the seam, raising nothing.
    HostRelativeMotionEvent bad{};
    bad.delta = {std::nan(""), 0.0};
    require(hosted.host.dispatch(bad, hosted.next()).error == HostDispatchError::invalid_payload,
            "a delta that is not finite is invalid_payload");
    require(hosted.scene.record.samples.size() == count + 1U, "and raises nothing");
}

void buttons_and_wheel_are_samples() {
    HostedScene hosted;
    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::confined)).accepted(),
            "confined is granted");
    static_cast<void>(hosted.motion({2.0, 0.0}));
    static_cast<void>(hosted.pointer(PointerAction::down, {200.0, 150.0}, PointerButton::primary));
    static_cast<void>(hosted.motion({0.0, 3.0}));
    static_cast<void>(hosted.pointer(PointerAction::up, {200.0, 150.0}, PointerButton::primary));
    PointerEvent wheel{};
    wheel.action = PointerAction::wheel;
    wheel.position = {200.0, 150.0};
    wheel.wheel_delta = {0.0, -1.5};
    wheel.wheel_unit = WheelDeltaUnit::pixels;
    static_cast<void>(hosted.host.dispatch(wheel, hosted.next()));
    const std::vector<RelativePointerEventArgs>& samples = hosted.scene.record.samples;
    require(samples.size() == 5U, "two motions, two buttons and a wheel: five samples");
    require(samples[0].kind == RelativePointerSampleKind::motion &&
                samples[1].kind == RelativePointerSampleKind::button_down &&
                samples[2].kind == RelativePointerSampleKind::motion &&
                samples[3].kind == RelativePointerSampleKind::button_up &&
                samples[4].kind == RelativePointerSampleKind::wheel,
            "the samples keep the host's order");
    require(samples[1].button == PointerButton::primary && near(samples[4].wheel_delta.y, -1.5) &&
                samples[4].wheel_unit == WheelDeltaUnit::pixels,
            "a button sample names its button, a wheel sample its delta and unit");
    require(near(samples[1].position.x, 200.0) && near(samples[1].position.y, 110.0),
            "a sample's position is in the owner's coordinates");
    require(hosted.scene.record.mouse_downs == 0U, "no mouse_down is routed in confined mode");
    require(!(*hosted.scene.game).has_pointer_capture(), "and no press took the capture");
}

void free_mode_routes_beside_samples() {
    HostedScene hosted;
    RelativePointerRequest request = asking(RelativePointerMode::free);
    const RelativePointerGrant grant = (*hosted.scene.game).request_relative_pointer(request);
    require(grant.result == RelativePointerResult::granted && grant.mode == RelativePointerMode::free &&
                !grant.cursor_hidden,
            "the visible-cursor variant: free, shown, raw deltas");
    require(hosted.native().active && hosted.native().mode == RelativePointerMode::free &&
                !hosted.native().hide_cursor,
            "the host keeps the pointer shown and free");
    static_cast<void>(hosted.pointer(PointerAction::move, {200.0, 150.0}));
    static_cast<void>(hosted.motion_at({1.5, 0.5}, {201.5, 150.5}));
    static_cast<void>(hosted.pointer(PointerAction::down, {201.5, 150.5}, PointerButton::primary));
    static_cast<void>(hosted.pointer(PointerAction::up, {201.5, 150.5}, PointerButton::primary));
    require(hosted.scene.record.samples.size() == 1U && near(hosted.scene.record.samples[0].delta.x, 1.5),
            "free mode raises the host's deltas only (no derived ones beside them)");
    require(hosted.scene.record.mouse_moves == 1U && hosted.scene.record.mouse_downs == 1U,
            "and routes the ordinary pointer as usual");
    require((*hosted.scene.game).has_relative_pointer(), "a click does not end free mode");
}

// -- the shell's release regions -------------------------------------------------

void locked_region_suspends_and_resumes() {
    HostedScene hosted;
    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::locked)).accepted(),
            "locked granted");
    // The virtual position starts at the game's centre, {200, 170}; each
    // step moves it by {-10, -10}. Step 14 reaches {60, 30}, in the capsule.
    for (int step = 1; step <= 14; ++step) static_cast<void>(hosted.motion({-10.0, -10.0}));
    require(hosted.scene.record.samples.size() == 13U, "the sample reaching the capsule is not raised");
    RelativePointerSnapshot snapshot = (*hosted.scene.window).relative_pointer_snapshot();
    require(snapshot.active && snapshot.suspended && snapshot.suspensions == 1U, "the mode is suspended");
    require(!hosted.native().active, "the host gives the ordinary pointer back");
    const std::optional<Point> warp = hosted.host.headless_services().last_pointer_warp();
    require(warp.has_value() && near((*warp).x, 60.0) && near((*warp).y, 30.0),
            "the pointer reappears where the virtual position reached the capsule");
    // The capsule is clickable as usual, and its press taking focus is
    // its use, not a focus loss.
    static_cast<void>(hosted.pointer(PointerAction::move, {40.0, 20.0}));
    static_cast<void>(hosted.pointer(PointerAction::down, {40.0, 20.0}, PointerButton::primary));
    static_cast<void>(hosted.pointer(PointerAction::up, {40.0, 20.0}, PointerButton::primary));
    require(hosted.scene.record.capsule_clicks == 1U, "the capsule is clicked while the mode is suspended");
    require((*hosted.scene.game).has_relative_pointer() && hosted.scene.record.ends.empty(),
            "the mode is suspended, not ended");
    // Back over the game: the mode resumes.
    static_cast<void>(hosted.pointer(PointerAction::move, {200.0, 150.0}));
    snapshot = (*hosted.scene.window).relative_pointer_snapshot();
    require(snapshot.active && !snapshot.suspended && snapshot.resumptions == 1U, "the mode resumes");
    require(hosted.native().active && hosted.native().mode == RelativePointerMode::locked,
            "the host takes the pointer again");
    static_cast<void>(hosted.motion({1.0, 1.0}));
    require(hosted.scene.record.samples.size() == 14U, "samples flow again");
}

void confined_region_suspends_and_resumes() {
    HostedScene hosted;
    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::confined)).accepted(),
            "confined granted");
    const Rect confine = hosted.native().confine;
    require(near(confine.x, 0.0) && near(confine.y, 0.0) && near(confine.width, 400.0) &&
                near(confine.height, 300.0),
            "the confine rectangle covers the game and the capsule, so the capsule stays reachable");
    static_cast<void>(hosted.motion_at({5.0, 0.0}, {300.0, 100.0}));
    static_cast<void>(hosted.motion_at({-260.0, -80.0}, {40.0, 20.0}));
    require(hosted.scene.record.samples.size() == 1U, "the motion onto the capsule is not a sample");
    require((*hosted.scene.window).relative_pointer_snapshot().suspended && !hosted.native().active,
            "a confined pointer over the capsule is the ordinary pointer");
    static_cast<void>(hosted.pointer(PointerAction::move, {50.0, 30.0}));
    require((*hosted.scene.window).relative_pointer_snapshot().suspended, "it stays suspended over the capsule");
    static_cast<void>(hosted.pointer(PointerAction::move, {200.0, 200.0}));
    require(!(*hosted.scene.window).relative_pointer_snapshot().suspended && hosted.native().active,
            "and resumes off it");

    const std::array<Rect, 9> too_many{};
    require(!(*hosted.scene.window).set_pointer_release_regions(too_many), "more than eight regions are refused");
    const std::array<Rect, 1> empty{{{0.0, 0.0, 0.0, 10.0}}};
    require(!(*hosted.scene.window).set_pointer_release_regions(empty), "an empty region is refused");
    require((*hosted.scene.window).pointer_release_regions().size() == 1U, "the regions stay as they were");
}

// -- the ends ----------------------------------------------------------------------

void escape_ends_and_is_taken() {
    HostedScene hosted;
    require((*hosted.scene.window).request_focus(hosted.scene.game), "the game takes focus");
    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::locked)).accepted(),
            "locked granted");
    require(hosted.key(KeyAction::down, PhysicalKey::escape).handled, "Escape is taken");
    static_cast<void>(hosted.key(KeyAction::up, PhysicalKey::escape));
    require(hosted.scene.record.ends.size() == 1U && hosted.scene.record.ends[0] == RelativePointerEnd::escape,
            "Escape ends the mode once");
    require(hosted.scene.record.key_downs == 0U, "and reaches no control");
    require(!hosted.native().active && !(*hosted.scene.game).has_relative_pointer(),
            "the host gives the pointer back");
    require(!hosted.motion({1.0, 1.0}).handled && hosted.scene.record.samples.empty(),
            "a sample after the end is not raised");
    static_cast<void>(hosted.key(KeyAction::down, PhysicalKey::escape));
    require(hosted.scene.record.key_downs == 1U, "a later Escape is an ordinary key");
}

void focus_loss_ends() {
    HostedScene hosted;
    require((*hosted.scene.window).request_focus(hosted.scene.game), "the game takes focus");
    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::confined)).accepted(),
            "confined granted");
    require((*hosted.scene.window).request_focus(hosted.scene.other), "focus moves to another control");
    require(hosted.scene.record.ends.size() == 1U && hosted.scene.record.ends[0] == RelativePointerEnd::focus_lost,
            "focus leaving the owner ends the mode");
}

void deactivation_ends() {
    HostedScene hosted;
    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::locked)).accepted(),
            "locked granted");
    require(hosted.host.dispatch(HostActivationEvent{false}, hosted.next()).accepted(), "deactivation accepted");
    require(hosted.scene.record.ends.size() == 1U &&
                hosted.scene.record.ends[0] == RelativePointerEnd::deactivated,
            "deactivation ends the mode");
    require(!hosted.native().active, "and gives the pointer back");
    const RelativePointerGrant refused =
        (*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::locked));
    require(refused.result == RelativePointerResult::not_active, "an inactive window refuses a request");
}

void focus_scope_ends() {
    HostedScene hosted;
    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::locked)).accepted(),
            "locked granted");
    const FocusScopeId scope = (*hosted.scene.window).begin_focus_scope(hosted.scene.other);
    require(static_cast<bool>(scope), "a focus scope (a dialog or menu) opens");
    require(hosted.scene.record.ends.size() == 1U && hosted.scene.record.ends[0] == RelativePointerEnd::modal,
            "a focus scope without the owner ends the mode");
    const RelativePointerGrant refused =
        (*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::confined));
    require(refused.result == RelativePointerResult::not_active, "and refuses a request while open");
}

void detaching_ends() {
    HostedScene hosted;
    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::confined)).accepted(),
            "confined granted");
    const Control::Ptr removed = (*hosted.scene.root).remove_child((*hosted.scene.game).runtime_id());
    require(removed == hosted.scene.game, "the game is removed");
    require(hosted.scene.record.ends.size() == 1U && hosted.scene.record.ends[0] == RelativePointerEnd::detached,
            "detaching the owner ends the mode");
    require(!hosted.native().active, "and gives the pointer back");
}

void until_release_and_supersede() {
    HostedScene hosted;
    RelativePointerRequest request = asking(RelativePointerMode::confined);
    request.until_release = true;
    require((*hosted.scene.game).request_relative_pointer(request).result == RelativePointerResult::no_press,
            "until_release without a held button is refused");
    static_cast<void>(hosted.pointer(PointerAction::down, {200.0, 150.0}, PointerButton::primary));
    require((*hosted.scene.game).request_relative_pointer(request).accepted(), "granted under the press");
    static_cast<void>(hosted.motion({1.0, 0.0}));
    static_cast<void>(hosted.pointer(PointerAction::up, {201.0, 150.0}, PointerButton::primary));
    require(hosted.scene.record.ends.size() == 1U &&
                hosted.scene.record.ends[0] == RelativePointerEnd::press_released,
            "the press's release ends the mode");
    require(hosted.scene.record.samples.size() == 2U &&
                hosted.scene.record.samples[1].kind == RelativePointerSampleKind::button_up,
            "the release is the mode's last sample");

    require((*hosted.scene.game).request_relative_pointer(asking(RelativePointerMode::free)).accepted(),
            "free granted to the game");
    Recorder other{};
    SubscriptionToken token = (*hosted.scene.other).relative_pointer_ended().subscribe(
        Delegate<const RelativePointerEnded&>::bind<Recorder, &Recorder::ended>(other));
    require((*hosted.scene.other).request_relative_pointer(asking(RelativePointerMode::free)).accepted(),
            "another control's request is granted");
    require(hosted.scene.record.ends.size() == 2U && hosted.scene.record.ends[1] == RelativePointerEnd::superseded,
            "the first owner's mode ends superseded");
    require((*hosted.scene.other).release_relative_pointer() && other.ends.size() == 1U &&
                other.ends[0] == RelativePointerEnd::released,
            "release ends the owner's own mode");
    require(!(*hosted.scene.other).release_relative_pointer(), "a second release finds none");
    require((*hosted.scene.window).relative_pointer_snapshot().ends == 3U, "the snapshot counts every end");
}

// -- the pointer audit's fields -------------------------------------------------------

void timestamps_and_scroll_phases() {
    HostedScene hosted;
    require((*hosted.scene.window).request_focus(hosted.scene.game), "the game takes focus (and the wheel)");
    PointerEvent wheel{};
    wheel.action = PointerAction::wheel;
    wheel.position = {200.0, 150.0};
    wheel.wheel_delta = {0.0, 2.25};
    wheel.wheel_unit = WheelDeltaUnit::pixels;
    wheel.wheel_phase = WheelPhase::changed;
    static_cast<void>(hosted.host.dispatch(wheel, 5'000'000U));
    const MouseEventArgs& arguments = hosted.scene.record.last_mouse;
    require(arguments.timestamp_nanoseconds == 5'000'000U, "an unstamped event carries the host event's time");
    require(arguments.wheel_phase == WheelPhase::changed && near(arguments.wheel_delta.y, 2.25) &&
                arguments.wheel_unit == WheelDeltaUnit::pixels,
            "MouseEventArgs carries the wheel phase and the precise delta");
    wheel.wheel_phase = WheelPhase::momentum;
    wheel.timestamp_nanoseconds = 4'000'000U;
    static_cast<void>(hosted.host.dispatch(wheel, 6'000'000U));
    require(hosted.scene.record.last_mouse.timestamp_nanoseconds == 4'000'000U &&
                hosted.scene.record.last_mouse.wheel_phase == WheelPhase::momentum,
            "a host's own stamp is kept, and momentum follows the fingers' end");
    PointerEvent move{};
    move.action = PointerAction::move;
    move.position = {210.0, 160.0};
    static_cast<void>((*hosted.scene.window).dispatch_pointer(move));
    require(hosted.scene.record.last_mouse.timestamp_nanoseconds != 0U,
            "an event handed to the window unstamped is stamped at dispatch");
    require(pointer_event_of(hosted.scene.record.last_mouse).timestamp_nanoseconds ==
                hosted.scene.record.last_mouse.timestamp_nanoseconds,
            "pointer_event_of keeps the timestamp");
}

} // namespace

int main() {
    capability_report();
    every_sample_in_order();
    buttons_and_wheel_are_samples();
    free_mode_routes_beside_samples();
    locked_region_suspends_and_resumes();
    confined_region_suspends_and_resumes();
    escape_ends_and_is_taken();
    focus_loss_ends();
    deactivation_ends();
    focus_scope_ends();
    detaching_ends();
    until_release_and_supersede();
    timestamps_and_scroll_phases();
    std::cout << "relative pointer tests passed\n";
    return 0;
}
