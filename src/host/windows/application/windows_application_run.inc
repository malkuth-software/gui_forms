// Application-scope windowing on Win32 (stage DK phase W0, decisions 6 and
// 7). Included inside windows_host.cpp's anonymous namespace after
// WindowsHostState and window_procedure.
//
// The run owns every window's WindowsHostState through a slot. Initial
// windows keep their slot until the run returns, as before; a window that
// closes during the run keeps its slot until the end of the loop turn in
// which it closed (reclaim), so no state is released inside one of its own
// messages.
//
// Desktop units: the process is per-monitor DPI aware, so Win32 screen
// coordinates are device pixels. Frames, the pointer and work areas are
// converted to desktop logical units through the portable per-monitor
// mapping (gui_forms/host/windowing/desktop_coordinates.hpp); a window's
// extent uses the window's own DPI.

struct WindowsRunSlot final {
    std::unique_ptr<WindowsHostState> state{};
    HostWindowId id{};
    std::string stable_id{};
    bool initial{};
    // Shown and hidden by the floating policy (HostWindowKindTraits).
    bool floating{};
    // CreateWindowExW failed: the state never had a native window, so it
    // never reaches destroyed(); reclaim releases it all the same.
    bool never_created{};
    bool print_metrics_on_close{};
    std::function<void(std::string_view metrics_json, std::string_view host_json)> final_snapshot{};
    std::string capture_path{};
};

BOOL CALLBACK collect_device_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    std::vector<HostMonitor>& monitors = *reinterpret_cast<std::vector<HostMonitor>*>(context);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    HostMonitor value;
    value.id = utf8_from_wide(info.szDevice);
    if (value.id.empty()) {
        value.id = "win32.monitor." + std::to_string(reinterpret_cast<std::uintptr_t>(monitor));
    }
    value.frame = {static_cast<double>(info.rcMonitor.left), static_cast<double>(info.rcMonitor.top),
                   static_cast<double>(info.rcMonitor.right - info.rcMonitor.left),
                   static_cast<double>(info.rcMonitor.bottom - info.rcMonitor.top)};
    value.work_area = {static_cast<double>(info.rcWork.left), static_cast<double>(info.rcWork.top),
                       static_cast<double>(info.rcWork.right - info.rcWork.left),
                       static_cast<double>(info.rcWork.bottom - info.rcWork.top)};
    value.scale = native_monitor_scale(monitor);
    value.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0U;
    monitors.push_back(std::move(value));
    return TRUE;
}

// EnumDisplayMonitors' context for fill_logical_monitor: the records to
// write in place and how many have been written.
struct LogicalMonitorFill final {
    std::vector<HostMonitor>* monitors{};
    std::size_t count{};
};

// Writes one monitor into the next record of a LogicalMonitorFill, in
// logical units, reusing the record's id storage: the same monitor as
// collect_device_monitor followed by logical_monitor, allocating nothing
// once the records exist.
BOOL CALLBACK fill_logical_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    LogicalMonitorFill& fill = *reinterpret_cast<LogicalMonitorFill*>(context);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    std::vector<HostMonitor>& monitors = *fill.monitors;
    if (fill.count == monitors.size()) monitors.emplace_back();
    HostMonitor& value = monitors[fill.count];
    // szDevice is at most CCHDEVICENAME (32) UTF-16 units: at most 96 UTF-8
    // bytes.
    char identifier[128]{};
    const int device_length = static_cast<int>(wcsnlen(info.szDevice, CCHDEVICENAME));
    int length = device_length == 0 ? 0
        : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, info.szDevice, device_length, identifier,
                              static_cast<int>(sizeof(identifier)), nullptr, nullptr);
    if (length <= 0) {
        length = std::snprintf(identifier, sizeof(identifier), "win32.monitor.%llu",
                               static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(monitor)));
    }
    value.id.assign(identifier, length > 0 ? static_cast<std::size_t>(length) : 0U);
    value.frame = {static_cast<double>(info.rcMonitor.left), static_cast<double>(info.rcMonitor.top),
                   static_cast<double>(info.rcMonitor.right - info.rcMonitor.left),
                   static_cast<double>(info.rcMonitor.bottom - info.rcMonitor.top)};
    value.work_area = {static_cast<double>(info.rcWork.left), static_cast<double>(info.rcWork.top),
                       static_cast<double>(info.rcWork.right - info.rcWork.left),
                       static_cast<double>(info.rcWork.bottom - info.rcWork.top)};
    value.scale = native_monitor_scale(monitor);
    value.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0U;
    make_logical_monitor(value);
    ++fill.count;
    return TRUE;
}

