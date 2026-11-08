#include "headless_application.hpp"
#include "../../../core/application/application_state.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms::host {
namespace {
void append_rect(std::ostringstream& line, Rect rect) {
    line << rect.x << ',' << rect.y << ',' << rect.width << 'x' << rect.height;
}

HostMonitor default_monitor() {
    HostMonitor monitor;
    monitor.id = "headless.monitor.0";
    monitor.frame = {0.0, 0.0, 1920.0, 1080.0};
    // A task bar along the bottom edge.
    monitor.work_area = {0.0, 0.0, 1920.0, 1040.0};
    monitor.scale = 1.0;
    monitor.primary = true;
    return monitor;
}

// The run's native operations a window state reaches, bound to one window.
struct HeadlessShow final {
    HeadlessApplication* application{};
    HostWindowId window{};
    bool visible{};
    void operator()() const;
};
struct HeadlessRequestClose final {
    HeadlessApplication* application{};
    HostWindowId window{};
    void operator()() const;
};
struct HeadlessTitle final {
    HeadlessWindowing* windowing{};
    HostWindowId window{};
    HostServiceStatus operator()(std::string_view title) const {
        std::ostringstream line;
        line << "title id=" << window.value << " text=" << title;
        (*windowing).note(line.str());
        return {};
    }
};
struct HeadlessClientSize final {
    HeadlessApplication* application{};
    HostWindowId window{};
    HostServiceStatus operator()(Size client_size) const;
};
// The headless run owns no clock or worker; wakes are the test's business.
struct HeadlessWake final {
    void operator()() const noexcept {}
};
} // namespace

// The public operations above reach private members through these
// friends-of-the-class trampolines.
struct HeadlessApplicationAccess final {
    static void show(HeadlessApplication& application, HostWindowId window, bool visible) {
        application.show(window, visible);
    }
    static void request_close(HeadlessApplication& application, HostWindowId window) {
        application.request_close(window);
    }
    static HostServiceStatus resize(HeadlessApplication& application, HostWindowId window,
                                    Size client_size) {
        const HostServiceStatus status = application.resize(window, client_size);
        return status;
    }
};

namespace {
void HeadlessShow::operator()() const {
    HeadlessApplicationAccess::show(*application, window, visible);
}
void HeadlessRequestClose::operator()() const {
    HeadlessApplicationAccess::request_close(*application, window);
}
HostServiceStatus HeadlessClientSize::operator()(Size client_size) const {
    const HostServiceStatus status =
        HeadlessApplicationAccess::resize(*application, window, client_size);
    return status;
}
} // namespace

// HeadlessWindowing ---------------------------------------------------------

HeadlessWindowing::HeadlessWindowing(HeadlessApplication& application,
                                     HostCapabilities capabilities)
    : HostWindowing(std::move(capabilities)), application_(&application) {
    monitors_.push_back(default_monitor());
    natives_.reserve(maximum_windows);
    order_.reserve(maximum_windows);
}

void HeadlessWindowing::set_monitors(std::vector<HostMonitor> monitors) {
    if (monitors.empty()) return;
    monitors_ = std::move(monitors);
}

void HeadlessWindowing::add_foreign_window(Rect outer) {
    foreign_windows_.push_back(outer);
}

void HeadlessWindowing::note(std::string_view line) {
    trace_.append(line);
    trace_.push_back('\n');
}

HeadlessWindowing::NativeWindow* HeadlessWindowing::native(HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < natives_.size(); ++index) {
        if (natives_[index].id == window) return &natives_[index];
    }
    return nullptr;
}

const HeadlessWindowing::NativeWindow* HeadlessWindowing::native(HostWindowId window) const noexcept {
    for (std::size_t index = 0U; index < natives_.size(); ++index) {
        if (natives_[index].id == window) return &natives_[index];
    }
    return nullptr;
}

std::optional<Rect> HeadlessWindowing::native_content(HostWindowId window) const {
    const NativeWindow* entry = native(window);
    if (entry == nullptr) return std::nullopt;
    return (*entry).content;
}

