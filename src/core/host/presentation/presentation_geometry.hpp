#pragma once
// The presentation shim's geometry (docs/user/presentation.md): device
// rectangles of one presented buffer, damage clipping, host scroll plans,
// the damage history a buffer of a given age is written with, and the two
// row kernels the shim writes buffers with (a copy from the frame and a
// move inside one buffer).
//
// Plain portable C++: no window system's types or assumptions. Every
// function here is pure over its arguments except the two kernels, which
// write only the target pixels named. Tested on every platform
// (tests/presentation_shim_tests.cpp).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::host::presentation {

// The largest buffer side the shim accepts, in device pixels. Every
// coordinate below then fits an int32_t with room for an offset of the same
// size, so the arithmetic is done in std::int64_t only where two such
// values are added.
inline constexpr std::uint32_t maximum_present_side = 32768U;

// A buffer's size in device pixels. Both sides are at least 1 for a buffer
// that exists; {0, 0} is no buffer.
struct PresentExtent final {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] bool empty() const noexcept { return width == 0U || height == 0U; }
    friend bool operator==(PresentExtent a, PresentExtent b) noexcept {
        const bool equal = a.width == b.width && a.height == b.height;
        return equal;
    }
    friend bool operator!=(PresentExtent a, PresentExtent b) noexcept { return !(a == b); }
};

// A rectangle in device pixels of one buffer, edges exclusive at right and
// bottom. Empty when right <= left or bottom <= top.
struct PresentRect final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    [[nodiscard]] bool empty() const noexcept { return right <= left || bottom <= top; }
    // The pixel count; 0 when empty.
    [[nodiscard]] std::uint64_t area() const noexcept;
    friend bool operator==(PresentRect a, PresentRect b) noexcept {
        const bool equal = a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
        return equal;
    }
    friend bool operator!=(PresentRect a, PresentRect b) noexcept { return !(a == b); }
};

// The whole buffer as a rectangle.
[[nodiscard]] PresentRect present_extent_rect(PresentExtent extent) noexcept;
// The overlap of two rectangles; an empty {0, 0, 0, 0} when they do not meet.
[[nodiscard]] PresentRect present_rect_intersection(PresentRect a, PresentRect b) noexcept;
// The smallest rectangle holding both; an empty one is ignored, and two
// empty ones give an empty {0, 0, 0, 0}.
[[nodiscard]] PresentRect present_rect_bounds(PresentRect a, PresentRect b) noexcept;
// Whether `outer` holds every pixel of `inner` (an empty inner is held).
[[nodiscard]] bool present_rect_contains(PresentRect outer, PresentRect inner) noexcept;

// The pixels of `rects` summed (overlaps counted twice).
[[nodiscard]] std::uint64_t present_rects_area(std::span<const PresentRect> rects) noexcept;

// Collects rectangles clipped to a buffer into a caller's array, keeping
// the bounding rectangle of everything offered so that an overflow
// collapses to it. Borrows `out` for its life; no allocation.
class PresentRectCollector final {
public:
    PresentRectCollector(PresentExtent extent, std::span<PresentRect> out) noexcept
        : clip_(present_extent_rect(extent)), out_(out) {}

    // Clips `rect` to the buffer and keeps it unless empty or inside a
    // rectangle already kept.
    void add(PresentRect rect) noexcept;
    // The count kept: the live count, or 1 (the bounding rectangle in
    // out[0]) after an overflow; 0 for an empty `out` or nothing kept.
    // The rectangles are out[0] to out[count - 1].
    [[nodiscard]] std::size_t finish() noexcept;

private:
    PresentRect clip_{};
    PresentRect bounds_{};
    std::span<PresentRect> out_;
    std::size_t offered_{};
};

// Clips each rectangle of `damage` to `extent`, drops the empty ones, and
// writes the rest into `out` in order. When more than out.size() remain,
// out[0] holds their one bounding rectangle instead and 1 is returned.
// Returns the count written (0 when `out` is empty or nothing remains).
// `damage` and `out` must not overlap.
std::size_t clip_present_damage(std::span<const PresentRect> damage, PresentExtent extent,
                                std::span<PresentRect> out) noexcept;

// A host scroll: the pixels presented inside `area` move by (dx, dy), and
// what moves outside `area` is dropped. Positive dx moves right, positive
// dy moves down.
struct PresentScroll final {
    PresentRect area{};
    std::int32_t dx{};
    std::int32_t dy{};
};

