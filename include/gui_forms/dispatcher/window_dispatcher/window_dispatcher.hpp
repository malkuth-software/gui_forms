#pragma once

#include "gui_forms/dispatcher/operation/dispatch_operation.hpp"
#include "gui_forms/dispatcher/types/dispatcher_types.hpp"

#include <functional>
#include <memory>

namespace gui_forms {

class SurfaceRoot;
namespace detail {
struct DispatcherState;
}

// A window's dispatcher, reachable from any thread for as long as the caller
// holds this handle, whatever happens to the window. Window::invoke and
// Window::begin_invoke need a live Window; a thread that may outlive the
// window (a client thread, a worker the window does not join) posts through
// this handle instead. The handle shares the dispatcher's state, never the
// window: once the window has closed (its host shut the dispatcher down, or
// its destructor began), the handle reports that as an explicit result and
// touches nothing of the window.
//
// The window is gone when its dispatcher has stopped accepting work or its
// teardown has begun. Work posted before that and still pending when it
// happens is cancelled, as Window::shutdown_dispatcher always does.
class WindowDispatcher final {
public:
    // An empty handle, bound to no window; posting through it throws
    // std::logic_error, as a programming error.
    WindowDispatcher() = default;

    // Posts `callback` to run on the window's UI thread. A window that is
    // gone returns an operation that is already cancelled (the explicit
    // result DispatchOperation carries) and releases the callback.
    [[nodiscard]] DispatchOperation begin_invoke(std::function<void()> callback) const;

    // Runs `callback` on the window's UI thread and waits for it: inline on
    // the UI thread, marshalled from any other through the host's wake. The
    // result is completed when it ran and window_gone when the window was
    // gone first or closed while the work waited; the callback did not run
    // then. An exception the callback throws is rethrown here, as
    // Window::invoke does. A live window with no running host (no wake
    // handler) throws std::logic_error, as Window::invoke does.
    [[nodiscard]] DispatchInvokeResult invoke(std::function<void()> callback) const;

    // Whether the window was still accepting work at the moment of the call;
    // an answer that may be stale by the time the caller acts on it.
    [[nodiscard]] bool window_alive() const noexcept;

private:
    friend class SurfaceRoot;
    explicit WindowDispatcher(std::shared_ptr<detail::DispatcherState> state) noexcept;

    std::shared_ptr<detail::DispatcherState> state_;
};

} // namespace gui_forms
