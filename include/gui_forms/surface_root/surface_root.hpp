#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/dispatcher.hpp"
#include "gui_forms/metrics.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <thread>

namespace gui_forms {

class Window;
class BackgroundWorker;
namespace detail {
struct DispatcherState;
}

// The root of one retained control tree and the thread that owns it (the
// threaded sub-surfaces design, stage S1): the thread identity every
// mutation is checked against, the dispatcher queue that thread drains, and
// the counters its work reports into. A Window is a SurfaceRoot with a
// native host; a sub-surface (stage S2) is one whose output is a
// LiveSurface. The root is bound to the thread that constructs it and never
// changes thread.
//
// Stage S1 moves the control-root part of Window here piece by piece, with
// no change of behaviour; until it is done, the control table, the dirty
// structure, layout, paint and focus remain Window's.
class SurfaceRoot {
public:
    SurfaceRoot(const SurfaceRoot&) = delete;
    SurfaceRoot& operator=(const SurfaceRoot&) = delete;

    // Whether the calling thread is the root's own.
    [[nodiscard]] bool check_access() const noexcept;
    // std::logic_error naming `operation` off the root's thread.
    void verify_access(std::string_view operation = "window access");
    [[nodiscard]] bool invoke_required() const noexcept { return !check_access(); }

    // Posts `callback` to run on the root's thread at its next dispatcher
    // turn (drain_posted_work). The owned overload cancels the work when
    // `owner` is disposed or has left this root before it runs; a null
    // owner is std::invalid_argument.
    [[nodiscard]] DispatchOperation begin_invoke(std::function<void()> callback);
    [[nodiscard]] DispatchOperation begin_invoke(
        const Control::Ptr& owner, std::function<void()> callback);
    // Synchronous Invoke never starts a nested message pump. Calls on the
    // root's thread run inline; other threads' calls require an installed
    // wake handler (set_dispatch_wake_handler) and wait for the turn.
    void invoke(std::function<void()> callback);
    void invoke(const Control::Ptr& owner, std::function<void()> callback);
    // A handle to this root's dispatcher that any thread may keep and post
    // through after the root has closed (WindowDispatcher).
    [[nodiscard]] WindowDispatcher dispatcher() const;
    // Runs at most `maximum_callbacks` posted callbacks, in posting order.
    // Root thread only.
    [[nodiscard]] DispatchDrainResult drain_posted_work(
        std::size_t maximum_callbacks = maximum_callbacks_per_dispatch_turn);
    [[nodiscard]] DispatcherSnapshot dispatcher_snapshot() const noexcept;
    // The thread-safe wake a host installs. Installing a handler after work
    // was queued immediately publishes one coalesced wake. The handler runs
    // under the dispatcher's lock, on whichever thread posted: it may only
    // signal (set a flag, post a message, write a pipe) and must never call
    // back into the root. In return, once this call replaces or removes a
    // handler, no wake through the old one is under way or to come, so its
    // target may be destroyed. A thread may post (begin_invoke, invoke)
    // while holding locks of its own, so the lock order is the poster's
    // locks, then the dispatcher's, then any the handler takes: the handler
    // must take no lock that a posting thread may hold. The stock hosts'
    // handlers take none of GUI.Forms' or the application's.
    void set_dispatch_wake_handler(std::function<void()> wake);

    [[nodiscard]] MetricsSnapshot metrics_snapshot() const { return metrics_.snapshot(); }
    Metrics& metrics() noexcept { return metrics_; }
    void reset_activity_metrics() noexcept { metrics_.reset_activity(); }

protected:
    // Binds the root to the calling thread, with an accepting dispatcher.
    SurfaceRoot();
    // Not virtual: a root is destroyed as what it is, never through this
    // base.
    ~SurfaceRoot();

private:
    friend class Window;
    friend class Control;
    friend class BackgroundWorker;

    // std::logic_error naming `operation`, counted as a wrong-thread
    // rejection, off the root's thread.
    void require_root_thread(std::string_view operation);
    // The first step of the owner's teardown: stops host wakes, so that a
    // post reaching the dispatcher after shutdown is refused by name instead
    // of throwing (DispatcherState::closing).
    void begin_dispatcher_teardown() noexcept;
    // Stops accepting posted work and cancels everything pending; the
    // part of the owner's shutdown_dispatcher that concerns the queue.
    void close_dispatcher() noexcept;

    std::thread::id root_thread_;
    std::shared_ptr<detail::DispatcherState> dispatcher_state_;
    Metrics metrics_;
};

} // namespace gui_forms
