#include "lab.hpp"

#include <dwmapi.h>

#include <cstdarg>
#include <cstdio>

namespace lab {

void report(const char* route, const char* key, const char* format, ...) {
    char value[512]{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(value, sizeof(value), format, arguments);
    va_end(arguments);
    std::printf("%s.%s=%s\n", route, key, value);
    std::fflush(stdout);
}

void report_hr(const char* route, const char* key, HRESULT result) {
    const char* verdict = SUCCEEDED(result) ? "ok" : "fail";
    report(route, key, "%s hr=0x%08lX", verdict, static_cast<unsigned long>(result));
}

void report_bool(const char* route, const char* key, BOOL result) {
    if (result != FALSE) {
        report(route, key, "ok");
        return;
    }
    const DWORD error = GetLastError();
    report(route, key, "fail error=%lu", static_cast<unsigned long>(error));
}

std::uint32_t pattern_pixel(int x, int y, int phase, bool with_alpha) {
    const std::uint32_t checker = static_cast<std::uint32_t>(((x / 16) + (y / 16)) & 1);
    std::uint32_t blue = static_cast<std::uint32_t>(x) & 255U;
    std::uint32_t green = static_cast<std::uint32_t>(y) & 255U;
    std::uint32_t red = (static_cast<std::uint32_t>(phase) * 60U + checker * 80U + 20U) & 255U;
    std::uint32_t alpha = 255U;
    if (with_alpha && x < window_width / 4) {
        // Premultiplied at alpha 128: every channel at most 128.
        alpha = 128U;
        blue = blue / 2U;
        green = green / 2U;
        red = red / 2U;
    } else if (with_alpha && x < window_width / 2) {
        alpha = 0U;
        blue = 0U;
        green = 0U;
        red = 0U;
    }
    const std::uint32_t pixel = (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
    return pixel;
}

void fill_pattern(std::uint32_t* pixels, int columns, int rows, int stride_pixels,
                  int origin_x, int origin_y, int phase, bool with_alpha) {
    for (int row = 0; row < rows; ++row) {
        std::uint32_t* const line = pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(stride_pixels);
        for (int column = 0; column < columns; ++column) {
            line[column] = pattern_pixel(origin_x + column, origin_y + row, phase, with_alpha);
        }
    }
}

namespace {

LRESULT CALLBACK lab_window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* const creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>((*creation).lpCreateParams));
    }
    if (message == WM_PAINT) {
        PaintTarget* const target = reinterpret_cast<PaintTarget*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (target != nullptr && (*target).paint != nullptr) {
            (*target).paint(window, (*target).context);
            return 0;
        }
        ValidateRect(window, nullptr);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(window, message, wparam, lparam);
}

constexpr const wchar_t* lab_class_name = L"gw_present_lab_window";

void register_lab_class() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = lab_window_procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = lab_class_name;
    RegisterClassExW(&window_class);
    registered = true;
}

struct CapturedImage final {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    std::uint32_t* pixels{};
};

bool allocate_capture(CapturedImage& image) {
    image.dc = CreateCompatibleDC(nullptr);
    if (image.dc == nullptr) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = window_width;
    info.bmiHeader.biHeight = -window_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* storage = nullptr;
    image.bitmap = CreateDIBSection(image.dc, &info, DIB_RGB_COLORS, &storage, nullptr, 0);
    if (image.bitmap == nullptr) return false;
    image.pixels = static_cast<std::uint32_t*>(storage);
    // A sentinel that no route draws, so "nothing was written" is visible.
    for (int index = 0; index < window_width * window_height; ++index) image.pixels[index] = 0x00ABCDEFU;
    image.previous = SelectObject(image.dc, image.bitmap);
    return true;
}

void release_capture(CapturedImage& image) {
    if (image.dc != nullptr && image.previous != nullptr) SelectObject(image.dc, image.previous);
    if (image.bitmap != nullptr) DeleteObject(image.bitmap);
    if (image.dc != nullptr) DeleteDC(image.dc);
    image = CapturedImage{};
}

Readback compare_capture(const CapturedImage& image, int phase, int scrolled_rows) {
    Readback result{};
    result.available = true;
    for (int row_index = 0; row_index < 6; ++row_index) {
        const int y = 8 + row_index * ((window_height - 16 - scrolled_rows) / 6);
        for (int column_index = 0; column_index < 8; ++column_index) {
            const int x = window_width / 2 + 4 + column_index * ((window_width / 2 - 8) / 8);
            const std::uint32_t seen = image.pixels[y * window_width + x] & 0x00FFFFFFU;
            const std::uint32_t expected = pattern_pixel(x, y + scrolled_rows, phase, false) & 0x00FFFFFFU;
            ++result.samples;
            if (seen == expected) ++result.matches;
            if (seen == 0x00ABCDEFU || seen == 0U) ++result.blank;
        }
    }
    return result;
}

} // namespace

