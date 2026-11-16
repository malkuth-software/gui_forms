#include "presentation_geometry.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace gui_forms::host::presentation {

namespace {

// The byte span [first, last) a pixel layout covers, for the overlap test.
struct ByteSpan final {
    std::uintptr_t first{};
    std::uintptr_t last{};
};

// Whether a layout of `extent` with `row_bytes` is usable: both sides in
// range, rows at least width * 4 bytes, and the whole span representable.
bool layout_usable(std::size_t row_bytes, PresentExtent extent) noexcept {
    if (extent.empty()) return false;
    if (extent.width > maximum_present_side || extent.height > maximum_present_side) return false;
    const std::size_t row_length = static_cast<std::size_t>(extent.width) * 4U;
    if (row_bytes < row_length) return false;
    const std::size_t rows = static_cast<std::size_t>(extent.height);
    if (row_bytes > std::numeric_limits<std::size_t>::max() / rows) return false;
    return true;
}

ByteSpan layout_span(const std::byte* pixels, std::size_t row_bytes, PresentExtent extent) noexcept {
    const std::size_t rows_before_last = static_cast<std::size_t>(extent.height) - 1U;
    const std::size_t length = row_bytes * rows_before_last + static_cast<std::size_t>(extent.width) * 4U;
    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(pixels);
    const ByteSpan span{first, first + length};
    return span;
}

std::int64_t magnitude(std::int64_t value) noexcept {
    const std::int64_t result = value < 0 ? -value : value;
    return result;
}

} // namespace

std::uint64_t PresentRect::area() const noexcept {
    if (empty()) return 0U;
    const std::uint64_t columns = static_cast<std::uint64_t>(static_cast<std::int64_t>(right) - left);
    const std::uint64_t rows = static_cast<std::uint64_t>(static_cast<std::int64_t>(bottom) - top);
    const std::uint64_t pixels = columns * rows;
    return pixels;
}

std::uint64_t present_rects_area(std::span<const PresentRect> rects) noexcept {
    std::uint64_t pixels = 0U;
    for (std::size_t index = 0U; index < rects.size(); ++index) pixels += rects[index].area();
    return pixels;
}

void PresentRectCollector::add(PresentRect rect) noexcept {
    const PresentRect clipped = present_rect_intersection(rect, clip_);
    if (clipped.empty()) return;
    // A rectangle inside one already kept adds nothing (an exposed band
    // that the damage also names). Checked only while no overflow has
    // happened, against at most out_.size() rectangles.
    if (offered_ <= out_.size()) {
        for (std::size_t index = 0U; index < offered_; ++index) {
            if (present_rect_contains(out_[index], clipped)) return;
        }
    }
    bounds_ = present_rect_bounds(bounds_, clipped);
    if (offered_ < out_.size()) out_[offered_] = clipped;
    ++offered_;
}

std::size_t PresentRectCollector::finish() noexcept {
    if (out_.empty()) return 0U;
    if (offered_ > out_.size()) {
        out_[0] = bounds_;
        return 1U;
    }
    return offered_;
}

PresentRect present_extent_rect(PresentExtent extent) noexcept {
    const std::uint32_t width = std::min(extent.width, maximum_present_side);
    const std::uint32_t height = std::min(extent.height, maximum_present_side);
    const PresentRect rect{0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)};
    return rect;
}

PresentRect present_rect_intersection(PresentRect a, PresentRect b) noexcept {
    const PresentRect overlap{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
                              std::min(a.bottom, b.bottom)};
    if (overlap.empty()) return PresentRect{};
    return overlap;
}

PresentRect present_rect_bounds(PresentRect a, PresentRect b) noexcept {
    if (a.empty() && b.empty()) return PresentRect{};
    if (a.empty()) return b;
    if (b.empty()) return a;
    const PresentRect bounds{std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
                             std::max(a.bottom, b.bottom)};
    return bounds;
}

bool present_rect_contains(PresentRect outer, PresentRect inner) noexcept {
    if (inner.empty()) return true;
    const bool contained = inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
                           inner.bottom <= outer.bottom;
    return contained;
}

