#pragma once

// Included inside the Win32 adapter's private namespace after its conversion,
// dialog, cursor, and monitor helpers have been declared. Keeping this state
// owner separate makes service policy review independent from message routing.
class WindowsHostServices final : public HostServices {
public:
    WindowsHostServices() : HostServices(windows_capabilities()) {}

    ~WindowsHostServices() override { shutdown(); }

    void bind_owner(HWND owner, Window& model) noexcept { owner_ = owner; cursor_model_ = &model; }
    // WM_HELP at the owner window: a help request of the open message box,
    // if one is open. F1 held down tells the key from the Help button.
    bool publish_message_help() {
        if (message_help_request_ == 0U) return false;
        HostDialogHelpEvent event{};
        event.request_id = message_help_request_;
        event.keyboard_initiated = (GetKeyState(VK_F1) & 0x8000) != 0;
        return publish_dialog_help(event);
    }
    bool maintain_hidden_cursor() noexcept {
        if (!cursor_hidden() || cursor_model_ == nullptr) { return false; }
        const CursorStatus authority = cursor_authority_impl(*cursor_model_, true);
        if (!authority.accepted()) { revoke_cursor_interaction(); return false; }
        const CursorStatus hidden = hide_cursor_impl();
        if (!hidden.accepted()) { revoke_cursor_interaction(CursorError::native_failure); return false; }
        return true;
    }

protected:
    CursorCapabilities cursor_capabilities_impl() const noexcept override { return {true, true}; }
    CursorStatus cursor_authority_impl(const Window& window, bool require_pointer) const noexcept override {
        if (cursor_model_ != &window) { return {CursorError::stale_window}; }
        const CursorStatus result = windows_detail::cursor_authority(owner_, require_pointer);
        return result;
    }
    CursorStatus hide_cursor_impl() noexcept override {
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, owner_, 0};
        const BOOL tracked = TrackMouseEvent(&tracking);
        if (!tracked) { return {CursorError::native_failure}; }
        SetCursor(nullptr);
        const HCURSOR current = GetCursor();
        const CursorStatus result{current == nullptr ? CursorError::none : CursorError::native_failure};
        return result;
    }
    CursorStatus restore_cursor_impl() noexcept override {
        try {
            if (owner_ == nullptr || cursor_model_ == nullptr) { return {CursorError::stale_window}; }
            // Outside this window's input scope, Windows owns the applicable cursor.
            // Do not overwrite another application's shape during focus departure.
            const HWND foreground = GetForegroundWindow();
            const HWND capture = GetCapture();
            if (foreground != owner_ && capture != owner_) { return {}; }
            POINT point{};
            const BOOL located = GetCursorPos(&point);
            if (!located) { return {CursorError::native_failure}; }
            const HWND pointed_window = WindowFromPoint(point);
            if (pointed_window != owner_ && capture != owner_) { return {}; }
            const BOOL converted = ScreenToClient(owner_, &point);
            if (!converted) { return {CursorError::native_failure}; }
            Window& model = *cursor_model_;
            const double scale = model.scale();
            const Control::Ptr captured = model.captured_control();
            const Control::Ptr target = captured ? captured : model.hit_test({point.x / scale, point.y / scale});
            const CursorImagesPtr images = target ? (*target).effective_cursor_images() : CursorImagesPtr{};
            const CursorKind kind = target ? (*target).effective_cursor() : CursorKind::arrow;
            const HostServiceStatus restored = set_custom_cursor(images, scale, kind);
            const CursorStatus result{restored.accepted() ? CursorError::none : CursorError::native_failure};
            return result;
        } catch (...) {
            // Window hit testing/custom raster preparation may allocate. Cleanup
            // cannot throw across native destruction; retain an explicit failure.
            return {CursorError::native_failure};
        }
    }
    CursorStatus warp_cursor_impl(int client_x, int client_y) noexcept override {
        if (cursor_model_ == nullptr) { return {CursorError::stale_window}; }
        POINT target{};
        const CursorStatus prepared = windows_detail::cursor_screen_target(owner_, client_x, client_y, target);
        if (!prepared.accepted()) { return prepared; }
        const CursorStatus authority = cursor_authority_impl(*cursor_model_, false);
        if (!authority.accepted()) { return authority; }
        const BOOL placed = SetCursorPos(target.x, target.y);
        const CursorStatus result{placed ? CursorError::none : CursorError::native_failure};
        return result;
    }
    HostMonitorResult query_monitors_impl() override {
        HostMonitorResult result;
        const BOOL enumerated = EnumDisplayMonitors(
            nullptr, nullptr, &WindowsHostServices::collect_monitor,
            reinterpret_cast<LPARAM>(&result.monitors));
        if (!enumerated || result.monitors.empty()) {
            result.status.error = HostServiceError::backend_failure;
        }
        return result;
    }

    HostServiceStatus set_cursor_impl(CursorKind cursor) override {
        if (cursor_hidden()) { return {}; }
        return apply_system_cursor(cursor)
            ? HostServiceStatus{}
            : HostServiceStatus{HostServiceError::backend_failure};
    }

    HostServiceStatus set_custom_cursor_impl(const CursorImagesPtr& images,
                                              const CursorImage& image) override {
        for (const CursorEntry& entry : cursors_) {
            if (entry.images == images && entry.scale == image.scale) {
                if (!cursor_hidden()) { SetCursor(entry.native); }
                return {};
            }
        }
        if (cursors_.capacity() < 32) cursors_.reserve(32);
        HCURSOR cursor = windows_detail::create_image_cursor(*images, image);
        if (cursor == nullptr) return {HostServiceError::backend_failure};
        if (!cursor_hidden()) { SetCursor(cursor); }
        if (cursors_.size() >= 32) {
            DestroyCursor(cursors_.front().native);
            cursors_.erase(cursors_.begin());
        }
        cursors_.push_back({images, image.scale, cursor});
        return {};
    }

    // Notification-area icons: one WindowsNotifyIcons, created with the first
    // icon, whose hidden window reports shell events back through
    // forward_tray_event on the UI thread.
    HostServiceStatus add_tray_icon_impl(HostTrayIconId icon_id, const HostTrayIconRequest& request) override {
        if (!notify_icons_) {
            notify_icons_ = std::make_unique<gui_forms::host::detail::WindowsNotifyIcons>(
                Delegate<const HostTrayIconEvent&>::bind<WindowsHostServices, &WindowsHostServices::forward_tray_event>(
                    *this));
        }
        return (*notify_icons_).add(icon_id, request);
    }
    HostServiceStatus update_tray_icon_impl(HostTrayIconId icon_id, const HostTrayIconRequest& request) override {
        if (!notify_icons_) return {HostServiceError::backend_failure};
        return (*notify_icons_).update(icon_id, request);
    }
    void remove_tray_icon_impl(HostTrayIconId icon_id) noexcept override {
        if (notify_icons_) (*notify_icons_).remove(icon_id);
    }
    HostServiceStatus show_tray_notification_impl(HostTrayIconId icon_id,
                                                  const HostTrayNotificationRequest& request) override {
        if (!notify_icons_) return {HostServiceError::backend_failure};
        return (*notify_icons_).notify(icon_id, request);
    }
    HostTrayMenuResult show_tray_menu_impl(HostTrayIconId icon_id, const HostTrayMenuRequest& request) override {
        if (!notify_icons_) {
            HostTrayMenuResult refused;
            refused.status = {HostServiceError::backend_failure};
            return refused;
        }
        return (*notify_icons_).show_menu(icon_id, request);
    }

    void shutdown_impl() noexcept override {
        notify_icons_.reset();
        for (const CursorEntry& entry : cursors_) {
            if (GetCursor() == entry.native) apply_system_cursor(CursorKind::arrow);
            DestroyCursor(entry.native);
        }
        cursors_.clear();
        release_chrome_icons();
        owner_ = nullptr;
        cursor_model_ = nullptr;
    }

    // The granted backdrop on the owner window (stage T5, C2).
    HostServiceStatus apply_window_backdrop_impl(WindowBackdrop backdrop) override {
        const HostServiceStatus applied = gui_forms::host::detail::apply_windows_backdrop(owner_, backdrop);
        return applied;
    }

    // The owner window's corners (DWM corner preference; the window outer
    // band follows the report).
    HostWindowCornersResult apply_window_corners_impl(WindowCornerPreference preference) override {
        const HostWindowCornersResult applied = gui_forms::host::detail::apply_windows_corners(owner_, preference);
        return applied;
    }

    // Window's chrome properties on the owner window. Every property maps:
    // the frame and the boxes are style bits, the state is ShowWindow (held
    // for the next show while the window is hidden), the start position
    // places the window before its first show, the opacity is a layered
    // window's constant alpha, and the taskbar button is WS_EX_APPWINDOW.
    WindowChromeFields supported_window_chrome() const noexcept override {
        const WindowChromeFields all{WindowChromeFields::all_bits};
        return all;
    }
    HostWindowChromeResult apply_window_chrome_impl(const WindowChrome& chrome, WindowChromeFields fields) override {
        HostWindowChromeResult result{};
        if (owner_ == nullptr) {
            result.status = HostServiceStatus{HostServiceError::after_shutdown};
            return result;
        }
        const bool visible = IsWindowVisible(owner_) != FALSE;
        if (fields.contains(WindowChromeField::text)) {
            const std::wstring title = wide_from_utf8(chrome.text);
            if (SetWindowTextW(owner_, title.c_str())) result.applied.add(WindowChromeField::text);
        }
        if (fields.contains(WindowChromeField::icon) && apply_chrome_icon(chrome.icon)) {
            result.applied.add(WindowChromeField::icon);
        }
        const WindowChromeFields frame_fields{fields.bits & frame_field_bits};
        if (!frame_fields.empty()) {
            apply_chrome_frame(chrome, fields);
            result.applied.bits = result.applied.bits | frame_fields.bits;
        }
        if (fields.contains(WindowChromeField::top_most)) {
            const HWND order = chrome.top_most ? HWND_TOPMOST : HWND_NOTOPMOST;
            if (SetWindowPos(owner_, order, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) {
                result.applied.add(WindowChromeField::top_most);
            }
        }
        if (fields.contains(WindowChromeField::opacity) && apply_chrome_opacity(chrome.opacity)) {
            result.applied.add(WindowChromeField::opacity);
        }
        if (fields.contains(WindowChromeField::show_in_taskbar) &&
            apply_chrome_taskbar(chrome.show_in_taskbar, visible)) {
            result.applied.add(WindowChromeField::show_in_taskbar);
        }
        if (fields.contains(WindowChromeField::minimum_size)) {
            // WM_GETMINMAXINFO reads it (WindowsHostState::minimum_size); the
            // window is not grown at once, the next resize honours it.
            chrome_minimum_size_ = chrome.minimum_size;
            result.applied.add(WindowChromeField::minimum_size);
        }
        if (fields.contains(WindowChromeField::start_position) && !shown_once_) {
            pending_start_position_ = chrome.start_position;
            result.applied.add(WindowChromeField::start_position);
        }
        if (fields.contains(WindowChromeField::window_state)) {
            if (visible) {
                ShowWindow(owner_, show_command_for(chrome.window_state, SW_RESTORE));
            } else {
                pending_window_state_ = chrome.window_state;
                pending_window_state_set_ = true;
            }
            result.applied.add(WindowChromeField::window_state);
        }
        return result;
    }

public:
    // The client area's least size Window's MinimumSize asks for, in logical
    // units; zero on a side keeps the window's own minimum there.
    [[nodiscard]] Size chrome_minimum_size() const noexcept { return chrome_minimum_size_; }
    // The show command for a show of the owner window: `ordinary` unless a
    // window state was written while the window was hidden. Before the first
    // show it also places the window for its start position.
    [[nodiscard]] int show_command(int ordinary) noexcept {
        if (owner_ != nullptr && !shown_once_) place_for_start_position();
        shown_once_ = true;
        if (!pending_window_state_set_) return ordinary;
        pending_window_state_set_ = false;
        const int command = show_command_for(pending_window_state_, ordinary);
        return command;
    }

protected:

    HostServiceStatus set_pointer_capture_impl(bool captured,
                                               std::uint64_t) override {
        if (owner_ == nullptr) return {HostServiceError::backend_failure};
        if (captured) {
            SetCapture(owner_);
            return GetCapture() == owner_ ? HostServiceStatus{}
                                          : HostServiceStatus{HostServiceError::backend_failure};
        }
        if (GetCapture() == owner_ && !ReleaseCapture()) {
            return {HostServiceError::backend_failure};
        }
        return {};
    }

    HostClipboardTextResult read_clipboard_text_impl() override {
        HostClipboardTextResult result;
        result.generation = static_cast<std::uint64_t>(GetClipboardSequenceNumber());
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return result;
        if (!gui_forms::host::detail::open_clipboard_with_retry(owner_)) {
            result.status.error = HostServiceError::backend_failure;
            return result;
        }
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        const wchar_t* locked = data == nullptr
            ? nullptr : static_cast<const wchar_t*>(GlobalLock(data));
        if (locked == nullptr) {
            CloseClipboard();
            result.status.error = HostServiceError::backend_failure;
            return result;
        }
        result.text_utf8 = utf8_from_wide(locked);
        result.has_text = true;
        GlobalUnlock(data);
        CloseClipboard();
        return result;
    }

    HostServiceStatus write_clipboard_text_impl(std::string_view text) override {
        const std::wstring wide = wide_from_utf8(text);
        if (!text.empty() && wide.empty()) return {HostServiceError::invalid_utf8};
        if (!gui_forms::host::detail::open_clipboard_with_retry(owner_)) return {HostServiceError::backend_failure};
        if (!EmptyClipboard()) {
            CloseClipboard();
            return {HostServiceError::backend_failure};
        }
        const std::size_t bytes = (wide.size() + 1U) * sizeof(wchar_t);
        HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (storage == nullptr) {
            CloseClipboard();
            return {HostServiceError::backend_failure};
        }
        void* destination = GlobalLock(storage);
        if (destination == nullptr) {
            GlobalFree(storage);
            CloseClipboard();
            return {HostServiceError::backend_failure};
        }
        std::memcpy(destination, wide.c_str(), bytes);
        GlobalUnlock(storage);
        if (SetClipboardData(CF_UNICODETEXT, storage) == nullptr) {
            GlobalFree(storage);
            CloseClipboard();
            return {HostServiceError::backend_failure};
        }
        CloseClipboard();
        return {};
    }

    HostClipboardFilesResult read_clipboard_files_impl() override {
        HostClipboardFilesResult result;
        if (!IsClipboardFormatAvailable(CF_HDROP)) return result;
        if (!gui_forms::host::detail::open_clipboard_with_retry(owner_)) {
            result.status.error = HostServiceError::backend_failure;
            return result;
        }
        struct ClipboardScope {
            ~ClipboardScope() { CloseClipboard(); }
        } scope;
        result.generation = static_cast<std::uint64_t>(GetClipboardSequenceNumber());
        const HDROP files = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        const UINT count = files == nullptr ? 0 : DragQueryFileW(files, 0xFFFFFFFFU, nullptr, 0);
        if (files == nullptr || count > maximum_dialog_paths) {
            result.status.error = files == nullptr ? HostServiceError::backend_failure
                                                   : HostServiceError::too_large;
        } else {
            for (UINT index = 0; index < count; ++index) {
                const UINT length = DragQueryFileW(files, index, nullptr, 0);
                if (length == 0 || length > maximum_dialog_text_bytes) {
                    result.status.error = HostServiceError::too_large;
                    break;
                }
                std::vector<wchar_t> path(static_cast<std::size_t>(length) + 1U);
                if (DragQueryFileW(files, index, path.data(), length + 1U) != length) {
                    result.status.error = HostServiceError::backend_failure;
                    break;
                }
                result.paths_utf8.push_back(utf8_from_wide(path.data()));
            }
        }
        return result;
    }

    HostClipboardImageResult read_clipboard_image_impl() override {
        return gui_forms::host::detail::read_windows_clipboard_image(owner_);
    }

    HostServiceStatus write_clipboard_image_impl(HostImageView image) override {
        return gui_forms::host::detail::write_windows_clipboard_image(owner_, image);
    }

    HostDialogResult show_dialog_impl(const HostDialogRequest& request) override {
        if (std::holds_alternative<HostMessageDialogRequest>(request.payload)) {
            // WM_HELP reaches the owner window while the box is open
            // (publish_message_help); nested boxes restore the outer one.
            const std::uint64_t outer = message_help_request_;
            message_help_request_ = request.request_id;
            const HostDialogResult result = native_message_dialog(
                owner_, request.request_id,
                std::get<HostMessageDialogRequest>(request.payload));
            message_help_request_ = outer;
            return result;
        }
        if (std::holds_alternative<HostOpenFileDialogRequest>(request.payload)) {
            return native_open_dialog(
                owner_, request.request_id,
                std::get<HostOpenFileDialogRequest>(request.payload));
        }
        if (std::holds_alternative<HostSaveFileDialogRequest>(request.payload)) {
            return native_save_dialog(
                owner_, request.request_id,
                std::get<HostSaveFileDialogRequest>(request.payload));
        }
        if (std::holds_alternative<HostFolderDialogRequest>(request.payload)) {
            return native_folder_dialog(
                owner_, request.request_id,
                std::get<HostFolderDialogRequest>(request.payload));
        }
        return native_color_dialog(
            owner_, request.request_id,
            std::get<HostColorDialogRequest>(request.payload), custom_colors_);
    }

    HostServiceStatus play_sound_cue_impl(
        const HostSoundCueRequest& request) override {
        const UINT type = sound_cue_beep(request.cue);
        return MessageBeep(type) ? HostServiceStatus{}
                                 : HostServiceStatus{HostServiceError::backend_failure};
    }


private:
    // The MessageBeep sound for a cue.
    static UINT sound_cue_beep(HostSoundCue cue) noexcept {
        switch (cue) {
        case HostSoundCue::notification: return MB_ICONASTERISK;
        case HostSoundCue::success: return MB_OK;
        case HostSoundCue::warning: return MB_ICONEXCLAMATION;
        case HostSoundCue::error: return MB_ICONHAND;
        case HostSoundCue::operation_complete: return MB_OK;
        }
        // A cue outside the enumeration plays the default beep.
        return MB_OK;
    }

    void forward_tray_event(const HostTrayIconEvent& event) { publish_tray_icon_event(event); }

    struct CursorEntry final {
        CursorImagesPtr images;
        double scale;
        HCURSOR native;
    };
    std::vector<CursorEntry> cursors_;
    static BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT,
                                         LPARAM context) {
        std::vector<HostMonitor>& monitors =
            *reinterpret_cast<std::vector<HostMonitor>*>(context);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) return TRUE;
        HostMonitor value;
        value.id = utf8_from_wide(info.szDevice);
        if (value.id.empty()) {
            value.id = "win32.monitor." + std::to_string(
                reinterpret_cast<std::uintptr_t>(monitor));
        }
        value.frame = {
            static_cast<double>(info.rcMonitor.left),
            static_cast<double>(info.rcMonitor.top),
            static_cast<double>(info.rcMonitor.right - info.rcMonitor.left),
            static_cast<double>(info.rcMonitor.bottom - info.rcMonitor.top)};
        value.work_area = {
            static_cast<double>(info.rcWork.left),
            static_cast<double>(info.rcWork.top),
            static_cast<double>(info.rcWork.right - info.rcWork.left),
            static_cast<double>(info.rcWork.bottom - info.rcWork.top)};
        value.scale = native_monitor_scale(monitor);
        value.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0U;
        monitors.push_back(std::move(value));
        return TRUE;
    }

    // The chrome fields that are style bits of the owner window.
    static constexpr std::uint32_t frame_field_bits =
        static_cast<std::uint32_t>(WindowChromeField::form_border_style) |
        static_cast<std::uint32_t>(WindowChromeField::minimize_box) |
        static_cast<std::uint32_t>(WindowChromeField::maximize_box) |
        static_cast<std::uint32_t>(WindowChromeField::control_box);

    static int show_command_for(FormWindowState state, int ordinary) noexcept {
        switch (state) {
        case FormWindowState::minimized:
            return ordinary == SW_SHOWNOACTIVATE ? SW_SHOWMINNOACTIVE : SW_SHOWMINIMIZED;
        case FormWindowState::maximized: return SW_SHOWMAXIMIZED;
        case FormWindowState::normal: return ordinary;
        }
        return ordinary;
    }

    // Rewrites the style bits the named frame fields govern and keeps the
    // others, so an unwritten box keeps the style the window opened with.
    // Windows itself hides the boxes without WS_SYSMENU and on tool frames.
    void apply_chrome_frame(const WindowChrome& chrome, WindowChromeFields fields) noexcept {
        DWORD style = static_cast<DWORD>(GetWindowLongPtrW(owner_, GWL_STYLE));
        DWORD extended = static_cast<DWORD>(GetWindowLongPtrW(owner_, GWL_EXSTYLE));
        if (fields.contains(WindowChromeField::form_border_style)) {
            const DWORD frame_mask = WS_POPUP | WS_CAPTION | WS_THICKFRAME;
            const DWORD extended_mask = WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE;
            style = style & ~frame_mask;
            extended = extended & ~extended_mask;
            switch (chrome.form_border_style) {
            case FormBorderStyle::none: style = style | WS_POPUP; break;
            case FormBorderStyle::fixed_single: style = style | WS_CAPTION; break;
            case FormBorderStyle::fixed_3d:
                style = style | WS_CAPTION;
                extended = extended | WS_EX_CLIENTEDGE;
                break;
            case FormBorderStyle::fixed_dialog:
                style = style | WS_CAPTION;
                extended = extended | WS_EX_DLGMODALFRAME;
                break;
            case FormBorderStyle::sizable: style = style | WS_CAPTION | WS_THICKFRAME; break;
            case FormBorderStyle::fixed_tool_window:
                style = style | WS_CAPTION;
                extended = extended | WS_EX_TOOLWINDOW;
                break;
            case FormBorderStyle::sizable_tool_window:
                style = style | WS_CAPTION | WS_THICKFRAME;
                extended = extended | WS_EX_TOOLWINDOW;
                break;
            default:
                // Window refuses a value outside the enumeration when it is
                // set, so none arrives; one would leave the frame bits clear.
                break;
            }
        }
        if (fields.contains(WindowChromeField::control_box)) {
            style = chrome.control_box ? (style | WS_SYSMENU) : (style & ~static_cast<DWORD>(WS_SYSMENU));
        }
        if (fields.contains(WindowChromeField::minimize_box)) {
            style = chrome.minimize_box ? (style | WS_MINIMIZEBOX) : (style & ~static_cast<DWORD>(WS_MINIMIZEBOX));
        }
        if (fields.contains(WindowChromeField::maximize_box)) {
            style = chrome.maximize_box ? (style | WS_MAXIMIZEBOX) : (style & ~static_cast<DWORD>(WS_MAXIMIZEBOX));
        }
        SetWindowLongPtrW(owner_, GWL_STYLE, static_cast<LONG_PTR>(style));
        SetWindowLongPtrW(owner_, GWL_EXSTYLE, static_cast<LONG_PTR>(extended));
        // The frame is recomputed (WM_NCCALCSIZE); the resize that follows
        // reaches the model as usual.
        SetWindowPos(owner_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    // The large and small icons from the one image (empty: the class icon).
    bool apply_chrome_icon(const HostImage& image) noexcept {
        HICON big_icon = nullptr;
        HICON small_icon = nullptr;
        if (image.width != 0U) {
            const std::span<const HostImage> images(&image, 1U);
            big_icon = gui_forms::host::detail::create_notify_icon_handle(images, GetSystemMetrics(SM_CXICON));
            small_icon = gui_forms::host::detail::create_notify_icon_handle(images, GetSystemMetrics(SM_CXSMICON));
            if (big_icon == nullptr || small_icon == nullptr) {
                if (big_icon != nullptr) DestroyIcon(big_icon);
                if (small_icon != nullptr) DestroyIcon(small_icon);
                return false;
            }
        }
        SendMessageW(owner_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon));
        SendMessageW(owner_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
        // The window no longer uses the previous handles.
        if (big_icon_ != nullptr) DestroyIcon(big_icon_);
        if (small_icon_ != nullptr) DestroyIcon(small_icon_);
        big_icon_ = big_icon;
        small_icon_ = small_icon;
        return true;
    }

    void release_chrome_icons() noexcept {
        if (big_icon_ == nullptr && small_icon_ == nullptr) return;
        if (owner_ != nullptr && IsWindow(owner_)) {
            SendMessageW(owner_, WM_SETICON, ICON_BIG, 0);
            SendMessageW(owner_, WM_SETICON, ICON_SMALL, 0);
        }
        if (big_icon_ != nullptr) DestroyIcon(big_icon_);
        if (small_icon_ != nullptr) DestroyIcon(small_icon_);
        big_icon_ = nullptr;
        small_icon_ = nullptr;
    }

    // A constant alpha through WS_EX_LAYERED; fully opaque drops the layered
    // style so the window keeps its ordinary redirection.
    bool apply_chrome_opacity(double opacity) noexcept {
        const DWORD extended = static_cast<DWORD>(GetWindowLongPtrW(owner_, GWL_EXSTYLE));
        if (opacity >= 1.0) {
            const DWORD opaque = extended & ~static_cast<DWORD>(WS_EX_LAYERED);
            SetWindowLongPtrW(owner_, GWL_EXSTYLE, static_cast<LONG_PTR>(opaque));
            return true;
        }
        SetWindowLongPtrW(owner_, GWL_EXSTYLE, static_cast<LONG_PTR>(extended | WS_EX_LAYERED));
        const BYTE alpha = static_cast<BYTE>(std::lround(opacity * 255.0));
        const BOOL set = SetLayeredWindowAttributes(owner_, 0, alpha, LWA_ALPHA);
        return set != FALSE;
    }

    // An owned window has a taskbar button only with WS_EX_APPWINDOW; an
    // unowned one always has one, so hiding it is not applied there. The
    // taskbar reads the style when the window is shown, so a visible window
    // is hidden and shown again.
    bool apply_chrome_taskbar(bool shown, bool visible) noexcept {
        const bool owned = GetWindow(owner_, GW_OWNER) != nullptr;
        if (!owned && !shown) return false;
        const DWORD extended = static_cast<DWORD>(GetWindowLongPtrW(owner_, GWL_EXSTYLE));
        const DWORD wanted =
            shown && owned ? (extended | WS_EX_APPWINDOW) : (extended & ~static_cast<DWORD>(WS_EX_APPWINDOW));
        if (wanted == extended) return true;
        SetWindowLongPtrW(owner_, GWL_EXSTYLE, static_cast<LONG_PTR>(wanted));
        if (visible) {
            ShowWindow(owner_, SW_HIDE);
            ShowWindow(owner_, SW_SHOWNA);
        }
        return true;
    }

    // Centres the window for center_screen (the work area of its monitor)
    // and center_parent (its owner, or the screen without one). The other
    // positions keep where the window was created.
    void place_for_start_position() noexcept {
        const FormStartPosition position = pending_start_position_;
        if (position != FormStartPosition::center_screen && position != FormStartPosition::center_parent) return;
        RECT frame{};
        if (!GetWindowRect(owner_, &frame)) return;
        RECT around{};
        const HWND parent = GetWindow(owner_, GW_OWNER);
        const bool over_parent =
            position == FormStartPosition::center_parent && parent != nullptr && GetWindowRect(parent, &around);
        if (!over_parent) {
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(MONITORINFO);
            const HMONITOR native = MonitorFromWindow(owner_, MONITOR_DEFAULTTOPRIMARY);
            if (!GetMonitorInfoW(native, &monitor)) return;
            around = monitor.rcWork;
        }
        const LONG width = frame.right - frame.left;
        const LONG height = frame.bottom - frame.top;
        const LONG left = around.left + ((around.right - around.left) - width) / 2;
        const LONG top = around.top + ((around.bottom - around.top) - height) / 2;
        SetWindowPos(owner_, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    HWND owner_{};
    Window* cursor_model_{};
    std::unique_ptr<gui_forms::host::detail::WindowsNotifyIcons> notify_icons_;
    std::array<COLORREF, 16> custom_colors_{};
    // The open message box's request identity; zero while none is open.
    std::uint64_t message_help_request_{};
    // The chrome icons the window shows; destroyed when replaced and at
    // shutdown, after the window lets them go.
    HICON big_icon_{};
    HICON small_icon_{};
    Size chrome_minimum_size_{};
    FormStartPosition pending_start_position_{FormStartPosition::windows_default_location};
    // A window state written while the window was hidden, for its next show.
    FormWindowState pending_window_state_{FormWindowState::normal};
    bool pending_window_state_set_{};
    bool shown_once_{};
};
