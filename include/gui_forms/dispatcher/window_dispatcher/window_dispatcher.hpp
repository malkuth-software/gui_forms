#pragma once

#include "gui_forms/export/application_export.hpp"
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
// ic: gui_forms::WindowDispatcher: generated from
// ic: include/gui_forms/dispatcher/window_dispatcher/window_dispatcher.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. States: unbound (initial), accepting, gone. Observed by state(): unbound
// ic: WindowDispatcherState::unbound, accepting WindowDispatcherState::accepting, gone WindowDispatcherState::gone.
// ic: Thread: any, unless a call says otherwise.
class GUI_FORMS_API WindowDispatcher final {
public:
    // An empty handle, bound to no window; posting through it throws
    // std::logic_error, as a programming error.
    WindowDispatcher() = default;

    // Posts `callback` to run on the window's UI thread. A window that is
    // gone returns an operation that is already cancelled (the explicit
    // result DispatchOperation carries) and releases the callback.
    // ic: Admitted in accepting, gone. In unbound: failure logic_error, unchanged. Requires bound != 0, else failure
    // ic: logic_error, unchanged. Fails cancelled (the window is gone: the operation returned is already cancelled and
    // ic: the callback released), unchanged. Returns DispatchOperation, owned by the caller. Transfers the callback
    // ic: from caller to the window's dispatcher.
    [[nodiscard]] DispatchOperation begin_invoke(std::function<void()> callback) const;

    // Runs `callback` on the window's UI thread and waits for it: inline on
    // the UI thread, marshalled from any other through the host's wake. The
    // result is completed when it ran and window_gone when the window was
    // gone first or closed while the work waited; the callback did not run
    // then. An exception the callback throws is rethrown here, as
    // Window::invoke does. A live window with no running host (no wake
    // handler) throws std::logic_error, as Window::invoke does.
    // ic: Admitted in accepting, gone. In unbound: failure logic_error, unchanged. Requires bound != 0, else failure
    // ic: logic_error, unchanged. Fails invalid_argument (an empty callback (a std::function the clause grammar cannot
    // ic: name, so its check stays in code)), unchanged; window_gone (the window was gone first or closed while the
    // ic: work waited (DispatchInvokeResult::window_gone); the callback did not run), unchanged; logic_error (a live
    // ic: window with no running host (no wake handler)), unchanged.
    [[nodiscard]] DispatchInvokeResult invoke(std::function<void()> callback) const;

    // Whether the window was still accepting work at the moment of the call;
    // an answer that may be stale by the time the caller acts on it.
    [[nodiscard]] bool window_alive() const noexcept;

    // The handle's state: unbound, accepting or gone (WindowDispatcherState).
    // accepting may be stale by the time the caller acts on it, as
    // window_alive's answer may; unbound and gone never change.
    [[nodiscard]] WindowDispatcherState state() const noexcept;

private:
    friend class SurfaceRoot;
    explicit WindowDispatcher(std::shared_ptr<detail::DispatcherState> state) noexcept;

    std::shared_ptr<detail::DispatcherState> state_;
};

} // namespace gui_forms
