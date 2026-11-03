#pragma once

// Whether the calling thread's desktop is the input desktop: the one the
// user sees and types into. Foreground tests may run on a private desktop of
// the same window station (projects tools/run_on_desktop.py), where Windows
// refuses what only the input desktop has: the cursor position
// (GetCursorPos fails with ERROR_ACCESS_DENIED), the foreground window, and
// DWM composition of the screen. A test that checks such a service accepts
// its documented refusal only off the input desktop. Windows only.

#include <windows.h>

namespace gui_forms::test_support {

// The name of desktop `desktop`, or empty when it cannot be read. `name`
// holds `capacity` wide characters.
inline bool desktop_name(HDESK desktop, wchar_t* name, DWORD capacity) {
    DWORD needed = 0U;
    const DWORD bytes = capacity * static_cast<DWORD>(sizeof(wchar_t));
    const BOOL read = GetUserObjectInformationW(desktop, UOI_NAME, name, bytes, &needed);
    if (!read) name[0] = L'\0';
    return read != FALSE;
}

// True when this thread's desktop is the input desktop. When the input
// desktop cannot be opened (a locked workstation, or no access), the thread
// is taken to be off it.
inline bool on_input_desktop() {
    HDESK input = OpenInputDesktop(0U, FALSE, DESKTOP_READOBJECTS);
    if (input == nullptr) return false;
    wchar_t input_name[256]{};
    wchar_t own_name[256]{};
    const bool input_read = desktop_name(input, input_name, 256U);
    CloseDesktop(input);
    const HDESK own = GetThreadDesktop(GetCurrentThreadId());
    const bool own_read = own != nullptr && desktop_name(own, own_name, 256U);
    const bool same = input_read && own_read && lstrcmpiW(input_name, own_name) == 0;
    return same;
}

} // namespace gui_forms::test_support
