#include "gui_forms/surface_material/colour_matrix/colour_matrix.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/types/linear_light/linear_light.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

// The same matrices on every host: no fused multiply-add.
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms {
namespace {

double finite_amount(double amount) {
    if (!std::isfinite(amount)) throw std::invalid_argument("a colour filter's amount must be finite");
    return amount;
}

// A matrix whose colour rows are `colour` (3 × 3, row-major) and whose
// alpha row is the identity, with no constants.
MaterialColourMatrix colour_only(const std::array<double, 9>& colour) noexcept {
    MaterialColourMatrix matrix{};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            matrix.values[row * MaterialColourMatrix::columns + column] = colour[row * 3U + column];
        }
    }
    return matrix;
}

// Each colour channel c becomes slope × c + intercept (CSS's linear
// component transfer); alpha is kept.
MaterialColourMatrix linear_transfer(double slope, double intercept) noexcept {
    MaterialColourMatrix matrix{};
    for (std::size_t row = 0U; row < 3U; ++row) {
        matrix.values[row * MaterialColourMatrix::columns + row] = slope;
        matrix.values[row * MaterialColourMatrix::columns + 4U] = intercept;
    }
    return matrix;
}

bool bounded_entry(double value) noexcept {
    const bool bounded = std::isfinite(value) && std::abs(value) <= 256.0;
    return bounded;
}

} // namespace

