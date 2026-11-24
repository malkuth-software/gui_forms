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
    // The model's pointer gesture (admit_model_pointer).
    bool model_gesture{};
    bool model_gesture_cancelled{};
    std::uint64_t model_events_dropped{};
    // The window runs a modal loop (ApplicationWindows::show_modal).
    bool shown_modally{};
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
    WindowSessionEntry made{};
    made.window = &window;
    made.session = &session;
    table.entries.push_back(made);
}

GestureVerdict admit_model_pointer(const SurfaceRoot& window, const bool button_down, const bool button_up,
                                   const bool button_held) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    for (WindowSessionEntry& entry : table.entries) {
        if (entry.window != &window) continue;
        if (button_down) {
            // A new press starts a new gesture, whatever ended the last.
            entry.model_gesture = true;
            entry.model_gesture_cancelled = false;
            return GestureVerdict::deliver;
        }
        if (entry.model_gesture_cancelled && (button_up || button_held)) {
            // The user took over: the rest of the gesture is dropped, and
            // the release ends it.
            ++entry.model_events_dropped;
            if (button_up) entry.model_gesture_cancelled = false;
            return GestureVerdict::drop;
        }
        if (button_up) entry.model_gesture = false;
        return GestureVerdict::deliver;
    }
    return GestureVerdict::deliver;
}

GestureVerdict admit_user_input(const SurfaceRoot& window, const bool idle_pointer, const bool press) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    for (WindowSessionEntry& entry : table.entries) {
        if (entry.window != &window) continue;
        if (!entry.model_gesture) return GestureVerdict::deliver;
        if (idle_pointer) return GestureVerdict::drop;
        if (!press) return GestureVerdict::deliver;
        entry.model_gesture = false;
        entry.model_gesture_cancelled = true;
        return GestureVerdict::cancel_model_gesture;
    }
    return GestureVerdict::deliver;
}

std::uint64_t model_events_dropped(const SurfaceRoot& window) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    for (const WindowSessionEntry& entry : table.entries) {
        if (entry.window == &window) return entry.model_events_dropped;
    }
    return 0U;
}

void note_shown_modally(const SurfaceRoot& window, const bool modal) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    for (WindowSessionEntry& entry : table.entries) {
        if (entry.window == &window) entry.shown_modally = modal;
    }
}

bool shown_modally(const SurfaceRoot& window) noexcept {
    SessionDirectory& table = directory();
    const stxlib::ScopedLock lock(table.mutex);
    for (const WindowSessionEntry& entry : table.entries) {
        if (entry.window == &window) return entry.shown_modally;
    }
    return false;
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

bool user_input_held(const SurfaceRoot& window, const bool press) noexcept {
    SessionDirectory& table = directory();
    InputGateEntry gate{};
    {
        const stxlib::ScopedLock lock(table.mutex);
        gate = table.gate;
    }
    // Called after the lock is released: the gate changes only its own state
    // (session_directory.hpp), on the thread that installed it.
    if (gate.gate == nullptr) return false;
    const bool held = gate.gate(gate.context, window, press);
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
