#include "drawing_platform.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "gui_forms/c/gf_abi.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gui_drawing::abi::platform {
namespace {

struct HdcLease final {
    Bitmap* bitmap{};
    std::thread::id owner;
    HDC device{};
    HBITMAP dib{};
    HGDIOBJ previous{};
    // The DIB's BGRA pixels, byte_count bytes, owned by dib: the bitmap's
    // own format, so acquire and release copy them as they are.
    void* pixels{};
    std::size_t byte_count{};
};

std::mutex lease_mutex;
std::unordered_map<std::uint64_t, HdcLease> leases;
std::uint64_t next_lease{1U};

BITMAPINFO bitmap_info(std::uint32_t width, std::uint32_t height);

std::uint8_t composite_channel(std::uint8_t foreground,
                               std::uint8_t backdrop,
                               std::uint8_t alpha) {
    return static_cast<std::uint8_t>(
        foreground +
        (static_cast<unsigned>(backdrop) * (255U - alpha) + 127U) / 255U);
}

struct CompatibilityUser32 final {
    // The compatibility shim exports user32's own names and signatures, so
    // the entry-point types come from user32's declarations (house style
    // section 2).
    using GetDc = decltype(&::GetDC);
    using ReleaseDc = decltype(&::ReleaseDC);
    using GetClientBounds = decltype(&::GetClientRect);

    HMODULE module{};
    GetDc get_dc{::GetDC};
    ReleaseDc release_dc{::ReleaseDC};
    GetClientBounds get_client_rect{::GetClientRect};

    CompatibilityUser32() {
        module = GetModuleHandleW(L"gui_forms_win32_compat.dll");
        if (module == nullptr) module = LoadLibraryW(L"gui_forms_win32_compat.dll");
        if (module == nullptr) return;
        if (const FARPROC value = GetProcAddress(module, "GetDC")) {
            get_dc = reinterpret_cast<GetDc>(value);
        }
        if (const FARPROC value = GetProcAddress(module, "ReleaseDC")) {
            release_dc = reinterpret_cast<ReleaseDc>(value);
        }
        if (const FARPROC value = GetProcAddress(module, "GetClientRect")) {
            get_client_rect = reinterpret_cast<GetClientBounds>(value);
        }
    }
};

CompatibilityUser32& compatibility_user32() {
    static CompatibilityUser32 api;
    return api;
}

struct CaptureStaging final {
    HDC device{};
    HBITMAP dib{};
    HGDIOBJ previous{};
    void* pixels{};
    std::uint32_t width{};
    std::uint32_t height{};

    ~CaptureStaging() {
        if (device != nullptr && previous != nullptr && previous != HGDI_ERROR) {
            SelectObject(device, previous);
        }
        if (device != nullptr) DeleteDC(device);
        if (dib != nullptr) DeleteObject(dib);
    }

    void ensure(std::uint32_t requested_width, std::uint32_t requested_height) {
        if (device != nullptr && width == requested_width && height == requested_height) {
            return;
        }
        if (device != nullptr && previous != nullptr && previous != HGDI_ERROR) {
            SelectObject(device, previous);
        }
        if (device != nullptr) DeleteDC(device);
        if (dib != nullptr) DeleteObject(dib);
        device = nullptr;
        dib = nullptr;
        previous = nullptr;
        pixels = nullptr;
        width = 0U;
        height = 0U;

        BITMAPINFO info = bitmap_info(requested_width, requested_height);
        dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                               &pixels, nullptr, 0U);
        device = dib == nullptr ? nullptr : CreateCompatibleDC(nullptr);
        previous = device == nullptr ? nullptr : SelectObject(device, dib);
        if (pixels == nullptr || previous == nullptr || previous == HGDI_ERROR) {
            if (device != nullptr) DeleteDC(device);
            if (dib != nullptr) DeleteObject(dib);
            device = nullptr;
            dib = nullptr;
            previous = nullptr;
            pixels = nullptr;
            throw std::runtime_error("native surface staging allocation failed");
        }
        width = requested_width;
        height = requested_height;
    }
};

