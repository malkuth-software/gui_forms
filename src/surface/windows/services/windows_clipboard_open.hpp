#pragma once
#include <windows.h>

namespace gui_forms::host::detail {

// The clipboard is one per desktop and only one window may hold it open.
// Another process (clipboard history, a clipboard manager, an application
// reading a change notification) often opens it for a moment, most of all
// just after it changed, and OpenClipboard then fails. The adapter retries
// for at most clipboard_open_attempts * clipboard_open_retry_ms (200 ms)
// before reporting a backend failure.
inline constexpr int clipboard_open_attempts = 20;
inline constexpr DWORD clipboard_open_retry_ms = 10;

[[nodiscard]] inline bool open_clipboard_with_retry(HWND owner) noexcept {
    for (int attempt = 0; attempt < clipboard_open_attempts; ++attempt) {
        if (OpenClipboard(owner) != FALSE) return true;
        Sleep(clipboard_open_retry_ms);
    }
    return false;
}

} // namespace gui_forms::host::detail
