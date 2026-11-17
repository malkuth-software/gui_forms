#include "headless_host.hpp"
#include "gui_forms/detail/bound_member_function.hpp"

#include <sstream>
#include <variant>

namespace gui_forms::host {
HeadlessHost::HeadlessHost(Window& window)
    : window_(window.window_ref()), services_(),
      session_(window, headless_capabilities(), &services_) {
    window.set_dispatch_wake_handler(
        detail::BoundMemberFunction<void (HeadlessHost::*)() noexcept>(
            *this, &HeadlessHost::request_dispatcher_wake));
    window.set_paint_wake_handler(
        detail::BoundMemberFunction<void (HeadlessHost::*)() noexcept>(
            *this, &HeadlessHost::request_paint_wake));
    observation_ = session_.observed().subscribe(
        Delegate<const HostEvent&, const HostDispatchResult&>::bind<
            HeadlessHost, &HeadlessHost::observe_dispatch>(*this));
}

void HeadlessHost::request_dispatcher_wake() noexcept {
    dispatcher_wake_pending_.store(true, std::memory_order_release);
}

void HeadlessHost::request_paint_wake() noexcept {
    paint_wake_pending_.store(true, std::memory_order_release);
}

void HeadlessHost::observe_dispatch(
    const HostEvent& event, const HostDispatchResult& result) {
    const HostSessionSnapshot snapshot = session_.snapshot();
    const HostServicesSnapshot services = services_.snapshot();
    std::ostringstream line;
    line << "seq=" << event.sequence
         << " time_ns=" << event.timestamp_nanoseconds
         << " event=" << host_event_name(event.payload)
         << " accepted=" << (result.accepted() ? 1 : 0)
         << " handled=" << (result.handled ? 1 : 0)
         << " input_deferred=" << (result.input_deferred ? 1 : 0)
         << " input_rejected="
         << (result.input_capacity_rejected ? 1 : 0)
         << " drag_effect=" << drag_effect_name(result.drag_effect)
         << " close_allowed=" << (result.close_allowed ? 1 : 0)
         << " error=" << host_dispatch_error_name(result.error)
         << " phase=" << host_lifecycle_phase_name(snapshot.phase)
         << " attached=" << (snapshot.attached ? 1 : 0)
         << " active=" << (snapshot.active ? 1 : 0)
         << " occluded=" << (snapshot.occluded ? 1 : 0)
         << " display_changes=" << snapshot.display_changes
         << " monitors=" << snapshot.monitor_count
         << " captured=" << (services.pointer_captured ? 1 : 0)
         << " pointer_id=" << services.captured_pointer_id
         << " modal_depth=" << snapshot.modal_depth
         << " modal_suppressed=" << snapshot.modal_input_suppressions
         << " drag_events=" << snapshot.drag_events
         << " drag_drops=" << snapshot.drag_drops
         << " closed=" << (snapshot.closed ? 1 : 0)
         << " shutdown=" << (snapshot.shutdown ? 1 : 0);
    // Compositor reports (stage T5) add the state and the window's grant;
    // other lines are unchanged.
    const Window* const window = window_.get();
    if (window != nullptr && std::holds_alternative<HostCompositorEvent>(event.payload)) {
        const CompositorState& state = (*window).compositor_state();
        const WindowBackdropGrant& grant = (*window).backdrop_grant();
        line << " compositor_generation=" << state.generation.value
             << " compositing=" << compositor_presence_name(state.compositing)
             << " backdrop=" << window_backdrop_name(grant.granted)
             << " reason=" << window_backdrop_reason_name(grant.reason)
             << " native_backdrop=" << window_backdrop_name(services_.native_backdrop());
    }
    line << '\n';
    trace_ += line.str();
}

HeadlessHost::~HeadlessHost() {
    Window* const window = window_.get();
    if (window != nullptr) {
        (*window).set_paint_wake_handler({});
        (*window).set_dispatch_wake_handler({});
    }
}

DispatchDrainResult HeadlessHost::pump_dispatcher(
    std::size_t maximum_callbacks) {
    dispatcher_wake_pending_.store(false, std::memory_order_release);
    Window* const window = window_.get();
    // A destroyed window has no posted work left.
    if (window == nullptr) return DispatchDrainResult{};
    const DispatchDrainResult drained = (*window).drain_posted_work(maximum_callbacks);
    return drained;
}

HostDispatchResult HeadlessHost::dispatch(HostEventPayload payload,
                                          std::uint64_t timestamp_nanoseconds) {
    HostEvent event;
    event.sequence = next_sequence_++;
    event.timestamp_nanoseconds = timestamp_nanoseconds;
    event.payload = std::move(payload);
    return session_.dispatch(std::move(event));
}

} // namespace gui_forms::host