thread_local CaptureStaging capture_staging;

BITMAPINFO bitmap_info(std::uint32_t width, std::uint32_t height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return info;
}

} // namespace

gd_result export_hbitmap(Bitmap& bitmap, Color background, std::uintptr_t& output) {
    const ImageSnapshot snapshot = bitmap.snapshot();
    BITMAPINFO info = bitmap_info(snapshot.width, snapshot.height);
    void* pixels = nullptr;
    HBITMAP native = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                      &pixels, nullptr, 0U);
    if (native == nullptr || pixels == nullptr) {
        if (native != nullptr) DeleteObject(native);
        throw std::runtime_error("CreateDIBSection failed");
    }
    const std::uint8_t br = background.is_empty() ? 0U : background.red();
    const std::uint8_t bg = background.is_empty() ? 0U : background.green();
    const std::uint8_t bb = background.is_empty() ? 0U : background.blue();
    const std::span<const std::byte> source = snapshot.pixels();
    std::uint8_t* destination = static_cast<std::uint8_t*>(pixels);
    for (std::size_t offset = 0; offset < source.size(); offset += 4U) {
        const std::uint8_t blue = std::to_integer<std::uint8_t>(source[offset]);
        const std::uint8_t green =
            std::to_integer<std::uint8_t>(source[offset + 1U]);
        const std::uint8_t red = std::to_integer<std::uint8_t>(source[offset + 2U]);
        const std::uint8_t alpha =
            std::to_integer<std::uint8_t>(source[offset + 3U]);
        destination[offset] = composite_channel(blue, bb, alpha);
        destination[offset + 1U] = composite_channel(green, bg, alpha);
        destination[offset + 2U] = composite_channel(red, br, alpha);
        destination[offset + 3U] = 255U;
    }
    output = reinterpret_cast<std::uintptr_t>(native);
    return GD_OK;
}

gd_result import_hbitmap(std::uintptr_t source, std::unique_ptr<Bitmap>& output) {
    const HBITMAP native = reinterpret_cast<HBITMAP>(source);
    BITMAP details{};
    if (GetObjectW(native, sizeof(details), &details) != sizeof(details) ||
        details.bmWidth <= 0 || details.bmHeight == 0) {
        throw std::invalid_argument("native HBITMAP is invalid");
    }
    const std::uint32_t width = static_cast<std::uint32_t>(details.bmWidth);
    const std::uint32_t height = static_cast<std::uint32_t>(
        details.bmHeight < 0 ? -static_cast<std::int64_t>(details.bmHeight) :
                               details.bmHeight);
    std::unique_ptr<Bitmap> bitmap = std::make_unique<Bitmap>(
        width, height, PixelFormat::bgra32_premultiplied);
    BitmapLockView lock = (*bitmap).lock(BitmapLockMode::write);
    BITMAPINFO info = bitmap_info(width, height);
    HDC device = GetDC(nullptr);
    if (device == nullptr) throw std::runtime_error("GetDC failed");
    const int rows = GetDIBits(device, native, 0U, height,
                               lock.writable_data, &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, device);
    if (rows != static_cast<int>(height)) {
        (*bitmap).unlock(lock.token);
        throw std::runtime_error("GetDIBits failed");
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = reinterpret_cast<std::uint8_t*>(lock.writable_data) +
                    static_cast<std::size_t>(y) * lock.row_bytes;
        for (std::uint32_t x = 0; x < width; ++x) row[x * 4U + 3U] = 255U;
    }
    (*bitmap).unlock(lock.token);
    output = std::move(bitmap);
    return GD_OK;
}

// Zero-copy drawing on a program's own DIB section. When the device
// context Graphics.FromHdc wraps is a memory DC holding a 32-bit top-down
// DIB section, unclipped, that is not a paint endpoint's, the GUI.Drawing
// bitmap borrows the section's pixels: GDI and GUI.Drawing then draw the
// same memory, and a flush neither captures nor presents. A paint
// endpoint's DC is excluded: its publisher reads those pixels on another
// thread under a write lease, and drawing them outside one would tear the
// published frame.
struct BorrowableDib final {
    std::byte* pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_bytes{};
};

