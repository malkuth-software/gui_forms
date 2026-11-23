#include "gui_forms/diagnostics/blend_study/blend_study.hpp"

#include "../../../platform/library_files.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <string_view>
#include <stxlib/files.hpp>
#include <stxlib/result.hpp>
#include <stxlib/threads.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

namespace gui_forms {
namespace {

// -- the process switch ---------------------------------------------------------

// 0 unread, else 1 + the BlendStudyMode: the environment is read once, on
// the first read of the switch, unless a program set it first.
std::atomic<std::uint8_t> process_switch{0U};
constexpr std::uint8_t switch_unread = 0U;

std::uint8_t switch_value(const BlendStudyMode mode) noexcept {
    const std::uint8_t value = static_cast<std::uint8_t>(static_cast<std::uint8_t>(mode) + 1U);
    return value;
}

BlendStudyMode switch_mode(const std::uint8_t value) noexcept {
    switch (value) {
    case 1U: return BlendStudyMode::off;
    case 2U: return BlendStudyMode::counters;
    case 3U: return BlendStudyMode::overdraw_heat_map;
    case 4U: return BlendStudyMode::blend_heat_map;
    default: break;
    }
    // Unread, or a value no set wrote: off.
    return BlendStudyMode::off;
}

// -- the heat map's tints -----------------------------------------------------------

// Premultiplied BGRA, opaque. Depth: index by min(depth, 6); 0 and 1 are
// not tinted.
struct HeatTint final {
    std::uint8_t blue{};
    std::uint8_t green{};
    std::uint8_t red{};
    std::uint8_t weight{};
};

constexpr std::array<HeatTint, 7> depth_tints{{
    {0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U},
    {255U, 96U, 0U, 1U},
    {0U, 200U, 0U, 1U},
    {0U, 230U, 255U, 1U},
    {0U, 140U, 255U, 1U},
    {0U, 0U, 255U, 1U},
}};

// Index by the class bits: none, copied only, blended only, both.
constexpr std::array<HeatTint, 4> class_tints{{
    {0U, 0U, 0U, 0U},
    {0U, 200U, 0U, 1U},
    {0U, 0U, 255U, 1U},
    {0U, 140U, 255U, 1U},
}};

// Half the frame and half the tint, opaque, where the tint's weight is one;
// the pixel unchanged where it is zero. Arithmetic, not a branch: weight is
// 0 or 1.
void tint_pixel(std::byte* const pixel, const HeatTint tint) noexcept {
    const unsigned keep = 1U - tint.weight;
    const unsigned blue = std::to_integer<unsigned>(pixel[0]);
    const unsigned green = std::to_integer<unsigned>(pixel[1]);
    const unsigned red = std::to_integer<unsigned>(pixel[2]);
    const unsigned alpha = std::to_integer<unsigned>(pixel[3]);
    const unsigned tinted_blue = (blue + tint.blue + 1U) / 2U;
    const unsigned tinted_green = (green + tint.green + 1U) / 2U;
    const unsigned tinted_red = (red + tint.red + 1U) / 2U;
    pixel[0] = static_cast<std::byte>(blue * keep + tinted_blue * tint.weight);
    pixel[1] = static_cast<std::byte>(green * keep + tinted_green * tint.weight);
    pixel[2] = static_cast<std::byte>(red * keep + tinted_red * tint.weight);
    pixel[3] = static_cast<std::byte>(alpha * keep + 255U * tint.weight);
}

} // namespace

// -- counters ---------------------------------------------------------------------

void add_blend_frame(BlendFrameCounters& totals, const BlendFrameCounters& frame) noexcept {
    totals.copied_pixels += frame.copied_pixels;
    totals.blended_pixels += frame.blended_pixels;
    totals.overdraw_pixels += frame.overdraw_pixels;
    totals.overdraw_writes += frame.overdraw_writes;
    for (std::size_t bin = 0U; bin < blend_depth_bins; ++bin) {
        totals.depth_histogram[bin] += frame.depth_histogram[bin];
    }
    totals.opaque_fills_blended += frame.opaque_fills_blended;
    totals.opaque_images_blended += frame.opaque_images_blended;
    totals.draws_counted += frame.draws_counted;
    totals.text_runs_unmeasured += frame.text_runs_unmeasured;
    totals.refused += frame.refused;
    totals.paint_nanoseconds += frame.paint_nanoseconds;
}

std::string blend_frame_counters_json(const BlendFrameCounters& counters) {
    std::string json = "{\"copied_pixels\":" + std::to_string(counters.copied_pixels);
    json += ",\"blended_pixels\":" + std::to_string(counters.blended_pixels);
    json += ",\"overdraw_pixels\":" + std::to_string(counters.overdraw_pixels);
    json += ",\"overdraw_writes\":" + std::to_string(counters.overdraw_writes);
    json += ",\"depth_histogram\":[";
    for (std::size_t bin = 0U; bin < blend_depth_bins; ++bin) {
        if (bin != 0U) json += ",";
        json += std::to_string(counters.depth_histogram[bin]);
    }
    json += "],\"opaque_fills_blended\":" + std::to_string(counters.opaque_fills_blended);
    json += ",\"opaque_images_blended\":" + std::to_string(counters.opaque_images_blended);
    json += ",\"draws_counted\":" + std::to_string(counters.draws_counted);
    json += ",\"text_runs_unmeasured\":" + std::to_string(counters.text_runs_unmeasured);
    json += ",\"refused\":" + std::to_string(counters.refused);
    json += ",\"paint_nanoseconds\":" + std::to_string(counters.paint_nanoseconds);
    json += "}";
    return json;
}

// -- the process switch -------------------------------------------------------------

BlendStudyMode blend_study_requested_by_environment() noexcept {
    const char* const value = std::getenv("GUI_FORMS_DEBUG_BLEND");
    if (value == nullptr) return BlendStudyMode::off;
    const std::string_view text(value);
    if (text.empty() || text == "0") return BlendStudyMode::off;
    if (text == "depth") return BlendStudyMode::overdraw_heat_map;
    if (text == "blend") return BlendStudyMode::blend_heat_map;
    return BlendStudyMode::counters;
}

void set_blend_study_default(const BlendStudyMode mode) noexcept {
    process_switch.store(switch_value(mode), std::memory_order_release);
}

BlendStudyMode blend_study_default() noexcept {
    std::uint8_t state = process_switch.load(std::memory_order_acquire);
    if (state == switch_unread) {
        const std::uint8_t read = switch_value(blend_study_requested_by_environment());
        // A program's set wins over the environment read racing it.
        std::uint8_t expected = switch_unread;
        if (process_switch.compare_exchange_strong(expected, read, std::memory_order_acq_rel)) {
            state = read;
        } else {
            state = expected;
        }
    }
    const BlendStudyMode mode = switch_mode(state);
    return mode;
}

// -- the log ------------------------------------------------------------------------

namespace {

// The log's file, opened once under its lock to append (STXlib's
// FileMode::append); it lives for the process, and the host closes it at
// exit.
struct BlendStudyLog final {
    stxlib::Mutex lock{};
    bool opened{};
    stxlib::File file{};
};

// Never destroyed: a window painting on another thread during exit may
// still append.
BlendStudyLog& blend_study_log() {
    static BlendStudyLog* const log = new BlendStudyLog();
    return *log;
}

// Quotes `text` for JSON: backslash and quote escaped, control characters
// dropped (a stable id holds none).
std::string json_quoted(const std::string_view text) {
    std::string quoted = "\"";
    for (const char character : text) {
        const bool control = static_cast<unsigned char>(character) < 0x20U;
        if (control) continue;
        if (character == '"' || character == '\\') quoted += '\\';
        quoted += character;
    }
    quoted += '"';
    return quoted;
}

} // namespace

void append_blend_study_log(const std::string_view window, const std::uint64_t frame,
                            const BlendFrameCounters& counters) noexcept {
    try {
        BlendStudyLog& log = blend_study_log();
        const std::string line = "{\"window\":" + json_quoted(window) + ",\"frame\":" + std::to_string(frame) +
                                 ",\"counters\":" + blend_frame_counters_json(counters) + "}\n";
        const stxlib::ScopedLock held(log.lock);
        if (!log.opened) {
            log.opened = true;
            const char* const path = std::getenv("GUI_FORMS_BLEND_STUDY_LOG");
            if (path != nullptr && path[0] != '\0') {
                stxlib::OperationResult<stxlib::File> opened =
                    platform::open_file(std::filesystem::path(path), stxlib::FileMode::append);
                if (opened.succeeded()) {
                    log.file = static_cast<stxlib::OperationResult<stxlib::File>&&>(opened).take_value();
                }
            }
        }
        if (!log.file.is_open()) return;
        // Each line goes to the host whole, unbuffered, as the flushed line
        // did; a failed write is dropped.
        const stxlib::View<stxlib::Byte> bytes(reinterpret_cast<const stxlib::Byte*>(line.data()),
                                               static_cast<stxlib::usize>(line.size()));
        static_cast<void>(log.file.write_all(bytes));
    } catch (const std::exception&) {
        // A line that cannot be made (storage) is dropped: the log is
        // diagnostics and never fails a paint.
        return;
    }
}

// -- the heat map -------------------------------------------------------------------

void compose_blend_heat_map(const InvalidationOverlayTarget& target, const BlendHeatMapView& map,
                            const DevicePixelRect clip) noexcept {
    const bool depth_mode = map.mode == BlendStudyMode::overdraw_heat_map;
    const bool class_mode = map.mode == BlendStudyMode::blend_heat_map;
    if (!depth_mode && !class_mode) return;
    const std::uint64_t live = static_cast<std::uint64_t>(map.width) * map.height;
    if (map.depth.size() < live || map.classes.size() < live) return;
    const std::int32_t width = static_cast<std::int32_t>(std::min(target.width, map.width));
    const std::int32_t height = static_cast<std::int32_t>(std::min(target.height, map.height));
    const std::int32_t left = std::clamp<std::int32_t>(clip.left, 0, width);
    const std::int32_t right = std::clamp<std::int32_t>(clip.right, left, width);
    const std::int32_t top = std::clamp<std::int32_t>(clip.top, 0, height);
    const std::int32_t bottom = std::clamp<std::int32_t>(clip.bottom, top, height);
    if (left == right || top == bottom) return;
    // Every row the loops reach lies inside the target's bytes.
    const std::size_t last_row_end =
        static_cast<std::size_t>(bottom - 1) * target.row_bytes + static_cast<std::size_t>(right) * 4U;
    if (target.row_bytes < static_cast<std::size_t>(width) * 4U || last_row_end > target.pixels.size()) return;
    // The mode is chosen before the loops: one loop each.
    if (depth_mode) {
        for (std::int32_t y = top; y < bottom; ++y) {
            std::byte* const row = target.pixels.data() + static_cast<std::size_t>(y) * target.row_bytes;
            const std::uint8_t* const depth = map.depth.data() + static_cast<std::size_t>(y) * map.width;
            for (std::int32_t x = left; x < right; ++x) {
                const std::size_t index = std::min<std::size_t>(depth[x], depth_tints.size() - 1U);
                tint_pixel(row + static_cast<std::size_t>(x) * 4U, depth_tints[index]);
            }
        }
        return;
    }
    for (std::int32_t y = top; y < bottom; ++y) {
        std::byte* const row = target.pixels.data() + static_cast<std::size_t>(y) * target.row_bytes;
        const std::uint8_t* const classes = map.classes.data() + static_cast<std::size_t>(y) * map.width;
        for (std::int32_t x = left; x < right; ++x) {
            const std::size_t index = classes[x] & 3U;
            tint_pixel(row + static_cast<std::size_t>(x) * 4U, class_tints[index]);
        }
    }
}

// -- the planes ---------------------------------------------------------------------

bool BlendStudyPlanes::begin_frame(const std::uint32_t width, const std::uint32_t height,
                                   const std::span<const DevicePixelRect> painted) noexcept {
    counters_ = BlendFrameCounters{};
    active_ = false;
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels == 0U || pixels > maximum_blend_study_pixels) return false;
    const std::size_t live = static_cast<std::size_t>(pixels);
    const bool resized = width != width_ || height != height_;
    try {
        if (depth_.size() < live) depth_.resize(live, 0U);
        if (classes_.size() < live) classes_.resize(live, 0U);
    } catch (const std::bad_alloc&) {
        release();
        return false;
    }
    if (resized) {
        // A new row length makes the old bytes meaningless.
        std::fill_n(depth_.begin(), live, std::uint8_t{0U});
        std::fill_n(classes_.begin(), live, std::uint8_t{0U});
        width_ = width;
        height_ = height;
    }
    active_ = true;
    for (const DevicePixelRect rectangle : painted) {
        const DevicePixelRect area = clip_to_planes(rectangle);
        const std::size_t span = static_cast<std::size_t>(area.right - area.left);
        for (std::int32_t y = area.top; y < area.bottom; ++y) {
            const std::size_t start = static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(area.left);
            std::fill_n(depth_.begin() + static_cast<std::ptrdiff_t>(start), span, std::uint8_t{0U});
            std::fill_n(classes_.begin() + static_cast<std::ptrdiff_t>(start), span, std::uint8_t{0U});
        }
    }
    return true;
}

