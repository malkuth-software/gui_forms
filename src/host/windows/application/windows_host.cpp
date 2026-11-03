#include "gui_forms/paint_framebuffer.hpp"
#include "windows_host.hpp"
#include "../accessibility/windows_accessibility.hpp"
#include "../services/windows_clipboard_image.hpp"
#include "../services/windows_clipboard_open.hpp"
#include "../services/windows_cursor.hpp"
#include "../services/windows_notify_icons.hpp"
#include "../compositor/windows_compositor.hpp"
#include "../input/windows_key_translation.hpp"
#include "../input/windows_message_queue.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../fonts/sealed_pages_windows.hpp"
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
#include "../raster/dib_frame_store.hpp"
#endif
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT_TEST)
#include "../../../core/text/prepared/prepared_storage.hpp"
#include "../../../../tests/prepared_text_test_support.hpp"
#endif
// The window's painter (stage SK3): the private Skia CPU raster.
#include "skia_raster.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/detail/bound_member_function.hpp"

#include "../paint_endpoint/windows_compatibility_paint_endpoint_metrics.hpp"
#include "../../../core/host/frame_capture/frame_capture.hpp"
#include "../../../core/image/png_writer.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gui_forms::host {
namespace {

constexpr wchar_t window_class_name[] = L"GUIForms.Window.v1";
constexpr wchar_t tooltip_class_name[] = L"GUIForms.ToolTip.v1";
constexpr UINT_PTR scheduler_timer = 1;
constexpr UINT_PTR modal_live_surface_timer = 2;
constexpr UINT live_surface_period_milliseconds = 16;
// The live frame clock's cadence when the display's composition timing is
// unavailable: 60 Hz, in milliseconds per thousand frames.
constexpr LONGLONG live_clock_fallback_millihertz = 60'000;
#if !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// DwmGetCompositionTimingInfo, loaded at run time like the compositor's DWM
// functions, so that the host starts where dwmapi is absent; the module stays
// loaded for the life of the process.
using DwmCompositionTimingFunction = HRESULT(WINAPI*)(HWND, DWM_TIMING_INFO*);
constexpr UINT managed_dispatch_message = WM_APP + 0x41U;
constexpr UINT render_dispatch_message = WM_APP + 0x42U;
constexpr std::size_t maximum_automation_command = 4096;

bool environment_flag(const char* name) noexcept {
    const char* value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
}

bool trace_win32_input() noexcept {
    static const bool enabled =
        environment_flag("GUI_FORMS_TRACE_WIN32_INPUT");
    return enabled;
}

// An input trace line prints at most this many bytes of a stable ID, so the
// `%.*s` precision always fits in int (a stable ID has no length limit of its
// own). A longer ID is printed cut to this length, and the line then carries
// `target_cut_from:<full length in bytes>` so the cut is never silent.
constexpr std::size_t maximum_traced_stable_id_bytes = 1024;

struct TracedStableId {
    // The `%.*s` precision: the number of bytes printed, at most
    // maximum_traced_stable_id_bytes.
    int printed_bytes = 0;
    // The full length when the ID was cut, otherwise 0.
    std::size_t cut_from_bytes = 0;
};

TracedStableId traced_stable_id(std::string_view id) noexcept {
    TracedStableId traced;
    std::size_t printed = id.size();
    if (printed > maximum_traced_stable_id_bytes) {
        traced.cut_from_bytes = printed;
        printed = maximum_traced_stable_id_bytes;
    }
    traced.printed_bytes = static_cast<int>(printed);
    return traced;
}

std::uint64_t now_nanoseconds() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::wstring wide_from_utf8(std::string_view text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), count) != count) {
        return {};
    }
    return result;
}

std::string utf8_from_wide(std::wstring_view text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0,
                                          nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), count,
                            nullptr, nullptr) != count) {
        return {};
    }
    return result;
}

std::wstring native_filter(const std::vector<HostFileDialogFilter>& filters) {
    std::wstring result;
    for (const gui_forms::HostFileDialogFilter& filter : filters) {
        std::wstring label = wide_from_utf8(filter.label);
        if (label.empty()) label = L"Files";
        std::wstring pattern;
        for (const std::basic_string<char>& extension_utf8 : filter.extensions) {
            std::wstring extension = wide_from_utf8(extension_utf8);
            while (!extension.empty() && (extension.front() == L'.' ||
                                           extension.front() == L'*')) {
                extension.erase(extension.begin());
            }
            if (extension.empty()) continue;
            if (!pattern.empty()) pattern.push_back(L';');
            pattern.append(L"*.").append(extension);
        }
        if (pattern.empty()) pattern = L"*.*";
        result.append(label).push_back(L'\0');
        result.append(pattern).push_back(L'\0');
    }
    if (result.empty()) {
        result.append(L"All files").push_back(L'\0');
        result.append(L"*.*").push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}

// The native filter index (counting from one) the dialog starts on, and the
// request's filter (counting from zero) it ended on. Without filters the
// dialog shows its own "All files", which is no filter of the request.
DWORD native_filter_index(const std::vector<HostFileDialogFilter>& filters,
                          std::size_t initial_filter) {
    if (filters.empty() || initial_filter >= filters.size()) return 1U;
    const DWORD index = static_cast<DWORD>(initial_filter + 1U);
    return index;
}

std::optional<std::size_t> chosen_filter(
    const std::vector<HostFileDialogFilter>& filters, DWORD native_index) {
    if (native_index == 0U || native_index > filters.size()) return std::nullopt;
    const std::size_t chosen = static_cast<std::size_t>(native_index) - 1U;
    return chosen;
}

HostDialogResult path_dialog_failure(std::uint64_t request_id,
                                     HostServiceError error) {
    return {{error}, request_id, HostPathDialogResult{}};
}

HostDialogResult native_open_dialog(HWND owner, std::uint64_t request_id,
                                    const HostOpenFileDialogRequest& request) {
    std::vector<wchar_t> path(65536, L'\0');
    const std::wstring title = wide_from_utf8(request.title);
    const std::wstring directory = wide_from_utf8(request.initial_directory);
    const std::wstring suggested = wide_from_utf8(request.suggested_name);
    const std::wstring filter = native_filter(request.filters);
    if (!suggested.empty()) {
        const std::size_t count = std::min(suggested.size(), path.size() - 2U);
        std::copy_n(suggested.data(), count, path.data());
    }
    OPENFILENAMEW native{};
    native.lStructSize = sizeof(native);
    native.hwndOwner = owner;
    native.lpstrFilter = filter.c_str();
    native.nFilterIndex = native_filter_index(request.filters, request.initial_filter);
    native.lpstrFile = path.data();
    native.nMaxFile = static_cast<DWORD>(path.size());
    native.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
    native.lpstrTitle = title.empty() ? nullptr : title.c_str();
    native.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST |
                   OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (request.allow_multiple) native.Flags |= OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&native)) {
        const DWORD error = CommDlgExtendedError();
        return error == 0 ? HostDialogResult{{}, request_id,
                    HostPathDialogResult{HostDialogOutcome::cancelled, {}}}
            : path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    HostPathDialogResult value;
    value.outcome = HostDialogOutcome::accepted;
    value.chosen_filter = chosen_filter(request.filters, native.nFilterIndex);
    const std::wstring first(path.data());
    const wchar_t* cursor = path.data() + first.size() + 1U;
    if (*cursor == L'\0') {
        value.paths.push_back(utf8_from_wide(first));
    } else {
        for (std::size_t count = 0; *cursor != L'\0' &&
             count < HostServices::maximum_dialog_paths; ++count) {
            const std::wstring name(cursor);
            std::wstring combined = first;
            if (!combined.empty() && combined.back() != L'\\' &&
                combined.back() != L'/') combined.push_back(L'\\');
            combined.append(name);
            value.paths.push_back(utf8_from_wide(combined));
            cursor += name.size() + 1U;
        }
    }
    return {{}, request_id, std::move(value)};
}

HostDialogResult native_save_dialog(HWND owner, std::uint64_t request_id,
                                    const HostSaveFileDialogRequest& request) {
    std::vector<wchar_t> path(32768, L'\0');
    const std::wstring title = wide_from_utf8(request.title);
    const std::wstring directory = wide_from_utf8(request.initial_directory);
    const std::wstring suggested = wide_from_utf8(request.suggested_name);
    const std::wstring extension = wide_from_utf8(request.default_extension);
    const std::wstring filter = native_filter(request.filters);
    if (!suggested.empty()) {
        const std::size_t count = std::min(suggested.size(), path.size() - 2U);
        std::copy_n(suggested.data(), count, path.data());
    }
    OPENFILENAMEW native{};
    native.lStructSize = sizeof(native);
    native.hwndOwner = owner;
    native.lpstrFilter = filter.c_str();
    native.nFilterIndex = native_filter_index(request.filters, request.initial_filter);
    native.lpstrFile = path.data();
    native.nMaxFile = static_cast<DWORD>(path.size());
    native.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
    native.lpstrTitle = title.empty() ? nullptr : title.c_str();
    native.lpstrDefExt = extension.empty() ? nullptr : extension.c_str();
    native.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                   OFN_NOCHANGEDIR;
    if (request.confirm_overwrite) native.Flags |= OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&native)) {
        const DWORD error = CommDlgExtendedError();
        return error == 0 ? HostDialogResult{{}, request_id,
                    HostPathDialogResult{HostDialogOutcome::cancelled, {}}}
            : path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    HostPathDialogResult value;
    value.outcome = HostDialogOutcome::accepted;
    value.chosen_filter = chosen_filter(request.filters, native.nFilterIndex);
    value.paths.push_back(utf8_from_wide(path.data()));
    return {{}, request_id, std::move(value)};
}

int CALLBACK select_initial_folder(HWND dialog, UINT message, LPARAM,
                                   LPARAM context) {
    if (message == BFFM_INITIALIZED && context != 0) {
        SendMessageW(dialog, BFFM_SETSELECTIONW, TRUE, context);
    }
    return 0;
}

HostDialogResult native_folder_dialog(HWND owner, std::uint64_t request_id,
                                      const HostFolderDialogRequest& request) {
    const std::wstring title = wide_from_utf8(request.title);
    const std::wstring directory = wide_from_utf8(request.initial_directory);
    BROWSEINFOW native{};
    native.hwndOwner = owner;
    native.lpszTitle = title.empty() ? nullptr : title.c_str();
    native.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE |
                     BIF_EDITBOX | BIF_VALIDATE;
    native.lpfn = select_initial_folder;
    native.lParam = directory.empty() ? 0 :
        reinterpret_cast<LPARAM>(directory.c_str());
    PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&native);
    if (selected == nullptr) {
        return {{}, request_id,
                HostPathDialogResult{HostDialogOutcome::cancelled, {}}};
    }
    std::vector<wchar_t> path(32768, L'\0');
    const bool converted = SHGetPathFromIDListW(selected, path.data()) != FALSE;
    CoTaskMemFree(selected);
    if (!converted) {
        return path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    HostPathDialogResult value;
    value.outcome = HostDialogOutcome::accepted;
    value.paths.push_back(utf8_from_wide(path.data()));
    return {{}, request_id, std::move(value)};
}

// The MessageBox style flags for a button set.
UINT message_button_style(HostMessageButtons buttons) noexcept {
    switch (buttons) {
    case HostMessageButtons::ok: return MB_OK;
    case HostMessageButtons::ok_cancel: return MB_OKCANCEL;
    case HostMessageButtons::yes_no: return MB_YESNO;
    case HostMessageButtons::yes_no_cancel: return MB_YESNOCANCEL;
    case HostMessageButtons::retry_cancel: return MB_RETRYCANCEL;
    }
    // A button set outside the enumeration shows OK alone.
    return MB_OK;
}

// The MessageBox style flags for an icon; none for no icon.
UINT message_icon_style(HostMessageIcon icon) noexcept {
    switch (icon) {
    case HostMessageIcon::information: return MB_ICONINFORMATION;
    case HostMessageIcon::warning: return MB_ICONWARNING;
    case HostMessageIcon::error: return MB_ICONERROR;
    case HostMessageIcon::question: return MB_ICONQUESTION;
    case HostMessageIcon::none: return 0U;
    }
    // An icon outside the enumeration shows none.
    return 0U;
}

UINT native_message_style(const HostMessageDialogRequest& request) noexcept {
    UINT style = MB_APPLMODAL;
    style |= message_button_style(request.buttons);
    style |= message_icon_style(request.icon);
    const bool second =
        (request.buttons == HostMessageButtons::ok_cancel &&
         request.default_choice == HostDialogChoice::cancel) ||
        (request.buttons == HostMessageButtons::yes_no &&
         request.default_choice == HostDialogChoice::no) ||
        (request.buttons == HostMessageButtons::yes_no_cancel &&
         request.default_choice == HostDialogChoice::no) ||
        (request.buttons == HostMessageButtons::retry_cancel &&
         request.default_choice == HostDialogChoice::cancel);
    const bool third = request.buttons == HostMessageButtons::yes_no_cancel &&
                       request.default_choice == HostDialogChoice::cancel;
    if (third) style |= MB_DEFBUTTON3;
    else if (second) style |= MB_DEFBUTTON2;
    return style;
}

HostDialogResult native_message_dialog(HWND owner, std::uint64_t request_id,
                                       const HostMessageDialogRequest& request) {
    const std::wstring title = wide_from_utf8(request.title);
    const std::wstring message = wide_from_utf8(request.message);
    const int native_result = MessageBoxW(owner, message.c_str(), title.c_str(),
                                          native_message_style(request));
    HostMessageDialogResult value;
    switch (native_result) {
    case IDOK: value.choice = HostDialogChoice::ok; break;
    case IDCANCEL:
        value.choice = HostDialogChoice::cancel;
        value.outcome = HostDialogOutcome::cancelled;
        break;
    case IDYES: value.choice = HostDialogChoice::yes; break;
    case IDNO: value.choice = HostDialogChoice::no; break;
    case IDRETRY: value.choice = HostDialogChoice::retry; break;
    default:
        return {{HostServiceError::backend_failure}, request_id,
                HostMessageDialogResult{}};
    }
    if (native_result != IDCANCEL) value.outcome = HostDialogOutcome::accepted;
    return {{}, request_id, value};
}

HostDialogResult native_color_dialog(HWND owner, std::uint64_t request_id,
                                     const HostColorDialogRequest& request,
                                     std::array<COLORREF, 16>& custom_colors) {
    const std::uint8_t red = static_cast<std::uint8_t>(request.initial_rgba >> 24U);
    const std::uint8_t green = static_cast<std::uint8_t>(request.initial_rgba >> 16U);
    const std::uint8_t blue = static_cast<std::uint8_t>(request.initial_rgba >> 8U);
    CHOOSECOLORW native{};
    native.lStructSize = sizeof(native);
    native.hwndOwner = owner;
    native.rgbResult = RGB(red, green, blue);
    native.lpCustColors = custom_colors.data();
    native.Flags = CC_ANYCOLOR | CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&native)) {
        const DWORD error = CommDlgExtendedError();
        return error == 0
            ? HostDialogResult{{}, request_id,
                  HostColorDialogResult{HostDialogOutcome::cancelled, 0U}}
            : HostDialogResult{{HostServiceError::backend_failure}, request_id,
                  HostColorDialogResult{}};
    }
    const std::uint32_t alpha = request.allow_alpha
        ? request.initial_rgba & 0xFFU : 0xFFU;
    const std::uint32_t rgba =
        (static_cast<std::uint32_t>(GetRValue(native.rgbResult)) << 24U) |
        (static_cast<std::uint32_t>(GetGValue(native.rgbResult)) << 16U) |
        (static_cast<std::uint32_t>(GetBValue(native.rgbResult)) << 8U) | alpha;
    return {{}, request_id,
            HostColorDialogResult{HostDialogOutcome::accepted, rgba}};
}

LPCWSTR native_cursor_identifier(CursorKind cursor) noexcept {
    switch (cursor) {
    case CursorKind::text: return IDC_IBEAM;
    case CursorKind::hand: return IDC_HAND;
    case CursorKind::crosshair: return IDC_CROSS;
    case CursorKind::resize_horizontal: return IDC_SIZEWE;
    case CursorKind::resize_vertical: return IDC_SIZENS;
    case CursorKind::wait: return IDC_WAIT;
    case CursorKind::forbidden: return IDC_NO;
    case CursorKind::resize_diagonal_down: return IDC_SIZENWSE;
    case CursorKind::resize_diagonal_up: return IDC_SIZENESW;
    case CursorKind::arrow: return IDC_ARROW;
    }
    return IDC_ARROW;
}

HCURSOR load_system_cursor(LPCWSTR identifier) noexcept {
    HCURSOR cursor = LoadCursorW(nullptr, identifier);
    if (cursor == nullptr && identifier != IDC_ARROW) {
        cursor = LoadCursorW(nullptr, IDC_ARROW);
    }
    if (cursor == nullptr) {
        cursor = reinterpret_cast<HCURSOR>(LoadImageW(
            nullptr, IDC_ARROW, IMAGE_CURSOR, 0, 0,
            LR_DEFAULTSIZE | LR_SHARED));
    }
    return cursor;
}

bool apply_system_cursor(CursorKind kind) noexcept {
    const LPCWSTR identifier = native_cursor_identifier(kind);
    HCURSOR cursor = load_system_cursor(identifier);
    if (cursor == nullptr) return false;
    HCURSOR previous = SetCursor(cursor);
    HCURSOR installed = GetCursor();
    const bool applied = installed != nullptr;
    if (trace_win32_input()) {
        std::fprintf(stderr,
                     "win32-input=cursor|kind:%u|identifier:%p|loaded:%p|"
                     "previous:%p|installed:%p|applied:%d\n",
                     static_cast<unsigned>(kind),
                     static_cast<const void*>(identifier),
                     static_cast<void*>(cursor), static_cast<void*>(previous),
                     static_cast<void*>(installed), applied);
        std::fflush(stderr);
    }
    return applied;
}

