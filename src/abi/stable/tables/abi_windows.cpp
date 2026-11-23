// GF_TABLE_WINDOWS: the run's window through GUIForms::Application, and
// the modal and owned windows opened over it during the run (since 1.1).
// See gf_abi.h.

#include "abi_tables.hpp"
#include "abi_records.hpp"

#include "../engine/abi_engine.hpp"
#include "../events/abi_event_fields.hpp"
#include "../registry/abi_registry.hpp"
#include "../schema/abi_schema.hpp"
#include "../values/abi_values.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/application.hpp"
#include "gui_forms/components/business_card/business_card.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/window/band/window_band.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"


#include <stxlib/optional.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <functional>
#include <memory>
#include <optional>
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
constexpr SlotIdentity key_state_slot{GF_TABLE_WINDOWS, 6U};
constexpr SlotIdentity show_modal_slot{GF_TABLE_WINDOWS, 7U};
constexpr SlotIdentity show_owned_slot{GF_TABLE_WINDOWS, 8U};
constexpr SlotIdentity window_properties_slot{GF_TABLE_WINDOWS, 9U};
constexpr SlotIdentity focused_control_slot{GF_TABLE_WINDOWS, 10U};
constexpr SlotIdentity declare_band_slot{GF_TABLE_WINDOWS, 11U};
constexpr SlotIdentity undeclare_band_slot{GF_TABLE_WINDOWS, 12U};
constexpr SlotIdentity set_band_appearance_slot{GF_TABLE_WINDOWS, 13U};
constexpr SlotIdentity band_report_slot{GF_TABLE_WINDOWS, 14U};
constexpr SlotIdentity set_corner_preference_slot{GF_TABLE_WINDOWS, 15U};
constexpr SlotIdentity content_rect_slot{GF_TABLE_WINDOWS, 16U};
constexpr SlotIdentity shaping_slot{GF_TABLE_WINDOWS, 17U};
constexpr SlotIdentity pointer_location_slot{GF_TABLE_WINDOWS, 18U};
constexpr SlotIdentity show_business_card_slot{GF_TABLE_WINDOWS, 19U};

// The 1.5 sizes of the band and shaping records.
constexpr std::uint32_t band_options_size_1_5 = 56U;
constexpr std::uint32_t band_appearance_size_1_5 = 80U;
constexpr std::uint32_t band_report_size_1_5 = 72U;
constexpr std::uint32_t shaping_size_1_5 = 32U;
static_assert(sizeof(gf_window_band_options) == band_options_size_1_5);
static_assert(sizeof(gf_window_band_appearance) == band_appearance_size_1_5);
static_assert(sizeof(gf_window_band_report) == band_report_size_1_5);
static_assert(sizeof(gf_window_shaping) == shaping_size_1_5);

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
    // The run's window model, from ready until run returns (observing; the
    // run's window owns it): closed reads its host_close_reason. Kept across
    // callbacks, so a weak handle, not a borrow.
    WindowRef model;
};

// The "reason" field's text (gf_abi.h, "Run callbacks"): the names of
// HostCloseReason, whose numbers the field's signed_integer carries.
std::string_view close_reason_name(HostCloseReason reason) noexcept {
    switch (reason) {
        case HostCloseReason::user: return "user";
        case HostCloseReason::application: return "application";
        case HostCloseReason::session_end: return "session_end";
        case HostCloseReason::test: return "test";
        case HostCloseReason::quit: return "quit";
    }
    return "application";
}

// Calls callback, if any, with the root as sender and, for closing and
// closed, one read-only ENUMERATION field "reason". Returns the event's
// cancel field. The field's records live in this frame and stay valid for
// the whole invoke.
std::uint32_t call_run_callback(const RunCallbacks& callbacks, const CallbackBinding* callback,
                                std::string_view name, std::optional<HostCloseReason> reason) {
    if (callback == nullptr) {
        return 0U;
    }
    gf_event event{};
    event.struct_size = sizeof(gf_event);
    event.sender = callbacks.root;
    event.name.data = name.data();
    event.name.size = name.size();
    if (!reason.has_value()) {
        (*callback).invoke(event);
        return event.cancel;
    }
    EventFields fields;
    gf_value& record = fields.append("reason", GF_VALUE_KIND_ENUMERATION, 0U, nullptr);
    record.signed_integer = static_cast<std::int64_t>(*reason);
    // Valid only during the callback (EventFields::lend_text).
    const std::string_view text = fields.lend_text(close_reason_name(*reason));
    record.text.data = text.data();
    record.text.size = text.size();
    fields.publish(event, false);
    (*callback).invoke(event);
    fields.end_publication();
    return event.cancel;
}