DevicePixelRect BlendStudyPlanes::clip_to_planes(const DevicePixelRect area) const noexcept {
    const std::int32_t width = static_cast<std::int32_t>(width_);
    const std::int32_t height = static_cast<std::int32_t>(height_);
    DevicePixelRect clipped{};
    clipped.left = std::clamp<std::int32_t>(area.left, 0, width);
    clipped.right = std::clamp<std::int32_t>(area.right, clipped.left, width);
    clipped.top = std::clamp<std::int32_t>(area.top, 0, height);
    clipped.bottom = std::clamp<std::int32_t>(area.bottom, clipped.top, height);
    return clipped;
}

// The copy kernel: no decision per pixel. Each pixel's earlier writes are
// summed (overdraw_writes) and counted once if any (overdraw_pixels, a
// comparison turned into 0 or 1), then it is set to one copied write.
void BlendStudyPlanes::replace(const DevicePixelRect area) noexcept {
    if (!active_ || area.right <= area.left || area.bottom <= area.top) return;
    const std::size_t span = static_cast<std::size_t>(area.right - area.left);
    std::uint64_t writes = 0U;
    std::uint64_t pixels = 0U;
    for (std::int32_t y = area.top; y < area.bottom; ++y) {
        const std::size_t start = static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(area.left);
        std::uint8_t* const depth = depth_.data() + start;
        std::uint8_t* const classes = classes_.data() + start;
        std::uint64_t row_writes = 0U;
        std::uint64_t row_pixels = 0U;
        for (std::size_t x = 0U; x < span; ++x) {
            const std::uint8_t earlier = depth[x];
            row_writes += earlier;
            row_pixels += static_cast<std::uint64_t>(earlier != 0U);
            depth[x] = 1U;
            classes[x] = blend_class_copied;
        }
        writes += row_writes;
        pixels += row_pixels;
    }
    counters_.overdraw_writes += writes;
    counters_.overdraw_pixels += pixels;
    counters_.copied_pixels += static_cast<std::uint64_t>(span) * static_cast<std::uint64_t>(area.bottom - area.top);
}