// The monitors in Win32 device desktop units, as HostServices reports them.
std::vector<HostMonitor> device_monitors() {
    std::vector<HostMonitor> monitors;
    monitors.reserve(8U);
    EnumDisplayMonitors(nullptr, nullptr, &collect_device_monitor,
                        reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

// dwmapi.dll's entry point, typed from its declaration (house style section 2).
using DwmGetWindowAttributeFunction = decltype(&::DwmGetWindowAttribute);

// The visible window bounds without the invisible resize borders Windows 10
// and later add around a sizable window (DWMWA_EXTENDED_FRAME_BOUNDS), read
// through dwmapi.dll when it is present; else GetWindowRect.
DwmGetWindowAttributeFunction load_dwm_get_window_attribute() noexcept {
    HMODULE library = LoadLibraryW(L"dwmapi.dll");
    if (library == nullptr) return nullptr;
    const DwmGetWindowAttributeFunction function =
        load_function<DwmGetWindowAttributeFunction>(library, "DwmGetWindowAttribute");
    return function;
}

using DwmSetWindowAttributeFunction = decltype(&::DwmSetWindowAttribute);

DwmSetWindowAttributeFunction load_dwm_set_window_attribute() noexcept {
    HMODULE library = LoadLibraryW(L"dwmapi.dll");
    if (library == nullptr) return nullptr;
    const DwmSetWindowAttributeFunction function =
        load_function<DwmSetWindowAttributeFunction>(library, "DwmSetWindowAttribute");
    return function;
}

// A floating window takes its owner's caption scheme: the dark title bar
// (DWMWA_USE_IMMERSIVE_DARK_MODE, 20) its owner has, whether the program or
// GUI.Forms set it, so a torn-out panel matches its main window. Nothing
// where dwmapi.dll or the attribute is missing (before Windows 10 20H1).
void follow_owner_caption_scheme(HWND owner, HWND window) noexcept {
    static const DwmGetWindowAttributeFunction get_attribute = load_dwm_get_window_attribute();
    static const DwmSetWindowAttributeFunction set_attribute = load_dwm_set_window_attribute();
    if (get_attribute == nullptr || set_attribute == nullptr || owner == nullptr) return;
    constexpr DWORD immersive_dark_mode = 20U;
    BOOL dark = FALSE;
    if (FAILED(get_attribute(owner, immersive_dark_mode, &dark, sizeof(dark)))) return;
    static_cast<void>(set_attribute(window, immersive_dark_mode, &dark, sizeof(dark)));
}

bool visible_window_bounds(HWND window, RECT& bounds) noexcept {
    static const DwmGetWindowAttributeFunction get_attribute = load_dwm_get_window_attribute();
    constexpr DWORD extended_frame_bounds = DWMWA_EXTENDED_FRAME_BOUNDS;
    if (get_attribute != nullptr &&
        SUCCEEDED(get_attribute(window, extended_frame_bounds, &bounds, sizeof(bounds)))) {
        return true;
    }
    const bool measured = GetWindowRect(window, &bounds) != FALSE;
    return measured;
}

// The Win32 window styles of one window.
struct WindowsWindowStyles final {
    DWORD style{};
    DWORD extended{};
};

// The styles for a window of `kind` with `options`: a popup window has a
// border only; otherwise an overlapped window, without its minimize box
// when the window is not minimizable, and a tool window takes the tool
// caption. Floating windows (decision 10) are owned by the primary window,
// which already keeps them off the taskbar, above the owner only and hidden
// while it is minimized; they minimize with it, never on their own, so they
// have no minimize box, and a floating tool takes the tool caption without
// a maximize box.
WindowsWindowStyles windows_window_styles(HostWindowKind kind, bool tool_window,
                                          const WindowsHostOptions& options) noexcept {
    WindowsWindowStyles styles;
    // A native popup (owner, 2026-11-04): undecorated, off the taskbar
    // (the tool style), above its owner as an owned window. A passive
    // popup is never activated (WS_EX_NOACTIVATE, and MA_NOACTIVATE on a
    // click: WindowsHostState::passive_popup_).
    if (kind == HostWindowKind::popup) {
        styles.style = WS_POPUP;
        styles.extended = WS_EX_TOOLWINDOW;
        return styles;
    }
    if (kind == HostWindowKind::floating_tool) {
        styles.style = WS_OVERLAPPEDWINDOW & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        styles.extended = WS_EX_TOOLWINDOW;
        return styles;
    }
    if (kind == HostWindowKind::floating_document) {
        styles.style = WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX;
        styles.extended = 0;
        return styles;
    }
    if (options.popup_window) {
        styles.style = WS_POPUP | WS_BORDER;
    } else if (options.minimizable) {
        styles.style = WS_OVERLAPPEDWINDOW;
    } else {
        styles.style = WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX;
    }
    styles.extended = tool_window ? WS_EX_TOOLWINDOW : 0;
    return styles;
}

class WindowsApplicationRun final : public HostWindowing {
public:
    // The maximum slots: every initial window (at most 64) plus the live
    // runtime windows (at most 64 together with the initial ones that are
    // still open) and those closed in the current turn.
    static constexpr std::size_t reserved_slots = 2U * HostWindowing::maximum_windows;

    explicit WindowsApplicationRun(HINSTANCE instance)
        : HostWindowing(windows_capabilities()), instance_(instance) {
        slots_.reserve(reserved_slots);
        clocks_.reserve(reserved_slots);
    }
    WindowsApplicationRun(const WindowsApplicationRun&) = delete;
    WindowsApplicationRun& operator=(const WindowsApplicationRun&) = delete;

    struct OpenOutcome final {
        HostWindowAdmission admission{};
        // run_windows_application's exit code for an initial window that
        // could not be created: 2 invalid, 4 native creation, 5 attach.
        int failure_code{};
    };

    // Creates one window: admission, the native window owned by its owner's
    // window, attachment and the ready callbacks. A runtime window is then
    // shown when initially_visible; initial windows are shown by the caller
    // once every initial window exists.
    OpenOutcome open(WindowsApplicationWindow entry, bool runtime);

    [[nodiscard]] std::size_t slot_count() const noexcept { return slots_.size(); }
    [[nodiscard]] WindowsRunSlot& slot_at(std::size_t index) noexcept { return *slots_[index]; }

    // Reports the native window's current frame (WM_MOVE, WM_SIZE).
    void report_native_frame(HostWindowId window, HWND native);
    // Reports whether this process owns the foreground window, once the
    // initial windows are shown (WM_ACTIVATEAPP reports later changes).
    void report_foreground();
    // Releases runtime windows that closed during the turn, after printing
    // their metrics as the run prints every window's at exit, and runtime
    // windows whose native creation failed.
    void reclaim();
    // Prints one window's final metrics and snapshot.
    static void publish_final_metrics(WindowsRunSlot& slot);
    // One loop turn: waits for a message or a live frame clock, dispatches
    // a bounded batch of messages and presents pending frames. Returns true
    // when it took WM_QUIT (copied to `quit_message`) or the wait failed
    // (`quit_message` cleared). Only the outermost loop (`outermost`)
    // releases closed runtime windows: a nested loop (run_modal) runs
    // inside a message of some window, so no window may be released under
    // it.
    [[nodiscard]] bool turn(MSG& quit_message, bool outermost);

protected:
    HostWindowFrameResult frame_impl(HostWindowId window) override;
    HostServiceStatus request_frame_impl(HostWindowId window, Rect content) override;
    HostScreenPointResult pointer_position_impl() override;
    HostWindowAtResult window_at_impl(DesktopLogicalPoint point) override;
    HostServiceStatus bring_to_front_impl(HostWindowId window) override;
    HostServiceStatus destroy_window_impl(HostWindowId window) override;
    HostMonitorResult query_monitors_impl() override;
    void query_monitors_into_impl(HostMonitorResult& result) override;

public:
    // Popup windows open during the run (HostWindowKind::popup); their
    // per-pixel alpha is the transparent backdrop through DWM's blur-behind
    // with an empty region (windows_compositor.cpp).
    [[nodiscard]] bool supports_popup_windows() const noexcept override { return true; }
    // Splash windows (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S,
    // SPLASH_STUDY D4) are the same popup windows, never activated, topmost,
    // and transparent to the pointer (WM_NCHITTEST answers HTTRANSPARENT).
    [[nodiscard]] bool supports_splash_windows() const noexcept override { return true; }
    // run_modal's nested loop: the other windows are disabled
    // (EnableWindow), as the Win32 dialog manager does for a modal dialog.
    [[nodiscard]] bool supports_modal_windows() const noexcept override { return true; }

protected:
    HostServiceStatus show_impl(HostWindowId window) override;
    HostServiceStatus hide_impl(HostWindowId window) override;
    HostServiceStatus run_modal_impl(HostWindowId window) override;

private:
    [[nodiscard]] WindowsRunSlot* slot(HostWindowId window) noexcept;
    [[nodiscard]] WindowsRunSlot* slot(std::string_view stable_id) noexcept;
    [[nodiscard]] HWND native(HostWindowId window) noexcept;

    HINSTANCE instance_{};
    // Stable addresses: a window's callbacks may open another window while
    // its own slot is in use.
    std::vector<std::unique_ptr<WindowsRunSlot>> slots_;
    // The live frame clocks one turn waits on, refilled at the start of
    // every turn (a nested turn refills it too, after the turn below it has
    // finished its wait); reserved for every slot so a turn allocates
    // nothing.
    std::vector<HANDLE> clocks_;
};

// The window frame in desktop logical units.
HostWindowFrame native_window_frame(HWND window) {
    HostWindowFrame frame;
    RECT client{};
    POINT origin{0, 0};
    if (!GetClientRect(window, &client) || !ClientToScreen(window, &origin)) return frame;
    const std::vector<HostMonitor> monitors = device_monitors();
    const double scale = query_scale(window);
    const DesktopLogicalPoint content_origin = desktop_logical_point(
        monitors, DesktopDevicePoint{static_cast<double>(origin.x), static_cast<double>(origin.y)});
    frame.content = {content_origin.x, content_origin.y,
                     static_cast<double>(client.right - client.left) / scale,
                     static_cast<double>(client.bottom - client.top) / scale};
    RECT outer{};
    if (visible_window_bounds(window, outer)) {
        const DesktopLogicalPoint outer_origin = desktop_logical_point(
            monitors, DesktopDevicePoint{static_cast<double>(outer.left), static_cast<double>(outer.top)});
        frame.outer = {outer_origin.x, outer_origin.y,
                       static_cast<double>(outer.right - outer.left) / scale,
                       static_cast<double>(outer.bottom - outer.top) / scale};
        frame.outer_measured = true;
    } else {
        frame.outer = frame.content;
    }
    return frame;
}

WindowsRunSlot* WindowsApplicationRun::slot(HostWindowId window) noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        WindowsRunSlot& candidate = *slots_[index];
        if (candidate.id == window && candidate.state && !(*candidate.state).destroyed()) {
            return &candidate;
        }
    }
    return nullptr;
}