using GetStableTable = decltype(&gf_abi_get_table);

// The C ABI's paint endpoint table (gui_forms_abi1.dll), resolved by name.
struct EndpointProbe final {
    gf_windows_paint_endpoint_table table{};
    bool table_present{};
};

// Resolved on first use. The library is loaded before any endpoint exists
// (an endpoint is made through it), so a library absent here has no
// endpoints to exclude.
EndpointProbe load_endpoint_probe() noexcept {
    EndpointProbe probe{};
    const HMODULE stable = GetModuleHandleW(L"gui_forms_abi1.dll");
    if (stable != nullptr) {
        FARPROC raw = GetProcAddress(stable, "gf_abi_get_table");
        GetStableTable get_table{};
        static_assert(sizeof(get_table) == sizeof(raw));
        std::memcpy(&get_table, &raw, sizeof(get_table));
        if (get_table != nullptr) {
            const gf_status negotiated = get_table(GF_TABLE_WINDOWS_PAINT_ENDPOINT, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                   &probe.table, sizeof(probe.table));
            probe.table_present = negotiated == GF_STATUS_OK && probe.table.begin_write != nullptr &&
                probe.table.end_write != nullptr;
        }
    }
    return probe;
}

// True when device is a paint endpoint's DC: a write lease begins on it.
// The probe lease ends at once without publishing.
bool is_endpoint_dc(HDC device) noexcept {
    static const EndpointProbe probe = load_endpoint_probe();
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(device);
    if (probe.table_present) {
        gf_paint_write_lease lease{};
        if (probe.table.begin_write(static_cast<std::uint64_t>(value), &lease) == GF_STATUS_OK) {
            probe.table.end_write(lease, 0U);
            return true;
        }
    }
    return false;
}

// True when the section's memory starts at its top row. GetObject reports
// a DIB section's height as positive whichever way it was made, so the
// first pixel word is changed for a moment and read back through GDI at
// (0, 0): only a top-down section shows the change there. One section is
// remembered per thread (the waterfall flushes the same one every frame).
// Single-row sections have one orientation.
bool top_down_section(HDC device, HGDIOBJ selected, const DIBSECTION& section) noexcept {
    struct Remembered final {
        HGDIOBJ bitmap{};
        void* bits{};
        bool top_down{};
    };
    thread_local Remembered remembered{};
    if (remembered.bitmap == selected && remembered.bits == section.dsBm.bmBits) return remembered.top_down;
    bool top_down = true;
    if (section.dsBm.bmHeight > 1) {
        std::uint32_t* const first = static_cast<std::uint32_t*>(section.dsBm.bmBits);
        if (GdiFlush() == FALSE) return false;
        const std::uint32_t original = *first;
        const std::uint32_t probe = original ^ 0x00808080U;
        *first = probe;
        const COLORREF read = GetPixel(device, 0, 0);
        *first = original;
        // The DIB word is 0x00RRGGBB in memory order B, G, R; COLORREF is
        // 0x00BBGGRR.
        const COLORREF expected = RGB((probe >> 16) & 0xffU, (probe >> 8) & 0xffU, probe & 0xffU);
        top_down = read == expected;
    }
    remembered.bitmap = selected;
    remembered.bits = section.dsBm.bmBits;
    remembered.top_down = top_down;
    return top_down;
}