std::size_t clip_present_damage(std::span<const PresentRect> damage, PresentExtent extent,
                                std::span<PresentRect> out) noexcept {
    PresentRectCollector sink(extent, out);
    for (std::size_t index = 0U; index < damage.size(); ++index) sink.add(damage[index]);
    const std::size_t count = sink.finish();
    return count;
}

PresentScrollPlan plan_present_scroll(const PresentScroll& scroll, PresentExtent extent) noexcept {
    PresentScrollPlan plan{};
    plan.area = present_rect_intersection(scroll.area, present_extent_rect(extent));
    if (plan.area.empty()) return plan;
    const PresentRect area = plan.area;
    const std::int64_t width = static_cast<std::int64_t>(area.right) - area.left;
    const std::int64_t height = static_cast<std::int64_t>(area.bottom) - area.top;
    const std::int64_t dx = scroll.dx;
    const std::int64_t dy = scroll.dy;
    // An offset of the area's size or more moves nothing into the area.
    if (magnitude(dx) >= width || magnitude(dy) >= height) {
        plan.exposed[0] = area;
        plan.exposed_count = 1U;
        return plan;
    }
    // From here every edge plus its offset stays inside the area, so the
    // narrowing casts below are exact.
    plan.destination = PresentRect{static_cast<std::int32_t>(area.left + std::max<std::int64_t>(dx, 0)),
                                   static_cast<std::int32_t>(area.top + std::max<std::int64_t>(dy, 0)),
                                   static_cast<std::int32_t>(area.right + std::min<std::int64_t>(dx, 0)),
                                   static_cast<std::int32_t>(area.bottom + std::min<std::int64_t>(dy, 0))};
    plan.source = PresentRect{static_cast<std::int32_t>(plan.destination.left - dx),
                              static_cast<std::int32_t>(plan.destination.top - dy),
                              static_cast<std::int32_t>(plan.destination.right - dx),
                              static_cast<std::int32_t>(plan.destination.bottom - dy)};
    // The band the rows left, full width.
    if (dy > 0) {
        plan.exposed[plan.exposed_count] = PresentRect{area.left, area.top, area.right, plan.destination.top};
        ++plan.exposed_count;
    } else if (dy < 0) {
        plan.exposed[plan.exposed_count] = PresentRect{area.left, plan.destination.bottom, area.right, area.bottom};
        ++plan.exposed_count;
    }
    // The band the columns left, over the destination's rows only, so the
    // two bands do not overlap.
    if (dx > 0) {
        plan.exposed[plan.exposed_count] =
            PresentRect{area.left, plan.destination.top, plan.destination.left, plan.destination.bottom};
        ++plan.exposed_count;
    } else if (dx < 0) {
        plan.exposed[plan.exposed_count] =
            PresentRect{plan.destination.right, plan.destination.top, area.right, plan.destination.bottom};
        ++plan.exposed_count;
    }
    return plan;
}

void PresentDamageHistory::reset() noexcept {
    newest_ = 0U;
    recorded_ = 0U;
}

void PresentDamageHistory::record(std::span<const PresentRect> damage) noexcept {
    const std::size_t slot = recorded_ == 0U ? 0U : (newest_ + 1U) % frames;
    Frame& frame = ring_[slot];
    frame.count = 0U;
    PresentRect bounds{};
    std::size_t offered = 0U;
    for (std::size_t index = 0U; index < damage.size(); ++index) {
        const PresentRect rect = damage[index];
        if (rect.empty()) continue;
        bounds = present_rect_bounds(bounds, rect);
        if (offered < rects_per_frame) frame.rects[offered] = rect;
        ++offered;
    }
    if (offered > rects_per_frame) {
        frame.rects[0] = bounds;
        frame.count = 1U;
    } else {
        frame.count = offered;
    }
    newest_ = slot;
    if (recorded_ < frames) ++recorded_;
}

bool PresentDamageHistory::accumulate(std::uint32_t age, std::span<const PresentRect> current, PresentExtent extent,
                                      std::span<PresentRect> out, std::size_t& count) const noexcept {
    if (age == 0U) return false;
    const std::size_t older = static_cast<std::size_t>(age) - 1U;
    if (older > recorded_) return false;
    PresentRectCollector sink(extent, out);
    for (std::size_t index = 0U; index < current.size(); ++index) sink.add(current[index]);
    for (std::size_t back = 0U; back < older; ++back) {
        const std::size_t slot = (newest_ + frames - back) % frames;
        const Frame& frame = ring_[slot];
        for (std::size_t index = 0U; index < frame.count; ++index) sink.add(frame.rects[index]);
    }
    count = sink.finish();
    return true;
}

