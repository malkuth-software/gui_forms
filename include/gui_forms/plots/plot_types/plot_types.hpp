#pragma once

// The plots package's shared value types (IMGUI_IMPLOT_STUDY part B; owner
// ruling of 2026-11-08: plots are a package beside the instruments, with
// the primitives public). Data is double throughout the primitives; device
// coordinates become Point (logical units, the painter's coordinates) only
// at the painter boundary.

#include "gui_forms/export/application_export.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gui_forms::plots {

// A closed interval of data values on one axis. `minimum` may exceed
// `maximum` only in an inverted range the caller builds on purpose; the
// primitives keep minimum < maximum and carry inversion separately.
struct GUI_FORMS_API PlotRange final {
    double minimum{};
    double maximum{1.0};

    [[nodiscard]] double extent() const noexcept {
        const double result = maximum - minimum;
        return result;
    }
    // Finite ends with minimum strictly below maximum.
    [[nodiscard]] bool valid() const noexcept {
        const bool result = std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum;
        return result;
    }
    [[nodiscard]] bool contains(double value) const noexcept {
        const bool result = value >= minimum && value <= maximum;
        return result;
    }

    friend bool operator==(const PlotRange& left, const PlotRange& right) noexcept {
        return left.minimum == right.minimum && left.maximum == right.maximum;
    }
};

// An element's position in a series, counted from the oldest live element
// (a ring's oldest survivor is 0). Kept apart from device columns and from
// absolute sequence numbers, which it is easily confused with.
struct GUI_FORMS_API ElementIndex final {
    std::size_t value{};

    friend constexpr bool operator==(ElementIndex left, ElementIndex right) noexcept {
        return left.value == right.value;
    }
    friend constexpr bool operator<(ElementIndex left, ElementIndex right) noexcept {
        return left.value < right.value;
    }
};

// An element's absolute sequence number: how many elements were ever
// written before it. A growable column's sequence equals its index; a
// ring's index is its sequence minus the elements it has dropped.
struct GUI_FORMS_API ElementSequence final {
    std::uint64_t value{};

    friend constexpr bool operator==(ElementSequence left, ElementSequence right) noexcept {
        return left.value == right.value;
    }
    friend constexpr bool operator<(ElementSequence left, ElementSequence right) noexcept {
        return left.value < right.value;
    }
};

// A series' identity inside one plot: its position in the plot's series
// list when the plot was last laid out, plus the generation that list had,
// so a pick result kept across a change of series is recognised as stale.
struct GUI_FORMS_API SeriesIdentity final {
    std::uint32_t position{};
    std::uint32_t generation{};

    friend constexpr bool operator==(SeriesIdentity left, SeriesIdentity right) noexcept {
        return left.position == right.position && left.generation == right.generation;
    }
};

// Whether a series' x values never decrease (NaN aside). Declared by the
// author and verified on every edit; it chooses the decimation and pick
// algorithms (binary search and the envelope need monotone x).
enum class XOrder : std::uint8_t { monotone, unordered };

// A device pixel column of a plot, counted from the plot's left device
// edge.
struct GUI_FORMS_API DeviceColumn final {
    std::int64_t value{};

    friend constexpr bool operator==(DeviceColumn left, DeviceColumn right) noexcept {
        return left.value == right.value;
    }
};

} // namespace gui_forms::plots