MaterialColourMatrix MaterialColourMatrix::brightness(double amount) {
    const double slope = std::max(0.0, finite_amount(amount));
    const MaterialColourMatrix matrix = linear_transfer(slope, 0.0);
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::contrast(double amount) {
    const double slope = std::max(0.0, finite_amount(amount));
    const MaterialColourMatrix matrix = linear_transfer(slope, 0.5 - slope * 0.5);
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::saturate(double amount) {
    const double s = std::max(0.0, finite_amount(amount));
    const std::array<double, 9> colour{0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s,
                                       0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s,
                                       0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s};
    const MaterialColourMatrix matrix = colour_only(colour);
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::grayscale(double amount) {
    const double kept = 1.0 - std::clamp(finite_amount(amount), 0.0, 1.0);
    const std::array<double, 9> colour{0.2126 + 0.7874 * kept, 0.7152 - 0.7152 * kept, 0.0722 - 0.0722 * kept,
                                       0.2126 - 0.2126 * kept, 0.7152 + 0.2848 * kept, 0.0722 - 0.0722 * kept,
                                       0.2126 - 0.2126 * kept, 0.7152 - 0.7152 * kept, 0.0722 + 0.9278 * kept};
    const MaterialColourMatrix matrix = colour_only(colour);
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::sepia(double amount) {
    const double kept = 1.0 - std::clamp(finite_amount(amount), 0.0, 1.0);
    const std::array<double, 9> colour{0.393 + 0.607 * kept, 0.769 - 0.769 * kept, 0.189 - 0.189 * kept,
                                       0.349 - 0.349 * kept, 0.686 + 0.314 * kept, 0.168 - 0.168 * kept,
                                       0.272 - 0.272 * kept, 0.534 - 0.534 * kept, 0.131 + 0.869 * kept};
    const MaterialColourMatrix matrix = colour_only(colour);
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::invert(double amount) {
    const double a = std::clamp(finite_amount(amount), 0.0, 1.0);
    const MaterialColourMatrix matrix = linear_transfer(1.0 - a * 2.0, a);
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::opacity(double amount) {
    MaterialColourMatrix matrix{};
    matrix.values[3U * columns + 3U] = std::clamp(finite_amount(amount), 0.0, 1.0);
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::hue_rotate(double degrees) {
    constexpr double degrees_to_radians = 3.14159265358979323846264338327950288 / 180.0;
    const double turns = finite_amount(degrees) / 360.0;
    // Reduced to one turn first, so that any finite angle is within the
    // engine sine's range.
    const double within = (turns - std::floor(turns)) * 360.0;
    const EngineSineCosine turned = engine_sine_cosine(within * degrees_to_radians);
    const double c = turned.cosine;
    const double s = turned.sine;
    const std::array<double, 9> colour{0.213 + c * 0.787 - s * 0.213, 0.715 - c * 0.715 - s * 0.715,
                                       0.072 - c * 0.072 + s * 0.928, 0.213 - c * 0.213 + s * 0.143,
                                       0.715 + c * 0.285 + s * 0.140, 0.072 - c * 0.072 - s * 0.283,
                                       0.213 - c * 0.213 - s * 0.787, 0.715 - c * 0.715 + s * 0.715,
                                       0.072 + c * 0.928 + s * 0.072};
    const MaterialColourMatrix matrix = colour_only(colour);
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::then(const MaterialColourMatrix& after) const noexcept {
    // after ∘ this: each row of `after` times this matrix, with this
    // matrix's constants carried through and `after`'s added.
    MaterialColourMatrix result{};
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < columns; ++column) {
            double sum = column == 4U ? after.values[row * columns + 4U] : 0.0;
            for (std::size_t inner = 0U; inner < rows; ++inner) {
                sum = sum + after.values[row * columns + inner] * values[inner * columns + column];
            }
            result.values[row * columns + column] = sum;
        }
    }
    return result;
}

bool valid_colour_matrix(const MaterialColourMatrix& matrix) noexcept {
    const bool bounded = std::all_of(matrix.values.begin(), matrix.values.end(), &bounded_entry);
    return bounded;
}

MaterialColourMatrix MaterialColourMatrix::fill(Color colour) noexcept {
    MaterialColourMatrix matrix{};
    matrix.values = std::array<double, rows * columns>{};
    matrix.values[4U] = static_cast<double>(colour.red) / 255.0;
    matrix.values[columns + 4U] = static_cast<double>(colour.green) / 255.0;
    matrix.values[2U * columns + 4U] = static_cast<double>(colour.blue) / 255.0;
    matrix.values[3U * columns + 3U] = static_cast<double>(colour.alpha) / 255.0;
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::duotone(Color dark, Color light) noexcept {
    const std::array<double, 3> coefficients{0.2126, 0.7152, 0.0722};
    const std::array<double, 3> low{static_cast<double>(dark.red) / 255.0, static_cast<double>(dark.green) / 255.0,
                                    static_cast<double>(dark.blue) / 255.0};
    const std::array<double, 3> high{static_cast<double>(light.red) / 255.0,
                                     static_cast<double>(light.green) / 255.0,
                                     static_cast<double>(light.blue) / 255.0};
    MaterialColourMatrix matrix{};
    for (std::size_t row = 0U; row < 3U; ++row) {
        const double span = high[row] - low[row];
        for (std::size_t column = 0U; column < 3U; ++column) {
            matrix.values[row * columns + column] = coefficients[column] * span;
        }
        matrix.values[row * columns + 4U] = low[row];
    }
    return matrix;
}

MaterialColourMatrix MaterialColourMatrix::luminance() noexcept {
    const std::array<double, 9> rows_of_luminance{0.2126, 0.7152, 0.0722, 0.2126, 0.7152,
                                                  0.0722, 0.2126, 0.7152, 0.0722};
    const MaterialColourMatrix matrix = colour_only(rows_of_luminance);
    return matrix;
}

bool MaterialColourMatrix::per_channel() const noexcept {
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < rows; ++column) {
            if (column != row && values[row * columns + column] != 0.0) return false;
        }
    }
    return true;
}

namespace {

bool bounded_number(double value) noexcept {
    const bool bounded = std::isfinite(value) && std::abs(value) <= 256.0;
    return bounded;
}

// One level of a table, stored as the engine stores a channel.
std::uint8_t table_level(double value) noexcept {
    const double clamped = std::clamp(value, 0.0, 1.0);
    const double scaled = std::floor(clamped * 255.0 + 0.5);
    const std::uint8_t level = static_cast<std::uint8_t>(scaled);
    return level;
}

// `function` (valid) at C in [0, 1].
double transferred(const MaterialTransferFunction& function, double c) noexcept {
    switch (function.kind) {
    case MaterialTransferKind::identity: return c;
    case MaterialTransferKind::table: {
        const std::size_t steps = function.values.size() - 1U;
        const double scaled = c * static_cast<double>(steps);
        const std::size_t low = std::min(static_cast<std::size_t>(std::floor(scaled)), steps - 1U);
        const double within = scaled - static_cast<double>(low);
        const double value = function.values[low] + within * (function.values[low + 1U] - function.values[low]);
        return value;
    }
    case MaterialTransferKind::discrete: {
        const std::size_t count = function.values.size();
        const double scaled = c * static_cast<double>(count);
        const std::size_t step = std::min(static_cast<std::size_t>(std::floor(scaled)), count - 1U);
        return function.values[step];
    }
    case MaterialTransferKind::linear: {
        const double value = function.slope * c + function.intercept;
        return value;
    }
    case MaterialTransferKind::gamma: {
        const double value = function.amplitude * engine_power(c, function.exponent) + function.offset;
        return value;
    }
    }
    // A kind outside the enumeration (refused by valid_transfer_function)
    // is the identity.
    return c;
}

} // namespace

bool valid_transfer_function(const MaterialTransferFunction& function) noexcept {
    const std::array<double, 5> numbers{function.slope, function.intercept, function.amplitude, function.exponent,
                                        function.offset};
    if (!std::all_of(numbers.begin(), numbers.end(), &bounded_number)) return false;
    if (!std::all_of(function.values.begin(), function.values.end(), &bounded_number)) return false;
    const std::size_t count = function.values.size();
    switch (function.kind) {
    case MaterialTransferKind::identity:
    case MaterialTransferKind::linear: return true;
    case MaterialTransferKind::table: return count >= 2U && count <= MaterialTransferFunction::maximum_values;
    case MaterialTransferKind::discrete: return count >= 1U && count <= MaterialTransferFunction::maximum_values;
    case MaterialTransferKind::gamma: return function.exponent > 0.0 && function.exponent <= 64.0;
    }
    return false;
}

MaterialColourTable MaterialColourTable::transfer(const MaterialTransferFunction& red,
                                                  const MaterialTransferFunction& green,
                                                  const MaterialTransferFunction& blue,
                                                  const MaterialTransferFunction& alpha) {
    const std::array<const MaterialTransferFunction*, channels> functions{&red, &green, &blue, &alpha};
    for (const MaterialTransferFunction* function : functions) {
        if (!valid_transfer_function(*function)) throw std::invalid_argument("an invalid colour transfer function");
    }
    MaterialColourTable table{};
    for (std::size_t channel = 0U; channel < channels; ++channel) {
        for (std::size_t level = 0U; level < levels; ++level) {
            const double c = static_cast<double>(level) / 255.0;
            table.entries[channel * levels + level] = table_level(transferred(*functions[channel], c));
        }
    }
    return table;
}

MaterialColourTable MaterialColourTable::tritone(Color shadow, Color midtone, Color highlight) {
    // Levels 0 to 127 run from the shadow to the midtone, 128 to 255 from
    // the midtone to the highlight (the three-value table's two segments,
    // split at 0.5), interpolated in linear light as every gradient is
    // (gui_forms/types/linear_light/linear_light.hpp). The colours' own
    // alpha takes no part: the table keeps alpha.
    const Color low{shadow.red, shadow.green, shadow.blue, 255U};
    const Color middle{midtone.red, midtone.green, midtone.blue, 255U};
    const Color high{highlight.red, highlight.green, highlight.blue, 255U};
    MaterialColourTable table{};
    for (std::size_t level = 0U; level < levels; ++level) {
        const bool lower = level < 128U;
        // The segment's position in 1/65536, rounded: 2 × level / 255 in
        // the lower segment, (2 × level − 255) / 255 in the upper.
        const std::uint64_t doubled = 2U * static_cast<std::uint64_t>(level);
        const std::uint64_t along = lower ? doubled : doubled - 255U;
        const std::uint32_t weight = static_cast<std::uint32_t>((along * linear_light_weight_one + 127U) / 255U);
        const Color mixed = lower ? interpolate_linear_light(low, middle, weight)
                                  : interpolate_linear_light(middle, high, weight);
        table.entries[level] = mixed.red;
        table.entries[levels + level] = mixed.green;
        table.entries[2U * levels + level] = mixed.blue;
    }
    return table;
}

MaterialColourTable MaterialColourTable::then(const MaterialColourTable& after) const noexcept {
    MaterialColourTable result{};
    for (std::size_t channel = 0U; channel < channels; ++channel) {
        const std::size_t first = channel * levels;
        for (std::size_t level = 0U; level < levels; ++level) {
            result.entries[first + level] = after.entries[first + entries[first + level]];
        }
    }
    return result;
}

MaterialColourTable MaterialColourTable::then_matrix(const MaterialColourMatrix& matrix) const noexcept {
    MaterialColourTable result{};
    for (std::size_t channel = 0U; channel < channels; ++channel) {
        const double coefficient = matrix.values[channel * MaterialColourMatrix::columns + channel];
        const double constant = matrix.values[channel * MaterialColourMatrix::columns + 4U];
        const std::size_t first = channel * levels;
        for (std::size_t level = 0U; level < levels; ++level) {
            const double value = coefficient * (static_cast<double>(entries[first + level]) / 255.0) + constant;
            result.entries[first + level] = table_level(value);
        }
    }
    return result;
}

MaterialColourFilter::MaterialColourFilter() noexcept : stages_{} {}

MaterialColourFilter::MaterialColourFilter(const MaterialColourMatrix& matrix)
    : stages_{new MaterialColourStages{matrix, stxlib::Optional<MaterialColourTable>::none()}} {}

MaterialColourFilter::MaterialColourFilter(const MaterialColourMatrix& matrix, const MaterialColourTable& table)
    : stages_{new MaterialColourStages{matrix, stxlib::Optional<MaterialColourTable>::of(table)}} {}

MaterialColourFilter::MaterialColourFilter(const MaterialColourFilter& other) : stages_{} {
    if (other.stages_) stages_ = new MaterialColourStages(*other.stages_);
}

MaterialColourFilter::MaterialColourFilter(MaterialColourFilter&& other) noexcept : stages_{other.stages_} {
    other.stages_ = nullptr;
}

MaterialColourFilter& MaterialColourFilter::operator=(const MaterialColourFilter& other) {
    if (this == &other) return *this;
    // The copy is made first: a failed allocation leaves this filter as it was.
    MaterialColourStages* copy = nullptr;
    if (other.stages_) copy = new MaterialColourStages(*other.stages_);
    delete stages_;
    stages_ = copy;
    return *this;
}

MaterialColourFilter& MaterialColourFilter::operator=(MaterialColourFilter&& other) noexcept {
    if (this == &other) return *this;
    delete stages_;
    stages_ = other.stages_;
    other.stages_ = nullptr;
    return *this;
}

MaterialColourFilter::~MaterialColourFilter() {
    delete stages_;
}

bool MaterialColourFilter::has_value() const noexcept {
    const bool held = stages_ != nullptr;
    return held;
}

MaterialColourFilter::operator bool() const noexcept {
    const bool held = stages_ != nullptr;
    return held;
}

const MaterialColourMatrix& MaterialColourFilter::operator*() const noexcept {
    return (*stages_).matrix;
}

const MaterialColourTable* MaterialColourFilter::table() const noexcept {
    if (stages_ == nullptr) return nullptr;
    const MaterialColourTable* const held = (*stages_).table.value();
    return held;
}

void MaterialColourFilter::reset() noexcept {
    delete stages_;
    stages_ = nullptr;
}

} // namespace gui_forms