bool HeadlessWindowing::native_visible(HostWindowId window) const {
    const NativeWindow* entry = native(window);
    const bool visible = entry != nullptr && (*entry).visible;
    return visible;
}

std::vector<HostWindowId> HeadlessWindowing::z_order() const {
    const std::vector<HostWindowId> result = order_;
    return result;
}

HostWindowFrame HeadlessWindowing::native_frame(HostWindowId window) const {
    HostWindowFrame frame;
    const NativeWindow* entry = native(window);
    if (entry == nullptr) return frame;
    frame.content = (*entry).content;
    frame.outer_measured = true;
    if (!(*entry).decorated) {
        frame.outer = (*entry).content;
        return frame;
    }
    frame.outer = {(*entry).content.x - border_width,
                   (*entry).content.y - title_bar_height - border_width,
                   (*entry).content.width + 2.0 * border_width,
                   (*entry).content.height + title_bar_height + 2.0 * border_width};
    frame.outer_measured = true;
    return frame;
}

bool HeadlessWindowing::supports_popup_windows() const noexcept {
    return popup_windows_;
}

bool HeadlessWindowing::supports_splash_windows() const noexcept {
    return splash_windows_;
}

bool HeadlessWindowing::transient_windows_translucent() const noexcept {
    return translucent_;
}

void HeadlessWindowing::create_native(HostWindowId window, std::string_view stable_id,
                                      Rect content, Size minimum_size, bool decorated) {
    NativeWindow entry;
    entry.id = window;
    entry.stable_id.assign(stable_id);
    entry.content = content;
    entry.minimum_size = minimum_size;
    entry.decorated = decorated;
    natives_.push_back(std::move(entry));
    order_.insert(order_.begin(), window);
    std::ostringstream line;
    line << "create id=" << window.value << " stable=" << stable_id << " content=";
    append_rect(line, content);
    note(line.str());
}

