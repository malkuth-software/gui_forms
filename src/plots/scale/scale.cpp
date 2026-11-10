#include "gui_forms/plots/scale/scale.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace gui_forms::plots {

namespace {

constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

// ln(10), correctly rounded.
constexpr double natural_log_ten = 2.302585092994045684;

// The powers of ten a double holds exactly (10^22 is the largest).
constexpr std::array<double, 23> exact_powers_of_ten{
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

double symlog_transform(double value, double width) noexcept {
    const double magnitude = std::fabs(value);
    const double logarithm = engine_log10(1.0 + magnitude / width);
    const double result = value < 0.0 ? -logarithm : logarithm;
    return result;
}

double symlog_untransform(double transformed, double width) noexcept {
    const double magnitude = width * (engine_exp10(std::fabs(transformed)) - 1.0);
    const double result = transformed < 0.0 ? -magnitude : magnitude;
    return result;
}

} // namespace

double engine_log10(double value) noexcept {
    if (!(value > 0.0)) return not_a_number;
    const double result = engine_logarithm(value) / natural_log_ten;
    // An exact power of ten gives its exact exponent, so decade ticks land
    // on integers: the nearest integer is taken when 10 to it is exactly
    // the value (10^-k as the double nearest it, 1 / 10^k).
    const double rounded = std::nearbyint(result);
    if (std::fabs(result - rounded) < 1e-9 && std::fabs(rounded) <= 22.0 && engine_exp10(rounded) == value) {
        return rounded;
    }
    return result;
}

double engine_exp10(double exponent) noexcept {
    if (std::isnan(exponent)) return not_a_number;
    const double rounded = std::nearbyint(exponent);
    if (rounded == exponent && std::fabs(exponent) <= 22.0) {
        const std::size_t index = static_cast<std::size_t>(std::fabs(exponent));
        const double power = exact_powers_of_ten[index];
        const double result = exponent < 0.0 ? 1.0 / power : power;
        return result;
    }
    const double result = engine_exponential(exponent * natural_log_ten);
    return result;
}

ScaleCheck check_scale(const Scale& scale) noexcept {
    if (!scale.domain.valid()) return ScaleCheck::domain_invalid;
    if (scale.kind == ScaleKind::log10 && !(scale.domain.minimum > 0.0)) return ScaleCheck::domain_not_positive;
    if (!std::isfinite(scale.device_start) || !std::isfinite(scale.device_end) ||
        scale.device_start == scale.device_end) {
        return ScaleCheck::device_invalid;
    }
    if (scale.kind == ScaleKind::symlog && !(std::isfinite(scale.symlog_width) && scale.symlog_width > 0.0)) {
        return ScaleCheck::symlog_width_invalid;
    }
    return ScaleCheck::valid;
}

ScaleMapping::ScaleMapping() noexcept = default;

ScaleMappingResult ScaleMapping::make(const Scale& scale) noexcept {
    ScaleMappingResult result{};
    result.check = check_scale(scale);
    if (result.check != ScaleCheck::valid) return result;
    ScaleMapping& mapping = result.mapping;
    mapping.scale_ = scale;
    const double low = mapping.transform(scale.domain.minimum);
    const double high = mapping.transform(scale.domain.maximum);
    mapping.transformed_minimum_ = low;
    mapping.device_per_unit_ = (scale.device_end - scale.device_start) / (high - low);
    return result;
}

double ScaleMapping::transform(double value) const noexcept {
    switch (scale_.kind) {
    case ScaleKind::log10:
        return engine_log10(value);
    case ScaleKind::symlog:
        return symlog_transform(value, scale_.symlog_width);
    case ScaleKind::linear:
    case ScaleKind::time:
    case ScaleKind::category:
        return value;
    }
    return value;
}

double ScaleMapping::untransform(double transformed) const noexcept {
    switch (scale_.kind) {
    case ScaleKind::log10:
        return engine_exp10(transformed);
    case ScaleKind::symlog:
        return symlog_untransform(transformed, scale_.symlog_width);
    case ScaleKind::linear:
    case ScaleKind::time:
    case ScaleKind::category:
        return transformed;
    }
    return transformed;
}

double ScaleMapping::forward(double value) const noexcept {
    const double transformed = transform(value);
    const double result = scale_.device_start + (transformed - transformed_minimum_) * device_per_unit_;
    return result;
}

double ScaleMapping::inverse(double device) const noexcept {
    const double transformed = transformed_minimum_ + (device - scale_.device_start) / device_per_unit_;
    const double result = untransform(transformed);
    return result;
}

std::size_t ScaleMapping::forward_span(std::span<const double> values, std::span<double> out) const noexcept {
    if (out.size() < values.size()) return 0U;
    std::size_t culled = 0U;
    // The kind is chosen once, before the loop: linear kinds need no
    // transform call.
    const bool linear = scale_.kind == ScaleKind::linear || scale_.kind == ScaleKind::time ||
                        scale_.kind == ScaleKind::category;
    if (linear) {
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const double device = scale_.device_start + (values[index] - transformed_minimum_) * device_per_unit_;
            out[index] = device;
            if (std::isnan(device)) culled = culled + 1U;
        }
        return culled;
    }
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const double device = forward(values[index]);
        out[index] = device;
        if (std::isnan(device)) culled = culled + 1U;
    }
    return culled;
}

} // namespace gui_forms::plots