// Shcore's entry point, typed from its declaration (house style section 2).
using GetDpiForMonitorFunction = decltype(&::GetDpiForMonitor);

GetDpiForMonitorFunction load_get_dpi_for_monitor() noexcept {
    HMODULE library = LoadLibraryW(L"Shcore.dll");
    if (library == nullptr) return nullptr;
    const FARPROC symbol = GetProcAddress(library, "GetDpiForMonitor");
    GetDpiForMonitorFunction function{};
    static_assert(sizeof(function) == sizeof(symbol));
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
}

double native_monitor_scale(HMONITOR monitor) noexcept {
    static const GetDpiForMonitorFunction get_dpi_for_monitor =
        load_get_dpi_for_monitor();
    UINT x = 96U;
    UINT y = 96U;
    if (get_dpi_for_monitor == nullptr ||
        FAILED(get_dpi_for_monitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)) || x == 0U) {
        return 1.0;
    }
    return static_cast<double>(x) / 96.0;
}

#include "../services/windows_host_services.hpp"

// A tooltip's content is painted by the window's text engine (ADR-004) into
// top-down BGRA pixels; the popup only presents them.
struct TooltipPopup final {
    HWND window{};
    std::wstring text;
    std::vector<std::uint32_t> pixels;
    int width{};
    int height{};
};

LRESULT CALLBACK tooltip_window_procedure(HWND window, UINT message,
                                          WPARAM wparam, LPARAM lparam) {
    TooltipPopup* state = reinterpret_cast<TooltipPopup*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create =
            reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<TooltipPopup*>((*create).lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        (*state).window = window;
    }
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: DestroyWindow(window); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (state && !(*state).pixels.empty()) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = (*state).width;
            info.bmiHeader.biHeight = -(*state).height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(dc, 0, 0, static_cast<DWORD>((*state).width), static_cast<DWORD>((*state).height),
                              0, 0, 0, static_cast<UINT>((*state).height), (*state).pixels.data(), &info,
                              DIB_RGB_COLORS);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        if (state) (*state).window = nullptr;
        return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

Modifier modifiers() noexcept {
    Modifier result = Modifier::none;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) result = result | Modifier::shift;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) result = result | Modifier::control;
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) result = result | Modifier::alt;
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0) result = result | Modifier::meta;
    return result;
}

PointerButton pointer_button(UINT message, WPARAM state) noexcept {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP: return PointerButton::primary;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP: return PointerButton::secondary;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: return PointerButton::middle;
    default:
        // WM_MOUSEMOVE identifies held buttons through wParam rather than the
        // message number. Preserve that state so WinForms-compatible custom
        // sliders can continue a drag by checking MouseEventArgs.Button.
        if ((state & MK_LBUTTON) != 0U) return PointerButton::primary;
        if ((state & MK_RBUTTON) != 0U) return PointerButton::secondary;
        if ((state & MK_MBUTTON) != 0U) return PointerButton::middle;
        return PointerButton::none;
    }
}

template <typename Function>
Function load_function(HMODULE module, const char* name) noexcept {
    static_assert(sizeof(Function) == sizeof(FARPROC));
    const FARPROC raw = GetProcAddress(module, name);
    Function function{};
    std::memcpy(&function, &raw, sizeof(function));
    return function;
}

double query_scale(HWND window) noexcept {
    using GetDpiForWindowFunction = decltype(&::GetDpiForWindow);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        const GetDpiForWindowFunction function =
            load_function<GetDpiForWindowFunction>(
            user32, "GetDpiForWindow");
        if (function != nullptr && window != nullptr) {
            const UINT dpi = function(window);
            if (dpi != 0U) return std::max(1.0, dpi / 96.0);
        }
    }
    HDC dc = GetDC(window);
    const int dpi = dc == nullptr ? 96 : GetDeviceCaps(dc, LOGPIXELSX);
    if (dc != nullptr) ReleaseDC(window, dc);
    return std::max(1.0, dpi / 96.0);
}

void adjust_client_frame(RECT& frame, DWORD style, DWORD extended_style,
                         double scale) noexcept {
    using AdjustForDpiFunction = decltype(&::AdjustWindowRectExForDpi);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        const AdjustForDpiFunction function = load_function<AdjustForDpiFunction>(
            user32, "AdjustWindowRectExForDpi");
        if (function != nullptr && function(&frame, style, FALSE, extended_style,
                static_cast<UINT>(std::lround(scale * 96.0)))) return;
    }
    AdjustWindowRectEx(&frame, style, FALSE, extended_style);
}

void enable_best_dpi_awareness() noexcept {
    using SetContextFunction = decltype(&::SetProcessDpiAwarenessContext);
    using SetAwareFunction = decltype(&::SetProcessDPIAware);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        const SetContextFunction set_context =
            load_function<SetContextFunction>(
            user32, "SetProcessDpiAwarenessContext");
        if (set_context != nullptr && set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
        const SetAwareFunction set_aware = load_function<SetAwareFunction>(
            user32, "SetProcessDPIAware");
        if (set_aware != nullptr) static_cast<void>(set_aware());
    }
}

// The bundled font directory: GUI_FORMS_FONT_DIR when it is set, as on Linux, else "fonts"
// beside the executable; empty when neither is known.
std::filesystem::path bundled_font_directory() {
    const wchar_t* override_directory = _wgetenv(L"GUI_FORMS_FONT_DIR");
    if (override_directory != nullptr) return std::filesystem::path(override_directory);
    wchar_t executable[MAX_PATH]{};
    const DWORD count = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (count == 0 || count >= MAX_PATH) return {};
    const std::filesystem::path directory =
        std::filesystem::path(std::wstring(executable, count)).parent_path() / L"fonts";
    return directory;
}

// The bundled font set in gui_forms::detail::bundled_face_uses() order, as every host
// registers it (ADR-004), shared with every other raster given the same set. Nullptr when the
// directory is unknown; a set missing a required face is refused when a raster registers it.
std::shared_ptr<gui_forms::detail::BundledFontSet> load_bundled_fonts(const std::filesystem::path& directory) {
    std::shared_ptr<gui_forms::detail::BundledFontSet> result;
    if (directory.empty()) return result;
    result = gui_forms::detail::shared_bundled_font_set(directory, windows_sealed_pages());
    return result;
}

// One rectangle of whole device pixels inside a raster, half open.
struct DeviceArea final {
    int left{};
    int top{};
    int right{};
    int bottom{};
};

// The whole device pixels a logical rectangle touches at `scale`, clamped to a
// raster of `width` by `height` pixels; the same rounding as the Skia raster's
// damage clip, so a copy covers every pixel a frame may have changed.
DeviceArea device_area(const Rect rect, const double scale, const int width, const int height) noexcept {
    const double left = std::floor(rect.x * scale);
    const double top = std::floor(rect.y * scale);
    const double right = std::ceil((rect.x + rect.width) * scale);
    const double bottom = std::ceil((rect.y + rect.height) * scale);
    DeviceArea area{};
    area.left = static_cast<int>(std::clamp(left, 0.0, static_cast<double>(width)));
    area.top = static_cast<int>(std::clamp(top, 0.0, static_cast<double>(height)));
    area.right = static_cast<int>(std::clamp(right, static_cast<double>(area.left), static_cast<double>(width)));
    area.bottom = static_cast<int>(std::clamp(bottom, static_cast<double>(area.top), static_cast<double>(height)));
    return area;
}

#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
// Copies `area` of a premultiplied BGRA8 raster into a 32-bit top-down DIB of
// the same extent, byte for byte (SK2: both are BGRA8, so nothing converts).
// With `opaque`, every copied pixel's alpha byte becomes 255, which is the
// premultiplied colour over black: its colour bytes need no change. The source
// rows are `source_row_bytes` apart; the DIB rows `target_width` pixels apart.
// Source and target never overlap: the raster and the DIB own separate memory.
void copy_raster_area(const std::byte* const source, const std::size_t source_row_bytes,
                      std::uint32_t* const target, const int target_width,
                      const DeviceArea area, const bool opaque) noexcept {
    if (area.right <= area.left || area.bottom <= area.top) return;
    const std::size_t columns = static_cast<std::size_t>(area.right - area.left);
    const std::size_t stride = static_cast<std::size_t>(target_width);
    const std::size_t first_column = static_cast<std::size_t>(area.left);
    if (!opaque) {
        for (int row = area.top; row < area.bottom; ++row) {
            const std::size_t row_index = static_cast<std::size_t>(row);
            const std::byte* const from = source + row_index * source_row_bytes + first_column * 4U;
            std::uint32_t* const to = target + row_index * stride + first_column;
            std::memcpy(to, from, columns * 4U);
        }
        return;
    }
    constexpr std::uint32_t opaque_alpha = 0xff000000U;
    for (int row = area.top; row < area.bottom; ++row) {
        const std::size_t row_index = static_cast<std::size_t>(row);
        const std::byte* const from = source + row_index * source_row_bytes + first_column * 4U;
        std::uint32_t* const to = target + row_index * stride + first_column;
        std::memcpy(to, from, columns * 4U);
        for (std::size_t column = 0; column < columns; ++column) to[column] |= opaque_alpha;
    }
}
#endif

// The window's raster (stage SK3). The Skia CPU painter draws every frame
// into its premultiplied BGRA8 surface, which is a top-down 32-bit DIB as it
// stands: nothing converts on the way to the window. Under
// GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB the frame's damaged device pixels are
// copied into the all-or-nothing DIB frame store: a frame reaches the screen
// only by committing a complete candidate, and a failed frame leaves the
// presented front and its key unchanged. Otherwise the presents copy the
// painter's pixels to the window directly.
//
// With prepared text (GUI_FORMS_WINDOWS_PREPARED_TEXT, transactional only)
// the painter runs its own prepared-frame transaction: its front is published
// into the DIB only after the painter commits it against the frame's receipt.
class WindowsRaster final {
#if defined(GUI_FORMS_DIB_LIFECYCLE_TEST)
    friend struct DibHostFixture;
#endif
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT_TEST)
    friend struct PreparedHostFixture;
#endif
public:
    WindowsRaster() = default;
    WindowsRaster(const WindowsRaster&) = delete;
    WindowsRaster& operator=(const WindowsRaster&) = delete;

    // The painter frames draw with, and the window's text metrics provider.
    // Frames begin and end through this raster, never on the painter itself.
    [[nodiscard]] render::SkiaRaster& painter() noexcept { return canvas_; }

    // Sizes the raster for `logical_size` at `scale`. True when frames of that
    // size can begin. A zero extent is accepted and begins no frame
    // (transactional); a failed resize keeps the presented front.
    bool resize(Size logical_size, double scale) {
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        abort_frame();
        size_valid_ = false;
        if (!std::isfinite(scale) || scale <= 0.0 ||
            !std::isfinite(logical_size.width) || !std::isfinite(logical_size.height) ||
            logical_size.width < 0.0 || logical_size.height < 0.0) return false;
        const double columns = std::ceil(logical_size.width * scale);
        const double rows = std::ceil(logical_size.height * scale);
        const double limit = static_cast<double>(detail::DibFrameStore::pixel_limit);
        if (!std::isfinite(columns) || !std::isfinite(rows) ||
            columns > limit || rows > limit ||
            (rows > 0.0 && columns > limit / rows)) return false;
        logical_size_ = logical_size;
        scale_ = scale;
        width_ = static_cast<int>(columns);
        height_ = static_cast<int>(rows);
        size_valid_ = true;
#if !defined(GUI_FORMS_WINDOWS_PREPARED_TEXT)
        // The painter's surface is replaced, clear, when its extent changes;
        // the next frame then copies all of it.
        if (width_ > 0 && height_ > 0 && canvas_.resize(logical_size, scale)) dib_synchronized_ = false;
#endif
        return true;
#else
        const int width = std::max(1, static_cast<int>(std::ceil(logical_size.width * scale)));
        const int height = std::max(1, static_cast<int>(std::ceil(logical_size.height * scale)));
        logical_size_ = logical_size;
        scale_ = scale;
        static_cast<void>(canvas_.resize(logical_size, scale));
        const bool painter_sized = canvas_.pixel_width() == static_cast<std::uint32_t>(width) &&
                                   canvas_.pixel_height() == static_cast<std::uint32_t>(height);
        width_ = painter_sized ? width : 0;
        height_ = painter_sized ? height : 0;
        return painter_sized;
#endif
    }

    // Begins a frame clipped to `damage` (the whole raster when empty). Only
    // the painter's pixels change until the frame commits or presents.
    bool begin_frame(const DamageRegion& damage) {
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        abort_frame();
        if (!size_valid_ || width_ <= 0 || height_ <= 0) return false;
#endif
        frame_damage_ = damage;
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT)
        // Admission may widen the damage to the whole raster (a new extent or
        // a revoked front); the copy into the DIB follows the widened damage.
        const PreparedTextStatus admitted = canvas_.begin_prepared_frame(logical_size_, scale_, frame_damage_);
        if (admitted != PreparedTextStatus::success) return false;
#else
        if (canvas_.pixels() == nullptr) return false;
        canvas_.begin_frame(frame_damage_);
#endif
        frame_open_ = true;
        return true;
    }
    bool begin_frame() {
        const DamageRegion whole{};
        const bool begun = begin_frame(whole);
        return begun;
    }

#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    // Drops the open frame. The presented front and its key stay; the
    // painter's pixels may differ from them now, so the next frame copies the
    // whole raster.
    void abort_frame() noexcept {
        frames_.abort();
        if (!frame_open_) return;
        frame_open_ = false;
        dib_synchronized_ = false;
        canvas_.end_frame();
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT)
        canvas_.abort_prepared_frame();
#endif
    }
    [[nodiscard]] bool matches_front(const PaintLeaseSnapshot& lease) const noexcept {
        const detail::DibFrameKey key = frames_.front_key();
        const detail::DibFrontView front = frames_.front();
        const bool matches = size_valid_ && frames_.has_front() && key.epoch == lease.surface_epoch &&
            key.revision == lease.content_revision && key.revision == lease.presented_revision &&
            key.scale == scale_ && front.width == width_ && front.height == height_;
        return matches;
    }
    [[nodiscard]] bool commit_frame(HDC target, const RECT& damage, PaintReceipt receipt) {
        const std::span<const RECT> single(&damage, 1U);
        const bool committed = commit_frame(target, single, receipt);
        return committed;
    }
    // Ends the open frame, copies it into a DIB candidate and commits that,
    // presenting exactly `damage` (stage DP partial present). The swap stays
    // all-or-nothing: on any failure the front and its key are unchanged.
    [[nodiscard]] bool commit_frame(HDC target, std::span<const RECT> damage, PaintReceipt receipt) {
        if (!frame_open_) return false;
        canvas_.end_frame();
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT)
        const PreparedTextStatus published = canvas_.commit_prepared_frame(receipt);
        if (published != PreparedTextStatus::success) {
            abort_frame();
            return false;
        }
#endif
        const detail::DibFrameKey key{.revision = receipt.rendered_revision,
            .epoch = receipt.surface_epoch, .scale = scale_};
        const bool committed = publish(target, damage, key);
        return committed;
    }
#endif

    bool synchronize_images(const ImageRegistry& registry) {
        const bool synchronized = canvas_.synchronize_images(registry);
        return synchronized;
    }

#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    // Copies the committed front's `damage` to the window.
    [[nodiscard]] bool present(HDC target, const RECT& damage) const {
        const detail::DibFrameStatus status = frames_.expose(target, damage);
        const bool exposed = status == detail::DibFrameStatus::success;
        return exposed;
    }
#else
    // Ends the open frame; its pixels reach the window through present.
    void end_frame() {
        if (!frame_open_) return;
        frame_open_ = false;
        canvas_.end_frame();
    }
    // Copies the painter's `damage` to the window, straight from the
    // painter's pixels: they are a top-down 32-bit DIB already (SK2, BGRA8),
    // so GDI copies them unconverted. The window is a copy of the painter's
    // last frame. Its alpha is not presented: this profile keeps no window
    // backdrop alpha, which needs the transactional store.
    [[nodiscard]] bool present(HDC target, const RECT& damage) {
        const void* const source = canvas_.pixels();
        const std::size_t row_bytes = canvas_.row_bytes();
        if (target == nullptr || source == nullptr || width_ <= 0 || height_ <= 0 ||
            canvas_.pixel_width() != static_cast<std::uint32_t>(width_) ||
            canvas_.pixel_height() != static_cast<std::uint32_t>(height_) ||
            row_bytes % 4U != 0U || row_bytes / 4U > static_cast<std::size_t>(std::numeric_limits<LONG>::max())) {
            return false;
        }
        DeviceArea area{};
        area.left = std::clamp<int>(damage.left, 0, width_);
        area.top = std::clamp<int>(damage.top, 0, height_);
        area.right = std::clamp<int>(damage.right, area.left, width_);
        area.bottom = std::clamp<int>(damage.bottom, area.top, height_);
        if (area.right == area.left || area.bottom == area.top) return true;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(row_bytes / 4U);
        info.bmiHeader.biHeight = -height_;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const int columns = area.right - area.left;
        const int rows = area.bottom - area.top;
        // GDI measures the source rectangle's y from the bottom row even for
        // a top-down DIB, so the rows above it are counted from there.
        const int source_y = height_ - area.bottom;
        const int copied = StretchDIBits(target, area.left, area.top, columns, rows,
                                         area.left, source_y, columns, rows,
                                         source, &info, DIB_RGB_COLORS, SRCCOPY);
        // StretchDIBits reports the scan lines it copied, or 0 or GDI_ERROR
        // on failure; for a partial source rectangle the count is not the
        // rectangle's height, so only failure is tested.
        const bool presented = copied != 0 && copied != static_cast<int>(GDI_ERROR);
        return presented;
    }

    // Draws one live frame into the open frame under `region` without
    // presenting it; the frame sequence presents afterwards (stage DP
    // decision 2). Retained painting can follow in the same frame.
    [[nodiscard]] bool compose_live_surface_frame(const LiveSurfaceFrame& frame,
                                                  const Rect destination, const Rect region) {
        if (!frame || destination.empty() || region.empty() || !frame_open_) return false;
        canvas_.save();
        canvas_.clip_rect(region);
        const bool drawn = canvas_.draw_live_surface_frame(frame, destination, 1.0);
        canvas_.restore();
        return drawn;
    }
