#include "../src/surface/windows/services/windows_cursor.hpp"
#include <iostream>

static bool owned_window_cursor_validation() {
    using namespace gui_forms;
    const HINSTANCE module = GetModuleHandleW(nullptr);
    const wchar_t* class_name = L"GUI.Forms.CursorValidation.Hidden";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = module;
    window_class.lpszClassName = class_name;
    const ATOM registered = RegisterClassW(&window_class);
    if (registered == 0) { return false; }
    RECT allowed{};
    const BOOL clip_known = GetClipCursor(&allowed);
    if (!clip_known) { UnregisterClassW(class_name, module); return false; }
    // A hidden owned window: no focus request, native hide or pointer placement.
    const HWND owner = CreateWindowExW(0, class_name, L"Cursor validation", WS_POPUP,
        allowed.left, allowed.top, 96, 72, nullptr, nullptr, module, nullptr);
    if (owner == nullptr) { UnregisterClassW(class_name, module); return false; }
    POINT target{123, 456};
    const CursorStatus authority = host::windows_detail::cursor_authority(owner, true);
    const CursorStatus negative = host::windows_detail::cursor_screen_target(owner, -1, 0, target);
    const bool unchanged = target.x == 123 && target.y == 456;
    const CursorStatus edge = host::windows_detail::cursor_screen_target(owner, 96, 0, target);
    const CursorStatus origin = host::windows_detail::cursor_screen_target(owner, 0, 0, target);
    POINT expected{};
    const BOOL converted = ClientToScreen(owner, &expected);
    const bool correct = origin.accepted() && converted && target.x == expected.x && target.y == expected.y;
    const BOOL destroyed = DestroyWindow(owner);
    const CursorStatus stale = host::windows_detail::cursor_authority(owner, false);
    const BOOL unregistered = UnregisterClassW(class_name, module);
    const bool passed = authority.error == CursorError::denied && negative.error == CursorError::invalid_coordinate &&
        edge.error == CursorError::invalid_coordinate && unchanged && correct && destroyed && unregistered &&
        stale.error == CursorError::stale_window;
    if (!passed) { std::cerr << "Owned-window cursor authority/coordinate validation failed\n"; }
    return passed;
}

static bool cursor_cycle(const gui_forms::CursorImages& images, const gui_forms::CursorImage& raster) {
    HCURSOR cursor = gui_forms::host::windows_detail::create_image_cursor(images, raster);
    if (cursor == nullptr) { std::cerr << "Create cursor failed: " << GetLastError() << '\n'; return false; }
    ICONINFO info{};
    if (!GetIconInfo(cursor, &info)) {
        std::cerr << "GetIconInfo failed: " << GetLastError() << '\n';
        DestroyCursor(cursor);
        return false;
    }
    BITMAP bitmap{};
    const bool got_bitmap = GetObject(info.hbmColor, sizeof(bitmap), &bitmap) != 0;
    const bool valid = got_bitmap && !info.fIcon && info.xHotspot == 10 && info.yHotspot == 30 &&
        bitmap.bmWidth == 40 && bitmap.bmHeight == 40;
    if (!valid) std::cerr << "Unexpected cursor: " << info.fIcon << ' ' << info.xHotspot << ' '
        << info.yHotspot << ' ' << bitmap.bmWidth << 'x' << bitmap.bmHeight << '\n';
    const bool color_deleted = DeleteObject(info.hbmColor) != 0;
    const bool mask_deleted = DeleteObject(info.hbmMask) != 0;
    const bool destroyed = DestroyCursor(cursor) != 0;
    if (!color_deleted || !mask_deleted || !destroyed) std::cerr << "Cursor resource deletion failed\n";
    return valid && color_deleted && mask_deleted && destroyed;
}

int main() {
    const bool scope_validated = owned_window_cursor_validation();
    if (!scope_validated) { return 4; }
    const gui_forms::CursorImagesPtr images = gui_forms::CursorImages::create(
        {{32, 32, 1, std::vector<gui_forms::Color>(1024, {200, 100, 50, 128})}}, .25, .75);
    const gui_forms::CursorImage raster = (*images).rasterize(1.25);
    const DWORD initial = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    // Let Win32 initialize its process GDI state before measuring repeated ownership.
    if (!cursor_cycle(*images, raster)) return 1;
    GdiFlush();
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user_before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 200; ++i) {
        if (!cursor_cycle(*images, raster)) return 2;
    }
    GdiFlush();
    const DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user_after = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    std::cout << "GDI initial / initialized / after 200 cycles: " << initial << " / " << before << " / " << after
        << "; USER before / after: " << user_before << " / " << user_after << '\n';
    if (after > before || user_after > user_before) return 3;
    std::cout << "Win32 cursor dimensions, fractional DPI, hotspot and 200 resource lifecycles passed\n";
}
