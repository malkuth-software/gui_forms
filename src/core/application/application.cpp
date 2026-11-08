#include "application_state.hpp"
#include "splashscreen_surface.hpp"
#include "gui_forms/text/types/text_types.hpp"

#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {
bool valid_text(std::string_view value, std::size_t limit, bool allow_empty) {
    const bool valid = (allow_empty || !value.empty()) && value.size() <= limit &&
        value.find('\0') == std::string_view::npos && validate_utf8(value).valid();
    return valid;
}
bool valid_size(Size size) noexcept {
    const bool valid = std::isfinite(size.width) && std::isfinite(size.height) &&
        size.width >= 1.0 && size.height >= 1.0 &&
        size.width <= 32768.0 && size.height <= 32768.0;
    return valid;
}

// The window's state when it is ready and open on this thread; otherwise
// empty, with the reason in `status`: after_shutdown once the state is gone
// or closed, wrong_thread off its thread, backend_failure before ready.
std::shared_ptr<detail::ApplicationWindowState> ready_state(
    const std::weak_ptr<detail::ApplicationWindowState>& weak, HostServiceStatus& status) {
    status = {};
    std::shared_ptr<detail::ApplicationWindowState> state = weak.lock();
    if (!state) { status.error = HostServiceError::after_shutdown; return {}; }
    if ((*state).thread != std::this_thread::get_id()) {
        status.error = HostServiceError::wrong_thread;
        return {};
    }
    if ((*state).closed) { status.error = HostServiceError::after_shutdown; return {}; }
    if (!(*state).ready) { status.error = HostServiceError::backend_failure; return {}; }
    return state;
}

// One of a window state's operations, moved out of the state for its call
// and moved back on every exit (an exception included) unless the state was
// closed or given another operation meanwhile. While it is out, a nested
// request for the same operation finds none and reports unsupported.
class HeldOperation final {
public:
    HeldOperation(detail::ApplicationWindowState& state,
                  std::function<void()> detail::ApplicationWindowState::* operation) noexcept
        : state_(state), operation_(operation), action_(std::move(state.*operation)) {
        state_.*operation_ = nullptr;
    }
    ~HeldOperation() {
        if (!state_.closed && !(state_.*operation_)) state_.*operation_ = std::move(action_);
    }
    HeldOperation(const HeldOperation&) = delete;
    HeldOperation& operator=(const HeldOperation&) = delete;

    void invoke() const { action_(); }

private:
    detail::ApplicationWindowState& state_;
    std::function<void()> detail::ApplicationWindowState::* operation_;
    std::function<void()> action_;
};

HostServiceStatus request(const std::weak_ptr<detail::ApplicationWindowState>& weak,
                          std::function<void()> detail::ApplicationWindowState::* operation,
                          bool hiding) {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationWindowState> state = ready_state(weak, status);
    if (!state) return status;
    if (hiding && !(*state).can_hide) return {HostServiceError::invalid_argument};
    if (!((*state).*operation)) return {HostServiceError::unsupported};
    try {
        // A native request may synchronously close and clear its state. The
        // callable is moved out (never copied: a copy allocates) and kept
        // alive until its invocation has returned, then put back unless the
        // call closed the window or installed another.
        HeldOperation held{*state, operation};
        held.invoke();
    } catch (...) {
        return {HostServiceError::backend_failure};
    }
    return {};
}

// The index of the entry named `stable_id`, or windows.size() when no entry
// has it.
std::size_t entry_index(const std::vector<ApplicationWindow>& windows, std::string_view stable_id) {
    std::size_t index = 0U;
    while (index < windows.size() && windows[index].stable_id != stable_id) ++index;
    return index;
}