HWND create_window(const wchar_t* title, DWORD extended_style, PaintTarget* target, bool show) {
    register_lab_class();
    RECT frame{40, 40, 40 + window_width, 40 + window_height};
    const DWORD style = WS_POPUP;
    AdjustWindowRectEx(&frame, style, FALSE, extended_style);
    HWND window = CreateWindowExW(extended_style | WS_EX_NOACTIVATE, lab_class_name, title, style,
                                  frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), target);
    if (window != nullptr && show) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
    return window;
}

void pump(DWORD milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    for (;;) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return;
        const DWORD remaining = static_cast<DWORD>(deadline - now);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, remaining, QS_ALLINPUT);
    }
}

void report_readback(const char* route, const char* key, HWND window, int phase, int scrolled_rows) {
    char method_key[128]{};
    CapturedImage printed{};
    if (allocate_capture(printed)) {
        const BOOL done = PrintWindow(window, printed.dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
        GdiFlush();
        std::snprintf(method_key, sizeof(method_key), "%s.printwindow", key);
        if (done == FALSE) {
            report(route, method_key, "unavailable error=%lu", static_cast<unsigned long>(GetLastError()));
        } else {
            const Readback seen = compare_capture(printed, phase, scrolled_rows);
            report(route, method_key, "matches=%d/%d blank=%d", seen.matches, seen.samples, seen.blank);
        }
    }
    release_capture(printed);
    CapturedImage copied{};
    if (allocate_capture(copied)) {
        HDC window_dc = GetDC(window);
        const BOOL done = window_dc != nullptr &&
            BitBlt(copied.dc, 0, 0, window_width, window_height, window_dc, 0, 0, SRCCOPY);
        if (window_dc != nullptr) ReleaseDC(window, window_dc);
        GdiFlush();
        std::snprintf(method_key, sizeof(method_key), "%s.windowdc", key);
        if (done == FALSE) {
            report(route, method_key, "unavailable error=%lu", static_cast<unsigned long>(GetLastError()));
        } else {
            const Readback seen = compare_capture(copied, phase, scrolled_rows);
            report(route, method_key, "matches=%d/%d blank=%d", seen.matches, seen.samples, seen.blank);
        }
    }
    release_capture(copied);
}

void report_environment() {
    report("env", "remote_session", "%d", GetSystemMetrics(SM_REMOTESESSION));
    report("env", "remote_control", "%d", GetSystemMetrics(SM_REMOTECONTROL));
    BOOL composited = FALSE;
    const HRESULT composition = DwmIsCompositionEnabled(&composited);
    report("env", "dwm_composition", "%d hr=0x%08lX", composited, static_cast<unsigned long>(composition));
    wchar_t desktop_name[128]{};
    DWORD needed = 0;
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop_name,
                              sizeof(desktop_name), &needed);
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    wchar_t input_name[128]{};
    if (input != nullptr) {
        GetUserObjectInformationW(input, UOI_NAME, input_name, sizeof(input_name), &needed);
        CloseDesktop(input);
    }
    report("env", "desktop", "%ls input_desktop=%ls", desktop_name, input != nullptr ? input_name : L"(unreadable)");
    using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFunction get_version =
        reinterpret_cast<RtlGetVersionFunction>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (get_version != nullptr) get_version(&version);
    report("env", "os", "%lu.%lu.%lu", static_cast<unsigned long>(version.dwMajorVersion),
           static_cast<unsigned long>(version.dwMinorVersion), static_cast<unsigned long>(version.dwBuildNumber));
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    report("env", "session_id", "%lu active_console=%lu", static_cast<unsigned long>(session),
           static_cast<unsigned long>(WTSGetActiveConsoleSessionId()));
}

} // namespace lab
