#pragma once

// The invalidation overlay (owner request 2026-11-09, for the rendering
// glitches of the SDR# swap-in and PlaySuite): a debugging mode that outlines
// every area a window presented again, for invalidation_outline_lifetime
// after it was presented, in a style that names what caused it, with a
// legend.
//
// The window records outlines as it paints (SurfaceRoot::paint attributes
// each painted rectangle to the damage that asked for it) and as its host
// presents live and external content (SurfaceRoot::record_presented_area).
// The host composites the outlines over a copy of the presented frame at
// present time, never into the window's own raster, so the overlay causes
// no damage and reports none of its own repaints: a fading outline is
// presented again from the raster, not painted again.
//
// Switched at run time: the process switch (set_invalidation_overlay_default,
// read at first from the environment variable GUI_FORMS_DEBUG_INVALIDATION),
// a window's own setting (Window::set_invalidation_overlay), the C ABI
// (diagnostics.set_invalidation_overlay) and the Gallery's diagnostics
// panel. Off, a window holds no overlay and pays one relaxed atomic load a
// damage record and a paint.

#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// What caused an area to be presented again.
enum class InvalidationCause : std::uint8_t {
    // A control invalidated its paint (or its structure): the source names
    // it. Red, solid.
    control,
    // A layout pass moved or resized a control, or the window resized.
    // Orange, dashed.
    layout,
    // The host asked for the area (an expose: a window uncovered, a native
    // update region) with no damage of the window's own. Blue, dotted.
    host_expose,
    // A scroll copied pixels instead of painting them: the copied area, and
    // the band it exposed. Green, long dashes.
    scroll_copy,
    // A live surface (or a sub-surface's output) presented a frame. Magenta,
    // double line.
    live_surface,
    // External double-buffered content presented through a compatibility
    // paint endpoint (child-window or GDI content). Cyan, double dashed.
    external_content,
};
inline constexpr std::size_t invalidation_cause_count = 6U;

// How long an outline shows after the area was presented; it fades
// linearly to nothing over this time.
inline constexpr std::chrono::milliseconds invalidation_outline_lifetime{300};

// The outlines a window keeps at once; the oldest is dropped for a new one.
inline constexpr std::size_t maximum_invalidation_outlines = 512U;

// The characters of an outline's source label it shows.
inline constexpr std::size_t maximum_invalidation_label = 32U;

// The time outlines are stamped and aged with.
using InvalidationClock = std::chrono::steady_clock;

// One presented area: its bounds in window logical units, its cause, the
// control that caused it where known (its stable id; empty otherwise), and
// when it was presented.
struct InvalidationOutline final {
    Rect bounds{};
    InvalidationCause cause{InvalidationCause::control};
    std::string source{};
    InvalidationClock::time_point at{};
};

// A cause's outline: its colour, its stroke width in logical units, its
// dash and gap lengths in logical units (no dash: solid), and whether it is
// drawn as two parallel lines.
struct InvalidationOutlineStyle final {
    Color colour{};
    double width{};
    double dash{};
    double gap{};
    bool double_line{};
};

[[nodiscard]] InvalidationOutlineStyle invalidation_outline_style(InvalidationCause cause) noexcept;
// The cause's name as the legend shows it: "control", "layout", "expose",
// "scroll copy", "live surface", "external".
[[nodiscard]] std::string_view invalidation_cause_name(InvalidationCause cause) noexcept;

// Whether GUI_FORMS_DEBUG_INVALIDATION asks for the overlay: set, and
// neither empty nor "0".
[[nodiscard]] bool invalidation_overlay_requested_by_environment() noexcept;
// The process switch every window without a setting of its own follows; on
// the first read it is the environment's request. Any thread.
void set_invalidation_overlay_default(bool enabled) noexcept;
[[nodiscard]] bool invalidation_overlay_default() noexcept;
// Counts each change of the process switch, so a window notices one with a
// single relaxed load. Any thread.
[[nodiscard]] std::uint64_t invalidation_overlay_default_generation() noexcept;

// A frame's pixels the overlay composites onto: `height` rows of `width`
// premultiplied BGRA pixels, `row_bytes` apart. Borrowed for the call.
struct InvalidationOverlayTarget final {
    std::span<std::byte> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_bytes{};
};

// A rectangle of whole device pixels: [left, right) by [top, bottom).
struct DevicePixelRect final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    friend bool operator==(const DevicePixelRect& first, const DevicePixelRect& second) noexcept {
        return first.left == second.left && first.top == second.top && first.right == second.right &&
               first.bottom == second.bottom;
    }
};

// A window's live outlines. Owned by the window (SurfaceRoot), on its UI
// thread; the host reads it there to composite.
class InvalidationOverlay final {
public:
    // Adds an outline presented at `at`; an empty or non-finite rectangle
    // adds none. The source is cut to maximum_invalidation_label bytes.
    void record(Rect bounds, InvalidationCause cause, std::string_view source, InvalidationClock::time_point at);
    // Drops the outlines older than invalidation_outline_lifetime at `now`.
    void expire(InvalidationClock::time_point now);
    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept { return outlines_.empty(); }
    // Oldest first.
    [[nodiscard]] std::span<const InvalidationOutline> outlines() const& noexcept { return outlines_; }
    std::span<const InvalidationOutline> outlines() const&& = delete;
    // Every outline recorded, by cause, since the overlay was made.
    [[nodiscard]] const std::array<std::uint64_t, invalidation_cause_count>& recorded() const noexcept {
        return recorded_;
    }
    // The device rectangles the overlay draws on at `scale` in a client of
    // `width` by `height` device pixels, into `regions` (replacing its
    // contents): each outline's four stroke bands and its label, and the
    // legend while any outline shows. A host presents these again from its
    // raster when the overlay changes, so that a faded outline leaves
    // nothing behind.
    void fill_regions(double scale, std::uint32_t width, std::uint32_t height,
                      std::vector<DevicePixelRect>& regions) const;
    // Composites the outlines, their labels and the legend at `now` onto
    // `target` at `scale`, inside `clip` only; each outline's opacity falls
    // with its age. The target is a copy of what the window presents.
    void compose(const InvalidationOverlayTarget& target, double scale, InvalidationClock::time_point now,
                 DevicePixelRect clip) const;

private:
    std::vector<InvalidationOutline> outlines_{};
    std::array<std::uint64_t, invalidation_cause_count> recorded_{};
};

} // namespace gui_forms