WindowsRunSlot* WindowsApplicationRun::slot(std::string_view stable_id) noexcept {
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        WindowsRunSlot& candidate = *slots_[index];
        if (candidate.stable_id == stable_id && live(candidate.id)) return &candidate;
    }
    return nullptr;
}

HWND WindowsApplicationRun::native(HostWindowId window) noexcept {
    WindowsRunSlot* found = slot(window);
    if (found == nullptr) return nullptr;
    return (*(*found).state).native_window();
}

WindowsApplicationRun::OpenOutcome WindowsApplicationRun::open(WindowsApplicationWindow entry,
                                                               bool runtime) {
    OpenOutcome outcome;
    if (!entry.model || entry.stable_id.empty()) {
        outcome.admission.status.error = HostServiceError::invalid_argument;
        outcome.failure_code = 2;
        return outcome;
    }
    HostWindowId owner_id{};
    HWND owner{};
    if (!entry.owner_id.empty()) {
        WindowsRunSlot* owner_slot = slot(std::string_view(entry.owner_id));
        if (owner_slot == nullptr) {
            outcome.admission.status.error = HostServiceError::invalid_argument;
            outcome.failure_code = 2;
            return outcome;
        }
        owner_id = (*owner_slot).id;
        owner = (*(*owner_slot).state).native_window();
    }
    const std::optional<HostPopupPlacement> popup = entry.popup;
    const HostWindowKind kind =
        host_window_kind(owner_id.valid(), entry.tool_window, entry.floating, popup.has_value());
    const bool floating = host_window_kind_traits(kind).floating;
    HostWindowDescription description;
    description.stable_id = entry.stable_id;
    description.owner = owner_id;
    description.kind = kind;
    if (popup.has_value()) description.transient = (*popup).kind;
    outcome.admission = admit(description);
    if (!outcome.admission.status.accepted()) {
        outcome.failure_code = 2;
        return outcome;
    }
    const HostWindowId id = outcome.admission.window;

    std::unique_ptr<WindowsRunSlot> created = std::make_unique<WindowsRunSlot>();
    WindowsRunSlot& window_slot = *created;
    window_slot.id = id;
    window_slot.stable_id = entry.stable_id;
    window_slot.initial = !runtime;
    window_slot.floating = floating;
    window_slot.print_metrics_on_close = entry.options.print_metrics_on_close;
    window_slot.final_snapshot = entry.options.final_snapshot;
    window_slot.capture_path = entry.options.capture_path;

    WindowsHostOptions host_options = entry.options;
    host_options.quit_thread_on_close = kind == HostWindowKind::primary;
    const bool initially_visible = entry.options.initially_visible;
    const bool close_after_launch = entry.options.close_after_launch_for_testing;
    window_slot.state = std::make_unique<WindowsHostState>(std::move(entry.model), std::move(host_options));
    (*window_slot.state).attach_run(*this, id);
    const WindowsWindowStyles styles = windows_window_styles(kind, entry.tool_window, entry.options);
    const DWORD style = styles.style;
    DWORD ex_style = styles.extended;
    const bool splash_popup = popup.has_value() && host_transient_is_splash((*popup).kind);
    const bool passive_popup = popup.has_value() && ((*popup).focus == HostPopupFocus::passive || splash_popup);
    if (passive_popup) {
        ex_style = ex_style | WS_EX_NOACTIVATE;
        (*window_slot.state).mark_passive_popup();
    }
    if (splash_popup) {
        // Above the program's popups and windows, as the host tooltip was.
        ex_style = ex_style | WS_EX_TOPMOST;
        (*window_slot.state).mark_splash();
    }
    const double initial_scale = query_scale(owner);
    const Size initial_size = entry.options.capture_size.has_value()
        ? *entry.options.capture_size : entry.options.initial_size;
    RECT frame{0, 0, static_cast<LONG>(std::ceil(initial_size.width * initial_scale)),
               static_cast<LONG>(std::ceil(initial_size.height * initial_scale))};
    adjust_client_frame(frame, style, ex_style, initial_scale);
    const std::wstring title = wide_from_utf8(entry.options.title);
    // The slot is published before creation: callbacks during initialization
    // (the ready callback) may already ask for this window's frame.
    slots_.push_back(std::move(created));
    WindowsHostState& state = *(*slots_.back()).state;
    HWND window = CreateWindowExW(ex_style, window_class_name, title.c_str(), style, CW_USEDEFAULT,
                                  CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top,
                                  owner, nullptr, instance_, &state);
    if (window == nullptr || !state.initialize(window)) {
        if (window != nullptr) DestroyWindow(window);
        if (window == nullptr) window_slot.never_created = true;
        retire(id);
        outcome.admission.status.error = HostServiceError::backend_failure;
        outcome.admission.window = {};
        outcome.failure_code = window == nullptr ? 4 : 5;
        return outcome;
    }
    report_native_frame(id, window);
    if (floating) follow_owner_caption_scheme(owner, window);
    // A popup is placed where the program put it, before it is shown.
    if (popup.has_value()) {
        const Rect content{(*popup).origin.x, (*popup).origin.y, initial_size.width, initial_size.height};
        if (!request_frame_impl(id, content).accepted()) {
            DestroyWindow(window);
            retire(id);
            outcome.admission.status.error = HostServiceError::backend_failure;
            outcome.admission.window = {};
            outcome.failure_code = 5;
            return outcome;
        }
    }
    if (!runtime) return outcome;
    if (!state.will_show()) {
        DestroyWindow(window);
        retire(id);
        outcome.admission.status.error = HostServiceError::backend_failure;
        outcome.admission.window = {};
        outcome.failure_code = 5;
        return outcome;
    }
    window_created(id, initially_visible);
    if (initially_visible && !floating) {
        const bool activates = kind != HostWindowKind::tool && !passive_popup;
        state.show_with(activates ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
    if (close_after_launch) PostMessageW(window, WM_CLOSE, 0, 0);
    return outcome;
}

void WindowsApplicationRun::report_foreground() {
    const HWND foreground = GetForegroundWindow();
    DWORD process = 0U;
    if (foreground != nullptr) GetWindowThreadProcessId(foreground, &process);
    report_application_active(foreground != nullptr && process == GetCurrentProcessId());
}

HostServiceStatus WindowsApplicationRun::show_impl(HostWindowId window) {
    WindowsRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    (*(*found).state).show_without_activation();
    return {};
}

HostServiceStatus WindowsApplicationRun::hide_impl(HostWindowId window) {
    WindowsRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    (*(*found).state).hide_for_policy();
    return {};
}

void WindowsApplicationRun::report_native_frame(HostWindowId window, HWND native_window) {
    if (!live(window) || native_window == nullptr) return;
    const HostWindowFrame frame = native_window_frame(native_window);
    report_frame(window, frame);
}

void WindowsApplicationRun::publish_final_metrics(WindowsRunSlot& run_slot) {
    if (!run_slot.state) return;
    const std::string metrics = (*run_slot.state).metrics_json();
    const std::string host = (*run_slot.state).host_json();
    if (run_slot.print_metrics_on_close) {
        std::fprintf(stdout, "{\"stable_id\":\"%s\",\"window\":%s,\"host\":%s}\n",
                     run_slot.stable_id.c_str(), metrics.c_str(), host.c_str());
    }
    if (run_slot.final_snapshot) run_slot.final_snapshot(metrics, host);
}

void WindowsApplicationRun::reclaim() {
    for (std::size_t index = slots_.size(); index > 0U; --index) {
        WindowsRunSlot& candidate = *slots_[index - 1U];
        if (candidate.initial || !candidate.state) continue;
        const bool closed = (*candidate.state).destroyed();
        if (!closed && !candidate.never_created) continue;
        // A window that was never created has no metrics to publish.
        if (closed) publish_final_metrics(candidate);
        slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(index - 1U));
    }
}

HostWindowFrameResult WindowsApplicationRun::frame_impl(HostWindowId window) {
    HostWindowFrameResult result;
    const HWND native_window = native(window);
    if (native_window == nullptr) {
        result.status.error = HostServiceError::after_shutdown;
        return result;
    }
    result.frame = native_window_frame(native_window);
    return result;
}

HostServiceStatus WindowsApplicationRun::request_frame_impl(HostWindowId window, Rect content) {
    WindowsRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    WindowsHostState& state = *(*found).state;
    const HWND native_window = state.native_window();
    if (native_window == nullptr) return {HostServiceError::after_shutdown};
    const Size minimum = state.minimum_size();
    if (content.width < minimum.width || content.height < minimum.height) {
        return {HostServiceError::invalid_argument};
    }
    if (state.frame_fixed()) return {HostServiceError::unsupported};
    const std::vector<HostMonitor> monitors = device_monitors();
    double target_scale = 1.0;
    const DesktopDevicePoint device_origin =
        desktop_device_point(monitors, DesktopLogicalPoint{content.x, content.y}, target_scale);
    RECT frame{0, 0, static_cast<LONG>(std::lround(content.width * target_scale)),
               static_cast<LONG>(std::lround(content.height * target_scale))};
    adjust_client_frame(frame, static_cast<DWORD>(GetWindowLongPtrW(native_window, GWL_STYLE)),
                        static_cast<DWORD>(GetWindowLongPtrW(native_window, GWL_EXSTYLE)),
                        target_scale);
    // `frame` now holds the decorations as negative left and top offsets
    // around the client rectangle placed at the origin.
    const int x = static_cast<int>(std::lround(device_origin.x)) + frame.left;
    const int y = static_cast<int>(std::lround(device_origin.y)) + frame.top;
    // WM_MOVE and WM_SIZE report the new frame before SetWindowPos returns.
    const BOOL moved = SetWindowPos(native_window, nullptr, x, y, frame.right - frame.left,
                                    frame.bottom - frame.top,
                                    SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    if (moved == FALSE) return {HostServiceError::backend_failure};
    return {};
}

HostScreenPointResult WindowsApplicationRun::pointer_position_impl() {
    HostScreenPointResult result;
    POINT device{};
    if (!GetCursorPos(&device)) {
        result.status.error = HostServiceError::backend_failure;
        return result;
    }
    const std::vector<HostMonitor> monitors = device_monitors();
    result.position = desktop_logical_point(
        monitors, DesktopDevicePoint{static_cast<double>(device.x), static_cast<double>(device.y)});
    return result;
}

HostWindowAtResult WindowsApplicationRun::window_at_impl(DesktopLogicalPoint point) {
    HostWindowAtResult result;
    const std::vector<HostMonitor> monitors = device_monitors();
    double scale = 1.0;
    const DesktopDevicePoint device = desktop_device_point(monitors, point, scale);
    const POINT native_point{static_cast<LONG>(std::lround(device.x)),
                             static_cast<LONG>(std::lround(device.y))};
    // WindowFromPoint skips hidden and disabled windows; its result may be a
    // child, so the top-level window is taken. Owned windows are top level.
    const HWND hit = WindowFromPoint(native_point);
    if (hit == nullptr) return result;
    const HWND top = GetAncestor(hit, GA_ROOT);
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        WindowsRunSlot& candidate = *slots_[index];
        if (!candidate.state || (*candidate.state).native_window() != top) continue;
        if (live(candidate.id)) result.window = candidate.id;
        break;
    }
    return result;
}

HostServiceStatus WindowsApplicationRun::bring_to_front_impl(HostWindowId window) {
    const HWND native_window = native(window);
    if (native_window == nullptr) return {HostServiceError::after_shutdown};
    // The topmost window of this run in the desktop z-order.
    HWND topmost_own{};
    for (HWND cursor = GetTopWindow(nullptr); cursor != nullptr && topmost_own == nullptr;
         cursor = GetWindow(cursor, GW_HWNDNEXT)) {
        for (std::size_t index = 0U; index < slots_.size(); ++index) {
            WindowsRunSlot& candidate = *slots_[index];
            if (candidate.state && (*candidate.state).native_window() == cursor &&
                live(candidate.id)) {
                topmost_own = cursor;
                break;
            }
        }
    }
    if (topmost_own == native_window) return {};
    // Insert directly above that window: after the window that precedes it,
    // or at the top of the non-topmost band when it is first or follows a
    // topmost window (inserting after a topmost window would not keep this
    // window out of the topmost band). Owned windows follow their owner, as
    // Win32 keeps them, because SWP_NOOWNERZORDER is not passed.
    HWND insert_after = HWND_TOP;
    if (topmost_own != nullptr) {
        const HWND previous = GetWindow(topmost_own, GW_HWNDPREV);
        const LONG_PTR previous_style = previous == nullptr ? 0 : GetWindowLongPtrW(previous, GWL_EXSTYLE);
        if (previous != nullptr && (previous_style & WS_EX_TOPMOST) == 0) insert_after = previous;
    }
    const BOOL ordered = SetWindowPos(native_window, insert_after, 0, 0, 0, 0,
                                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (ordered == FALSE) return {HostServiceError::backend_failure};
    return {};
}

bool WindowsApplicationRun::turn(MSG& quit_message, const bool outermost) {
    // A window opened or closed in the previous turn changes the slots;
    // their count is read afresh in every turn.
    clocks_.clear();
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        const HANDLE clock = (*(*slots_[index]).state).live_frame_clock();
        if (clock != nullptr) clocks_.push_back(clock);
    }
    if (clocks_.size() >= MAXIMUM_WAIT_OBJECTS) {
        clocks_.resize(MAXIMUM_WAIT_OBJECTS - 1U);
    }
    const DWORD wait = MsgWaitForMultipleObjectsEx(
        static_cast<DWORD>(clocks_.size()),
        clocks_.empty() ? nullptr : clocks_.data(), INFINITE,
        QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (wait == WAIT_FAILED) {
        quit_message = MSG{};
        return true;
    }
    if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + clocks_.size()) {
        const HANDLE ready = clocks_[wait - WAIT_OBJECT_0];
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            WindowsHostState& state = *(*slots_[index]).state;
            if (state.live_frame_clock() == ready) {
                state.mark_live_frame_due();
                break;
            }
        }
    }

    // Input and managed callbacks receive a bounded turn, not an unbounded
    // queue drain, so presentation has a maximum queue latency.
    bool quit = false;
    const std::chrono::steady_clock::time_point batch_started = std::chrono::steady_clock::now();
    for (std::size_t drained = 0U; drained < 32U; ++drained) {
        MSG pending{};
        if (!detail::take_window_message(pending, drained % 2U == 0U)) break;
        if (pending.message == WM_QUIT) {
            quit_message = pending;
            quit = true;
            break;
        }
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
        // Finish an input/ordinary pair before applying the time budget.
        if (drained % 2U == 1U && std::chrono::steady_clock::now() - batch_started >=
            std::chrono::milliseconds(4)) {
            break;
        }
    }
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        WindowsHostState& state = *(*slots_[index]).state;
        const HANDLE clock = state.live_frame_clock();
        if (clock != nullptr && WaitForSingleObject(clock, 0U) == WAIT_OBJECT_0) {
            state.mark_live_frame_due();
        }
    }
    if (!quit) {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            (*(*slots_[index]).state).present_pending_frame();
        }
        // The end of an outermost turn: no message of a closed window is on
        // the stack, so runtime windows closed in it are released.
        if (outermost) reclaim();
    }
    return quit;
}

