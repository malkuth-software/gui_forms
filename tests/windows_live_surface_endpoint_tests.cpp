#include "gui_forms/platform/windows_host.hpp"
#include "gui_forms/live_surface.hpp"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

// Throws on failure, so the check runs in every build type (Release defines
// NDEBUG); an uncaught throw fails the test.
void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

struct ProducerBitmap final {
    HDC device{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};

    ProducerBitmap(HDC compatible_with, int width, int height) {
        device = CreateCompatibleDC(compatible_with);
        bitmap = CreateCompatibleBitmap(compatible_with, width, height);
        require(device != nullptr, "device != nullptr");
        require(bitmap != nullptr, "bitmap != nullptr");
        previous = SelectObject(device, bitmap);
        require(previous != nullptr && previous != HGDI_ERROR, "previous != nullptr && previous != HGDI_ERROR");
    }

    ~ProducerBitmap() {
        if (device != nullptr && previous != nullptr && previous != HGDI_ERROR) {
            SelectObject(device, previous);
        }
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (device != nullptr) DeleteDC(device);
    }

    ProducerBitmap(const ProducerBitmap&) = delete;
    ProducerBitmap& operator=(const ProducerBitmap&) = delete;
};

std::uint32_t pixel_at(const gui_forms::LiveSurfaceFrame& frame,
                       std::uint32_t x, std::uint32_t y) {
    const std::size_t offset = static_cast<std::size_t>(y) * frame.row_bytes() +
                               static_cast<std::size_t>(x) * 4U;
    const std::span<const std::byte> pixels = frame.pixels();
    require(offset + 4U <= pixels.size(), "offset + 4U <= pixels.size()");
    return static_cast<std::uint32_t>(pixels[offset]) |
           (static_cast<std::uint32_t>(pixels[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(pixels[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(pixels[offset + 3U]) << 24U);
}

void paint_and_publish(
    const std::shared_ptr<gui_forms::host::WindowsCompatibilityPaintEndpoint>&
        endpoint,
    ProducerBitmap& producer, int width, int height, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    require(brush != nullptr, "brush != nullptr");
    RECT bounds{0, 0, width, height};
    require(FillRect(producer.device, &bounds, brush) != 0, "FillRect(producer.device, &bounds, brush) != 0");
    DeleteObject(brush);

    const HDC destination =
        reinterpret_cast<HDC>((*endpoint).device_context());
    require(destination != nullptr, "destination != nullptr");
    require(BitBlt(destination, 0, 0, width, height, producer.device, 0, 0,
                  SRCCOPY) != 0, "BitBlt(destination, 0, 0, width, height, producer.device, 0, 0, SRCCOPY) != 0");
    require((*endpoint).publish_device_context(
        reinterpret_cast<std::uintptr_t>(destination)), "(*endpoint).publish_device_context( reinterpret_cast<std::uintptr_t>(destination))");
}

} // namespace

int main() {
    using gui_forms::host::WindowsCompatibilityPaintEndpoint;

    constexpr std::array<std::array<int, 2>, 6> sizes{{
        {{150, 150}}, {{90, 150}}, {{30, 150}},
        {{332, 324}}, {{303, 324}}, {{128, 64}},
    }};

    // Exercise a direct-GDI consumer shape: a control HDC retained for the
    // control lifetime, a compatible memory DC/bitmap for the producer,
    // repeated BitBlt publication, resize, and reverse-order cleanup.
    for (int lifetime = 0; lifetime < 200; ++lifetime) {
        const std::array<int, 2> initial =
            sizes[static_cast<std::size_t>(lifetime) % sizes.size()];
        std::shared_ptr<WindowsCompatibilityPaintEndpoint> endpoint =
            WindowsCompatibilityPaintEndpoint::acquire(
            static_cast<std::uint32_t>(initial[0]),
            static_cast<std::uint32_t>(initial[1]));
        require(static_cast<bool>(endpoint), "endpoint");
        const std::uintptr_t compatibility_handle =
            (*endpoint).compatibility_handle();
        require(compatibility_handle != 0U, "compatibility_handle != 0U");
        // Direct retained surfaces must not allocate a native window. If this
        // assertion regresses, the endpoint can enter hit-testing/input state.
        require(IsWindow(reinterpret_cast<HWND>(compatibility_handle)) == FALSE, "IsWindow(reinterpret_cast<HWND>(compatibility_handle)) == FALSE");
        require((*endpoint).device_context() != 0U, "(*endpoint).device_context() != 0U");

        const std::array<int, 2> producer_size = sizes[
            (static_cast<std::size_t>(lifetime) + 3U) % sizes.size()];
        ProducerBitmap producer(
            reinterpret_cast<HDC>((*endpoint).device_context()),
            producer_size[0], producer_size[1]);

        for (int frame_number = 0; frame_number < 80; ++frame_number) {
            const COLORREF color = RGB(
                (frame_number * 37 + lifetime) & 0xff,
                (frame_number * 67 + lifetime * 3) & 0xff,
                (frame_number * 97 + lifetime * 7) & 0xff);
            const int width = (std::min)(initial[0], producer_size[0]);
            const int height = (std::min)(initial[1], producer_size[1]);
            paint_and_publish(endpoint, producer, width, height, color);

            gui_forms::LiveSurfaceFrame frame =
                (*(*endpoint).live_surface()).acquire_latest();
            require(static_cast<bool>(frame), "frame");
            const std::uint32_t expected =
                0xff000000U |
                (static_cast<std::uint32_t>(GetRValue(color)) << 16U) |
                (static_cast<std::uint32_t>(GetGValue(color)) << 8U) |
                static_cast<std::uint32_t>(GetBValue(color));
            require(pixel_at(frame, 0U, 0U) == expected, "pixel_at(frame, 0U, 0U) == expected");
        }

        const std::array<int, 2> resized = sizes[
            (static_cast<std::size_t>(lifetime) + 1U) % sizes.size()];
        require((*endpoint).configure(static_cast<std::uint32_t>(resized[0]),
                                     static_cast<std::uint32_t>(resized[1])), "(*endpoint).configure(static_cast<std::uint32_t>(resized[0]), static_cast<std::uint32_t>(resized[1]))");
        (*endpoint).release();
        endpoint.reset();
    }
}