// The host's callbacks, as named targets with the run's callbacks as their
// context. Each runs on the UI thread during Application::run.
struct RunReady {
    RunCallbacks* callbacks = nullptr;
    void operator()(Window& model, ApplicationWindowHandle window) const {
        registry().set_run_window(window);
        engine().attach_window(model);
        (*callbacks).model = model.window_ref();
        static_cast<void>(call_run_callback(*callbacks, (*callbacks).ready.get(), "ready", std::nullopt));
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
        const std::uint32_t cancel =
            call_run_callback(*callbacks, (*callbacks).closing.get(), "closing", request.reason);
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
        // The model outlives its closed callback; a run whose window never
        // became ready reports the program's close.
        std::optional<HostCloseReason> reason{HostCloseReason::application};
        Window* const model = (*callbacks).model.get();
        if (model != nullptr) {
            const stxlib::Optional<HostCloseReason> held = (*model).host_close_reason();
            const HostCloseReason* const host_reason = held.value();
            if (host_reason != nullptr) reason = *host_reason;
        }
        (*callbacks).model.reset();
        static_cast<void>(call_run_callback(*callbacks, (*callbacks).closed.get(), "closed", reason));
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
    // Admitted first (N21): a run refused here (another run under way)
    // leaves a created window as it was and builds no window around a bare
    // root. Past this point the window is handed to Application::run, which
    // destroys it on its own refusals too (A-gf-abi-03-F8; the header says
    // so). Giving it back would need Application::run to return it.
    const gf_status began = registry().begin_run(root, run_slot);
    if (began != GF_STATUS_OK) {
        disarm_all(callbacks);
        return began;
    }
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
        registry().end_run();
        throw;
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

gf_status windows_run_body(gf_object root, const gf_run_options* options) {
    const gf_status ran = run_window(root, options);
    return ran;
}

gf_status GF_CALL windows_run(gf_object root, const gf_run_options* options) noexcept {
    return call_slot(run_slot, windows_run_body, root, options);
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
    // Never destroyed (termination releases none of our state, rule 1;
    // A-gf-abi-03-F2): no client callback or control is released by a static
    // destructor at exit.
    static std::vector<std::shared_ptr<OwnedWindow>>* const windows = new std::vector<std::shared_ptr<OwnedWindow>>();
    return *windows;
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
    void operator()(Window& model, ApplicationWindowHandle window) const {
        (*owned).window = window;
        (*owned).ready_seen = true;
        owned_windows().push_back(owned);
        // Published values and dialog owners reach the window from here
        // until it closes (N32, N33).
        engine().attach_owned_window((*owned).root, model);
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
        engine().detach_owned_window((*owned).root);
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
    std::shared_ptr<OwnedWindow> made = gui_forms::detail::make_library_shared<OwnedWindow>();
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

gf_status windows_show_modal_body(gf_object root, const gf_owned_window_options* options,
                                  gf_dialog_result_code* result) {
    const gf_status shown = show_modal_window(root, options, result);
    return shown;
}

gf_status GF_CALL windows_show_modal(gf_object root, const gf_owned_window_options* options,
                                     gf_dialog_result_code* result) noexcept {
    return call_slot(show_modal_slot, windows_show_modal_body, root, options, result);
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

gf_status windows_show_owned_body(gf_object root, const gf_owned_window_options* options) {
    const gf_status shown = show_owned_window(root, options);
    return shown;
}

gf_status GF_CALL windows_show_owned(gf_object root, const gf_owned_window_options* options) noexcept {
    return call_slot(show_owned_slot, windows_show_owned_body, root, options);
}

gf_status windows_request_close_body(gf_object root) {
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
}

gf_status GF_CALL windows_request_close(gf_object root) noexcept {
    return call_slot(request_close_slot, windows_request_close_body, root);
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
    const Component::Ptr handle = gui_forms::detail::make_library_shared<WindowHandle>(std::move(window), control);
    *made = registry().add_reference(handle);
    return GF_STATUS_OK;
}

gf_status windows_create_body(gf_object root, gf_object* window) {
    const gf_status created = create_window(root, window);
    return created;
}

gf_status GF_CALL windows_create(gf_object root, gf_object* window) noexcept {
    return call_slot(create_slot, windows_create_body, root, window);
}

// The interactions every window raises (Window::append_interactions). Their
// names and payloads (the ABI's words for them, K2) are static strings, so
// the views interaction_at hands out stay valid while the library is loaded.
std::vector<InteractionDeclaration> window_interactions() {
    std::vector<InteractionDeclaration> interactions;
    Window::append_interactions(interactions);
    return interactions;
}

gf_status windows_interaction_count_body(std::uint64_t* count) {
    if (count == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, interaction_count_slot, "interaction_count needs a count output");
    }
    const std::vector<InteractionDeclaration> interactions = window_interactions();
    *count = static_cast<std::uint64_t>(interactions.size());
    return GF_STATUS_OK;
}

gf_status GF_CALL windows_interaction_count(std::uint64_t* count) noexcept {
    return call_slot(interaction_count_slot, windows_interaction_count_body, count);
}

gf_status windows_interaction_at_body(std::uint64_t interaction, gf_schema_interaction* record) {
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
    const std::string_view fields = interaction_payload_fields(held.payload);
    complete.payload = gf_utf8_view{fields.data(), fields.size()};
    write_record(complete, record);
    return GF_STATUS_OK;
}

gf_status GF_CALL windows_interaction_at(std::uint64_t interaction, gf_schema_interaction* record) noexcept {
    return call_slot(interaction_at_slot, windows_interaction_at_body, interaction, record);
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

gf_status windows_request_help_body(gf_object window, std::uint32_t* handled) {
    const gf_status requested = request_window_help(window, handled);
    return requested;
}

gf_status GF_CALL windows_request_help(gf_object window, std::uint32_t* handled) noexcept {
    return call_slot(request_help_slot, windows_request_help_body, window, handled);
}

// Window properties (since 1.1): what the window's host applies.
gf_status report_window_properties(gf_object window, gf_window_property_flags* supported,
                                   gf_window_property_flags* applied) {
    const gf_status entered = registry().enter_ui(window_properties_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status found = registry().find_component(window, window_properties_slot, object);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, window_properties_slot, "window_properties takes a window");
    }
    const Window* const held = (*handle).window();
    if (!(*handle).is_alive() || held == nullptr) {
        return fail(GF_STATUS_DISPOSED, window_properties_slot, "the window has been disposed or has closed");
    }
    // WindowChromeField's bits are GF_WINDOW_PROPERTY_*'s.
    if (supported != nullptr) *supported = (*held).supported_chrome().bits;
    if (applied != nullptr) *applied = (*held).applied_chrome().bits;
    return GF_STATUS_OK;
}

gf_status windows_window_properties_body(gf_object window, gf_window_property_flags* supported,
                                         gf_window_property_flags* applied) {
    const gf_status reported = report_window_properties(window, supported, applied);
    return reported;
}

gf_status GF_CALL windows_window_properties(gf_object window, gf_window_property_flags* supported,
                                            gf_window_property_flags* applied) noexcept {
    return call_slot(window_properties_slot, windows_window_properties_body, window, supported, applied);
}

// The focused control of a window from create, or of the window a control
// is in (a run's root, an owned or modal window's root, or any control).
gf_status report_focused_control(gf_object window_or_root, gf_object* control) {
    if (control == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, focused_control_slot, "focused_control needs a control output");
    }
    const gf_status entered = registry().enter_ui(focused_control_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status found = registry().find_component(window_or_root, focused_control_slot, object);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const SurfaceRoot* window = nullptr;
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle != nullptr) {
        if (!(*handle).is_alive() || (*handle).window() == nullptr) {
            return fail(GF_STATUS_DISPOSED, focused_control_slot, "the window has been disposed or has closed");
        }
        window = (*handle).window();
    } else {
        const Control::Ptr in_window = std::dynamic_pointer_cast<Control>(object);
        if (!in_window) {
            return fail(GF_STATUS_WRONG_HANDLE_KIND, focused_control_slot,
                        "focused_control takes a window or a control in one");
        }
        if (!(*in_window).is_alive()) {
            return fail(GF_STATUS_DISPOSED, focused_control_slot, "the object has been disposed");
        }
        window = (*in_window).attached_surface_root();
        if (window == nullptr) {
            return fail(GF_STATUS_INVALID_STATE, focused_control_slot, "the control is not in a window");
        }
    }
    // The composite rather than its inner edit part (ActiveControl).
    const Control::Ptr focused = (*window).active_control();
    // The reference is made before the output is written: a failed
    // allocation leaves the output as it was.
    const gf_object made = focused ? registry().add_reference(focused) : gf_object{0U};
    *control = made;
    return GF_STATUS_OK;
}

gf_status windows_focused_control_body(gf_object window_or_root, gf_object* control) {
    const gf_status reported = report_focused_control(window_or_root, control);
    return reported;
}

gf_status GF_CALL windows_focused_control(gf_object window_or_root, gf_object* control) noexcept {
    return call_slot(focused_control_slot, windows_focused_control_body, window_or_root, control);
}

// Where the pointer is, as the window of window_or_control last saw it:
// a window from create reports window coordinates, a control in a window
// its own (Control::pointer_location). The outputs are written only on
// success, each one given.
gf_status report_pointer_location(gf_object window_or_control, gf_logical_point* location,
                                  gf_logical_point* window_position, std::uint32_t* inside) {
    const gf_status entered = registry().enter_ui(pointer_location_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status found = registry().find_component(window_or_control, pointer_location_slot, object);
    if (found != GF_STATUS_OK) {
        return found;
    }
    PointerLocation reported{};
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle != nullptr) {
        const SurfaceRoot* window = (*handle).window();
        if (!(*handle).is_alive() || window == nullptr) {
            return fail(GF_STATUS_DISPOSED, pointer_location_slot, "the window has been disposed or has closed");
        }
        reported = (*window).pointer_location();
    } else {
        const Control::Ptr control = std::dynamic_pointer_cast<Control>(object);
        if (!control) {
            return fail(GF_STATUS_WRONG_HANDLE_KIND, pointer_location_slot,
                        "pointer_location takes a window or a control in one");
        }
        if (!(*control).is_alive()) {
            return fail(GF_STATUS_DISPOSED, pointer_location_slot, "the object has been disposed");
        }
        const SurfaceRoot* attached = (*control).attached_surface_root();
        if (attached == nullptr) {
            return fail(GF_STATUS_INVALID_STATE, pointer_location_slot, "the control is not in a window");
        }
        reported = (*control).pointer_location();
    }
    if (location != nullptr) {
        *location = gf_logical_point{reported.location.x, reported.location.y};
    }
    if (window_position != nullptr) {
        *window_position = gf_logical_point{reported.window_position.x, reported.window_position.y};
    }
    if (inside != nullptr) {
        *inside = reported.inside ? 1U : 0U;
    }
    return GF_STATUS_OK;
}

gf_status windows_pointer_location_body(gf_object window_or_control, gf_logical_point* location,
                                        gf_logical_point* window_position, std::uint32_t* inside) {
    const gf_status reported = report_pointer_location(window_or_control, location, window_position, inside);
    return reported;
}

gf_status GF_CALL windows_pointer_location(gf_object window_or_control, gf_logical_point* location,
                                           gf_logical_point* window_position, std::uint32_t* inside) noexcept {
    return call_slot(pointer_location_slot, windows_pointer_location_body, window_or_control, location, window_position, inside);
}

// The business card in the hand over a window from create, or over the
// window a control is in (since 1.5).
gf_status show_card(gf_object window_or_root, std::uint32_t* shown) {
    const gf_status entered = registry().enter_ui(show_business_card_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status found = registry().find_component(window_or_root, show_business_card_slot, object);
    if (found != GF_STATUS_OK) {
        return found;
    }
    Window* window = nullptr;
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle != nullptr) {
        if (!(*handle).is_alive() || (*handle).window() == nullptr) {
            return fail(GF_STATUS_DISPOSED, show_business_card_slot, "the window has been disposed or has closed");
        }
        window = (*handle).window();
    } else {
        const Control::Ptr in_window = std::dynamic_pointer_cast<Control>(object);
        if (!in_window) {
            return fail(GF_STATUS_WRONG_HANDLE_KIND, show_business_card_slot,
                        "show_business_card takes a window or a control in one");
        }
        if (!(*in_window).is_alive()) {
            return fail(GF_STATUS_DISPOSED, show_business_card_slot, "the object has been disposed");
        }
        window = (*in_window).attached_window();
        if (window == nullptr) {
            return fail(GF_STATUS_INVALID_STATE, show_business_card_slot,
                        "the control is not in a window (or is in a sub-surface)");
        }
    }
    const bool came_out = show_business_card(*window);
    if (shown != nullptr) *shown = came_out ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status windows_show_business_card_body(gf_object window_or_root, std::uint32_t* shown) {
    const gf_status reported = show_card(window_or_root, shown);
    return reported;
}

gf_status GF_CALL windows_show_business_card(gf_object window_or_root, std::uint32_t* shown) noexcept {
    return call_slot(show_business_card_slot, windows_show_business_card_body, window_or_root, shown);
}

static_assert(static_cast<std::uint32_t>(WindowChromeField::text) == GF_WINDOW_PROPERTY_TEXT &&
                  static_cast<std::uint32_t>(WindowChromeField::show_in_taskbar) ==
                      GF_WINDOW_PROPERTY_SHOW_IN_TASKBAR &&
                  static_cast<std::uint32_t>(WindowChromeField::minimum_size) == GF_WINDOW_PROPERTY_MINIMUM_SIZE &&
                  WindowChromeFields::all_bits == 4095U,
              "the chrome fields are the ABI's window property bits");

// Whether a created or running window holds physical_key down.
gf_status read_key_state(gf_object window, std::uint32_t physical_key, std::uint32_t* down) {
    if (down == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, key_state_slot, "key_state needs a down output");
    }
    const gf_status entered = registry().enter_ui(key_state_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status found = registry().find_component(window, key_state_slot, object);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, key_state_slot, "key_state takes a window");
    }
    Window* const held = (*handle).window();
    if (!(*handle).is_alive() || held == nullptr) {
        return fail(GF_STATUS_DISPOSED, key_state_slot, "the window has been disposed or has closed");
    }
    const bool pressed = (*held).is_key_down(physical_key);
    *down = pressed ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status windows_key_state_body(gf_object window, std::uint32_t physical_key, std::uint32_t* down) {
    const gf_status read = read_key_state(window, physical_key, down);
    return read;
}

gf_status GF_CALL windows_key_state(gf_object window, std::uint32_t physical_key, std::uint32_t* down) noexcept {
    return call_slot(key_state_slot, windows_key_state_body, window, physical_key, down);
}

// --- The window outer band and shaping (since 1.5) ----------------------------

// The window `window_or_root` names: a window from create, or the window a
// control is in. Fails with the slot's status otherwise.
gf_status find_band_window(gf_object window_or_root, SlotIdentity slot, Window*& found) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status looked_up = registry().find_component(window_or_root, slot, object);
    if (looked_up != GF_STATUS_OK) {
        return looked_up;
    }
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle != nullptr) {
        Window* const held = (*handle).window();
        if (!(*handle).is_alive() || held == nullptr) {
            return fail(GF_STATUS_DISPOSED, slot, "the window has been disposed or has closed");
        }
        found = held;
        return GF_STATUS_OK;
    }
    const Control::Ptr control = std::dynamic_pointer_cast<Control>(object);
    if (!control) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the band takes a window or a control in one");
    }
    if (!(*control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the object has been disposed");
    }
    Window* const window = (*control).attached_window();
    if (window == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, slot, "the control is not in a window");
    }
    found = window;
    return GF_STATUS_OK;
}

