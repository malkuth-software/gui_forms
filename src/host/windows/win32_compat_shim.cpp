#include <windows.h>

#include "gui_forms/c_api.h"
#include "gui_forms/c/gf_abi.h"

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_set>

namespace {

// Pointer types for the gui_forms_abi0.dll entry points this shim resolves by
// name at run time; it never links the ABI. Calling a function through a
// pointer to a different function type is undefined behaviour even where the
// calling convention agrees (an `int` return for the `gf_result` enum was
// reported by UBSan's -fsanitize=function), so each type spells the declared
// signature and the assertions hold it to the declaration in c_api.h. Only
// the declarations are used; nothing here imports the ABI's symbols.
using GetEndpointDc = gf_result (*)(std::uintptr_t, std::uintptr_t*);
using ReleaseEndpointDc = gf_result (*)(std::uintptr_t, std::uintptr_t);
using PublishEndpointDc = gf_result (*)(std::uintptr_t);
using BeginEndpointWrite = gf_result (*)(std::uintptr_t, std::uint64_t*);
using EndEndpointWrite = gf_result (*)(std::uint64_t, std::uint32_t);
static_assert(std::is_same_v<GetEndpointDc, decltype(&gf_windows_paint_endpoint_get_dc_v1)>);
static_assert(std::is_same_v<ReleaseEndpointDc, decltype(&gf_windows_paint_endpoint_release_dc_v1)>);
static_assert(std::is_same_v<PublishEndpointDc, decltype(&gf_windows_paint_endpoint_publish_dc_v1)>);
static_assert(std::is_same_v<BeginEndpointWrite, decltype(&gf_windows_paint_endpoint_begin_write_v1)>);
static_assert(std::is_same_v<EndEndpointWrite, decltype(&gf_windows_paint_endpoint_end_write_v1)>);

std::mutex bitmap_orientation_mutex;
std::unordered_set<HBITMAP> top_down_bitmaps;
// DC classifications are keyed by handle value, and GDI recycles DC values.
// A DC destroyed outside the shim leaves its classification behind, so a
// recycled value may be recorded as ordinary although it is now an endpoint
// DC. GetDC and BeginPaint, the consumer's route to an endpoint DC,
// reclassify the value; CreateCompatibleBitmap, whose orientation choice
// lasts for the bitmap's lifetime, asks the ABI instead of trusting an
// ordinary record. Drawing calls trust the record: re-asking the ABI there
// would construct an error message per call for every ordinary DC.
std::mutex endpoint_dc_mutex;
std::unordered_set<HDC> endpoint_dcs;
std::unordered_set<HDC> ordinary_dcs;

enum class DcKind : std::uint8_t { unknown, endpoint, ordinary };

DcKind cached_dc_kind(HDC dc) noexcept {
    if (dc == nullptr) return DcKind::ordinary;
    std::scoped_lock lock(endpoint_dc_mutex);
    if (endpoint_dcs.contains(dc)) return DcKind::endpoint;
    if (ordinary_dcs.contains(dc)) return DcKind::ordinary;
    return DcKind::unknown;
}

void remember_dc(HDC dc, DcKind kind) noexcept {
    if (dc == nullptr) return;
    try {
        std::scoped_lock lock(endpoint_dc_mutex);
        endpoint_dcs.erase(dc);
        ordinary_dcs.erase(dc);
        (kind == DcKind::endpoint ? endpoint_dcs : ordinary_dcs).insert(dc);
    } catch (...) {
        // Classification is a fast-path cache. The ABI lookup remains the
        // correctness fallback for an unclassified DC.
    }
}

void forget_dc(HDC dc) noexcept {
    if (dc == nullptr) return;
    std::scoped_lock lock(endpoint_dc_mutex);
    endpoint_dcs.erase(dc);
    ordinary_dcs.erase(dc);
}

bool tracked_top_down(HBITMAP bitmap) noexcept {
    if (bitmap == nullptr) return false;
    std::scoped_lock lock(bitmap_orientation_mutex);
    return top_down_bitmaps.contains(bitmap);
}

void remember_top_down(HBITMAP bitmap) noexcept {
    if (bitmap == nullptr) return;
    try {
        std::scoped_lock lock(bitmap_orientation_mutex);
        top_down_bitmaps.insert(bitmap);
    } catch (...) {
        // The optimization is optional. An untracked bitmap retains the
        // ordinary GDI fallback rather than weakening object lifetime.
    }
}

void forget_bitmap(HGDIOBJ object) noexcept {
    if (object == nullptr) return;
    std::scoped_lock lock(bitmap_orientation_mutex);
    top_down_bitmaps.erase(reinterpret_cast<HBITMAP>(object));
}

template <typename Function>
Function forms_entry(const char* name) noexcept {
    HMODULE module = GetModuleHandleW(L"gui_forms_abi0.dll");
    if (module == nullptr) return nullptr;
    FARPROC raw = GetProcAddress(module, name);
    Function result{};
    static_assert(sizeof(result) == sizeof(raw));
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}

// The paint endpoint table of the stable ABI (gui_forms_abi1.dll), for a
// process that loads the stable library instead of gui_forms_abi0.dll (the
// swap-in launcher's facade does). Resolved once, by name, as the ABI0
// entries are; present is false when neither the library nor the table is
// there, and the shim then falls through to the system for every handle.
struct StableEndpoints final {
    bool present{};
    gf_windows_paint_endpoint_table table{};
};

using GetStableTable = gf_status(GF_CALL*)(gf_table_id, std::uint32_t, std::uint32_t, void*, std::uint64_t);
static_assert(std::is_same_v<GetStableTable, decltype(&gf_abi_get_table)>);

StableEndpoints load_stable_endpoints() noexcept {
    StableEndpoints loaded{};
    HMODULE module = GetModuleHandleW(L"gui_forms_abi1.dll");
    if (module == nullptr) return loaded;
    FARPROC raw = GetProcAddress(module, "gf_abi_get_table");
    GetStableTable get_table{};
    static_assert(sizeof(get_table) == sizeof(raw));
    std::memcpy(&get_table, &raw, sizeof(get_table));
    if (get_table == nullptr) return loaded;
    const gf_status negotiated = get_table(GF_TABLE_WINDOWS_PAINT_ENDPOINT, GF_ABI_MAJOR, GF_ABI_MINOR,
                                           &loaded.table, sizeof(loaded.table));
    loaded.present = negotiated == GF_STATUS_OK && loaded.table.get_dc != nullptr &&
        loaded.table.release_dc != nullptr && loaded.table.publish_dc != nullptr &&
        loaded.table.begin_write != nullptr && loaded.table.end_write != nullptr;
    return loaded;
}

const StableEndpoints& stable_endpoints() noexcept {
    static const StableEndpoints loaded = load_stable_endpoints();
    return loaded;
}

bool endpoint_dc(HWND window, HDC* output) noexcept {
    static const GetEndpointDc entry = forms_entry<GetEndpointDc>(
        "gf_windows_paint_endpoint_get_dc_v1");
    if (output == nullptr) return false;
    std::uintptr_t value{};
    if (entry != nullptr) {
        if (entry(reinterpret_cast<std::uintptr_t>(window), &value) != GF_OK || value == 0U) return false;
    } else {
        const StableEndpoints& stable = stable_endpoints();
        if (!stable.present) return false;
        std::uint64_t device{};
        if (stable.table.get_dc(reinterpret_cast<std::uintptr_t>(window), &device) != GF_STATUS_OK ||
            device == 0U) return false;
        value = static_cast<std::uintptr_t>(device);
    }
    *output = reinterpret_cast<HDC>(value);
    remember_dc(*output, DcKind::endpoint);
    return true;
}

bool release_endpoint_dc(HWND window, HDC dc) noexcept {
    static const ReleaseEndpointDc entry = forms_entry<ReleaseEndpointDc>(
        "gf_windows_paint_endpoint_release_dc_v1");
    if (entry != nullptr) {
        return entry(reinterpret_cast<std::uintptr_t>(window), reinterpret_cast<std::uintptr_t>(dc)) == GF_OK;
    }
    const StableEndpoints& stable = stable_endpoints();
    return stable.present &&
        stable.table.release_dc(reinterpret_cast<std::uintptr_t>(window), reinterpret_cast<std::uintptr_t>(dc)) ==
            GF_STATUS_OK;
}

void publish_endpoint_dc(HDC dc) noexcept {
    if (cached_dc_kind(dc) == DcKind::ordinary) return;
    static const PublishEndpointDc entry = forms_entry<PublishEndpointDc>(
        "gf_windows_paint_endpoint_publish_dc_v1");
    if (entry != nullptr) {
        static_cast<void>(entry(reinterpret_cast<std::uintptr_t>(dc)));
        return;
    }
    const StableEndpoints& stable = stable_endpoints();
    if (stable.present) static_cast<void>(stable.table.publish_dc(reinterpret_cast<std::uintptr_t>(dc)));
}

bool request_endpoint_write(HDC dc, std::uint64_t* lease) noexcept {
    static const BeginEndpointWrite entry = forms_entry<BeginEndpointWrite>(
        "gf_windows_paint_endpoint_begin_write_v1");
    if (lease == nullptr) return false;
    if (entry != nullptr) {
        return entry(reinterpret_cast<std::uintptr_t>(dc), lease) == GF_OK && *lease != 0U;
    }
    const StableEndpoints& stable = stable_endpoints();
    return stable.present && stable.table.begin_write(reinterpret_cast<std::uintptr_t>(dc), lease) == GF_STATUS_OK &&
        *lease != 0U;
}

bool begin_endpoint_write(HDC dc, std::uint64_t* lease) noexcept {
    const DcKind kind = cached_dc_kind(dc);
    if (kind == DcKind::ordinary) return false;
    const bool acquired = request_endpoint_write(dc, lease);
    if (kind == DcKind::unknown) {
        remember_dc(dc, acquired ? DcKind::endpoint : DcKind::ordinary);
    }
    return acquired;
}

// Asks the ABI even when the cache records dc as ordinary, then records the
// answer. Used where a stale record would have a lasting effect.
bool begin_endpoint_write_revalidated(HDC dc, std::uint64_t* lease) noexcept {
    if (dc == nullptr) return false;
    const bool acquired = request_endpoint_write(dc, lease);
    remember_dc(dc, acquired ? DcKind::endpoint : DcKind::ordinary);
    return acquired;
}

void end_endpoint_write(std::uint64_t lease, bool publish) noexcept {
    static const EndEndpointWrite entry = forms_entry<EndEndpointWrite>(
        "gf_windows_paint_endpoint_end_write_v1");
    if (lease == 0U) return;
    if (entry != nullptr) {
        static_cast<void>(entry(lease, publish ? 1U : 0U));
        return;
    }
    const StableEndpoints& stable = stable_endpoints();
    if (stable.present) static_cast<void>(stable.table.end_write(lease, publish ? 1U : 0U));
}

bool default_text_mapping(HDC dc) noexcept {
    POINT window_origin{};
    POINT viewport_origin{};
    return dc != nullptr && GetMapMode(dc) == MM_TEXT &&
        GetWindowOrgEx(dc, &window_origin) &&
        GetViewportOrgEx(dc, &viewport_origin) &&
        window_origin.x == 0 && window_origin.y == 0 &&
        viewport_origin.x == 0 && viewport_origin.y == 0;
}

bool selected_dib32(HDC dc, HBITMAP* bitmap, DIBSECTION* section) noexcept {
    if (dc == nullptr || bitmap == nullptr || section == nullptr) return false;
    const HBITMAP selected = reinterpret_cast<HBITMAP>(
        GetCurrentObject(dc, OBJ_BITMAP));
    if (selected == nullptr) return false;
    DIBSECTION candidate{};
    const int copied = GetObjectW(selected, sizeof(candidate), &candidate);
    if (copied < static_cast<int>(sizeof(DIBSECTION)) ||
        candidate.dsBm.bmBits == nullptr ||
        candidate.dsBm.bmBitsPixel != 32 ||
        candidate.dsBm.bmWidthBytes <= 0 ||
        candidate.dsBm.bmWidth <= 0 ||
        candidate.dsBm.bmHeight == 0 ||
        candidate.dsBmih.biHeight == 0) {
        return false;
    }
    if (tracked_top_down(selected)) {
        candidate.dsBmih.biHeight =
            -std::abs(candidate.dsBmih.biHeight);
    }
    *bitmap = selected;
    *section = candidate;
    return true;
}

std::byte* logical_dib_row(DIBSECTION& section, int y) noexcept {
    const int height = std::abs(section.dsBm.bmHeight);
    const bool top_down = section.dsBmih.biHeight < 0;
    const int memory_y = top_down ? y : height - 1 - y;
    return static_cast<std::byte*>(section.dsBm.bmBits) +
        static_cast<std::size_t>(memory_y) *
            static_cast<std::size_t>(section.dsBm.bmWidthBytes);
}

bool try_dib32_srccopy(HDC destination, int destination_x, int destination_y,
                       int width, int height, HDC source, int source_x,
                       int source_y, DWORD operation, BOOL* result) noexcept {
    if (result == nullptr || operation != SRCCOPY || width <= 0 || height <= 0 ||
        !default_text_mapping(destination) || !default_text_mapping(source)) {
        return false;
    }
    RECT destination_clip{};
    const int clip_kind = GetClipBox(destination, &destination_clip);
    if (clip_kind == ERROR || clip_kind == COMPLEXREGION) return false;
    if (clip_kind == NULLREGION) {
        *result = TRUE;
        return true;
    }

    HBITMAP destination_bitmap{};
    HBITMAP source_bitmap{};
    DIBSECTION destination_section{};
    DIBSECTION source_section{};
    if (!selected_dib32(destination, &destination_bitmap,
                        &destination_section) ||
        !selected_dib32(source, &source_bitmap, &source_section)) {
        return false;
    }
    if (destination_bitmap != source_bitmap) return false;

    // Compatibility bitmaps derived from the GUI.Forms endpoint are explicit
    // top-down DIB sections. Wine's overlapping self-BitBlt path does not
    // reliably handle that object, while forcing GdiFlush before every direct
    // scroll makes a high-rate compatibility consumer flush and copy its
    // entire completed history for every scanline. The producer already owns
    // the DIB memory synchronously; keep
    // the overlap-safe memory move. Unknown/native DIBs retain GDI's flush
    // boundary before direct access.
    if (!tracked_top_down(destination_bitmap) && GdiFlush() == FALSE) {
        return false;
    }

    int copy_width = width;
    int copy_height = height;
    if (destination_x < destination_clip.left) {
        const int delta = destination_clip.left - destination_x;
        destination_x += delta;
        source_x += delta;
        copy_width -= delta;
    }
    if (destination_y < destination_clip.top) {
        const int delta = destination_clip.top - destination_y;
        destination_y += delta;
        source_y += delta;
        copy_height -= delta;
    }
    copy_width = (std::min)(copy_width,
        static_cast<int>(destination_clip.right) - destination_x);
    copy_height = (std::min)(copy_height,
        static_cast<int>(destination_clip.bottom) - destination_y);

    if (destination_x < 0) {
        source_x -= destination_x;
        copy_width += destination_x;
        destination_x = 0;
    }
    if (destination_y < 0) {
        source_y -= destination_y;
        copy_height += destination_y;
        destination_y = 0;
    }
    if (source_x < 0) {
        destination_x -= source_x;
        copy_width += source_x;
        source_x = 0;
    }
    if (source_y < 0) {
        destination_y -= source_y;
        copy_height += source_y;
        source_y = 0;
    }
    copy_width = (std::min)(copy_width,
        static_cast<int>(destination_section.dsBm.bmWidth) - destination_x);
    copy_width = (std::min)(copy_width,
        static_cast<int>(source_section.dsBm.bmWidth) - source_x);
    copy_height = (std::min)(copy_height,
        std::abs(static_cast<int>(destination_section.dsBm.bmHeight)) -
            destination_y);
    copy_height = (std::min)(copy_height,
        std::abs(static_cast<int>(source_section.dsBm.bmHeight)) - source_y);
    if (copy_width <= 0 || copy_height <= 0) {
        *result = TRUE;
        return true;
    }

    const std::size_t copy_bytes =
        static_cast<std::size_t>(copy_width) * 4U;
    const bool complete_rows = destination_x == 0 && source_x == 0 &&
        copy_bytes == static_cast<std::size_t>(destination_section.dsBm.bmWidthBytes) &&
        copy_bytes == static_cast<std::size_t>(source_section.dsBm.bmWidthBytes) &&
        (destination_section.dsBmih.biHeight < 0) ==
            (source_section.dsBmih.biHeight < 0);
    if (complete_rows) {
        std::byte* destination_first =
            logical_dib_row(destination_section, destination_y);
        std::byte* destination_last =
            logical_dib_row(destination_section, destination_y + copy_height - 1);
        std::byte* source_first = logical_dib_row(source_section, source_y);
        std::byte* source_last =
            logical_dib_row(source_section, source_y + copy_height - 1);
        std::memmove((std::min)(destination_first, destination_last),
                     (std::min)(source_first, source_last),
                     copy_bytes * static_cast<std::size_t>(copy_height));
        *result = TRUE;
        return true;
    }
    const bool reverse = destination_y > source_y &&
        destination_y < source_y + copy_height;
    for (int index = 0; index < copy_height; ++index) {
        const int row = reverse ? copy_height - 1 - index : index;
        std::byte* destination_row =
            logical_dib_row(destination_section, destination_y + row) +
            static_cast<std::size_t>(destination_x) * 4U;
        std::byte* source_row =
            logical_dib_row(source_section, source_y + row) +
            static_cast<std::size_t>(source_x) * 4U;
        std::memmove(destination_row, source_row, copy_bytes);
    }
    *result = TRUE;
    return true;
}

void trace(const char* operation, const void* first,
           const void* second = nullptr, long result = 0,
           int width = 0, int height = 0) noexcept {
    const DWORD error = GetLastError();
    static LONG count{};
    char enabled[2]{};
    if (GetEnvironmentVariableA("GUI_FORMS_TRACE_GDI_SHIM", enabled,
                                sizeof(enabled)) == 0 || enabled[0] != '1' ||
        InterlockedIncrement(&count) > 4096) {
        SetLastError(error);
        return;
    }
    std::fprintf(stderr,
                 "gui-forms-gdi-shim=%s|sequence:%ld|first:%p|second:%p|"
                 "result:%ld|size:%dx%d|thread:%lu|last-error:%lu\n",
                 operation, count, first, second, result, width, height,
                 GetCurrentThreadId(), error);
    SetLastError(error);
}

} // namespace