#endif

#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    // Draws one live surface under its clip and commits it with the front's
    // own key: live pixels are no new retained revision.
    [[nodiscard]] bool present_live_surface(
        HDC target, const LiveSurfacePresentation& presentation) {
        if (target == nullptr || !presentation.surface ||
            presentation.destination.empty() || presentation.clip.empty()) {
            return false;
        }
        DamageRegion clip;
        clip.add(presentation.clip);
        try {
            if (!begin_frame(clip)) return false;
            canvas_.save();
            canvas_.clip_rect(presentation.clip);
            canvas_.draw_live_surface(presentation.surface, presentation.destination, 1.0);
            canvas_.restore();
        } catch (...) {
            abort_frame();
            return false;
        }
        canvas_.end_frame();
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT)
        const PreparedTextStatus published = canvas_.commit_prepared_frame(canvas_.prepared_front_receipt());
        if (published != PreparedTextStatus::success) {
            abort_frame();
            return false;
        }
#endif
        const DeviceArea area = device_area(presentation.clip, scale_, width_, height_);
        const RECT damage{area.left, area.top, area.right, area.bottom};
        const std::span<const RECT> single(&damage, 1U);
        const bool committed = publish(target, single, frames_.front_key());
        return committed;
    }
#endif

    bool save_bmp(std::wstring_view path) const {
        const PresentedPixels saved = presented_pixels();
        if (saved.pixels == nullptr || saved.width <= 0 || saved.height <= 0 || path.empty()) return false;
        const std::uint64_t pixel_bytes = static_cast<std::uint64_t>(saved.width) *
            static_cast<std::uint64_t>(saved.height) * 4U;
        if (pixel_bytes > std::numeric_limits<DWORD>::max()) return false;
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER bitmap{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(bitmap);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(pixel_bytes);
        bitmap.biSize = sizeof(bitmap);
        bitmap.biWidth = saved.width;
        bitmap.biHeight = -saved.height;
        bitmap.biPlanes = 1;
        bitmap.biBitCount = 32;
        bitmap.biCompression = BI_RGB;
        bitmap.biSizeImage = static_cast<DWORD>(pixel_bytes);
        HANDLE output = CreateFileW(std::wstring(path).c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) return false;
        DWORD written{};
        bool ok = WriteFile(output, &file, sizeof(file), &written, nullptr) && written == sizeof(file) &&
                  WriteFile(output, &bitmap, sizeof(bitmap), &written, nullptr) && written == sizeof(bitmap);
        const std::size_t row_bytes = static_cast<std::size_t>(saved.width) * 4U;
        for (int row = 0; ok && row < saved.height; ++row) {
            const std::byte* const line = saved.pixels + static_cast<std::size_t>(row) * saved.row_bytes;
            ok = WriteFile(output, line, static_cast<DWORD>(row_bytes), &written, nullptr) &&
                 written == row_bytes;
        }
        CloseHandle(output);
        return ok;
    }

    // Whether the frames begun after this keep premultiplied alpha in the
    // DIB: true while the window's backdrop grant is not opaque (stage T5,
    // C2), so that DWM composes the client's alpha. Otherwise every copied
    // pixel is presented opaque.
    void set_frame_alpha(bool kept) noexcept { frame_alpha_ = kept; }

    // Encodes the presented pixels as a PNG through the renderer-neutral core
    // writer: with alpha when frames keep it (stage T5, PLAN.md §3), else
    // opaque.
    [[nodiscard]] bool encode_png(std::vector<std::byte>& output) const {
        output.clear();
        const PresentedPixels saved = presented_pixels();
        if (saved.pixels == nullptr || saved.width <= 0 || saved.height <= 0) return false;
        gui_forms::image::PngEncodeInput input;
        input.width = static_cast<std::uint32_t>(saved.width);
        input.height = static_cast<std::uint32_t>(saved.height);
        input.row_bytes = static_cast<std::uint64_t>(saved.row_bytes);
        input.pixels = std::span<const std::byte>(saved.pixels,
            saved.row_bytes * static_cast<std::size_t>(saved.height));
        input.order = gui_forms::image::PngChannelOrder::bgra;
        input.alpha = frame_alpha_ ? gui_forms::image::PngSourceAlpha::premultiplied
                                   : gui_forms::image::PngSourceAlpha::opaque;
        return gui_forms::image::encode_png(input, output);
    }

    // Registers the bundled faces with the painter. Faces share their bytes
    // with every other raster given the same set.
    void set_bundled_fonts(std::shared_ptr<gui_forms::detail::BundledFontSet> fonts) {
        bundled_fonts_ready_ = fonts && canvas_.register_bundled_fonts(std::move(fonts));
    }
    [[nodiscard]] bool bundled_fonts_ready() const noexcept { return bundled_fonts_ready_; }

private:
    // The pixels a capture reads: the committed front (transactional), else
    // the painter's last frame.
    struct PresentedPixels final {
        const std::byte* pixels{};
        std::size_t row_bytes{};
        int width{};
        int height{};
    };
    [[nodiscard]] PresentedPixels presented_pixels() const noexcept {
        PresentedPixels result{};
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        const detail::DibFrontView front = frames_.front();
        result.pixels = reinterpret_cast<const std::byte*>(front.pixels.data());
        result.row_bytes = static_cast<std::size_t>(front.width) * 4U;
        result.width = front.width;
        result.height = front.height;
#else
        result.pixels = static_cast<const std::byte*>(canvas_.pixels());
        result.row_bytes = canvas_.row_bytes();
        result.width = static_cast<int>(canvas_.pixel_width());
        result.height = static_cast<int>(canvas_.pixel_height());
#endif
        return result;
    }

#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    // Copies the painter's ended frame into a new DIB candidate (the
    // candidate starts as a copy of the front) and commits it under `key`.
    // Only the frame's damage is copied while the front holds the painter's
    // previous frame; after any failure the whole raster is copied.
    [[nodiscard]] bool publish(HDC target, std::span<const RECT> damage, const detail::DibFrameKey key) {
        frame_open_ = false;
        const std::byte* const source = static_cast<const std::byte*>(canvas_.pixels());
        const bool extent_matches = canvas_.pixel_width() == static_cast<std::uint32_t>(width_) &&
                                    canvas_.pixel_height() == static_cast<std::uint32_t>(height_);
        if (source == nullptr || !extent_matches) {
            dib_synchronized_ = false;
            return false;
        }
        const detail::DibFrameStatus begun = frames_.begin(width_, height_);
        if (begun != detail::DibFrameStatus::success) {
            dib_synchronized_ = false;
            return false;
        }
        const detail::DibSurfaceView candidate = frames_.candidate();
        const std::size_t source_row_bytes = canvas_.row_bytes();
        const bool opaque = !frame_alpha_;
        if (!dib_synchronized_ || frame_damage_.empty()) {
            const DeviceArea whole{0, 0, width_, height_};
            copy_raster_area(source, source_row_bytes, candidate.pixels.data(), width_, whole, opaque);
        } else {
            const std::span<const Rect> rectangles = frame_damage_.rectangles();
            for (std::size_t index = 0; index < rectangles.size(); ++index) {
                const DeviceArea area = device_area(rectangles[index], scale_, width_, height_);
                copy_raster_area(source, source_row_bytes, candidate.pixels.data(), width_, area, opaque);
            }
        }
        const detail::DibFrameStatus status = frames_.commit(target, damage, key);
        frames_.abort();
        const bool committed = status == detail::DibFrameStatus::success;
        dib_synchronized_ = committed;
        return committed;
    }
#endif

    render::SkiaRaster canvas_;
    // The frame's damage, as admitted, for the copy into the DIB.
    DamageRegion frame_damage_;
    Size logical_size_{};
    double scale_{1.0};
    // The DIB's extent in device pixels.
    int width_{};
    int height_{};
    bool frame_open_{};
    bool frame_alpha_{};
    bool bundled_fonts_ready_{};
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    bool size_valid_{};
    // Whether the DIB front holds the painter's pixels outside the next
    // frame's damage.
    bool dib_synchronized_{};
    detail::DibFrameStore frames_;
#endif
};

// Only the UI thread reads or revokes window. A retained callback owns no host
// or native window; the immutable thread id is checked before mutable access.
struct TitleCallbackState final {
    const DWORD thread{GetCurrentThreadId()};
    HWND window{};
};
struct RequestWindowTitle final {
    std::weak_ptr<TitleCallbackState> owner{};
    HostServiceStatus operator()(const std::string_view title) const {
        const std::shared_ptr<TitleCallbackState> state = owner.lock();
        if (!state) return {HostServiceError::after_shutdown};
        if ((*state).thread != GetCurrentThreadId()) return {HostServiceError::wrong_thread};
        if ((*state).window == nullptr) return {HostServiceError::after_shutdown};
        try {
            if (title.size() > 65536U || title.find('\0') != std::string_view::npos ||
                !validate_utf8(title).valid()) return {HostServiceError::invalid_argument};
            const std::wstring native = wide_from_utf8(title);
            if (!SetWindowTextW((*state).window, native.c_str())) return {HostServiceError::backend_failure};
            return {};
        } catch (...) { return {HostServiceError::backend_failure}; }
    }
};
class WindowsHostState;
class WindowsApplicationRun;
// As TitleCallbackState: only the UI thread reads or revokes host. The
// operation owns no host; it reaches the host only while the window lives.
struct ClientSizeCallbackState final {
    const DWORD thread{GetCurrentThreadId()};
    WindowsHostState* host{};
};
struct RequestClientSize final {
    std::weak_ptr<ClientSizeCallbackState> owner{};
    HostServiceStatus operator()(Size client_size) const;
};
class WindowsHostState final {
    // Reaches request_client_size through its revocable callback state.
    friend struct RequestClientSize;
#if defined(GUI_FORMS_DIB_LIFECYCLE_TEST)
    friend struct DibHostFixture;
#endif
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT_TEST)
    friend struct PreparedHostFixture;
#endif
#if defined(GUI_FORMS_WINDOWS_TOOLTIP_TEST)
    friend struct TooltipHostFixture;
