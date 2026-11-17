#pragma once
#include "gui_forms/host.hpp"
#include <windows.h>
namespace gui_forms::host::detail {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).
[[nodiscard]] HostClipboardImageResult read_windows_clipboard_image(HWND owner);
[[nodiscard]] HostServiceStatus write_windows_clipboard_image(HWND owner, HostImageView image);
#endif
}