extern "C" BOOL WINAPI gf_compat_IsWindow(HWND window) {
    HDC endpoint{};
    return endpoint_dc(window, &endpoint) ? TRUE : ::IsWindow(window);
}

extern "C" BOOL WINAPI gf_compat_IsWindowEnabled(HWND window) {
    HDC endpoint{};
    if (endpoint_dc(window, &endpoint)) return FALSE;
    return ::IsWindowEnabled(window);
}

extern "C" BOOL WINAPI gf_compat_IsWindowVisible(HWND window) {
    HDC endpoint{};
    if (endpoint_dc(window, &endpoint)) return FALSE;
    return ::IsWindowVisible(window);
}

extern "C" BOOL WINAPI gf_compat_GetLayeredWindowAttributes(
    HWND window, COLORREF* color_key, BYTE* alpha, DWORD* flags) {
    HDC endpoint{};
    if (endpoint_dc(window, &endpoint)) {
        if (color_key != nullptr) *color_key = 0;
        if (alpha != nullptr) *alpha = 0;
        if (flags != nullptr) *flags = LWA_ALPHA;
        return TRUE;
    }
    return ::GetLayeredWindowAttributes(window, color_key, alpha, flags);
}

extern "C" BOOL WINAPI gf_compat_GetClientRect(HWND window, RECT* bounds) {
    HDC endpoint{};
    if (endpoint_dc(window, &endpoint)) {
        if (bounds == nullptr) return FALSE;
        const HBITMAP bitmap = reinterpret_cast<HBITMAP>(
            GetCurrentObject(endpoint, OBJ_BITMAP));
        BITMAP details{};
        if (bitmap == nullptr ||
            GetObjectW(bitmap, sizeof(details), &details) != sizeof(details)) {
            return FALSE;
        }
        *bounds = RECT{0, 0, details.bmWidth, std::abs(details.bmHeight)};
        return TRUE;
    }
    return ::GetClientRect(window, bounds);
}