HostServiceStatus WindowsApplicationRun::run_modal_impl(HostWindowId window) {
    WindowsRunSlot* modal = slot(window);
    if (modal == nullptr) return {HostServiceError::after_shutdown};
    const HWND modal_native = (*(*modal).state).native_window();
    // Every other enabled window of the run is disabled, except windows
    // owned (at any depth) by the modal one. The list is fixed here: a
    // window opened during the loop keeps its own state. Capacity: one
    // entry per slot, reserved before the loop.
    std::vector<HWND> disabled;
    disabled.reserve(slots_.size());
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        WindowsRunSlot& candidate = *slots_[index];
        if (!candidate.state || (*candidate.state).destroyed()) continue;
        const HWND native_window = (*candidate.state).native_window();
        if (native_window == nullptr || native_window == modal_native) continue;
        if (!IsWindowEnabled(native_window)) continue;
        bool owned_by_modal = false;
        for (HWND owner_native = GetWindow(native_window, GW_OWNER); owner_native != nullptr;
             owner_native = GetWindow(owner_native, GW_OWNER)) {
            if (owner_native == modal_native) {
                owned_by_modal = true;
                break;
            }
        }
        if (owned_by_modal) continue;
        EnableWindow(native_window, FALSE);
        disabled.push_back(native_window);
    }
    const std::optional<HostWindowId> owner_id = owner(window);
    MSG quit_message{};
    bool quit = false;
    while (!quit && slot(window) != nullptr) {
        quit = turn(quit_message, false);
    }
    // The windows are enabled again before the owner is activated, as the
    // dialog manager does; otherwise Windows activates another
    // application when the modal window goes.
    for (std::size_t index = 0; index < disabled.size(); ++index) {
        if (IsWindow(disabled[index])) EnableWindow(disabled[index], TRUE);
    }
    if (owner_id.has_value() && !quit) {
        const HWND owner_native = native(*owner_id);
        if (owner_native != nullptr) {
            SetActiveWindow(owner_native);
            // The keyboard goes back to the owner, whose WM_ACTIVATE gives
            // its last focused control focus again (SurfaceRoot::restore_focus).
            SetFocus(owner_native);
        }
    }
    // The quit belongs to the outer loop: post it again for it to take.
    if (quit && quit_message.message == WM_QUIT) {
        PostQuitMessage(static_cast<int>(quit_message.wParam));
    }
    return {};
}

