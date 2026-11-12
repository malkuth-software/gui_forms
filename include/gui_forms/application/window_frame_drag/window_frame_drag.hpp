#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/application/application.hpp"
#include "gui_forms/event.hpp"

#include <cstdint>

namespace gui_forms {

// How a window is moved by dragging its drawn caption (stage DK decision 12).
enum class WindowFrameDragMode : std::uint8_t {
    // The framework's own capture drag: every pointer move reads the
    // desktop pointer and requests the window's frame, so drop guides in
    // other windows can follow the pointer and Escape can put it back.
    framework_capture,
    // The platform's move loop, started from an authored drag region
    // (WindowChromeHitRole::drag_region): for a host without frames or a
    // desktop pointer, or one measured not to keep up.
    native_move_loop,
};

[[nodiscard]] GUI_FORMS_API const char* window_frame_drag_mode_name(WindowFrameDragMode mode) noexcept;

// What a drag achieved, for the per-host measurement (notes:results/).
// Timestamps are the caller's monotonic clock, in nanoseconds.
struct GUI_FORMS_API WindowFrameDragStatistics final {
    std::uint64_t pointer_moves{};
    std::uint64_t frame_requests{};
    std::uint64_t refused_requests{};
    // Frame reports of the dragged window received during the drag.
    std::uint64_t frame_reports{};
    std::uint64_t first_timestamp_nanoseconds{};
    std::uint64_t last_timestamp_nanoseconds{};

    [[nodiscard]] double duration_seconds() const noexcept;
    // Frame reports per second over the drag; 0 for a drag without
    // duration.
    [[nodiscard]] double achieved_rate_hz() const noexcept;
    // Pointer moves per second over the drag.
    [[nodiscard]] double pointer_rate_hz() const noexcept;
    // Whether the window kept up: it moved for at least 90% of the pointer
    // moves, or at `minimum_rate_hz` or more when the pointer moved faster
    // than that. A host that does not keep up uses the native move loop.
    [[nodiscard]] bool keeps_up(double minimum_rate_hz = 30.0) const noexcept;
};

// The framework's capture drag of one native window. The caption control
// that owns the drag calls begin on pointer down (with the pointer
// captured), move on every pointer move, end on pointer up and cancel on
// Escape or capture loss. Positions come from the desktop pointer, not from
// the event's client position, because the window moves under the pointer.
//
// Every member runs on the run's UI thread. The drag holds a weak handle and
// a subscription to the run's frame reports; both end with the drag.
class GUI_FORMS_API WindowFrameDrag final {
public:
    WindowFrameDrag() noexcept = default;
    ~WindowFrameDrag();
    WindowFrameDrag(const WindowFrameDrag&) = delete;
    WindowFrameDrag& operator=(const WindowFrameDrag&) = delete;

    // framework_capture when the host reports window_frames, screen_pointer
    // and pointer_capture; native_move_loop otherwise.
    [[nodiscard]] static WindowFrameDragMode mode_for(const HostCapabilities& capabilities) noexcept;

    // Starts dragging `window` from the pointer's current desktop position.
    // unsupported when mode_for gives native_move_loop (the caller then
    // starts the native loop); invalid_argument while a drag is active.
    [[nodiscard]] HostServiceStatus begin(const ApplicationWindowHandle& window,
                                          std::uint64_t timestamp_nanoseconds);
    // Requests the frame that keeps the pointer's grab offset. Moves that do
    // not change the target request nothing.
    [[nodiscard]] HostServiceStatus move(std::uint64_t timestamp_nanoseconds);
    // Ends the drag where the window is.
    void end(std::uint64_t timestamp_nanoseconds) noexcept;
    // Ends the drag and asks for the frame the window had when it began.
    [[nodiscard]] HostServiceStatus cancel(std::uint64_t timestamp_nanoseconds);

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const WindowFrameDragStatistics& statistics() const noexcept { return statistics_; }

private:
    struct FrameCounter final {
        WindowFrameDrag* drag{};
        void operator()(const HostWindowFrameEvent& event) const;
    };
    void stop(std::uint64_t timestamp_nanoseconds) noexcept;

    ApplicationWindowHandle window_{};
    HostWindowId window_id_{};
    bool active_{};
    Rect start_content_{};
    DesktopLogicalPoint start_pointer_{};
    Point last_target_{};
    WindowFrameDragStatistics statistics_{};
    SubscriptionToken reports_{};
};

} // namespace gui_forms