// Fills dib when device's selected bitmap can be borrowed; false otherwise
// (the copying path then serves the surface).
bool borrowable_dib(HDC device, BorrowableDib& dib) noexcept {
    if (std::getenv("GUI_DRAWING_NO_BORROW") != nullptr) return false;
    if (GetObjectType(device) != OBJ_MEMDC) return false;
    const HGDIOBJ selected = GetCurrentObject(device, OBJ_BITMAP);
    if (selected == nullptr) return false;
    DIBSECTION section{};
    if (GetObjectW(selected, sizeof(section), &section) != static_cast<int>(sizeof(section))) return false;
    if (section.dsBm.bmBits == nullptr || section.dsBm.bmBitsPixel != 32 ||
        section.dsBmih.biCompression != BI_RGB || section.dsBm.bmWidth <= 0 || section.dsBm.bmHeight <= 0) {
        return false;
    }
    if (section.dsBm.bmWidth > static_cast<LONG>(Bitmap::maximum_dimension) ||
        section.dsBm.bmHeight > static_cast<LONG>(Bitmap::maximum_dimension)) {
        return false;
    }
    // Only an unclipped DC: the borrowed bitmap is the whole section, and a
    // clipped one must keep GDI's clip.
    RECT clip{};
    if (GetClipBox(device, &clip) != SIMPLEREGION || clip.left != 0 || clip.top != 0 ||
        clip.right != section.dsBm.bmWidth || clip.bottom != section.dsBm.bmHeight) {
        return false;
    }
    if (!top_down_section(device, selected, section)) return false;
    if (is_endpoint_dc(device)) return false;
    dib.pixels = static_cast<std::byte*>(section.dsBm.bmBits);
    dib.width = static_cast<std::uint32_t>(section.dsBm.bmWidth);
    dib.height = static_cast<std::uint32_t>(section.dsBm.bmHeight);
    dib.row_bytes = static_cast<std::size_t>(section.dsBm.bmWidthBytes);
    return true;
}

// GDI clears the alpha byte of what it draws; the surface is opaque, so
// every pixel becomes alpha 255 before GUI.Drawing composites over it.
void force_opaque(std::byte* pixels, std::uint32_t width, std::uint32_t height,
                  std::size_t row_bytes) noexcept {
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint32_t* row = reinterpret_cast<std::uint32_t*>(pixels + static_cast<std::size_t>(y) * row_bytes);
        for (std::uint32_t x = 0; x < width; ++x) row[x] |= 0xff000000U;
    }
}