// Whether the owners of windows[index] form a chain of listed entries that
// ends at an entry without an owner, and a floating entry's own owner is the
// primary window. A chain longer than the list repeats an entry: an
// ownership cycle, refused.
bool owner_chain_valid(const std::vector<ApplicationWindow>& windows, std::size_t index) {
    const ApplicationWindow& entry = windows[index];
    std::string_view owner = entry.owner_id;
    for (std::size_t depth = 0U; !owner.empty(); ++depth) {
        if (depth >= windows.size()) return false;
        const std::size_t parent = entry_index(windows, owner);
        if (parent == windows.size()) return false;
        const bool floating_owner = depth == 0U && entry.floating != HostWindowFloating::none;
        if (floating_owner && !detail::primary_window_entry(windows[parent])) return false;
        owner = windows[parent].owner_id;
    }
    return true;
}
}

namespace detail {
bool valid_window_entry(const ApplicationWindow& entry) {
    if (!entry.model) return false;
    // UTF-8 without NUL; the stable id is not empty and is not its owner's.
    const bool texts_valid = valid_text(entry.stable_id, 1024U, false) &&
        valid_text(entry.owner_id, 1024U, true) &&
        valid_text(entry.options.title, 65536U, true);
    if (!texts_valid || entry.owner_id == entry.stable_id) return false;
    const Size initial = entry.options.initial_size;
    const Size minimum = entry.options.minimum_size;
    if (!valid_size(initial) || !valid_size(minimum)) return false;
    if (minimum.width > initial.width || minimum.height > initial.height) return false;
    // A popup has an owner, is neither a tool nor a floating window, and
    // its origin is a finite desktop point in range.
    if (entry.popup.has_value()) {
        const DesktopLogicalPoint origin = (*entry.popup).origin;
        const double bound = HostWindowing::maximum_desktop_coordinate;
        const bool origin_valid = std::isfinite(origin.x) && std::isfinite(origin.y) &&
                                  std::abs(origin.x) <= bound && std::abs(origin.y) <= bound;
        // A Splash takes no input, so it never takes focus.
        const bool focus_valid = !host_transient_is_splash((*entry.popup).kind) ||
                                 (*entry.popup).focus == HostPopupFocus::passive;
        const bool popup_valid = !entry.tool_window && entry.floating == HostWindowFloating::none &&
                                 !entry.owner_id.empty() && origin_valid && focus_valid;
        return popup_valid;
    }
    // A floating window has an owner and is not a tool window in the host's
    // sense (HostWindowFloating).
    if (entry.floating == HostWindowFloating::none) return true;
    const bool floating_valid = !entry.tool_window && !entry.owner_id.empty();
    return floating_valid;
}

bool primary_window_entry(const ApplicationWindow& entry) noexcept {
    const bool primary = entry.owner_id.empty() && !entry.tool_window;
    return primary;
}

void attach_application_window(SurfaceRoot& model, const std::shared_ptr<ApplicationWindowState>& state) noexcept {
    model.application_window_ = state;
    // The first window of the run is ready: a splashscreen shown before
    // Application::create hands over to it (its later windows find none).
    hand_over_splashscreen();
}

void revoke_window_state(ApplicationWindowState& state) noexcept {
    state.closed = true;
    state.ready = false;
    state.request_close = {};
    state.show = {};
    state.hide = {};
    state.toggle_full_screen = {};
    state.set_title = {};
    state.request_client_size = {};
    state.frame_changed = {};
}

namespace {
// Routes one HostWindowing frame report to the window's frame_changed.
struct RunFrameRouter final {
    std::weak_ptr<ApplicationRunState> run{};
    void operator()(const HostWindowFrameEvent& event) const {
        const std::shared_ptr<ApplicationRunState> state = run.lock();
        if (!state || (*state).closed) return;
        std::shared_ptr<ApplicationWindowState> target{};
        for (std::size_t index = 0U; index < (*state).windows.size(); ++index) {
            if ((*state).windows[index].id == event.window) {
                target = (*state).windows[index].state.lock();
                break;
            }
        }
        if (!target || (*target).closed || !(*target).frame_changed) return;
        // A handler may close this window; keep the callable alive through
        // the call.
        const std::function<void(const HostWindowFrameEvent&)> handler = (*target).frame_changed;
        handler(event);
    }
};
} // namespace

void attach_run_windowing(
    const std::shared_ptr<ApplicationRunState>& run, HostWindowing& windowing,
    std::function<ApplicationWindowOpenResult(ApplicationWindow&&)> open_window) {
    (*run).windowing = &windowing;
    (*run).open_window = std::move(open_window);
    (*run).windows.reserve(HostWindowing::maximum_windows);
    (*run).frame_subscription = windowing.frame_changed().subscribe(RunFrameRouter{run});
}

void detach_run_windowing(ApplicationRunState& run) noexcept {
    run.frame_subscription.disconnect();
    run.closed = true;
    run.windowing = nullptr;
    run.open_window = {};
    run.windows.clear();
}

void register_run_window(const std::shared_ptr<ApplicationWindowState>& state) {
    const std::shared_ptr<ApplicationRunState> run = (*state).run.lock();
    if (!run || (*run).closed || (*run).windowing == nullptr) return;
    const std::optional<HostWindowId> id = (*(*run).windowing).find((*state).stable_id);
    if (!id.has_value()) return;
    (*state).id = *id;
    for (std::size_t index = 0U; index < (*run).windows.size(); ++index) {
        if ((*run).windows[index].id == *id) return;
    }
    // Bounded by the windowing table: at most maximum_windows identities are
    // live, and closed windows are removed below.
    (*run).windows.push_back({*id, state});
}

void unregister_run_window(ApplicationWindowState& state) noexcept {
    const std::shared_ptr<ApplicationRunState> run = state.run.lock();
    if (!run) return;
    for (std::size_t index = 0U; index < (*run).windows.size(); ++index) {
        if ((*run).windows[index].id != state.id) continue;
        (*run).windows.erase((*run).windows.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
}

void PolicyVisibility::operator()() const {
    const std::shared_ptr<ApplicationWindowState> target = state.lock();
    if (!target || (*target).closed) return;
    const std::shared_ptr<ApplicationRunState> run = (*target).run.lock();
    if (!run || (*run).closed || (*run).windowing == nullptr) return;
    static_cast<void>((*(*run).windowing).set_visible((*target).id, visible));
}
} // namespace detail

ApplicationWindowHandle::ApplicationWindowHandle(
    std::weak_ptr<detail::ApplicationWindowState> state) noexcept : state_(std::move(state)) {}

bool ApplicationWindowHandle::active() const noexcept {
    const std::shared_ptr<detail::ApplicationWindowState> state = state_.lock();
    const bool active = state && (*state).thread == std::this_thread::get_id() &&
        (*state).ready && !(*state).closed;
    return active;
}
HostServiceStatus ApplicationWindowHandle::request_close() const {
    const HostServiceStatus result = request(state_, &detail::ApplicationWindowState::request_close, false);
    return result;
}
HostServiceStatus ApplicationWindowHandle::show() const {
    const HostServiceStatus result = request(state_, &detail::ApplicationWindowState::show, false);
    return result;
}
HostServiceStatus ApplicationWindowHandle::hide() const {
    const HostServiceStatus result = request(state_, &detail::ApplicationWindowState::hide, true);
    return result;
}

HostServiceStatus ApplicationWindowHandle::toggle_full_screen() const {
    const HostServiceStatus result = request(state_, &detail::ApplicationWindowState::toggle_full_screen, false);
    return result;
}

namespace {

// The current application (Application::current): set by create, cleared
// by close or destruction. Atomic so any thread may ask; the application
// itself is used on its own thread only.
std::atomic<Application*> current_application{nullptr};

// The anatomies the application's theme compiles against: the stock ones,
// then the application's own. Fills `storage` only when there are both.
std::span<const ControlAnatomy> theme_anatomies(std::span<const ControlAnatomy> own,
                                                std::vector<ControlAnatomy>& storage) {
    const std::span<const ControlAnatomy> stock = stock_control_anatomies();
    if (own.empty()) return stock;
    storage.reserve(stock.size() + own.size());
    storage.insert(storage.end(), stock.begin(), stock.end());
    storage.insert(storage.end(), own.begin(), own.end());
    const std::span<const ControlAnatomy> both(storage);
    return both;
}

} // namespace

ApplicationCreateResult Application::create(ApplicationConfig config) {
    ApplicationCreateResult result{};
    if (current_application.load(std::memory_order_acquire) != nullptr) {
        result.error = ApplicationCreateError::already_exists;
        return result;
    }
    std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>(config.image_limits);
    // The theme compiles here, once (APP_WINDOW_RULES_DESIGN K.1): the base,
    // then the program's packages. The process default's base alone is
    // already built (default_compiled_theme).
    if (config.theme.packages.empty() && config.theme.base == default_theme_base) {
        (*resources).set_theme(default_compiled_theme());
    } else {
        std::vector<ControlAnatomy> storage;
        ThemeCompileOptions options{};
        options.base = config.theme.base;
        options.controls = theme_anatomies(config.theme.controls, storage);
        const ThemeCompileResult compiled =
            (*resources).compile_theme_packages(config.theme.packages, config.theme.library, options);
        result.findings.assign(compiled.diagnostics().begin(), compiled.diagnostics().end());
        if (!compiled.succeeded()) {
            result.error = ApplicationCreateError::theme_refused;
            return result;
        }
        (*resources).set_theme(compiled.theme());
    }
    const PresentationSettings presentation = config.presentation.value_or(PresentationSettings{});
    std::unique_ptr<Application> application(new Application(std::move(resources), presentation));
    Application* expected = nullptr;
    if (!current_application.compare_exchange_strong(expected, application.get(), std::memory_order_acq_rel)) {
        // Another create won since the check: this one was never current.
        (*application).closed_ = true;
        result.error = ApplicationCreateError::already_exists;
        result.findings.clear();
        return result;
    }
    result.application = std::move(application);
    return result;
}

Application* Application::current() noexcept {
    Application* const application = current_application.load(std::memory_order_acquire);
    return application;
}

Application::Application(std::shared_ptr<ApplicationResources> resources, PresentationSettings presentation)
    : resources_(std::move(resources)),
      presentation_(presentation),
      key_bindings_(std::make_shared<KeyBindings>()),
      tool_tips_shown_(std::make_shared<std::atomic<bool>>(true)) {}

Application::~Application() {
    if (window_count_ > 0U) {
        std::fprintf(stderr,
                     "GUI.Forms: the application was destroyed with %zu window(s) alive; they keep its theme "
                     "and resources and read nothing more from it\n",
                     window_count_);
    }
    // The windows forget the application: each keeps the resources and
    // tooltip switch it shares.
    Window* window = first_window_;
    while (window != nullptr) {
        Window* const next = (*window).next_in_application_;
        (*window).application_ = nullptr;
        (*window).previous_in_application_ = nullptr;
        (*window).next_in_application_ = nullptr;
        window = next;
    }
    if (!closed_) {
        Application* expected = this;
        static_cast<void>(current_application.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel));
    }
}

ApplicationResult Application::close() {
    if (closed_) return {};
    if (window_count_ > 0U) return {ApplicationError::windows_alive};
    closed_ = true;
    Application* expected = this;
    static_cast<void>(current_application.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel));
    return {};
}

