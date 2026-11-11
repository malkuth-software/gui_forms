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
#include "../../../core/resources/fonts/bundled_font_set.hpp"
#include "../raster/dib_band_present.hpp"
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
#include "../raster/dib_frame_store.hpp"
#endif
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT_TEST)
#include "../../../core/text/prepared/prepared_storage.hpp"
#include "../../../../tests/prepared_text_test_support.hpp"
#endif
// The window's painter (stage SK3): the private Skia CPU raster.
#include "skia_raster.hpp"
#include "skia_splash_bake.hpp"
#include "../../../core/application/splashscreen_surface.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "../../../core/failure/fail_fast.hpp"

#include "../paint_endpoint/windows_compatibility_paint_endpoint_metrics.hpp"
#include "../../../core/host/frame_capture/frame_capture.hpp"
#include "../../../core/image/png_writer.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <imm.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <deque>
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
constexpr UINT_PTR scheduler_timer = 1;
constexpr UINT_PTR modal_live_surface_timer = 2;
// The invalidation overlay's fade (invalidation_overlay.hpp): while outlines
// show, the overlay is presented again at this period, from the raster,
// without a paint; the timer stops with the last outline.
constexpr UINT_PTR invalidation_overlay_timer = 3;
constexpr UINT invalidation_overlay_period_milliseconds = 33;

// Copies `area` (device pixels, inside the frame) of a top-down 32-bit BGRA
// frame to `target`; false when GDI refuses. The frame is borrowed.
bool present_bgra_area(HDC target, const std::byte* pixels, std::size_t row_bytes, int width, int height,
                       const RECT& area) noexcept {
    if (target == nullptr || pixels == nullptr || width <= 0 || height <= 0 || row_bytes % 4U != 0U) return false;
    const int left = std::clamp<int>(area.left, 0, width);
    const int top = std::clamp<int>(area.top, 0, height);
    const int right = std::clamp<int>(area.right, left, width);
    const int bottom = std::clamp<int>(area.bottom, top, height);
    if (right == left || bottom == top) return true;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(row_bytes / 4U);
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const int columns = right - left;
    const int rows = bottom - top;
    // GDI measures a top-down source's y from its bottom row.
    const int source_y = height - bottom;
    const int copied = StretchDIBits(target, left, top, columns, rows, left, source_y, columns, rows, pixels, &info,
                                     DIB_RGB_COLORS, SRCCOPY);
    const bool presented = copied != 0 && copied != static_cast<int>(GDI_ERROR);
    return presented;
}

// Retries automation commands deferred while the UI thread was inside a
// dispatch or a native size/move loop (WindowsHostState::automation).
constexpr UINT_PTR automation_retry_timer = 4;
constexpr UINT automation_retry_milliseconds = 10;
// At most this many automation commands wait per window; more are refused.
constexpr std::size_t maximum_deferred_automation = 64U;

// How deep the UI thread is in host dispatches into a model, across every
// window of the thread: an automation command arriving through WM_COPYDATA
// while one runs (sent by another process, it is delivered whenever the
// thread waits in a send or a nested pump) would run model code inside
// model code, menus and popups half opened. Such commands wait until the
// count is back to zero. A modal loop entered by a dispatch sets the count
// aside for its length (ModalDispatchDepth): the modal window is meant to
// take input there.
thread_local std::uint32_t host_dispatch_depth = 0U;

// Counts one host dispatch for its length.
struct HostDispatchScope final {
    HostDispatchScope() noexcept { ++host_dispatch_depth; }
    ~HostDispatchScope() { --host_dispatch_depth; }
    HostDispatchScope(const HostDispatchScope&) = delete;
    HostDispatchScope& operator=(const HostDispatchScope&) = delete;
};

// Sets the dispatch count aside for a modal loop's length.
struct ModalDispatchDepth final {
    std::uint32_t saved{host_dispatch_depth};
    ModalDispatchDepth() noexcept { host_dispatch_depth = 0U; }
    ~ModalDispatchDepth() { host_dispatch_depth = saved; }
    ModalDispatchDepth(const ModalDispatchDepth&) = delete;
    ModalDispatchDepth& operator=(const ModalDispatchDepth&) = delete;
};
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
// Posted by the modal live clock (modal_live_clock_signalled) when the live
// frame clock fires inside the native size/move loop.
constexpr UINT modal_live_clock_message = WM_APP + 0x43U;
// Posted from a producer thread when a live surface publishes while the
// window's live clock rests (the idle handshake, audit E4).
constexpr UINT live_surface_wake_message = WM_APP + 0x44U;
// A scheduled wake nearer than this rides the high-resolution scheduler
// clock instead of WM_TIMER, whose floor is about 10 ms, whose granularity
// is the system timer's (15.6 ms by default) and whose message has the
// lowest priority (audit N63). Farther wakes keep SetTimer.
constexpr std::chrono::milliseconds near_wake_limit{50};

// The live-surface idle wake (audit E4): runs on the producer thread that
// published, so it only posts to the window. It holds the HWND by value,
// not the host state, so a call racing the window's teardown posts to a
// window that is gone, which the post rejects.
struct PostLiveSurfaceWake final {
    HWND window{};
    void operator()() const noexcept {
        static_cast<void>(PostMessageW(window, live_surface_wake_message, 0, 0));
    }
};

// The live frame clock fired while the window is in the native size/move
// loop, which waits on messages only (threadpool thread). `context` is the
// window's HWND; posting to it is all this does, and a window already gone
// rejects the post. The host stops the wait, and waits for this callback to
// finish, before it destroys the window.
void post_modal_live_clock(PVOID context) {
    const HWND window = static_cast<HWND>(context);
    static_cast<void>(PostMessageW(window, modal_live_clock_message, 0, 0));
}
void CALLBACK modal_live_clock_signalled(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WAIT, TP_WAIT_RESULT) {
    gui_forms::detail::call_failing_fast("windows.modal_live_clock", &post_modal_live_clock, context);
}
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

// One COM interface reference, released when the holder ends.
template <typename Interface>
class ComReference final {
public:
    ComReference() = default;
    ComReference(const ComReference&) = delete;
    ComReference& operator=(const ComReference&) = delete;
    ~ComReference() {
        if (pointer != nullptr) (*pointer).Release();
    }
    Interface* pointer{};
};

// A shell path (CoTaskMemAlloc'd by the shell), freed when the holder ends.
class ShellPath final {
public:
    ShellPath() = default;
    ShellPath(const ShellPath&) = delete;
    ShellPath& operator=(const ShellPath&) = delete;
    ~ShellPath() { CoTaskMemFree(text); }
    PWSTR text{};
};

// A path the dialog chose, as UTF-8, or nullopt when it cannot be converted
// (an unpaired surrogate, legal in an NTFS name) or is empty: the dialog
// then reports a failure, never an empty accepted path (audit N65).
std::optional<std::string> dialog_path_utf8(std::wstring_view path) {
    if (path.empty()) return std::nullopt;
    std::string converted = utf8_from_wide(path);
    if (converted.empty()) return std::nullopt;
    return converted;
}

// The file system path of `item`, or nullopt when it has none or it cannot
// be converted.
std::optional<std::string> shell_item_path(IShellItem& item) {
    ShellPath path;
    if (FAILED(item.GetDisplayName(SIGDN_FILESYSPATH, &path.text)) || path.text == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::string> converted = dialog_path_utf8(std::wstring_view(path.text));
    return converted;
}

// The request's filters as the dialog's file types: labels and patterns
// kept alive in `labels` and `patterns`, which `specs` points into. Filled
// in place; empty without filters (the dialog then lists every file).
struct DialogFileTypes final {
    std::vector<std::wstring> labels;
    std::vector<std::wstring> patterns;
    std::vector<COMDLG_FILTERSPEC> specs;
};

void fill_dialog_file_types(const std::vector<HostFileDialogFilter>& filters, DialogFileTypes& types) {
    types.labels.reserve(filters.size());
    types.patterns.reserve(filters.size());
    types.specs.reserve(filters.size());
    for (const gui_forms::HostFileDialogFilter& filter : filters) {
        std::wstring label = wide_from_utf8(filter.label);
        if (label.empty()) label = L"Files";
        std::wstring pattern;
        for (const std::basic_string<char>& extension_utf8 : filter.extensions) {
            std::wstring extension = wide_from_utf8(extension_utf8);
            while (!extension.empty() && (extension.front() == L'.' || extension.front() == L'*')) {
                extension.erase(extension.begin());
            }
            if (extension.empty()) continue;
            if (!pattern.empty()) pattern.push_back(L';');
            pattern.append(L"*.").append(extension);
        }
        if (pattern.empty()) pattern = L"*.*";
        types.labels.push_back(std::move(label));
        types.patterns.push_back(std::move(pattern));
    }
    // The strings no longer move: the specs point into them.
    for (std::size_t index = 0; index < types.labels.size(); ++index) {
        const COMDLG_FILTERSPEC spec{types.labels[index].c_str(), types.patterns[index].c_str()};
        types.specs.push_back(spec);
    }
}

// The settings every path dialog shares: title, starting folder, suggested
// name and file types. A starting folder the shell cannot parse is left to
// the dialog's own choice.
void configure_path_dialog(IFileDialog& dialog, const std::string& title_utf8,
                           const std::string& directory_utf8, const std::string& suggested_utf8,
                           const DialogFileTypes& types, const std::vector<HostFileDialogFilter>& filters,
                           std::size_t initial_filter) {
    const std::wstring title = wide_from_utf8(title_utf8);
    if (!title.empty()) static_cast<void>(dialog.SetTitle(title.c_str()));
    const std::wstring directory = wide_from_utf8(directory_utf8);
    if (!directory.empty()) {
        ComReference<IShellItem> folder;
        const HRESULT parsed = SHCreateItemFromParsingName(
            directory.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&folder.pointer));
        if (SUCCEEDED(parsed) && folder.pointer != nullptr) static_cast<void>(dialog.SetFolder(folder.pointer));
    }
    const std::wstring suggested = wide_from_utf8(suggested_utf8);
    if (!suggested.empty()) static_cast<void>(dialog.SetFileName(suggested.c_str()));
    if (!types.specs.empty()) {
        static_cast<void>(dialog.SetFileTypes(static_cast<UINT>(types.specs.size()), types.specs.data()));
        static_cast<void>(dialog.SetFileTypeIndex(native_filter_index(filters, initial_filter)));
    }
}

// The outcome of Show: accepted, cancelled by the user, or failed.
enum class DialogShown : std::uint8_t { accepted, cancelled, failed };

DialogShown show_path_dialog(IFileDialog& dialog, HWND owner) {
    const HRESULT shown = dialog.Show(owner);
    if (SUCCEEDED(shown)) return DialogShown::accepted;
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return DialogShown::cancelled;
    return DialogShown::failed;
}

HostDialogResult path_dialog_cancelled(std::uint64_t request_id) {
    return {{}, request_id, HostPathDialogResult{HostDialogOutcome::cancelled, {}}};
}

// The request's filter the dialog ended on.
std::optional<std::size_t> dialog_chosen_filter(IFileDialog& dialog,
                                                const std::vector<HostFileDialogFilter>& filters) {
    UINT index = 0U;
    if (FAILED(dialog.GetFileTypeIndex(&index))) return std::nullopt;
    const std::optional<std::size_t> chosen = chosen_filter(filters, static_cast<DWORD>(index));
    return chosen;
}

