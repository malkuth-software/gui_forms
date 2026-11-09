// GF_TABLE_WINDOWS: the run's window through GUIForms::Application, and
// the modal and owned windows opened over it during the run (since 1.1).
// See gf_abi.h.

#include "abi_tables.hpp"
#include "abi_records.hpp"

#include "../engine/abi_engine.hpp"
#include "../registry/abi_registry.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/application.hpp"
#include "gui_forms/window.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity run_slot{GF_TABLE_WINDOWS, 0U};
constexpr SlotIdentity request_close_slot{GF_TABLE_WINDOWS, 1U};
constexpr SlotIdentity create_slot{GF_TABLE_WINDOWS, 2U};
constexpr SlotIdentity interaction_count_slot{GF_TABLE_WINDOWS, 3U};
constexpr SlotIdentity interaction_at_slot{GF_TABLE_WINDOWS, 4U};
constexpr SlotIdentity request_help_slot{GF_TABLE_WINDOWS, 5U};
constexpr SlotIdentity show_modal_slot{GF_TABLE_WINDOWS, 6U};
constexpr SlotIdentity show_owned_slot{GF_TABLE_WINDOWS, 7U};

// The 1.0 size of gf_run_options: a caller passes at least this much.
constexpr std::uint32_t run_options_size_1_0 = 64U;
// Since 1.1: minimum_width and minimum_height.
constexpr std::uint32_t run_options_size_1_1 = 80U;
static_assert(sizeof(gf_run_options) == run_options_size_1_1);

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

// A logical length for a failure message: whole units without a fraction.
void append_length(std::string& message, double length) {
    const double whole = std::floor(length);
    if (whole == length && std::abs(length) < 1.0e9) {
        message += std::to_string(static_cast<long long>(whole));
    } else {
        message += std::to_string(length);
    }
}

// The run's refusal of the window: its reason, and for a client size below
// the minimum both sizes, as "120 x 100 is below the minimum 150 x 150".
gf_status refused_window(const ApplicationResult& result) noexcept {
    try {
        std::string message("the run refused the window: ");
        const std::string_view reason = application_window_refusal_text(result.refusal);
        message += reason.empty() ? std::string_view("the host refused it") : reason;
        if (result.refusal == ApplicationWindowRefusal::below_minimum_size) {
            message += " (";
            append_length(message, result.initial_size.width);
            message += " x ";
            append_length(message, result.initial_size.height);
            message += " is below the minimum ";
            append_length(message, result.minimum_size.width);
            message += " x ";
            append_length(message, result.minimum_size.height);
            message += ")";
        }
        return fail(GF_STATUS_INVALID_ARGUMENT, run_slot, message);
    } catch (...) {
        return fail(GF_STATUS_INVALID_ARGUMENT, run_slot, "the run refused the window");
    }
}