void HeadlessWindowing::remove_native(HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < natives_.size(); ++index) {
        if (natives_[index].id != window) continue;
        natives_.erase(natives_.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        if (order_[index] != window) continue;
        order_.erase(order_.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    try {
        std::ostringstream line;
        line << "destroy id=" << window.value;
        note(line.str());
    } catch (...) {
        // The trace is diagnostic; losing a line must not stop a close.
    }
}

void HeadlessWindowing::set_native_visible(HostWindowId window, bool visible) {
    NativeWindow* entry = native(window);
    if (entry == nullptr || (*entry).visible == visible) return;
    (*entry).visible = visible;
    if (!tracing_) return;
    std::ostringstream line;
    line << (visible ? "show" : "hide") << " id=" << window.value;
    note(line.str());
}

void HeadlessWindowing::move_native(HostWindowId window, Rect content) {
    NativeWindow* entry = native(window);
    if (entry == nullptr) return;
    const Rect old_content = (*entry).content;
    if (native_follow_ && window == primary()) {
        // The platform carries the floating windows first and reports them
        // before the primary window's own report.
        const double dx = content.x - old_content.x;
        const double dy = content.y - old_content.y;
        std::vector<HostWindowId> carried;
        carried.reserve(maximum_windows);
        for (std::size_t index = 0U; index < natives_.size(); ++index) {
            const HostWindowId candidate = natives_[index].id;
            const std::optional<HostWindowKind> candidate_kind = kind(candidate);
            const std::optional<HostWindowId> candidate_owner = owner(candidate);
            if (!candidate_kind.has_value() || !candidate_owner.has_value()) continue;
            if (*candidate_owner != window || !host_window_kind_traits(*candidate_kind).floating) continue;
            carried.push_back(candidate);
        }
        // The primary window's native rectangle moves with them, as the
        // platform's state does, before any report.
        (*entry).content = content;
        for (std::size_t index = 0U; index < carried.size(); ++index) {
            NativeWindow* child = native(carried[index]);
            if (child == nullptr) continue;
            const Rect child_old = (*child).content;
            (*child).content.x += dx;
            (*child).content.y += dy;
            std::ostringstream carried_line;
            carried_line << "carried id=" << carried[index].value << " content=";
            append_rect(carried_line, (*child).content);
            note(carried_line.str());
            (*application_).native_frame_changed(carried[index], child_old, (*child).content);
        }
        entry = native(window);
        if (entry == nullptr) return;
    }
    (*entry).content = content;
    std::ostringstream line;
    line << "user-frame id=" << window.value << " content=";
    append_rect(line, content);
    note(line.str());
    (*application_).native_frame_changed(window, old_content, content);
}

void HeadlessWindowing::raise_native(HostWindowId window) {
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        if (order_[index] != window) continue;
        order_.erase(order_.begin() + static_cast<std::ptrdiff_t>(index));
        order_.insert(order_.begin(), window);
        return;
    }
}

HostWindowFrameResult HeadlessWindowing::frame_impl(HostWindowId window) {
    HostWindowFrameResult result;
    if (native(window) == nullptr) {
        result.status.error = HostServiceError::invalid_argument;
        return result;
    }
    result.frame = native_frame(window);
    return result;
}

HostServiceStatus HeadlessWindowing::request_frame_impl(HostWindowId window, Rect content) {
    NativeWindow* entry = native(window);
    if (entry == nullptr) return {HostServiceError::invalid_argument};
    if (content.width < (*entry).minimum_size.width ||
        content.height < (*entry).minimum_size.height) {
        return {HostServiceError::invalid_argument};
    }
    const Rect old_content = (*entry).content;
    (*entry).content = content;
    if (tracing_) {
        std::ostringstream line;
        line << "frame id=" << window.value << " content=";
        append_rect(line, content);
        note(line.str());
    }
    if (deferred_reports_) {
        deferred_.push_back({window, old_content});
        return {};
    }
    // As on Windows, the native frame report arrives before the request
    // returns.
    (*application_).native_frame_changed(window, old_content, content);
    return {};
}

void HeadlessWindowing::flush_frame_reports() {
    // Reports raised while flushing (follow moves) are queued behind.
    for (std::size_t index = 0U; index < deferred_.size(); ++index) {
        const DeferredReport report = deferred_[index];
        const NativeWindow* entry = native(report.window);
        if (entry == nullptr) continue;
        const Rect content = (*entry).content;
        (*application_).native_frame_changed(report.window, report.old_content, content);
    }
    deferred_.clear();
}

HostScreenPointResult HeadlessWindowing::pointer_position_impl() {
    HostScreenPointResult result;
    result.position = pointer_;
    return result;
}

HostWindowAtResult HeadlessWindowing::window_at_impl(DesktopLogicalPoint point) {
    HostWindowAtResult result;
    // The simulated desktop's rectangles are logical, as the point is.
    const Point desktop{point.x, point.y};
    for (std::size_t index = 0U; index < foreign_windows_.size(); ++index) {
        if (foreign_windows_[index].contains(desktop)) return result;
    }
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        const NativeWindow* entry = native(order_[index]);
        if (entry == nullptr || !(*entry).visible) continue;
        const HostWindowFrame frame = native_frame(order_[index]);
        if (!frame.outer.contains(desktop)) continue;
        result.window = order_[index];
        return result;
    }
    return result;
}

HostServiceStatus HeadlessWindowing::bring_to_front_impl(HostWindowId window) {
    if (native(window) == nullptr) return {HostServiceError::invalid_argument};
    if (tracing_) {
        std::ostringstream line;
        line << "raise id=" << window.value;
        note(line.str());
    }
    // Owned windows stay above their owners: raise the window, then each
    // window it owns (directly or through another owned window), keeping
    // their order among themselves. The table holds at most 64 windows.
    std::vector<HostWindowId> pending;
    pending.reserve(maximum_windows);
    pending.push_back(window);
    for (std::size_t next = 0U; next < pending.size(); ++next) {
        raise_native(pending[next]);
        for (std::size_t index = order_.size(); index > 0U; --index) {
            const HostWindowId candidate = order_[index - 1U];
            const std::optional<HostWindowId> candidate_owner = owner(candidate);
            if (candidate_owner.has_value() && *candidate_owner == pending[next]) {
                pending.push_back(candidate);
            }
        }
    }
    return {};
}