// The C++ declaration a caller's record gives, or empty when it is not
// one the window accepts.
std::optional<WindowBandOptions> band_options_from(const gf_window_band_options& record) noexcept {
    if (record.reserved != 0U || (record.flags & ~GF_WINDOW_BAND_PER_EDGE) != 0U) return std::nullopt;
    WindowBandOptions options{};
    if (record.full_screen == GF_WINDOW_BAND_FULL_SCREEN_STAYS) {
        options.full_screen = WindowBandFullScreen::stays;
    } else if (record.full_screen == GF_WINDOW_BAND_FULL_SCREEN_HIDES) {
        options.full_screen = WindowBandFullScreen::hides;
    } else {
        return std::nullopt;
    }
    options.width = record.width;
    if ((record.flags & GF_WINDOW_BAND_PER_EDGE) != 0U) {
        options.width = 0.0;
        options.edges.set(WindowBandEdges{record.edges.left, record.edges.top, record.edges.right, record.edges.bottom});
    }
    if (!window_band_options_valid(options)) return std::nullopt;
    return options;
}

std::optional<WindowBandState> band_state_from(gf_window_band_state state) noexcept {
    switch (state) {
    case GF_WINDOW_BAND_STATE_REST: return WindowBandState::rest;
    case GF_WINDOW_BAND_STATE_LIT: return WindowBandState::lit;
    case GF_WINDOW_BAND_STATE_FLASHING: return WindowBandState::flashing;
    case GF_WINDOW_BAND_STATE_CYCLING: return WindowBandState::cycling;
    default: return std::nullopt;
    }
}