// The open, save and folder dialogs use the shell's common item dialog
// (IFileOpenDialog, IFileSaveDialog; audit N65): no fixed path buffer, no
// MAX_PATH limit on folders, and a path that cannot be converted to UTF-8
// is a backend failure, never an empty accepted path. COM is initialized by
// the host's run (apartment threaded).
HostDialogResult native_open_dialog(HWND owner, std::uint64_t request_id,
                                    const HostOpenFileDialogRequest& request) {
    ComReference<IFileOpenDialog> dialog;
    const HRESULT created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                             IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog.pointer));
    if (FAILED(created) || dialog.pointer == nullptr) {
        return path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    IFileOpenDialog& open = *dialog.pointer;
    FILEOPENDIALOGOPTIONS options = 0;
    static_cast<void>(open.GetOptions(&options));
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR;
    if (request.allow_multiple) options |= FOS_ALLOWMULTISELECT;
    static_cast<void>(open.SetOptions(options));
    DialogFileTypes types;
    fill_dialog_file_types(request.filters, types);
    configure_path_dialog(open, request.title, request.initial_directory, request.suggested_name, types,
                          request.filters, request.initial_filter);
    const DialogShown shown = show_path_dialog(open, owner);
    if (shown == DialogShown::cancelled) return path_dialog_cancelled(request_id);
    if (shown == DialogShown::failed) return path_dialog_failure(request_id, HostServiceError::backend_failure);
    ComReference<IShellItemArray> items;
    if (FAILED(open.GetResults(&items.pointer)) || items.pointer == nullptr) {
        return path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    DWORD count = 0U;
    if (FAILED((*items.pointer).GetCount(&count)) || count == 0U) {
        return path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    HostPathDialogResult value;
    value.outcome = HostDialogOutcome::accepted;
    value.chosen_filter = dialog_chosen_filter(open, request.filters);
    const std::size_t taken = std::min<std::size_t>(count, HostServices::maximum_dialog_paths);
    value.paths.reserve(taken);
    for (std::size_t index = 0; index < taken; ++index) {
        ComReference<IShellItem> item;
        if (FAILED((*items.pointer).GetItemAt(static_cast<DWORD>(index), &item.pointer)) || item.pointer == nullptr) {
            return path_dialog_failure(request_id, HostServiceError::backend_failure);
        }
        std::optional<std::string> path = shell_item_path(*item.pointer);
        if (!path) return path_dialog_failure(request_id, HostServiceError::backend_failure);
        value.paths.push_back(std::move(*path));
    }
    return {{}, request_id, std::move(value)};
}

HostDialogResult native_save_dialog(HWND owner, std::uint64_t request_id,
                                    const HostSaveFileDialogRequest& request) {
    ComReference<IFileSaveDialog> dialog;
    const HRESULT created = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                             IID_IFileSaveDialog, reinterpret_cast<void**>(&dialog.pointer));
    if (FAILED(created) || dialog.pointer == nullptr) {
        return path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    IFileSaveDialog& save = *dialog.pointer;
    FILEOPENDIALOGOPTIONS options = 0;
    static_cast<void>(save.GetOptions(&options));
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    if (request.confirm_overwrite) {
        options |= FOS_OVERWRITEPROMPT;
    } else {
        options &= ~static_cast<FILEOPENDIALOGOPTIONS>(FOS_OVERWRITEPROMPT);
    }
    static_cast<void>(save.SetOptions(options));
    DialogFileTypes types;
    fill_dialog_file_types(request.filters, types);
    configure_path_dialog(save, request.title, request.initial_directory, request.suggested_name, types,
                          request.filters, request.initial_filter);
    std::wstring extension = wide_from_utf8(request.default_extension);
    while (!extension.empty() && extension.front() == L'.') extension.erase(extension.begin());
    if (!extension.empty()) static_cast<void>(save.SetDefaultExtension(extension.c_str()));
    const DialogShown shown = show_path_dialog(save, owner);
    if (shown == DialogShown::cancelled) return path_dialog_cancelled(request_id);
    if (shown == DialogShown::failed) return path_dialog_failure(request_id, HostServiceError::backend_failure);
    ComReference<IShellItem> item;
    if (FAILED(save.GetResult(&item.pointer)) || item.pointer == nullptr) {
        return path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    std::optional<std::string> path = shell_item_path(*item.pointer);
    if (!path) return path_dialog_failure(request_id, HostServiceError::backend_failure);
    HostPathDialogResult value;
    value.outcome = HostDialogOutcome::accepted;
    value.chosen_filter = dialog_chosen_filter(save, request.filters);
    value.paths.push_back(std::move(*path));
    return {{}, request_id, std::move(value)};
}

HostDialogResult native_folder_dialog(HWND owner, std::uint64_t request_id,
                                      const HostFolderDialogRequest& request) {
    ComReference<IFileOpenDialog> dialog;
    const HRESULT created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                             IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog.pointer));
    if (FAILED(created) || dialog.pointer == nullptr) {
        return path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    IFileOpenDialog& folder = *dialog.pointer;
    FILEOPENDIALOGOPTIONS options = 0;
    static_cast<void>(folder.GetOptions(&options));
    options |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    static_cast<void>(folder.SetOptions(options));
    const DialogFileTypes none;
    const std::vector<HostFileDialogFilter> no_filters;
    configure_path_dialog(folder, request.title, request.initial_directory, std::string(), none, no_filters, 0U);
    const DialogShown shown = show_path_dialog(folder, owner);
    if (shown == DialogShown::cancelled) return path_dialog_cancelled(request_id);
    if (shown == DialogShown::failed) return path_dialog_failure(request_id, HostServiceError::backend_failure);
    ComReference<IShellItem> item;
    if (FAILED(folder.GetResult(&item.pointer)) || item.pointer == nullptr) {
        return path_dialog_failure(request_id, HostServiceError::backend_failure);
    }
    std::optional<std::string> path = shell_item_path(*item.pointer);
    if (!path) return path_dialog_failure(request_id, HostServiceError::backend_failure);
    HostPathDialogResult value;
    value.outcome = HostDialogOutcome::accepted;
    value.paths.push_back(std::move(*path));
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
    case HostMessageButtons::abort_retry_ignore: return MB_ABORTRETRYIGNORE;
    case HostMessageButtons::cancel_try_continue: return MB_CANCELTRYCONTINUE;
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

// The number of buttons of a set, as MessageBoxW shows them.
std::size_t message_button_count(HostMessageButtons buttons) noexcept {
    if (buttons == HostMessageButtons::ok) return 1U;
    const bool three = buttons == HostMessageButtons::yes_no_cancel ||
                       buttons == HostMessageButtons::abort_retry_ignore ||
                       buttons == HostMessageButtons::cancel_try_continue;
    return three ? 3U : 2U;
}

// The default choice's place among the set's buttons, as MessageBoxW counts
// them, with Help after the set's buttons: the second for No, the cancel of
// OK/Cancel and Retry/Cancel, Retry of Abort/Retry/Ignore and Try Again; the
// third for the cancel of Yes/No/Cancel, Ignore and Continue; else the
// first.
UINT message_default_style(const HostMessageDialogRequest& request) noexcept {
    const HostMessageButtons buttons = request.buttons;
    const HostDialogChoice choice = request.default_choice;
    std::size_t position = 0U;
    if (choice == HostDialogChoice::help) {
        position = message_button_count(buttons);
    } else {
        const bool third = (buttons == HostMessageButtons::yes_no_cancel && choice == HostDialogChoice::cancel) ||
                           (buttons == HostMessageButtons::abort_retry_ignore && choice == HostDialogChoice::ignore) ||
                           (buttons == HostMessageButtons::cancel_try_continue &&
                            choice == HostDialogChoice::continue_);
        const bool cancel_second =
            buttons == HostMessageButtons::ok_cancel || buttons == HostMessageButtons::retry_cancel;
        const bool second = (cancel_second && choice == HostDialogChoice::cancel) || choice == HostDialogChoice::no ||
                            (buttons == HostMessageButtons::abort_retry_ignore && choice == HostDialogChoice::retry) ||
                            choice == HostDialogChoice::try_again;
        if (third) position = 2U;
        else if (second) position = 1U;
    }
    if (position == 3U) return MB_DEFBUTTON4;
    if (position == 2U) return MB_DEFBUTTON3;
    if (position == 1U) return MB_DEFBUTTON2;
    return MB_DEFBUTTON1;
}

UINT native_message_style(const HostMessageDialogRequest& request) noexcept {
    UINT style = MB_APPLMODAL;
    style |= message_button_style(request.buttons);
    style |= message_icon_style(request.icon);
    style |= message_default_style(request);
    if (request.right_align) style |= MB_RIGHT;
    if (request.rtl_reading) style |= MB_RTLREADING;
    // MB_HELP: the Help button sends WM_HELP to the owner; F1 does so with
    // or without it (DefWindowProc passes a top-level window's WM_HELP to
    // its owner).
    if (request.help_button) style |= MB_HELP;
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
    case IDABORT: value.choice = HostDialogChoice::abort; break;
    case IDIGNORE: value.choice = HostDialogChoice::ignore; break;
    case IDTRYAGAIN: value.choice = HostDialogChoice::try_again; break;
    case IDCONTINUE: value.choice = HostDialogChoice::continue_; break;
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

// The run's fixed device scale, or 0 to follow each monitor: the
// scale_override (WindowsHostOptions) of the window run_windows opens, or of
// the first window run_windows_application opens, which every window of the
// run then takes, popups included, and in which the monitors are reported.
// One scale for the whole desktop keeps desktop logical units and every
// window's logical units the same, so a popup lands where its owner put it
// (a capture at scale 1 on a 2x display). Set on the UI thread before the
// first window exists and read there only.
double& run_scale_override() noexcept {
    static double scale = 0.0;
    return scale;
}

// Development hook (GUI_FORMS_WINDOWS_SCALE, a number from 1 to 4): the
// fixed device scale of an application run whose first window asks for
// none, so that a program run through Application::run can be captured at
// one scale whatever the display; 0 when unset or out of range.
double development_scale_override() noexcept {
    const char* const value = std::getenv("GUI_FORMS_WINDOWS_SCALE");
    if (value == nullptr) return 0.0;
    char* end = nullptr;
    const double scale = std::strtod(value, &end);
    const bool valid = end != value && *end == '\0' && std::isfinite(scale) && scale >= 1.0 && scale <= 4.0;
    return valid ? scale : 0.0;
}

double native_monitor_scale(HMONITOR monitor) noexcept {
    if (run_scale_override() > 0.0) return run_scale_override();
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
    if (run_scale_override() > 0.0) return run_scale_override();
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

// A byte in the module that holds GUI.Forms, whose address names that module to
// GetModuleHandleExW.
const char gui_forms_module_anchor{};

// The longest path GetModuleFileNameW can return with long paths enabled, in UTF-16 units.
constexpr DWORD maximum_module_path_units = 32768U;

// The directory of `module`'s file (null for the executable); empty when Windows cannot say.
std::filesystem::path module_directory(HMODULE module) {
    std::vector<wchar_t> path(maximum_module_path_units, L'\0');
    const DWORD count = GetModuleFileNameW(module, path.data(), maximum_module_path_units);
    if (count == 0U || count >= maximum_module_path_units) return {};
    const std::filesystem::path directory =
        std::filesystem::path(std::wstring(path.data(), count)).parent_path();
    return directory;
}

// The directory of the module holding this code: the GUI.Forms DLL when another process (a
// .NET runtime, a plug-in host) loaded it, else the executable it is linked into; empty when
// Windows cannot say. The handle is borrowed without a reference.
std::filesystem::path gui_forms_module_directory() {
    HMODULE module = nullptr;
    const DWORD flags =
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    const BOOL found =
        GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&gui_forms_module_anchor), &module);
    if (found == FALSE || module == nullptr) return {};
    const std::filesystem::path directory = module_directory(module);
    return directory;
}

// The bundled font directory: the first of GUI_FORMS_FONT_DIR, "fonts" beside the executable
// and "fonts" beside the GUI.Forms module that holds the required faces; empty when none does,
// which locate_bundled_fonts counts and reports (bundled_font_diagnostics,
// bundled_font_refusal).
std::filesystem::path bundled_font_directory() {
    std::array<gui_forms::detail::BundledFontCandidate, 3> candidates{};
    const wchar_t* override_directory = _wgetenv(L"GUI_FORMS_FONT_DIR");
    candidates[0].source = gui_forms::BundledFontSource::environment;
    if (override_directory != nullptr) candidates[0].directory = std::filesystem::path(override_directory);
    const std::filesystem::path executable_directory = module_directory(nullptr);
    candidates[1].source = gui_forms::BundledFontSource::executable;
    if (!executable_directory.empty()) candidates[1].directory = executable_directory / L"fonts";
    const std::filesystem::path library_directory = gui_forms_module_directory();
    candidates[2].source = gui_forms::BundledFontSource::library_module;
    // A GUI.Forms linked into the executable is the executable; it is not offered twice.
    if (!library_directory.empty() && library_directory != executable_directory)
        candidates[2].directory = library_directory / L"fonts";
    const gui_forms::detail::BundledFontLocation location =
        gui_forms::detail::locate_bundled_fonts(candidates);
    return location.directory;
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
        detail::DibBandSource band_source{};
        band_source.pixels = static_cast<const std::byte*>(source);
        band_source.row_bytes = row_bytes;
        band_source.width = width_;
        band_source.height = height_;
        const RECT band{area.left, area.top, area.right, area.bottom};
        const bool presented = detail::present_dib_band(target, band_source, band);
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
        // A live drain is housekeeping: nothing here is caught.
        if (!begin_frame(clip)) return false;
        canvas_.save();
        canvas_.clip_rect(presentation.clip);
        canvas_.draw_live_surface(presentation.surface, presentation.destination, 1.0);
        canvas_.restore();
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

    // What the window last presented, for a composite capture
    // (capture-composite): premultiplied BGRA8 rows `row_bytes` apart,
    // borrowed until the next frame; `alpha` false when every pixel is
    // presented opaque.
    struct PresentedView final {
        const std::byte* pixels{};
        std::size_t row_bytes{};
        int width{};
        int height{};
        bool alpha{};
    };
    [[nodiscard]] PresentedView presented_view() const noexcept {
        const PresentedPixels saved = presented_pixels();
        const PresentedView view{saved.pixels, saved.row_bytes, saved.width, saved.height, frame_alpha_};
        return view;
    }

public:
    // The pixels a capture and the invalidation overlay read: the committed
    // front (transactional), else the painter's last frame.
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

private:

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
// Writes what `owner` and the visible windows it owns show, composed in
// their desktop places over a checkerboard, as one PNG at `path`, and
// prints their client rectangles (capture-composite). Defined in
// windows_application_run.inc; `run` is null outside an application run.
bool write_composite_capture(WindowsApplicationRun* run, WindowsHostState& owner, const std::wstring& path);
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
#if defined(GUI_FORMS_WINDOWS_SCROLL_COPY_TEST)
    friend struct ScrollCopyHostFixture;
#endif
#if defined(GUI_FORMS_WINDOWS_LIVE_IDLE_TEST)
    friend struct LiveIdleHostFixture;
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
        // The scheduler clock for near wakes (audit N63), high resolution
        // where the system offers it. Without one, every wake uses SetTimer.
        scheduler_clock_ = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (scheduler_clock_ == nullptr) {
            scheduler_clock_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
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
        // The modal live clock posts to the window: stop it first.
        if (modal_live_clock_ != nullptr) {
            stop_modal_live_clock();
            CloseThreadpoolWait(modal_live_clock_);
            modal_live_clock_ = nullptr;
        }
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
        if (scheduler_clock_ != nullptr) {
            CancelWaitableTimer(scheduler_clock_);
            CloseHandle(scheduler_clock_);
            scheduler_clock_ = nullptr;
        }
        if (model_) {
            (*model_).set_live_surface_idle_wake_handler({});
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
        scale_ = options_.scale_override > 0.0 ? options_.scale_override : query_scale(window);
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
        report_window_state();
        accessibility_.attach(hwnd_, *model_, scale_);
        (*model_).set_dispatch_wake_handler(
            gui_forms::detail::BoundMemberFunction<
                void (WindowsHostState::*)() noexcept>(
                    *this, &WindowsHostState::post_managed_dispatch));
        (*model_).set_paint_wake_handler(
            gui_forms::detail::BoundMemberFunction<
                void (WindowsHostState::*)() noexcept>(
                    *this, &WindowsHostState::request_render_wake));
        (*model_).set_live_surface_idle_wake_handler(PostLiveSurfaceWake{hwnd_});
#if !defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        // The painter keeps its pixels between frames, so a scroll's pixels
        // can be moved instead of repainted (apply_scroll_copies). The
        // transactional store presents committed DIB candidates instead and
        // repaints scrolled viewports.
        (*model_).set_scroll_copies_supported(true);
#endif
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
        // The window is ready before the initialization turn: a callback in
        // it may run a nested loop (a modal window shown from posted work),
        // whose messages for this window, its managed wakes included, a window
        // that is not ready would drop.
        native_phase_ = NativePhase::ready;
        // One FIFO initialization turn runs before presentation. Work posted by
        // those callbacks stays deferred to the ordinary next host turn.
        static_cast<void>((*model_).drain_posted_work());
        if (options_.dispatch_pending) options_.dispatch_pending();
        collect_damage();
        return true;
    }

    // Reports the native window's state (HostWindowStateEvent; owner,
    // 2026-11-08: the window outer band) after attach and whenever it
    // changes: WM_SIZE (maximize, restore, and a snap's resize),
    // WM_EXITSIZEMOVE (a snap by dragging) and WM_STYLECHANGED (a caption
    // removed for full screen). The session passes full screen to the band
    // and asks DWM for the corners again, which the state may square. A
    // report refused (before attach, after close) is tried again on the
    // next change.
    void report_window_state() {
        const detail::WindowsWindowState state = detail::read_windows_window_state(hwnd_);
        if (window_state_reported_ && state == reported_window_state_) return;
        const HostDispatchResult reported = dispatch(HostWindowStateEvent{state.full_screen});
        if (!reported.accepted()) return;
        reported_window_state_ = state;
        window_state_reported_ = true;
        collect_damage();
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

    // A passive native popup (HostPopupFocus::passive): a click does not
    // activate it. Set before the native window is created.
    void mark_passive_popup() noexcept { passive_popup_ = true; }
    // A Splash takes no input: the pointer passes through it to the
    // windows beneath (HTTRANSPARENT, SPLASH_STUDY D4 (a)).
    void mark_splash() noexcept { splash_ = true; }

    // Application-scope windowing (windows_application_run.inc). Bound before
    // the native window is created; the run outlives this state.
    void attach_run(WindowsApplicationRun& run, HostWindowId window) noexcept {
        run_ = &run;
        window_id_ = window;
    }
    [[nodiscard]] HWND native_window() const noexcept { return hwnd_; }
    // The client area's least size: Window's MinimumSize where a side is
    // written non-zero, else the size the window opened with.
    [[nodiscard]] Size minimum_size() const noexcept {
        const Size chrome = services_.chrome_minimum_size();
        const Size minimum{chrome.width > 0.0 ? chrome.width : options_.minimum_size.width,
                           chrome.height > 0.0 ? chrome.height : options_.minimum_size.height};
        return minimum;
    }
    // Shows the window with `ordinary`, or with the state Window's
    // WindowState asked for while it was hidden.
    void show_with(int ordinary) noexcept {
        if (hwnd_ == nullptr || closed_) return;
        const int command = services_.show_command(ordinary);
        ShowWindow(hwnd_, command);
    }
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
    [[nodiscard]] WindowsRaster::PresentedView presented_view() const noexcept { return raster_.presented_view(); }
    // Defined in windows_application_run.inc.
    void report_frame_to_run();
    // Whether the screen point in `lparam` (WM_NCHITTEST) lies in a content
    // surface's margin: inside the client, where the model hits no control
    // but its root.
    [[nodiscard]] bool popup_margin_at(LPARAM lparam) noexcept {
        if (!model_ || !(*model_).content_surface_declared()) return false;
        POINT point{static_cast<LONG>(static_cast<short>(LOWORD(lparam))),
                    static_cast<LONG>(static_cast<short>(HIWORD(lparam)))};
        if (!ScreenToClient(hwnd_, &point)) return false;
        const Point logical{point.x / scale_, point.y / scale_};
        const Control::Ptr hit = (*model_).hit_test(logical);
        const bool margin = !hit || hit.get() == (*model_).root().get();
        return margin;
    }
    // WM_MOVE: tells the model the window moved when its client origin on
    // the screen changed (not for a Popup or Splash window, which nothing
    // is placed against).
    void report_moved_to_model(LPARAM lparam) {
        if (passive_popup_ || splash_ || model_ == nullptr) return;
        const POINT origin{static_cast<LONG>(static_cast<short>(LOWORD(lparam))),
                           static_cast<LONG>(static_cast<short>(HIWORD(lparam)))};
        const bool first = !last_client_origin_.has_value();
        const bool moved = !first && ((*last_client_origin_).x != origin.x || (*last_client_origin_).y != origin.y);
        last_client_origin_ = origin;
        if (moved) (*model_).report_frame_moved();
    }
    void retire_from_run() noexcept;
    void report_application_active_to_run(bool active);
    void hidden_on_close_to_run() noexcept;
    // Shows without activating, for the floating policy.
    void show_without_activation() noexcept {
        show_with(SW_SHOWNOACTIVATE);
    }
    void hide_for_policy() noexcept { hide_window(); }

    [[nodiscard]] HANDLE live_frame_clock() const noexcept {
        return live_frame_clock_;
    }
    // The clock near scheduled wakes ride (audit N63); null without one.
    [[nodiscard]] HANDLE scheduler_clock() const noexcept {
        return scheduler_clock_;
    }
    // The scheduler clock fired: the model's near wake is due.
    void scheduler_clock_signalled() {
        scheduler_clock_armed_ = false;
        ++scheduler_clock_signals_;
        scheduled_wake();
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
        case WM_HELP:
            // A message box this window owns asks for help (its Help button
            // or F1); the box stays open.
            if (services_.publish_message_help()) return TRUE;
            break;
        case WM_ACTIVATEAPP:
            // Sent to every top-level window of the thread; the windowing
            // ignores repeated reports of the same state.
            report_application_active_to_run(wparam != FALSE);
            break;
        case WM_SIZE:
            // Inside the native size/move loop the sizes arriving between two
            // frames are coalesced: the newest is laid out when the next frame
            // is painted or the live clock ticks (apply_pending_resize). The
            // class has no CS_HREDRAW/CS_VREDRAW, so a shrink exposes nothing
            // and sends no WM_PAINT: the coalesced size requests that paint
            // itself, or a window without a live clock would not lay out
            // until the loop ends. The paint presents the whole raster once
            // (expose_whole_raster_), it does not repaint the window.
            if (in_size_move_) {
                resize_pending_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                resize();
            }
            report_frame_to_run();
            report_window_state();
            return 0;
        case WM_MOVE:
            report_frame_to_run();
            report_moved_to_model(lparam);
            return 0;
        case WM_DPICHANGED: dpi_changed(wparam, lparam); return 0;
        case WM_DISPLAYCHANGE:
            // A display was added, removed or changed resolution (audit
            // N62): the window hears the new monitor set, as the macOS
            // host's screen-parameters notification reports it.
            report_displays();
            break;
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
        case WM_NCLBUTTONDOWN: {
            // A press on the frame or caption whose button is already up
            // (dispatched behind a long frame) starts no move or size loop:
            // that loop would wait for a second click to end.
            const int primary = GetSystemMetrics(SM_SWAPBUTTON) != 0 ? VK_RBUTTON : VK_LBUTTON;
            const bool primary_down = (GetAsyncKeyState(primary) & 0x8000) != 0;
            if (!detail::frame_press_may_drag(wparam, primary_down)) return 0;
            break;
        }
        case WM_NCHITTEST:
            // A Splash is never hit: the message goes to the window beneath
            // it on this thread (its owner, for a tooltip).
            if (splash_) return HTTRANSPARENT;
            // A Popup window drawing its own content (a menu panel, a list)
            // is larger than its face by the shadow around it; the shadow
            // takes no input, so a point there reaches the window beneath
            // (the owner) as if the Popup were not there (owner,
            // 2026-11-10: the dead margin round a menu).
            if (passive_popup_ && popup_margin_at(lparam)) return HTTRANSPARENT;
            break;
        case WM_MOUSEACTIVATE:
            if (trace_win32_input()) {
                std::fprintf(stderr, "win32-input=mouse-activate|hit:%u|message:%u|active:%d|focus:%d\n",
                             static_cast<unsigned>(LOWORD(lparam)),
                             static_cast<unsigned>(HIWORD(lparam)),
                             GetActiveWindow() == hwnd_, GetFocus() == hwnd_);
                std::fflush(stderr);
            }
            // A passive popup leaves its owner active; the click still
            // reaches the popup.
            if (passive_popup_) return MA_NOACTIVATE;
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
            hover_tracked_ = {};
            break;
        case WM_PAINT:
            // A render wake remains coalesced until the host actually begins
            // the paint transaction. Releasing it here allows content that
            // changes during this frame to request one later frame without
            // turning the posted wake itself into a synchronous paint loop.
            render_dispatch_pending_ = false;
            presentation_pending_ = false;
            apply_pending_resize();
            paint();
            return 0;
        case WM_ENTERSIZEMOVE:
            // DefWindowProc enters a nested modal loop while a top-level
            // window is moved or sized. The outer waitable-timer loop cannot
            // run there, but the live plane must keep presenting independently
            // of retained layout and input. A modal-loop timer is the native
            // Win32 clock bridge for exactly that interval.
            // The live frame clock keeps its display phase there through a
            // threadpool wait that posts modal_live_clock_message; a timer
            // message (coarse, and only sent when the queue is empty) is the
            // fallback when that wait cannot be made.
            in_size_move_ = true;
            // A native move or size loop starts: menus placed against the
            // window close before it moves under them.
            if (!passive_popup_ && !splash_ && model_ != nullptr) (*model_).report_frame_moved();
            if ((*model_).has_live_surface_presentations() && !start_modal_live_clock()) {
                SetTimer(hwnd_, modal_live_surface_timer,
                         live_surface_period_milliseconds, nullptr);
            }
            return 0;
        case WM_EXITSIZEMOVE:
            KillTimer(hwnd_, modal_live_surface_timer);
            stop_modal_live_clock();
            in_size_move_ = false;
            apply_pending_resize();
            report_window_state();
            mark_live_frame_due();
            return 0;
        case WM_STYLECHANGED:
            // A caption removed or restored can make the window full screen
            // or leave it (read_windows_window_state).
            report_window_state();
            break;
        case modal_live_clock_message:
            // A late post after the loop ended is ignored: the outer loop
            // waits on the clock again.
            if (!in_size_move_ || modal_live_clock_ == nullptr) return 0;
            apply_pending_resize();
            mark_live_frame_due();
            present_live_surface_updates();
            live_frame_due_ = false;
            if (live_clock_armed_) SetThreadpoolWait(modal_live_clock_, live_frame_clock_, nullptr);
            return 0;
        case live_surface_wake_message:
            leave_live_idle();
            return 0;
        case managed_dispatch_message: {
            const HostDispatchScope scope;
            static_cast<void>((*model_).drain_posted_work());
            if (options_.dispatch_pending) options_.dispatch_pending();
            collect_damage();
            return 0;
        }
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
            if (wparam == invalidation_overlay_timer) {
                present_invalidation_overlay();
                return 0;
            }
            if (wparam == automation_retry_timer) {
                run_deferred_automation();
                return 0;
            }
            if (wparam == modal_live_surface_timer) {
                // Do not enter retained polling/layout from the native modal
                // loop. Sample and present only the newest live generation,
                // after the newest coalesced size.
                apply_pending_resize();
                present_live_surface_updates();
                return 0;
            }
            if (wparam == scheduler_timer) {
                KillTimer(hwnd_, scheduler_timer);
                scheduled_wake();
                return 0;
            }
            break;
        case WM_MOUSEMOVE: pointer(message, PointerAction::move, wparam, lparam); return 0;
        // A passive popup (a menu panel, a list) never takes the native
        // focus: SetFocus would activate it and deactivate its owner, whose
        // open menu then closes under the press.
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            if (!passive_popup_ && !splash_) SetFocus(hwnd_);
            pointer(message, PointerAction::down, wparam, lparam); return 0;
        case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK:
            if (!passive_popup_ && !splash_) SetFocus(hwnd_);
            pointer(message, PointerAction::down, wparam, lparam); return 0;
        case WM_LBUTTONUP: case WM_MBUTTONUP:
            pointer(message, PointerAction::up, wparam, lparam); return 0;
        case WM_RBUTTONUP:
            // DefWindowProc turns the right button's release into
            // WM_CONTEXTMENU, Windows' own context request.
            pointer(message, PointerAction::up, wparam, lparam);
            return DefWindowProcW(hwnd_, message, wparam, lparam);
        case WM_CONTEXTMENU: context_request(lparam); return 0;
        case WM_MOUSEHOVER: hover(lparam); return 0;
        case WM_MOUSEWHEEL: wheel(wparam, lparam, false); return 0;
        case WM_MOUSEHWHEEL: wheel(wparam, lparam, true); return 0;
        case WM_SYSKEYDOWN:
            // Preserve the native close gesture. Swallowing all system keys
            // prevents DefWindowProc from producing the ordinary WM_CLOSE,
            // including its existing cancellation and owned-window policy.
            if (wparam == VK_F4 && (lparam & (1LL << 29)) != 0) break;
            if (!key(KeyAction::down, wparam, lparam) && context_key(message, wparam)) break;
            return 0;
        case WM_KEYDOWN:
            if (!key(KeyAction::down, wparam, lparam) && context_key(message, wparam)) break;
            return 0;
        case WM_KEYUP: case WM_SYSKEYUP:
            if (!key(KeyAction::up, wparam, lparam) && context_key(message, wparam)) break;
            return 0;
        case WM_CHAR: character(static_cast<wchar_t>(wparam)); return 0;
        case WM_IME_SETCONTEXT:
            // Inline composition (audit N61): the window draws the
            // composition itself, so the input method shows only its
            // candidate window.
            lparam &= ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
            break;
        case WM_IME_STARTCOMPOSITION:
            // Handled without DefWindowProc, which would open the system's
            // own composition window.
            place_input_method_windows();
            return 0;
        case WM_IME_COMPOSITION:
            // The composition string reaches the focused control as a
            // composing text input and the result string as committed input
            // (ime_composition). Not passed on: DefWindowProc would turn
            // the result into WM_IME_CHAR and WM_CHAR, a second insertion.
            place_input_method_windows();
            ime_composition(lparam);
            return 0;
        case WM_IME_ENDCOMPOSITION:
            // An open composition that ended without a result was cancelled:
            // its marked text is removed.
            if (ime_composing_) {
                TextInputEvent cancel{};
                cancel.composing = true;
                static_cast<void>(dispatch(std::move(cancel)));
                ime_composing_ = false;
                collect_damage();
            }
            return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == OBJID_CLIENT) { return accessibility_.object(wparam); }
            if (WindowsAccessibility::automation_request(lparam)) {
                return accessibility_.automation_object(wparam, lparam);
            }
            break;
        case WM_GETMINMAXINFO: minimum_size(reinterpret_cast<MINMAXINFO*>(lparam)); return 0;
        case WM_COPYDATA: return automation(reinterpret_cast<const COPYDATASTRUCT*>(lparam));
        case WM_CLOSE: return close();
        case WM_QUERYENDSESSION: return query_end_session();
        case WM_ENDSESSION: return end_session(wparam != FALSE);
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
            (*model_).set_live_surface_idle_wake_handler({});
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
               ",\"idle_entries\":" + std::to_string(live_idle_entries_) +
               ",\"scheduler_clock_signals\":" + std::to_string(scheduler_clock_signals_) +
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

    // The invalidation overlay at present time (invalidation_overlay.hpp):
    // the areas the overlay draws on now and drew on last are copied from
    // the presented frame into a scratch frame, the outlines are composited
    // there, and those areas reach the window from it. The raster is never
    // drawn on, so the overlay damages nothing and a faded outline leaves
    // the raster's pixels behind. Off, with nothing left drawn, this is one
    // relaxed load and a size test.
    void present_invalidation_overlay() {
        if (hwnd_ == nullptr || !model_) return;
        InvalidationOverlay* const overlay = (*model_).invalidation_overlay();
        if (overlay == nullptr && overlay_drawn_.empty()) {
            disarm_invalidation_overlay_timer();
            return;
        }
        const WindowsRaster::PresentedPixels source = raster_.presented_pixels();
        if (source.pixels == nullptr || source.width <= 0 || source.height <= 0) return;
        const std::uint32_t width = static_cast<std::uint32_t>(source.width);
        const std::uint32_t height = static_cast<std::uint32_t>(source.height);
        const InvalidationClock::time_point now = InvalidationClock::now();
        overlay_regions_.clear();
        if (overlay != nullptr) {
            (*overlay).expire(now);
            (*overlay).fill_regions(scale_, width, height, overlay_regions_);
        }
        overlay_present_.assign(overlay_regions_.begin(), overlay_regions_.end());
        overlay_present_.insert(overlay_present_.end(), overlay_drawn_.begin(), overlay_drawn_.end());
        if (overlay_present_.empty()) {
            disarm_invalidation_overlay_timer();
            return;
        }
        const std::size_t frame_bytes = source.row_bytes * static_cast<std::size_t>(height);
        if (overlay_scratch_.size() != frame_bytes) overlay_scratch_.assign(frame_bytes, std::byte{0});
        InvalidationOverlayTarget target{};
        target.pixels = std::span<std::byte>(overlay_scratch_);
        target.width = width;
        target.height = height;
        target.row_bytes = source.row_bytes;
        // One pass over the bounding box of every area: a window full of
        // outlines (up to maximum_invalidation_outlines, each with five
        // areas) would otherwise cost thousands of GDI calls a fade tick.
        DevicePixelRect box{source.width, source.height, 0, 0};
        for (const DevicePixelRect& region : overlay_present_) {
            box.left = std::min(box.left, std::clamp<std::int32_t>(region.left, 0, source.width));
            box.top = std::min(box.top, std::clamp<std::int32_t>(region.top, 0, source.height));
            box.right = std::max(box.right, std::clamp<std::int32_t>(region.right, 0, source.width));
            box.bottom = std::max(box.bottom, std::clamp<std::int32_t>(region.bottom, 0, source.height));
        }
        if (box.right <= box.left || box.bottom <= box.top) {
            overlay_drawn_.assign(overlay_regions_.begin(), overlay_regions_.end());
            return;
        }
        const std::size_t row_offset = static_cast<std::size_t>(box.left) * 4U;
        const std::size_t row_length = static_cast<std::size_t>(box.right - box.left) * 4U;
        for (std::int32_t y = box.top; y < box.bottom; ++y) {
            const std::size_t offset = static_cast<std::size_t>(y) * source.row_bytes + row_offset;
            std::memcpy(overlay_scratch_.data() + offset, source.pixels + offset, row_length);
        }
        if (overlay != nullptr) (*overlay).compose(target, scale_, now, box);
        HDC dc = GetDC(hwnd_);
        if (dc == nullptr) return;
        const RECT area{box.left, box.top, box.right, box.bottom};
        static_cast<void>(present_bgra_area(dc, overlay_scratch_.data(), source.row_bytes, source.width,
                                            source.height, area));
        ReleaseDC(hwnd_, dc);
        overlay_drawn_.assign(overlay_regions_.begin(), overlay_regions_.end());
        if (overlay != nullptr && !(*overlay).empty()) {
            if (!overlay_timer_armed_) {
                SetTimer(hwnd_, invalidation_overlay_timer, invalidation_overlay_period_milliseconds, nullptr);
                overlay_timer_armed_ = true;
            }
        } else {
            disarm_invalidation_overlay_timer();
        }
    }

    void disarm_invalidation_overlay_timer() noexcept {
        if (!overlay_timer_armed_) return;
        overlay_timer_armed_ = false;
        if (hwnd_ != nullptr) KillTimer(hwnd_, invalidation_overlay_timer);
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
        show_with(SW_SHOW);
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
        const Size minimum = minimum_size();
        if (client_size.width < minimum.width || client_size.height < minimum.height) {
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
        const HostDispatchScope scope;
        return session_.dispatch(std::move(event));
    }

    // The system's double-click interval (GetDoubleClickTime, which Windows
    // keeps between 1 and 5000 milliseconds), reported after attach and on
    // WM_SETTINGCHANGE whenever it differs from the last report.
    void report_input_settings() {
        const UINT read = GetDoubleClickTime();
        std::uint32_t interval = static_cast<std::uint32_t>(read);
        if (interval == 0U) interval = 1U;
        if (interval > maximum_double_click_milliseconds) interval = maximum_double_click_milliseconds;
        HostInputSettingsEvent input{interval, SettingOrigin::host, PointerSettings{}};
        PointerSettings& pointer = input.pointer;
        pointer.drag_width = clamp_pointer_distance(GetSystemMetrics(SM_CXDRAG) / scale_);
        pointer.drag_height = clamp_pointer_distance(GetSystemMetrics(SM_CYDRAG) / scale_);
        pointer.drag_origin = SettingOrigin::host;
        UINT hover_time = 0U;
        UINT hover_width = 0U;
        UINT hover_height = 0U;
        const bool hover_read = SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0U, &hover_time, 0U) != FALSE &&
                                SystemParametersInfoW(SPI_GETMOUSEHOVERWIDTH, 0U, &hover_width, 0U) != FALSE &&
                                SystemParametersInfoW(SPI_GETMOUSEHOVERHEIGHT, 0U, &hover_height, 0U) != FALSE;
        if (hover_read) {
            std::uint32_t hover = static_cast<std::uint32_t>(hover_time);
            if (hover == 0U) hover = 1U;
            if (hover > maximum_hover_milliseconds) hover = maximum_hover_milliseconds;
            pointer.hover_milliseconds = hover;
            pointer.hover_width = clamp_pointer_distance(static_cast<double>(hover_width) / scale_);
            pointer.hover_height = clamp_pointer_distance(static_cast<double>(hover_height) / scale_);
            pointer.hover_origin = SettingOrigin::host;
        }
        pointer.native_hover = true;
        // The caret's blink phase (audit N75): INFINITE means the user
        // turned blinking off; 0 is a failed read, which keeps the default.
        const UINT blink = GetCaretBlinkTime();
        if (blink == INFINITE) {
            input.caret = CaretSettings{0U, SettingOrigin::host};
        } else if (blink != 0U) {
            std::uint32_t phase = static_cast<std::uint32_t>(blink);
            if (phase > maximum_caret_blink_milliseconds) phase = maximum_caret_blink_milliseconds;
            input.caret = CaretSettings{phase, SettingOrigin::host};
        }
        const bool unchanged = reported_input_settings_ &&
                               (*reported_input_settings_).double_click_milliseconds == input.double_click_milliseconds &&
                               (*reported_input_settings_).pointer == input.pointer &&
                               (*reported_input_settings_).caret == input.caret;
        if (unchanged) return;
        const HostDispatchResult reported = dispatch(input);
        if (reported.accepted()) reported_input_settings_ = input;
    }

    // Sends the current monitors (HostDisplayEvent); a failed query sends
    // nothing and the window keeps its last set.
    void report_displays() {
        HostMonitorResult monitors = services_.query_monitors();
        if (!monitors.status.accepted()) return;
        static_cast<void>(dispatch(HostDisplayEvent{std::move(monitors.monitors)}));
    }

    static double clamp_pointer_distance(double distance) noexcept {
        if (!(distance >= 0.0)) return 0.0;
        return distance > maximum_pointer_distance ? maximum_pointer_distance : distance;
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
#if !defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        // The raster now holds the whole client: the pixels it carried and the
        // damage the window reported for what the resize exposed. The
        // window's own pixels need not match it, since Windows keeps or
        // copies a resized window's old client pixels as it chooses (a
        // maximized window restored kept them 144 rows lower). So the next
        // present copies the whole raster, and an empty damage still
        // presents (expose_whole_raster_).
        expose_whole_raster_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    }

    // Lays out the newest size coalesced during the size/move loop, if any.
    void apply_pending_resize() {
        if (!resize_pending_) return;
        resize_pending_ = false;
        resize();
    }

    void dpi_changed(WPARAM wparam, LPARAM lparam) {
        double next = std::max(1.0, HIWORD(wparam) / 96.0);
        if (run_scale_override() > 0.0) next = run_scale_override();
        if (options_.scale_override > 0.0) next = options_.scale_override;
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

    // The client area in logical units.
    [[nodiscard]] Size logical_size_of_client() const noexcept {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const Size size{static_cast<double>(client.right - client.left) / scale_,
                        static_cast<double>(client.bottom - client.top) / scale_};
        return size;
    }

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

    // Takes the scroll copies the model recorded with the damage just taken
    // (SurfaceRoot::take_scroll_copies), before that damage joins the held
    // damage: what this host still holds moves with each copy's pixels. The
    // next frame applies them in order (apply_scroll_copies). The
    // transactional profile records none.
    void take_scroll_copies() {
        taken_copies_.clear();
        (*model_).take_scroll_copies(taken_copies_);
        for (const ScrollCopy& copy : taken_copies_) {
            if (!pending_damage_.empty()) {
                pending_damage_ = gui_forms::damage_after_scroll_copy(pending_damage_, copy.clip, copy.offset);
            }
            pending_copies_.push_back(copy);
        }
        taken_copies_.clear();
    }

#if !defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    // Applies the pending scroll copies to the frame begun (`frame_begun`)
    // before its damage is painted, and leaves the clips of those applied in
    // applied_copy_clips_: the frame presents them whole, since the window
    // still shows their pixels where they were. A copy that cannot be applied
    // (no frame, or the raster refuses it) damages every pending clip
    // instead: copies earlier in the list may have moved pixels in it, and
    // the repaint covers each clip. Without a frame the clips are also
    // invalidated, so a later WM_PAINT paints them.
    void apply_scroll_copies(const bool frame_begun) {
        applied_copy_clips_.clear();
        if (pending_copies_.empty()) return;
        bool applied = frame_begun;
        for (const ScrollCopy& copy : pending_copies_) {
            if (applied) applied = raster_.painter().scroll_copy(copy.clip, copy.offset);
            applied_copy_clips_.push_back(copy.clip);
        }
        pending_copies_.clear();
        if (applied) {
            scroll_copies_applied_ += applied_copy_clips_.size();
            return;
        }
        for (const Rect clip : applied_copy_clips_) {
            pending_damage_.add(clip);
            if (frame_begun) continue;
            const RECT native_clip = device_rect(clip);
            InvalidateRect(hwnd_, &native_clip, FALSE);
        }
        scroll_copies_refused_ += applied_copy_clips_.size();
        applied_copy_clips_.clear();
    }

    // A frame that applied copies but did not reach the window leaves the
    // window's pixels behind the raster's in their clips: they are damaged
    // and painted again.
    void damage_unpresented_copies() {
        for (const Rect clip : applied_copy_clips_) {
            pending_damage_.add(clip);
            const RECT native_clip = device_rect(clip);
            InvalidateRect(hwnd_, &native_clip, FALSE);
        }
        applied_copy_clips_.clear();
    }
#endif

    void collect_damage() {
        static_cast<void>((*model_).poll_frame_schedule(FrameClock::now()));
        accessibility_.notify(scale_);
        DamageRegion damage = (*model_).take_damage();
        take_scroll_copies();
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
        arm_scheduled_wake();
        // New retained damage may move or uncover a live surface: an idle
        // clock runs one more drain, which rests it again if nothing is
        // published (audit E4).
        if (!rectangles.empty() && live_idle_) leave_live_idle();
        const bool needs_live_clock = (*model_).has_live_surface_presentations() && !live_idle_;
        if (needs_live_clock && !live_clock_armed_) {
            arm_live_clock();
        } else if (!needs_live_clock && live_clock_armed_) {
            CancelWaitableTimer(live_frame_clock_);
            live_clock_armed_ = false;
            live_frame_due_ = false;
            live_clock_deadline_ = 0;
        }
    }

    // Arms the model's next wake (audit N63): one nearer than
    // near_wake_limit on the high-resolution scheduler clock, a farther one
    // (or any while the native size/move loop runs, which waits on messages
    // only) on WM_TIMER. Either way the other is cancelled first.
    void arm_scheduled_wake() {
        KillTimer(hwnd_, scheduler_timer);
        if (scheduler_clock_armed_) {
            CancelWaitableTimer(scheduler_clock_);
            scheduler_clock_armed_ = false;
        }
        const std::optional<FrameTime> wake = (*model_).next_wake();
        if (!wake) return;
        const FrameInterval remaining = *wake - FrameClock::now();
        const bool soon = remaining < near_wake_limit;
        if (soon && scheduler_clock_ != nullptr && !in_size_move_) {
            // Relative due times are in 100 ns units, negative.
            const std::chrono::nanoseconds::rep nanoseconds =
                std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
            LARGE_INTEGER due{};
            due.QuadPart = -(std::max<LONGLONG>)(static_cast<LONGLONG>(nanoseconds / 100), 1);
            scheduler_clock_armed_ = SetWaitableTimer(scheduler_clock_, &due, 0, nullptr, nullptr, FALSE) != FALSE;
            if (scheduler_clock_armed_) return;
        }
        const std::chrono::milliseconds::rep milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        SetTimer(hwnd_, scheduler_timer,
                 static_cast<UINT>(std::clamp<std::int64_t>(milliseconds, 1, 60'000)), nullptr);
    }

    // A scheduled wake came due (WM_TIMER or the scheduler clock): the model
    // polls its schedule and its damage is collected. Housekeeping: nothing
    // is caught here; the window procedure is the fail-fast boundary.
    void scheduled_wake() {
        collect_damage();
    }

    // The live clock rests (audit E4): a drain found no new publication, so
    // the clock stops and the window arms the idle handshake; a producer's
    // next publication posts live_surface_wake_message. An idle window with
    // live surfaces registered schedules no frames.
    void enter_live_idle() {
        if (live_clock_armed_) CancelWaitableTimer(live_frame_clock_);
        live_clock_armed_ = false;
        live_frame_due_ = false;
        live_clock_deadline_ = 0;
        live_idle_ = true;
        ++live_idle_entries_;
        // Arming rechecks the generations, so a publication racing this
        // drain still wakes the window.
        (*model_).set_live_surface_idle_waiting(true);
    }

    // A publication or retained damage ends the rest: the clock runs again
    // while live presentations remain.
    void leave_live_idle() {
        if (!live_idle_) return;
        live_idle_ = false;
        (*model_).set_live_surface_idle_waiting(false);
        if (!(*model_).has_live_surface_presentations() || live_clock_armed_) return;
        arm_live_clock();
        if (in_size_move_ && modal_live_clock_ != nullptr && live_clock_armed_) {
            SetThreadpoolWait(modal_live_clock_, live_frame_clock_, nullptr);
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
            live_timing_vblank_ = static_cast<LONGLONG>(timing.qpcVBlank);
            live_timing_source_ = LiveTimingSource::display_phased;
        } else {
            // The cadence's own phase stands in for the vertical blank.
            live_timing_vblank_ = anchor;
            live_timing_source_ = LiveTimingSource::fallback;
        }
        // DWM reports the primary display's timing only (a window handle is
        // refused since Windows 8.1). A window on another display with a
        // different refresh rate takes that display's period, at the
        // cadence's own phase (audit N64).
        const LONGLONG window_period = window_monitor_period();
        if (window_period > 0 && (window_period * 100 < period * 99 || window_period * 100 > period * 101)) {
            period = window_period;
            anchor = live_clock_deadline_ != 0 ? live_clock_deadline_ : now.QuadPart;
            live_timing_vblank_ = anchor;
            live_timing_source_ = LiveTimingSource::fallback;
        }
        live_timing_period_ = period;
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

    // The refresh period, in counter ticks, of the display the window is on
    // when that is not the primary display (EnumDisplaySettingsW); 0 on the
    // primary display or when the rate is unknown (0 or 1 Hz: the
    // hardware default).
    [[nodiscard]] LONGLONG window_monitor_period() const noexcept {
        if (hwnd_ == nullptr || qpc_frequency_ <= 0) return 0;
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONULL);
        const POINT origin{0, 0};
        const HMONITOR primary = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        if (monitor == nullptr || monitor == primary) return 0;
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) == FALSE) return 0;
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) == FALSE) return 0;
        if (mode.dmDisplayFrequency <= 1U) return 0;
        const LONGLONG ticks = qpc_frequency_ / static_cast<LONGLONG>(mode.dmDisplayFrequency);
        return ticks;
    }

    // The display timing the live clock was last phased to, in FrameClock
    // time, for the surfaces' presentation reports (design note section 8);
    // empty before the clock was first armed.
    [[nodiscard]] LiveDisplayTiming live_display_timing() const noexcept {
        LiveDisplayTiming timing;
        if (qpc_frequency_ <= 0 || live_timing_period_ <= 0) return timing;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const FrameTime steady_now = FrameClock::now();
        timing.refresh_period = std::chrono::duration_cast<FrameInterval>(
            std::chrono::nanoseconds(qpc_nanoseconds(live_timing_period_)));
        timing.last_vblank = steady_now - std::chrono::duration_cast<FrameInterval>(
            std::chrono::nanoseconds(qpc_nanoseconds(now.QuadPart - live_timing_vblank_)));
        timing.source = live_timing_source_;
        return timing;
    }

    // QueryPerformanceCounter ticks as nanoseconds, split so that a long
    // interval does not overflow.
    [[nodiscard]] LONGLONG qpc_nanoseconds(LONGLONG ticks) const noexcept {
        const LONGLONG seconds = ticks / qpc_frequency_;
        const LONGLONG remainder = ticks % qpc_frequency_;
        const LONGLONG nanoseconds = seconds * 1'000'000'000LL + remainder * 1'000'000'000LL / qpc_frequency_;
        return nanoseconds;
    }

    // Starts the modal live clock (WM_ENTERSIZEMOVE): arms the live frame
    // clock and waits on it from the threadpool. False when there is no
    // clock or the wait cannot be made, and the caller falls back.
    bool start_modal_live_clock() noexcept {
        if (live_frame_clock_ == nullptr || hwnd_ == nullptr) return false;
        if (modal_live_clock_ == nullptr) {
            modal_live_clock_ = CreateThreadpoolWait(&modal_live_clock_signalled, static_cast<PVOID>(hwnd_), nullptr);
            if (modal_live_clock_ == nullptr) return false;
        }
        if (!live_clock_armed_) arm_live_clock();
        if (!live_clock_armed_) return false;
        SetThreadpoolWait(modal_live_clock_, live_frame_clock_, nullptr);
        return true;
    }

    // Stops the modal live clock and waits for a callback in flight, so that
    // none runs after this returns; the wait object is kept for the next
    // size/move loop.
    void stop_modal_live_clock() noexcept {
        if (modal_live_clock_ == nullptr) return;
        SetThreadpoolWait(modal_live_clock_, nullptr, nullptr);
        WaitForThreadpoolWaitCallbacks(modal_live_clock_, TRUE);
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
        take_scroll_copies();
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
        // A scroll copy can move a live surface's previous pixels: frames
        // are then drawn over their whole clips.
        const bool regions_usable = !live_region_lost_ && pending_copies_.empty();
        apply_scroll_copies(frame_begun);
        DamageRegion screen;
        for (const Rect clip : applied_copy_clips_) screen.add(clip);
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
            (*model_).note_live_surface_presented(update, region);
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
        // After a resize the whole raster is copied once (expose_whole_raster_).
        const bool expose_whole = expose_whole_raster_ && frame_begun;
        if (expose_whole) screen.add(Rect{0.0, 0.0, logical_size_of_client().width, logical_size_of_client().height});
        // Present: each rectangle once, and none left for WM_PAINT to repeat.
        HDC target = GetDC(hwnd_);
        if (target == nullptr) {
            live_updates_failed_ += live_composed;
            damage_unpresented_copies();
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
        present_invalidation_overlay();
        if (expose_whole && presented) expose_whole_raster_ = false;
        copied.copy_nanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - copy_started).count());
        const std::uint64_t elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
        // A region taken from a surface is cleared; only a presented raster
        // keeps it valid, so after a failure the next tick copies whole clips.
        if (regions_taken) live_region_lost_ = !presented;
        if (presented) {
            applied_copy_clips_.clear();
        } else {
            damage_unpresented_copies();
        }
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
        // Each surface shown learns the generation it showed and the display
        // timing of this tick (design note sections 7 and 8), once per tick.
        const LiveDisplayTiming display_timing = live_display_timing();
        for (std::size_t index = 0; index < updates.size(); ++index) {
            const std::size_t source = live_frame_sources_[index];
            bool first = true;
            for (std::size_t earlier = 0; earlier < index; ++earlier) {
                if (live_frame_sources_[earlier] == source) first = false;
            }
            if (!first || !updates[index].surface || !live_frames_[source]) continue;
            if (composed_live_rectangles_[source] == 0U) continue;
            (*updates[index].surface).notify_presented(live_frames_[source].generation(), display_timing);
        }
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
        // Nothing new: the clock rests until a producer publishes (E4).
        if (updates.empty() && (*model_).has_live_surface_presentations()) enter_live_idle();
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
                (*model_).note_live_surface_presented(update, update.clip);
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
        present_invalidation_overlay();
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
        // A paint tick is housekeeping: nothing is caught. Its budget is the
        // model's expectation (paint_within_budget); an exception from a
        // control's paint ends the process at the window procedure.
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
        present_invalidation_overlay();
        collect_damage();
#else
        // After a resize the raster is current outside the pending damage, so
        // the window's update region (what Windows invalidated) is copied
        // from it, not painted again: the whole client is presented.
        const bool expose_whole = expose_whole_raster_;
        const bool update_region_read = expose_whole || add_update_region_damage();
        PAINTSTRUCT paint_state{};
        HDC dc = BeginPaint(hwnd_, &paint_state);
        const std::chrono::steady_clock::time_point started =
            std::chrono::steady_clock::now();
        // Match the whole device pixels that the update region covers. A
        // narrower fractional clip can leave stripes from previous frames.
        if (!update_region_read) pending_damage_.add(logical_rect(paint_state.rcPaint));
        prepare_frame_alpha();
        static_cast<void>(raster_.synchronize_images((*model_).image_resources()));
        // Scroll copies move pixels the frame then presents whole, so the
        // frame's surface admits their clips too; the paint itself stays
        // clipped to the damage.
        const bool copies_pending = !pending_copies_.empty();
        DamageRegion frame_damage = pending_damage_;
        for (const ScrollCopy& copy : pending_copies_) frame_damage.add(copy.clip);
        std::optional<PaintReceipt> receipt{};
        const bool frame_begun = raster_.begin_frame(frame_damage);
        apply_scroll_copies(frame_begun);
        if (frame_begun) {
            render::SkiaRaster& painter = raster_.painter();
            if (copies_pending) {
                painter.save();
                painter.clip_damage(pending_damage_);
            }
            receipt = (*model_).paint(painter, pending_damage_.rectangles());
            if (copies_pending) painter.restore();
            raster_.end_frame();
        }
        // Present exactly the rectangles painted and the copies' clips: the
        // raster keeps the last frame everywhere else, and the window already
        // shows it there.
        const std::span<const Rect> painted_rectangles = pending_damage_.rectangles();
        std::vector<RECT> present_rectangles;
        present_rectangles.reserve(painted_rectangles.size() + applied_copy_clips_.size() + 1U);
        if (expose_whole) {
            RECT client{};
            GetClientRect(hwnd_, &client);
            present_rectangles.push_back(client);
        } else {
            for (std::size_t index = 0; index < painted_rectangles.size(); ++index) {
                present_rectangles.push_back(device_rect(painted_rectangles[index]));
            }
            for (const Rect clip : applied_copy_clips_) present_rectangles.push_back(device_rect(clip));
        }
        // With no damage pending there is nothing to paint, and an exposure
        // still copies the raster as it stands.
        bool presented = receipt.has_value() || (expose_whole && pending_damage_.empty());
        const std::chrono::steady_clock::time_point copy_started =
            std::chrono::steady_clock::now();
        // BeginPaint's device context is clipped to the update region, which
        // need not hold a copy's clip: a frame that applied copies presents
        // through the window's own device context.
        const bool copied_clips = !applied_copy_clips_.empty();
        HDC present_dc = copied_clips ? GetDC(hwnd_) : dc;
        if (present_dc == nullptr) presented = false;
        for (std::size_t index = 0; presented && index < present_rectangles.size(); ++index) {
            presented = raster_.present(present_dc, present_rectangles[index]);
        }
        if (copied_clips && present_dc != nullptr) ReleaseDC(hwnd_, present_dc);
        if (presented) {
            applied_copy_clips_.clear();
        } else {
            damage_unpresented_copies();
        }
        if (presented && expose_whole) expose_whole_raster_ = false;
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
        if (presented && receipt &&
            (*model_).notify_presented(*receipt,
                                     static_cast<std::uint64_t>(elapsed.count()), copied)) {
            pending_damage_.clear();
            complete_frame_capture_if_due(paint_state.rcPaint);
        }
        present_invalidation_overlay();
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
        if (action == PointerAction::move) track_hover();
        if (!update_cursor(client_point(lparam))) apply_system_cursor(CursorKind::arrow);
        collect_damage();
    }

    // WM_MOUSEWHEEL (vertical) or WM_MOUSEHWHEEL (horizontal: a tilt wheel,
    // a precision touchpad's sideways pan). Both carry WHEEL_DELTA per
    // notch, or a fraction of one from a smooth device, as lines. A
    // positive vertical delta scrolls up and a positive horizontal one
    // right; the portable horizontal delta is positive to scroll left (as
    // the other hosts report it), so the horizontal sign is flipped.
    void wheel(WPARAM wparam, LPARAM lparam, bool horizontal) {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        PointerEvent event;
        event.action = PointerAction::wheel;
        event.position = {point.x / scale_, point.y / scale_};
        const double lines = GET_WHEEL_DELTA_WPARAM(wparam) / static_cast<double>(WHEEL_DELTA);
        event.wheel_delta = horizontal ? Point{-lines, 0.0} : Point{0.0, lines};
        event.modifiers = modifiers();
        event.pointer_id = 1;
        dispatch(std::move(event));
        collect_damage();
    }

    // Whether DefWindowProc should see a key the window left unhandled, so
    // that it raises WM_CONTEXTMENU as Windows does: Shift+F10 and the Menu
    // key (VK_APPS). Every other key stays the window's alone.
    static bool context_key(UINT message, WPARAM wparam) noexcept {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool shift_f10 = wparam == VK_F10 && shift && (message == WM_SYSKEYDOWN || message == WM_KEYDOWN);
        const bool menu_key = wparam == VK_APPS;
        return shift_f10 || menu_key;
    }

    // WM_CONTEXTMENU: a pointer request at its screen position, or a
    // keyboard one (Shift+F10, the Menu key), which Windows marks with the
    // position (-1, -1).
    void context_request(LPARAM lparam) {
        HostContextRequestEvent request{};
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        if (x == -1 && y == -1) {
            request.origin = ContextRequestOrigin::keyboard;
        } else {
            POINT point{x, y};
            ScreenToClient(hwnd_, &point);
            request.origin = ContextRequestOrigin::pointer;
            request.position = {point.x / scale_, point.y / scale_};
        }
        dispatch(request);
        collect_damage();
    }

    // WM_MOUSEHOVER: Windows measured the rest (SPI_GETMOUSEHOVERTIME within
    // the hover rectangle); the window raises MouseHover on the hovered
    // control. Tracking ends with it and is armed again when the pointer
    // reaches another control (track_hover).
    void hover(LPARAM lparam) {
        hover_tracked_ = {};
        dispatch(HostHoverEvent{client_point(lparam)});
        collect_damage();
    }

    // Arms TrackMouseEvent's hover report for the control under the pointer
    // once per control: each control raises MouseHover once per entry.
    void track_hover() {
        const Control::Ptr hovered = (*model_).hovered_control();
        const RuntimeId current = hovered ? (*hovered).runtime_id() : RuntimeId{};
        if (current == hover_tracked_ || current == RuntimeId{}) return;
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_HOVER | TME_LEAVE;
        track.hwndTrack = hwnd_;
        track.dwHoverTime = HOVER_DEFAULT;
        if (TrackMouseEvent(&track) != FALSE) hover_tracked_ = current;
    }

    bool key(KeyAction action, WPARAM wparam, LPARAM lparam) {
        KeyEvent event;
        event.action = action;
        event.physical_key = detail::physical_key_from_key_message(wparam, lparam);
        event.scan_code = detail::scan_code_from_key_message(lparam);
        event.modifiers = modifiers();
        event.repeat = action == KeyAction::down && (lparam & (1LL << 30)) != 0;
        const HostDispatchResult result = dispatch(std::move(event));
        collect_damage();
        return result.handled;
    }

    // Places the input method's composition window at the caret and keeps
    // its candidate window off the caret line, in device pixels of the
    // client area: SurfaceRoot::text_input_caret, the box a host gives its
    // input method. Nothing happens without an eligible focused control or
    // without an input context on this window; a refused placement leaves
    // the input method's own default.
    void place_input_method_windows() {
        const std::optional<Rect> caret = (*model_).text_input_caret();
        if (!caret) return;
        const HIMC context = ImmGetContext(hwnd_);
        if (context == nullptr) return;
        const RECT device = device_rect(*caret);
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = POINT{device.left, device.top};
        static_cast<void>(ImmSetCompositionWindow(context, &composition));
        CANDIDATEFORM candidate{};
        candidate.dwIndex = 0;
        candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = POINT{device.left, device.bottom};
        candidate.rcArea = device;
        static_cast<void>(ImmSetCandidateWindow(context, &candidate));
        static_cast<void>(ImmReleaseContext(hwnd_, context));
    }

    // Reads the strings WM_IME_COMPOSITION announces in `flags` (GCS_RESULTSTR,
    // GCS_COMPSTR) from the window's input context and passes them on.
    void ime_composition(LPARAM flags) {
        const HIMC context = ImmGetContext(hwnd_);
        if (context == nullptr) return;
        std::wstring result;
        std::wstring composition;
        const bool has_result = (flags & GCS_RESULTSTR) != 0 && read_ime_string(context, GCS_RESULTSTR, result);
        const bool has_composition =
            (flags & GCS_COMPSTR) != 0 && read_ime_string(context, GCS_COMPSTR, composition);
        static_cast<void>(ImmReleaseContext(hwnd_, context));
        ime_strings(has_result, result, has_composition, composition);
    }

    // One input context string into `text`; false when it cannot be read.
    // The byte count IMM reports is checked against the string's limit
    // before the buffer is sized.
    static bool read_ime_string(HIMC context, DWORD which, std::wstring& text) {
        const LONG bytes = ImmGetCompositionStringW(context, which, nullptr, 0U);
        if (bytes < 0) return false;
        const std::size_t units = static_cast<std::size_t>(bytes) / sizeof(wchar_t);
        if (units > maximum_ime_units) return false;
        text.assign(units, L'\0');
        if (units == 0U) return true;
        const LONG copied = ImmGetCompositionStringW(context, which, text.data(), static_cast<DWORD>(bytes));
        if (copied != bytes) return false;
        return true;
    }

    // The input method's strings as text input (audit N61): a result is
    // committed input, which replaces the composition's marked text; a
    // composition string, sent after the result as WM_IME_COMPOSITION
    // orders them, is the new marked text, and an empty one cancels an open
    // composition. A string that is not valid UTF-16 is dropped.
    void ime_strings(bool has_result, std::wstring_view result, bool has_composition,
                     std::wstring_view composition) {
        bool changed = false;
        if (has_result && !result.empty()) {
            TextInputEvent committed{};
            committed.text_utf8 = utf8_from_wide(result);
            if (!committed.text_utf8.empty()) {
                static_cast<void>(dispatch(std::move(committed)));
                changed = true;
            }
            ime_composing_ = false;
        }
        if (has_composition) {
            const bool open = !composition.empty();
            if (open || ime_composing_) {
                TextInputEvent composing{};
                composing.composing = true;
                composing.text_utf8 = utf8_from_wide(composition);
                if (!open || !composing.text_utf8.empty()) {
                    static_cast<void>(dispatch(std::move(composing)));
                    changed = true;
                    ime_composing_ = open;
                }
            }
        }
        if (changed) collect_damage();
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
        const Size minimum = minimum_size();
        RECT rect{0, 0,
                  static_cast<LONG>(std::ceil(minimum.width * scale_)),
                  static_cast<LONG>(std::ceil(minimum.height * scale_))};
        adjust_client_frame(rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)),
            static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), scale_);
        // A side of 0 sets no minimum there: the system's own minimum
        // tracking size, already in `info`, stays (Windows Forms' empty
        // MinimumSize).
        if (minimum.width > 0.0) (*info).ptMinTrackSize.x = rect.right - rect.left;
        if (minimum.height > 0.0) (*info).ptMinTrackSize.y = rect.bottom - rect.top;
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

    // WM_QUERYENDSESSION: the user is logging off or the system is shutting
    // down or restarting. Ask the window's closing handler with
    // HostCloseReason::session_end; TRUE lets the session end, FALSE asks
    // Windows to hold it (Windows may still end it). An allowed request
    // leaves the window attached and visible, since WM_ENDSESSION may yet
    // report that the session's end was called off.
    LRESULT query_end_session() {
        if (closed_ || native_phase_ == NativePhase::closing) return TRUE;
        HostCloseRequest request{HostCloseReason::session_end, false, false};
        if (options_.close_request) options_.close_request(request);
        request.hide_on_accept = false;
        const HostDispatchResult result = dispatch(request);
        if (result.accepted() && !result.close_allowed) return FALSE;
        return TRUE;
    }

    // WM_ENDSESSION: ending is TRUE when the session ends after this message
    // returns (the process may be terminated without further messages), so
    // the window closes now and WM_DESTROY reports HostCloseReason::
    // session_end. FALSE means the session's end was called off; the window
    // stays as it was.
    LRESULT end_session(bool ending) {
        if (!ending || closed_ || native_phase_ == NativePhase::closing) return 0;
        destroy_reason_ = HostCloseReason::session_end;
        native_phase_ = NativePhase::closing;
        DestroyWindow(hwnd_);
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
        // A command that feeds the model waits while the thread is inside a
        // dispatch or a native size/move loop, and runs, in arrival order,
        // once neither holds: whichever of an input and a command arrives
        // first, each runs whole before the other starts. It is accepted
        // (TRUE) when deferred; its own result is printed when it runs.
        const bool reads_only = command == "snapshot" || command == "capture" ||
                                command.starts_with("capture ") || command.starts_with("capture-composite ") ||
                                command == "close";
        if (!reads_only && (automation_busy() || !deferred_automation_.empty())) {
            if (deferred_automation_.size() >= maximum_deferred_automation) return FALSE;
            deferred_automation_.push_back(std::move(command));
            SetTimer(hwnd_, automation_retry_timer, automation_retry_milliseconds, nullptr);
            return TRUE;
        }
        return automation_command(command);
    }

    [[nodiscard]] bool automation_busy() const noexcept {
        const bool busy = host_dispatch_depth != 0U || in_size_move_;
        return busy;
    }

    // Runs the deferred automation commands, oldest first, while the thread
    // is free; the timer stops once none wait.
    void run_deferred_automation() {
        while (!deferred_automation_.empty() && !automation_busy() && hwnd_ != nullptr) {
            const std::string command = std::move(deferred_automation_.front());
            deferred_automation_.pop_front();
            static_cast<void>(automation_command(command));
        }
        if (deferred_automation_.empty() && hwnd_ != nullptr) KillTimer(hwnd_, automation_retry_timer);
    }

    // Model code run by a command counts as a dispatch: a command sent
    // while it runs waits for it.
    LRESULT automation_command(const std::string& command) {
        const HostDispatchScope scope;
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
        constexpr std::string_view composite = "capture-composite ";
        if (command.starts_with(composite)) {
            const std::wstring path = wide_from_utf8(command.substr(composite.size()));
            if (path.empty()) return FALSE;
            const bool saved = write_composite_capture(run_, *this, path);
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
        // A key to whatever has the keyboard now (an open menu, a lifted
        // drop-down), without moving the focus: "key-focused <physical>".
        constexpr std::string_view focused_key = "key-focused ";
        if (command.starts_with(focused_key)) {
            std::istringstream input(command.substr(focused_key.size()));
            std::uint32_t physical{};
            if (!(input >> physical)) return FALSE;
            KeyEvent pressed;
            pressed.action = KeyAction::down;
            pressed.physical_key = physical;
            const bool handled_down = dispatch(std::move(pressed)).handled;
            KeyEvent released;
            released.action = KeyAction::up;
            released.physical_key = physical;
            static_cast<void>(dispatch(std::move(released)));
            collect_damage();
            std::fprintf(stdout, "{\"automation\":\"key-focused\",\"physical\":%u,\"handled\":%s}\n", physical,
                         handled_down ? "true" : "false");
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
    // Scroll copies taken from the model and not yet applied to the raster,
    // in order (take_scroll_copies); scratch for the take; the clips the
    // current frame applied, presented whole by it or damaged again.
    std::vector<ScrollCopy> pending_copies_;
    std::vector<ScrollCopy> taken_copies_;
#if !defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    std::vector<Rect> applied_copy_clips_;
    std::uint64_t scroll_copies_applied_{};
    std::uint64_t scroll_copies_refused_{};
#endif
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
    // The invalidation overlay's present-time storage, reused: the scratch
    // frame it composites in (allocated on its first use), the areas it
    // draws on now, those it drew on last, and both together.
    std::vector<std::byte> overlay_scratch_;
    std::vector<DevicePixelRect> overlay_regions_;
    std::vector<DevicePixelRect> overlay_drawn_;
    std::vector<DevicePixelRect> overlay_present_;
    bool overlay_timer_armed_{};
    // The last compositor state report's generation; 0 before the first.
    CompositorGeneration compositor_generation_{};
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    bool recovery_expose_pending_{};
#endif
    double scale_{1.0};
    std::uint64_t next_sequence_{1};
    // The double-click interval last reported; 0 before the first report.
    std::optional<HostInputSettingsEvent> reported_input_settings_{};
    // The control TrackMouseEvent's hover report is armed for (zero for
    // none): each control raises MouseHover once per entry.
    RuntimeId hover_tracked_{};
    wchar_t pending_high_surrogate_{};
    bool render_dispatch_pending_{};
    bool presentation_pending_{};
    HANDLE live_frame_clock_{};
    // The threadpool wait on live_frame_clock_ during the native size/move
    // loop (start_modal_live_clock); made on first use, closed with the host.
    PTP_WAIT modal_live_clock_{};
    // A WM_SIZE inside the size/move loop not yet laid out (WM_SIZE).
    bool resize_pending_{};
    // The last native window state the session accepted
    // (report_window_state); none before the first.
    detail::WindowsWindowState reported_window_state_{};
    bool window_state_reported_{};
    // Whether the next present copies the whole raster: set by every
    // resize, whose pixels Windows may have left out of step with the
    // raster; cleared when such a present succeeds (paint,
    // present_live_frame_sequence).
    bool expose_whole_raster_{};
    bool live_clock_armed_{};
    // The live clock's armed deadline in QueryPerformanceCounter ticks (0
    // when it is not armed), the counter's frequency, and the compositor's
    // timing query (null without dwmapi).
    LONGLONG live_clock_deadline_{};
    LONGLONG qpc_frequency_{};
    // The phase the clock was last armed to (live_display_timing): the
    // vertical blank and refresh period in counter ticks, and their source.
    LONGLONG live_timing_vblank_{};
    LONGLONG live_timing_period_{};
    LiveTimingSource live_timing_source_{LiveTimingSource::none};
    DwmCompositionTimingFunction dwm_composition_timing_{};
    bool live_frame_due_{};
    // An inline composition is open in the model (audit N61): the last
    // composing input had text.
    bool ime_composing_{};
    // The longest input method string read, in UTF-16 units.
    static constexpr std::size_t maximum_ime_units = 4096U;
    // The live clock rests (enter_live_idle) until a publication or new
    // retained damage (leave_live_idle); counted for the idle proof.
    bool live_idle_{};
    std::uint64_t live_idle_entries_{};
    // The high-resolution clock for near scheduled wakes (audit N63).
    HANDLE scheduler_clock_{};
    bool scheduler_clock_armed_{};
    std::uint64_t scheduler_clock_signals_{};
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
    // The application run this window belongs to (observing; the run owns
    // this state) and the window's identity in it. Null for run_windows.
    WindowsApplicationRun* run_{};
    HostWindowId window_id_{};
    // mark_passive_popup: never activated by a click.
    bool passive_popup_{};
    // Automation commands waiting for the thread to leave a dispatch or a
    // size/move loop, oldest first (at most maximum_deferred_automation).
    std::deque<std::string> deferred_automation_{};
    // The client origin on the screen at the last WM_MOVE, for
    // report_moved_to_model.
    std::optional<POINT> last_client_origin_{};
    // mark_splash: transparent to the pointer.
    bool splash_{};
    // The reason WM_DESTROY reports: user for every native close, application
    // when the run destroys the window (HostWindowing::destroy_window),
    // session_end when WM_ENDSESSION closes it.
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

// One window message, delivered to the window's host state (bound at
// WM_NCCREATE). Runs inside the fail-fast boundary (window_procedure).
LRESULT deliver_window_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
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

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    const LRESULT result = gui_forms::detail::call_failing_fast("windows.window_procedure", &deliver_window_message,
                                                                window, message, wparam, lparam);
    return result;
}

#include "windows_application_run.inc"

#if defined(GUI_FORMS_DIB_LIFECYCLE_TEST)
#include "windows_dib_lifecycle_fixture.inc"
#endif
#if defined(GUI_FORMS_WINDOWS_PREPARED_TEXT_TEST)
#include "windows_prepared_text_fixture.inc"
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
                HostCapability::presentation_preferences |
                HostCapability::transient_surfaces |
                HostCapability::window_corners |
                HostCapability::window_state_reports,
            // Windows follows the PC keyboard convention (Ctrl+F4 closes a
            // document).
            pc_key_map()};
}

int run_windows(std::unique_ptr<Window> model, WindowsHostOptions options) {
    run_scale_override() = options.scale_override > 0.0 ? options.scale_override : 0.0;
    if (!model || !options.initially_visible || options.hide_on_close) return 2;
    const HRESULT com_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    enable_best_dpi_awareness();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW native_class{};
    native_class.cbSize = sizeof(native_class);
    // No CS_HREDRAW or CS_VREDRAW: the window decides what a resize
    // damages (Window::resize), and the system invalidates only what the
    // resize exposed.
    native_class.style = CS_DBLCLKS;
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
    const double initial_scale =
        options.scale_override > 0.0 ? options.scale_override : query_scale(nullptr);
    const Size initial_size = options.capture_size.has_value()
        ? *options.capture_size : options.initial_size;
    RECT frame{0, 0, static_cast<LONG>(std::ceil(initial_size.width * initial_scale)),
               static_cast<LONG>(std::ceil(initial_size.height * initial_scale))};
    // The frame's own metrics follow the monitor even when the content's
    // scale is overridden.
    adjust_client_frame(frame, style, 0, query_scale(nullptr));
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
    state.show_with(SW_SHOWNORMAL);
    UpdateWindow(window);
    if (options.close_after_launch_for_testing) {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }

    MSG message{};
    bool quit{};
    while (!quit) {
        // The live clock and the scheduler clock (audit N63), whichever
        // exist, in that order.
        std::array<HANDLE, 2> clocks{};
        DWORD handle_count = 0U;
        if (state.live_frame_clock() != nullptr) clocks[handle_count++] = state.live_frame_clock();
        if (state.scheduler_clock() != nullptr) clocks[handle_count++] = state.scheduler_clock();
        const DWORD wait = MsgWaitForMultipleObjectsEx(
            handle_count, handle_count == 0U ? nullptr : clocks.data(), INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_FAILED) break;
        if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + handle_count) {
            const HANDLE ready = clocks[wait - WAIT_OBJECT_0];
            if (ready == state.live_frame_clock()) state.mark_live_frame_due();
            else state.scheduler_clock_signalled();
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
        if (state.live_frame_clock() != nullptr &&
            WaitForSingleObject(state.live_frame_clock(), 0U) == WAIT_OBJECT_0) {
            state.mark_live_frame_due();
        }
        if (state.scheduler_clock() != nullptr &&
            WaitForSingleObject(state.scheduler_clock(), 0U) == WAIT_OBJECT_0) {
            state.scheduler_clock_signalled();
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
    // The run's Splash baker (Part S, phase 2), after the fonts it paints with and
    // before every window, so it outlives each tooltip that reads it.
    const gui_forms::render::SkiaSplashBakeRun splash_baking(run_fonts);
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
    run_scale_override() = windows.front().options.scale_override > 0.0 ? windows.front().options.scale_override
                                                                         : development_scale_override();
    // Development hook (GUI_FORMS_WINDOWS_AUTOMATION=1): a program run
    // through Application::run, whose portable options cannot ask for it,
    // answers the automation channel in every window (menu probes).
    if (environment_flag("GUI_FORMS_WINDOWS_AUTOMATION")) {
        for (WindowsApplicationWindow& entry : windows) entry.options.automation_enabled = true;
    }

    const HRESULT com_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    enable_best_dpi_awareness();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW native_class{};
    native_class.cbSize = sizeof(native_class);
    // No CS_HREDRAW or CS_VREDRAW: the window decides what a resize
    // damages (Window::resize), and the system invalidates only what the
    // resize exposed.
    native_class.style = CS_DBLCLKS;
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
                (*slot.state).show_with(windows[listed].tool_window ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
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
    while (!quit) {
        quit = (*run).turn(message, true);
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

#include "windows_splashscreen.inc"

} // namespace gui_forms::host