gd_result capture_surface(std::uintptr_t source, std::uint32_t kind,
                          CapturedSurface& output) {
    if (kind != GD_NATIVE_SURFACE_HWND) {
        BorrowableDib dib{};
        if (borrowable_dib(reinterpret_cast<HDC>(source), dib)) {
            if (GdiFlush() == FALSE) throw std::runtime_error("GdiFlush failed");
            force_opaque(dib.pixels, dib.width, dib.height, dib.row_bytes);
            output.bitmap = Bitmap::borrow(dib.width, dib.height, dib.row_bytes, dib.pixels);
            output.bounds = {0.0, 0.0, static_cast<double>(dib.width), static_cast<double>(dib.height)};
            return GD_OK;
        }
    }
    const bool window_surface = kind == GD_NATIVE_SURFACE_HWND;
    const HWND window = window_surface ? reinterpret_cast<HWND>(source) : nullptr;
    CompatibilityUser32& compatibility = compatibility_user32();
    HDC device = window_surface ? compatibility.get_dc(window) :
                                  reinterpret_cast<HDC>(source);
    if (device == nullptr) throw std::invalid_argument("surface has no device context");
    RECT bounds{};
    bool have_bounds = window_surface ?
        compatibility.get_client_rect(window, &bounds) != 0 :
                                        GetClipBox(device, &bounds) != ERROR;
    if (!have_bounds || bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        bounds = {0, 0, GetDeviceCaps(device, HORZRES), GetDeviceCaps(device, VERTRES)};
    }
    const std::int64_t width64 = static_cast<std::int64_t>(bounds.right) - bounds.left;
    const std::int64_t height64 = static_cast<std::int64_t>(bounds.bottom) - bounds.top;
    if (width64 <= 0 || height64 <= 0) {
        if (window_surface) compatibility.release_dc(window, device);
        throw std::invalid_argument("native surface has empty bounds");
    }
    if (width64 > Bitmap::maximum_dimension || height64 > Bitmap::maximum_dimension) {
        if (window_surface) compatibility.release_dc(window, device);
        throw std::length_error("native surface exceeds bitmap limits");
    }
    const std::uint32_t width = static_cast<std::uint32_t>(width64);
    const std::uint32_t height = static_cast<std::uint32_t>(height64);
    std::vector<std::byte> captured(static_cast<std::size_t>(width) * height * 4U);
    BITMAPINFO info = bitmap_info(width, height);
    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(device, &info, DIB_RGB_COLORS,
                                   &pixels, nullptr, 0U);
    HDC memory = dib == nullptr ? nullptr : CreateCompatibleDC(device);
    HGDIOBJ previous = memory == nullptr ? nullptr : SelectObject(memory, dib);
    const bool ok = previous != nullptr && previous != HGDI_ERROR &&
        BitBlt(memory, 0, 0, static_cast<int>(width), static_cast<int>(height),
               device, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT) != 0 &&
        // The copy is read from the DIB's memory below; GDI may hold the
        // BitBlt in this thread's batch until flushed.
        GdiFlush() != FALSE;
    if (std::getenv("GUI_DRAWING_TRACE_NATIVE_SURFACES") != nullptr && !ok) {
        std::fprintf(stderr,
                     "gui-drawing-native-surface=capture-failed|kind:%u|size:%ux%u|dc:%p|dib:%p|memory:%p|previous:%p|error:%lu\n",
                     kind, width, height, static_cast<void*>(device),
                     static_cast<void*>(dib), static_cast<void*>(memory),
                     previous, static_cast<unsigned long>(GetLastError()));
    }
    if (window_surface) compatibility.release_dc(window, device);
    if (!ok) {
        if (memory != nullptr && previous != nullptr && previous != HGDI_ERROR) {
            SelectObject(memory, previous);
        }
        if (memory != nullptr) DeleteDC(memory);
        if (dib != nullptr) DeleteObject(dib);
        throw std::runtime_error("BitBlt failed");
    }
    std::memcpy(captured.data(), pixels, captured.size());
    SelectObject(memory, previous);
    DeleteDC(memory);
    DeleteObject(dib);
    std::unique_ptr<Bitmap> bitmap = std::make_unique<Bitmap>(
        width, height, PixelFormat::bgra32_premultiplied);
    BitmapLockView lock = (*bitmap).lock(BitmapLockMode::write);
    std::memcpy(lock.writable_data, captured.data(), captured.size());
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = reinterpret_cast<std::uint8_t*>(lock.writable_data) +
                    static_cast<std::size_t>(y) * lock.row_bytes;
        for (std::uint32_t x = 0; x < width; ++x) row[x * 4U + 3U] = 255U;
    }
    (*bitmap).unlock(lock.token);
    output.bitmap = std::move(bitmap);
    output.bounds = {static_cast<double>(bounds.left), static_cast<double>(bounds.top),
                     static_cast<double>(width), static_cast<double>(height)};
    return GD_OK;
}

