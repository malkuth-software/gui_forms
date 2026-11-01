#include "application_state.hpp"
#include "gui_forms/text/types/text_types.hpp"

#include <atomic>
#include <cmath>
#include <utility>

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

HostServiceStatus request(const std::weak_ptr<detail::ApplicationWindowState>& weak,
                          std::function<void()> detail::ApplicationWindowState::* operation,
                          bool hiding) {
    HostServiceStatus status;
    const std::shared_ptr<detail::ApplicationWindowState> state = ready_state(weak, status);
    if (!state) return status;
    if (hiding && !(*state).can_hide) return {HostServiceError::invalid_argument};
    if (!((*state).*operation)) return {HostServiceError::unsupported};
    try {
        // A native request may synchronously close and clear its state. Keep
        // the active callable alive until its invocation has returned.
        const std::function<void()> action = (*state).*operation;
        action();
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

const std::shared_ptr<KeyBindings>& Application::key_bindings() {
    static const std::shared_ptr<KeyBindings> shared = std::make_shared<KeyBindings>();
    return shared;
}

namespace {
// The program's tooltip switch. Windows on any UI thread read it, so it is
// atomic; it orders nothing else.
std::atomic<bool> program_tool_tips_shown{true};
} // namespace

void Application::set_tool_tips_shown(bool shown) noexcept {
    program_tool_tips_shown.store(shown, std::memory_order_relaxed);
}

bool Application::tool_tips_shown() noexcept {
    const bool shown = program_tool_tips_shown.load(std::memory_order_relaxed);
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
