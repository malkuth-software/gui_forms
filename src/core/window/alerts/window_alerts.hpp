#pragma once

#include "../../surface_root/requests/alert_route.hpp"

#include <deque>
#include <memory>

namespace gui_forms {
class Window;
}

namespace gui_forms::detail {

// A window's alerts (design note section 15): shown one at a time, in the
// order raised, through the host's message dialog. Raising posts the next
// showing to the window's dispatcher, so raise_alert returns before the
// dialog's modal loop runs; an alert raised while one shows waits. Owned by
// its window (SurfaceRoot::alerts_); window thread only.
class WindowAlerts final : public AlertRoute {
public:
    explicit WindowAlerts(Window& window);
    ~WindowAlerts() override;
    WindowAlerts(const WindowAlerts&) = delete;
    WindowAlerts& operator=(const WindowAlerts&) = delete;

    void raise(AlertRecord record) override;
    // A waiting alert is answered revoked; the one showing is not affected.
    void withdraw(std::uint64_t id) override;
    void shutdown() noexcept override;

    // Shows the oldest waiting alert, if none shows (ShowNextAlert). The
    // dialog runs the host's modal loop, inside which the window may be
    // destroyed: nothing of this object is used after it returns unless
    // `liveness_` still names it.
    void show_next();

    // Names this route until it shuts down or goes; posted work holds it
    // weakly, and show_next holds it across the modal loop.
    struct Liveness final {
        WindowAlerts* alerts{};
    };

private:

    // Posts show_next when an alert waits and none shows or is posted.
    void schedule();
    void answer(const AlertRecord& record, SurfaceRequestResult result, HostDialogChoice choice);

    // Observes the window that owns this route.
    Window& window_;
    std::shared_ptr<Liveness> liveness_;
    std::deque<AlertRecord> waiting_{};
    bool scheduled_{};
    bool showing_{};
};

} // namespace gui_forms::detail