#endif
public:
    WindowsHostState(std::unique_ptr<Window> model, WindowsHostOptions options)
        : model_(std::move(model)), options_(std::move(options)),
          services_(), session_(*model_, windows_capabilities(), &services_) {
        // The availability record (stage DP). Both the default host and the
        // transactional store present each damage rectangle.
        PresentationDeclaration presentation;
        presentation.contract_version = paint_contract_version;
        presentation.extensions.partial_present = true;
        (*model_).declare_presentation(presentation);
        // A high-resolution timer keeps the live clock's deadlines to well
        // under a millisecond; an ordinary one rounds them to the system
        // timer resolution (15.6 ms by default), which beats against the
        // display's refresh.
        live_frame_clock_ = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (live_frame_clock_ == nullptr) {
            live_frame_clock_ = CreateWaitableTimerExW(
                nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        qpc_frequency_ = frequency.QuadPart;
        const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm != nullptr) {
            dwm_composition_timing_ = reinterpret_cast<DwmCompositionTimingFunction>(
                reinterpret_cast<void*>(GetProcAddress(dwm, "DwmGetCompositionTimingInfo")));
        }
    }

    ~WindowsHostState() {
        (*title_state_).window = nullptr;
        (*size_state_).host = nullptr;
        accessibility_.detach();
        // Initialization can unwind before WM_DESTROY. Detach the callback
        // address before releasing the native window so no later message can
        // reach this partially destroyed owner.
        if (hwnd_ != nullptr) {
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        if (live_frame_clock_ != nullptr) {
            CancelWaitableTimer(live_frame_clock_);
            CloseHandle(live_frame_clock_);
            live_frame_clock_ = nullptr;
        }
        hide_tooltip();
        if (model_) {
            (*model_).set_paint_wake_handler({});
            (*model_).set_text_metrics_provider(nullptr);
            (*model_).set_framebuffer_painter(nullptr);
        }
        session_.shutdown();
        services_.shutdown();
    }

    void bind_window(HWND window) noexcept {
        if (native_phase_ != NativePhase::creating) return;
        hwnd_ = window;
        (*title_state_).window = window;
        (*size_state_).host = this;
        services_.bind_owner(window, *model_);
        native_phase_ = NativePhase::bound;
    }

    bool initialize(HWND window) {
        if (native_phase_ != NativePhase::bound || window == nullptr || hwnd_ != window) {
            return false;
        }
        hwnd_ = window;
        scale_ = query_scale(window);
        const std::filesystem::path font_directory = bundled_font_directory();
        std::shared_ptr<gui_forms::detail::BundledFontSet> fonts = load_bundled_fonts(font_directory);
        RECT client{};
        GetClientRect(window, &client);
        const Size logical{(client.right - client.left) / scale_,
                           (client.bottom - client.top) / scale_};
        raster_.resize(logical, scale_);
        raster_.set_bundled_fonts(std::move(fonts));
        const bool fonts_ready = raster_.bundled_fonts_ready();
        render::SkiaRaster& painter = raster_.painter();
        (*model_).set_text_metrics_provider(fonts_ready ? &painter : nullptr);
        (*model_).set_framebuffer_painter(fonts_ready ? &painter : nullptr);
        (*model_).metrics().set_renderer(
            fonts_ready
                ? "Win32 DIB · Skia CPU raster · HarfBuzz/FreeType text · owned PNG · bundled fonts"
                : "Win32 DIB · Skia CPU raster · incomplete bundled font pack",
            true);
        const HostDispatchResult attached = dispatch(HostAttachEvent{logical, scale_});
        if (!attached.accepted()) return false;
        native_phase_ = NativePhase::attached;
        report_compositor_state();
        report_input_settings();
        accessibility_.attach(hwnd_, *model_, scale_);
        (*model_).set_dispatch_wake_handler(
            gui_forms::detail::BoundMemberFunction<
                void (WindowsHostState::*)() noexcept>(
                    *this, &WindowsHostState::post_managed_dispatch));
        (*model_).set_paint_wake_handler(
            gui_forms::detail::BoundMemberFunction<
                void (WindowsHostState::*)() noexcept>(
                    *this, &WindowsHostState::request_render_wake));
        if (options_.host_ready) {
            options_.host_ready(
                gui_forms::detail::BoundMemberFunction<
                    void (WindowsHostState::*)() noexcept>(
                        *this, &WindowsHostState::post_managed_dispatch),
                gui_forms::detail::BoundMemberFunction<
                    void (WindowsHostState::*)() noexcept>(
                        *this, &WindowsHostState::post_close),
                gui_forms::detail::BoundMemberFunction<
                    HostDialogResult (HostServices::*)(
                        const HostDialogRequest&)>(
                            services_, &HostServices::show_dialog),
                gui_forms::detail::BoundMemberFunction<
                    HostServiceStatus (WindowsHostState::*)(
                        const HostTooltipRequest&)>(
                            *this, &WindowsHostState::show_tooltip),
                gui_forms::detail::BoundMemberFunction<
                    void (WindowsHostState::*)() noexcept>(
                        *this, &WindowsHostState::hide_tooltip),
                gui_forms::detail::BoundMemberFunction<
                    HostClipboardTextResult (HostServices::*)()>(
                        services_, &HostServices::read_clipboard_text),
                gui_forms::detail::BoundMemberFunction<
                    HostServiceStatus (HostServices::*)(std::string_view)>(
                        services_, &HostServices::write_clipboard_text));
        }
        if (options_.full_screen_ready) {
            options_.full_screen_ready(gui_forms::detail::BoundMemberFunction<void (WindowsHostState::*)() noexcept>(
                *this, &WindowsHostState::toggle_full_screen));
        }
        if (options_.title_ready) {
            options_.title_ready(RequestWindowTitle{title_state_});
        }
        if (options_.client_size_ready) {
            options_.client_size_ready(RequestClientSize{size_state_});
        }
        if (options_.visibility_ready) {
            options_.visibility_ready(
                gui_forms::detail::BoundMemberFunction<void (WindowsHostState::*)() noexcept>(
                    *this, &WindowsHostState::show_window),
                gui_forms::detail::BoundMemberFunction<void (WindowsHostState::*)() noexcept>(
                    *this, &WindowsHostState::hide_window));
        }
        // One FIFO initialization turn runs before presentation. Work posted by
        // those callbacks stays deferred to the ordinary next host turn.
        static_cast<void>((*model_).drain_posted_work());
        if (options_.dispatch_pending) options_.dispatch_pending();
        native_phase_ = NativePhase::ready;
        collect_damage();
        return true;
    }

    // Reports DWM's state to the window (stage T5, C2): after attach, and
    // whenever a message may have changed a setting it reads. A reading
    // that differs from the window's last report only by its generation is
    // not sent.
    void report_compositor_state() {
        const CompositorGeneration next{compositor_generation_.value + 1U};
        const CompositorState state = detail::read_windows_compositor_state(hwnd_, next);
        CompositorState unchanged = state;
        unchanged.generation = (*model_).compositor_state().generation;
        if (compositor_generation_.value != 0U && unchanged == (*model_).compositor_state()) return;
        compositor_generation_ = next;
        static_cast<void>(dispatch(HostCompositorEvent{state}));
        collect_damage();
    }

    // The next frame keeps alpha while the window's backdrop is not opaque.
    void prepare_frame_alpha() noexcept {
        const bool kept = (*model_).backdrop_grant().granted != WindowBackdrop::opaque;
        raster_.set_frame_alpha(kept);
    }

    bool will_show() noexcept {
        if (native_phase_ != NativePhase::ready) return false;
        native_phase_ = NativePhase::visible;
        return true;
    }

    // Application-scope windowing (windows_application_run.inc). Bound before
    // the native window is created; the run outlives this state.
    void attach_run(WindowsApplicationRun& run, HostWindowId window) noexcept {
        run_ = &run;
        window_id_ = window;
    }
    [[nodiscard]] HWND native_window() const noexcept { return hwnd_; }
    [[nodiscard]] Size minimum_size() const noexcept { return options_.minimum_size; }
    // Full screen, maximized and minimized windows refuse frame requests, as
    // they refuse client size requests.
    [[nodiscard]] bool frame_fixed() const noexcept {
        const bool fixed = hwnd_ == nullptr || full_screen_ || IsZoomed(hwnd_) || IsIconic(hwnd_);
        return fixed;
    }
    // The native window and its session are gone (WM_NCDESTROY ran).
    [[nodiscard]] bool destroyed() const noexcept {
        const bool gone = hwnd_ == nullptr && native_phase_ == NativePhase::shutdown;
        return gone;
    }
    // HostWindowing::destroy_window: closes without asking the closing
    // handler; WM_DESTROY publishes the closed event with reason application.
    void destroy_for_application() noexcept {
        if (hwnd_ == nullptr || closed_) return;
        destroy_reason_ = HostCloseReason::application;
        native_phase_ = NativePhase::closing;
        DestroyWindow(hwnd_);
    }
    // Defined in windows_application_run.inc.
    void report_frame_to_run();
    void retire_from_run() noexcept;
    void report_application_active_to_run(bool active);
    void hidden_on_close_to_run() noexcept;
    // Shows without activating, for the floating policy.
    void show_without_activation() noexcept {
        if (hwnd_ != nullptr && !closed_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    void hide_for_policy() noexcept { hide_window(); }

    [[nodiscard]] HANDLE live_frame_clock() const noexcept {
        return live_frame_clock_;
    }

    // The live clock fired: a live frame is due, and the clock is armed for
    // the next refresh while live presentations remain (the drain cancels it
    // when none do).
    void mark_live_frame_due() noexcept {
        ++live_clock_signals_;
        live_frame_due_ = true;
        if (live_clock_armed_) arm_live_clock();
    }

    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCDESTROY) {
            const HWND destroyed = hwnd_;
            hwnd_ = nullptr;
            SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
            return DefWindowProcW(destroyed, message, wparam, lparam);
        }
        const bool ready = native_phase_ == NativePhase::ready ||
            native_phase_ == NativePhase::visible ||
            native_phase_ == NativePhase::closing;
        if (!ready) {
            switch (message) {
            case WM_GETOBJECT:
                if (static_cast<LONG>(lparam) == OBJID_CLIENT) { return accessibility_.object(wparam); }
                if (WindowsAccessibility::automation_request(lparam)) {
                    return accessibility_.automation_object(wparam, lparam);
                }
                break;
            case WM_GETMINMAXINFO:
                minimum_size(reinterpret_cast<MINMAXINFO*>(lparam));
                return 0;
            case WM_PAINT: {
                PAINTSTRUCT paint_state{};
                BeginPaint(hwnd_, &paint_state);
                EndPaint(hwnd_, &paint_state);
                return 0;
            }
            case WM_DESTROY:
                accessibility_.detach();
                break;
            default:
                return DefWindowProcW(hwnd_, message, wparam, lparam);
            }
        }
        switch (message) {
        case WM_SETTINGCHANGE:
            // Broadcast to top-level windows for any system setting; the
            // double-click interval is read again and reported if it moved,
            // and the compositor state when the setting is one it reads.
            report_input_settings();
            if (detail::windows_compositor_message(message, wparam, lparam)) report_compositor_state();
            break;
        case WM_THEMECHANGED:
        case WM_DWMCOMPOSITIONCHANGED:
            if (detail::windows_compositor_message(message, wparam, lparam)) report_compositor_state();
            break;
        case WM_ACTIVATEAPP:
            // Sent to every top-level window of the thread; the windowing
            // ignores repeated reports of the same state.
            report_application_active_to_run(wparam != FALSE);
            break;
        case WM_SIZE: resize(); report_frame_to_run(); return 0;
        case WM_MOVE: report_frame_to_run(); return 0;
        case WM_DPICHANGED: dpi_changed(wparam, lparam); return 0;
        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE) { services_.revoke_cursor_interaction(); }
            if (trace_win32_input()) {
                std::fprintf(stderr, "win32-input=activate|state:%u|active:%d|focus:%d\n",
                             static_cast<unsigned>(LOWORD(wparam)),
                             GetActiveWindow() == hwnd_, GetFocus() == hwnd_);
                std::fflush(stderr);
            }
            dispatch(HostActivationEvent{LOWORD(wparam) != WA_INACTIVE});
            collect_damage(); return 0;
        case WM_MOUSEACTIVATE:
            if (trace_win32_input()) {
                std::fprintf(stderr, "win32-input=mouse-activate|hit:%u|message:%u|active:%d|focus:%d\n",
                             static_cast<unsigned>(LOWORD(lparam)),
                             static_cast<unsigned>(HIWORD(lparam)),
                             GetActiveWindow() == hwnd_, GetFocus() == hwnd_);
                std::fflush(stderr);
            }
            break;
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                const bool hidden = services_.maintain_hidden_cursor();
                if (hidden) { return TRUE; }
                POINT point{};
                bool applied = false;
                if (GetCursorPos(&point) && ScreenToClient(hwnd_, &point)) {
                    applied = update_cursor({point.x / scale_, point.y / scale_});
                } else {
                    applied = apply_system_cursor(CursorKind::arrow);
                }
                // Only consume WM_SETCURSOR after installing a valid cursor.
                // Returning TRUE after SetCursor(nullptr) leaves the cursor
                // hidden and prevents DefWindowProc from restoring hCursor.
                if (applied) return TRUE;
            }
            services_.revoke_cursor_interaction();
            break;
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
            services_.revoke_cursor_interaction();
            (*model_).release_pointer();
            break;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lparam) != hwnd_) {
                services_.revoke_cursor_interaction();
                (*model_).release_pointer();
            }
            break;
        case WM_SHOWWINDOW:
            if (wparam == FALSE) { services_.revoke_cursor_interaction(); }
            break;
        case WM_MOUSELEAVE:
            services_.revoke_cursor_interaction();
            break;
        case WM_PAINT:
            // A render wake remains coalesced until the host actually begins
            // the paint transaction. Releasing it here allows content that
            // changes during this frame to request one later frame without
            // turning the posted wake itself into a synchronous paint loop.
            render_dispatch_pending_ = false;
            presentation_pending_ = false;
            paint();
            return 0;
        case WM_ENTERSIZEMOVE:
            // DefWindowProc enters a nested modal loop while a top-level
            // window is moved or sized. The outer waitable-timer loop cannot
            // run there, but the live plane must keep presenting independently
            // of retained layout and input. A modal-loop timer is the native
            // Win32 clock bridge for exactly that interval.
            in_size_move_ = true;
            if ((*model_).has_live_surface_presentations()) {
                SetTimer(hwnd_, modal_live_surface_timer,
                         live_surface_period_milliseconds, nullptr);
            }
            return 0;
        case WM_EXITSIZEMOVE:
            KillTimer(hwnd_, modal_live_surface_timer);
            in_size_move_ = false;
            mark_live_frame_due();
            return 0;
        case managed_dispatch_message:
            static_cast<void>((*model_).drain_posted_work());
            if (options_.dispatch_pending) options_.dispatch_pending();
            collect_damage();
            return 0;
        case render_dispatch_message:
            // Gather damage in this asynchronous turn, but let the outer host
            // loop drain its bounded message batch before presenting. A
            // continuously animated surface must not monopolize the posted-
            // message lane ahead of pointer and keyboard input.
            render_dispatch_pending_ = false;
            collect_damage();
            presentation_pending_ =
                GetUpdateRect(hwnd_, nullptr, FALSE) != FALSE;
            return 0;
        case WM_TIMER:
            if (wparam == modal_live_surface_timer) {
                // Do not enter retained polling/layout from the native modal
                // loop. Sample and present only the newest live generation.
                present_live_surface_updates();
                return 0;
            }
            if (wparam == scheduler_timer) {
                KillTimer(hwnd_, scheduler_timer);
                try {
                    collect_damage();
                } catch (const std::exception& error) {
                    ++native_callback_faults_;
                    std::fprintf(
                        stderr,
                        "gui-forms-host=scheduled-wake-fault|what:%s\n",
                        error.what());
                    std::fflush(stderr);
                } catch (...) {
                    ++native_callback_faults_;
                    std::fprintf(
                        stderr,
                        "gui-forms-host=scheduled-wake-fault|what:unknown\n");
                    std::fflush(stderr);
                }
                return 0;
            }
            break;
        case WM_MOUSEMOVE: pointer(message, PointerAction::move, wparam, lparam); return 0;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            SetFocus(hwnd_); pointer(message, PointerAction::down, wparam, lparam); return 0;
        case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK:
            SetFocus(hwnd_); pointer(message, PointerAction::down, wparam, lparam); return 0;
        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
            pointer(message, PointerAction::up, wparam, lparam); return 0;
        case WM_MOUSEWHEEL: wheel(wparam, lparam); return 0;
        case WM_SYSKEYDOWN:
            // Preserve the native close gesture. Swallowing all system keys
            // prevents DefWindowProc from producing the ordinary WM_CLOSE,
            // including its existing cancellation and owned-window policy.
            if (wparam == VK_F4 && (lparam & (1LL << 29)) != 0) break;
            key(KeyAction::down, wparam, lparam); return 0;
        case WM_KEYDOWN: key(KeyAction::down, wparam, lparam); return 0;
        case WM_KEYUP: case WM_SYSKEYUP: key(KeyAction::up, wparam, lparam); return 0;
        case WM_CHAR: character(static_cast<wchar_t>(wparam)); return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == OBJID_CLIENT) { return accessibility_.object(wparam); }
            if (WindowsAccessibility::automation_request(lparam)) {
                return accessibility_.automation_object(wparam, lparam);
            }
            break;
        case WM_GETMINMAXINFO: minimum_size(reinterpret_cast<MINMAXINFO*>(lparam)); return 0;
        case WM_COPYDATA: return automation(reinterpret_cast<const COPYDATASTRUCT*>(lparam));
        case WM_CLOSE: return close();
        case WM_DESTROY:
            (*title_state_).window = nullptr;
            (*size_state_).host = nullptr;
            services_.revoke_cursor_interaction(CursorError::closing);
            accessibility_.detach();
            if (!closed_) {
                const HostLifecyclePhase phase = session_.snapshot().phase;
                if (phase == HostLifecyclePhase::attached ||
                    phase == HostLifecyclePhase::close_authorized) {
                    dispatch(HostClosedEvent{destroy_reason_});
                }
                closed_ = true;
                native_phase_ = NativePhase::closed;
                // Retired before the closed callback, so the callback may open
                // a replacement under the same stable id.
                retire_from_run();
                if (options_.closed) options_.closed();
            }
            (*model_).set_paint_wake_handler({});
            session_.shutdown();
            native_phase_ = NativePhase::shutdown;
            services_.shutdown();
            if (options_.quit_thread_on_close) PostQuitMessage(0);
            return 0;
        default: break; // Every other message takes the default handling below.
        }
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    }

    std::string metrics_json() const { return (*model_).metrics_snapshot().to_json(); }
    std::string host_json() const {
        return "{\"session\":" + session_.snapshot().to_json() +
               ",\"services\":" + services_.snapshot().to_json() +
               ",\"presentation\":" + (*model_).presentation_availability().to_json() +
               ",\"compatibility_surfaces\":" +
               detail::compatibility_paint_metrics_json() +
               ",\"live_presentations\":{" +
               "\"clock_signals\":" + std::to_string(live_clock_signals_) +
               ",\"drains\":" + std::to_string(live_presentation_drains_) +
               ",\"updates_sampled\":" +
                   std::to_string(live_updates_sampled_) +
               ",\"updates_presented\":" +
                   std::to_string(live_updates_presented_) +
               ",\"updates_failed\":" +
                   std::to_string(live_updates_failed_) +
               ",\"duration_nanoseconds\":" +
                   std::to_string(live_present_duration_nanoseconds_) +
               ",\"worst_duration_nanoseconds\":" +
                   std::to_string(live_worst_present_duration_nanoseconds_) +
               ",\"retained_with_frames\":" +
                   std::to_string(live_sequence_retained_presented_) +
               ",\"retained_deferred\":" +
                   std::to_string(live_sequence_retained_deferred_) +
               "}}";
    }

    void present_pending_frame() {
        if (hwnd_ == nullptr) return;
        if (presentation_pending_) {
            presentation_pending_ = false;
            if (GetUpdateRect(hwnd_, nullptr, FALSE) != FALSE) paint();
        }
        if (live_frame_due_) {
            live_frame_due_ = false;
            present_live_surface_updates();
        }
    }