std::optional<WindowBandColourRole> band_role_from(gf_window_band_role role) noexcept {
    switch (role) {
    case GF_WINDOW_BAND_ROLE_THEME: return WindowBandColourRole::theme;
    case GF_WINDOW_BAND_ROLE_FOCUS: return WindowBandColourRole::focus;
    case GF_WINDOW_BAND_ROLE_ACCENT: return WindowBandColourRole::accent;
    case GF_WINDOW_BAND_ROLE_ACCENT_LIGHT: return WindowBandColourRole::accent_light;
    case GF_WINDOW_BAND_ROLE_TEXT: return WindowBandColourRole::text;
    case GF_WINDOW_BAND_ROLE_BORDER: return WindowBandColourRole::border;
    default: return std::nullopt;
    }
}

// The texture a caller's record gives: none, or the procedural pattern or
// finish it names (GF_PROCEDURAL_PATTERN_*, GF_HATCH_STYLE_* and GF_FINISH_*
// are the C++ enumerations' orders; a hatch reads its style from the seed).
// False for an unknown texture, kind or hatch style.
bool band_texture_from(const gf_window_band_appearance& record, stxlib::Optional<WindowBandTexture>& texture) noexcept {
    texture.reset();
    if (record.texture == GF_WINDOW_BAND_TEXTURE_NONE) return true;
    WindowBandTexture made{};
    made.opacity = record.texture_opacity;
    if (record.texture == GF_WINDOW_BAND_TEXTURE_PROCEDURAL) {
        if (record.texture_kind > GF_PROCEDURAL_PATTERN_CARBON_FIBRE) return false;
        made.source = WindowBandTextureSource::procedural;
        made.procedural.pattern = static_cast<ProceduralPattern>(record.texture_kind);
        made.procedural.seed = record.texture_seed;
        if (record.texture_kind == GF_PROCEDURAL_PATTERN_HATCH) {
            if (record.texture_seed > GF_HATCH_STYLE_DIAGONAL_CROSS) return false;
            made.procedural.seed = 0U;
            made.procedural.hatch_style = static_cast<HatchStyle>(record.texture_seed);
        }
        made.procedural.ground = unpacked_color(record.texture_primary);
        made.procedural.figure = unpacked_color(record.texture_secondary);
        made.procedural.tile = record.texture_tile;
        made.procedural.strength = record.texture_strength;
    } else if (record.texture == GF_WINDOW_BAND_TEXTURE_FINISH) {
        if (record.texture_kind > GF_FINISH_CLOTH_WEAVE) return false;
        made.source = WindowBandTextureSource::finish;
        made.finish.kind = static_cast<FinishKind>(record.texture_kind);
        made.finish.seed = static_cast<std::int32_t>(record.texture_seed);
        made.finish.paint = unpacked_color(record.texture_primary);
        made.finish.secondary = unpacked_color(record.texture_secondary);
        made.finish.grain = record.texture_tile;
        made.finish.relief = record.texture_strength;
    } else {
        return false;
    }
    texture.set(made);
    return true;
}