gd_result refresh_surface(std::uintptr_t source, std::uint32_t kind,
                          Bitmap& bitmap, RectF& output_bounds) {
    if (bitmap.borrows_memory()) {
        // The bitmap already is the surface: only GDI's batch and its
        // cleared alpha need settling. A section replaced or resized since
        // the capture is reported as changed, so the caller captures anew.
        BorrowableDib dib{};
        if (kind == GD_NATIVE_SURFACE_HWND || !borrowable_dib(reinterpret_cast<HDC>(source), dib) ||
            dib.pixels != bitmap.borrowed_memory() || dib.width != bitmap.width() ||
            dib.height != bitmap.height()) {
            throw std::invalid_argument("native surface dimensions changed");
        }
        if (GdiFlush() == FALSE) throw std::runtime_error("GdiFlush failed");
        BitmapLockView lock = bitmap.lock(BitmapLockMode::write);
        force_opaque(reinterpret_cast<std::byte*>(lock.writable_data), dib.width, dib.height, lock.row_bytes);
        bitmap.unlock(lock.token);
        output_bounds = {0.0, 0.0, static_cast<double>(dib.width), static_cast<double>(dib.height)};
        return GD_OK;
    }
    const bool window_surface = kind == GD_NATIVE_SURFACE_HWND;
    const HWND window = window_surface ? reinterpret_cast<HWND>(source) : nullptr;
    CompatibilityUser32& compatibility = compatibility_user32();
    HDC source_device = window_surface ? compatibility.get_dc(window) :
                                         reinterpret_cast<HDC>(source);
    if (source_device == nullptr) {
        throw std::invalid_argument("surface has no device context");
    }
    RECT bounds{};
    bool have_bounds = window_surface ?
        compatibility.get_client_rect(window, &bounds) != 0 :
                                        GetClipBox(source_device, &bounds) != ERROR;
    if (!have_bounds || bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        bounds = {0, 0, GetDeviceCaps(source_device, HORZRES),
                        GetDeviceCaps(source_device, VERTRES)};
    }
    const std::uint32_t width = static_cast<std::uint32_t>(bounds.right - bounds.left);
    const std::uint32_t height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
    if (width == 0U || height == 0U ||
        width != bitmap.width() || height != bitmap.height()) {
        if (window_surface) compatibility.release_dc(window, source_device);
        throw std::invalid_argument("native surface dimensions changed");
    }

    try {
        capture_staging.ensure(width, height);
        if (BitBlt(capture_staging.device, 0, 0,
                   static_cast<int>(width), static_cast<int>(height),
                   source_device, bounds.left, bounds.top,
                   SRCCOPY | CAPTUREBLT) == 0) {
            throw std::runtime_error("BitBlt failed");
        }
        // The staging DIB is read from memory next; a BitBlt still in this
        // thread's GDI batch would leave the previous refresh there, and its
        // stale pixels would be presented back over the newer surface.
        if (GdiFlush() == FALSE) {
            throw std::runtime_error("GdiFlush failed");
        }
        BitmapLockView lock = bitmap.lock(BitmapLockMode::write);
        try {
            const std::size_t packed_row_bytes = static_cast<std::size_t>(width) * 4U;
            const std::byte* source_bytes = static_cast<const std::byte*>(capture_staging.pixels);
            for (std::uint32_t y = 0; y < height; ++y) {
                std::byte* destination = lock.writable_data +
                    static_cast<std::size_t>(y) * lock.row_bytes;
                std::memcpy(destination,
                            source_bytes + static_cast<std::size_t>(y) * packed_row_bytes,
                            packed_row_bytes);
                std::uint8_t* row = reinterpret_cast<std::uint8_t*>(destination);
                for (std::uint32_t x = 0; x < width; ++x) {
                    row[x * 4U + 3U] = 255U;
                }
            }
        } catch (...) {
            bitmap.unlock(lock.token);
            throw;
        }
        bitmap.unlock(lock.token);
    } catch (...) {
        if (window_surface) compatibility.release_dc(window, source_device);
        throw;
    }
    if (window_surface) compatibility.release_dc(window, source_device);
    output_bounds = {static_cast<double>(bounds.left),
                     static_cast<double>(bounds.top),
                     static_cast<double>(width),
                     static_cast<double>(height)};
    return GD_OK;
}

gd_result publish_retained_surface(std::uintptr_t destination,
                                   std::uint32_t kind, Bitmap& bitmap) {
    const HWND window = kind == GD_NATIVE_SURFACE_HWND
        ? reinterpret_cast<HWND>(destination)
        : WindowFromDC(reinterpret_cast<HDC>(destination));
    if (window == nullptr) return GD_ERROR_UNSUPPORTED_VERSION;
    // The C ABI's paint endpoint table, resolved by name (the drawing
    // library never links GUI.Forms): submit_bgra publishes to the endpoint
    // behind the window's compatibility handle.
    static const EndpointProbe probe = load_endpoint_probe();
    if (!probe.table_present || probe.table.submit_bgra == nullptr) return GD_ERROR_UNSUPPORTED_VERSION;
    const ImageSnapshot snapshot = bitmap.snapshot();
    const std::uint64_t handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window));
    const std::uint8_t* const pixels = reinterpret_cast<const std::uint8_t*>(snapshot.pixels().data());
    const gf_status submitted =
        probe.table.submit_bgra(handle, snapshot.width, snapshot.height, snapshot.row_bytes(), pixels);
    if (submitted != GF_STATUS_OK) return GD_ERROR_UNSUPPORTED_VERSION;
    return GD_OK;
}

