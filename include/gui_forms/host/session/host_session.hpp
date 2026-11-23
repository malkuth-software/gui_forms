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
class GUI_FORMS_API HostSession final {
public:
    HostSession(Window& window,
                HostCapabilities capabilities,
                HostServices* services = nullptr);
    ~HostSession();
    HostSession(const HostSession&) = delete;
    HostSession& operator=(const HostSession&) = delete;

    [[nodiscard]] HostDispatchResult dispatch(HostEvent event);
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
