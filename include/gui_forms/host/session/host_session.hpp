#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/types/platform_types/platform_types.hpp"


namespace gui_forms {

class Window;
struct PointerCaptureChange;

// Experimental 0.x host seam. It normalizes host events into the retained
// Window without admitting any platform type into the portable API.
//
// The session observes its window and does not own it (house style 4: the
// fallback when an owner disappears). It keeps a WindowRef, so a window
// destroyed before its session is seen as gone, never read: the session
// then ends without it. dispatch refuses every later event with
// after_shutdown, and shutdown (and so the destructor) does none of the
// window's teardown, which the window's own destructor has done.
// ic: gui_forms::HostSession: generated from include/gui_forms/host/session/host_session.ic.toml by
// ic: tools/ic_declarations.py; edit the declaration, not these lines. States: constructed (initial), attached,
// ic: close_authorized, closed, shutdown. Observed by snapshot(): constructed phase == HostLifecyclePhase::constructed,
// ic: attached phase == HostLifecyclePhase::attached, close_authorized phase == HostLifecyclePhase::close_authorized,
// ic: closed phase == HostLifecyclePhase::closed, shutdown phase == HostLifecyclePhase::shutdown. Thread: ui, unless a
// ic: call says otherwise.
class GUI_FORMS_API HostSession final {
public:
    // ic: Returns a borrow of window, valid until the window is destroyed: the session then ends without it.
    HostSession(Window& window,
                HostCapabilities capabilities,
                HostServices* services = nullptr);
    ~HostSession();
    HostSession(const HostSession&) = delete;
    HostSession& operator=(const HostSession&) = delete;

    // ic: Admitted in constructed, attached, close_authorized, closed. In shutdown: returns
    // ic: HostDispatchError::after_shutdown, unchanged. Requires sequence != 0, else returns
    // ic: HostDispatchError::sequence_zero, unchanged. Requires sequence > last_sequence, else returns
    // ic: HostDispatchError::non_monotonic_sequence, unchanged. Requires (phase != phase_constructed or attach != 0 or
    // ic: shutdown != 0) and (phase != phase_attached or attach == 0) and (phase != phase_close_authorized or closed !=
    // ic: 0 or shutdown != 0 or passive_terminal != 0) and (phase != phase_closed or shutdown != 0), else returns
    // ic: HostDispatchError::invalid_lifecycle, unchanged. Requires finite(width) and finite(height) and width > 0 and
    // ic: height > 0, else returns HostDispatchError::invalid_geometry, unchanged. Requires finite(scale) and scale >
    // ic: 0, else returns HostDispatchError::invalid_geometry, unchanged. When several refusals apply:
    // ic: dispatch_sequence, then dispatch_order, then the state refusal, then dispatch_lifecycle, then dispatch_size,
    // ic: then dispatch_scale. Fails wrong_thread (off the thread that constructed the session
    // ic: (HostDispatchError::wrong_thread)), unchanged; invalid_payload (a payload its own rules refuse: invalid UTF-8
    // ic: text, input settings out of range, a stale compositor report), unchanged.
    [[nodiscard]] HostDispatchResult dispatch(HostEvent event);
    // ic: Admitted in any state; then shutdown.
    void shutdown() noexcept;

    [[nodiscard]] Event<HostCloseRequest&>& closing() noexcept {
        return closing_;
    }
    [[nodiscard]] Event<const HostEvent&, const HostDispatchResult&>&
    observed() noexcept {
        return observed_;
    }
    // ic: Returns HostSessionSnapshot, owned by the caller.
    [[nodiscard]] HostSessionSnapshot snapshot() const;

private:
    // Delivers an accepted event; dispatch calls it through the fail-fast
    // boundary.
    void deliver(HostEvent& event, HostDispatchResult& result);
    void observe_pointer_capture(const PointerCaptureChange& change);
    void observe_modal_transition(const HostModalTransition& transition);
    // Ends the session after its window was destroyed: the snapshot is
    // marked shut down and the observations dropped; no window is touched.
    void end_without_window() noexcept;
    // Writes the snapshot's phase and, while the window is alive, the
    // window's (Window::host_lifecycle_phase), so the two never disagree.
    void set_phase(HostLifecyclePhase phase) noexcept;

    struct DispatchVisitor final {
        HostSession* session{};
        // The session's window, alive for this delivery (deliver checks it).
        Window* window{};
        HostDispatchResult* result{};
        HostEvent* event{};

        template <typename Payload>
        void operator()(Payload& payload) const;
    };

    // The window observed, not owned: get() is null once it is destroyed.
    WindowRef window_;
    HostServices* services_{};
    ThreadId ui_thread_;
    HostSessionSnapshot snapshot_;
    Event<HostCloseRequest&> closing_;
    Event<const HostEvent&, const HostDispatchResult&> observed_;
    SubscriptionToken capture_observation_;
    SubscriptionToken modal_observation_;
};

} // namespace gui_forms
