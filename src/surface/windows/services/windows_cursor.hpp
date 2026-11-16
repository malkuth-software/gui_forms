#pragma once
#include "gui_forms/types/cursor_image/cursor_image.hpp"
#include <windows.h>
#include "gui_forms/host/cursor_interaction/cursor_interaction.hpp"
#include <limits>
namespace gui_forms::host::windows_detail {

// Borrowed HWND must remain live on the calling UI thread for each operation.
// These helpers inspect authority/geometry only; they do not hide or move input.
inline CursorStatus cursor_authority(HWND owner, bool require_pointer) noexcept {
    const BOOL live = owner == nullptr ? FALSE : IsWindow(owner);
    if (!live) { return {CursorError::stale_window}; }
    const BOOL visible = IsWindowVisible(owner);
    const BOOL minimized = IsIconic(owner);
    const HWND foreground = GetForegroundWindow();
    const HWND active = GetActiveWindow();
    const HWND focused = GetFocus();
    if (!visible || minimized || foreground != owner || active != owner || focused != owner) { return {CursorError::denied}; }
    RECT bounds{};
    const BOOL measured = GetClientRect(owner, &bounds);
    if (!measured) { return {CursorError::native_failure}; }
    if (bounds.right <= 0 || bounds.bottom <= 0) { return {CursorError::denied}; }
    if (require_pointer) {
        POINT screen{};
        const BOOL located = GetCursorPos(&screen);
        if (!located) { return {CursorError::native_failure}; }
        const HWND pointed = WindowFromPoint(screen);
        if (pointed != owner) { return {CursorError::denied}; }
        POINT client = screen;
        const BOOL converted = ScreenToClient(owner, &client);
        if (!converted) { return {CursorError::native_failure}; }
        if (client.x < 0 || client.y < 0 || client.x >= bounds.right || client.y >= bounds.bottom) { return {CursorError::denied}; }
    }
    return {};
}
inline CursorStatus cursor_screen_target(HWND owner, int client_x, int client_y, POINT& target) noexcept {
    const BOOL live = owner == nullptr ? FALSE : IsWindow(owner);
    if (!live) { return {CursorError::stale_window}; }
    RECT bounds{};
    const BOOL measured = GetClientRect(owner, &bounds);
    if (!measured) { return {CursorError::native_failure}; }
    if (client_x < 0 || client_y < 0 || client_x >= bounds.right || client_y >= bounds.bottom) {
        return {CursorError::invalid_coordinate};
    }
    POINT origin{};
    const BOOL converted = ClientToScreen(owner, &origin);
    if (!converted) { return {CursorError::native_failure}; }
    const std::int64_t x = std::int64_t(origin.x) + client_x;
    const std::int64_t y = std::int64_t(origin.y) + client_y;
    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
        y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max()) { return {CursorError::invalid_coordinate}; }
    RECT allowed{};
    const BOOL queried = GetClipCursor(&allowed);
    if (!queried) { return {CursorError::native_failure}; }
    if (x < allowed.left || x >= allowed.right || y < allowed.top || y >= allowed.bottom) { return {CursorError::denied}; }
    target = {static_cast<LONG>(x), static_cast<LONG>(y)};
    return {};
}
// Native resources are created only from the validated, immutable image set.
inline HCURSOR create_image_cursor(const CursorImages& images, const CursorImage& image) {
        const std::vector<unsigned char> zeros(static_cast<std::size_t>((image.width + 15) / 16 * 2) * image.height, 0);
        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(header);
        header.bV5Width = image.width; header.bV5Height = -image.height;
        header.bV5Planes = 1; header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00ff0000; header.bV5GreenMask = 0x0000ff00;
        header.bV5BlueMask = 0x000000ff; header.bV5AlphaMask = 0xff000000;
        void* bits = nullptr;
        HDC dc = GetDC(nullptr);
        HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header),
                                         DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, dc);
        if (color == nullptr || bits == nullptr) {
            if (color != nullptr) DeleteObject(color);
            return nullptr;
        }
        std::uint32_t* pixels = static_cast<std::uint32_t*>(bits);
        for (std::size_t i = 0; i < image.rgba.size(); ++i) {
            const Color c = image.rgba[i];
            pixels[i] = (std::uint32_t(c.alpha) << 24) |
                (((std::uint32_t(c.red) * c.alpha + 127) / 255) << 16) |
                (((std::uint32_t(c.green) * c.alpha + 127) / 255) << 8) |
                 ((std::uint32_t(c.blue) * c.alpha + 127) / 255);
        }
        HBITMAP mask = CreateBitmap(image.width, image.height, 1, 1, zeros.data());
        ICONINFO info{};
        info.fIcon = FALSE; info.xHotspot = images.hotspot_x(image);
        info.yHotspot = images.hotspot_y(image); info.hbmColor = color; info.hbmMask = mask;
        HCURSOR cursor = mask == nullptr ? nullptr : static_cast<HCURSOR>(CreateIconIndirect(&info));
        DeleteObject(color);
        if (mask != nullptr) DeleteObject(mask);
        if (cursor == nullptr) return nullptr;
        return cursor;
}
} // namespace gui_forms::host::windows_detail
