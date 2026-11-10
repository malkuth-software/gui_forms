#include "gui_forms/platform/windows_host.hpp"
#include "gui_forms/live_surface.hpp"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
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

bool snapshot_has(const std::string& snapshot, const char* field) {
    return snapshot.find(field) != std::string::npos;
}

// N48: a write whose end names another device context, and a write that never
// reaches its end, must both leave the endpoint usable from the same thread.
// Before the fix the first returned with state_mutex held, and the next
// begin_write on that thread locked it again (a hang).
void check_write_lease_recovery() {
    using gui_forms::host::WindowsCompatibilityPaintEndpoint;
    std::shared_ptr<WindowsCompatibilityPaintEndpoint> endpoint =
        WindowsCompatibilityPaintEndpoint::acquire(64U, 32U);
    require(static_cast<bool>(endpoint), "recovery endpoint");
    const std::uintptr_t device = (*endpoint).device_context();
    require(device != 0U, "recovery device context");
    const std::uintptr_t other = device + 1U;

    // A write ended with the wrong device context fails, and ends the write.
    require((*endpoint).begin_device_context_write(device), "mismatch begin");
    require(!(*endpoint).end_device_context_write(other, true),
            "a mismatched end reports failure");
    // A second end has no write to close.
    require(!(*endpoint).end_device_context_write(device, true),
            "an end without a write reports failure");
    // The mutex is free: snapshot locks it, and a new write begins and ends.
    require(snapshot_has((*endpoint).snapshot(), "|mismatched-write-ends:1"),
            "the mismatch is counted");
    require((*endpoint).begin_device_context_write(device), "begin after mismatch");
    require((*endpoint).end_device_context_write(device, false), "end after mismatch");

    // A write abandoned without its end is closed by the next begin on the
    // same thread instead of locking the held mutex again.
    require((*endpoint).begin_device_context_write(device), "abandoned begin");
    require((*endpoint).begin_device_context_write(device), "begin over an abandoned write");
    require((*endpoint).end_device_context_write(device, false), "end after abandoned write");
    require(snapshot_has((*endpoint).snapshot(), "|abandoned-writes:1"),
            "the abandoned write is counted");

    // A begin with the wrong device context fails and holds nothing.
    require(!(*endpoint).begin_device_context_write(other), "a wrong begin fails");
    require((*endpoint).configure(48U, 24U), "configure after a failed begin");
    (*endpoint).release();
}

} // namespace

int main() {
    check_write_lease_recovery();

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