bool copy_present_rects(const PresentSourcePixels& source, const PresentTargetPixels& target,
                        std::span<const PresentRect> rects) noexcept {
    // Every fact the row loop relies on is established here.
    if (source.pixels == nullptr || target.pixels == nullptr) return false;
    if (!layout_usable(source.row_bytes, source.extent) || !layout_usable(target.row_bytes, target.extent))
        return false;
    const ByteSpan read = layout_span(source.pixels, source.row_bytes, source.extent);
    const ByteSpan written = layout_span(target.pixels, target.row_bytes, target.extent);
    if (read.first < written.last && written.first < read.last) return false;
    const PresentRect readable = present_extent_rect(source.extent);
    const PresentRect writable = present_extent_rect(target.extent);
    for (std::size_t index = 0U; index < rects.size(); ++index) {
        if (!present_rect_contains(readable, rects[index]) || !present_rect_contains(writable, rects[index]))
            return false;
    }
    for (std::size_t index = 0U; index < rects.size(); ++index) {
        const PresentRect rect = rects[index];
        if (rect.empty()) continue;
        const std::size_t column_offset = static_cast<std::size_t>(rect.left) * 4U;
        const std::size_t length = static_cast<std::size_t>(rect.right - rect.left) * 4U;
        for (std::int32_t row = rect.top; row < rect.bottom; ++row) {
            const std::size_t line = static_cast<std::size_t>(row);
            const std::byte* const from = source.pixels + line * source.row_bytes + column_offset;
            std::byte* const to = target.pixels + line * target.row_bytes + column_offset;
            std::memcpy(to, from, length);
        }
    }
    return true;
}

bool move_present_pixels(const PresentTargetPixels& target, const PresentScrollPlan& plan) noexcept {
    if (target.pixels == nullptr || !layout_usable(target.row_bytes, target.extent)) return false;
    if (!plan.moves()) return false;
    const PresentRect whole = present_extent_rect(target.extent);
    if (!present_rect_contains(whole, plan.source) || !present_rect_contains(whole, plan.destination)) return false;
    const std::int32_t columns = plan.destination.right - plan.destination.left;
    const std::int32_t rows = plan.destination.bottom - plan.destination.top;
    if (columns != plan.source.right - plan.source.left || rows != plan.source.bottom - plan.source.top) return false;
    const std::size_t length = static_cast<std::size_t>(columns) * 4U;
    const std::size_t from_column = static_cast<std::size_t>(plan.source.left) * 4U;
    const std::size_t to_column = static_cast<std::size_t>(plan.destination.left) * 4U;
    // Moving down, the bottom row goes first so that no source row is
    // written before it is read; moving up or sideways, the top row does.
    const bool bottom_up = plan.destination.top > plan.source.top;
    for (std::int32_t step = 0; step < rows; ++step) {
        const std::int32_t offset = bottom_up ? rows - 1 - step : step;
        const std::size_t from_row = static_cast<std::size_t>(plan.source.top + offset);
        const std::size_t to_row = static_cast<std::size_t>(plan.destination.top + offset);
        const std::byte* const from = target.pixels + from_row * target.row_bytes + from_column;
        std::byte* const to = target.pixels + to_row * target.row_bytes + to_column;
        std::memmove(to, from, length);
    }
    return true;
}

PresentHoldCount count_present_holds(std::span<const bool> displayed) noexcept {
    std::size_t newest_displayed = 0U;
    bool any = false;
    for (std::size_t index = 0U; index < displayed.size(); ++index) {
        if (!displayed[index]) continue;
        newest_displayed = index;
        any = true;
    }
    PresentHoldCount count{};
    count.displaced = any ? newest_displayed : 0U;
    count.held = displayed.size() - count.displaced;
    return count;
}

} // namespace gui_forms::host::presentation