HostServiceStatus HeadlessWindowing::destroy_window_impl(HostWindowId window) {
    const HostServiceStatus status = (*application_).destroy(window);
    return status;
}

HostMonitorResult HeadlessWindowing::query_monitors_impl() {
    HostMonitorResult result;
    result.monitors = monitors_;
    return result;
}

HostServiceStatus HeadlessWindowing::show_impl(HostWindowId window) {
    if (native(window) == nullptr) return {HostServiceError::invalid_argument};
    set_native_visible(window, true);
    return {};
}

HostServiceStatus HeadlessWindowing::hide_impl(HostWindowId window) {
    if (native(window) == nullptr) return {HostServiceError::invalid_argument};
    set_native_visible(window, false);
    return {};
}

// HeadlessApplication -------------------------------------------------------

HeadlessApplication::HeadlessApplication(std::vector<ApplicationWindow> windows,
                                         HostCapabilities capabilities)
    : run_(std::make_shared<detail::ApplicationRunState>()),
      windowing_(*this, std::move(capabilities)) {
    const ApplicationResult validation = Application::validate(windows);
    if (!validation.accepted()) {
        throw std::invalid_argument("HeadlessApplication: Application::validate refused the windows");
    }
    detail::attach_run_windowing(run_, windowing_, HeadlessOpen{this});
    entries_.reserve(HostWindowing::maximum_windows);
    // Open owners before the windows they own; validation has excluded
    // cycles and unknown owners, so each pass opens at least one window.
    std::vector<bool> opened(windows.size(), false);
    std::size_t remaining = windows.size();
    while (remaining > 0U) {
        for (std::size_t index = 0U; index < windows.size(); ++index) {
            if (opened[index]) continue;
            const bool owner_open = windows[index].owner_id.empty() ||
                windowing_.find(windows[index].owner_id).has_value();
            if (!owner_open) continue;
            const ApplicationWindowOpenResult result = open(std::move(windows[index]), false);
            if (!result.status.accepted()) {
                throw std::invalid_argument("HeadlessApplication: a window was refused");
            }
            opened[index] = true;
            --remaining;
        }
    }
    windowing_.mark_started();
}

HeadlessApplication::~HeadlessApplication() {
    for (std::size_t index = entries_.size(); index > 0U; --index) {
        WindowEntry& entry = *entries_[index - 1U];
        if (!entry.closed) close(entry, HostCloseReason::application);
    }
    detail::detach_run_windowing(*run_);
    windowing_.shutdown();
    entries_.clear();
}

ApplicationWindowOpenResult HeadlessApplication::HeadlessOpen::operator()(
    ApplicationWindow&& entry) const {
    const ApplicationWindowOpenResult result = (*application).open(std::move(entry), true);
    return result;
}

ApplicationWindows HeadlessApplication::windows() const {
    const ApplicationWindows result = detail::ApplicationHandleAccess::make_windows(run_);
    return result;
}

ApplicationWindowHandle HeadlessApplication::handle(std::string_view stable_id) const {
    for (std::size_t index = 0U; index < entries_.size(); ++index) {
        const WindowEntry& candidate = *entries_[index];
        if (candidate.closed || candidate.stable_id != stable_id) continue;
        const ApplicationWindowHandle result = detail::ApplicationHandleAccess::make(candidate.state);
        return result;
    }
    return {};
}

HostWindowId HeadlessApplication::id(std::string_view stable_id) const {
    const std::optional<HostWindowId> result = windowing_.find(stable_id);
    if (!result.has_value()) return {};
    return *result;
}

HeadlessApplication::WindowEntry* HeadlessApplication::entry(HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < entries_.size(); ++index) {
        WindowEntry& candidate = *entries_[index];
        if (!candidate.closed && candidate.id == window) return &candidate;
    }
    return nullptr;
}