private:
    void post_managed_dispatch() noexcept {
        if (hwnd_ != nullptr) {
            PostMessageW(hwnd_, managed_dispatch_message, 0, 0);
        }
    }

    void post_close() noexcept {
        if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }

    void show_window() noexcept {
        if (hwnd_ != nullptr && !closed_) ShowWindow(hwnd_, SW_SHOW);
    }

    void toggle_full_screen() noexcept {
        if (hwnd_ == nullptr || closed_) return;
        if (!full_screen_) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            restore_placement_.length = sizeof(WINDOWPLACEMENT);
            if (!GetWindowPlacement(hwnd_, &restore_placement_) ||
                !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) return;
            restore_style_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
            SetWindowLongPtrW(hwnd_, GWL_STYLE, restore_style_ & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        } else {
            SetWindowLongPtrW(hwnd_, GWL_STYLE, restore_style_);
            SetWindowPlacement(hwnd_, &restore_placement_);
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        full_screen_ = !full_screen_;
    }

    void hide_window() noexcept {
        if (hwnd_ != nullptr && !closed_) ShowWindow(hwnd_, SW_HIDE);
    }

    // Asks for a client area of `client_size` logical units. SetWindowPos
    // sends WM_SIZE before it returns, and the model is resized through the
    // ordinary resize event (deferred while a host callback is running).
    HostServiceStatus request_client_size(const Size client_size) {
        if (hwnd_ == nullptr || closed_) return {HostServiceError::after_shutdown};
        if (client_size.width < options_.minimum_size.width ||
            client_size.height < options_.minimum_size.height) {
            return {HostServiceError::invalid_argument};
        }
        if (full_screen_ || IsZoomed(hwnd_) || IsIconic(hwnd_)) {
            return {HostServiceError::unsupported};
        }
        RECT frame{0, 0,
                   static_cast<LONG>(std::lround(client_size.width * scale_)),
                   static_cast<LONG>(std::lround(client_size.height * scale_))};
        adjust_client_frame(frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)),
            static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), scale_);
        const BOOL moved = SetWindowPos(hwnd_, nullptr, 0, 0,
            frame.right - frame.left, frame.bottom - frame.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        if (moved == FALSE) return {HostServiceError::backend_failure};
        return {};
    }

    template <class Payload>
    HostDispatchResult dispatch(Payload payload) {
        HostEvent event;
        event.sequence = next_sequence_++;
        event.timestamp_nanoseconds = now_nanoseconds();
        event.payload = std::move(payload);
        return session_.dispatch(std::move(event));
    }

    HostServiceStatus show_tooltip(const HostTooltipRequest& request) {
        hide_tooltip();
        if (request.text.empty()) return {};
        tooltip_.text = wide_from_utf8(request.text);
        if (tooltip_.text.empty()) return {HostServiceError::invalid_utf8};

        WNDCLASSEXW native_class{};
        native_class.cbSize = sizeof(native_class);
        native_class.lpfnWndProc = tooltip_window_procedure;
        native_class.hInstance = GetModuleHandleW(nullptr);
        native_class.hCursor = load_system_cursor(IDC_ARROW);
        native_class.lpszClassName = tooltip_class_name;
        if (RegisterClassExW(&native_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return {HostServiceError::backend_failure};
        }

        if (!paint_tooltip(request.text)) return {HostServiceError::backend_failure};
        const int width = tooltip_.width;
        const int height = tooltip_.height;

        POINT anchor{static_cast<LONG>(std::lround(request.anchor.x * scale_)),
                     static_cast<LONG>(std::lround(request.anchor.y * scale_))};
        ClientToScreen(hwnd_, &anchor);
        RECT work{};
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST),
                            &monitor)) {
            work = monitor.rcWork;
        } else {
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        }
        const int x = std::clamp<int>(anchor.x, work.left,
                                      std::max(work.left, work.right - width));
        const int y = std::clamp<int>(anchor.y, work.top,
                                      std::max(work.top, work.bottom - height));
        tooltip_.window = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            tooltip_class_name, tooltip_.text.c_str(), WS_POPUP,
            x, y, width, height, hwnd_, nullptr, GetModuleHandleW(nullptr), &tooltip_);
        if (tooltip_.window == nullptr) return {HostServiceError::backend_failure};
        ShowWindow(tooltip_.window, SW_SHOWNOACTIVATE);
        UpdateWindow(tooltip_.window);
        if (request.duration_milliseconds != 0U) {
            SetTimer(tooltip_.window, 1,
                     std::max<UINT>(1U, request.duration_milliseconds), nullptr);
        }
        return {};
    }

    void hide_tooltip() noexcept {
        if (tooltip_.window != nullptr && IsWindow(tooltip_.window)) {
            DestroyWindow(tooltip_.window);
        }
        tooltip_.window = nullptr;
        tooltip_.text.clear();
        tooltip_.pixels.clear();
    }

    // The logical extent that the framebuffer rounds up to exactly pixels.
    // pixels / scale can land a hair above the quotient (at 1.1, 71 / 1.1
    // * 1.1 > 71), which would allocate an extra unpainted row or column.
    static double logical_extent(const double pixels, const double scale) {
        double logical = pixels / scale;
        while (std::ceil(logical * scale) > pixels) logical = std::nextafter(logical, 0.0);
        return logical;
    }

    // A logical rect that fill_rect maps to exactly the given device pixels.
    // fill_rect covers every touched pixel, so edges at whole-pixel logical
    // positions can spill a pixel either way; edges a quarter pixel inside
    // the span cannot.
    static Rect device_pixels(const double column, const double row, const double columns, const double rows,
                              const double scale) {
        return {(column + 0.25) / scale, (row + 0.25) / scale, (columns - 0.5) / scale, (rows - 0.5) / scale};
    }

    // Wraps the tooltip text at word boundaries to 360 logical pixels and
    // paints it, with its face and border, through the window's painter.
    bool paint_tooltip(const std::string_view utf8) {
        if (!raster_.bundled_fonts_ready()) return false;
        const FontSpec font{.role = FontRole::control, .size = 13.0};
        constexpr double wrap_width = 360.0;
        std::vector<std::string> lines;
        std::size_t offset = 0;
        for (;;) {
            const std::size_t newline = utf8.find('\n', offset);
            std::string_view paragraph = utf8.substr(offset, newline == std::string_view::npos ? std::string_view::npos
                                                                                               : newline - offset);
            if (!paragraph.empty() && paragraph.back() == '\r') paragraph.remove_suffix(1U);
            std::string line;
            std::size_t word_start = 0;
            while (word_start <= paragraph.size()) {
                std::size_t word_end = paragraph.find(' ', word_start);
                if (word_end == std::string_view::npos) word_end = paragraph.size();
                const std::string_view word = paragraph.substr(word_start, word_end - word_start);
                std::string candidate = line;
                if (!candidate.empty()) candidate += ' ';
                candidate += word;
                if (line.empty() || raster_.painter().measure_text_utf8(candidate, font).width <= wrap_width) {
                    line = std::move(candidate);
                } else {
                    lines.push_back(std::move(line));
                    line = std::string(word);
                }
                word_start = word_end + 1U;
            }
            lines.push_back(std::move(line));
            if (newline == std::string_view::npos) break;
            offset = newline + 1U;
        }
        double text_width = 0.0;
        for (const std::string& line : lines) {
            text_width = std::max(text_width, raster_.painter().measure_text_utf8(line, font).width);
        }
        const ResolvedTextLayout metrics = raster_.painter().resolve_text_layout_utf8("Ag", font);
        const double line_height = metrics.ascent + metrics.descent + metrics.line_gap;
        const Size requested{std::clamp(text_width + 16.0, 40.0, 376.0),
                             std::max(24.0, line_height * static_cast<double>(lines.size()) + 10.0)};
        // The popup presents every allocated device pixel. Align its logical
        // extent before painting so fractional sizes cannot leave a clear
        // final row/column or make the one-device-pixel border two pixels wide.
        const double columns = std::ceil(requested.width * scale_);
        const double rows = std::ceil(requested.height * scale_);
        const Size logical{logical_extent(columns, scale_), logical_extent(rows, scale_)};
        const std::unique_ptr<PaintFramebuffer> framebuffer = raster_.painter().create_framebuffer(logical, scale_);
        if (!framebuffer) return false;
        // The tooltip draws no images; its frame covers the whole popup.
        const ImageRegistry no_images{};
        if (!(*framebuffer).begin(no_images, {0.0, 0.0, logical.width, logical.height})) return false;
        Painter& painter = (*framebuffer).painter();
        const Color border = Color::rgba(105, 116, 128);
        painter.fill_rect({0.0, 0.0, logical.width, logical.height}, Color::rgba(255, 255, 240));
        painter.fill_rect(device_pixels(0.0, 0.0, columns, 1.0, scale_), border);
        painter.fill_rect(device_pixels(0.0, rows - 1.0, columns, 1.0, scale_), border);
        painter.fill_rect(device_pixels(0.0, 0.0, 1.0, rows, scale_), border);
        painter.fill_rect(device_pixels(columns - 1.0, 0.0, 1.0, rows, scale_), border);
        for (std::size_t index = 0; index < lines.size(); ++index) {
            const Point baseline{8.0, 5.0 + metrics.ascent + line_height * static_cast<double>(index)};
            painter.draw_text_utf8(baseline, lines[index], font, Color::rgba(31, 37, 44));
        }
        (*framebuffer).end();
        tooltip_.width = static_cast<int>((*framebuffer).width());
        tooltip_.height = static_cast<int>((*framebuffer).height());
        const std::span<std::byte> bytes = (*framebuffer).pixels();
        tooltip_.pixels.resize(static_cast<std::size_t>(tooltip_.width) * static_cast<std::size_t>(tooltip_.height));
        for (int row = 0; row < tooltip_.height; ++row) {
            std::memcpy(tooltip_.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(tooltip_.width),
                        bytes.data() + static_cast<std::size_t>(row) * (*framebuffer).row_bytes(),
                        static_cast<std::size_t>(tooltip_.width) * 4U);
        }
        return true;
    }

    // The system's double-click interval (GetDoubleClickTime, which Windows
    // keeps between 1 and 5000 milliseconds), reported after attach and on
    // WM_SETTINGCHANGE whenever it differs from the last report.
    void report_input_settings() {
        const UINT read = GetDoubleClickTime();
        std::uint32_t interval = static_cast<std::uint32_t>(read);
        if (interval == 0U) interval = 1U;
        if (interval > maximum_double_click_milliseconds) interval = maximum_double_click_milliseconds;
        if (interval == reported_double_click_milliseconds_) return;
        const HostDispatchResult reported = dispatch(HostInputSettingsEvent{interval, SettingOrigin::host});
        if (reported.accepted()) reported_double_click_milliseconds_ = interval;
    }

    void resize() {
        RECT client{};
        GetClientRect(hwnd_, &client);
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        const Size logical{std::max(0.0, (client.right - client.left) / scale_),
                           std::max(0.0, (client.bottom - client.top) / scale_)};
#else
        const Size logical{std::max(1.0, (client.right - client.left) / scale_),
                           std::max(1.0, (client.bottom - client.top) / scale_)};
#endif
        raster_.resize(logical, scale_);
        dispatch(HostResizeEvent{logical});
        collect_damage();
    }

    void dpi_changed(WPARAM wparam, LPARAM lparam) {
        const double next = std::max(1.0, HIWORD(wparam) / 96.0);
        if (next != scale_) { scale_ = next; dispatch(HostScaleEvent{scale_}); }
        if (const RECT* suggested = reinterpret_cast<const RECT*>(lparam)) {
            SetWindowPos(hwnd_, nullptr, (*suggested).left, (*suggested).top,
                         (*suggested).right - (*suggested).left,
                         (*suggested).bottom - (*suggested).top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
        resize();
    }

    // Portable frame capture (capture_path host option): runs once, after the
    // first presented frame whose update rectangle covers the whole client
    // area, then requests close through the ordinary WM_CLOSE path.
    void complete_frame_capture_if_due(const RECT& painted) {
        if (frame_capture_completed_ || options_.capture_path.empty() || hwnd_ == nullptr) return;
        RECT client{};
        if (!GetClientRect(hwnd_, &client) || client.right <= client.left ||
            client.bottom <= client.top) return;
        if (painted.left > client.left || painted.top > client.top ||
            painted.right < client.right || painted.bottom < client.bottom) return;
        frame_capture_completed_ = true;
        std::vector<std::byte> encoded;
        if (!raster_.encode_png(encoded)) encoded.clear();
        frame_capture_saved_ = detail::complete_frame_capture(options_.capture_path, encoded);
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
public:
    // Read by run_windows after the message loop: the portable contract fails
    // the process when a requested capture was never saved.
    [[nodiscard]] bool frame_capture_saved() const noexcept { return frame_capture_saved_; }

private:

    // The whole device pixels covering a logical rectangle, clipped to the
    // client area.
    [[nodiscard]] RECT device_rect(Rect logical) const noexcept {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const RECT device{
            std::clamp<LONG>(static_cast<LONG>(std::floor(logical.x * scale_)),
                             client.left, client.right),
            std::clamp<LONG>(static_cast<LONG>(std::floor(logical.y * scale_)),
                             client.top, client.bottom),
            std::clamp<LONG>(static_cast<LONG>(std::ceil(
                                 (logical.x + logical.width) * scale_)),
                             client.left, client.right),
            std::clamp<LONG>(static_cast<LONG>(std::ceil(
                                 (logical.y + logical.height) * scale_)),
                             client.top, client.bottom)};
        return device;
    }

    // The device pixels a native rectangle covers; zero when it is empty.
    [[nodiscard]] static std::uint64_t device_pixels(const RECT& device) noexcept {
        if (device.right <= device.left || device.bottom <= device.top) return 0U;
        return static_cast<std::uint64_t>(device.right - device.left) *
               static_cast<std::uint64_t>(device.bottom - device.top);
    }

    [[nodiscard]] Rect logical_rect(const RECT& device) const noexcept {
        const Rect logical{device.left / scale_, device.top / scale_,
                           (device.right - device.left) / scale_,
                           (device.bottom - device.top) / scale_};
        return logical;
    }

    // Adds the window's update region to the pending damage rectangle by
    // rectangle. It must run before BeginPaint, which validates the region.
    // Windows exposes whole device pixels, so painting these rectangles
    // restores the same pixel edges a native exposure needs. Returns false
    // when the region cannot be read; the caller then falls back to rcPaint.
    bool add_update_region_damage() {
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        if (region == nullptr) return false;
        bool added = false;
        const int kind = GetUpdateRgn(hwnd_, region, FALSE);
        if (kind == SIMPLEREGION || kind == COMPLEXREGION) {
            const DWORD bytes = GetRegionData(region, 0, nullptr);
            // RGNDATA needs DWORD alignment, so the buffer is held in DWORDs.
            std::vector<DWORD> storage((bytes + sizeof(DWORD) - 1U) / sizeof(DWORD));
            RGNDATA* data = reinterpret_cast<RGNDATA*>(storage.data());
            if (bytes != 0U && GetRegionData(region, bytes, data) == bytes) {
                const RECT* rectangles = reinterpret_cast<const RECT*>((*data).Buffer);
                const DWORD count = (*data).rdh.nCount;
                for (DWORD index = 0; index < count; ++index) {
                    pending_damage_.add(logical_rect(rectangles[index]));
                }
                added = count != 0U;
            }
        } else if (kind == NULLREGION) {
            added = true;
        }
        DeleteObject(region);
        return added;
    }

    void collect_damage() {
        static_cast<void>((*model_).poll_frame_schedule(FrameClock::now()));
        accessibility_.notify(scale_);
        DamageRegion damage = (*model_).take_damage();
        // Each rectangle is invalidated on its own (stage DP): Windows keeps
        // them as one update region, so distant changes do not repaint what
        // lies between them.
        const std::span<const Rect> rectangles = damage.rectangles();
        for (std::size_t index = 0; index < rectangles.size(); ++index) {
            pending_damage_.add(rectangles[index]);
            const RECT native_damage = device_rect(rectangles[index]);
            if (native_damage.right > native_damage.left &&
                native_damage.bottom > native_damage.top) {
                InvalidateRect(hwnd_, &native_damage, FALSE);
            }
        }
        KillTimer(hwnd_, scheduler_timer);
        if (const std::optional<FrameTime> wake = (*model_).next_wake()) {
            const std::chrono::milliseconds::rep milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    *wake - FrameClock::now()).count();
            SetTimer(hwnd_, scheduler_timer,
                     static_cast<UINT>(std::clamp<std::int64_t>(milliseconds, 1, 60'000)), nullptr);
        }
        const bool needs_live_clock = (*model_).has_live_surface_presentations();
        if (needs_live_clock && !live_clock_armed_) {
            arm_live_clock();
        } else if (!needs_live_clock && live_clock_armed_) {
            CancelWaitableTimer(live_frame_clock_);
            live_clock_armed_ = false;
            live_frame_due_ = false;
            live_clock_deadline_ = 0;
        }
    }

    // Arms the live frame clock once, for the next deadline at a fixed phase
    // of the display's refresh: half a refresh period after the compositor's
    // vertical blank, as far as can be from the moment it samples window
    // content, so that each refresh takes exactly one live frame and none is
    // shown twice or skipped. A deadline is never less than half a period
    // after the previous one, so one refresh never fires twice. Without the
    // composition timing (no compositor, or a desktop it does not compose)
    // the deadlines keep a 60 Hz cadence from the previous one.
    void arm_live_clock() noexcept {
        live_clock_armed_ = false;
        if (live_frame_clock_ == nullptr || qpc_frequency_ <= 0) return;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        LONGLONG period = qpc_frequency_ * 1000 / live_clock_fallback_millihertz;
        LONGLONG anchor = live_clock_deadline_ != 0 ? live_clock_deadline_ : now.QuadPart;
        DWM_TIMING_INFO timing{};
        timing.cbSize = sizeof(timing);
        if (dwm_composition_timing_ != nullptr &&
            dwm_composition_timing_(nullptr, &timing) == S_OK &&
            timing.qpcRefreshPeriod > 0U && timing.qpcVBlank > 0U) {
            period = static_cast<LONGLONG>(timing.qpcRefreshPeriod);
            anchor = static_cast<LONGLONG>(timing.qpcVBlank) + period / 2;
        }
        LONGLONG earliest = now.QuadPart + 1;
        if (live_clock_deadline_ != 0) {
            earliest = (std::max)(earliest, live_clock_deadline_ + period / 2);
        }
        // The first deadline at or after `earliest` in the anchor's phase.
        LONGLONG offset = (earliest - anchor) % period;
        if (offset < 0) offset += period;
        const LONGLONG deadline = offset == 0 ? earliest : earliest + (period - offset);
        // Relative due times are in 100 ns units, negative.
        const LONGLONG hundreds = (deadline - now.QuadPart) * 10'000'000LL / qpc_frequency_;
        LARGE_INTEGER due{};
        due.QuadPart = -(std::max)(hundreds, static_cast<LONGLONG>(1));
        live_clock_armed_ = SetWaitableTimer(live_frame_clock_, &due, 0, nullptr, nullptr,
                                             FALSE) != FALSE;
        if (live_clock_armed_) live_clock_deadline_ = deadline;
    }

    void request_render_wake() noexcept {
        if (hwnd_ == nullptr || render_dispatch_pending_ ||
            presentation_pending_) return;
        render_dispatch_pending_ = true;
        if (!PostMessageW(hwnd_, render_dispatch_message, 0, 0)) {
            render_dispatch_pending_ = false;
        }
    }

#if !defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    // The frame sequence for new live frames (stage DP decision 2), in one
    // tick: gather the frames and the retained damage they caused (an
    // overlay's share above a changed frame, #69) with any damage still
    // pending; compose the frames, then the retained painting over them, into
    // the raster; present the combined rectangles once. An overlay is never a
    // wake behind the frame beneath it, and the screen stays a copy of the
    // raster.
    void present_live_frame_sequence(const std::vector<LiveSurfacePresentation>& updates) {
        const std::chrono::steady_clock::time_point started =
            std::chrono::steady_clock::now();
        const DamageRegion taken = (*model_).take_damage();
        const std::span<const Rect> taken_rectangles = taken.rectangles();
        for (std::size_t index = 0; index < taken_rectangles.size(); ++index) {
            pending_damage_.add(taken_rectangles[index]);
        }
        // Gather and compose. Each surface's frame is taken once, with the
        // region it changed since this host last drew it; the core splits a
        // clip around overlays, and every fragment draws from that one frame.
        // Where the placement is unchanged and the frame opaque, only the
        // changed region is drawn and copied: outside it the raster already
        // holds this frame's pixels (as on macOS).
        prepare_frame_alpha();
        static_cast<void>(raster_.synchronize_images((*model_).image_resources()));
        const bool frame_begun = raster_.begin_frame();
        const bool regions_usable = !live_region_lost_;
        DamageRegion screen;
        std::uint64_t live_pixels = 0U;
        bool regions_taken = false;
        live_frames_.clear();
        live_frame_sources_.clear();
        composed_live_pixels_.clear();
        composed_live_rectangles_.clear();
        for (std::size_t index = 0; index < updates.size(); ++index) {
            const gui_forms::LiveSurfacePresentation& update = updates[index];
            std::size_t source = live_frames_.size();
            for (std::size_t earlier = 0; earlier < index; ++earlier) {
                if (updates[earlier].surface == update.surface) {
                    source = live_frame_sources_[earlier];
                    break;
                }
            }
            if (source == live_frames_.size()) {
                LiveSurfaceFrame taken_frame{};
                if (update.surface) {
                    taken_frame = (*update.surface).acquire_for_presentation(this);
                    if (taken_frame) regions_taken = true;
                }
                live_frames_.push_back(std::move(taken_frame));
                composed_live_pixels_.push_back(0U);
                composed_live_rectangles_.push_back(0U);
            }
            live_frame_sources_.push_back(source);
            const LiveSurfaceFrame& frame = live_frames_[source];
            if (!frame) continue;
            Rect region = update.clip;
            if (regions_usable && update.damage_limited && frame.opaque()) {
                const Rect changed = gui_forms::detail::live_damage_in_window(
                    frame.damage(), update.destination, frame.width(), frame.height(), scale_);
                region = Rect::intersection(update.clip, changed);
            }
            if (region.empty() || !frame_begun) continue;
            if (!raster_.compose_live_surface_frame(frame, update.destination, region)) {
                ++live_updates_failed_;
                continue;
            }
            screen.add(region);
            const std::uint64_t pixels = device_pixels(device_rect(region));
            composed_live_pixels_[source] += pixels;
            ++composed_live_rectangles_[source];
            live_pixels += pixels;
        }
        std::uint64_t live_composed = 0U;
        for (std::size_t index = 0; index < composed_live_rectangles_.size(); ++index) {
            if (composed_live_rectangles_[index] != 0U) ++live_composed;
        }
        std::optional<PaintReceipt> receipt;
        if (frame_begun && !pending_damage_.empty()) {
            // Clip to the damaged rectangles, not their bounds, as on macOS.
            render::SkiaRaster& painter = raster_.painter();
            painter.save();
            painter.clip_damage(pending_damage_);
            receipt = (*model_).paint(painter, pending_damage_.rectangles());
            painter.restore();
            if (receipt) {
                const std::span<const Rect> painted = pending_damage_.rectangles();
                for (std::size_t index = 0; index < painted.size(); ++index) {
                    screen.add(painted[index]);
                }
            }
        }
        raster_.end_frame();
        // Present: each rectangle once, and none left for WM_PAINT to repeat.
        HDC target = GetDC(hwnd_);
        if (target == nullptr) {
            live_updates_failed_ += live_composed;
            return;
        }
        bool presented = true;
        PresentedArea copied;
        const std::chrono::steady_clock::time_point copy_started =
            std::chrono::steady_clock::now();
        const std::span<const Rect> presented_rectangles = screen.rectangles();
        for (std::size_t index = 0; index < presented_rectangles.size(); ++index) {
            const RECT device = device_rect(presented_rectangles[index]);
            const std::uint64_t pixels = device_pixels(device);
            if (pixels == 0U) continue;
            if (raster_.present(target, device)) {
                ValidateRect(hwnd_, &device);
                copied.device_pixels += pixels;
                ++copied.rectangles;
            } else {
                presented = false;
            }
        }
        ReleaseDC(hwnd_, target);
        copied.copy_nanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - copy_started).count());
        const std::uint64_t elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
        // A region taken from a surface is cleared; only a presented raster
        // keeps it valid, so after a failure the next tick copies whole clips.
        if (regions_taken) live_region_lost_ = !presented;
        // Live frames are copied inside this one present: their regions are
        // reported as live and the rest as retained, and the duration is
        // split between them by pixel share.
        PresentedArea retained;
        retained.device_pixels = copied.device_pixels > live_pixels ?
            copied.device_pixels - live_pixels : 0U;
        retained.rectangles = copied.rectangles;
        // A tick with a receipt reports the copy time with the retained
        // present; a live-only tick with its first live frame.
        if (receipt) retained.copy_nanoseconds = copied.copy_nanoseconds;
        std::uint64_t unreported_copy = receipt ? 0U : copied.copy_nanoseconds;
        const std::uint64_t all_pixels = retained.device_pixels + live_pixels;
        const std::uint64_t live_elapsed = all_pixels == 0U ? 0U :
            static_cast<std::uint64_t>(static_cast<double>(elapsed) *
                static_cast<double>(live_pixels) / static_cast<double>(all_pixels));
        if (presented && receipt &&
            (*model_).notify_presented(*receipt, elapsed - live_elapsed, retained)) {
            pending_damage_.clear();
        }
        // Retained damage the sequence painted with the frames, or left for a
        // later WM_PAINT (a declined paint or a failed copy).
        if (!pending_damage_.empty()) {
            ++live_sequence_retained_deferred_;
        } else if (receipt) {
            ++live_sequence_retained_presented_;
        }
        if (!presented) {
            live_updates_failed_ += live_composed;
            return;
        }
        live_updates_presented_ += live_composed;
        const std::uint64_t live_duration = live_composed == 0U ? 0U :
            (receipt ? live_elapsed : elapsed) / live_composed;
        // One report per surface frame; its fragments are its rectangles.
        for (std::size_t index = 0; index < composed_live_pixels_.size(); ++index) {
            if (composed_live_rectangles_[index] == 0U) continue;
            PresentedArea live;
            live.device_pixels = composed_live_pixels_[index];
            live.rectangles = composed_live_rectangles_[index];
            live.copy_nanoseconds = unreported_copy;
            unreported_copy = 0U;
            (*model_).notify_live_presented(live_duration, live);
        }
    }
#endif

    void present_live_surface_updates() {
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        // Placements are model-current. Never consume their generations while
        // the retained front belongs to an older layout, scale or revision.
        const PaintLeaseSnapshot lease = (*model_).paint_lease_snapshot();
        if (!pending_damage_.empty() || !raster_.matches_front(lease)) return;
#endif
        ++live_presentation_drains_;
        const std::chrono::steady_clock::time_point started =
            std::chrono::steady_clock::now();
        std::vector<LiveSurfacePresentation> updates =
            (*model_).take_live_surface_presentations();
        live_updates_sampled_ += updates.size();
        if (!(*model_).has_live_surface_presentations() && live_clock_armed_) {
            CancelWaitableTimer(live_frame_clock_);
            live_clock_armed_ = false;
            live_clock_deadline_ = 0;
        }
        if (updates.empty() || hwnd_ == nullptr) return;
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        HDC target = GetDC(hwnd_);
        if (target == nullptr) {
            live_updates_failed_ += updates.size();
            return;
        }
        for (const gui_forms::LiveSurfacePresentation& update : updates) {
            const std::chrono::steady_clock::time_point update_started =
                std::chrono::steady_clock::now();
            if (raster_.present_live_surface(target, update)) {
                ++live_updates_presented_;
                PresentedArea copied;
                copied.device_pixels = device_pixels(device_rect(update.clip));
                copied.rectangles = copied.device_pixels != 0U ? 1U : 0U;
                const std::uint64_t update_duration = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - update_started).count());
                (*model_).notify_live_presented(update_duration, copied);
            } else {
                ++live_updates_failed_;
                pending_damage_.add(update.clip);
                recovery_expose_pending_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        ReleaseDC(hwnd_, target);
#else
        present_live_frame_sequence(updates);
#endif
        const std::uint64_t duration = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
        live_present_duration_nanoseconds_ += duration;
        live_worst_present_duration_nanoseconds_ =
            (std::max)(live_worst_present_duration_nanoseconds_, duration);
    }

    void paint() {
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        const bool update_region_read = add_update_region_damage();
        PAINTSTRUCT paint_state{};
        HDC dc = BeginPaint(hwnd_, &paint_state);
        const PaintLeaseSnapshot before = (*model_).paint_lease_snapshot();
        if (recovery_expose_pending_ ||
            (pending_damage_.empty() && raster_.matches_front(before))) {
            recovery_expose_pending_ = false;
            static_cast<void>(raster_.present(dc, paint_state.rcPaint));
            EndPaint(hwnd_, &paint_state);
            return;
        }
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        // Windows exposes whole device pixels. Include its complete update
        // region even when logical damage exists, so all drawing operations
        // restore the same pixel edges and native exposures are not omitted.
        if (!update_region_read) pending_damage_.add(logical_rect(paint_state.rcPaint));
        // Present exactly the rectangles painted (stage DP partial present):
        // the candidate carries the front's pixels everywhere else.
        const std::span<const Rect> painted_rectangles = pending_damage_.rectangles();
        std::vector<RECT> present_rectangles;
        present_rectangles.reserve(painted_rectangles.size());
        for (std::size_t index = 0; index < painted_rectangles.size(); ++index) {
            const RECT device = device_rect(painted_rectangles[index]);
            if (device_pixels(device) != 0U) present_rectangles.push_back(device);
        }
        std::optional<PaintReceipt> receipt{};
        bool presented = false;
        std::uint64_t transactional_copy_nanoseconds = 0U;
        try {
            prepare_frame_alpha();
            const bool images_ready = raster_.synchronize_images((*model_).image_resources());
            const bool begun = images_ready && raster_.begin_frame(pending_damage_);
            if (begun) receipt = (*model_).paint(raster_.painter(), pending_damage_.rectangles());
            if (receipt) {
                const std::chrono::steady_clock::time_point copy_started =
                    std::chrono::steady_clock::now();
                presented = raster_.commit_frame(dc, present_rectangles, *receipt);
                transactional_copy_nanoseconds = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - copy_started).count());
            }
        } catch (...) {
            ++native_callback_faults_;
            std::fprintf(stderr, "gui-forms-host=transaction-paint-fault\n");
        }
        if (!presented) {
            raster_.abort_frame();
            static_cast<void>(raster_.present(dc, paint_state.rcPaint));
        }
        EndPaint(hwnd_, &paint_state);
        const std::chrono::nanoseconds elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started);
        if (presented) {
            const PaintLeaseSnapshot current = (*model_).paint_lease_snapshot();
            bool acknowledged = current.surface_epoch == (*receipt).surface_epoch &&
                current.presented_revision == (*receipt).rendered_revision;
            if (!acknowledged) {
                PresentedArea copied;
                copied.copy_nanoseconds = transactional_copy_nanoseconds;
                for (std::size_t index = 0; index < present_rectangles.size(); ++index) {
                    copied.device_pixels += device_pixels(present_rectangles[index]);
                    ++copied.rectangles;
                }
                acknowledged = (*model_).notify_presented(*receipt,
                    static_cast<std::uint64_t>(elapsed.count()), copied);
            }
            if (acknowledged) {
                pending_damage_.clear();
                complete_frame_capture_if_due(paint_state.rcPaint);
            }
        }
        if (!presented) {
            // One exposure-only recovery pass; retain damage until a later
            // application/native request instead of continuously retrying OOM.
            std::fprintf(stderr, "gui-forms-host=transaction-frame-not-presented\n");
            recovery_expose_pending_ = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        collect_damage();
#else
        const bool update_region_read = add_update_region_damage();
        PAINTSTRUCT paint_state{};
        HDC dc = BeginPaint(hwnd_, &paint_state);
        const std::chrono::steady_clock::time_point started =
            std::chrono::steady_clock::now();
        // Match the whole device pixels that the update region covers. A
        // narrower fractional clip can leave stripes from previous frames.
        if (!update_region_read) pending_damage_.add(logical_rect(paint_state.rcPaint));
        // Present exactly the rectangles painted: the raster keeps the last
        // frame everywhere else, and the window already shows it there.
        const std::span<const Rect> painted_rectangles = pending_damage_.rectangles();
        std::vector<RECT> present_rectangles;
        present_rectangles.reserve(painted_rectangles.size());
        for (std::size_t index = 0; index < painted_rectangles.size(); ++index) {
            present_rectangles.push_back(device_rect(painted_rectangles[index]));
        }
        prepare_frame_alpha();
        static_cast<void>(raster_.synchronize_images((*model_).image_resources()));
        std::optional<PaintReceipt> receipt{};
        if (raster_.begin_frame(pending_damage_)) {
            receipt = (*model_).paint(raster_.painter(), pending_damage_.rectangles());
            raster_.end_frame();
        }
        bool presented = receipt.has_value();
        const std::chrono::steady_clock::time_point copy_started =
            std::chrono::steady_clock::now();
        for (std::size_t index = 0; presented && index < present_rectangles.size(); ++index) {
            presented = raster_.present(dc, present_rectangles[index]);
        }
        const std::uint64_t copy_nanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - copy_started).count());
        EndPaint(hwnd_, &paint_state);
        const std::chrono::nanoseconds elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started);
        PresentedArea copied;
        copied.copy_nanoseconds = copy_nanoseconds;
        for (std::size_t index = 0; index < present_rectangles.size(); ++index) {
            const std::uint64_t pixels = device_pixels(present_rectangles[index]);
            copied.device_pixels += pixels;
            if (pixels != 0U) ++copied.rectangles;
        }
        if (presented &&
            (*model_).notify_presented(*receipt,
                                     static_cast<std::uint64_t>(elapsed.count()), copied)) {
            pending_damage_.clear();
            complete_frame_capture_if_due(paint_state.rcPaint);
        }
        collect_damage();
