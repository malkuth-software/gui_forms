#include "windows_vblank_clock.hpp"

#include <dxgi.h>

#include <chrono>
#include <utility>

namespace gui_forms::host::windows_detail {

namespace {

// IID_IDXGIFactory1 (dxgi.h), spelled here so the host links no dxguid.
constexpr GUID dxgi_factory1_interface{0x770aae78U, 0xf26fU, 0x4dbaU,
                                       {0xa8U, 0x29U, 0x25U, 0x3cU, 0x83U, 0xd1U, 0xb3U, 0x87U}};

// Owns one COM reference.
template <typename Interface>
class ComReference final {
public:
    ComReference() = default;
    ComReference(const ComReference&) = delete;
    ComReference& operator=(const ComReference&) = delete;
    ~ComReference() { reset(); }
    void reset() noexcept {
        if (pointer != nullptr) (*pointer).Release();
        pointer = nullptr;
    }
    Interface* pointer{};
};

// The output of any adapter that drives `monitor`, with a reference the
// caller releases; null when none does.
[[nodiscard]] IDXGIOutput* output_for(IDXGIFactory1& factory, HMONITOR monitor) noexcept {
    for (UINT adapter_index = 0U;; ++adapter_index) {
        ComReference<IDXGIAdapter1> adapter;
        if (factory.EnumAdapters1(adapter_index, &adapter.pointer) != S_OK) return nullptr;
        for (UINT output_index = 0U;; ++output_index) {
            IDXGIOutput* output = nullptr;
            if ((*adapter.pointer).EnumOutputs(output_index, &output) != S_OK) break;
            DXGI_OUTPUT_DESC description{};
            if ((*output).GetDesc(&description) == S_OK && description.Monitor == monitor) return output;
            (*output).Release();
        }
    }
}

// How long the thread rests after a wait that failed or returned at once
// (a monitor asleep, an output gone), before it looks again.
constexpr stxlib::Duration failed_wait_rest = stxlib::Duration::milliseconds(50);
// Waits in a row that return at once before the thread rests.
constexpr unsigned int immediate_return_limit = 3U;

} // namespace

void WindowsVBlankClock::thread_main(const stxlib::StopSignal& stop, void* const context) noexcept {
    WindowsVBlankClock& clock = *static_cast<WindowsVBlankClock*>(context);
    clock.run(stop);
}

bool WindowsVBlankClock::wake_now(const void* const context) noexcept {
    const WindowsVBlankClock& clock = *static_cast<const WindowsVBlankClock*>(context);
    const bool wake = clock.stopping_ || (clock.wanted_ && clock.monitor_ != nullptr);
    return wake;
}

WindowsVBlankClock::~WindowsVBlankClock() { stop(); }

bool WindowsVBlankClock::start() {
    if (thread_.identity().present()) return true;
    const HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dxgi == nullptr) return false;
    create_factory_ = reinterpret_cast<CreateFactoryFunction>(
        reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
    if (create_factory_ == nullptr) return false;
    {
        const stxlib::ScopedLock lock(mutex_);
        stopping_ = false;
    }
    stxlib::OperationResult<stxlib::Thread> started = stxlib::Thread::start(&WindowsVBlankClock::thread_main, this);
    if (started.failed()) return false;
    thread_ = static_cast<stxlib::OperationResult<stxlib::Thread>&&>(started).take_value();
    return true;
}

void WindowsVBlankClock::stop() noexcept {
    if (!thread_.identity().present()) return;
    {
        const stxlib::ScopedLock lock(mutex_);
        stopping_ = true;
    }
    thread_.request_stop();
    changed_.notify_all();
    static_cast<void>(thread_.join());
    thread_ = stxlib::Thread{};
}

void WindowsVBlankClock::want(HMONITOR monitor, bool wanted) noexcept {
    bool changed = false;
    {
        const stxlib::ScopedLock lock(mutex_);
        changed = monitor_ != monitor || wanted_ != wanted;
        monitor_ = monitor;
        wanted_ = wanted;
    }
    if (changed) changed_.notify_all();
}

stxlib::Optional<DisplayTiming> WindowsVBlankClock::timing(HMONITOR monitor) const {
    const stxlib::ScopedLock lock(mutex_);
    stxlib::Optional<DisplayTiming> result;
    if (monitor == nullptr || measured_monitor_ != monitor) return result;
    result = measured_;
    return result;
}

void WindowsVBlankClock::run(const stxlib::StopSignal& stop) {
    ComReference<IDXGIFactory1> factory;
    if ((*create_factory_)(dxgi_factory1_interface, reinterpret_cast<void**>(&factory.pointer)) != S_OK ||
        factory.pointer == nullptr) {
        return;
    }
    ComReference<IDXGIOutput> output;
    HMONITOR timed{};
    DisplayTimingEstimator estimator;
    unsigned int immediate_returns = 0U;
    while (!stop.requested()) {
        HMONITOR monitor{};
        {
            stxlib::ScopedLock lock(mutex_);
            changed_.wait(lock, &WindowsVBlankClock::wake_now, this);
            if (stopping_) return;
            monitor = monitor_;
        }
        if (monitor != timed || output.pointer == nullptr) {
            // Another monitor: its own output and stamps, and no timing
            // until they agree.
            output.reset();
            estimator.reset();
            timed = monitor;
            {
                const stxlib::ScopedLock lock(mutex_);
                measured_monitor_ = monitor;
                measured_.reset();
            }
            // The adapters change with the displays: a fresh factory sees
            // the current ones.
            if ((*factory.pointer).IsCurrent() == FALSE) {
                factory.reset();
                if ((*create_factory_)(dxgi_factory1_interface, reinterpret_cast<void**>(&factory.pointer)) != S_OK ||
                    factory.pointer == nullptr) {
                    return;
                }
            }
            output.pointer = output_for(*factory.pointer, monitor);
            if (output.pointer == nullptr) {
                stxlib::sleep_for(failed_wait_rest);
                continue;
            }
        }
        const FrameTime before = FrameClock::now();
        if ((*output.pointer).WaitForVBlank() != S_OK) {
            output.reset();
            stxlib::sleep_for(failed_wait_rest);
            continue;
        }
        const FrameTime blank = FrameClock::now();
        if (blank - before < minimum_refresh_period / 4) {
            // Returned at once: a blank that fell just after the call, or,
            // several times running, an output asleep or not scanning out.
            immediate_returns = immediate_returns + 1U;
            if (immediate_returns >= immediate_return_limit) {
                immediate_returns = 0U;
                stxlib::sleep_for(failed_wait_rest);
            }
            continue;
        }
        immediate_returns = 0U;
        const stxlib::Optional<DisplayTiming> measured = estimator.take(blank);
        if (measured.absent()) continue;
        DisplayTiming timing = *measured.value();
        timing.source = DisplayTimingSource::display;
        const stxlib::ScopedLock lock(mutex_);
        if (measured_monitor_ == timed) measured_.set(timing);
    }
}

} // namespace gui_forms::host::windows_detail
