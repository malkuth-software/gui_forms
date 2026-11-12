#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/live_surface/types/live_surface_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gui_forms {

namespace detail {
struct LiveSurfaceState;
struct LiveSurfaceBuffer;
}

class LiveSurface;

// Exclusive producer lease. Destruction without publish abandons the candidate
// frame. Acquiring may fail instead of blocking when all buffers are retained
// by readers; high-rate producers should drop that frame.
class GUI_FORMS_API LiveSurfaceWriteLease final {
public:
    LiveSurfaceWriteLease() = default;
    ~LiveSurfaceWriteLease();
    LiveSurfaceWriteLease(LiveSurfaceWriteLease&& other) noexcept;
    LiveSurfaceWriteLease& operator=(LiveSurfaceWriteLease&& other) noexcept;
    LiveSurfaceWriteLease(const LiveSurfaceWriteLease&) = delete;
    LiveSurfaceWriteLease& operator=(const LiveSurfaceWriteLease&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return state_ != nullptr && buffer_ != nullptr;
    }
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::uint64_t row_bytes() const noexcept;
    [[nodiscard]] std::span<std::byte> pixels() noexcept;
    // Damage promises that every pixel outside it equals the previous
    // publication; a presenter may copy only the damage. Acquire with
    // preserve_published_contents or repair the buffer completely. Empty or
    // out-of-bounds damage means the whole surface. The content tag is the
    // producer's own label for what the frame shows (a sub-surface's content
    // epoch, stage S2); the surface keeps it with the frame and never reads it.
    [[nodiscard]] std::uint64_t publish(Rect damage = {}, std::uint64_t content_tag = 0U);
    void abandon() noexcept;

private:
    friend class LiveSurface;
    LiveSurfaceWriteLease(std::shared_ptr<detail::LiveSurfaceState> state,
                          std::shared_ptr<detail::LiveSurfaceBuffer> buffer,
                          std::size_t slot, std::uint64_t epoch) noexcept;

    std::shared_ptr<detail::LiveSurfaceState> state_;
    std::shared_ptr<detail::LiveSurfaceBuffer> buffer_;
    std::size_t slot_{};
    std::uint64_t epoch_{};
};

} // namespace gui_forms