#endif
    }

    Point client_point(LPARAM lparam) const noexcept {
        return {GET_X_LPARAM(lparam) / scale_, GET_Y_LPARAM(lparam) / scale_};
    }

    void pointer(UINT message, PointerAction action, WPARAM wparam, LPARAM lparam) {
        PointerEvent event;
        event.action = action;
        event.button = pointer_button(message, wparam);
        event.position = client_point(lparam);
        event.modifiers = modifiers();
        event.pointer_id = 1;
        event.click_count = message == WM_LBUTTONDBLCLK ||
            message == WM_RBUTTONDBLCLK || message == WM_MBUTTONDBLCLK ? 2U : 1U;
        if (trace_win32_input()) {
            const Control::Ptr target = (*model_).hit_test(event.position);
            const std::string_view target_id = target ? (*target).stable_id().value() : std::string_view{"<none>"};
            const TracedStableId traced = traced_stable_id(target_id);
            if (traced.cut_from_bytes != 0) {
                std::fprintf(stderr,
                             "win32-input=pointer|message:%u|action:%u|x:%.2f|y:%.2f|target:%.*s|target_cut_from:%zu|"
                             "active:%d|focus:%d|capture:%d\n",
                             static_cast<unsigned>(message), static_cast<unsigned>(action),
                             event.position.x, event.position.y,
                             traced.printed_bytes, target_id.data(), traced.cut_from_bytes,
                             GetActiveWindow() == hwnd_, GetFocus() == hwnd_, GetCapture() == hwnd_);
            } else {
                std::fprintf(stderr,
                             "win32-input=pointer|message:%u|action:%u|x:%.2f|y:%.2f|target:%.*s|active:%d|focus:%d|capture:%d\n",
                             static_cast<unsigned>(message), static_cast<unsigned>(action),
                             event.position.x, event.position.y,
                             traced.printed_bytes, target_id.data(),
                             GetActiveWindow() == hwnd_, GetFocus() == hwnd_, GetCapture() == hwnd_);
            }
            std::fflush(stderr);
        }
        dispatch(std::move(event));
        synchronize_capture();
        if (!update_cursor(client_point(lparam))) apply_system_cursor(CursorKind::arrow);
        collect_damage();
    }

    void wheel(WPARAM wparam, LPARAM lparam) {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        PointerEvent event;
        event.action = PointerAction::wheel;
        event.position = {point.x / scale_, point.y / scale_};
        event.wheel_delta = {0.0, GET_WHEEL_DELTA_WPARAM(wparam) / static_cast<double>(WHEEL_DELTA)};
        event.modifiers = modifiers();
        event.pointer_id = 1;
        dispatch(std::move(event));
        collect_damage();
    }

    void key(KeyAction action, WPARAM wparam, LPARAM lparam) {
        KeyEvent event;
        event.action = action;
        event.physical_key = detail::physical_key_from_virtual_key(wparam);
        event.modifiers = modifiers();
        event.repeat = action == KeyAction::down && (lparam & (1LL << 30)) != 0;
        dispatch(std::move(event));
        collect_damage();
    }

    void character(wchar_t character) {
        // Key events own editing commands. TranslateMessage also emits these
        // WM_CHAR values; forwarding them would insert a second newline/tab or
        // a literal control byte after the command has already been handled.
        if (character < 0x20 || character == 0x7F) {
            pending_high_surrogate_ = 0;
            return;
        }
        if (character >= 0xD800 && character <= 0xDBFF) {
            pending_high_surrogate_ = character;
            return;
        }
        std::wstring text;
        if (pending_high_surrogate_ != 0) {
            if (character >= 0xDC00 && character <= 0xDFFF) text.push_back(pending_high_surrogate_);
            pending_high_surrogate_ = 0;
        }
        text.push_back(character);
        TextInputEvent event;
        event.text_utf8 = utf8_from_wide(text);
        if (!event.text_utf8.empty()) dispatch(std::move(event));
        collect_damage();
    }

    void synchronize_capture() {
        const bool requested = (*model_).captured_control() != nullptr;
        if (requested && GetCapture() != hwnd_) SetCapture(hwnd_);
        if (!requested && GetCapture() == hwnd_) ReleaseCapture();
    }

    bool update_cursor(Point position) {
        const bool hidden = services_.maintain_hidden_cursor();
        if (hidden) { return true; }
        const Control::Ptr captured = (*model_).captured_control();
        const Control::Ptr target = captured ? captured : (*model_).hit_test(position);
        const CursorKind cursor = target ? (*target).effective_cursor() : CursorKind::arrow;
        // The class cursor is authoritative for the ordinary pointer. Let
        // DefWindowProc complete WM_SETCURSOR for that case instead of writing
        // process-global cursor state again from every WM_MOUSEMOVE turn.
        const CursorImagesPtr images = target ? (*target).effective_cursor_images() : CursorImagesPtr{};
        if (cursor == CursorKind::arrow && !images) return false;
        return services_.set_custom_cursor(images, scale_, cursor).accepted();
    }

    void minimum_size(MINMAXINFO* info) const {
        if (info == nullptr) return;
        RECT rect{0, 0,
                  static_cast<LONG>(std::ceil(options_.minimum_size.width * scale_)),
                  static_cast<LONG>(std::ceil(options_.minimum_size.height * scale_))};
        adjust_client_frame(rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)),
            static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), scale_);
        (*info).ptMinTrackSize.x = rect.right - rect.left;
        (*info).ptMinTrackSize.y = rect.bottom - rect.top;
    }

    LRESULT close() {
        HostCloseRequest request{HostCloseReason::user, false, options_.hide_on_close};
        if (options_.close_request) options_.close_request(request);
        request.hide_on_accept = options_.hide_on_close;
        const HostDispatchResult result = dispatch(request);
        if (result.accepted() && result.close_allowed) {
            if (options_.hide_on_close) {
                hide_window();
                hidden_on_close_to_run();
                return 0;
            }
            native_phase_ = NativePhase::closing;
            DestroyWindow(hwnd_);
        }
        return 0;
    }

    LRESULT automation(const COPYDATASTRUCT* data) {
        if (!options_.automation_enabled || data == nullptr || (*data).lpData == nullptr ||
            (*data).cbData == 0 || (*data).cbData > maximum_automation_command) return FALSE;
        const char* bytes = static_cast<const char*>((*data).lpData);
        std::string command(bytes, bytes + (*data).cbData);
        while (!command.empty() && command.back() == '\0') command.pop_back();
        constexpr std::string_view prefix = "GUI.Forms.Automation/1 ";
        if (!command.starts_with(prefix)) return FALSE;
        command.erase(0, prefix.size());
        if (command == "snapshot") {
            const DispatcherSnapshot dispatcher = (*model_).dispatcher_snapshot();
            std::fprintf(
                stdout,
                "{\"automation\":\"snapshot\",\"window\":%s,\"host\":%s,"
                "\"dispatcher\":{\"posted\":%llu,\"invoked\":%llu,"
                "\"cancelled\":%llu,\"faulted\":%llu,"
                "\"synchronous_invocations\":%llu,\"inline_invocations\":%llu,"
                "\"marshalled_invocations\":%llu,\"pending\":%zu,"
                "\"maximum_pending\":%zu,\"accepting\":%s}}\n",
                metrics_json().c_str(), host_json().c_str(),
                static_cast<unsigned long long>(dispatcher.posted),
                static_cast<unsigned long long>(dispatcher.invoked),
                static_cast<unsigned long long>(dispatcher.cancelled),
                static_cast<unsigned long long>(dispatcher.faulted),
                static_cast<unsigned long long>(dispatcher.synchronous_invocations),
                static_cast<unsigned long long>(dispatcher.inline_invocations),
                static_cast<unsigned long long>(dispatcher.marshalled_invocations),
                dispatcher.pending, dispatcher.maximum_pending,
                dispatcher.accepting ? "true" : "false");
            std::fflush(stdout);
            return TRUE;
        }
        constexpr std::string_view capture = "capture";
        if (command == capture || command.starts_with("capture ")) {
            const std::wstring path = command.size() == capture.size()
                ? executable_directory() + L"gallery-automation.bmp"
                : wide_from_utf8(command.substr(capture.size() + 1U));
            if (path.empty()) return FALSE;
            const bool saved = raster_.save_bmp(path);
            std::fprintf(stdout,
                         "{\"automation\":\"capture\",\"saved\":%s,\"explicit\":%s}\n",
                         saved ? "true" : "false",
                         command.size() == capture.size() ? "false" : "true");
            std::fflush(stdout);
            return saved ? TRUE : FALSE;
        }
        if (command == "close") { PostMessageW(hwnd_, WM_CLOSE, 0, 0); return TRUE; }
        constexpr std::string_view move_at = "move ";
        if (command.starts_with(move_at)) {
            std::istringstream input(command.substr(move_at.size()));
            std::string id;
            double local_x{};
            double local_y{};
            if (!(input >> id >> local_x >> local_y) || !std::isfinite(local_x) ||
                !std::isfinite(local_y)) return FALSE;
            const std::shared_ptr<gui_forms::Control> control = options_.automation_resolve
                ? options_.automation_resolve(id) : (*model_).find(id);
            if (!control || !(*control).eligible_for_input()) return FALSE;
            const Rect bounds = (*control).absolute_bounds();
            const Point point{bounds.x + std::clamp(local_x, 0.0, bounds.width),
                              bounds.y + std::clamp(local_y, 0.0, bounds.height)};
            const bool handled = dispatch(PointerEvent{
                PointerAction::move, PointerButton::none, point, {},
                Modifier::none, 1}).handled;
            collect_damage();
            std::fprintf(stdout,
                         "{\"automation\":\"move\",\"id\":\"%s\",\"handled\":%s}\n",
                         id.c_str(), handled ? "true" : "false");
            std::fflush(stdout);
            return TRUE;
        }
        constexpr std::string_view focus = "focus ";
        if (command.starts_with(focus)) {
            const std::string id = command.substr(focus.size());
            const std::shared_ptr<gui_forms::Control> control = options_.automation_resolve
                ? options_.automation_resolve(id) : (*model_).find(id);
            if (!control || !(*control).eligible_for_input() ||
                !(*model_).request_focus(control)) return FALSE;
            std::fprintf(stdout, "{\"automation\":\"focus\",\"id\":\"%s\"}\n",
                         id.c_str());
            std::fflush(stdout);
            return TRUE;
        }
        constexpr std::string_view text_input = "text ";
        if (command.starts_with(text_input)) {
            const std::string payload = command.substr(text_input.size());
            const std::size_t separator = payload.find(' ');
            if (separator == std::string::npos) return FALSE;
            const std::string id = payload.substr(0, separator);
            const std::string text = payload.substr(separator + 1U);
            const std::shared_ptr<gui_forms::Control> control = options_.automation_resolve
                ? options_.automation_resolve(id) : (*model_).find(id);
            if (!control || !(*control).eligible_for_input() ||
                !(*model_).request_focus(control)) return FALSE;
            const bool handled = dispatch(TextInputEvent{text}).handled;
            collect_damage();
            std::fprintf(stdout,
                         "{\"automation\":\"text\",\"id\":\"%s\",\"bytes\":%zu,\"handled\":%s}\n",
                         id.c_str(), text.size(), handled ? "true" : "false");
            std::fflush(stdout);
            return TRUE;
        }
        constexpr std::string_view key_input = "key ";
        if (command.starts_with(key_input)) {
            std::istringstream input(command.substr(key_input.size()));
            std::string id;
            std::uint32_t physical{};
            std::string action;
            std::uint32_t modifiers{};
            if (!(input >> id >> physical >> action) ||
                (action != "down" && action != "up")) return FALSE;
            if (!(input >> modifiers)) modifiers = 0;
            const std::shared_ptr<gui_forms::Control> control = options_.automation_resolve
                ? options_.automation_resolve(id) : (*model_).find(id);
            if (!control || !(*control).eligible_for_input() ||
                !(*model_).request_focus(control)) return FALSE;
            KeyEvent event;
            event.action = action == "down" ? KeyAction::down : KeyAction::up;
            event.physical_key = physical;
            event.modifiers = static_cast<Modifier>(modifiers);
            const bool handled = dispatch(std::move(event)).handled;
            collect_damage();
            std::fprintf(stdout,
                         "{\"automation\":\"key\",\"id\":\"%s\",\"physical\":%u,\"action\":\"%s\",\"modifiers\":%u,\"handled\":%s}\n",
                         id.c_str(), physical, action.c_str(), modifiers,
                         handled ? "true" : "false");
            std::fflush(stdout);
            return TRUE;
        }
        constexpr std::string_view activate = "activate ";
        if (command.starts_with(activate)) {
            const std::string id = command.substr(activate.size());
            const std::shared_ptr<gui_forms::Control> control = options_.automation_resolve
                ? options_.automation_resolve(id) : (*model_).find(id);
            if (!control || !(*control).eligible_for_input()) {
                std::fprintf(stdout,
                             "{\"automation\":\"activate\",\"id\":\"%s\",\"accepted\":false,\"reason\":\"%s\"}\n",
                             id.c_str(), !control ? "not-found" : "ineligible");
                std::fflush(stdout);
                return FALSE;
            }
            (*control).on_activate();
            collect_damage();
            std::fprintf(stdout,
                         "{\"automation\":\"activate\",\"id\":\"%s\",\"stable_id\":\"%s\"}\n",
                         id.c_str(), (*control).stable_id().value().data());
            std::fflush(stdout);
            return TRUE;
        }
        constexpr std::string_view click_at = "click-at ";
        if (command.starts_with(click_at)) {
            std::istringstream input(command.substr(click_at.size()));
            std::string id;
            double local_x{};
            double local_y{};
            if (!(input >> id >> local_x >> local_y) || !std::isfinite(local_x) ||
                !std::isfinite(local_y)) return FALSE;
            const std::shared_ptr<gui_forms::Control> control = options_.automation_resolve
                ? options_.automation_resolve(id) : (*model_).find(id);
            if (!control || !(*control).eligible_for_input()) {
                std::fprintf(stdout,
                             "{\"automation\":\"click-at\",\"id\":\"%s\",\"accepted\":false,\"reason\":\"%s\"}\n",
                             id.c_str(), !control ? "not-found" : "ineligible");
                std::fflush(stdout);
                return FALSE;
            }
            const Rect bounds = (*control).absolute_bounds();
            const Point point{bounds.x + std::clamp(local_x, 0.0, bounds.width),
                              bounds.y + std::clamp(local_y, 0.0, bounds.height)};
            const bool handled_down = dispatch(PointerEvent{
                PointerAction::down, PointerButton::primary, point, {},
                Modifier::none, 1}).handled;
            const bool handled_up = dispatch(PointerEvent{
                PointerAction::up, PointerButton::primary, point, {},
                Modifier::none, 1}).handled;
            synchronize_capture();
            collect_damage();
            std::fprintf(stdout,
                         "{\"automation\":\"click-at\",\"id\":\"%s\",\"local_x\":%.3f,\"local_y\":%.3f,\"handled\":%s}\n",
                         id.c_str(), local_x, local_y,
                         (handled_down || handled_up) ? "true" : "false");
            std::fflush(stdout);
            return TRUE;
        }
        constexpr std::string_view drag = "drag ";
        if (command.starts_with(drag)) {
            std::istringstream input(command.substr(drag.size()));
            std::string id;
            double start_x{};
            double start_y{};
            double end_x{};
            double end_y{};
            unsigned requested_steps{8U};
            if (!(input >> id >> start_x >> start_y >> end_x >> end_y) ||
                !std::isfinite(start_x) || !std::isfinite(start_y) ||
                !std::isfinite(end_x) || !std::isfinite(end_y)) return FALSE;
            if (!(input >> requested_steps)) requested_steps = 8U;
            const unsigned steps = std::clamp(requested_steps, 1U, 256U);
            const std::shared_ptr<gui_forms::Control> control = options_.automation_resolve
                ? options_.automation_resolve(id) : (*model_).find(id);
            if (!control || !(*control).eligible_for_input()) {
                std::fprintf(stdout,
                             "{\"automation\":\"drag\",\"id\":\"%s\",\"accepted\":false,\"reason\":\"%s\"}\n",
                             id.c_str(), !control ? "not-found" : "ineligible");
                std::fflush(stdout);
                return FALSE;
            }
            const Rect bounds = (*control).absolute_bounds();
            const Point start{
                bounds.x + std::clamp(start_x, 0.0, bounds.width),
                bounds.y + std::clamp(start_y, 0.0, bounds.height)};
            const Point end{
                bounds.x + std::clamp(end_x, 0.0, bounds.width),
                bounds.y + std::clamp(end_y, 0.0, bounds.height)};
            bool handled = dispatch(PointerEvent{
                PointerAction::down, PointerButton::primary, start, {},
                Modifier::none, 1}).handled;
            for (unsigned index = 1; index <= steps; ++index) {
                const double ratio = static_cast<double>(index) /
                                     static_cast<double>(steps);
                const Point position{start.x + (end.x - start.x) * ratio,
                                     start.y + (end.y - start.y) * ratio};
                handled = dispatch(PointerEvent{
                    PointerAction::move, PointerButton::primary, position, {},
                    Modifier::none, 1}).handled || handled;
            }
            handled = dispatch(PointerEvent{
                PointerAction::up, PointerButton::primary, end, {},
                Modifier::none, 1}).handled || handled;
            synchronize_capture();
            collect_damage();
            std::fprintf(stdout,
                         "{\"automation\":\"drag\",\"id\":\"%s\",\"steps\":%u,\"handled\":%s}\n",
                         id.c_str(), steps, handled ? "true" : "false");
            std::fflush(stdout);
            return TRUE;
        }
        constexpr std::string_view click = "click ";
        if (command.starts_with(click)) {
            const std::string id = command.substr(click.size());
            std::fprintf(stdout, "{\"automation\":\"click-phase\",\"id\":\"%s\",\"phase\":\"request\"}\n",
                         id.c_str());
            std::fflush(stdout);
            const std::shared_ptr<gui_forms::Control> control = options_.automation_resolve
                ? options_.automation_resolve(id) : (*model_).find(id);
            if (!control || !(*control).eligible_for_input()) return FALSE;
            std::fprintf(stdout, "{\"automation\":\"click-phase\",\"id\":\"%s\",\"phase\":\"resolved\"}\n",
                         id.c_str());
            std::fflush(stdout);
            const Rect bounds = (*control).absolute_bounds();
            const Point center{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0};
            const Control::Ptr hit = (*model_).hit_test(center);
            std::fprintf(stdout,
                         "{\"automation\":\"click-target\",\"id\":\"%s\",\"resolved\":\"%s\",\"hit\":\"%s\",\"x\":%.3f,\"y\":%.3f}\n",
                         id.c_str(), (*control).stable_id().value().data(),
                         hit ? (*hit).stable_id().value().data() : "", center.x,
                         center.y);
            std::fflush(stdout);
            PointerEvent down{PointerAction::down, PointerButton::primary, center,
                              {}, Modifier::none, 1};
            PointerEvent up{PointerAction::up, PointerButton::primary, center,
                            {}, Modifier::none, 1};
            const bool handled_down = dispatch(std::move(down)).handled;
            std::fprintf(stdout, "{\"automation\":\"click-phase\",\"id\":\"%s\",\"phase\":\"down\"}\n",
                         id.c_str());
            std::fflush(stdout);
            const bool handled_up = dispatch(std::move(up)).handled;
            std::fprintf(stdout, "{\"automation\":\"click-phase\",\"id\":\"%s\",\"phase\":\"up\"}\n",
                         id.c_str());
            std::fflush(stdout);
            synchronize_capture();
            collect_damage();
            std::fprintf(stdout, "{\"automation\":\"click\",\"id\":\"%s\",\"handled\":%s}\n",
                         id.c_str(), (handled_down || handled_up) ? "true" : "false");
            std::fflush(stdout);
            return TRUE;
        }
        return FALSE;
    }

    static std::wstring executable_directory() {
        wchar_t executable[MAX_PATH]{};
        const DWORD count = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        if (count == 0 || count >= MAX_PATH) return {};
        std::wstring directory(executable, count);
        const std::wstring::size_type slash = directory.find_last_of(L"\\/");
        directory.resize(slash == std::wstring::npos ? 0 : slash + 1);
        return directory;
    }

    enum class NativePhase : std::uint8_t {
        creating,
        bound,
        attached,
        ready,
        visible,
        closing,
        closed,
        shutdown,
    };

    WindowsAccessibility accessibility_;
    std::unique_ptr<Window> model_;
    WindowsHostOptions options_;
    WindowsHostServices services_;
    HostSession session_;
    HWND hwnd_{};
    bool full_screen_{};
    LONG_PTR restore_style_{};
    WINDOWPLACEMENT restore_placement_{};
    WindowsRaster raster_;
    DamageRegion pending_damage_;
    // The frame sequence's per-tick storage, reused across ticks: each
    // surface's frame, the frame each presentation draws from, and the device
    // pixels and rectangles composed per frame.
    std::vector<LiveSurfaceFrame> live_frames_;
    std::vector<std::size_t> live_frame_sources_;
    std::vector<std::uint64_t> composed_live_pixels_;
    std::vector<std::uint64_t> composed_live_rectangles_;
    // A taken presentation region that was never presented: the next tick
    // copies whole clips.
    bool live_region_lost_{};
    // The last compositor state report's generation; 0 before the first.
    CompositorGeneration compositor_generation_{};
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    bool recovery_expose_pending_{};
#endif
    double scale_{1.0};
    std::uint64_t next_sequence_{1};
    // The double-click interval last reported; 0 before the first report.
    std::uint32_t reported_double_click_milliseconds_{};
    std::uint64_t native_callback_faults_{};
    wchar_t pending_high_surrogate_{};
    bool render_dispatch_pending_{};
    bool presentation_pending_{};
    HANDLE live_frame_clock_{};
    bool live_clock_armed_{};
    // The live clock's armed deadline in QueryPerformanceCounter ticks (0
    // when it is not armed), the counter's frequency, and the compositor's
    // timing query (null without dwmapi).
    LONGLONG live_clock_deadline_{};
    LONGLONG qpc_frequency_{};
    DwmCompositionTimingFunction dwm_composition_timing_{};
    bool live_frame_due_{};
    std::uint64_t live_clock_signals_{};
    std::uint64_t live_presentation_drains_{};
    std::uint64_t live_updates_sampled_{};
    // Frame-sequence ticks whose retained damage went out with the live frames,
    // and ticks that left it for a later WM_PAINT (stage DP).
    std::uint64_t live_sequence_retained_presented_{};
    std::uint64_t live_sequence_retained_deferred_{};
    std::uint64_t live_updates_presented_{};
    std::uint64_t live_updates_failed_{};
    std::uint64_t live_present_duration_nanoseconds_{};
    std::uint64_t live_worst_present_duration_nanoseconds_{};
    bool in_size_move_{};
    std::shared_ptr<TitleCallbackState> title_state_{std::make_shared<TitleCallbackState>()};
    std::shared_ptr<ClientSizeCallbackState> size_state_{std::make_shared<ClientSizeCallbackState>()};
    bool closed_{};
    bool frame_capture_completed_{};
    bool frame_capture_saved_{};
    NativePhase native_phase_{NativePhase::creating};
    TooltipPopup tooltip_;
    // The application run this window belongs to (observing; the run owns
    // this state) and the window's identity in it. Null for run_windows.
    WindowsApplicationRun* run_{};
    HostWindowId window_id_{};
    // The reason WM_DESTROY reports: user for every native close, application
    // when the run destroys the window (HostWindowing::destroy_window).
    HostCloseReason destroy_reason_{HostCloseReason::user};
};