HostServiceStatus WindowsApplicationRun::destroy_window_impl(HostWindowId window) {
    WindowsRunSlot* found = slot(window);
    if (found == nullptr) return {HostServiceError::after_shutdown};
    // WM_DESTROY publishes the closed event and retires the identity before
    // DestroyWindow returns.
    (*(*found).state).destroy_for_application();
    return {};
}

HostMonitorResult WindowsApplicationRun::query_monitors_impl() {
    HostMonitorResult result;
    query_monitors_into_impl(result);
    return result;
}

// The monitors in logical units, written into `result`'s records in place:
// a repeated query on an unchanged set allocates nothing (a warm Splash or
// Popup placement queries on every show).
void WindowsApplicationRun::query_monitors_into_impl(HostMonitorResult& result) {
    result.status = {};
    LogicalMonitorFill fill{};
    fill.monitors = &result.monitors;
    EnumDisplayMonitors(nullptr, nullptr, &fill_logical_monitor, reinterpret_cast<LPARAM>(&fill));
    result.monitors.resize(fill.count);
    if (result.monitors.empty()) result.status.error = HostServiceError::backend_failure;
}

void WindowsHostState::report_frame_to_run() {
    if (run_ == nullptr || hwnd_ == nullptr) return;
    if (native_phase_ != NativePhase::ready && native_phase_ != NativePhase::visible) return;
    // A minimized window sits at -32000; its frame is not a place to follow.
    if (IsIconic(hwnd_)) {
        (*run_).report_minimized(window_id_, true);
        return;
    }
    (*run_).report_minimized(window_id_, false);
    (*run_).report_native_frame(window_id_, hwnd_);
}

