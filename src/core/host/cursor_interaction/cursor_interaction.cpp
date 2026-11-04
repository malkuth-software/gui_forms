#include "gui_forms/host/cursor_interaction/cursor_interaction.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/window.hpp"
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

namespace gui_forms {
struct CursorLeaseState final {
    // UI-thread observer, cleared only after native cleanup. Host retains this
    // record too, so a mistaken wrong-thread lease destructor cannot destroy it.
    HostServices* owner{};
    std::thread::id thread{};
    CursorLeaseSnapshot snapshot{};
    bool cleanup_retried{};
};
namespace {
std::atomic<std::uint64_t> next_cursor_window_id{1};
CursorStatus terminal_status(const CursorLeaseSnapshot& snapshot) {
    if (!snapshot.restoration.accepted()) { return snapshot.restoration; }
    return snapshot.cause;
}
}
HostServiceStatus CursorStatus::host_status() const noexcept {
    switch (error) {
    case CursorError::none: return {};
    case CursorError::unsupported: return {HostServiceError::unsupported};
    case CursorError::wrong_thread: return {HostServiceError::wrong_thread};
    case CursorError::invalid_coordinate:
    case CursorError::stale_window:
    case CursorError::stale_metrics: return {HostServiceError::invalid_argument};
    case CursorError::closing: return {HostServiceError::after_shutdown};
    case CursorError::denied:
    case CursorError::busy:
    case CursorError::revoked:
    case CursorError::native_failure: return {HostServiceError::backend_failure};
    }
    // An error outside the enumeration is the backend's failure.
    return {HostServiceError::backend_failure};
}
CursorHiddenLease::CursorHiddenLease(std::shared_ptr<CursorLeaseState> state) : state_(std::move(state)) {}
CursorHiddenLease::~CursorHiddenLease() { static_cast<void>(release()); }
CursorHiddenLease::CursorHiddenLease(CursorHiddenLease&& other) noexcept : state_(std::move(other.state_)) {}
CursorHiddenLease& CursorHiddenLease::operator=(CursorHiddenLease&& other) noexcept {
    if (this != &other) {
        const CursorStatus released = release();
        if (released.error != CursorError::wrong_thread) { state_ = std::move(other.state_); }
    }
    return *this;
}
CursorStatus CursorHiddenLease::release() noexcept {
    if (!state_) { return {}; }
    CursorLeaseState& state = *state_;
    if (state.thread != std::this_thread::get_id()) { return {CursorError::wrong_thread}; }
    if (state.snapshot.phase != CursorLeasePhase::active) {
        const CursorStatus result = terminal_status(state.snapshot);
        return result;
    }
    if (state.owner == nullptr) { return {CursorError::stale_window}; }
    const CursorStatus result = (*state.owner).release_cursor_lease(state);
    return result;
}
CursorLeaseSnapshot CursorHiddenLease::snapshot() const noexcept {
    if (!state_) { return {}; }
    const CursorLeaseState& state = *state_;
    if (state.thread != std::this_thread::get_id()) {
        return {CursorLeasePhase::empty, {CursorError::wrong_thread}, {}};
    }
    return state.snapshot;
}
HostServices::~HostServices() {
    // Concrete native adapters must shutdown before their native fields die.
    // Never call virtual/native restoration from the base destructor.
    if (cursor_lease_) {
        CursorLeaseState& state = *cursor_lease_;
        if (state.snapshot.phase == CursorLeasePhase::active) {
            state.snapshot = {CursorLeasePhase::revoked, {CursorError::closing}, {CursorError::native_failure}};
        }
        state.owner = nullptr;
    }
}
CursorCapabilities HostServices::cursor_capabilities_impl() const noexcept { return {}; }
CursorStatus HostServices::cursor_authority_impl(const Window&, bool) const noexcept { return {CursorError::unsupported}; }
CursorStatus HostServices::hide_cursor_impl() noexcept { return {CursorError::unsupported}; }
CursorStatus HostServices::restore_cursor_impl() noexcept { return {CursorError::unsupported}; }
CursorStatus HostServices::warp_cursor_impl(int, int) noexcept { return {CursorError::unsupported}; }
CursorCapabilities HostServices::cursor_capabilities() const noexcept {
    if (std::this_thread::get_id() != ui_thread_ || snapshot_.shutdown) { return {}; }
    const CursorCapabilities result = cursor_capabilities_impl();
    return result;
}
CursorStatus HostServices::validate_cursor_request(const Window& window) const noexcept {
    if (std::this_thread::get_id() != ui_thread_) { return {CursorError::wrong_thread}; }
    if (snapshot_.shutdown) { return {CursorError::closing}; }
    if (window.host_services() != this) { return {CursorError::stale_window}; }
    if (!window.active() || !modal_stack_.empty()) { return {CursorError::denied}; }
    return {};
}
CursorLeaseResult HostServices::begin_cursor_hidden(Window& window) {
    const CursorStatus request = validate_cursor_request(window);
    if (!request.accepted()) { return {{}, request}; }
    const CursorCapabilities capabilities = cursor_capabilities_impl();
    if (!capabilities.hide) { return {{}, {CursorError::unsupported}}; }
    const CursorMetrics metrics = window.cursor_metrics();
    if (metrics.window_id == 0 || metrics.generation == 0) { return {{}, {CursorError::stale_window}}; }
    if (!std::isfinite(metrics.client_dip.width) || !std::isfinite(metrics.client_dip.height) ||
        metrics.client_dip.width <= 0 || metrics.client_dip.height <= 0 ||
        !std::isfinite(metrics.scale) || metrics.scale <= 0) { return {{}, {CursorError::invalid_coordinate}}; }
    if (cursor_lease_) {
        const CursorLeaseSnapshot previous = (*cursor_lease_).snapshot;
        if (previous.phase == CursorLeasePhase::active) { return {{}, {CursorError::busy}}; }
        if (!previous.restoration.accepted()) { return {{}, previous.restoration}; }
    }
    const CursorStatus authority = cursor_authority_impl(window, true);
    if (!authority.accepted()) { return {{}, authority}; }
    std::shared_ptr<CursorLeaseState> state{};
    try { state = std::make_shared<CursorLeaseState>(); }
    catch (const std::bad_alloc&) { return {{}, {CursorError::native_failure}}; }
    const CursorStatus hidden = hide_cursor_impl();
    if (!hidden.accepted()) { return {{}, hidden}; }
    (*state).owner = this;
    (*state).thread = ui_thread_;
    (*state).snapshot.phase = CursorLeasePhase::active;
    cursor_lease_ = state;
    CursorLeaseResult result{CursorHiddenLease(std::move(state)), {}};
    return result;
}
CursorStatus HostServices::release_cursor_lease(CursorLeaseState& state) noexcept {
    if (std::this_thread::get_id() != ui_thread_) { return {CursorError::wrong_thread}; }
    if (cursor_lease_.get() != &state) { return {CursorError::revoked}; }
    state.snapshot.phase = CursorLeasePhase::released;
    state.snapshot.cause = {};
    state.snapshot.restoration = restore_cursor_impl();
    state.owner = nullptr;
    return state.snapshot.restoration;
}
void HostServices::revoke_cursor_interaction(CursorError cause) noexcept {
    if (std::this_thread::get_id() != ui_thread_ || !cursor_lease_) { return; }
    CursorLeaseState& state = *cursor_lease_;
    if (state.snapshot.phase == CursorLeasePhase::active) {
        state.snapshot.phase = CursorLeasePhase::revoked;
        state.snapshot.cause = {cause};
        state.snapshot.restoration = restore_cursor_impl();
        state.owner = nullptr;
    }
    if (cause == CursorError::closing && !state.snapshot.restoration.accepted() && !state.cleanup_retried) {
        state.cleanup_retried = true;
        // Retry while native ownership is still valid, including a first
        // failure during this shutdown. Preserve the original failure.
        static_cast<void>(restore_cursor_impl());
    }
}
CursorLeaseSnapshot HostServices::cursor_interaction_snapshot() const noexcept {
    if (std::this_thread::get_id() != ui_thread_) { return {CursorLeasePhase::empty, {CursorError::wrong_thread}, {}}; }
    if (!cursor_lease_) { return {}; }
    return (*cursor_lease_).snapshot;
}
bool HostServices::cursor_hidden() const noexcept {
    if (std::this_thread::get_id() != ui_thread_) { return false; }
    const bool hidden = cursor_lease_ && (*cursor_lease_).snapshot.phase == CursorLeasePhase::active;
    return hidden;
}
CursorStatus HostServices::warp_cursor(Window& window, CursorMetrics metrics, Point target) {
    const CursorStatus request = validate_cursor_request(window);
    if (!request.accepted()) { return request; }
    const CursorCapabilities capabilities = cursor_capabilities_impl();
    if (!capabilities.warp) { return {CursorError::unsupported}; }
    const CursorMetrics current = window.cursor_metrics();
    if (metrics.window_id == 0 || metrics.window_id != current.window_id) { return {CursorError::stale_window}; }
    if (metrics.generation == 0 || metrics.generation != current.generation ||
        metrics.client_dip != current.client_dip || metrics.scale != current.scale) { return {CursorError::stale_metrics}; }
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(current.scale) || current.scale <= 0 ||
        !std::isfinite(current.client_dip.width) || !std::isfinite(current.client_dip.height) ||
        target.x < 0 || target.y < 0 || target.x >= current.client_dip.width || target.y >= current.client_dip.height) {
        return {CursorError::invalid_coordinate};
    }
    const double x = std::floor(target.x * current.scale);
    const double y = std::floor(target.y * current.scale);
    if (!std::isfinite(x) || !std::isfinite(y) || x > std::numeric_limits<int>::max() || y > std::numeric_limits<int>::max()) {
        return {CursorError::invalid_coordinate};
    }
    const CursorStatus authority = cursor_authority_impl(window, false);
    if (!authority.accepted()) { return authority; }
    const CursorStatus result = warp_cursor_impl(static_cast<int>(x), static_cast<int>(y));
    return result;
}
void Window::initialize_cursor_identity() noexcept {
    std::uint64_t identity = next_cursor_window_id.load();
    while (identity != 0) {
        const std::uint64_t next = identity == std::numeric_limits<std::uint64_t>::max() ? 0 : identity + 1;
        const bool assigned = next_cursor_window_id.compare_exchange_weak(identity, next);
        if (assigned) { cursor_window_id_ = identity; return; }
    }
    cursor_window_id_ = 0; // Exhaustion refuses interaction rather than reusing identity.
}
void Window::invalidate_cursor_interaction(CursorError cause) noexcept {
    if (host_services_ != nullptr) { (*host_services_).revoke_cursor_interaction(cause); }
    if (cursor_metrics_generation_ != 0) { ++cursor_metrics_generation_; }
}
CursorMetrics Window::cursor_metrics() const noexcept {
    if (std::this_thread::get_id() != root_thread_) { return {}; }
    return {cursor_window_id_, cursor_metrics_generation_, client_size_, scale_};
}
CursorCapabilities Window::cursor_capabilities() const noexcept {
    if (std::this_thread::get_id() != root_thread_) { return {}; }
    if (host_services_ == nullptr) { return {}; }
    const CursorCapabilities result = (*host_services_).cursor_capabilities();
    return result;
}
CursorLeaseResult Window::begin_cursor_hidden() {
    if (std::this_thread::get_id() != root_thread_) { return {{}, {CursorError::wrong_thread}}; }
    if (host_services_ == nullptr) { return {{}, {CursorError::stale_window}}; }
    CursorLeaseResult result = (*host_services_).begin_cursor_hidden(*this);
    return result;
}
CursorStatus Window::warp_cursor(CursorMetrics metrics, Point target) {
    if (std::this_thread::get_id() != root_thread_) { return {CursorError::wrong_thread}; }
    if (host_services_ == nullptr) { return {CursorError::stale_window}; }
    const CursorStatus result = (*host_services_).warp_cursor(*this, metrics, target);
    return result;
}
}