HeadlessHost* HeadlessApplication::host(HostWindowId window) noexcept {
    WindowEntry* found = entry(window);
    if (found == nullptr) return nullptr;
    return (*found).host.get();
}

Window* HeadlessApplication::model(HostWindowId window) noexcept {
    WindowEntry* found = entry(window);
    if (found == nullptr) return nullptr;
    return (*found).model.get();
}

Rect HeadlessApplication::default_placement(HostWindowKind kind, HostWindowId owner,
                                            Size size) const {
    if (!owner.valid()) {
        if (kind == HostWindowKind::primary) {
            const Rect primary{100.0, 100.0, size.width, size.height};
            return primary;
        }
        const double offset = 160.0 + 24.0 * static_cast<double>(placements_);
        const Rect cascade{offset, offset, size.width, size.height};
        return cascade;
    }
    const std::optional<Rect> owner_content = windowing_.native_content(owner);
    if (!owner_content.has_value()) {
        const Rect fallback{100.0, 100.0, size.width, size.height};
        return fallback;
    }
    // To the owner's right, top edges aligned, past both borders.
    const Rect beside{(*owner_content).right() + 2.0 * HeadlessWindowing::border_width + 16.0,
                      (*owner_content).y, size.width, size.height};
    return beside;
}

ApplicationWindowOpenResult HeadlessApplication::open(ApplicationWindow&& source, bool runtime) {
    ApplicationWindowOpenResult result;
    if (!detail::valid_window_entry(source) || !(*source.model).check_access()) {
        result.status.error = HostServiceError::invalid_argument;
        return result;
    }
    HostWindowId owner{};
    if (!source.owner_id.empty()) {
        const std::optional<HostWindowId> found = windowing_.find(source.owner_id);
        if (!found.has_value()) {
            result.status.error = HostServiceError::invalid_argument;
            return result;
        }
        owner = *found;
    }
    const std::optional<HostPopupPlacement> popup = source.popup;
    const HostWindowKind kind =
        host_window_kind(owner.valid(), source.tool_window, source.floating, popup.has_value());
    const bool floating = host_window_kind_traits(kind).floating;
    if (kind == HostWindowKind::primary &&
        (runtime || !source.options.initially_visible || source.options.hide_on_close)) {
        result.status.error = HostServiceError::invalid_argument;
        return result;
    }
    HostWindowDescription description;
    description.stable_id = source.stable_id;
    description.owner = owner;
    description.kind = kind;
    if (popup.has_value()) description.transient = (*popup).kind;
    const HostWindowAdmission admission = windowing_.admit(description);
    if (!admission.status.accepted()) {
        result.status = admission.status;
        return result;
    }

    // The entry is consumed from here.
    std::unique_ptr<WindowEntry> created = std::make_unique<WindowEntry>();
    WindowEntry& window = *created;
    window.id = admission.window;
    window.stable_id = std::move(source.stable_id);
    window.model = std::move(source.model);
    window.options = std::move(source.options);
    if (runtime && open_fault_ != HeadlessOpenFault::none) {
        // An injected native creation failure, after the entry was taken.
        const HeadlessOpenFault fault = open_fault_;
        open_fault_ = HeadlessOpenFault::none;
        windowing_.retire(window.id);
        if (fault == HeadlessOpenFault::refuse_and_keep) refused_models_.push_back(std::move(window.model));
        result.status.error = HostServiceError::backend_failure;
        return result;
    }
    // Before the session is bound (stage T5).
    if (window.options.non_opaque) static_cast<void>((*window.model).declare_non_opaque());
    window.host = std::make_unique<HeadlessHost>(*window.model);
    const HostDispatchResult attached = (*window.host).dispatch(
        HostAttachEvent{window.options.initial_size, 1.0}, next_timestamp());
    if (!attached.accepted()) {
        windowing_.retire(window.id);
        result.status.error = HostServiceError::backend_failure;
        return result;
    }
    // The simulated compositor's state, once one has been reported, as a
    // native host reports it right after attach.
    if (compositor_state_.generation.value != 0U) {
        static_cast<void>((*window.host).dispatch(HostCompositorEvent{compositor_state_}, next_timestamp()));
    }
    const HostInputSettingsEvent input{headless_double_click_milliseconds, SettingOrigin::host};
    static_cast<void>((*window.host).dispatch(input, next_timestamp()));
    Rect content = default_placement(kind, owner, window.options.initial_size);
    // A popup is where the program put it.
    if (popup.has_value()) {
        content = Rect{(*popup).origin.x, (*popup).origin.y, window.options.initial_size.width,
                       window.options.initial_size.height};
    }
    if (!owner.valid() && kind != HostWindowKind::primary) ++placements_;
    windowing_.create_native(window.id, window.stable_id, content, window.options.minimum_size,
                             !popup.has_value());
    {
        std::ostringstream line;
        line << "open id=" << window.id.value << " kind=" << host_window_kind_name(kind)
             << " owner=" << owner.value << " visible=" << (window.options.initially_visible ? 1 : 0)
             << " runtime=" << (runtime ? 1 : 0);
        if (popup.has_value()) {
            line << " focus=" << host_popup_focus_name((*popup).focus)
                 << " transient=" << host_transient_kind_name((*popup).kind);
        }
        windowing_.note(line.str());
    }
    windowing_.report_frame(window.id, windowing_.native_frame(window.id));

    window.state = std::make_shared<detail::ApplicationWindowState>();
    detail::ApplicationWindowState& state = *window.state;
    state.stable_id = window.stable_id;
    state.run = run_;
    state.can_hide = kind != HostWindowKind::primary;
    state.request_close = HeadlessRequestClose{this, window.id};
    if (floating) {
        // The floating policy decides when the window is on screen.
        state.show = detail::PolicyVisibility{window.state, true};
        state.hide = detail::PolicyVisibility{window.state, false};
    } else {
        state.show = HeadlessShow{this, window.id, true};
        state.hide = HeadlessShow{this, window.id, false};
    }
    state.set_title = HeadlessTitle{&windowing_, window.id};
    state.request_client_size = HeadlessClientSize{this, window.id};
    state.frame_changed = window.options.frame_changed;
    entries_.push_back(std::move(created));

    // As on the native hosts: wake, then ready, then the initial show.
    WindowEntry& opened = *entries_.back();
    if (opened.options.wake_ready) opened.options.wake_ready(HeadlessWake{});
    (*opened.state).ready = true;
    detail::register_run_window(opened.state);
    detail::attach_application_window(*opened.model, opened.state);
    const ApplicationWindowHandle handle = detail::ApplicationHandleAccess::make(opened.state);
    if (opened.options.ready) opened.options.ready(*opened.model, handle);
    // The ready callback may have closed the window.
    WindowEntry* still_open = entry(admission.window);
    if (still_open != nullptr) {
        const bool initially_visible = (*still_open).options.initially_visible;
        // A floating window was created hidden: window_created shows it when
        // the policy lets it be on screen. Any other window is shown here.
        windowing_.window_created(admission.window, initially_visible);
        if (!floating && initially_visible) show(admission.window, true);
    }
    result.window = handle;
    return result;
}