static_assert(static_cast<std::uint32_t>(ProceduralPattern::checker) == GF_PROCEDURAL_PATTERN_CHECKER &&
                  static_cast<std::uint32_t>(ProceduralPattern::felt) == GF_PROCEDURAL_PATTERN_FELT &&
                  static_cast<std::uint32_t>(ProceduralPattern::hatch) == GF_PROCEDURAL_PATTERN_HATCH &&
                  static_cast<std::uint32_t>(ProceduralPattern::carbon_fibre) == GF_PROCEDURAL_PATTERN_CARBON_FIBRE,
              "GF_PROCEDURAL_PATTERN_* follow ProceduralPattern");
static_assert(static_cast<std::uint32_t>(HatchStyle::horizontal) == GF_HATCH_STYLE_HORIZONTAL &&
                  static_cast<std::uint32_t>(HatchStyle::diagonal_cross) == GF_HATCH_STYLE_DIAGONAL_CROSS,
              "GF_HATCH_STYLE_* follow HatchStyle");
static_assert(static_cast<std::uint32_t>(FinishKind::cloth_weave) == GF_FINISH_CLOTH_WEAVE &&
                  static_cast<std::uint32_t>(FinishKind::dusty_matte) == GF_FINISH_DUSTY_MATTE,
              "GF_FINISH_* follow FinishKind");

