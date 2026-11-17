#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>

namespace gui_forms::host::detail {

// A top-down 32-bit BGRA raster in caller-owned memory: `height` rows of
// `row_bytes` bytes each, of which the first `width` pixels are the image.
// The pixels are borrowed for one call.
struct DibBandSource final {
    const std::byte* pixels{};
    std::size_t row_bytes{};
    int width{};
    int height{};
};

// Copies `band` (device pixels, already inside the source's extent and not
// empty) of `source` to the same place on `target`. Returns false when GDI
// reports failure; the target then holds whatever GDI copied.
//
// The source is described to GDI as a top-down DIB of exactly the band's
// rows, starting at its first row, and copied whole in height. GDI does not
// read a partial-height source rectangle of a top-down DIB one way:
// StretchDIBits counts its y from the bottom row, except that a rectangle
// whose y is 0 (a band touching the bottom edge, with rows above it) is read
// from the top rows, so such a band was drawn with the raster's top rows,
// shifted down by its own top. A whole-height source has one reading.
[[nodiscard]] inline bool present_dib_band(HDC target, const DibBandSource& source, const RECT& band) noexcept {
    const int columns = band.right - band.left;
    const int rows = band.bottom - band.top;
    const std::byte* const first_row = source.pixels + static_cast<std::size_t>(band.top) * source.row_bytes;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(source.row_bytes / 4U);
    info.bmiHeader.biHeight = -rows;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const int copied = StretchDIBits(target, band.left, band.top, columns, rows,
                                     band.left, 0, columns, rows,
                                     first_row, &info, DIB_RGB_COLORS, SRCCOPY);
    // StretchDIBits reports the scan lines it copied, or 0 or GDI_ERROR on
    // failure; only failure is tested.
    const bool presented = copied != 0 && copied != static_cast<int>(GDI_ERROR);
    return presented;
}

} // namespace gui_forms::host::detail
