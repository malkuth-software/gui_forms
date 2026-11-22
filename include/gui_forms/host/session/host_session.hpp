#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/types/platform_types/platform_types.hpp"

#include <cstdint>
#include <thread>

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
class GUI_FORMS_API HostSession final {
public:
    HostSession(Window& window,
                HostCapabilities capabilities,
                HostServices* services = nullptr);
    ~HostSession();
    HostSession(const HostSession&) = delete;
    HostSession& operator=(const HostSession&) = delete;

    [[nodiscard]] HostDispatchResult dispatch(HostEvent event);
    // A model's input (notes:planning/studies/MCP_AUTOMATION_STUDY.md,
    // section 8.1): the automation core builds a pointer, key, text, hover
    // or context request event as a host would and delivers it through this
    // same session, so the model's input and the user's go through one door
    // in the order they arrive. It takes no sequence number (the host's own
    // sequence stays the host's, so no later host event is refused as out
    // of order) and is never held by the user input gate that
    // model.directed and suspend_input install; a modal window over this
    // one suppresses it exactly as it suppresses the user's input. Any
    // other payload is refused as invalid_payload; the other refusals are
    // dispatch's (wrong_thread, after_shutdown, the lifecycle, invalid
    // UTF-8 text, an invalid pointer report). Counted in the snapshot's
    // model_input_events. UI thread.
    [[nodiscard]] HostDispatchResult dispatch_model_input(HostEventPayload payload,
                                                          std::uint64_t timestamp_nanoseconds);
    void shutdown() noexcept;

    [[nodiscard]] Event<HostCloseRequest&>& closing() noexcept {
        return closing_;
    }
    [[nodiscard]] Event<const HostEvent&, const HostDispatchResult&>&
    observed() noexcept {
        return observed_;
    }
    [[nodiscard]] HostSessionSnapshot snapshot() const;

private:
    // Where an event came from: the host adapter (the user) or the
    // automation core (a model).
    enum class InputOrigin : std::uint8_t { user, model };
    // The checks after the sequence and lifecycle (geometry, payloads,
    // modal and user input suppression), then delivery; shared by dispatch
    // and dispatch_model_input.
    HostDispatchResult admit(HostEvent& event, InputOrigin origin);
    // Delivers an accepted event; dispatch calls it through the fail-fast
    // boundary.
    void deliver(HostEvent& event, HostDispatchResult& result);
    void observe_pointer_capture(const PointerCaptureChange& change);
    void observe_modal_transition(const HostModalTransition& transition);
    // Ends the session after its window was destroyed: the snapshot is
    // marked shut down and the observations dropped; no window is touched.
    void end_without_window() noexcept;

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