// The layer kernel: the write and its bit are chosen before the loop; the
// depth saturates at 255 by adding the comparison's 0 or 1.
void BlendStudyPlanes::layer(const DevicePixelRect area, const BlendWrite write) noexcept {
    if (!active_ || area.right <= area.left || area.bottom <= area.top) return;
    const std::size_t span = static_cast<std::size_t>(area.right - area.left);
    const std::uint8_t bit = write == BlendWrite::copied ? blend_class_copied : blend_class_blended;
    for (std::int32_t y = area.top; y < area.bottom; ++y) {
        const std::size_t start = static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(area.left);
        std::uint8_t* const depth = depth_.data() + start;
        std::uint8_t* const classes = classes_.data() + start;
        for (std::size_t x = 0U; x < span; ++x) {
            const std::uint8_t earlier = depth[x];
            depth[x] = static_cast<std::uint8_t>(earlier + static_cast<std::uint8_t>(earlier != 255U));
            classes[x] = static_cast<std::uint8_t>(classes[x] | bit);
        }
    }
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(span) * static_cast<std::uint64_t>(area.bottom - area.top);
    if (write == BlendWrite::copied) {
        counters_.copied_pixels += pixels;
    } else {
        counters_.blended_pixels += pixels;
    }
}

BlendFrameCounters BlendStudyPlanes::end_frame(const std::span<const DevicePixelRect> painted) noexcept {
    if (!active_) {
        BlendFrameCounters refused{};
        refused.refused = 1U;
        return refused;
    }
    std::array<std::uint64_t, blend_depth_bins>& histogram = counters_.depth_histogram;
    for (const DevicePixelRect rectangle : painted) {
        const DevicePixelRect area = clip_to_planes(rectangle);
        const std::size_t span = static_cast<std::size_t>(area.right - area.left);
        for (std::int32_t y = area.top; y < area.bottom; ++y) {
            const std::uint8_t* const depth =
                depth_.data() + static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(area.left);
            for (std::size_t x = 0U; x < span; ++x) {
                const std::size_t bin = std::min<std::size_t>(depth[x], blend_depth_bins - 1U);
                histogram[bin] += 1U;
            }
        }
    }
    active_ = false;
    const BlendFrameCounters counted = counters_;
    return counted;
}

BlendHeatMapView BlendStudyPlanes::heat_map(const BlendStudyMode mode) const& noexcept {
    BlendHeatMapView view{};
    const std::size_t live = static_cast<std::size_t>(width_) * height_;
    if (depth_.size() < live || classes_.size() < live || live == 0U) return view;
    view.mode = mode;
    view.depth = std::span<const std::uint8_t>(depth_.data(), live);
    view.classes = std::span<const std::uint8_t>(classes_.data(), live);
    view.width = width_;
    view.height = height_;
    return view;
}

void BlendStudyPlanes::release() noexcept {
    std::vector<std::uint8_t>().swap(depth_);
    std::vector<std::uint8_t>().swap(classes_);
    width_ = 0U;
    height_ = 0U;
    active_ = false;
}

} // namespace gui_forms
