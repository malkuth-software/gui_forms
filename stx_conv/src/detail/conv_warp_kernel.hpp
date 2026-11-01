#pragma once
// Warp of a tensor-quintic RGBA atlas through an inverse homography, one
// target row at a time. Target pixel (column c, row r) takes the atlas sample
// at inverse(c, r): pixel for pixel the result of map_points followed by
// sample_rgba at (c, r), except that a zero result's sign may differ.
//
// It is a reformulation of that point path, not a vectorized copy:
// - Lanes are target pixels for the mapping, the clamp and both quintic
//   bases; for the 36-control contraction the RGBA channels are the lanes.
// - patch_rgba's grid-line and node reductions are gone. With finite
//   controls the full contraction reproduces them: on a line or node the
//   weights are exactly 0 and 1, so only the sign of a zero sum can differ.
// - The footprint test is a lane mask (keep_or_zero) for projective maps.
//   For affine maps it is an exact column interval found per row before the
//   loop (affine_footprint), and the loop has no test at all.
#include "conv_mapping_kernel.hpp"
#include "conv_tensor_kernel.hpp"
#include <algorithm>

namespace stx_conv::kernels::detail {

// One warp's fixed inputs, validated by stx_conv::kernels::warp_rgba.
template<class Scalar>
struct WarpGeometry {
    const Scalar* controls = nullptr;
    // Scalar elements between lattice rows, RGBA included.
    std::size_t lattice_row_stride = 0;
    // At least 2 each.
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    std::array<Scalar, 9> inverse{};
};

// Target columns [begin, end) of one row.
struct ColumnInterval {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// The pixels [column, column + width) of `row`; output is the row's first
// pixel. Masked writes +0 outside the half-pixel source footprint; unmasked
// assumes every pixel is inside it.
template<class Ops, class Scalar, bool Affine, bool Masked, bool Premultiplied>
STX_CONV_INLINE void warp_group(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t column,
                                Scalar* output) noexcept {
    using Vector = typename Ops::Value;
    using ChannelOps = NativeOps<Scalar>;
    using Channels = typename ChannelOps::Value;
    constexpr std::size_t width = Ops::width;
    constexpr std::size_t groups = rgba_groups<Scalar>;
    std::array<Scalar, width> target_x{};
    std::array<Scalar, width> target_y{};
    for (std::size_t lane = 0; lane < width; ++lane) {
        target_x[lane] = static_cast<Scalar>(column + lane);
        target_y[lane] = static_cast<Scalar>(row);
    }
    std::array<Scalar, width> mapped_x{};
    std::array<Scalar, width> mapped_y{};
    mapping_group<Ops, Scalar, Affine>(geometry.inverse, target_x.data(), target_y.data(), mapped_x.data(),
                                       mapped_y.data(), 0);
    // Clamped into the node domain [0, width - 1] x [0, height - 1].
    const Vector last_x = Ops::set(static_cast<Scalar>(geometry.source_width - 1));
    const Vector last_y = Ops::set(static_cast<Scalar>(geometry.source_height - 1));
    const Vector clamped_x = Ops::min(Ops::max(Ops::load(mapped_x.data()), Ops::set(0)), last_x);
    const Vector clamped_y = Ops::min(Ops::max(Ops::load(mapped_y.data()), Ops::set(0)), last_y);
    std::array<Scalar, width> node_x{};
    std::array<Scalar, width> node_y{};
    Ops::store(node_x.data(), clamped_x);
    Ops::store(node_y.data(), clamped_y);
    // Cell lookup per lane: the last cell also takes the last node.
    std::array<Scalar, width> u{};
    std::array<Scalar, width> v{};
    std::array<const Scalar*, width> origin{};
    for (std::size_t lane = 0; lane < width; ++lane) {
        const std::size_t cell_x = std::min(static_cast<std::size_t>(node_x[lane]), geometry.source_width - 2);
        const std::size_t cell_y = std::min(static_cast<std::size_t>(node_y[lane]), geometry.source_height - 2);
        u[lane] = node_x[lane] - static_cast<Scalar>(cell_x);
        v[lane] = node_y[lane] - static_cast<Scalar>(cell_y);
        origin[lane] = geometry.controls + cell_y * 5 * geometry.lattice_row_stride + cell_x * 5 * 4;
    }
    std::array<bool, width> inside{};
    if constexpr (Masked) {
        const Scalar half = Scalar(0.5);
        const Scalar right_edge = static_cast<Scalar>(geometry.source_width) - half;
        const Scalar bottom_edge = static_cast<Scalar>(geometry.source_height) - half;
        for (std::size_t lane = 0; lane < width; ++lane) {
            const bool within_x = mapped_x[lane] >= -half && mapped_x[lane] < right_edge;
            const bool within_y = mapped_y[lane] >= -half && mapped_y[lane] < bottom_edge;
            inside[lane] = within_x && within_y;
        }
    }
    // Both bases for all lanes at once, then laid out weight-major
    // ([weight][lane]) for the per-pixel contraction.
    std::array<Vector, 6> basis_x{};
    std::array<Vector, 6> basis_y{};
    quintic_basis<Ops>(Ops::load(u.data()), basis_x);
    quintic_basis<Ops>(Ops::load(v.data()), basis_y);
    std::array<Scalar, 6 * width> weights_x{};
    std::array<Scalar, 6 * width> weights_y{};
    for (std::size_t weight = 0; weight < 6; ++weight) {
        Ops::store(weights_x.data() + weight * width, basis_x[weight]);
        Ops::store(weights_y.data() + weight * width, basis_y[weight]);
    }
    for (std::size_t lane = 0; lane < width; ++lane) {
        std::array<Channels, groups> sums = weighted_patch_rgba(origin[lane], geometry.lattice_row_stride,
                                                                weights_x.data() + lane, weights_y.data() + lane, width);
        if constexpr (Premultiplied) {
            // Alpha into [0, 1], then every channel into [0, alpha]; the
            // alpha lane's own result is alpha.
            std::array<Scalar, 4> sample{};
            for (std::size_t group = 0; group < groups; ++group) {
                ChannelOps::store(sample.data() + group * ChannelOps::width, sums[group]);
            }
            const Channels alpha = ChannelOps::set(std::clamp(sample[3], Scalar(0), Scalar(1)));
            for (std::size_t group = 0; group < groups; ++group) {
                sums[group] = ChannelOps::min(alpha, ChannelOps::max(sums[group], ChannelOps::set(0)));
            }
        }
        Scalar* pixel = output + 4 * (column + lane);
        for (std::size_t group = 0; group < groups; ++group) {
            Channels value = sums[group];
            if constexpr (Masked) { value = ChannelOps::keep_or_zero(inside[lane], value); }
            ChannelOps::store(pixel + group * ChannelOps::width, value);
        }
    }
}

// How the samples of one affine target row meet the lattice, found once per
// row as at most row_segment_limit segments. Where one source coordinate's
// cell fraction is exactly 0 or 1 at every pixel of a segment, each sample
// lies on a lattice line and the patch reduces to the six-control curve
// along it, as patch_rgba reduces it, with the same operation order:
// - row_curve: v in {0, 1}; the curve runs across a lattice row, weighted
//   by the u basis.
// - column_curve: u in {0, 1}; the curve runs down a lattice column,
//   weighted by the v basis.
// The line may change from pixel to pixel; only the fraction is fixed. Such
// rows come from axis-aligned maps, including those that rounding makes
// axis-aligned (a rotation by a computed 90 degrees has a tiny nonzero
// cosine). Three probe pixels decide whether a row is scanned for them;
// any other row is one general segment.
enum class RowShape { general, row_curve, column_curve };

struct RowSegment {
    RowShape shape = RowShape::general;
    ColumnInterval columns{};
};

inline constexpr std::size_t row_segment_limit = 8;

// Segments [0, count) cover a row's footprint interval in column order.
struct RowSegments {
    std::array<RowSegment, row_segment_limit> segments{};
    std::size_t count = 0;
};

// One node-domain coordinate's cell and fraction, as warp_group computes
// them (clamp into [0, extent - 1], cell at most extent - 2).
template<class Scalar>
void node_cell(Scalar mapped, std::size_t extent, std::size_t& cell, Scalar& fraction) noexcept {
    using Ops = ScalarOps<Scalar>;
    const Scalar node = Ops::min(Ops::max(mapped, Scalar(0)), static_cast<Scalar>(extent - 1));
    cell = std::min(static_cast<std::size_t>(node), extent - 2);
    fraction = node - static_cast<Scalar>(cell);
}

// Whether source coordinate `axis` (0 for x, 1 for y) of one target pixel
// lies on a lattice line, with mapping_group's rounding.
template<class Scalar>
bool on_lattice_line(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t column,
                     std::size_t axis) noexcept {
    const Scalar target_x = static_cast<Scalar>(column);
    const Scalar target_y = static_cast<Scalar>(row);
    std::array<Scalar, 2> mapped{};
    mapping_group<ScalarOps<Scalar>, Scalar, true>(geometry.inverse, &target_x, &target_y, &mapped[0], &mapped[1], 0);
    const std::size_t extent = axis == 0 ? geometry.source_width : geometry.source_height;
    std::size_t cell = 0;
    Scalar fraction = Scalar(0);
    node_cell(mapped[axis], extent, cell, fraction);
    const bool result = fraction == Scalar(0) || fraction == Scalar(1);
    return result;
}

// Splits `inside` into alternating on-line and general runs of coordinate
// `axis`. False, with `result` unspecified, when the runs outnumber the
// limit.
template<class Scalar>
bool line_runs(const WarpGeometry<Scalar>& geometry, std::size_t row, ColumnInterval inside, std::size_t axis,
               RowSegments& result) noexcept {
    const RowShape curve = axis == 1 ? RowShape::row_curve : RowShape::column_curve;
    result.count = 0;
    std::size_t begin = inside.begin;
    bool on_line = on_lattice_line(geometry, row, begin, axis);
    for (std::size_t column = inside.begin + 1; column <= inside.end; ++column) {
        const bool next = column < inside.end && on_lattice_line(geometry, row, column, axis);
        if (column < inside.end && next == on_line) { continue; }
        if (result.count == row_segment_limit) { return false; }
        result.segments[result.count] = RowSegment{on_line ? curve : RowShape::general, ColumnInterval{begin, column}};
        ++result.count;
        begin = column;
        on_line = next;
    }
    return true;
}

// The segments of a nonempty footprint interval of an affine row.
template<class Scalar>
RowSegments row_segments(const WarpGeometry<Scalar>& geometry, std::size_t row, ColumnInterval inside) noexcept {
    RowSegments result{};
    const std::array<std::size_t, 3> probes{inside.begin, inside.begin + (inside.end - inside.begin) / 2,
                                            inside.end - 1};
    for (std::size_t axis = 2; axis-- > 0;) {
        std::size_t hits = 0;
        for (const std::size_t column : probes) {
            if (on_lattice_line(geometry, row, column, axis)) { ++hits; }
        }
        if (hits >= 2 && line_runs(geometry, row, inside, axis, result)) { return result; }
    }
    result.segments[0] = RowSegment{RowShape::general, inside};
    result.count = 1;
    return result;
}

// The pixels [column, column + width) of a curve segment, all inside the
// footprint, the fixed coordinate's fraction 0 or 1 at every pixel. The
// varying coordinate's basis weights the six controls along each pixel's
// lattice line.
template<class Ops, class Scalar, RowShape Shape, bool Premultiplied>
STX_CONV_INLINE void curve_group(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t column,
                                 Scalar* output) noexcept {
    using Vector = typename Ops::Value;
    using ChannelOps = NativeOps<Scalar>;
    using Channels = typename ChannelOps::Value;
    constexpr std::size_t width = Ops::width;
    constexpr std::size_t groups = rgba_groups<Scalar>;
    constexpr bool across = Shape == RowShape::row_curve;
    std::array<Scalar, width> target_x{};
    std::array<Scalar, width> target_y{};
    for (std::size_t lane = 0; lane < width; ++lane) {
        target_x[lane] = static_cast<Scalar>(column + lane);
        target_y[lane] = static_cast<Scalar>(row);
    }
    std::array<Scalar, width> mapped_x{};
    std::array<Scalar, width> mapped_y{};
    mapping_group<Ops, Scalar, true>(geometry.inverse, target_x.data(), target_y.data(), mapped_x.data(),
                                     mapped_y.data(), 0);
    const Vector last_x = Ops::set(static_cast<Scalar>(geometry.source_width - 1));
    const Vector last_y = Ops::set(static_cast<Scalar>(geometry.source_height - 1));
    std::array<Scalar, width> node_x{};
    std::array<Scalar, width> node_y{};
    Ops::store(node_x.data(), Ops::min(Ops::max(Ops::load(mapped_x.data()), Ops::set(0)), last_x));
    Ops::store(node_y.data(), Ops::min(Ops::max(Ops::load(mapped_y.data()), Ops::set(0)), last_y));
    // Controls along the curve: RGBA-adjacent across a lattice row, a lattice
    // row apart down a column. The fixed coordinate's line is its cell's
    // first or (fraction 1) last.
    const std::size_t step = across ? 4 : geometry.lattice_row_stride;
    std::array<Scalar, width> fraction{};
    std::array<const Scalar*, width> origin{};
    for (std::size_t lane = 0; lane < width; ++lane) {
        const std::size_t cell_x = std::min(static_cast<std::size_t>(node_x[lane]), geometry.source_width - 2);
        const std::size_t cell_y = std::min(static_cast<std::size_t>(node_y[lane]), geometry.source_height - 2);
        const Scalar u = node_x[lane] - static_cast<Scalar>(cell_x);
        const Scalar v = node_y[lane] - static_cast<Scalar>(cell_y);
        const std::size_t lattice_x = cell_x * 5 + (!across && u == Scalar(1) ? 5 : 0);
        const std::size_t lattice_y = cell_y * 5 + (across && v == Scalar(1) ? 5 : 0);
        fraction[lane] = across ? u : v;
        origin[lane] = geometry.controls + lattice_y * geometry.lattice_row_stride + lattice_x * 4;
    }
    std::array<Vector, 6> basis{};
    quintic_basis<Ops>(Ops::load(fraction.data()), basis);
    std::array<Scalar, 6 * width> weights{};
    for (std::size_t weight = 0; weight < 6; ++weight) { Ops::store(weights.data() + weight * width, basis[weight]); }
    for (std::size_t lane = 0; lane < width; ++lane) {
        std::array<Channels, groups> sums{};
        for (std::size_t group = 0; group < groups; ++group) { sums[group] = ChannelOps::set(0); }
        for (std::size_t control = 0; control < 6; ++control) {
            const Channels weight = ChannelOps::set(weights[control * width + lane]);
            const Scalar* point = origin[lane] + control * step;
            for (std::size_t group = 0; group < groups; ++group) {
                const Channels value = ChannelOps::load(point + group * ChannelOps::width);
                sums[group] = ChannelOps::add(sums[group], ChannelOps::mul(weight, value));
            }
        }
        if constexpr (Premultiplied) {
            std::array<Scalar, 4> sample{};
            for (std::size_t group = 0; group < groups; ++group) {
                ChannelOps::store(sample.data() + group * ChannelOps::width, sums[group]);
            }
            const Channels alpha = ChannelOps::set(std::clamp(sample[3], Scalar(0), Scalar(1)));
            for (std::size_t group = 0; group < groups; ++group) {
                sums[group] = ChannelOps::min(alpha, ChannelOps::max(sums[group], ChannelOps::set(0)));
            }
        }
        Scalar* pixel = output + 4 * (column + lane);
        for (std::size_t group = 0; group < groups; ++group) {
            ChannelOps::store(pixel + group * ChannelOps::width, sums[group]);
        }
    }
}

template<class Scalar, RowShape Shape, bool Premultiplied>
void curve_columns(const WarpGeometry<Scalar>& geometry, std::size_t row, ColumnInterval columns,
                   Scalar* output) noexcept {
    using Ops = NativeOps<Scalar>;
    std::size_t column = columns.begin;
    for (; column + Ops::width <= columns.end; column += Ops::width) {
        curve_group<Ops, Scalar, Shape, Premultiplied>(geometry, row, column, output);
    }
    for (; column < columns.end; ++column) {
        curve_group<ScalarOps<Scalar>, Scalar, Shape, Premultiplied>(geometry, row, column, output);
    }
}

// Columns [begin, end) of `row`: whole lane groups, then single pixels.
template<class Scalar, bool Affine, bool Masked, bool Premultiplied>
void warp_columns(const WarpGeometry<Scalar>& geometry, std::size_t row, ColumnInterval columns,
                  Scalar* output) noexcept {
    using Ops = NativeOps<Scalar>;
    std::size_t column = columns.begin;
    for (; column + Ops::width <= columns.end; column += Ops::width) {
        warp_group<Ops, Scalar, Affine, Masked, Premultiplied>(geometry, row, column, output);
    }
    for (; column < columns.end; ++column) {
        warp_group<ScalarOps<Scalar>, Scalar, Affine, Masked, Premultiplied>(geometry, row, column, output);
    }
}

// The four half-planes of the half-pixel source footprint.
enum class FootprintEdge { left, top, right, bottom };

// Whether column `column` of `row` maps inside one edge, with the rounding
// warp_group uses.
template<class Scalar>
bool within_edge(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t column,
                 FootprintEdge edge) noexcept {
    const Scalar target_x = static_cast<Scalar>(column);
    const Scalar target_y = static_cast<Scalar>(row);
    Scalar mapped_x = Scalar(0);
    Scalar mapped_y = Scalar(0);
    mapping_group<ScalarOps<Scalar>, Scalar, true>(geometry.inverse, &target_x, &target_y, &mapped_x, &mapped_y, 0);
    const Scalar half = Scalar(0.5);
    switch (edge) {
    case FootprintEdge::left: return mapped_x >= -half;
    case FootprintEdge::top: return mapped_y >= -half;
    case FootprintEdge::right: return mapped_x < static_cast<Scalar>(geometry.source_width) - half;
    case FootprintEdge::bottom: return mapped_y < static_cast<Scalar>(geometry.source_height) - half;
    }
    return false;
}

// The columns of `row` inside one edge. An affine map's rounded source
// coordinates are monotone along a row (each operation of mapping_group is a
// correctly rounded monotone function of the column), so every edge holds on
// a prefix or a suffix of the row: bisect for its end. target_width > 0.
template<class Scalar>
ColumnInterval edge_columns(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t target_width,
                            FootprintEdge edge) noexcept {
    const std::size_t last = target_width - 1;
    const bool first_within = within_edge(geometry, row, 0, edge);
    const bool last_within = within_edge(geometry, row, last, edge);
    if (first_within == last_within) {
        const ColumnInterval uniform{0, first_within ? target_width : 0};
        return uniform;
    }
    // Invariant: `low` has first_within's answer, `high` the other.
    std::size_t low = 0;
    std::size_t high = last;
    while (high - low > 1) {
        const std::size_t middle = low + (high - low) / 2;
        const bool middle_within = within_edge(geometry, row, middle, edge);
        if (middle_within == first_within) {
            low = middle;
        } else {
            high = middle;
        }
    }
    const ColumnInterval prefix{0, high};
    const ColumnInterval suffix{high, target_width};
    return first_within ? prefix : suffix;
}

// The exact columns of `row` an affine map takes inside the footprint: the
// intersection of the four edges' prefixes and suffixes. target_width > 0.
template<class Scalar>
ColumnInterval affine_footprint(const WarpGeometry<Scalar>& geometry, std::size_t row,
                                std::size_t target_width) noexcept {
    ColumnInterval inside{0, target_width};
    constexpr std::array<FootprintEdge, 4> edges{FootprintEdge::left, FootprintEdge::top, FootprintEdge::right,
                                                 FootprintEdge::bottom};
    for (const FootprintEdge edge : edges) {
        const ColumnInterval columns = edge_columns(geometry, row, target_width, edge);
        inside.begin = std::max(inside.begin, columns.begin);
        inside.end = std::min(inside.end, columns.end);
    }
    if (inside.end < inside.begin) { inside.end = inside.begin; }
    return inside;
}

} // namespace stx_conv::kernels::detail