extern "C" HWND WINAPI gf_compat_GetParent(HWND window) {
    HDC endpoint{};
    if (endpoint_dc(window, &endpoint)) return nullptr;
    return ::GetParent(window);
}

extern "C" HDC WINAPI gf_compat_GetDC(HWND window) {
    HDC dc{};
    const bool endpoint = endpoint_dc(window, &dc);
    if (!endpoint) dc = ::GetDC(window);
    trace(endpoint ? "get-dc-endpoint" : "get-dc-forward", window, dc,
          dc != nullptr ? 1 : 0);
    return dc;
}

extern "C" int WINAPI gf_compat_ReleaseDC(HWND window, HDC dc) {
    return release_endpoint_dc(window, dc) ? 1 : ::ReleaseDC(window, dc);
}

extern "C" BOOL WINAPI gf_compat_BitBlt(
    HDC destination, int x, int y, int width, int height,
    HDC source, int source_x, int source_y, DWORD operation) {
    std::uint64_t lease{};
    const bool leased = begin_endpoint_write(destination, &lease);
    BOOL result{};
    const bool direct_dib = try_dib32_srccopy(
        destination, x, y, width, height, source, source_x, source_y,
        operation, &result);
    if (!direct_dib) {
        result = ::BitBlt(destination, x, y, width, height,
                          source, source_x, source_y, operation);
    }
    trace(direct_dib ? "bitblt-dib32" : "bitblt", destination, source,
          result, width, height);
    if (leased) end_endpoint_write(lease, result != FALSE);
    return result;
}