void HeadlessApplication::show(HostWindowId window, bool visible) {
    if (entry(window) == nullptr) return;
    windowing_.set_native_visible(window, visible);
}

HostServiceStatus HeadlessApplication::resize(HostWindowId window, Size client_size) {
    const std::optional<Rect> content = windowing_.native_content(window);
    if (!content.has_value()) return {HostServiceError::after_shutdown};
    const Rect requested{(*content).x, (*content).y, client_size.width, client_size.height};
    const HostServiceStatus status = windowing_.request_frame(window, requested);
    return status;
}

void HeadlessApplication::native_frame_changed(HostWindowId window, Rect old_content,
                                               Rect content) {
    WindowEntry* found = entry(window);
    if (found == nullptr) return;
    if (old_content.width != content.width || old_content.height != content.height) {
        static_cast<void>((*(*found).host).dispatch(
            HostResizeEvent{{content.width, content.height}}, next_timestamp()));
    }
    windowing_.report_frame(window, windowing_.native_frame(window));
}

void HeadlessApplication::simulate_user_frame(HostWindowId window, Rect content) {
    windowing_.move_native(window, content);
}

void HeadlessApplication::request_close(HostWindowId window) {
    simulate_user_close(window);
}

void HeadlessApplication::simulate_user_close(HostWindowId window) {
    WindowEntry* found = entry(window);
    if (found == nullptr) return;
    HostCloseRequest request{HostCloseReason::user, false, (*found).options.hide_on_close};
    if ((*found).options.closing) (*found).options.closing(request);
    found = entry(window);
    if (found == nullptr) return;
    request.hide_on_accept = (*found).options.hide_on_close;
    const HostDispatchResult result = (*(*found).host).dispatch(request, next_timestamp());
    if (!result.accepted() || !result.close_allowed) return;
    found = entry(window);
    if (found == nullptr) return;
    if ((*found).options.hide_on_close) {
        show(window, false);
        windowing_.report_hidden_on_close(window);
        return;
    }
    if (window != windowing_.primary()) {
        close(*found, HostCloseReason::user);
        return;
    }
    // Closing the primary window ends the run: every other window closes
    // first, owned windows before their owners (reverse opening order).
    for (std::size_t index = entries_.size(); index > 0U; --index) {
        WindowEntry& other = *entries_[index - 1U];
        if (!other.closed && other.id != window) close(other, HostCloseReason::application);
    }
    WindowEntry* primary = entry(window);
    if (primary != nullptr) close(*primary, HostCloseReason::user);
}

