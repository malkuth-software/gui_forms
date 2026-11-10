// The Win32 host's band present (detail::present_dib_band) against GDI
// itself: every band shape of a small top-down raster, bands touching the
// bottom edge with rows above them included, is copied into an offscreen DIB
// section and read back pixel by pixel. Each source pixel encodes its own row
// and column, so a band drawn from the wrong rows (StretchDIBits reads a
// partial-height top-down source whose y is 0 from the top rows) is caught at
// its first pixel. No window is created.
#include "dib_band_present.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using gui_forms::host::detail::DibBandSource;
using gui_forms::host::detail::present_dib_band;

// The raster's extent in device pixels. Each pixel is (row << 16) | column,
// so both fit with room for the background's mark.
constexpr int raster_width = 23;
constexpr int raster_height = 37;
// What the target holds before a band is copied: no source pixel has it.
constexpr std::uint32_t background = 0xFFFFFFFFU;

void require(bool condition, const std::string& message) {
    if (!condition) { throw std::runtime_error(message); }
}

[[nodiscard]] std::uint32_t source_pixel(int row, int column) {
    const std::uint32_t result = (static_cast<std::uint32_t>(row) << 16U) | static_cast<std::uint32_t>(column);
    return result;
}

// An offscreen top-down 32-bit DIB section selected into a memory DC; owns
// both until destroyed.
struct OffscreenTarget final {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    std::uint32_t* pixels{};

    OffscreenTarget() {
        dc = CreateCompatibleDC(nullptr);
        require(dc != nullptr, "CreateCompatibleDC failed");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = raster_width;
        info.bmiHeader.biHeight = -raster_height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        require(bitmap != nullptr && bits != nullptr, "CreateDIBSection failed");
        pixels = static_cast<std::uint32_t*>(bits);
        previous = SelectObject(dc, bitmap);
    }
    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;
    ~OffscreenTarget() {
        if (previous != nullptr) SelectObject(dc, previous);
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (dc != nullptr) DeleteDC(dc);
    }

    void fill_background() {
        GdiFlush();
        const std::size_t count = static_cast<std::size_t>(raster_width) * static_cast<std::size_t>(raster_height);
        for (std::size_t index = 0; index < count; ++index) pixels[index] = background;
    }
};

// Copies one band and checks every target pixel: inside the band the source
// pixel of the same place, outside it the background.
void check_band(OffscreenTarget& target, const DibBandSource& source, const RECT& band) {
    target.fill_background();
    require(present_dib_band(target.dc, source, band), "present_dib_band reported failure");
    GdiFlush();
    for (int row = 0; row < raster_height; ++row) {
        for (int column = 0; column < raster_width; ++column) {
            const bool inside = row >= band.top && row < band.bottom && column >= band.left && column < band.right;
            const std::uint32_t expected = inside ? source_pixel(row, column) : background;
            const std::size_t index = static_cast<std::size_t>(row) * raster_width + static_cast<std::size_t>(column);
            const std::uint32_t found = target.pixels[index];
            if (found != expected) {
                throw std::runtime_error("band [" + std::to_string(band.left) + "," + std::to_string(band.top) + ")-(" +
                                         std::to_string(band.right) + "," + std::to_string(band.bottom) + "): pixel (" +
                                         std::to_string(column) + "," + std::to_string(row) + ") holds row " +
                                         std::to_string(found >> 16U) + " column " + std::to_string(found & 0xFFFFU));
            }
        }
    }
}

// Every band of whole rows (all columns), and every band of a narrower
// column span, including the bottom-edge bands that used to be shifted.
void test_every_band_shape() {
    std::vector<std::uint32_t> raster(static_cast<std::size_t>(raster_width) * static_cast<std::size_t>(raster_height));
    for (int row = 0; row < raster_height; ++row) {
        for (int column = 0; column < raster_width; ++column) {
            raster[static_cast<std::size_t>(row) * raster_width + static_cast<std::size_t>(column)] = source_pixel(row, column);
        }
    }
    DibBandSource source{};
    source.pixels = reinterpret_cast<const std::byte*>(raster.data());
    source.row_bytes = static_cast<std::size_t>(raster_width) * 4U;
    source.width = raster_width;
    source.height = raster_height;
    OffscreenTarget target;
    std::size_t bands = 0;
    std::size_t bottom_bands = 0;
    for (int top = 0; top < raster_height; ++top) {
        for (int bottom = top + 1; bottom <= raster_height; ++bottom) {
            check_band(target, source, RECT{0, top, raster_width, bottom});
            check_band(target, source, RECT{5, top, 17, bottom});
            bands += 2U;
            if (bottom == raster_height && top > 0) bottom_bands += 2U;
        }
    }
    require(bottom_bands == static_cast<std::size_t>(raster_height - 1) * 2U, "bottom-edge bands not all covered");
    std::cout << bands << " bands copied exactly, " << bottom_bands << " of them touching the bottom edge below row 0\n";
}

} // namespace

int main() {
    try {
        test_every_band_shape();
        std::cout << "Windows DIB band present passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