extern "C" HBITMAP WINAPI gf_compat_CreateCompatibleBitmap(
    HDC dc, int width, int height) {
    std::uint64_t endpoint_lease{};
    const HBITMAP selected = dc == nullptr ? nullptr :
        reinterpret_cast<HBITMAP>(GetCurrentObject(dc, OBJ_BITMAP));
    // GDI's own compatible bitmap for a DC holding a top-down DIB section is
    // bottom-up, so misclassifying an endpoint DC here would give the
    // producer the opposite orientation for the bitmap's whole lifetime.
    const bool top_down =
        begin_endpoint_write_revalidated(dc, &endpoint_lease) ||
        tracked_top_down(selected);
    HBITMAP result{};
    if (top_down && width > 0 && height > 0) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels{};
        result = CreateDIBSection(
            dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (result != nullptr && pixels != nullptr) {
            remember_top_down(result);
        }
    }
    if (result == nullptr) result = ::CreateCompatibleBitmap(dc, width, height);
    if (endpoint_lease != 0U) end_endpoint_write(endpoint_lease, false);
    trace("create-compatible-bitmap", dc, result,
          result != nullptr ? 1 : 0, width, height);
    return result;
}

extern "C" HDC WINAPI gf_compat_CreateCompatibleDC(HDC dc) {
    HDC result = ::CreateCompatibleDC(dc);
    remember_dc(result, DcKind::ordinary);
    trace("create-compatible-dc", dc, result, result != nullptr ? 1 : 0);
    return result;
}