void Application::join(Window& window) noexcept {
    window.application_ = this;
    window.previous_in_application_ = last_window_;
    window.next_in_application_ = nullptr;
    if (last_window_ != nullptr) {
        (*last_window_).next_in_application_ = &window;
    } else {
        first_window_ = &window;
    }
    last_window_ = &window;
    ++window_count_;
}

void Application::leave(Window& window) noexcept {
    Window* const previous = window.previous_in_application_;
    Window* const next = window.next_in_application_;
    if (previous != nullptr) {
        (*previous).next_in_application_ = next;
    } else {
        first_window_ = next;
    }
    if (next != nullptr) {
        (*next).previous_in_application_ = previous;
    } else {
        last_window_ = previous;
    }
    window.application_ = nullptr;
    window.previous_in_application_ = nullptr;
    window.next_in_application_ = nullptr;
    --window_count_;
}

void Application::set_presentation_settings(PresentationSettings settings) {
    validate_presentation_settings(settings);
    if (presentation_ == settings) return;
    presentation_ = settings;
    // A window's handlers may destroy windows: each is reached through its
    // lifetime record, taken before the first is told.
    std::vector<std::shared_ptr<detail::WindowLifetime>> windows;
    windows.reserve(window_count_);
    for (Window* window = first_window_; window != nullptr; window = (*window).next_in_application_) {
        windows.push_back((*window).lifetime_);
    }
    for (const std::shared_ptr<detail::WindowLifetime>& lifetime : windows) {
        Window* const window = (*lifetime).window;
        if (window == nullptr || (*window).application_ != this) continue;
        (*window).set_presentation_settings(presentation_);
    }
}

