#include "gui_forms/surface_material/colour_matrix/colour_matrix.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

// The same matrices on every host: no fused multiply-add.
#if defined(__clang__)
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

MaterialColourFilter::MaterialColourFilter() noexcept : matrix_{} {}

MaterialColourFilter::MaterialColourFilter(const MaterialColourMatrix& matrix)
    : matrix_{new MaterialColourMatrix(matrix)} {}

MaterialColourFilter::MaterialColourFilter(const MaterialColourFilter& other) : matrix_{} {
    if (other.matrix_) matrix_ = new MaterialColourMatrix(*other.matrix_);
}

MaterialColourFilter::MaterialColourFilter(MaterialColourFilter&& other) noexcept : matrix_{other.matrix_} {
    other.matrix_ = nullptr;
}

MaterialColourFilter& MaterialColourFilter::operator=(const MaterialColourFilter& other) {
    if (this == &other) return *this;
    // The copy is made first: a failed allocation leaves this filter as it was.
    MaterialColourMatrix* copy = nullptr;
    if (other.matrix_) copy = new MaterialColourMatrix(*other.matrix_);
    delete matrix_;
    matrix_ = copy;
    return *this;
}

MaterialColourFilter& MaterialColourFilter::operator=(MaterialColourFilter&& other) noexcept {
    if (this == &other) return *this;
    delete matrix_;
    matrix_ = other.matrix_;
    other.matrix_ = nullptr;
    return *this;
}

MaterialColourFilter::~MaterialColourFilter() {
    delete matrix_;
}

bool MaterialColourFilter::has_value() const noexcept {
    const bool held = matrix_ != nullptr;
    return held;
}

MaterialColourFilter::operator bool() const noexcept {
    const bool held = matrix_ != nullptr;
    return held;
}

const MaterialColourMatrix& MaterialColourFilter::operator*() const noexcept {
    return *matrix_;
}

void MaterialColourFilter::reset() noexcept {
    delete matrix_;
    matrix_ = nullptr;
}

} // namespace gui_forms
