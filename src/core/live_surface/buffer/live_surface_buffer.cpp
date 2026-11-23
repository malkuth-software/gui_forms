#include "live_surface_buffer.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <limits>
#include <new>

namespace gui_forms::detail {
namespace {

constexpr std::uint32_t maximum_dimension = 32768U;
constexpr std::uint64_t maximum_pixels = 268435456ULL;

} // namespace

bool valid_live_surface_description(
    LiveSurfaceDescription description) noexcept {
    if (description.width == 0U || description.height == 0U ||
        description.width > maximum_dimension ||
        description.height > maximum_dimension ||
        description.pixel_format !=
            LiveSurfacePixelFormat::bgra32_premultiplied_srgb ||
        description.buffer_count < minimum_live_surface_buffer_count ||
        description.buffer_count > maximum_live_surface_buffer_count) {
        return false;
    }
    return static_cast<std::uint64_t>(description.width) *
        description.height <= maximum_pixels;
}

std::shared_ptr<LiveSurfaceBuffer> make_live_surface_buffer(
    LiveSurfaceDescription description) {
    if (!valid_live_surface_description(description)) return {};
    const std::uint64_t row_bytes =
        static_cast<std::uint64_t>(description.width) * 4U;
    const std::uint64_t byte_count = row_bytes * description.height;
    if (byte_count > std::numeric_limits<std::size_t>::max()) return {};
    try {
        std::shared_ptr<gui_forms::detail::LiveSurfaceBuffer> result = gui_forms::detail::make_library_shared<LiveSurfaceBuffer>();
        (*result).description = description;
        (*result).row_bytes = row_bytes;
        (*result).pixels.resize(static_cast<std::size_t>(byte_count));
        return result;
    } catch (const std::bad_alloc&) {
        return {};
    }
}

Rect full_live_surface_damage(LiveSurfaceDescription description) noexcept {
    return {0.0, 0.0, static_cast<double>(description.width),
            static_cast<double>(description.height)};
}

} // namespace gui_forms::detail