void Application::set_theme(std::shared_ptr<const Theme> theme) {
    if (!theme) throw std::invalid_argument("GUI.Forms application theme may not be null");
    // The shared resources first: their refusal changes nothing.
    (*resources_).set_theme(theme);
    std::vector<std::shared_ptr<ApplicationResources>> own;
    for (Window* window = first_window_; window != nullptr; window = (*window).next_in_application_) {
        const std::shared_ptr<ApplicationResources> resources = (*window).resources();
        const bool listed = std::find(own.begin(), own.end(), resources) != own.end();
        if (resources != resources_ && !listed) own.push_back(resources);
    }
    for (const std::shared_ptr<ApplicationResources>& resources : own) (*resources).set_theme(theme);
}

void Application::set_tool_tips_shown(bool shown) noexcept {
    (*tool_tips_shown_).store(shown, std::memory_order_relaxed);
}

bool Application::tool_tips_shown() const noexcept {
    const bool shown = (*tool_tips_shown_).load(std::memory_order_relaxed);
    return shown;
}

ApplicationResult Application::validate(const std::vector<ApplicationWindow>& windows) {
    if (windows.empty() || windows.size() > maximum_windows) {
        return {ApplicationError::invalid_argument};
    }
    std::size_t primary_count = 0U;
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        const ApplicationWindow& entry = windows[index];
        ApplicationResult invalid{ApplicationError::invalid_argument};
        invalid.window_index = index;
        if (!detail::valid_window_entry(entry)) return invalid;
        // A popup opens during the run only (Window::open_native_popup).
        if (entry.popup.has_value()) return invalid;
        if (!(*entry.model).check_access()) {
            invalid.error = ApplicationError::wrong_thread;
            return invalid;
        }
        if (detail::primary_window_entry(entry)) {
            ++primary_count;
            if (!entry.options.initially_visible || entry.options.hide_on_close) return invalid;
        }
        // Stable ids are unique: an earlier entry with this one refuses it.
        if (entry_index(windows, entry.stable_id) < index) return invalid;
        if (!owner_chain_valid(windows, index)) return invalid;
    }
    if (primary_count != 1U) return {ApplicationError::invalid_argument};
    return {};
}
HostServiceStatus ApplicationWindowHandle::set_title(const std::string_view title) const {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationWindowState> state = ready_state(state_, status);
    if (!state) return status;
    if (!valid_text(title, 65536U, true)) return {HostServiceError::invalid_argument};
    if (!(*state).set_title) return {HostServiceError::unsupported};
    try {
        const std::function<HostServiceStatus(std::string_view)> action = (*state).set_title;
        const HostServiceStatus result = action(title);
        return result;
    } catch (...) { return {HostServiceError::backend_failure}; }
}
HostServiceStatus ApplicationWindowHandle::request_client_size(const Size client_size) const {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationWindowState> state = ready_state(state_, status);
    if (!state) return status;
    if (!valid_size(client_size)) return {HostServiceError::invalid_argument};
    if (!(*state).request_client_size) return {HostServiceError::unsupported};
    try {
        // The native request may resize the model synchronously; keep the
        // callable alive until it returns.
        const std::function<HostServiceStatus(Size)> action = (*state).request_client_size;
        const HostServiceStatus result = action(client_size);
        return result;
    } catch (...) { return {HostServiceError::backend_failure}; }
}

