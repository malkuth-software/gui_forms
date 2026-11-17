#include "lab.hpp"

#include <uxtheme.h>

#include <cstring>
#include <vector>

// GDI routes: a DIB section copied with BitBlt (today's GUI.Forms baseline),
// StretchDIBits from plain memory (today's band path), UpdateLayeredWindow,
// and buffered paint (uxtheme). None needs a GPU API symbol.
namespace lab {

namespace {

struct DibSection final {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    std::uint32_t* pixels{};
};

bool allocate_dib(DibSection& dib) {
    dib.dc = CreateCompatibleDC(nullptr);
    if (dib.dc == nullptr) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = window_width;
    info.bmiHeader.biHeight = -window_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* storage = nullptr;
    dib.bitmap = CreateDIBSection(dib.dc, &info, DIB_RGB_COLORS, &storage, nullptr, 0);
    if (dib.bitmap == nullptr) return false;
    dib.pixels = static_cast<std::uint32_t*>(storage);
    dib.previous = SelectObject(dib.dc, dib.bitmap);
    return true;
}

void release_dib(DibSection& dib) {
    if (dib.dc != nullptr && dib.previous != nullptr) SelectObject(dib.dc, dib.previous);
    if (dib.bitmap != nullptr) DeleteObject(dib.bitmap);
    if (dib.dc != nullptr) DeleteDC(dib.dc);
    dib = DibSection{};
}

// --- DIB section + BitBlt -------------------------------------------------

struct BitBltContext final {
    DibSection dib{};
    int paints{};
    RECT last_paint{};
    BOOL last_copy{};
    long long copied_pixels{};
};

void paint_bitblt(HWND window, void* context) {
    BitBltContext& state = *static_cast<BitBltContext*>(context);
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    const int columns = paint.rcPaint.right - paint.rcPaint.left;
    const int rows = paint.rcPaint.bottom - paint.rcPaint.top;
    state.last_copy = BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top, columns, rows,
                             state.dib.dc, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
    EndPaint(window, &paint);
    state.last_paint = paint.rcPaint;
    state.copied_pixels += static_cast<long long>(columns) * rows;
    ++state.paints;
}

void run_bitblt_route() {
    const char* route = "dib_bitblt";
    BitBltContext state{};
    if (!allocate_dib(state.dib)) {
        report(route, "allocate", "fail error=%lu", static_cast<unsigned long>(GetLastError()));
        return;
    }
    fill_pattern(state.dib.pixels, window_width, window_height, window_width, 0, 0, 0, false);
    PaintTarget target{paint_bitblt, &state};
    HWND window = create_window(L"dib_bitblt", 0, &target, true);
    pump(300);
    report(route, "first_paint", "paints=%d copy=%d pixels=%lld", state.paints, state.last_copy, state.copied_pixels);
    report_readback(route, "frame0", window, 0, 0);

    // Damage-limited: only the right half changes and is invalidated.
    fill_pattern(state.dib.pixels + window_width / 2, window_width / 2, window_height, window_width,
                 window_width / 2, 0, 1, false);
    const RECT right_half{window_width / 2, 0, window_width, window_height};
    state.copied_pixels = 0;
    InvalidateRect(window, &right_half, FALSE);
    pump(200);
    report(route, "partial", "paint_rect=%ld,%ld,%ld,%ld copy=%d pixels=%lld", state.last_paint.left,
           state.last_paint.top, state.last_paint.right, state.last_paint.bottom, state.last_copy,
           state.copied_pixels);
    // The left half still shows phase 0; the samples are on the right.
    report_readback(route, "frame1_partial", window, 1, 0);

    // Host scroll: the window's own pixels move up 24 rows, the raster moves
    // the same way, and only the exposed strip is painted.
    fill_pattern(state.dib.pixels, window_width / 2, window_height, window_width, 0, 0, 1, false);
    const int scroll_rows = 24;
    std::memmove(state.dib.pixels, state.dib.pixels + static_cast<std::size_t>(scroll_rows) * window_width,
                 static_cast<std::size_t>(window_height - scroll_rows) * window_width * sizeof(std::uint32_t));
    fill_pattern(state.dib.pixels + static_cast<std::size_t>(window_height - scroll_rows) * window_width,
                 window_width, scroll_rows, window_width, 0, window_height, 1, false);
    HRGN exposed = CreateRectRgn(0, 0, 0, 0);
    RECT exposed_bounds{};
    state.copied_pixels = 0;
    const int region_kind = ScrollWindowEx(window, 0, -scroll_rows, nullptr, nullptr, exposed, &exposed_bounds,
                                           SW_INVALIDATE);
    pump(200);
    report(route, "scrollwindowex", "region_kind=%d exposed=%ld,%ld,%ld,%ld repaint_pixels=%lld", region_kind,
           exposed_bounds.left, exposed_bounds.top, exposed_bounds.right, exposed_bounds.bottom,
           state.copied_pixels);
    report_readback(route, "frame2_scrolled", window, 1, scroll_rows);
    DeleteObject(exposed);
    DestroyWindow(window);
    pump(50);
    release_dib(state.dib);
}

// --- StretchDIBits from plain memory ----------------------------------------

struct StretchContext final {
    std::vector<std::uint32_t> pixels{};
    int paints{};
    int last_lines{};
};

void paint_stretch(HWND window, void* context) {
    StretchContext& state = *static_cast<StretchContext*>(context);
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = window_width;
    info.bmiHeader.biHeight = -window_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    state.last_lines = StretchDIBits(dc, 0, 0, window_width, window_height, 0, 0, window_width, window_height,
                                     state.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    EndPaint(window, &paint);
    ++state.paints;
}

void run_stretch_route() {
    const char* route = "stretchdibits";
    StretchContext state{};
    state.pixels.resize(static_cast<std::size_t>(window_width) * window_height);
    fill_pattern(state.pixels.data(), window_width, window_height, window_width, 0, 0, 2, false);
    PaintTarget target{paint_stretch, &state};
    HWND window = create_window(L"stretchdibits", 0, &target, true);
    pump(300);
    report(route, "paint", "paints=%d lines=%d", state.paints, state.last_lines);
    report_readback(route, "frame", window, 2, 0);
    DestroyWindow(window);
    pump(50);
}

// --- UpdateLayeredWindow ------------------------------------------------------

void run_layered_route() {
    const char* route = "ulw";
    DibSection dib{};
    if (!allocate_dib(dib)) {
        report(route, "allocate", "fail");
        return;
    }
    fill_pattern(dib.pixels, window_width, window_height, window_width, 0, 0, 3, true);
    HWND window = create_window(L"ulw", WS_EX_LAYERED, nullptr, false);
    POINT destination{40, 40};
    SIZE size{window_width, window_height};
    POINT source{0, 0};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UPDATELAYEREDWINDOWINFO update{};
    update.cbSize = sizeof(update);
    update.pptDst = &destination;
    update.psize = &size;
    update.hdcSrc = dib.dc;
    update.pptSrc = &source;
    update.pblend = &blend;
    update.dwFlags = ULW_ALPHA;
    report_bool(route, "update_full", UpdateLayeredWindowIndirect(window, &update));
    ShowWindow(window, SW_SHOWNOACTIVATE);
    pump(300);
    report_readback(route, "frame3", window, 3, 0);

    // Damage-limited: prcDirty names the changed rectangle; the source must
    // still describe the whole window.
    fill_pattern(dib.pixels + window_width / 2, window_width / 2, window_height, window_width,
                 window_width / 2, 0, 4, true);
    const RECT dirty{window_width / 2, 0, window_width, window_height};
    update.prcDirty = &dirty;
    report_bool(route, "update_dirty", UpdateLayeredWindowIndirect(window, &update));
    pump(200);
    report_readback(route, "frame4_dirty", window, 4, 0);

    // A dirty rectangle with a smaller size than the window (the source
    // covering only the dirty part) is refused by design.
    SIZE partial_size{window_width / 2, window_height};
    update.psize = &partial_size;
    report_bool(route, "update_partial_size", UpdateLayeredWindowIndirect(window, &update));
    DestroyWindow(window);
    pump(50);
    release_dib(dib);
}

// --- Buffered paint -----------------------------------------------------------

struct BufferedContext final {
    int phase{};
    int paints{};
    HRESULT bits_result{};
    void* bits{};
    void* previous_bits{};
    int row_pixels{};
    RECT last_paint{};
    bool began{};
};

void paint_buffered(HWND window, void* context) {
    BufferedContext& state = *static_cast<BufferedContext*>(context);
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    HDC buffer_dc = nullptr;
    HPAINTBUFFER buffer = BeginBufferedPaint(dc, &paint.rcPaint, BPBF_TOPDOWNDIB, nullptr, &buffer_dc);
    state.began = buffer != nullptr;
    if (buffer != nullptr) {
        RGBQUAD* bits = nullptr;
        int row = 0;
        state.bits_result = GetBufferedPaintBits(buffer, &bits, &row);
        state.previous_bits = state.bits;
        state.bits = bits;
        state.row_pixels = row;
        if (SUCCEEDED(state.bits_result) && bits != nullptr) {
            // The bits start at the buffer's target rectangle (rcPaint).
            fill_pattern(reinterpret_cast<std::uint32_t*>(bits), paint.rcPaint.right - paint.rcPaint.left,
                         paint.rcPaint.bottom - paint.rcPaint.top, row, paint.rcPaint.left, paint.rcPaint.top,
                         state.phase, false);
        }
        EndBufferedPaint(buffer, TRUE);
    }
    EndPaint(window, &paint);
    state.last_paint = paint.rcPaint;
    ++state.paints;
}

void run_buffered_route() {
    const char* route = "bufferedpaint";
    report_hr(route, "init", BufferedPaintInit());
    BufferedContext state{};
    state.phase = 5;
    PaintTarget target{paint_buffered, &state};
    HWND window = create_window(L"bufferedpaint", 0, &target, true);
    pump(300);
    report(route, "paint1", "began=%d bits_hr=0x%08lX row_pixels=%d paint_rect=%ld,%ld,%ld,%ld", state.began,
           static_cast<unsigned long>(state.bits_result), state.row_pixels, state.last_paint.left,
           state.last_paint.top, state.last_paint.right, state.last_paint.bottom);
    report_readback(route, "frame5", window, 5, 0);
    state.phase = 6;
    const RECT right_half{window_width / 2, 0, window_width, window_height};
    InvalidateRect(window, &right_half, FALSE);
    pump(200);
    report(route, "paint2", "row_pixels=%d paint_rect=%ld,%ld,%ld,%ld same_pool_buffer=%d", state.row_pixels,
           state.last_paint.left, state.last_paint.top, state.last_paint.right, state.last_paint.bottom,
           state.bits == state.previous_bits);
    report_readback(route, "frame6_partial", window, 6, 0);
    DestroyWindow(window);
    pump(50);
    report_hr(route, "uninit", BufferedPaintUnInit());
}

} // namespace

void run_gdi_routes() {
    run_bitblt_route();
    run_stretch_route();
    run_layered_route();
    run_buffered_route();
}

} // namespace lab
