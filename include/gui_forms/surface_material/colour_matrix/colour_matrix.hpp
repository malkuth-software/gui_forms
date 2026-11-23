#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <stxlib/optional.hpp>

namespace gui_forms {

namespace detail {
// The identity colour matrix's entries, row-major: MaterialColourMatrix's
// default. Held here so that its declaration reads on one line.
inline constexpr std::array<double, 20> identity_colour_entries{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
                                                               0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
// The identity colour table's entries: each channel's 256 levels map to
// themselves. MaterialColourTable's default.
[[nodiscard]] constexpr std::array<std::uint8_t, 1024> identity_colour_table_entries() noexcept {
    std::array<std::uint8_t, 1024> entries{};
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        entries[index] = static_cast<std::uint8_t>(index % 256U);
    }
    return entries;
}
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
    // The theme language's colour stages (owner 2026-11-16; not CSS):
    // - fill: every colour channel `colour`'s, and alpha the pixel's times
    //   the colour's (a covered pixel recoloured, its coverage kept);
    // - duotone (tint): with L = 0.2126 R + 0.7152 G + 0.0722 B, each
    //   colour channel dark + L (light - dark), a straight line on the
    //   sRGB-encoded values; alpha kept, the colours' own alpha not read.
    //   The theme language's duotone() no longer lowers to this matrix but
    //   to luminance() and MaterialColourTable::duotone, which mixes in
    //   linear light as every gradient does (owner, 2026-11-16);
    // - luminance: each colour channel L (the first stage of duotone() and
    //   tritone(), whose second is MaterialColourTable::duotone or
    //   ::tritone); alpha kept.
    [[nodiscard]] static MaterialColourMatrix fill(Color colour) noexcept;
    [[nodiscard]] static MaterialColourMatrix duotone(Color dark, Color light) noexcept;
    [[nodiscard]] static MaterialColourMatrix luminance() noexcept;
    // Whether each row reads only its own channel and the constant, as a
    // colour table can (MaterialColourTable::then_matrix).
    [[nodiscard]] bool per_channel() const noexcept;

    // This filter, then `after`: one matrix applying both.
    [[nodiscard]] MaterialColourMatrix then(const MaterialColourMatrix& after) const noexcept;

    friend bool operator==(const MaterialColourMatrix& left, const MaterialColourMatrix& right) noexcept {
        return left.values == right.values;
    }
};

// Every entry finite and at most 256 in magnitude.
[[nodiscard]] GUI_FORMS_API bool valid_colour_matrix(const MaterialColourMatrix& matrix) noexcept;

// One channel's transfer (SVG feComponentTransfer's feFuncR, feFuncG,
// feFuncB and feFuncA), on a straight channel C in [0, 1], the result
// clamped to [0, 1]:
// - identity: C;
// - table, values v0 .. vn (n >= 1): with k = floor(C n) (n - 1 at C = 1),
//   vk + (C n - k) (vk+1 - vk);
// - discrete, values v0 .. vn-1 (n >= 1): vk with k = floor(C n), at most
//   n - 1;
// - linear: slope C + intercept;
// - gamma: amplitude C^exponent + offset, the power STXlib's
//   (stxlib::math::pow), so the same bits on every host.
// Valid when every number is finite and at most 256 in size, a table has
// 2 to maximum_values values and discrete 1 to maximum_values, and the
// exponent lies in (0, 64].
enum class MaterialTransferKind : std::uint8_t {
    identity,
    table,
    discrete,
    linear,
    gamma,
};

struct GUI_FORMS_API MaterialTransferFunction final {
    static constexpr std::size_t maximum_values = 32U;
    MaterialTransferKind kind{MaterialTransferKind::identity};
    std::vector<double> values{};
    double slope{1.0};
    double intercept{0.0};
    double amplitude{1.0};
    double exponent{1.0};
    double offset{0.0};
    friend bool operator==(const MaterialTransferFunction& left, const MaterialTransferFunction& right) noexcept {
        return left.kind == right.kind && left.values == right.values && left.slope == right.slope &&
               left.intercept == right.intercept && left.amplitude == right.amplitude &&
               left.exponent == right.exponent && left.offset == right.offset;
    }
};

[[nodiscard]] GUI_FORMS_API bool valid_transfer_function(const MaterialTransferFunction& function) noexcept;

// A per-channel colour table (owner 2026-11-16: feComponentTransfer, the
// second colour stage after MaterialColourMatrix): each straight channel,
// quantized once to 8 bits (floor(C × 255 + 0.5)), is looked up in its
// channel's 256 entries, an entry e giving e / 255. Red's levels first,
// then green's, blue's and alpha's. The identity by default. The table is
// made once, from transfer functions evaluated at each level; drawing it is
// a lookup, the same on every host.
struct GUI_FORMS_API MaterialColourTable final {
    static constexpr std::size_t channels = 4U;
    static constexpr std::size_t levels = 256U;
    std::array<std::uint8_t, channels * levels> entries{detail::identity_colour_table_entries()};

