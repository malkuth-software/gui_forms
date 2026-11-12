#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host/services/host_services.hpp"

#include <thread>

namespace gui_forms {

class Window;
struct PointerCaptureChange;

// Experimental 0.x host seam. It normalizes host events into the retained
// Window without admitting any platform type into the portable API.
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

    struct DispatchVisitor final {
        HostSession* session{};
        HostDispatchResult* result{};
        HostEvent* event{};

        template <typename Payload>
        void operator()(Payload& payload) const;
    };

    Window* window_{};
    HostServices* services_{};
    std::thread::id ui_thread_;
    HostSessionSnapshot snapshot_;
    Event<HostCloseRequest&> closing_;
    Event<const HostEvent&, const HostDispatchResult&> observed_;
    SubscriptionToken capture_observation_;
    SubscriptionToken modal_observation_;
};

} // namespace gui_forms