std::optional<WindowBandAppearance> band_appearance_from(const gf_window_band_appearance& record) noexcept {
    const std::uint32_t known_flags = GF_WINDOW_BAND_REST_COLOUR | GF_WINDOW_BAND_LIT_COLOUR;
    if (record.reserved != 0U || (record.flags & ~known_flags) != 0U) return std::nullopt;
    const std::optional<WindowBandState> state = band_state_from(record.state);
    const std::optional<WindowBandColourRole> role = band_role_from(record.lit_role);
    if (!state || !role) return std::nullopt;
    WindowBandAppearance appearance{};
    appearance.state = *state;
    appearance.lit_role = *role;
    appearance.frequency = record.frequency;
    if ((record.flags & GF_WINDOW_BAND_REST_COLOUR) != 0U) appearance.rest_colour.set(unpacked_color(record.rest_colour));
    if ((record.flags & GF_WINDOW_BAND_LIT_COLOUR) != 0U) appearance.lit_colour.set(unpacked_color(record.lit_colour));
    if (!band_texture_from(record, appearance.texture)) return std::nullopt;
    if (!window_band_appearance_valid(appearance)) return std::nullopt;
    return appearance;
}

gf_status declare_window_band(gf_object window, const gf_window_band_options* options) {
    if (options == nullptr || (*options).struct_size < band_options_size_1_5) {
        return fail(GF_STATUS_INVALID_ARGUMENT, declare_band_slot, "declare_band needs a 1.5 options record");
    }
    const std::optional<WindowBandOptions> declared = band_options_from(*options);
    if (!declared) {
        return fail(GF_STATUS_INVALID_ARGUMENT, declare_band_slot,
                    "the band's widths must be finite from 0 to 32 with a known full-screen choice and flags");
    }
    Window* found = nullptr;
    const gf_status located = find_band_window(window, declare_band_slot, found);
    if (located != GF_STATUS_OK) {
        return located;
    }
    (*found).declare_band(*declared);
    return GF_STATUS_OK;
}

gf_status windows_declare_band_body(gf_object window, const gf_window_band_options* options) {
    const gf_status declared = declare_window_band(window, options);
    return declared;
}

gf_status GF_CALL windows_declare_band(gf_object window, const gf_window_band_options* options) noexcept {
    return call_slot(declare_band_slot, windows_declare_band_body, window, options);
}

gf_status undeclare_window_band(gf_object window) {
    Window* found = nullptr;
    const gf_status located = find_band_window(window, undeclare_band_slot, found);
    if (located != GF_STATUS_OK) {
        return located;
    }
    (*found).undeclare_band();
    return GF_STATUS_OK;
}

gf_status windows_undeclare_band_body(gf_object window) {
    const gf_status undeclared = undeclare_window_band(window);
    return undeclared;
}

gf_status GF_CALL windows_undeclare_band(gf_object window) noexcept {
    return call_slot(undeclare_band_slot, windows_undeclare_band_body, window);
}

gf_status set_window_band_appearance(gf_object window, const gf_window_band_appearance* record) {
    if (record == nullptr || (*record).struct_size < band_appearance_size_1_5) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_band_appearance_slot,
                    "set_band_appearance needs a 1.5 appearance record");
    }
    const std::optional<WindowBandAppearance> appearance = band_appearance_from(*record);
    if (!appearance) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_band_appearance_slot,
                    "the appearance names an unknown state, role, flag or texture, or a value out of range");
    }
    Window* found = nullptr;
    const gf_status located = find_band_window(window, set_band_appearance_slot, found);
    if (located != GF_STATUS_OK) {
        return located;
    }
    (*found).set_band_appearance(*appearance);
    return GF_STATUS_OK;
}