// What a host scroll does to one buffer, worked out before any pixel moves.
// `area` is the scroll's area clipped to the buffer. The pixels at `source`
// land at `destination` (the same size, both inside `area`); `exposed`
// holds the parts of `area` that `destination` does not cover, which the
// frame must repaint: at most two disjoint bands, a full-width one on the
// side the rows left and, beside it, one on the side the columns left.
// destination and the exposed bands partition `area`. When the offset is
// at least the area's size (or the area is empty), nothing moves: source
// and destination are empty and the whole area is exposed.
struct PresentScrollPlan final {
    PresentRect area{};
    PresentRect source{};
    PresentRect destination{};
    std::array<PresentRect, 2> exposed{};
    std::size_t exposed_count{};

    [[nodiscard]] bool moves() const noexcept { return !destination.empty(); }
};

[[nodiscard]] PresentScrollPlan plan_present_scroll(const PresentScroll& scroll, PresentExtent extent) noexcept;

// The damage the frames before the current one wrote, so that a buffer
// presented some frames ago can be brought up to date by writing only what
// changed since (a buffer's age: 1 for the buffer presented last, n for the
// one presented n presents ago, 0 for contents unknown). It keeps the last
// `frames` presents, each as at most `rects_per_frame` rectangles (more are
// kept as their bounding rectangle). Fixed storage, no allocation. One
// execution context: the shim's.
class PresentDamageHistory final {
public:
    static constexpr std::size_t frames = 4U;
    static constexpr std::size_t rects_per_frame = 16U;

    // Forgets every frame: every later age above 1 is unknown. Called on a
    // path change, a new buffer size, or a lost buffer.
    void reset() noexcept;
    // Records one presented frame's damage as the newest.
    void record(std::span<const PresentRect> damage) noexcept;
    // Writes into `out` what a buffer of `age` must be written with for the
    // current frame: `current`, plus the damage of the age - 1 frames
    // recorded before it, clipped to `extent` (clip_present_damage's
    // bounding rule when `out` is too small). Returns false, writing
    // nothing, when the age is 0 or older than the frames kept: the whole
    // buffer must then be written. `current` and `out` must not overlap.
    [[nodiscard]] bool accumulate(std::uint32_t age, std::span<const PresentRect> current, PresentExtent extent,
                                  std::span<PresentRect> out, std::size_t& count) const noexcept;
    // The frames recorded since the last reset, at most `frames`.
    [[nodiscard]] std::size_t recorded() const noexcept { return recorded_; }

private:
    struct Frame final {
        std::array<PresentRect, rects_per_frame> rects{};
        std::size_t count{};
    };
    // A ring: newest_ is the slot of the newest frame; recorded_ slots
    // counting back from it are live.
    std::array<Frame, frames> ring_{};
    std::size_t newest_{};
    std::size_t recorded_{};
};

// A frame the shim reads: `extent.height` rows of `extent.width`
// premultiplied BGRA8 pixels, `row_bytes` apart (at least width * 4),
// borrowed for one call.
struct PresentSourcePixels final {
    const std::byte* pixels{};
    std::size_t row_bytes{};
    PresentExtent extent{};
};

// A buffer the shim writes, in the same layout; borrowed between acquire
// and present.
struct PresentTargetPixels final {
    std::byte* pixels{};
    std::size_t row_bytes{};
    PresentExtent extent{};
};

// Copies each rectangle of `rects` from `source` to the same place in
// `target`, row by row. Validated before the first row: every rectangle
// must be inside both extents, the layouts must hold width * 4 bytes a row,
// and the two buffers must not overlap. Returns false, writing nothing,
// when any of that fails. Empty rectangles are skipped.
[[nodiscard]] bool copy_present_rects(const PresentSourcePixels& source, const PresentTargetPixels& target,
                                      std::span<const PresentRect> rects) noexcept;

// Moves plan.source to plan.destination inside `target` (a host scroll in
// one buffer). Rows are visited away from the direction of motion and each
// row is moved with memmove, so the overlap is safe. Returns false, moving
// nothing, when the plan does not move or lies outside the buffer.
[[nodiscard]] bool move_present_pixels(const PresentTargetPixels& target, const PresentScrollPlan& plan) noexcept;

} // namespace gui_forms::host::presentation