HostServiceStatus RequestClientSize::operator()(const Size client_size) const {
    const std::shared_ptr<ClientSizeCallbackState> state = owner.lock();
    if (!state) return {HostServiceError::after_shutdown};
    if ((*state).thread != GetCurrentThreadId()) return {HostServiceError::wrong_thread};
    if ((*state).host == nullptr) return {HostServiceError::after_shutdown};
    if (!std::isfinite(client_size.width) || !std::isfinite(client_size.height) ||
        client_size.width < 1.0 || client_size.height < 1.0 ||
        client_size.width > 32768.0 || client_size.height > 32768.0) {
        return {HostServiceError::invalid_argument};
    }
    try {
        const HostServiceStatus result = (*(*state).host).request_client_size(client_size);
        return result;
    } catch (...) { return {HostServiceError::backend_failure}; }
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    WindowsHostState* state = reinterpret_cast<WindowsHostState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create =
            reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<WindowsHostState*>((*create).lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        (*state).bind_window(window);
    }
    if (state != nullptr) return (*state).message(message, wparam, lparam);
    return DefWindowProcW(window, message, wparam, lparam);
}

#include "windows_application_run.inc"

#if defined(GUI_FORMS_DIB_LIFECYCLE_TEST)
#include "windows_dib_lifecycle_fixture.inc"
#endif
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT_TEST)
#include "windows_prepared_text_fixture.inc"
#endif
#if defined(GUI_FORMS_WINDOWS_TOOLTIP_TEST)
#include "windows_tooltip_fixture.inc"
#endif

} // namespace

