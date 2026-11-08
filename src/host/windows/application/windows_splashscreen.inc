// The Windows splashscreen (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part
// S): a WS_POPUP tool window, topmost, never activated and transparent to
// the mouse (HTTRANSPARENT, D4), centred on the primary monitor's work area
// and shown before Application::create. paint_splashscreen draws each frame
// into a CPU raster with the bundled fonts; WM_PAINT copies the kept frame.
// Included by windows_host.cpp inside gui_forms::host.

namespace {

constexpr wchar_t splashscreen_class_name[] = L"GUIForms.Splashscreen.v1";

// The last frame, top-down BGRA rows of width pixels each. Owned by the
// surface; the window procedure reads it through GWLP_USERDATA.
struct SplashscreenFrame final {
    std::vector<std::uint32_t> pixels;
    int width{};
    int height{};
};

LRESULT CALLBACK splashscreen_window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>((*create).lpCreateParams));
        return DefWindowProcW(window, message, wparam, lparam);
    }
    const SplashscreenFrame* frame = reinterpret_cast<const SplashscreenFrame*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (frame != nullptr && !(*frame).pixels.empty()) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = (*frame).width;
            info.bmiHeader.biHeight = -(*frame).height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(dc, 0, 0, static_cast<DWORD>((*frame).width), static_cast<DWORD>((*frame).height), 0,
                              0, 0, static_cast<UINT>((*frame).height), (*frame).pixels.data(), &info,
                              DIB_RGB_COLORS);
        }
        EndPaint(window, &paint);
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

class WindowsSplashscreenSurface final : public gui_forms::detail::SplashscreenSurface {
public:
    WindowsSplashscreenSurface() = default;
    ~WindowsSplashscreenSurface() override {
        if (window_ != nullptr && IsWindow(window_)) DestroyWindow(window_);
        window_ = nullptr;
    }
    WindowsSplashscreenSurface(const WindowsSplashscreenSurface&) = delete;
    WindowsSplashscreenSurface& operator=(const WindowsSplashscreenSurface&) = delete;

    // Registers the fonts and makes the window (hidden until the first
    // present); false when either fails.
    [[nodiscard]] bool open(const Size size) {
        if (!raster_.register_bundled_fonts(load_bundled_fonts(bundled_font_directory()))) return false;
        WNDCLASSEXW native_class{};
        native_class.cbSize = sizeof(native_class);
        native_class.lpfnWndProc = splashscreen_window_procedure;
        native_class.hInstance = GetModuleHandleW(nullptr);
        native_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        native_class.lpszClassName = splashscreen_class_name;
        if (RegisterClassExW(&native_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        const POINT origin{0, 0};
        const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!GetMonitorInfoW(monitor, &monitor_info)) return false;
        scale_ = native_monitor_scale(monitor);
        const int width = static_cast<int>(std::ceil(size.width * scale_));
        const int height = static_cast<int>(std::ceil(size.height * scale_));
        const RECT work = monitor_info.rcWork;
        const int x = work.left + (work.right - work.left - width) / 2;
        const int y = work.top + (work.bottom - work.top - height) / 2;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, splashscreen_class_name, L"",
                                  WS_POPUP, x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), &frame_);
        return window_ != nullptr;
    }

    bool present(const gui_forms::detail::SplashscreenFrame& frame) override {
        if (window_ == nullptr) return false;
        if (frame.images != nullptr && !raster_.synchronize_images(*frame.images)) return false;
        static_cast<void>(raster_.resize(frame.size, scale_));
        DamageRegion whole;
        whole.add(Rect{0.0, 0.0, frame.size.width, frame.size.height});
        raster_.begin_frame(whole);
        gui_forms::detail::paint_splashscreen(raster_, frame);
        raster_.end_frame();
        const std::byte* pixels = static_cast<const std::byte*>(raster_.pixels());
        if (pixels == nullptr) return false;
        frame_.width = static_cast<int>(raster_.pixel_width());
        frame_.height = static_cast<int>(raster_.pixel_height());
        const std::size_t row_pixels = static_cast<std::size_t>(frame_.width);
        frame_.pixels.resize(row_pixels * static_cast<std::size_t>(frame_.height));
        for (int row = 0; row < frame_.height; ++row) {
            std::memcpy(frame_.pixels.data() + static_cast<std::size_t>(row) * row_pixels,
                        pixels + static_cast<std::size_t>(row) * raster_.row_bytes(), row_pixels * 4U);
        }
        if (!shown_) {
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            shown_ = true;
        }
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
        // No run loop runs yet: deliver this window's pending messages.
        MSG message{};
        while (PeekMessageW(&message, window_, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return true;
    }

private:
    HWND window_{};
    double scale_{1.0};
    bool shown_{};
    SplashscreenFrame frame_{};
    render::SkiaRaster raster_{};
};

} // namespace

std::unique_ptr<gui_forms::detail::SplashscreenSurface> open_platform_splashscreen(const Size size) {
    std::unique_ptr<gui_forms::detail::SplashscreenSurface> result;
    std::unique_ptr<WindowsSplashscreenSurface> surface = std::make_unique<WindowsSplashscreenSurface>();
    if (!(*surface).open(size)) return result;
    result = std::move(surface);
    return result;
}
