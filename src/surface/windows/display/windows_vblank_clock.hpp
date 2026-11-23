#pragma once
// The display timing of the monitor a window is on, from that monitor's own
// vertical blanks (design note frame-source, section 3). DWM's composition
// timing describes the primary display only; on any other monitor a thread
// waits for each vertical blank of the DXGI output that drives it
// (IDXGIOutput::WaitForVBlank), stamps it on the frame clock, and measures
// the period and phase from the stamps (DisplayTimingEstimator), which the
// window reads before each frame poll. DXGI is loaded at run time
// (LoadLibraryExW and GetProcAddress, as dwmapi is), so the host's image
// imports no graphics library (cmake/check_windows_boundaries.cmake).
#include "gui_forms/scheduler/display_timing/display_timing.hpp"

#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

#include <stxlib/optional.hpp>

namespace gui_forms::host::windows_detail {

class WindowsVBlankClock final {
public:
    WindowsVBlankClock() = default;
    WindowsVBlankClock(const WindowsVBlankClock&) = delete;
    WindowsVBlankClock& operator=(const WindowsVBlankClock&) = delete;
    ~WindowsVBlankClock();

    // Loads DXGI and starts the waiting thread, at rest until wanted.
    // False, starting nothing, where DXGI cannot be loaded. UI thread.
    bool start();
    // Stops and joins the thread. Called before the window goes; safe
    // when never started.
    void stop() noexcept;
    // The monitor to time and whether timing is wanted now (frames are
    // paced to the display, and the monitor is not the one DWM times).
    // The thread waits for nothing while not wanted, and finds the output
    // again when the monitor changes. UI thread.
    void want(HMONITOR monitor, bool wanted) noexcept;
    // The timing measured for `monitor` (DisplayTimingSource::display: the
    // stamps are the display's own vertical blanks); nothing before the
    // stamps agree or for another monitor. UI thread.
    [[nodiscard]] stxlib::Optional<DisplayTiming> timing(HMONITOR monitor) const;

private:
    class Runner;
    class WantedNow;

    void run(const std::stop_token& stop);

    using CreateFactoryFunction = HRESULT(WINAPI*)(REFIID, void**);
    CreateFactoryFunction create_factory_{};
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    // Guarded by mutex_.
    HMONITOR monitor_{};
    bool wanted_{};
    HMONITOR measured_monitor_{};
    stxlib::Optional<DisplayTiming> measured_;
    std::jthread thread_;
};

} // namespace gui_forms::host::windows_detail