gf_status windows_set_band_appearance_body(gf_object window, const gf_window_band_appearance* appearance) {
    const gf_status set = set_window_band_appearance(window, appearance);
    return set;
}

gf_status GF_CALL windows_set_band_appearance(gf_object window, const gf_window_band_appearance* appearance) noexcept {
    return call_slot(set_band_appearance_slot, windows_set_band_appearance_body, window, appearance);
}

gf_status report_window_band(gf_object window, gf_window_band_report* report) {
    if (report == nullptr || (*report).struct_size < band_report_size_1_5) {
        return fail(GF_STATUS_INVALID_ARGUMENT, band_report_slot, "band_report needs a 1.5 report record");
    }
    Window* found = nullptr;
    const gf_status located = find_band_window(window, band_report_slot, found);
    if (located != GF_STATUS_OK) {
        return located;
    }
    const WindowBandSnapshot snapshot = (*found).band_snapshot();
    gf_window_band_status status = 0U;
    if (snapshot.declared) status = status | GF_WINDOW_BAND_DECLARED;
    if (snapshot.reserved) status = status | GF_WINDOW_BAND_RESERVED;
    if (snapshot.flashing) status = status | GF_WINDOW_BAND_FLASHING;
    if (snapshot.cycling) status = status | GF_WINDOW_BAND_CYCLING;
    if (snapshot.textured) status = status | GF_WINDOW_BAND_TEXTURED;
    gf_window_band_report complete{};
    complete.struct_size = sizeof(gf_window_band_report);
    complete.status = status;
    complete.edges = gf_logical_insets{snapshot.edges.left, snapshot.edges.top, snapshot.edges.right,
                                       snapshot.edges.bottom};
    complete.drawn_colour = packed_color(snapshot.drawn);
    complete.full_screen = snapshot.options.full_screen == WindowBandFullScreen::hides
                               ? GF_WINDOW_BAND_FULL_SCREEN_HIDES
                               : GF_WINDOW_BAND_FULL_SCREEN_STAYS;
    complete.frequency = snapshot.frequency;
    complete.flash_changes = snapshot.flash_changes;
    complete.cycle_frames = snapshot.cycle_frames;
    // Leaves the caller's struct_size and any later bytes (A-gf-abi-03-F9).
    write_record(complete, report);
    return GF_STATUS_OK;
}

gf_status windows_band_report_body(gf_object window, gf_window_band_report* report) {
    const gf_status reported = report_window_band(window, report);
    return reported;
}

gf_status GF_CALL windows_band_report(gf_object window, gf_window_band_report* report) noexcept {
    return call_slot(band_report_slot, windows_band_report_body, window, report);
}

gf_status set_window_corner_preference(gf_object window, gf_window_corner_preference preference) {
    WindowCornerPreference asked = WindowCornerPreference::system;
    switch (preference) {
    case GF_WINDOW_CORNER_PREFERENCE_SYSTEM: asked = WindowCornerPreference::system; break;
    case GF_WINDOW_CORNER_PREFERENCE_SQUARE: asked = WindowCornerPreference::square; break;
    case GF_WINDOW_CORNER_PREFERENCE_ROUND: asked = WindowCornerPreference::round; break;
    default:
        return fail(GF_STATUS_INVALID_ARGUMENT, set_corner_preference_slot,
                    "the corner preference is not a gf_window_corner_preference");
    }
    Window* found = nullptr;
    const gf_status located = find_band_window(window, set_corner_preference_slot, found);
    if (located != GF_STATUS_OK) {
        return located;
    }
    (*found).set_corner_preference(asked);
    return GF_STATUS_OK;
}

gf_status windows_set_corner_preference_body(gf_object window, gf_window_corner_preference preference) {
    const gf_status set = set_window_corner_preference(window, preference);
    return set;
}

gf_status GF_CALL windows_set_corner_preference(gf_object window, gf_window_corner_preference preference) noexcept {
    return call_slot(set_corner_preference_slot, windows_set_corner_preference_body, window, preference);
}

gf_status report_content_rect(gf_object window, gf_logical_rect* rect) {
    if (rect == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, content_rect_slot, "content_rect needs a rectangle output");
    }
    Window* found = nullptr;
    const gf_status located = find_band_window(window, content_rect_slot, found);
    if (located != GF_STATUS_OK) {
        return located;
    }
    const Rect content = (*found).content_rect();
    *rect = gf_logical_rect{content.x, content.y, content.width, content.height};
    return GF_STATUS_OK;
}

gf_status windows_content_rect_body(gf_object window, gf_logical_rect* rect) {
    const gf_status reported = report_content_rect(window, rect);
    return reported;
}

gf_status GF_CALL windows_content_rect(gf_object window, gf_logical_rect* rect) noexcept {
    return call_slot(content_rect_slot, windows_content_rect_body, window, rect);
}

