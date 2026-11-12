#include "gui_forms/drawing/image_snapshot/image_snapshot.hpp"

#include "../support/drawing_support.hpp"

bool ImageSnapshot::has_pixels() const noexcept {
    return storage_ != nullptr;
}

std::size_t ImageSnapshot::row_bytes() const noexcept {
    return storage_ ? (*storage_).row_bytes : 0U;
}

std::span<const std::byte> ImageSnapshot::pixels() const& noexcept {
    return storage_ ? std::span<const std::byte>((*storage_).data(), (*storage_).size())
                    : std::span<const std::byte>{};
}

} // namespace gui_drawing
