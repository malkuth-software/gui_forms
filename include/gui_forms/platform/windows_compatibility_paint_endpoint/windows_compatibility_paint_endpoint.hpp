#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gui_forms {

class LiveSurface;

namespace host {

// Thread-affine Win32 compatibility raster endpoint. Its registry token is
// never an HWND and its retained renderer consumes only the durable surface.
class GUI_FORMS_API WindowsCompatibilityPaintEndpoint final {
public:
    static std::shared_ptr<WindowsCompatibilityPaintEndpoint> acquire(
        std::uint32_t width, std::uint32_t height);

    ~WindowsCompatibilityPaintEndpoint();

    WindowsCompatibilityPaintEndpoint(
        const WindowsCompatibilityPaintEndpoint&) = delete;
    WindowsCompatibilityPaintEndpoint& operator=(
        const WindowsCompatibilityPaintEndpoint&) = delete;

    [[nodiscard]] std::uintptr_t compatibility_handle() const noexcept;
    [[nodiscard]] std::uintptr_t device_context() const noexcept;
    [[nodiscard]] std::shared_ptr<LiveSurface> live_surface() const noexcept;
    [[nodiscard]] bool publish_device_context(
        std::uintptr_t device_context) noexcept;
    [[nodiscard]] bool begin_device_context_write(
        std::uintptr_t device_context) noexcept;
    [[nodiscard]] bool end_device_context_write(
        std::uintptr_t device_context, bool publish) noexcept;
    [[nodiscard]] bool configure(std::uint32_t width,
                                 std::uint32_t height) noexcept;
    [[nodiscard]] bool submit_bgra32_premultiplied(
        std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
        std::span<const std::byte> pixels) noexcept;
    void touch(bool explicit_boundary = false) noexcept;
    [[nodiscard]] bool drain_now() noexcept;
    [[nodiscard]] std::string snapshot() const;
    void release() noexcept;

private:
    struct Implementation;
    explicit WindowsCompatibilityPaintEndpoint(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;
};

} // namespace host
} // namespace gui_forms