std::uint32_t abi_corner_shape(WindowCornerShape shape) noexcept {
    switch (shape) {
    case WindowCornerShape::square: return GF_WINDOW_CORNER_SHAPE_SQUARE;
    case WindowCornerShape::rounded: return GF_WINDOW_CORNER_SHAPE_ROUNDED;
    case WindowCornerShape::unknown: return 0U;
    }
    return 0U;
}

gf_window_corner_preference abi_corner_preference(WindowCornerPreference preference) noexcept {
    switch (preference) {
    case WindowCornerPreference::system: return GF_WINDOW_CORNER_PREFERENCE_SYSTEM;
    case WindowCornerPreference::square: return GF_WINDOW_CORNER_PREFERENCE_SQUARE;
    case WindowCornerPreference::round: return GF_WINDOW_CORNER_PREFERENCE_ROUND;
    }
    return GF_WINDOW_CORNER_PREFERENCE_SYSTEM;
}

gf_status report_window_shaping(gf_object window, gf_window_shaping* report) {
    if (report == nullptr || (*report).struct_size < shaping_size_1_5) {
        return fail(GF_STATUS_INVALID_ARGUMENT, shaping_slot, "shaping needs a 1.5 report record");
    }
    Window* found = nullptr;
    const gf_status located = find_band_window(window, shaping_slot, found);
    if (located != GF_STATUS_OK) {
        return located;
    }
    const WindowShapingReport shaping = (*found).shaping_report();
    gf_window_shaping_flags flags = 0U;
    if (shaping.host_attached) flags = flags | GF_WINDOW_SHAPING_HOST_ATTACHED;
    if (shaping.corners_reported) flags = flags | GF_WINDOW_SHAPING_CORNERS_REPORTED;
    if (shaping.state_reported) flags = flags | GF_WINDOW_SHAPING_STATE_REPORTED;
    if (shaping.native_border) flags = flags | GF_WINDOW_SHAPING_NATIVE_BORDER;
    if (shaping.full_screen) flags = flags | GF_WINDOW_SHAPING_FULL_SCREEN;
    if (shaping.corners.complied) flags = flags | GF_WINDOW_SHAPING_COMPLIED;
    std::uint32_t shapes = 0U;
    if (shaping.square_corners) shapes = shapes | GF_WINDOW_CORNER_SHAPE_SQUARE;
    if (shaping.rounded_corners) shapes = shapes | GF_WINDOW_CORNER_SHAPE_ROUNDED;
    gf_window_shaping complete{};
    complete.struct_size = sizeof(gf_window_shaping);
    complete.flags = flags;
    complete.preference = abi_corner_preference(shaping.preference);
    complete.corner_shapes = shapes;
    complete.corner_shape = abi_corner_shape(shaping.corners.shape);
    complete.corner_radius = shaping.corners.shape == WindowCornerShape::rounded ? shaping.corners.radius : 0.0;
    // Leaves the caller's struct_size and any later bytes (A-gf-abi-03-F9).
    write_record(complete, report);
    return GF_STATUS_OK;
}

gf_status windows_shaping_body(gf_object window, gf_window_shaping* report) {
    const gf_status reported = report_window_shaping(window, report);
    return reported;
}

gf_status GF_CALL windows_shaping(gf_object window, gf_window_shaping* report) noexcept {
    return call_slot(shaping_slot, windows_shaping_body, window, report);
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
    .key_state = &windows_key_state,
    .show_modal = &windows_show_modal,
    .show_owned = &windows_show_owned,
    .window_properties = &windows_window_properties,
    .focused_control = &windows_focused_control,
    .declare_band = &windows_declare_band,
    .undeclare_band = &windows_undeclare_band,
    .set_band_appearance = &windows_set_band_appearance,
    .band_report = &windows_band_report,
    .set_corner_preference = &windows_set_corner_preference,
    .content_rect = &windows_content_rect,
    .shaping = &windows_shaping,
    .pointer_location = &windows_pointer_location,
    .show_business_card = &windows_show_business_card,
    .set_mdi_container = &windows_set_mdi_container,
    .mdi_container = &windows_mdi_container,
    .mdi_child_count = &windows_mdi_child_count,
    .mdi_child_at = &windows_mdi_child_at,
    .active_mdi_child = &windows_active_mdi_child,
    .activate_mdi_child = &windows_activate_mdi_child,
    .layout_mdi = &windows_layout_mdi,
    .set_mdi_children_minimized_anchor_bottom = &windows_set_mdi_children_minimized_anchor_bottom,
    .mdi_children_minimized_anchor_bottom = &windows_mdi_children_minimized_anchor_bottom,
    .set_main_menu_strip = &windows_set_main_menu_strip,
    .main_menu_strip = &windows_main_menu_strip,
    .overflow_records = &windows_overflow_records,
    .unanswered_overflow_count = &windows_unanswered_overflow_count,
    .safe_insets = &windows_safe_insets,
};

} // namespace

const gf_windows_table& windows_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
