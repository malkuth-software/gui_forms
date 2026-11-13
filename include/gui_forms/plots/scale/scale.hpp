#pragma once

// Scale: data values to device coordinates along one axis
// (IMGUI_IMPLOT_STUDY B.3, P2). A Scale is a value: its kind, the data
// domain and the device range it maps onto. ScaleMapping is a validated,
// prepared Scale: the only way to map values, so an invalid domain is
// refused once, as a named result, never mapped to infinity.
//
// Kinds:
// - linear: v.
// - log10: log10(v); the domain must be positive. Values at or below zero
//   map to NaN (culled and counted by the span form).
// - symlog: sign(v) * log10(1 + |v| / w), linear within about ±w of zero.
// - time: seconds since 1970-01-01T00:00:00 UTC, mapped linearly (the
//   tick generator alone treats it as a calendar).
// - category: category positions 0, 1, 2, ..., mapped linearly.
//
// Accuracy: linear, time and category map through one subtraction, one
// multiplication and one addition, so a round trip forward then inverse is
// within a few ulp of the domain's extent. Log and symlog use engine_math
// (engine_logarithm, engine_exponential), never the host libm, so a mapping
// is the same bits on every host; their error is engine_math's bound.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/plot_types/plot_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::plots {

enum class ScaleKind : std::uint8_t { linear, log10, symlog, time, category };

struct GUI_FORMS_API Scale final {
    ScaleKind kind{ScaleKind::linear};
    PlotRange domain{};
    // The device coordinates the domain's minimum and maximum map to. An
    // inverted axis (or a y axis, whose device y grows downwards) passes
    // device_start > device_end.
    double device_start{};
    double device_end{1.0};
    // Symlog's linear width w (> 0, finite); ignored by the other kinds.
    double symlog_width{1.0};

    friend bool operator==(const Scale& left, const Scale& right) noexcept {
        return left.kind == right.kind && left.domain == right.domain && left.device_start == right.device_start &&
               left.device_end == right.device_end && left.symlog_width == right.symlog_width;
    }
};

// Why a scale cannot map (ScaleMapping::make).
enum class ScaleCheck : std::uint8_t {
    valid,
    // A domain end is not finite, or minimum is not below maximum.
    domain_invalid,
    // A log10 domain reaches zero or below.
    domain_not_positive,
    // A device end is not finite, or the ends are equal.
    device_invalid,
    // A symlog width that is not finite and positive.
    symlog_width_invalid,
};

class ScaleMapping;

struct ScaleMappingResult;

// A validated scale, prepared for mapping. Copyable value.
class GUI_FORMS_API ScaleMapping final {
public:
    // The identity-like mapping of the default Scale (linear, [0, 1] onto
    // [0, 1]).
    ScaleMapping() noexcept;

    // Validates and prepares `scale`. A refused scale's result holds the
    // default mapping.
    [[nodiscard]] static ScaleMappingResult make(const Scale& scale) noexcept;

    [[nodiscard]] const Scale& scale() const noexcept { return scale_; }
    // The device coordinate of `value`; NaN for NaN and for values a log10
    // scale cannot take (≤ 0). Values outside the domain extrapolate.
    [[nodiscard]] double forward(double value) const noexcept;
    // The data value at device coordinate `device`; NaN for NaN.
    [[nodiscard]] double inverse(double device) const noexcept;
    // forward() over `values` into `out` (the first values.size() entries;
    // `out` at least as large, or nothing is written and 0 returned).
    // Returns how many mapped to NaN (gaps and culled log values).
    std::size_t forward_span(std::span<const double> values, std::span<double> out) const noexcept;
    // The value in the scale's transformed space (log10's exponent,
    // symlog's), and back: the space ticks and zoom work in.
    [[nodiscard]] double transform(double value) const noexcept;
    [[nodiscard]] double untransform(double transformed) const noexcept;

private:
    Scale scale_{};
    // transform(domain.minimum), and device units per transformed unit.
    double transformed_minimum_{};
    double device_per_unit_{1.0};
};

struct GUI_FORMS_API ScaleMappingResult final {
    ScaleCheck check{ScaleCheck::valid};
    ScaleMapping mapping{};
};

[[nodiscard]] GUI_FORMS_API ScaleCheck check_scale(const Scale& scale) noexcept;

// log10 of `value` through engine_logarithm (NaN for ≤ 0 and NaN), and 10
// raised to `exponent` through engine_exponential. Exact at integer
// exponents from -22 to 22, where the result is a double exactly.
[[nodiscard]] GUI_FORMS_API double engine_log10(double value) noexcept;
[[nodiscard]] GUI_FORMS_API double engine_exp10(double exponent) noexcept;

} // namespace gui_forms::plots