extern "C" BOOL WINAPI gf_compat_DeleteObject(HGDIOBJ object) {
    const BOOL result = ::DeleteObject(object);
    if (result != FALSE) forget_bitmap(object);
    trace("delete-object", object, nullptr, result);
    return result;
}

extern "C" HGDIOBJ WINAPI gf_compat_GetStockObject(int object) {
    return ::GetStockObject(object);
}

extern "C" HBRUSH WINAPI gf_compat_CreateSolidBrush(COLORREF color) {
    return ::CreateSolidBrush(color);
}

extern "C" COLORREF WINAPI gf_compat_GetPixel(HDC dc, int x, int y) {
    return ::GetPixel(dc, x, y);
}

extern "C" BOOL WINAPI gf_compat_MoveToEx(
    HDC dc, int x, int y, LPPOINT previous) {
    return ::MoveToEx(dc, x, y, previous);
}

extern "C" BOOL WINAPI gf_compat_PolylineTo(
    HDC dc, const POINT* points, DWORD count) {
    return ::PolylineTo(dc, points, count);
}

extern "C" BOOL WINAPI gf_compat_Polygon(
    HDC dc, const POINT* points, int count) {
    return ::Polygon(dc, points, count);
}

extern "C" HGDIOBJ WINAPI gf_compat_SelectObject(HDC dc, HGDIOBJ object) {
    HGDIOBJ result = ::SelectObject(dc, object);
    trace("select-object", dc, object,
          result != nullptr && result != HGDI_ERROR ? 1 : 0);
    return result;
}

