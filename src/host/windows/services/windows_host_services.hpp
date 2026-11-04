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
        owner_ = nullptr;
        cursor_model_ = nullptr;
    }

    // The granted backdrop on the owner window (stage T5, C2).
    HostServiceStatus apply_window_backdrop_impl(WindowBackdrop backdrop) override {
        const HostServiceStatus applied = gui_forms::host::detail::apply_windows_backdrop(owner_, backdrop);
        return applied;
    }

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

    HWND owner_{};
    Window* cursor_model_{};
    std::unique_ptr<gui_forms::host::detail::WindowsNotifyIcons> notify_icons_;
    std::array<COLORREF, 16> custom_colors_{};
    // The open message box's request identity; zero while none is open.
    std::uint64_t message_help_request_{};
};