    // The four channels' functions, each evaluated at level / 255 and
    // stored floor(clamp(f, 0, 1) × 255 + 0.5). An invalid function
    // throws std::invalid_argument.
    [[nodiscard]] static MaterialColourTable transfer(const MaterialTransferFunction& red,
                                                      const MaterialTransferFunction& green,
                                                      const MaterialTransferFunction& blue,
                                                      const MaterialTransferFunction& alpha);
    // Tritone's table (after MaterialColourMatrix::luminance()): levels 0 to
    // 127 from the shadow to the midtone, 128 to 255 from the midtone to the
    // highlight, interpolated in linear light as every gradient is
    // (gui_forms/types/linear_light/linear_light.hpp); alpha kept.
    [[nodiscard]] static MaterialColourTable tritone(Color shadow, Color midtone, Color highlight);
    // Duotone's table (after MaterialColourMatrix::luminance()): levels 0 to
    // 255 from `dark` to `light`, interpolated in linear light as every
    // gradient is; alpha kept, the colours' own alpha not read.
    [[nodiscard]] static MaterialColourTable duotone(Color dark, Color light) noexcept;
    // This table, then `after`: one table applying both.
    [[nodiscard]] MaterialColourTable then(const MaterialColourTable& after) const noexcept;
    // This table, then `matrix`, which must be per_channel(): each entry e
    // becomes floor(clamp(m e / 255 + c, 0, 1) × 255 + 0.5) with its row's
    // own coefficient m and constant c.
    [[nodiscard]] MaterialColourTable then_matrix(const MaterialColourMatrix& matrix) const noexcept;

    friend bool operator==(const MaterialColourTable& left, const MaterialColourTable& right) noexcept {
        return left.entries == right.entries;
    }
};

// A filter's stages: the matrix, then, if present, the table.
struct GUI_FORMS_API MaterialColourStages final {
    MaterialColourMatrix matrix{};
    stxlib::Optional<MaterialColourTable> table{};
    friend bool operator==(const MaterialColourStages& left, const MaterialColourStages& right) noexcept {
        return left.matrix == right.matrix && left.table == right.table;
    }
};

// A material's colour filter: none, or its stages held out of line: one
// matrix, then optionally one per-channel table (MaterialColourTable).
// Every recipe of every theme holds a SurfaceMaterial, so stages held
// inline would grow a theme definition by tens of kilobytes; this holder
// is one pointer. It owns its stages; a copy copies them, a move leaves the
// source empty, and two filters are equal when both are empty or their
// stages are equal.
class GUI_FORMS_API MaterialColourFilter final {
public:
    MaterialColourFilter() noexcept;
    // Implicit, as std::optional's is: `material.colour_filter = matrix;`.
    // Throws std::bad_alloc when the matrix cannot be held.
    MaterialColourFilter(const MaterialColourMatrix& matrix);
    // The matrix, then the table. Throws std::bad_alloc when they cannot
    // be held.
    MaterialColourFilter(const MaterialColourMatrix& matrix, const MaterialColourTable& table);
    MaterialColourFilter(const MaterialColourFilter& other);
    MaterialColourFilter(MaterialColourFilter&& other) noexcept;
    MaterialColourFilter& operator=(const MaterialColourFilter& other);
    MaterialColourFilter& operator=(MaterialColourFilter&& other) noexcept;
    ~MaterialColourFilter();

    [[nodiscard]] bool has_value() const noexcept;
    explicit operator bool() const noexcept;
    // The matrix; only when has_value().
    [[nodiscard]] const MaterialColourMatrix& operator*() const noexcept;
    // The table after the matrix, or null for none (or no filter).
    [[nodiscard]] const MaterialColourTable* table() const noexcept;
    // Leaves the filter empty.
    void reset() noexcept;

    friend bool operator==(const MaterialColourFilter& left, const MaterialColourFilter& right) noexcept {
        if (!left.stages_ || !right.stages_) return !left.stages_ && !right.stages_;
        return *left.stages_ == *right.stages_;
    }

private:
    // The owned stages, or null. A raw owning address rather than
    // std::unique_ptr: libstdc++'s unique_ptr is not standard layout, and a
    // material is the value of standard-layout property capabilities
    // (MaterialPanel's material cell). This class's special members are its
    // only owners: they allocate, copy, transfer and delete it.
    MaterialColourStages* stages_;
};

} // namespace gui_forms