gd_result present_surface(std::uintptr_t destination, std::uint32_t kind,
                          Bitmap& bitmap) {
    if (bitmap.borrows_memory() && kind != GD_NATIVE_SURFACE_HWND) {
        // Presenting a borrowed bitmap to the DC whose section it borrows
        // is a no-op: the pixels are already there.
        const HDC device = reinterpret_cast<HDC>(destination);
        const HGDIOBJ selected = GetCurrentObject(device, OBJ_BITMAP);
        DIBSECTION section{};
        if (selected != nullptr &&
            GetObjectW(selected, sizeof(section), &section) == static_cast<int>(sizeof(section)) &&
            static_cast<const std::byte*>(section.dsBm.bmBits) == bitmap.borrowed_memory()) {
            return GD_OK;
        }
    }
    const ImageSnapshot snapshot = bitmap.snapshot();
    // The bitmap is BGRA8, the DIB's own order.
    const void* const pixels = snapshot.pixels().data();
    const bool window_surface = kind == GD_NATIVE_SURFACE_HWND;
    const HWND window = window_surface ? reinterpret_cast<HWND>(destination) : nullptr;
    CompatibilityUser32& compatibility = compatibility_user32();
    HDC device = window_surface ? compatibility.get_dc(window) :
                                  reinterpret_cast<HDC>(destination);
    if (device == nullptr) throw std::invalid_argument("surface has no device context");
    RECT bounds{};
    if (window_surface) compatibility.get_client_rect(window, &bounds);
    else if (GetClipBox(device, &bounds) == ERROR) bounds = {};
    const int width = bounds.right > bounds.left ? bounds.right - bounds.left :
                                                      static_cast<int>(snapshot.width);
    const int height = bounds.bottom > bounds.top ? bounds.bottom - bounds.top :
                                                        static_cast<int>(snapshot.height);
    BITMAPINFO info = bitmap_info(snapshot.width, snapshot.height);
    const int copied = StretchDIBits(device, bounds.left, bounds.top, width, height,
                                     0, 0, snapshot.width, snapshot.height,
                                     pixels, &info, DIB_RGB_COLORS, SRCCOPY);
    if (window_surface) compatibility.release_dc(window, device);
    if (copied == static_cast<int>(GDI_ERROR)) {
        throw std::runtime_error("StretchDIBits failed");
    }
    return GD_OK;
}

// Alpha through an HDC lease. GDI writes colour and clears the alpha byte of
// every pixel it touches; it leaves untouched pixels alone. Before the lease
// each fully transparent pixel (premultiplied: alpha 0, colour 0) is marked
// with alpha 1, so after it a pixel with alpha 0 is one GDI wrote and becomes
// opaque whatever its colour, black included (no colour key), and a pixel
// still holding the mark with colour 0 returns to transparent. Cost: a pixel
// that held alpha 1 with colour 0 before the lease (1/255 black) also reads
// as transparent afterwards. Both passes cover byte_count bytes of BGRA8.
namespace {

void mark_untouched_alpha(std::uint8_t* pixels, std::size_t byte_count) noexcept {
    for (std::size_t offset = 0; offset < byte_count; offset += 4U) {
        if (pixels[offset + 3U] == 0U) {
            pixels[offset] = 0U;
            pixels[offset + 1U] = 0U;
            pixels[offset + 2U] = 0U;
            pixels[offset + 3U] = 1U;
        }
    }
}

void resolve_written_alpha(std::uint8_t* pixels, std::size_t byte_count) noexcept {
    for (std::size_t offset = 0; offset < byte_count; offset += 4U) {
        const std::uint8_t alpha = pixels[offset + 3U];
        if (alpha == 0U) {
            pixels[offset + 3U] = 255U;
        } else if (alpha == 1U && pixels[offset] == 0U && pixels[offset + 1U] == 0U &&
                   pixels[offset + 2U] == 0U) {
            pixels[offset + 3U] = 0U;
        }
    }
}

} // namespace