void HeadlessApplication::simulate_application_active(bool active) {
    std::ostringstream line;
    line << "application active=" << (active ? 1 : 0);
    windowing_.note(line.str());
    windowing_.report_application_active(active);
}

void HeadlessApplication::simulate_compositor_state(CompositorState state) {
    state.generation.value = compositor_state_.generation.value + 1U;
    compositor_state_ = state;
    std::ostringstream line;
    line << "compositor generation=" << state.generation.value
         << " compositing=" << compositor_presence_name(state.compositing);
    windowing_.note(line.str());
    // In opening order. A window's handler may open or close windows; the
    // count is read on every turn and closed windows are skipped.
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        WindowEntry& window = *entries_[index];
        if (window.closed || !window.host) continue;
        static_cast<void>((*window.host).dispatch(HostCompositorEvent{compositor_state_}, next_timestamp()));
    }
}

void HeadlessApplication::simulate_minimized(HostWindowId window, bool minimized) {
    if (entry(window) == nullptr) return;
    std::ostringstream line;
    line << (minimized ? "minimize" : "restore") << " id=" << window.value;
    windowing_.note(line.str());
    windowing_.report_minimized(window, minimized);
}

HostServiceStatus HeadlessApplication::destroy(HostWindowId window) {
    WindowEntry* found = entry(window);
    if (found == nullptr) return {HostServiceError::invalid_argument};
    close(*found, HostCloseReason::application);
    return {};
}

void HeadlessApplication::close(WindowEntry& window, HostCloseReason reason) {
    if (window.closed) return;
    window.closed = true;
    static_cast<void>((*window.host).dispatch(HostClosedEvent{reason}, next_timestamp()));
    detail::ApplicationWindowState& state = *window.state;
    detail::revoke_window_state(state);
    detail::unregister_run_window(state);
    windowing_.remove_native(window.id);
    windowing_.retire(window.id);
    // The closed callback runs last, with the window gone from the table,
    // so it may open a replacement under the same stable id.
    const std::function<void()> closed = window.options.closed;
    if (closed) closed();
}

void HeadlessApplication::reclaim() {
    for (std::size_t index = entries_.size(); index > 0U; --index) {
        if (!(*entries_[index - 1U]).closed) continue;
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
    }
}

} // namespace gui_forms::host