void WindowsHostState::report_application_active_to_run(bool active) {
    if (run_ == nullptr) return;
    (*run_).report_application_active(active);
}

void WindowsHostState::hidden_on_close_to_run() noexcept {
    if (run_ == nullptr) return;
    (*run_).report_hidden_on_close(window_id_);
}

void WindowsHostState::retire_from_run() noexcept {
    if (run_ == nullptr) return;
    (*run_).retire(window_id_);
}

// The run's open operation. Only the UI thread reaches the run, and only
// while it lives.
struct WindowsRunOpenState final {
    const DWORD thread{GetCurrentThreadId()};
    WindowsApplicationRun* run{};
};

struct WindowsRunOpen final {
    std::weak_ptr<WindowsRunOpenState> owner{};
    HostWindowAdmission operator()(WindowsApplicationWindow entry) const {
        HostWindowAdmission refused;
        const std::shared_ptr<WindowsRunOpenState> state = owner.lock();
        if (!state || (*state).run == nullptr) {
            refused.status.error = HostServiceError::after_shutdown;
            return refused;
        }
        if ((*state).thread != GetCurrentThreadId()) {
            refused.status.error = HostServiceError::wrong_thread;
            return refused;
        }
        const WindowsApplicationRun::OpenOutcome outcome =
            (*(*state).run).open(std::move(entry), true);
        return outcome.admission;
    }
};

// Revokes the run's open operation and tells the runtime the windowing is
// ending; the run itself is shut down and released by the caller.
void end_run_windowing(WindowsRunOpenState& open_state, WindowsApplicationOptions& options,
                       bool published) {
    open_state.run = nullptr;
    if (published && options.windowing_ended) options.windowing_ended();
}
