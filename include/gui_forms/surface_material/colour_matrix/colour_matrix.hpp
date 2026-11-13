#pragma once

#include "gui_forms/export/application_export.hpp"
#include <array>
#include <cstddef>
#include <memory>

namespace gui_forms {

namespace detail {
// The identity colour matrix's entries, row-major: MaterialColourMatrix's
// default. Held here so that its declaration reads on one line.
inline constexpr std::array<double, 20> identity_colour_entries{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
                                                               0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
} // namespace detail

// A colour filter (E9.6; CSS `filter` and `backdrop-filter` colour
// functions): a 4 × 5 matrix on straight (not premultiplied) colour with
// each channel in [0, 1]. Its rows give red, green, blue and alpha; its
// columns read red, green, blue and alpha, and the fifth is a constant.
// Each result is clamped to [0, 1].
//
// The factories are the CSS functions, with CSS's matrices (Filter Effects
// Level 1). A list of functions composes into one matrix with `then`, in
// source order. CSS clamps between the functions of a list; one matrix
// does not, which differs only where an intermediate colour leaves [0, 1].
struct GUI_FORMS_API MaterialColourMatrix final {
    static constexpr std::size_t rows = 4U;
    static constexpr std::size_t columns = 5U;
    // Row-major: values[row × 5 + column]. The identity by default.
    std::array<double, rows * columns> values{detail::identity_colour_entries};

    // Each amount is clamped as CSS clamps it: brightness, contrast and
    // saturate to at least 0; grayscale, sepia, invert and opacity to
    // [0, 1]. A non-finite amount throws std::invalid_argument.
    [[nodiscard]] static MaterialColourMatrix brightness(double amount);
    [[nodiscard]] static MaterialColourMatrix contrast(double amount);
    [[nodiscard]] static MaterialColourMatrix saturate(double amount);
    [[nodiscard]] static MaterialColourMatrix grayscale(double amount);
    [[nodiscard]] static MaterialColourMatrix sepia(double amount);
    [[nodiscard]] static MaterialColourMatrix invert(double amount);
    [[nodiscard]] static MaterialColourMatrix opacity(double amount);
    // Turns hues by `degrees` (any finite angle), with the engine's sine and
    // cosine so that the matrix has the same bits on every host.
    [[nodiscard]] static MaterialColourMatrix hue_rotate(double degrees);

    // This filter, then `after`: one matrix applying both.
    [[nodiscard]] MaterialColourMatrix then(const MaterialColourMatrix& after) const noexcept;

    friend bool operator==(const MaterialColourMatrix& left, const MaterialColourMatrix& right) noexcept {
        return left.values == right.values;
    }
};

// Every entry finite and at most 256 in magnitude.
[[nodiscard]] GUI_FORMS_API bool valid_colour_matrix(const MaterialColourMatrix& matrix) noexcept;

// A material's colour filter: none, or one matrix held out of line. Every
// recipe of every theme holds a SurfaceMaterial, so a matrix held inline
// (160 bytes) would grow a theme definition by tens of kilobytes; this
// holder is one pointer. It owns its matrix; a copy copies the matrix, a
// move leaves the source empty, and two filters are equal when both are
// empty or their matrices are equal.
class GUI_FORMS_API MaterialColourFilter final {
public:
    MaterialColourFilter() noexcept;
    // Implicit, as std::optional's is: `material.colour_filter = matrix;`.
    // Throws std::bad_alloc when the matrix cannot be held.
    MaterialColourFilter(const MaterialColourMatrix& matrix);
    MaterialColourFilter(const MaterialColourFilter& other);
    MaterialColourFilter(MaterialColourFilter&& other) noexcept;
    MaterialColourFilter& operator=(const MaterialColourFilter& other);
    MaterialColourFilter& operator=(MaterialColourFilter&& other) noexcept;
    ~MaterialColourFilter();

    [[nodiscard]] bool has_value() const noexcept;
    explicit operator bool() const noexcept;
    // The matrix; only when has_value().
    [[nodiscard]] const MaterialColourMatrix& operator*() const noexcept;
    // Leaves the filter empty.
    void reset() noexcept;

    friend bool operator==(const MaterialColourFilter& left, const MaterialColourFilter& right) noexcept {
        if (!left.matrix_ || !right.matrix_) return !left.matrix_ && !right.matrix_;
        return *left.matrix_ == *right.matrix_;
    }

private:
    // The owned matrix, or null. A raw owning address rather than
    // std::unique_ptr: libstdc++'s unique_ptr is not standard layout, and a
    // material is the value of standard-layout property capabilities
    // (MaterialPanel's material cell). This class's special members are its
    // only owners: they allocate, copy, transfer and delete it.
    MaterialColourMatrix* matrix_;
};

} // namespace gui_forms