gf_status application_status(const ApplicationResult& result) noexcept {
    switch (result.error) {
    case ApplicationError::none:
        return GF_STATUS_OK;
    case ApplicationError::invalid_argument:
        return refused_window(result);
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
    case ApplicationError::windows_alive:
        // Application::close's result; a run never gives it.
        return fail(GF_STATUS_INTERNAL, run_slot, "the run gave an application close result");
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
    // The minimum, since 1.1; a 1.0 record has none.
    Size minimum{};
    if ((*options).struct_size >= run_options_size_1_1) {
        minimum = Size{(*options).minimum_width, (*options).minimum_height};
        const bool minimum_valid = std::isfinite(minimum.width) && std::isfinite(minimum.height) &&
                                   minimum.width >= 0.0 && minimum.height >= 0.0 && minimum.width <= 32768.0 &&
                                   minimum.height <= 32768.0;
        if (!minimum_valid) {
            return fail(GF_STATUS_INVALID_ARGUMENT, run_slot,
                        "a run's minimum size is finite and within 0 to 32768 logical units");
        }
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
        window_options.minimum_size = minimum;
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
        // The program's current application runs the window; a C program
        // that made none gets one for this run (its configuration through
        // the ABI is a later step).
        Application* application = Application::current();
        ApplicationCreateResult made{};
        if (application == nullptr) {
            made = Application::create(ApplicationConfig{});
            if (!made) throw std::logic_error("GUI.Forms: the run's application was refused");
            application = made.application.get();
        }
        result = (*application).run(std::move(window), std::move(window_options));
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

// ---------------------------------------------------------------------------
// Owned windows (since 1.1): show_modal and show_owned.

// The 1.1 size of gf_owned_window_options: a caller passes at least this much.
constexpr std::uint32_t owned_window_options_size_1_1 = 72U;
static_assert(sizeof(gf_owned_window_options) == owned_window_options_size_1_1);

constexpr gf_owned_window_flags known_owned_window_flags = GF_OWNED_WINDOW_TOOL;

// One window show_modal or show_owned opened. Shared by the window's
// callbacks (which the host owns until it releases the window) and, from
// ready until closed, by owned_windows(). The bindings are released when
// the window closes; a refused open disarms them instead, so the caller
// keeps its callbacks.
struct OwnedWindow final {
    gf_object root = 0U;
    // Keeps the root alive while its window is open.
    Control::Ptr control;
    std::string stable_id;
    ApplicationWindowHandle window;
    bool ready_seen = false;
    std::unique_ptr<CallbackBinding> ready;
    std::unique_ptr<CallbackBinding> closing;
    std::unique_ptr<CallbackBinding> closed;
};

// The owned windows from ready until closed, in opening order. UI thread
// only; bounded by the host's window table (HostWindowing::maximum_windows).
std::vector<std::shared_ptr<OwnedWindow>>& owned_windows() {
    static std::vector<std::shared_ptr<OwnedWindow>> windows;
    return windows;
}

// The open owned window over root, or null.
std::shared_ptr<OwnedWindow> find_owned(gf_object root) {
    const std::vector<std::shared_ptr<OwnedWindow>>& windows = owned_windows();
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        if ((*windows[index]).root == root) {
            return windows[index];
        }
    }
    return nullptr;
}

// Calls callback, if any, with the window's root as sender. Returns the
// event's cancel field.
std::uint32_t call_owned_callback(gf_object root, const CallbackBinding* callback, std::string_view name) noexcept {
    if (callback == nullptr) {
        return 0U;
    }
    gf_event event{};
    event.struct_size = sizeof(gf_event);
    event.sender = root;
    event.name.data = name.data();
    event.name.size = name.size();
    (*callback).invoke(event);
    return event.cancel;
}

struct OwnedReady {
    std::shared_ptr<OwnedWindow> owned;
    void operator()(Window&, ApplicationWindowHandle window) const {
        (*owned).window = window;
        (*owned).ready_seen = true;
        owned_windows().push_back(owned);
        static_cast<void>(call_owned_callback((*owned).root, (*owned).ready.get(), "ready"));
    }
};

struct OwnedClosing {
    std::shared_ptr<OwnedWindow> owned;
    void operator()(HostCloseRequest& request) const {
        const std::uint32_t cancel = call_owned_callback((*owned).root, (*owned).closing.get(), "closing");
        if (cancel != 0U) {
            request.cancel = true;
        }
    }
};

// Forgets the window, calls closed, then releases the three callbacks
// once each.
struct OwnedClosed {
    std::shared_ptr<OwnedWindow> owned;
    void operator()() const {
        std::vector<std::shared_ptr<OwnedWindow>>& windows = owned_windows();
        for (std::size_t index = 0U; index < windows.size(); ++index) {
            if (windows[index] == owned) {
                windows.erase(windows.begin() + static_cast<std::ptrdiff_t>(index));
                break;
            }
        }
        static_cast<void>(call_owned_callback((*owned).root, (*owned).closed.get(), "closed"));
        (*owned).ready.reset();
        (*owned).closing.reset();
        (*owned).closed.reset();
    }
};

void disarm_owned(OwnedWindow& owned) noexcept {
    if (owned.ready) (*owned.ready).disarm();
    if (owned.closing) (*owned.closing).disarm();
    if (owned.closed) (*owned.closed).disarm();
}

gf_status owned_status(const HostServiceStatus& status, SlotIdentity slot) noexcept {
    switch (status.error) {
    case HostServiceError::none:
        return GF_STATUS_OK;
    case HostServiceError::unsupported:
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot, "the host cannot open this window during a run");
    case HostServiceError::wrong_thread:
        return fail(GF_STATUS_WRONG_THREAD, slot, "the host runs windows on another thread");
    case HostServiceError::invalid_argument:
        return fail(GF_STATUS_INVALID_STATE, slot, "the host refused the window (its owner or stable id)");
    case HostServiceError::invalid_utf8:
    case HostServiceError::too_large:
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the host refused the window's text");
    case HostServiceError::modal_limit:
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "too many modal windows are open");
    case HostServiceError::backend_failure:
        return fail(GF_STATUS_INTERNAL, slot, "the window host failed");
    case HostServiceError::after_shutdown:
        return fail(GF_STATUS_INVALID_STATE, slot, "the run has ended");
    }
    return fail(GF_STATUS_INTERNAL, slot, "the host returned an unknown result");
}

