// GF_TABLE_WINDOWS: one window at a time through GUIForms::Application. See
// gf_abi.h.

#include "abi_tables.hpp"

#include "../engine/abi_engine.hpp"
#include "../registry/abi_registry.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/application.hpp"
#include "gui_forms/window.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity run_slot{GF_TABLE_WINDOWS, 0U};
constexpr SlotIdentity request_close_slot{GF_TABLE_WINDOWS, 1U};
constexpr SlotIdentity create_slot{GF_TABLE_WINDOWS, 2U};

// The 1.0 size of gf_run_options: a caller passes at least this much.
constexpr std::uint32_t run_options_size_1_0 = 64U;
static_assert(sizeof(gf_run_options) == run_options_size_1_0);

constexpr gf_run_flags known_run_flags = GF_RUN_FLAGS_NONE;

// The run's three callbacks, owned for the length of run. Destroying them
// (on the UI thread, when run returns) calls each release once.
struct RunCallbacks {
    gf_object root = 0U;
    std::unique_ptr<CallbackBinding> ready;
    std::unique_ptr<CallbackBinding> closing;
    std::unique_ptr<CallbackBinding> closed;
};

// Calls callback, if any, with the root as sender. Returns the event's
// cancel field.
std::uint32_t call_run_callback(const RunCallbacks& callbacks, const CallbackBinding* callback,
                                std::string_view name) noexcept {
    if (callback == nullptr) {
        return 0U;
    }
    gf_event event{};
    event.struct_size = sizeof(gf_event);
    event.sender = callbacks.root;
    event.name.data = name.data();
    event.name.size = name.size();
    (*callback).invoke(event);
    return event.cancel;
}

// The host's callbacks, as named targets with the run's callbacks as their
// context. Each runs on the UI thread during Application::run.
struct RunReady {
    RunCallbacks* callbacks = nullptr;
    void operator()(Window& model, ApplicationWindowHandle window) const {
        registry().set_run_window(window);
        engine().attach_window(model);
        static_cast<void>(call_run_callback(*callbacks, (*callbacks).ready.get(), "ready"));
    }
};

struct RunWakeReady {
    void operator()(std::function<void()> wake) const {
        registry().set_run_wake(std::move(wake));
    }
};

struct RunDispatchPending {
    void operator()() const {
        static_cast<void>(registry().run_posted(maximum_callbacks_per_dispatch_turn));
    }
};

struct RunClosing {
    RunCallbacks* callbacks = nullptr;
    void operator()(HostCloseRequest& request) const {
        const std::uint32_t cancel = call_run_callback(*callbacks, (*callbacks).closing.get(), "closing");
        if (cancel != 0U) {
            request.cancel = true;
        }
    }
};

struct RunClosed {
    RunCallbacks* callbacks = nullptr;
    void operator()() const {
        // The wake stops before the window goes (ApplicationWindowOptions).
        registry().set_run_wake(std::function<void()>{});
        engine().detach_window();
        static_cast<void>(call_run_callback(*callbacks, (*callbacks).closed.get(), "closed"));
    }
};

// Copies one optional callback of the options into binding.
gf_status read_optional_callback(const gf_callback* callback, std::unique_ptr<CallbackBinding>& binding) {
    if (callback == nullptr) {
        return GF_STATUS_OK;
    }
    gf_callback copy{};
    const gf_status read = read_callback(callback, run_slot, copy);
    if (read != GF_STATUS_OK) {
        return read;
    }
    binding = std::make_unique<CallbackBinding>(copy);
    return GF_STATUS_OK;
}

void disarm_all(RunCallbacks& callbacks) noexcept {
    if (callbacks.ready) (*callbacks.ready).disarm();
    if (callbacks.closing) (*callbacks.closing).disarm();
    if (callbacks.closed) (*callbacks.closed).disarm();
}