namespace {
// The windowing of the run a ready window `state` belongs to; null, with the
// reason in `status`, when that run has ended (after_shutdown) or the window
// has no identity in a run with windowing (unsupported). The pointer is the
// run's borrow of its adapter's windowing, valid until the run detaches it.
HostWindowing* run_windowing(const detail::ApplicationWindowState& state, HostServiceStatus& status) {
    status = {};
    const std::shared_ptr<detail::ApplicationRunState> run = state.run.lock();
    if (run && (*run).closed) { status.error = HostServiceError::after_shutdown; return nullptr; }
    if (!run || (*run).windowing == nullptr || !state.id.valid()) {
        status.error = HostServiceError::unsupported;
        return nullptr;
    }
    return (*run).windowing;
}

// The run when it is live and on this thread; otherwise empty, with the
// reason in `status`.
std::shared_ptr<detail::ApplicationRunState> live_run(
    const std::weak_ptr<detail::ApplicationRunState>& weak, HostServiceStatus& status) {
    status = {};
    std::shared_ptr<detail::ApplicationRunState> run = weak.lock();
    if (!run) { status.error = HostServiceError::after_shutdown; return {}; }
    if ((*run).thread != std::this_thread::get_id()) {
        status.error = HostServiceError::wrong_thread;
        return {};
    }
    if ((*run).closed) { status.error = HostServiceError::after_shutdown; return {}; }
    if ((*run).windowing == nullptr) { status.error = HostServiceError::unsupported; return {}; }
    return run;
}
} // namespace