extern "C" BOOL WINAPI gf_compat_DeleteDC(HDC dc) {
    const BOOL result = ::DeleteDC(dc);
    if (result != FALSE) forget_dc(dc);
    trace("delete-dc", dc, nullptr, result);
    return result;
}

extern "C" COLORREF WINAPI gf_compat_SetDCBrushColor(HDC dc, COLORREF color) {
    return ::SetDCBrushColor(dc, color);
}

extern "C" COLORREF WINAPI gf_compat_SetDCPenColor(HDC dc, COLORREF color) {
    return ::SetDCPenColor(dc, color);
}

extern "C" COLORREF WINAPI gf_compat_SetPixel(
    HDC dc, int x, int y, COLORREF color) {
    std::uint64_t lease{};
    const bool leased = begin_endpoint_write(dc, &lease);
    const COLORREF result = ::SetPixel(dc, x, y, color);
    if (leased) end_endpoint_write(lease, result != CLR_INVALID);
    return result;
}

extern "C" int WINAPI gf_compat_SetStretchBltMode(HDC dc, int mode) {
    return ::SetStretchBltMode(dc, mode);
}

extern "C" BOOL WINAPI gf_compat_StretchBlt(
    HDC destination, int x, int y, int width, int height,
    HDC source, int source_x, int source_y, int source_width,
    int source_height, DWORD operation) {
    std::uint64_t lease{};
    const bool leased = begin_endpoint_write(destination, &lease);
    const BOOL result = ::StretchBlt(
        destination, x, y, width, height, source, source_x, source_y,
        source_width, source_height, operation);
    if (leased) end_endpoint_write(lease, result != FALSE);
    return result;
}