#if defined(GUI_FORMS_DIB_LIFECYCLE_TEST)
void run_windows_dib_lifecycle_fixture() {
    DibHostFixture::run();
}
#endif
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT_TEST)
void run_windows_prepared_text_fixture(const std::span<const std::byte> fonts) { PreparedHostFixture::run(fonts); }
#endif
#if defined(GUI_FORMS_WINDOWS_TOOLTIP_TEST)
void run_windows_tooltip_fixture(const std::filesystem::path& fonts) {
    TooltipHostFixture::run(fonts);
}
#endif

// accessibility: MSAA and UI Automation publication of the semantic tree,
// including the TextPattern, verified by the MSAA and UI Automation client
// tests over the Gallery (stage A).
HostCapabilities windows_capabilities() {
    return {HostCapabilities::current_protocol_version,
            "win32-dib",
            HostCapability::lifecycle |
                HostCapability::accessibility |
                HostCapability::frame_capture |
                HostCapability::scale_notifications |
                HostCapability::scheduled_wake |
                HostCapability::pointer_input |
                HostCapability::keyboard_input |
                HostCapability::text_composition |
                HostCapability::monitor_geometry |
                HostCapability::pointer_capture |
                HostCapability::cursor |
                HostCapability::clipboard |
                HostCapability::clipboard_images |
                HostCapability::dialogs |
                HostCapability::sound_cues |
                HostCapability::notification_area |
                HostCapability::notification_area_messages |
                HostCapability::notification_area_menus |
                HostCapability::runtime_windows |
                HostCapability::window_frames |
                HostCapability::screen_pointer |
                HostCapability::window_at_point |
                HostCapability::window_ordering |
                HostCapability::transparent_windows |
                HostCapability::window_backdrops |
                HostCapability::compositor_notifications |
                HostCapability::presentation_preferences,
            // Windows follows the PC keyboard convention (Ctrl+F4 closes a
            // document).
            pc_key_map()};
}

int run_windows(std::unique_ptr<Window> model, WindowsHostOptions options) {
    if (!model || !options.initially_visible || options.hide_on_close) return 2;
    const HRESULT com_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    enable_best_dpi_awareness();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW native_class{};
    native_class.cbSize = sizeof(native_class);
    native_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    native_class.lpfnWndProc = window_procedure;
    native_class.hInstance = instance;
    native_class.hCursor = load_system_cursor(IDC_ARROW);
    native_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    native_class.hbrBackground = nullptr;
    native_class.lpszClassName = window_class_name;
    if (RegisterClassExW(&native_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (SUCCEEDED(com_status)) CoUninitialize();
        return 3;
    }

    WindowsHostState state(std::move(model), options);
    const DWORD style = options.popup_window ? (WS_POPUP | WS_BORDER) :
        (options.minimizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX));
    const double initial_scale = query_scale(nullptr);
    const Size initial_size = options.capture_size.has_value()
        ? *options.capture_size : options.initial_size;
    RECT frame{0, 0, static_cast<LONG>(std::ceil(initial_size.width * initial_scale)),
               static_cast<LONG>(std::ceil(initial_size.height * initial_scale))};
    adjust_client_frame(frame, style, 0, initial_scale);
    const std::wstring title = wide_from_utf8(options.title);
    const int initial_x = options.popup_window
        ? static_cast<int>(std::lround(options.initial_position.x)) : CW_USEDEFAULT;
    const int initial_y = options.popup_window
        ? static_cast<int>(std::lround(options.initial_position.y)) : CW_USEDEFAULT;
    HWND window = CreateWindowExW(0, window_class_name, title.c_str(),
                                  style, initial_x, initial_y,
                                  frame.right - frame.left, frame.bottom - frame.top,
                                  nullptr, nullptr, instance, &state);
    if (window == nullptr) {
        if (SUCCEEDED(com_status)) CoUninitialize();
        return 4;
    }
    if (!state.initialize(window)) {
        DestroyWindow(window);
        if (SUCCEEDED(com_status)) CoUninitialize();
        return 5;
    }
    if (!state.will_show()) {
        DestroyWindow(window);
        if (SUCCEEDED(com_status)) CoUninitialize();
        return 5;
    }
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    if (options.close_after_launch_for_testing) {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }

    MSG message{};
    bool quit{};
    while (!quit) {
        HANDLE clock = state.live_frame_clock();
        const DWORD handle_count = clock == nullptr ? 0U : 1U;
        const DWORD wait = MsgWaitForMultipleObjectsEx(
            handle_count, handle_count == 0U ? nullptr : &clock, INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_FAILED) break;
        if (handle_count != 0U && wait == WAIT_OBJECT_0) {
            state.mark_live_frame_due();
        }

        // Input and managed callbacks receive a bounded turn, not an
        // unbounded queue drain. Presentation therefore has a maximum queue
        // latency even while pointer or plugin traffic remains continuous.
        const std::chrono::steady_clock::time_point batch_started =
            std::chrono::steady_clock::now();
        for (std::size_t drained = 0U; drained < 32U; ++drained) {
            MSG pending{};
            if (!detail::take_window_message(pending, drained % 2U == 0U)) break;
            if (pending.message == WM_QUIT) {
                message = pending;
                quit = true;
                break;
            }
            TranslateMessage(&pending);
            DispatchMessageW(&pending);
            if (!options.quit_thread_on_close && !IsWindow(window)) {
                quit = true;
                break;
            }
            // Finish an input/ordinary pair before applying the time budget.
            if (drained % 2U == 1U && std::chrono::steady_clock::now() - batch_started >=
                std::chrono::milliseconds(4)) {
                break;
            }
        }
        if (clock != nullptr &&
            WaitForSingleObject(clock, 0U) == WAIT_OBJECT_0) {
            state.mark_live_frame_due();
        }
        if (!quit && IsWindow(window)) state.present_pending_frame();
        if (!options.quit_thread_on_close && !IsWindow(window)) break;
    }
    const std::string metrics = state.metrics_json();
    const std::string host = state.host_json();
    if (options.print_metrics_on_close) {
        std::fprintf(stdout, "{\"window\":%s,\"host\":%s}\n",
                     metrics.c_str(), host.c_str());
        std::fflush(stdout);
    }
    if (options.final_snapshot) options.final_snapshot(metrics, host);
    if (SUCCEEDED(com_status)) CoUninitialize();
    // Portable contract: a requested capture that was never saved fails the
    // process even when the window closed normally.
    if (!options.capture_path.empty() && !state.frame_capture_saved()) return 6;
    return static_cast<int>(message.wParam);
}

int run_windows_application(std::vector<WindowsApplicationWindow> windows,
                            WindowsApplicationOptions application) {
    if (windows.empty() || windows.size() > HostWindowing::maximum_windows) return 2;
    // The run holds the bundled font set, so windows opened and closed during it share one
    // copy of each face.
    const std::filesystem::path font_directory = bundled_font_directory();
    std::shared_ptr<gui_forms::detail::BundledFontSet> run_fonts;
    if (!font_directory.empty())
        run_fonts = gui_forms::detail::shared_bundled_font_set(font_directory, windows_sealed_pages());
    std::unordered_set<std::string> identities;
    std::size_t primary_count{};
    for (const WindowsApplicationWindow& entry : windows) {
        if (!entry.model || entry.stable_id.empty() ||
            !identities.insert(entry.stable_id).second ||
            (!entry.owner_id.empty() && entry.owner_id == entry.stable_id)) {
            return 2;
        }
        if (entry.owner_id.empty() && !entry.tool_window) {
            if (!entry.options.initially_visible || entry.options.hide_on_close) return 2;
            ++primary_count;
        }
    }
    for (const WindowsApplicationWindow& entry : windows) {
        if (!entry.owner_id.empty() && !identities.contains(entry.owner_id)) return 2;
    }
    if (primary_count != 1U) return 2;

    const HRESULT com_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    enable_best_dpi_awareness();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW native_class{};
    native_class.cbSize = sizeof(native_class);
    native_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    native_class.lpfnWndProc = window_procedure;
    native_class.hInstance = instance;
    native_class.hCursor = load_system_cursor(IDC_ARROW);
    native_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    native_class.hbrBackground = nullptr;
    native_class.lpszClassName = window_class_name;
    if (RegisterClassExW(&native_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (SUCCEEDED(com_status)) CoUninitialize();
        return 3;
    }

    // The run outlives every window state it owns; the open operation it
    // publishes is revoked (end_run_windowing) before the run is released.
    std::unique_ptr<WindowsApplicationRun> run = std::make_unique<WindowsApplicationRun>(instance);
    const std::shared_ptr<WindowsRunOpenState> open_state = std::make_shared<WindowsRunOpenState>();
    (*open_state).run = run.get();
    const bool windowing_published = static_cast<bool>(application.windowing_ready);
    if (windowing_published) application.windowing_ready(*run, WindowsRunOpen{open_state});

    // Owners are created before the windows they own. A remaining ownership
    // cycle is invalid even when a separate primary exists; it is rejected
    // before the message loop starts.
    std::vector<bool> created(windows.size(), false);
    // Each listed entry's identity; the entries' ids are moved out on open.
    std::vector<HostWindowId> listed_ids(windows.size());
    std::size_t created_count{};
    int failure{};
    while (created_count < windows.size() && failure == 0) {
        bool made_progress{};
        for (std::size_t index = 0; index < windows.size() && failure == 0; ++index) {
            if (created[index]) continue;
            WindowsApplicationWindow& entry = windows[index];
            if (!entry.owner_id.empty() && !(*run).find(entry.owner_id).has_value()) continue;
            const WindowsApplicationRun::OpenOutcome outcome = (*run).open(std::move(entry), false);
            if (!outcome.admission.status.accepted()) {
                failure = outcome.failure_code == 0 ? 2 : outcome.failure_code;
                break;
            }
            created[index] = true;
            listed_ids[index] = outcome.admission.window;
            ++created_count;
            made_progress = true;
        }
        if (!made_progress && failure == 0) failure = 2;
    }
    if (failure == 0) {
        (*run).mark_started();
        for (std::size_t index = 0; index < (*run).slot_count() && failure == 0; ++index) {
            WindowsRunSlot& slot = (*run).slot_at(index);
            if (slot.initial && !(*slot.state).will_show()) failure = 5;
        }
    }
    if (failure != 0) {
        end_run_windowing(*open_state, application, windowing_published);
        (*run).shutdown();
        for (std::size_t index = 0; index < (*run).slot_count(); ++index) {
            const HWND created_window = (*(*run).slot_at(index).state).native_window();
            if (IsWindow(created_window)) DestroyWindow(created_window);
        }
        run.reset();
        if (SUCCEEDED(com_status)) CoUninitialize();
        return failure;
    }
    // Initial windows are shown in the order they were listed.
    for (std::size_t listed = 0; listed < windows.size(); ++listed) {
        for (std::size_t index = 0; index < (*run).slot_count(); ++index) {
            WindowsRunSlot& slot = (*run).slot_at(index);
            if (slot.id != listed_ids[listed]) continue;
            const HWND window = (*slot.state).native_window();
            const bool visible = windows[listed].options.initially_visible;
            // A floating window is shown by the floating policy.
            (*run).window_created(slot.id, visible);
            if (visible && !slot.floating) {
                ShowWindow(window, windows[listed].tool_window ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
                UpdateWindow(window);
            }
            if (windows[listed].options.close_after_launch_for_testing) {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
            break;
        }
    }

    (*run).report_foreground();

    MSG message{};
    bool quit{};
    std::vector<HANDLE> clocks;
    clocks.reserve(WindowsApplicationRun::reserved_slots);
    while (!quit) {
        // A window opened or closed in the previous turn changes the slots;
        // their count is read afresh in every pass.
        clocks.clear();
        for (std::size_t index = 0; index < (*run).slot_count(); ++index) {
            const HANDLE clock = (*(*run).slot_at(index).state).live_frame_clock();
            if (clock != nullptr) clocks.push_back(clock);
        }
        if (clocks.size() >= MAXIMUM_WAIT_OBJECTS) {
            clocks.resize(MAXIMUM_WAIT_OBJECTS - 1U);
        }
        const DWORD wait = MsgWaitForMultipleObjectsEx(
            static_cast<DWORD>(clocks.size()),
            clocks.empty() ? nullptr : clocks.data(), INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_FAILED) break;
        if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + clocks.size()) {
            const HANDLE ready = clocks[wait - WAIT_OBJECT_0];
            for (std::size_t index = 0; index < (*run).slot_count(); ++index) {
                WindowsHostState& state = *(*run).slot_at(index).state;
                if (state.live_frame_clock() == ready) {
                    state.mark_live_frame_due();
                    break;
                }
            }
        }

        const std::chrono::steady_clock::time_point batch_started =
            std::chrono::steady_clock::now();
        for (std::size_t drained = 0U; drained < 32U; ++drained) {
            MSG pending{};
            if (!detail::take_window_message(pending, drained % 2U == 0U)) break;
            if (pending.message == WM_QUIT) {
                message = pending;
                quit = true;
                break;
            }
            TranslateMessage(&pending);
            DispatchMessageW(&pending);
            // Finish an input/ordinary pair before applying the time budget.
            if (drained % 2U == 1U && std::chrono::steady_clock::now() - batch_started >=
                std::chrono::milliseconds(4)) {
                break;
            }
        }
        for (std::size_t index = 0; index < (*run).slot_count(); ++index) {
            WindowsHostState& state = *(*run).slot_at(index).state;
            const HANDLE clock = state.live_frame_clock();
            if (clock != nullptr && WaitForSingleObject(clock, 0U) == WAIT_OBJECT_0) {
                state.mark_live_frame_due();
            }
        }
        if (!quit) {
            for (std::size_t index = 0; index < (*run).slot_count(); ++index) {
                (*(*run).slot_at(index).state).present_pending_frame();
            }
            // The end of the turn: no message of a closed window is on the
            // stack, so runtime windows closed in it are released.
            (*run).reclaim();
        }
    }
    // The program's handles stop reaching the windowing before the remaining
    // windows are destroyed, so a closed callback cannot open another.
    end_run_windowing(*open_state, application, windowing_published);
    (*run).shutdown();
    for (std::size_t index = 0; index < (*run).slot_count(); ++index) {
        const HWND window = (*(*run).slot_at(index).state).native_window();
        if (IsWindow(window)) DestroyWindow(window);
    }
    bool capture_failed{};
    for (std::size_t index = 0; index < (*run).slot_count(); ++index) {
        WindowsRunSlot& slot = (*run).slot_at(index);
        WindowsApplicationRun::publish_final_metrics(slot);
        if (!slot.capture_path.empty() && !(*slot.state).frame_capture_saved()) {
            capture_failed = true;
        }
    }
    std::fflush(stdout);
    run.reset();
    if (SUCCEEDED(com_status)) CoUninitialize();
    if (capture_failed) return 6;
    return static_cast<int>(message.wParam);
}

} // namespace gui_forms::host