HostWindowId ApplicationWindowHandle::id() const noexcept {
    const std::shared_ptr<detail::ApplicationWindowState> state = state_.lock();
    if (!state || (*state).closed || !(*state).ready) return {};
    return (*state).id;
}

ApplicationWindows ApplicationWindowHandle::windows() const noexcept {
    const std::shared_ptr<detail::ApplicationWindowState> state = state_.lock();
    if (!state || (*state).closed) return {};
    const std::shared_ptr<detail::ApplicationRunState> run = (*state).run.lock();
    if (!run) return {};
    const ApplicationWindows result = detail::ApplicationHandleAccess::make_windows(run);
    return result;
}

HostWindowFrameResult ApplicationWindowHandle::frame() const {
    HostWindowFrameResult result;
    const std::shared_ptr<detail::ApplicationWindowState> state = ready_state(state_, result.status);
    if (!state) return result;
    HostWindowing* windowing = run_windowing(*state, result.status);
    if (windowing == nullptr) return result;
    try {
        result = (*windowing).frame((*state).id);
    } catch (...) {
        result = {};
        result.status.error = HostServiceError::backend_failure;
    }
    return result;
}

HostServiceStatus ApplicationWindowHandle::request_frame(const Rect content) const {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationWindowState> state = ready_state(state_, status);
    if (!state) return status;
    HostWindowing* windowing = run_windowing(*state, status);
    if (windowing == nullptr) return status;
    try {
        // A native request may report the new frame synchronously and its
        // handlers may close this window; `state` is held through the call.
        status = (*windowing).request_frame((*state).id, content);
    } catch (...) { status.error = HostServiceError::backend_failure; }
    return status;
}

HostServiceStatus ApplicationWindowHandle::bring_to_front() const {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationWindowState> state = ready_state(state_, status);
    if (!state) return status;
    HostWindowing* windowing = run_windowing(*state, status);
    if (windowing == nullptr) return status;
    try {
        status = (*windowing).bring_to_front((*state).id);
    } catch (...) { status.error = HostServiceError::backend_failure; }
    return status;
}

ApplicationWindows::ApplicationWindows(std::weak_ptr<detail::ApplicationRunState> run) noexcept
    : run_(std::move(run)) {}

bool ApplicationWindows::active() const noexcept {
    const std::shared_ptr<detail::ApplicationRunState> run = run_.lock();
    const bool result = run && (*run).thread == std::this_thread::get_id() &&
        !(*run).closed && (*run).windowing != nullptr;
    return result;
}

