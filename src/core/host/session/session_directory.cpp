#include "session_directory.hpp"

#include <cstddef>
#include <atomic>
#include <stxlib/threads.hpp>
#include <vector>

namespace gui_forms::detail {
namespace {

struct WindowSessionEntry final {
    const SurfaceRoot* window{};
    HostSession* session{};
};

struct InputGateEntry final {
    UserInputGate gate{};
    void* context{};
};

struct RouteObserverEntry final {
    RouteObserver observer{};
    void* context{};
};

struct ReadyHookEntry final {
    WindowReadyHook hook{};
    void* context{};
};

// One entry per bound window; a program has at most a few hundred windows
// (Application::maximum_windows, popups and sub-surfaces), so a linear
// search is the whole cost.
struct SessionDirectory final {
    stxlib::Mutex mutex{};
    std::vector<WindowSessionEntry> entries{};
    InputGateEntry gate{};
    ReadyHookEntry ready{};
    RouteObserverEntry route{};
};

// Whether an observer is installed: read for every routed event without the
// lock, so a window without the automation pays one relaxed load.
std::atomic<bool> route_observer_installed{false};

SessionDirectory& directory() noexcept {
    // Never destroyed (termination releases nothing): a late call at exit
    // never meets a destroyed lock.
    static SessionDirectory* const instance = new SessionDirectory();
    return *instance;
}

} // namespace

void note_window_session(const SurfaceRoot& window, HostSession& session) {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    for (WindowSessionEntry& entry : table.entries) {
        if (entry.window != &window) continue;
        entry.session = &session;
        return;
    }
    table.entries.push_back(WindowSessionEntry{&window, &session});
}

void forget_window_session(const HostSession& session) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    std::size_t index = 0U;
    while (index < table.entries.size()) {
        if (table.entries[index].session == &session) {
            table.entries[index] = table.entries.back();
            table.entries.pop_back();
            continue;
        }
        ++index;
    }
}

HostSession* window_session(const SurfaceRoot& window) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    for (const WindowSessionEntry& entry : table.entries) {
        if (entry.window == &window) return entry.session;
    }
    return nullptr;
}

void install_user_input_gate(UserInputGate gate, void* context) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    table.gate = InputGateEntry{gate, gate == nullptr ? nullptr : context};
}

void remove_user_input_gate(void* context) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    if (table.gate.gate != nullptr && table.gate.context == context) table.gate = InputGateEntry{};
}

bool user_input_held(const SurfaceRoot& window) noexcept {
    SessionDirectory& table = directory();
    InputGateEntry gate{};
    {
        const stxlib::ScopedLock lock(table.mutex);
        gate = table.gate;
    }
    // Called after the lock is released: the gate reads only its own state
    // (session_directory.hpp), on the thread that installed it.
    if (gate.gate == nullptr) return false;
    const bool held = gate.gate(gate.context, window);
    return held;
}

void install_window_ready_hook(WindowReadyHook hook, void* context) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    table.ready = ReadyHookEntry{hook, hook == nullptr ? nullptr : context};
}

void notify_window_ready(SurfaceRoot& window, std::string_view stable_id,
                         const ApplicationWindowHandle& handle) noexcept {
    SessionDirectory& table = directory();
    ReadyHookEntry ready{};
    {
        const stxlib::ScopedLock lock(table.mutex);
        ready = table.ready;
    }
    if (ready.hook == nullptr) return;
    ready.hook(ready.context, window, stable_id, handle);
}

void install_route_observer(RouteObserver observer, void* context) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    table.route = RouteObserverEntry{observer, observer == nullptr ? nullptr : context};
    route_observer_installed.store(observer != nullptr, std::memory_order_relaxed);
}

bool route_observed() noexcept {
    const bool observed = route_observer_installed.load(std::memory_order_relaxed);
    return observed;
}

void notify_route(const SurfaceRoot& window, const RouteRecord& record) noexcept {
    SessionDirectory& table = directory();
    RouteObserverEntry route{};
    {
        const stxlib::ScopedLock lock(table.mutex);
        route = table.route;
    }
    if (route.observer == nullptr) return;
    route.observer(route.context, window, record);
}

} // namespace gui_forms::detail
