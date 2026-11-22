#pragma once

// Which HostSession serves a window, and the gate that holds a user's input
// back while a model holds the interface (notes:planning/studies/
// MCP_AUTOMATION_STUDY.md, sections 8.2 and 8.3).
//
// The automation core injects a model's input through the window's own
// session (HostSession::dispatch_model_input), the same door the host's
// events use, so it must find that session from the window. A window holds
// no pointer to its session (the session observes the window, never the
// reverse), so this directory keeps the pairs instead: a session notes
// itself when it binds a window and forgets itself when it shuts down.
//
// Thread: every entry is read and written on the window's own UI thread, but
// a program may run windows on several UI threads (sub-surfaces have their
// own), so the table is guarded by one mutex. Nothing is called while it is
// held.
//
// The gate is one process-wide function and context, installed by the
// automation control while a model holds input (model.directed or
// suspend_input) and removed when the user has control again. Null: no
// user input is held. The session asks it for every user input event
// (pointer, key, text, drag, context request, hover, relative motion); the
// gate answers on the asking thread and may only read its own state.

#include <cstdint>

#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms {
class ApplicationWindowHandle;
class HostSession;
class SurfaceRoot;
} // namespace gui_forms

namespace gui_forms::detail {

// Notes that `session` serves `window`, replacing an older entry for the
// window. Called by the HostSession constructor.
void note_window_session(const SurfaceRoot& window, HostSession& session);
// Forgets `session`, wherever it is listed. Called as the session shuts
// down or is destroyed; a session not listed changes nothing.
void forget_window_session(const HostSession& session) noexcept;
// The session serving `window`, or null. A borrow for the current call on
// the window's UI thread: the session may shut down at the next callback.
[[nodiscard]] HostSession* window_session(const SurfaceRoot& window) noexcept;

// True when a user's input to `window` must be held back. Called on the
// window's UI thread for each user input event.
using UserInputGate = bool (*)(void* context, const SurfaceRoot& window) noexcept;

// Installs the gate (null removes it). The caller keeps `context` alive
// until it removes the gate; once this returns no call through the old gate
// is under way on this thread. UI thread of the automation's windows.
void install_user_input_gate(UserInputGate gate, void* context) noexcept;
[[nodiscard]] bool user_input_held(const SurfaceRoot& window) noexcept;

// The window-ready hook: the automation joins each application window as it
// becomes ready (study 4.1: every window of a program launched with --mcp
// is reachable), with its stable ID in the run and its handle (the
// presenter opens its popups owned by it). Called by
// attach_application_window on the window's UI thread, before the window's
// ready callback; the hook may not throw and returns promptly. One hook per
// process, installed by the automation's run (src/runtime/automation) for
// its lifetime; null removes it.
using WindowReadyHook = void (*)(void* context, SurfaceRoot& window, std::string_view stable_id,
                                 const ApplicationWindowHandle& handle) noexcept;
void install_window_ready_hook(WindowReadyHook hook, void* context) noexcept;
void notify_window_ready(SurfaceRoot& window, std::string_view stable_id,
                         const ApplicationWindowHandle& handle) noexcept;

// The route observer: the event-propagation trace that `inspect` shows a
// model (study 18, the inspection extensions). SurfaceRoot reports each
// pointer and key event its route delivered (dispatch_pointer after
// route_pointer, dispatch_key after route_key) while an observer is
// installed, which the automation core does for its lifetime (a program
// launched with --mcp or --mcp-script); otherwise the report is one load of
// a null pointer. Called on the window's UI thread; the observer copies what
// it keeps and returns promptly.
enum class RouteKind : std::uint8_t {
    pointer,
    key,
};
struct RouteRecord final {
    RouteKind kind{RouteKind::pointer};
    // PointerAction or KeyAction, as its number.
    std::uint8_t action{};
    // PointerButton's number, or the key's PhysicalKey.
    std::uint32_t code{};
    double x{};
    double y{};
    // The control the event was routed to (pointer: the hit or captured
    // control; key: the focused control), by its stable ID and anatomy ID;
    // empty when none.
    std::string target_stable_id{};
    std::string target_control_id{};
    // Whether a control on the route handled it.
    bool handled{};
};
using RouteObserver = void (*)(void* context, const SurfaceRoot& window, const RouteRecord& record) noexcept;
void install_route_observer(RouteObserver observer, void* context) noexcept;
[[nodiscard]] bool route_observed() noexcept;
void notify_route(const SurfaceRoot& window, const RouteRecord& record) noexcept;

} // namespace gui_forms::detail