HostCapabilities ApplicationWindows::capabilities() const {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationRunState> run = live_run(run_, status);
    if (!run) {
        const HostCapabilities none{HostCapabilities::current_protocol_version, "none",
                                    HostCapability::none};
        return none;
    }
    const HostCapabilities result = (*(*run).windowing).capabilities();
    return result;
}

ApplicationWindowOpenResult ApplicationWindows::open_window(ApplicationWindow&& window) const {
    ApplicationWindowOpenResult result;
    const std::shared_ptr<detail::ApplicationRunState> run = live_run(run_, result.status);
    if (!run) return result;
    if (!(*(*run).windowing).capabilities().supports(HostCapability::runtime_windows) ||
        !(*run).open_window) {
        result.status.error = HostServiceError::unsupported;
        return result;
    }
    // Before the native window exists (stage T5). A model already bound to
    // a host session keeps its declaration.
    if (window.options.non_opaque && window.model) static_cast<void>((*window.model).declare_non_opaque());
    try {
        // The open operation runs the new window's callbacks, which may
        // change the run; keep the callable alive through the call.
        const std::function<ApplicationWindowOpenResult(ApplicationWindow&&)> open =
            (*run).open_window;
        result = open(std::move(window));
    } catch (...) {
        result = {};
        result.status.error = HostServiceError::backend_failure;
    }
    return result;
}

HostServiceStatus ApplicationWindows::destroy_window(const ApplicationWindowHandle& window) const {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationRunState> run = live_run(run_, status);
    if (!run) return status;
    const std::shared_ptr<detail::ApplicationWindowState> state =
        detail::ApplicationHandleAccess::state(window);
    if (!state || (*state).closed) return {HostServiceError::after_shutdown};
    if ((*state).run.lock() != run || !(*state).id.valid()) {
        return {HostServiceError::invalid_argument};
    }
    try {
        status = (*(*run).windowing).destroy_window((*state).id);
    } catch (...) { status.error = HostServiceError::backend_failure; }
    return status;
}

HostScreenPointResult ApplicationWindows::pointer_position() const {
    HostScreenPointResult result;
    const std::shared_ptr<detail::ApplicationRunState> run = live_run(run_, result.status);
    if (!run) return result;
    try {
        result = (*(*run).windowing).pointer_position();
    } catch (...) {
        result = {};
        result.status.error = HostServiceError::backend_failure;
    }
    return result;
}

ApplicationWindowAtResult ApplicationWindows::window_at(const DesktopLogicalPoint point) const {
    ApplicationWindowAtResult result;
    const std::shared_ptr<detail::ApplicationRunState> run = live_run(run_, result.status);
    if (!run) return result;
    HostWindowAtResult native;
    try {
        native = (*(*run).windowing).window_at(point);
    } catch (...) {
        native = {};
        native.status.error = HostServiceError::backend_failure;
    }
    result.status = native.status;
    if (!native.status.accepted() || !native.window.valid()) return result;
    for (std::size_t index = 0U; index < (*run).windows.size(); ++index) {
        if ((*run).windows[index].id != native.window) continue;
        const std::shared_ptr<detail::ApplicationWindowState> state =
            (*run).windows[index].state.lock();
        if (!state || (*state).closed) break;
        result.found = true;
        result.window = detail::ApplicationHandleAccess::make(state);
        break;
    }
    return result;
}

HostMonitorResult ApplicationWindows::monitors() const {
    HostMonitorResult result;
    const std::shared_ptr<detail::ApplicationRunState> run = live_run(run_, result.status);
    if (!run) return result;
    try {
        result = (*(*run).windowing).query_monitors();
    } catch (...) {
        result = {};
        result.status.error = HostServiceError::backend_failure;
    }
    return result;
}

std::size_t ApplicationWindows::window_count() const noexcept {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationRunState> run = live_run(run_, status);
    if (!run) return 0U;
    const std::size_t result = (*(*run).windowing).window_count();
    return result;
}
} // namespace gui_forms