extern "C" int WINAPI gf_compat_StretchDIBits(
    HDC destination, int x, int y, int width, int height,
    int source_x, int source_y, int source_width, int source_height,
    const void* pixels, const BITMAPINFO* info, UINT usage, DWORD operation) {
    std::uint64_t lease{};
    const bool leased = begin_endpoint_write(destination, &lease);
    const int result = ::StretchDIBits(
        destination, x, y, width, height, source_x, source_y,
        source_width, source_height, pixels, info, usage, operation);
    if (leased) {
        end_endpoint_write(lease, result != static_cast<int>(GDI_ERROR));
    }
    return result;
}

extern "C" int WINAPI gf_compat_FillRect(
    HDC dc, const RECT* rectangle, HBRUSH brush) {
    std::uint64_t lease{};
    const bool leased = begin_endpoint_write(dc, &lease);
    const int result = ::FillRect(dc, rectangle, brush);
    if (leased) end_endpoint_write(lease, result != 0);
    return result;
}

extern "C" int WINAPI gf_compat_FrameRect(
    HDC dc, const RECT* rectangle, HBRUSH brush) {
    std::uint64_t lease{};
    const bool leased = begin_endpoint_write(dc, &lease);
    const int result = ::FrameRect(dc, rectangle, brush);
    if (leased) end_endpoint_write(lease, result != 0);
    return result;
}

