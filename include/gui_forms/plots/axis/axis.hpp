#pragma once

// Axis: a scale's kind and visible range with its presentation and
// behaviour (IMGUI_IMPLOT_STUDY B.3, P4), and AxisLink, a group of axes
// sharing one range (linked subplots, a plot and its overview).
//
// The range is in data units; `inverted` flips the device direction, so the
// range itself always keeps minimum < maximum. Pan and zoom (the view
// model, plot_view_model.hpp) work in the scale's transformed space and
// respect `lock`: a locked end does not move, and a zoom about a point
// with one end locked zooms about that end instead.

#include "gui_forms/plots/plot_types/plot_types.hpp"
#include "gui_forms/plots/scale/scale.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace gui_forms::plots {

enum class AxisSide : std::uint8_t { bottom, top, left, right };

[[nodiscard]] constexpr bool horizontal_side(AxisSide side) noexcept {
    return side == AxisSide::bottom || side == AxisSide::top;
}

enum class AxisLock : std::uint8_t { none, minimum, maximum, both };

// When an axis fits its range to the data: never, at the next layout only,
// on every data change, or to the data visible along the other axis.
enum class AxisFit : std::uint8_t { none, once, always, visible_range };

// A follow-tail axis keeps its maximum on the newest data and its extent at
// `span` (scrolling time); the user's pan stops it until a fit or End.
struct AxisFollow final {
    bool tail{};
    double span{};

    friend bool operator==(const AxisFollow& left, const AxisFollow& right) noexcept {
        return left.tail == right.tail && left.span == right.span;
    }
};

// The most axes of one orientation a plot holds.
inline constexpr std::size_t maximum_axes_per_orientation = 4U;

struct Axis final {
    ScaleKind kind{ScaleKind::linear};
    PlotRange range{};
    AxisSide side{AxisSide::bottom};
    bool inverted{};
    AxisLock lock{AxisLock::none};
    AxisFit fit{AxisFit::none};
    AxisFollow follow{};
    double symlog_width{1.0};

    friend bool operator==(const Axis& left, const Axis& right) noexcept {
        return left.kind == right.kind && left.range == right.range && left.side == right.side &&
               left.inverted == right.inverted && left.lock == right.lock && left.fit == right.fit &&
               left.follow == right.follow && left.symlog_width == right.symlog_width;
    }
};

// The scale an axis gives over the device interval [device_low,
// device_high] along it (left to right, or top to bottom). A horizontal
// axis maps its minimum to the low end; a vertical one to the high end
// (device y grows downwards); `inverted` swaps them.
[[nodiscard]] Scale axis_scale(const Axis& axis, double device_low, double device_high) noexcept;

[[nodiscard]] constexpr bool locks_minimum(AxisLock lock) noexcept {
    return lock == AxisLock::minimum || lock == AxisLock::both;
}
[[nodiscard]] constexpr bool locks_maximum(AxisLock lock) noexcept {
    return lock == AxisLock::maximum || lock == AxisLock::both;
}

// A range the axis may take: valid, and positive for a log10 axis.
[[nodiscard]] bool admissible_range(const Axis& axis, PlotRange range) noexcept;

// The smallest extent a zoom may reach, relative to the range's magnitude,
// so that zooming in never collapses a range below what doubles resolve.
inline constexpr double minimum_relative_extent = 1e-12;

// A group of axes sharing one range. Each linked axis reads the link's
// range when its plot lays out, and a change on any of them is written to
// the link. One thread: the plots of a link live on one UI thread (linked
// live plots exchange ranges as messages, plot_view.hpp). Shared by the
// plots that join it.
class AxisLink final {
public:
    explicit AxisLink(PlotRange range) noexcept : range_(range) {}

    [[nodiscard]] PlotRange range() const noexcept { return range_; }
    // Bumped by every change, so each plot sees whether it is current.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // Sets the shared range; an invalid range or an equal one changes
    // nothing. Returns whether it changed.
    bool set_range(PlotRange range) noexcept;

private:
    PlotRange range_{};
    std::uint64_t revision_{};
};

} // namespace gui_forms::plots