gf_status application_status(const ApplicationResult& result) noexcept {
    switch (result.error) {
    case ApplicationError::none:
        return GF_STATUS_OK;
    case ApplicationError::invalid_argument:
        return fail(GF_STATUS_INVALID_ARGUMENT, run_slot, "the host refused the window");
    case ApplicationError::wrong_thread:
        return fail(GF_STATUS_WRONG_THREAD, run_slot, "the host runs windows on another thread");
    case ApplicationError::already_running:
        return fail(GF_STATUS_INVALID_STATE, run_slot, "the host is already running a window");
    case ApplicationError::unsupported:
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, run_slot, "this build has no window host");
    case ApplicationError::backend_failure:
        return fail(GF_STATUS_INTERNAL, run_slot, "the window host failed");
    case ApplicationError::callback_failure:
        return fail(GF_STATUS_INTERNAL, run_slot, "a host callback failed");
    case ApplicationError::after_shutdown:
        return fail(GF_STATUS_INVALID_STATE, run_slot, "the host has been shut down");
    }
    return fail(GF_STATUS_INTERNAL, run_slot, "the host returned an unknown result");
}

gf_status run_window(gf_object root, const gf_run_options* options) {
    if (options == nullptr || (*options).struct_size < run_options_size_1_0 ||
        ((*options).flags & ~known_run_flags) != 0U || !std::isfinite((*options).width) ||
        !std::isfinite((*options).height) || (*options).width < 0.0 || (*options).height < 0.0) {
        return fail(GF_STATUS_INVALID_ARGUMENT, run_slot,
                    "run needs options of at least the 1.0 size, known flags and a finite, non-negative size");
    }
    std::string_view title;
    const gf_status titled = read_text((*options).title, run_slot, title);
    if (titled != GF_STATUS_OK) {
        return titled;
    }
    const gf_status entered = registry().enter_ui(run_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status found = registry().find_component(root, run_slot, object);
    if (found != GF_STATUS_OK) {
        return found;
    }
    // A window from create, or a root control the run makes a window for.
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    const Control::Ptr control = std::dynamic_pointer_cast<Control>(object);
    if (handle == nullptr && control == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, run_slot, "run takes a window or a root control");
    }
    if (!(*object).is_alive()) {
        return fail(GF_STATUS_DISPOSED, run_slot, "the window or root has been disposed");
    }
    if (handle != nullptr && !(*handle).owns_window()) {
        return fail(GF_STATUS_INVALID_STATE, run_slot, "the window is running");
    }
    if (control != nullptr && ((*control).parent() || (*control).attached())) {
        return fail(GF_STATUS_INVALID_STATE, run_slot, "the root has a parent or is already in a window");
    }
    RunCallbacks callbacks{};
    callbacks.root = root;
    gf_status read = read_optional_callback((*options).ready, callbacks.ready);
    if (read == GF_STATUS_OK) {
        read = read_optional_callback((*options).closing, callbacks.closing);
    }
    if (read == GF_STATUS_OK) {
        read = read_optional_callback((*options).closed, callbacks.closed);
    }
    if (read != GF_STATUS_OK) {
        disarm_all(callbacks);
        return read;
    }
    ApplicationWindowOptions window_options{};
    Size client_size = window_options.initial_size;
    if ((*options).width > 0.0) client_size.width = (*options).width;
    if ((*options).height > 0.0) client_size.height = (*options).height;
    std::unique_ptr<Window> window;
    try {
        if (!title.empty()) window_options.title = std::string(title);
        window_options.initial_size = client_size;
        window_options.ready = RunReady{&callbacks};
        window_options.wake_ready = RunWakeReady{};
        window_options.dispatch_pending = RunDispatchPending{};
        window_options.closing = RunClosing{&callbacks};
        window_options.closed = RunClosed{&callbacks};
        // The window shares the library's resources: its theme, images and
        // published values (GF_TABLE_THEMES).
        if (handle != nullptr) {
            Window& made = *(*handle).window();
            made.resize(client_size);
            engine().publish_all(made);
        } else {
            window = make_run_window(control, client_size);
        }
    } catch (...) {
        disarm_all(callbacks);
        throw;
    }
    const gf_status began = registry().begin_run(root, run_slot);
    if (began != GF_STATUS_OK) {
        disarm_all(callbacks);
        return began;
    }
    // From here the callbacks are taken: they are released when this
    // function returns, whatever the run's result.
    if (handle != nullptr) {
        window = (*handle).take_for_run();
    }
    ApplicationResult result{};
    try {
        result = Application::run(std::move(window), std::move(window_options));
    } catch (...) {
        engine().detach_window();
        registry().end_run();
        if (handle != nullptr) {
            (*handle).end_run();
        }
        throw;
    }
    // A run that ended before closed (a host failure) leaves no window.
    engine().detach_window();
    registry().end_run();
    // The run destroyed the window: components bound to it are unbound.
    if (handle != nullptr) {
        (*handle).end_run();
    }
    // Releases that arrived during the last turn complete before run returns.
    static_cast<void>(registry().run_posted(0U));
    const gf_status status = application_status(result);
    return status;
}

