#pragma once

#include "gui_forms/export/application_export.hpp"
#include <memory>

namespace gui_forms {

namespace detail {
struct LiveSurfaceState;
struct LiveSurfaceWake;
}

class LiveSurface;

// Revocable connection for a retained consumer's presentation wake. The wake
// may run on the producer thread and therefore must only signal/marshal work;
// it must never paint, mutate controls, or call application code directly.
class GUI_FORMS_API LiveSurfaceWakeConnection final {
public:
    LiveSurfaceWakeConnection() = default;
    ~LiveSurfaceWakeConnection();
    LiveSurfaceWakeConnection(LiveSurfaceWakeConnection&&) noexcept;
    LiveSurfaceWakeConnection& operator=(LiveSurfaceWakeConnection&&) noexcept;
    LiveSurfaceWakeConnection(const LiveSurfaceWakeConnection&) = delete;
    LiveSurfaceWakeConnection& operator=(const LiveSurfaceWakeConnection&) = delete;

    [[nodiscard]] bool connected() const noexcept;
    void disconnect() noexcept;

private:
    friend class LiveSurface;
    LiveSurfaceWakeConnection(
        std::weak_ptr<detail::LiveSurfaceState> state,
        std::shared_ptr<detail::LiveSurfaceWake> wake) noexcept;

    std::weak_ptr<detail::LiveSurfaceState> state_;
    std::shared_ptr<detail::LiveSurfaceWake> wake_;
};

} // namespace gui_forms
