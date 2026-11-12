#pragma once

// TickGenerator: where an axis's ticks fall and what their labels say
// (IMGUI_IMPLOT_STUDY B.3, P3). Instruments use the same generator on a
// gauge's arc, so a gauge and a plot axis tick identically.
//
// - linear (and symlog's linear core): nice steps 1, 2 and 5 times a power
//   of ten, chosen so that majors are at least the requested spacing
//   apart; minors divide a step by 5 (by 4 for a step of 2);
// - log10: decades (every k-th when decades are crowded), minors at 2 to 9
//   times each decade when a decade is wide enough;
// - symlog: 0 and ±10^k from the linear width outwards;
// - time: calendar steps from one second to centuries, in UTC (owner
//   decision 6: the host zone is a capability, not a default), labelled by
//   the step's unit; spans under two seconds tick as linear seconds;
// - category: each category position, labelled by its name.
//
// Labels go through the shared number formatter (number_format.hpp, the
// invariant culture) and are thinned against measured label widths so that
// none overlap: only every m-th major is labelled, m from 1, 2, 5, 10, ...
//
// Values are computed as integer multiples of the step, never accumulated,
// so a tick at zero is exactly zero. Everything is written into a TickSet
// of fixed capacity; generate() never allocates.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/scale/scale.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms::plots {

inline constexpr std::size_t maximum_major_ticks = 64U;
inline constexpr std::size_t maximum_minor_ticks = 256U;
// The longest label a tick holds, in bytes of UTF-8.
inline constexpr std::size_t maximum_tick_label = 31U;

struct GUI_FORMS_API TickLabel final {
    std::array<char, maximum_tick_label + 1U> bytes{};
    std::uint8_t length{};

    [[nodiscard]] std::string_view text() const noexcept {
        const std::string_view result(bytes.data(), length);
        return result;
    }
};

struct GUI_FORMS_API MajorTick final {
    double value{};
    // The device coordinate along the axis (ScaleMapping::forward).
    double device{};
    // Whether the label is shown (thinning).
    bool labelled{};
    TickLabel label{};
};

// Caller-owned output of TickGenerator::generate: capacity fixed, live
// counts stated. A generation replaces it whole.
struct GUI_FORMS_API TickSet final {
    std::array<MajorTick, maximum_major_ticks> major{};
    std::size_t major_count{};
    // Minor ticks' device coordinates.
    std::array<double, maximum_minor_ticks> minor{};
    std::size_t minor_count{};
    // The major step in the scale's transformed space (decades for log10,
    // seconds for time), and every how many majors one is labelled.
    double step{};
    std::size_t label_every{1U};

    [[nodiscard]] std::span<const MajorTick> majors() const noexcept {
        const std::span<const MajorTick> result(major.data(), major_count);
        return result;
    }
    [[nodiscard]] std::span<const double> minors() const noexcept {
        const std::span<const double> result(minor.data(), minor_count);
        return result;
    }
};

// Measures labels for thinning: their extent along the axis in device
// units (a horizontal axis's widths, a vertical axis's line heights).
// Runtime variation is real (each host and font measures differently), so
// this is an interface; the control passes its painter's text metrics.
class GUI_FORMS_API TickLabelMetrics {
public:
    virtual ~TickLabelMetrics() = default;
    [[nodiscard]] virtual double label_extent(std::string_view label) const = 0;
};

// Estimates a label's extent as `advance` per byte: deterministic, for
// headless tests and for vertical axes (pass the line height and 0 per
// byte through a fixed extent instead).
class GUI_FORMS_API EstimatedLabelMetrics final : public TickLabelMetrics {
public:
    EstimatedLabelMetrics(double advance, double fixed) noexcept : advance_(advance), fixed_(fixed) {}
    [[nodiscard]] double label_extent(std::string_view label) const override;

private:
    double advance_{};
    double fixed_{};
};

struct GUI_FORMS_API TickRequest final {
    // The fewest device units between two major ticks.
    double minimum_spacing{64.0};
    // The fewest device units between two labels' facing edges.
    double label_gap{8.0};
    // The category names of a category scale, by position; borrowed for
    // the call.
    std::span<const std::string_view> categories{};
};

enum class TickOutcome : std::uint8_t {
    generated,
    // More majors than maximum_major_ticks would be needed even at the
    // coarsest admitted step: the set holds the first ones.
    truncated,
    // The device extent is under one unit: no ticks.
    too_small,
};

// Fills `out` for `mapping` (its domain is the visible range, its device
// ends the axis's) with `metrics` measuring labels (null: no thinning by
// measurement, every major labelled).
GUI_FORMS_API TickOutcome generate_ticks(const ScaleMapping& mapping, const TickRequest& request,
                           const TickLabelMetrics* metrics, TickSet& out);

// The nice linear step for `raw` (> 0, finite): the smallest of 1, 2, 5 and
// 10 times a power of ten that is at least `raw`. Exact: the power of ten
// is the correctly rounded double (std::from_chars of "1e<k>").
[[nodiscard]] GUI_FORMS_API double nice_step(double raw) noexcept;

// A calendar date and time in UTC from seconds since the Unix epoch (the
// proleptic Gregorian calendar; Howard Hinnant's days-from-civil in
// integers), and back.
struct GUI_FORMS_API CivilTime final {
    std::int64_t year{1970};
    std::int32_t month{1};
    std::int32_t day{1};
    std::int32_t hour{};
    std::int32_t minute{};
    std::int32_t second{};

    friend bool operator==(const CivilTime& left, const CivilTime& right) noexcept {
        return left.year == right.year && left.month == right.month && left.day == right.day &&
               left.hour == right.hour && left.minute == right.minute && left.second == right.second;
    }
};

[[nodiscard]] GUI_FORMS_API CivilTime civil_from_seconds(std::int64_t seconds) noexcept;
[[nodiscard]] GUI_FORMS_API std::int64_t seconds_from_civil(const CivilTime& time) noexcept;

} // namespace gui_forms::plots