// Validates root and options and fills entry, owned and windows for an
// open. Nothing is opened; on a refusal the callbacks are not taken.
gf_status prepare_owned(gf_object root, const gf_owned_window_options* options, SlotIdentity slot,
                        ApplicationWindows& windows, ApplicationWindow& entry,
                        std::shared_ptr<OwnedWindow>& owned) {
    if (options == nullptr || (*options).struct_size < owned_window_options_size_1_1 ||
        ((*options).flags & ~known_owned_window_flags) != 0U || !std::isfinite((*options).width) ||
        !std::isfinite((*options).height) || (*options).width < 0.0 || (*options).height < 0.0) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "the options need at least the 1.1 size, known flags and a finite, non-negative size");
    }
    std::string_view title;
    const gf_status titled = read_text((*options).title, slot, title);
    if (titled != GF_STATUS_OK) {
        return titled;
    }
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Control::Ptr control;
    const gf_status found = registry().find_object(root, slot, control);
    if (found != GF_STATUS_OK) {
        return found;
    }
    if (!(*control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the root has been disposed");
    }
    if ((*control).parent() || (*control).attached()) {
        return fail(GF_STATUS_INVALID_STATE, slot, "the root has a parent or is already in a window");
    }
    gf_object run_root = 0U;
    ApplicationWindowHandle running;
    const gf_status run = registry().current_run(slot, run_root, running);
    if (run != GF_STATUS_OK) {
        return run;
    }
    if (!running.active()) {
        return fail(GF_STATUS_INVALID_STATE, slot, "the running window is not ready yet or has closed");
    }
    // The owner: the running window ("main", Application::run's stable id)
    // or an open owned window.
    std::string owner_id = "main";
    if ((*options).owner != 0U && (*options).owner != run_root) {
        const std::shared_ptr<OwnedWindow> owner = find_owned((*options).owner);
        if (owner == nullptr || !(*owner).window.active()) {
            return fail(GF_STATUS_INVALID_STATE, slot, "the owner is not the root of an open window");
        }
        owner_id = (*owner).stable_id;
    }
    const std::string stable_id((*control).stable_id().value());
    if (stable_id.empty() || stable_id == "main" || find_owned(root) != nullptr) {
        return fail(GF_STATUS_INVALID_STATE, slot, "the root's stable id is empty, \"main\" or already a window's");
    }
    const std::vector<std::shared_ptr<OwnedWindow>>& open = owned_windows();
    for (std::size_t index = 0U; index < open.size(); ++index) {
        if ((*open[index]).stable_id == stable_id) {
            return fail(GF_STATUS_INVALID_STATE, slot, "another open window has the root's stable id");
        }
    }
    std::shared_ptr<OwnedWindow> made = std::make_shared<OwnedWindow>();
    (*made).root = root;
    (*made).control = control;
    (*made).stable_id = stable_id;
    gf_status read = read_optional_callback((*options).ready, (*made).ready);
    if (read == GF_STATUS_OK) {
        read = read_optional_callback((*options).closing, (*made).closing);
    }
    if (read == GF_STATUS_OK) {
        read = read_optional_callback((*options).closed, (*made).closed);
    }
    if (read != GF_STATUS_OK) {
        disarm_owned(*made);
        return read;
    }
    try {
        ApplicationWindowOptions window_options{};
        Size client_size = window_options.initial_size;
        if ((*options).width > 0.0) client_size.width = (*options).width;
        if ((*options).height > 0.0) client_size.height = (*options).height;
        if (!title.empty()) window_options.title = std::string(title);
        window_options.initial_size = client_size;
        window_options.ready = OwnedReady{made};
        window_options.closing = OwnedClosing{made};
        window_options.closed = OwnedClosed{made};
        entry.stable_id = stable_id;
        entry.owner_id = owner_id;
        entry.tool_window = ((*options).flags & GF_OWNED_WINDOW_TOOL) != 0U;
        entry.model = make_run_window(control, client_size);
        entry.options = std::move(window_options);
    } catch (...) {
        disarm_owned(*made);
        throw;
    }
    windows = running.windows();
    owned = std::move(made);
    return GF_STATUS_OK;
}