gd_result acquire_hdc(Bitmap& bitmap, std::uintptr_t& output,
                      std::uint64_t& lease_token) {
    const ImageSnapshot snapshot = bitmap.snapshot();
    BITMAPINFO info = bitmap_info(snapshot.width, snapshot.height);
    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                   &pixels, nullptr, 0U);
    HDC device = dib == nullptr ? nullptr : CreateCompatibleDC(nullptr);
    HGDIOBJ previous = device == nullptr ? nullptr : SelectObject(device, dib);
    if (pixels == nullptr || previous == nullptr || previous == HGDI_ERROR) {
        if (device != nullptr && previous != nullptr && previous != HGDI_ERROR) {
            SelectObject(device, previous);
        }
        if (device != nullptr) DeleteDC(device);
        if (dib != nullptr) DeleteObject(dib);
        throw std::runtime_error("bitmap-backed HDC creation failed");
    }
    std::memcpy(pixels, snapshot.pixels().data(), snapshot.pixels().size());
    mark_untouched_alpha(static_cast<std::uint8_t*>(pixels), snapshot.pixels().size());
    std::scoped_lock lock(lease_mutex);
    const std::uint64_t token = next_lease++;
    if (token == 0U) {
        SelectObject(device, previous);
        DeleteDC(device);
        DeleteObject(dib);
        throw std::length_error("HDC lease token space exhausted");
    }
    try {
        const HdcLease lease{.bitmap = &bitmap,
                             .owner = std::this_thread::get_id(),
                             .device = device,
                             .dib = dib,
                             .previous = previous,
                             .pixels = pixels,
                             .byte_count = snapshot.pixels().size()};
        leases.emplace(token, lease);
    } catch (...) {
        SelectObject(device, previous);
        DeleteDC(device);
        DeleteObject(dib);
        throw;
    }
    output = reinterpret_cast<std::uintptr_t>(device);
    lease_token = token;
    return GD_OK;
}

gd_result release_hdc(Bitmap& bitmap, std::uint64_t lease_token) {
    HdcLease lease;
    {
        std::scoped_lock lock(lease_mutex);
        const std::unordered_map<std::uint64_t, HdcLease>::iterator found =
            leases.find(lease_token);
        if (found == leases.end()) throw std::invalid_argument("HDC lease token is stale");
        if ((*found).second.bitmap != &bitmap) {
            throw std::invalid_argument("HDC lease belongs to a different bitmap");
        }
        if ((*found).second.owner != std::this_thread::get_id()) {
            return GD_ERROR_WRONG_THREAD;
        }
        lease = (*found).second;
        leases.erase(found);
    }
    struct LeaseCleanup final {
        HdcLease& lease;
        ~LeaseCleanup() {
            SelectObject(lease.device, lease.previous);
            DeleteDC(lease.device);
            DeleteObject(lease.dib);
        }
    } cleanup{lease};
    resolve_written_alpha(static_cast<std::uint8_t*>(lease.pixels), lease.byte_count);
    // The DIB and the bitmap are both BGRA8.
    BitmapLockView lock = bitmap.lock(BitmapLockMode::write);
    std::memcpy(lock.writable_data, lease.pixels, lease.byte_count);
    bitmap.unlock(lock.token);
    return GD_OK;
}

bool has_hdc_lease(Bitmap& bitmap) {
    std::scoped_lock lock(lease_mutex);
    for (const std::pair<const std::uint64_t, HdcLease>& entry : leases) {
        if (entry.second.bitmap == &bitmap) return true;
    }
    return false;
}

} // namespace gui_drawing::abi::platform