gf_status GF_CALL windows_run(gf_object root, const gf_run_options* options) noexcept {
    try {
        const ShadowFrameScope shadow_frame(run_slot.table_id, run_slot.slot_index);
        const gf_status ran = run_window(root, options);
        return ran;
    } catch (...) {
        return fail_with_current_exception(run_slot);
    }
}

gf_status GF_CALL windows_request_close(gf_object root) noexcept {
    try {
        const ShadowFrameScope shadow_frame(request_close_slot.table_id, request_close_slot.slot_index);
        const gf_status entered = registry().enter_ui(request_close_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Component::Ptr object;
        const gf_status found = registry().find_component(root, request_close_slot, object);
        if (found != GF_STATUS_OK) {
            return found;
        }
        ApplicationWindowHandle window;
        const gf_status running = registry().running_window(root, request_close_slot, window);
        if (running != GF_STATUS_OK) {
            return running;
        }
        if (!window.active()) {
            return fail(GF_STATUS_INVALID_STATE, request_close_slot, "the window is not ready yet or has closed");
        }
        const HostServiceStatus requested = window.request_close();
        if (!requested.accepted()) {
            return fail(GF_STATUS_INTERNAL, request_close_slot, "the host refused the close request");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(request_close_slot);
    }
}

// A window over root, not yet running. Validated before anything is made.
gf_status create_window(gf_object root, gf_object* made) {
    if (made == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "create needs a window output");
    }
    const gf_status entered = registry().enter_ui(create_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Control::Ptr control;
    const gf_status found = registry().find_object(root, create_slot, control);
    if (found != GF_STATUS_OK) {
        return found;
    }
    if (!(*control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, create_slot, "the root has been disposed");
    }
    if ((*control).parent() || (*control).attached()) {
        return fail(GF_STATUS_INVALID_STATE, create_slot, "the root has a parent or is already in a window");
    }
    const ApplicationWindowOptions defaults{};
    std::unique_ptr<Window> window = make_run_window(control, defaults.initial_size);
    const Component::Ptr handle = std::make_shared<WindowHandle>(std::move(window), control);
    *made = registry().add_reference(handle);
    return GF_STATUS_OK;
}

gf_status GF_CALL windows_create(gf_object root, gf_object* window) noexcept {
    try {
        const ShadowFrameScope shadow_frame(create_slot.table_id, create_slot.slot_index);
        const gf_status created = create_window(root, window);
        return created;
    } catch (...) {
        return fail_with_current_exception(create_slot);
    }
}

constexpr gf_windows_table table{
    .struct_size = sizeof(gf_windows_table),
    .abi_minor = GF_ABI_MINOR,
    .run = &windows_run,
    .request_close = &windows_request_close,
    .create = &windows_create,
};

} // namespace

const gf_windows_table& windows_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