gf_status show_modal_window(gf_object root, const gf_owned_window_options* options, gf_dialog_result_code* result) {
    ApplicationWindows windows;
    ApplicationWindow entry{};
    std::shared_ptr<OwnedWindow> owned;
    const gf_status prepared = prepare_owned(root, options, show_modal_slot, windows, entry, owned);
    if (prepared != GF_STATUS_OK) {
        return prepared;
    }
    ApplicationModalResult shown{};
    try {
        shown = windows.show_modal(std::move(entry));
    } catch (...) {
        if (!(*owned).ready_seen) disarm_owned(*owned);
        throw;
    }
    if (!shown.status.accepted()) {
        // A window that never became ready took nothing.
        if (!(*owned).ready_seen) disarm_owned(*owned);
        return owned_status(shown.status, show_modal_slot);
    }
    if ((*owned).window.active()) {
        // The run is ending: the window closes with it.
        return fail(GF_STATUS_CANCELLED, show_modal_slot, "the run ended while the modal window was open");
    }
    if (result != nullptr) {
        *result = static_cast<gf_dialog_result_code>(shown.dialog_result);
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL windows_show_modal(gf_object root, const gf_owned_window_options* options,
                                     gf_dialog_result_code* result) noexcept {
    try {
        const ShadowFrameScope shadow_frame(show_modal_slot.table_id, show_modal_slot.slot_index);
        const gf_status shown = show_modal_window(root, options, result);
        return shown;
    } catch (...) {
        return fail_with_current_exception(show_modal_slot);
    }
}

gf_status show_owned_window(gf_object root, const gf_owned_window_options* options) {
    ApplicationWindows windows;
    ApplicationWindow entry{};
    std::shared_ptr<OwnedWindow> owned;
    const gf_status prepared = prepare_owned(root, options, show_owned_slot, windows, entry, owned);
    if (prepared != GF_STATUS_OK) {
        return prepared;
    }
    ApplicationWindowOpenResult opened{};
    try {
        opened = windows.open_window(std::move(entry));
    } catch (...) {
        if (!(*owned).ready_seen) disarm_owned(*owned);
        throw;
    }
    if (!opened.status.accepted()) {
        if (!(*owned).ready_seen) disarm_owned(*owned);
        return owned_status(opened.status, show_owned_slot);
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL windows_show_owned(gf_object root, const gf_owned_window_options* options) noexcept {
    try {
        const ShadowFrameScope shadow_frame(show_owned_slot.table_id, show_owned_slot.slot_index);
        const gf_status shown = show_owned_window(root, options);
        return shown;
    } catch (...) {
        return fail_with_current_exception(show_owned_slot);
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
        // A window show_modal or show_owned opened (since 1.1).
        const std::shared_ptr<OwnedWindow> owned = find_owned(root);
        if (owned != nullptr) {
            const HostServiceStatus asked = (*owned).window.request_close();
            if (!asked.accepted()) {
                return owned_status(asked, request_close_slot);
            }
            return GF_STATUS_OK;
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

// The interactions every window raises (Window::append_interactions). Their
// names and payloads are static strings, so the views interaction_at hands
// out stay valid while the library is loaded.
std::vector<InteractionDeclaration> window_interactions() {
    std::vector<InteractionDeclaration> interactions;
    Window::append_interactions(interactions);
    return interactions;
}

gf_status GF_CALL windows_interaction_count(std::uint64_t* count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(interaction_count_slot.table_id, interaction_count_slot.slot_index);
        if (count == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, interaction_count_slot, "interaction_count needs a count output");
        }
        const std::vector<InteractionDeclaration> interactions = window_interactions();
        *count = static_cast<std::uint64_t>(interactions.size());
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(interaction_count_slot);
    }
}

gf_status GF_CALL windows_interaction_at(std::uint64_t interaction, gf_schema_interaction* record) noexcept {
    try {
        const ShadowFrameScope shadow_frame(interaction_at_slot.table_id, interaction_at_slot.slot_index);
        const gf_status valid = check_record(record, interaction_at_slot);
        if (valid != GF_STATUS_OK) {
            return valid;
        }
        const std::vector<InteractionDeclaration> interactions = window_interactions();
        if (interaction >= interactions.size()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, interaction_at_slot, "the index is past the end of its list");
        }
        const InteractionDeclaration& held = interactions[static_cast<std::size_t>(interaction)];
        gf_schema_interaction complete{};
        complete.struct_size = sizeof(gf_schema_interaction);
        complete.mutable_payload = held.mutable_payload ? 1U : 0U;
        complete.name = gf_utf8_view{held.name.data(), held.name.size()};
        complete.payload = gf_utf8_view{held.payload.data(), held.payload.size()};
        write_record(complete, record);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(interaction_at_slot);
    }
}

// Raises a created or running window's help request, not keyboard-initiated.
gf_status request_window_help(gf_object window, std::uint32_t* handled) {
    if (handled == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, request_help_slot, "request_help needs a handled output");
    }
    const gf_status entered = registry().enter_ui(request_help_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status found = registry().find_component(window, request_help_slot, object);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, request_help_slot, "request_help takes a window");
    }
    Window* const held = (*handle).window();
    if (!(*handle).is_alive() || held == nullptr) {
        return fail(GF_STATUS_DISPOSED, request_help_slot, "the window has been disposed or has closed");
    }
    const bool answered = (*held).request_help(false);
    *handled = answered ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL windows_request_help(gf_object window, std::uint32_t* handled) noexcept {
    try {
        const ShadowFrameScope shadow_frame(request_help_slot.table_id, request_help_slot.slot_index);
        const gf_status requested = request_window_help(window, handled);
        return requested;
    } catch (...) {
        return fail_with_current_exception(request_help_slot);
    }
}

constexpr gf_windows_table table{
    .struct_size = sizeof(gf_windows_table),
    .abi_minor = GF_ABI_MINOR,
    .run = &windows_run,
    .request_close = &windows_request_close,
    .create = &windows_create,
    .interaction_count = &windows_interaction_count,
    .interaction_at = &windows_interaction_at,
    .request_help = &windows_request_help,
    .show_modal = &windows_show_modal,
    .show_owned = &windows_show_owned,
};

} // namespace

const gf_windows_table& windows_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
