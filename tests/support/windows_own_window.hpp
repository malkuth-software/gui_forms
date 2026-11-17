#pragma once

// The native window a test's own UI thread created, found without a
// desktop-wide lookup. FindWindowW searches every process on the desktop, so
// when two copies of a test run at once (other workspaces run the same
// tests, sometimes under a sanitizer) one process can take the other's
// window. No public GUI.Forms seam hands out the HWND (Win32 types stay
// inside src/surface/windows/), so the test asks for the top-level windows of
// the calling thread only: call these from the window's UI thread (a
// host_ready or window-ready callback). Windows only.

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace gui_forms::test_support {

// The class every GUI.Forms Win32 window registers.
inline constexpr const wchar_t* gui_forms_window_class = L"GUIForms.Window.v1";

// What one window enumeration looks for, and what it found.
struct WindowSearch final {
    const wchar_t* title{};
    HWND found{};
    std::size_t matches{};
};

// Whether `hwnd` is a GUI.Forms window titled `title`.
inline bool gui_forms_window_titled(HWND hwnd, const wchar_t* title) {
    wchar_t class_name[64]{};
    const int class_length = GetClassNameW(hwnd, class_name, 64);
    if (class_length <= 0 || lstrcmpW(class_name, gui_forms_window_class) != 0) return false;
    wchar_t text[512]{};
    const int text_length = GetWindowTextW(hwnd, text, 512);
    if (text_length < 0) return false;
    const bool titled = lstrcmpW(text, title) == 0;
    return titled;
}

// The enumeration callback: counts matching windows and keeps the first.
inline BOOL CALLBACK collect_titled_window(HWND hwnd, LPARAM parameter) {
    WindowSearch& search = *reinterpret_cast<WindowSearch*>(parameter);
    if (!gui_forms_window_titled(hwnd, search.title)) return TRUE;
    ++search.matches;
    if (search.found == nullptr) search.found = hwnd;
    return TRUE;
}
static_assert(std::is_same_v<decltype(&collect_titled_window), WNDENUMPROC>,
              "the callback has the type EnumThreadWindows and EnumWindows declare");

// The calling thread's GUI.Forms window titled `title`, or null. The window
// found belongs to this process by construction; the process is checked
// too, so a caller can rely on it.
inline HWND own_gui_forms_window(const wchar_t* title) {
    WindowSearch search{title, nullptr, 0U};
    EnumThreadWindows(GetCurrentThreadId(), collect_titled_window,
                      reinterpret_cast<LPARAM>(&search));
    if (search.found == nullptr) return nullptr;
    DWORD process = 0;
    GetWindowThreadProcessId(search.found, &process);
    if (process != GetCurrentProcessId()) return nullptr;
    return search.found;
}

// How many top-level GUI.Forms windows titled `title` the whole desktop
// holds, in every process: what a desktop-wide lookup would choose among.
inline std::size_t desktop_gui_forms_windows(const wchar_t* title) {
    WindowSearch search{title, nullptr, 0U};
    EnumWindows(collect_titled_window, reinterpret_cast<LPARAM>(&search));
    return search.matches;
}

// `base` followed by this process's ID: a window title no other copy of the
// test shares, so even a copy built before the thread-scoped lookup (which
// searched the whole desktop by title) cannot take this window. `base` is
// ASCII.
inline std::string process_title(std::string_view base) {
    const std::string title =
        std::string(base) + " - process " + std::to_string(GetCurrentProcessId());
    return title;
}

// process_title as UTF-16, for the lookups above.
inline std::wstring process_title_wide(std::string_view base) {
    const std::string narrow = process_title(base);
    const std::wstring wide(narrow.begin(), narrow.end());
    return wide;
}

} // namespace gui_forms::test_support
