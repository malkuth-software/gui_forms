#include "host_scroll.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gui_forms::host::presentation {

namespace {

// Whether a device value is a whole pixel within the shim's range.
bool whole_device_value(const double value) noexcept {
    if (!std::isfinite(value)) return false;
    if (std::abs(value - std::round(value)) > 1.0e-6) return false;
    const double limit = static_cast<double>(maximum_present_side);
    const bool in_range = std::abs(value) <= limit;
    return in_range;
}

} // namespace

bool device_present_scroll(const ScrollCopy& copy, const double scale, PresentScroll& scroll) noexcept {
    const std::array<double, 6> values{copy.clip.x * scale,       copy.clip.y * scale,
                                       copy.clip.right() * scale, copy.clip.bottom() * scale,
                                       copy.offset.x * scale,     copy.offset.y * scale};
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (!whole_device_value(values[index])) return false;
    }
    // Every value is a whole number of at most maximum_present_side in
    // magnitude, so each lround is exact and fits an int32_t.
    PresentScroll made{};
    made.area.left = std::max(static_cast<std::int32_t>(std::lround(values[0])), 0);
    made.area.top = std::max(static_cast<std::int32_t>(std::lround(values[1])), 0);
    made.area.right = std::max(static_cast<std::int32_t>(std::lround(values[2])), made.area.left);
    made.area.bottom = std::max(static_cast<std::int32_t>(std::lround(values[3])), made.area.top);
    made.dx = static_cast<std::int32_t>(std::lround(values[4]));
    made.dy = static_cast<std::int32_t>(std::lround(values[5]));
    scroll = made;
    return true;
}

PresentOffset present_scroll_offset(const PresentScrollPlan& plan) noexcept {
    PresentOffset offset{};
    if (!plan.moves()) return offset;
    // Both rectangles lie inside the area (plan_present_scroll), so the
    // differences fit an int32_t.
    offset.dx = plan.destination.left - plan.source.left;
    offset.dy = plan.destination.top - plan.source.top;
    return offset;
}

HostScrollMove classify_host_scroll_move(const PresentScrollPlan& plan,
                                         std::span<const PresentRect> uncovered) noexcept {
    if (!plan.moves()) return HostScrollMove::partial;
    for (std::size_t index = 0U; index < uncovered.size(); ++index) {
        const PresentRect reported = uncovered[index];
        if (reported.empty()) continue;
        // The exposed bands are disjoint (plan_present_scroll), so the
        // reported rectangle lies inside their union exactly when its
        // overlaps with them add up to its own area.
        std::uint64_t inside = 0U;
        for (std::size_t band = 0U; band < plan.exposed_count; ++band) {
            inside += present_rect_intersection(reported, plan.exposed[band]).area();
        }
        if (inside != reported.area()) return HostScrollMove::partial;
    }
    return HostScrollMove::whole;
}

} // namespace gui_forms::host::presentation
