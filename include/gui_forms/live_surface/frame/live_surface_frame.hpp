#pragma once

#include "gui_forms/live_surface/types/live_surface_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gui_forms {

namespace detail {
struct LiveSurfaceBuffer;
}

class LiveSurface;

// Immutable read lease over one completely published frame. The producer can
// continue rendering into another buffer while this lease is held.
class LiveSurfaceFrame final {
public:
    LiveSurfaceFrame() = default;
    ~LiveSurfaceFrame() = default;
    LiveSurfaceFrame(LiveSurfaceFrame&&) noexcept = default;
    LiveSurfaceFrame& operator=(LiveSurfaceFrame&&) noexcept = default;
    LiveSurfaceFrame(const LiveSurfaceFrame&) = delete;
    LiveSurfaceFrame& operator=(const LiveSurfaceFrame&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return buffer_ != nullptr;
    }
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::uint64_t row_bytes() const noexcept;
    [[nodiscard]] LiveSurfacePixelFormat pixel_format() const noexcept;
    // This frame's promise remains valid across surface reconfiguration.
    [[nodiscard]] bool opaque() const noexcept;
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    [[nodiscard]] Rect damage() const noexcept { return damage_; }
    // The producer's label given at publish (LiveSurfaceWriteLease::publish).
    [[nodiscard]] std::uint64_t content_tag() const noexcept { return content_tag_; }
    [[nodiscard]] std::span<const std::byte> pixels() const noexcept;

private:
    friend class LiveSurface;
    LiveSurfaceFrame(std::shared_ptr<const detail::LiveSurfaceBuffer> buffer,
                     std::uint64_t epoch, std::uint64_t generation,
                     Rect damage, std::uint64_t content_tag) noexcept;

    std::shared_ptr<const detail::LiveSurfaceBuffer> buffer_;
    std::uint64_t epoch_{};
    std::uint64_t generation_{};
    Rect damage_{};
    std::uint64_t content_tag_{};
};

} // namespace gui_forms