extern "C" int WINAPI gf_compat_ScrollWindowEx(
    HWND window, int dx, int dy, const RECT* scroll, const RECT* clip,
    HRGN update_region, LPRECT update_rectangle, UINT flags) {
    HDC dc{};
    if (!endpoint_dc(window, &dc)) {
        return ::ScrollWindowEx(window, dx, dy, scroll, clip, update_region,
                                update_rectangle, flags);
    }
    static_cast<void>(flags);
    RECT surface_bounds{};
    BITMAP bitmap{};
    const HGDIOBJ selected = GetCurrentObject(dc, OBJ_BITMAP);
    if (selected == nullptr || GetObjectW(selected, sizeof(bitmap), &bitmap) == 0) {
        SetLastError(ERROR_INVALID_HANDLE);
        return ERROR;
    }
    surface_bounds.right = bitmap.bmWidth;
    surface_bounds.bottom = bitmap.bmHeight;
    const RECT* effective_scroll = scroll != nullptr ? scroll : &surface_bounds;
    const RECT* effective_clip = clip != nullptr ? clip : &surface_bounds;
    std::uint64_t lease{};
    const bool leased = begin_endpoint_write(dc, &lease);
    const BOOL result = ::ScrollDC(dc, dx, dy, effective_scroll, effective_clip,
                                   update_region, update_rectangle);
    trace("scroll", window, dc, result, dx, dy);
    if (leased) end_endpoint_write(lease, result != FALSE);
    if (result == FALSE) return ERROR;
    if (update_region != nullptr) {
        RECT update_bounds{};
        return GetRgnBox(update_region, &update_bounds);
    }
    return dx == 0 && dy == 0 ? NULLREGION : SIMPLEREGION;
}

extern "C" HDC WINAPI gf_compat_BeginPaint(
    HWND window, LPPAINTSTRUCT paint) {
    HDC dc{};
    if (!endpoint_dc(window, &dc)) return ::BeginPaint(window, paint);
    if (paint != nullptr) {
        std::memset(paint, 0, sizeof(*paint));
        (*paint).hdc = dc;
    }
    return dc;
}

extern "C" BOOL WINAPI gf_compat_EndPaint(
    HWND window, const PAINTSTRUCT* paint) {
    HDC dc{};
    if (!endpoint_dc(window, &dc)) return ::EndPaint(window, paint);
    publish_endpoint_dc(dc);
    return TRUE;
}

extern "C" LRESULT WINAPI gf_compat_SendMessageW(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    HDC dc{};
    if (endpoint_dc(window, &dc)) {
        static_cast<void>(wparam);
        static_cast<void>(lparam);
        switch (message) {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_PAINT:
        case 0x83f1U: // GUI.Forms explicit compatibility-present boundary.
            publish_endpoint_dc(dc);
            return 0;
        default:
            // A virtual handle has no operating-system WndProc. Unsupported
            // messages retain DefWindowProc's neutral result without creating
            // a hidden window or entering native input state.
            return 0;
        }
    }
    return ::SendMessageW(window, message, wparam, lparam);
}

extern "C" BOOL WINAPI gf_compat_GdiAlphaBlend(
    HDC destination, int x, int y, int width, int height,
    HDC source, int source_x, int source_y, int source_width,
    int source_height, BLENDFUNCTION blend) {
    std::uint64_t lease{};
    const bool leased = begin_endpoint_write(destination, &lease);
    const BOOL result = ::GdiAlphaBlend(
        destination, x, y, width, height, source, source_x, source_y,
        source_width, source_height, blend);
    if (leased) end_endpoint_write(lease, result != FALSE);
    return result;
}
