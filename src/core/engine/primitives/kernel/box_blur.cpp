#include "box_blur.hpp"

#include "../../../types/restrict/restrict.hpp"

#include <algorithm>
#include <array>
#include <cmath>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

// (sum + h) / (2h + 1) without a division. Every element is at most 255,
// and so is every rounded average, so a window's sum x is at most
// 255 × w + h < 256 × w for the width w = 2h + 1. With
// m = ceil(2^40 / w), floor(x × m / 2^40) = floor(x / w) whenever
// 256 × w² < 2^40, that is w < 2^16; w is at most 2049 (h ≤ 1024).
struct BoxAverage final {
    std::uint64_t multiplier{};
    std::uint32_t half{};
};

constexpr unsigned average_shift = 40U;

BoxAverage box_average(std::uint32_t half) noexcept {
    const std::uint64_t width = static_cast<std::uint64_t>(half) * 2U + 1U;
    const std::uint64_t scaled = std::uint64_t{1} << average_shift;
    const BoxAverage average{(scaled + width - 1U) / width, half};
    return average;
}

std::uint32_t averaged(std::uint32_t sum, const BoxAverage& average) noexcept {
    const std::uint64_t rounded = static_cast<std::uint64_t>(sum) + average.half;
    const std::uint32_t value = static_cast<std::uint32_t>((rounded * average.multiplier) >> average_shift);
    return value;
}

// One pass across: `rows` rows of `length` pixels of `lanes` lanes, rows
// `stride` elements apart in both buffers, averaged over windows of
// 2h + 1 pixels into length − 2h pixels per row.
struct AcrossPass final {
    std::size_t rows{};
    std::size_t length{};
    std::size_t stride{};
    std::size_t lanes{};
};

// Runs `pass` from `source` into `target`. Restricted parameters: no element
// the pass reads through `source` is reached through `target` (box_across
// checks it).
void across_rows(const std::uint32_t* GUI_FORMS_RESTRICT source, std::uint32_t* GUI_FORMS_RESTRICT target,
                 const AcrossPass& pass, const BoxAverage& average) noexcept {
    const std::size_t width = static_cast<std::size_t>(average.half) * 2U + 1U;
    const std::size_t outputs = pass.length - (width - 1U);
    const std::size_t lanes = pass.lanes;
    for (std::size_t row = 0U; row < pass.rows; ++row) {
        const std::size_t first = row * pass.stride;
        std::array<std::uint32_t, box_blur_lanes> sums{};
        for (std::size_t node = 0U; node < width; ++node) {
            const std::size_t at = first + node * lanes;
            for (std::size_t lane = 0U; lane < lanes; ++lane) sums[lane] = sums[lane] + source[at + lane];
        }
        for (std::size_t output = 0U; output < outputs; ++output) {
            const std::size_t write = first + output * lanes;
            for (std::size_t lane = 0U; lane < lanes; ++lane) target[write + lane] = averaged(sums[lane], average);
            if (output + 1U == outputs) break;
            // Slide the window one pixel: add the next, drop the first.
            const std::size_t entering = first + (output + width) * lanes;
            const std::size_t leaving = first + output * lanes;
            for (std::size_t lane = 0U; lane < lanes; ++lane) {
                sums[lane] = sums[lane] + source[entering + lane] - source[leaving + lane];
            }
        }
    }
}

// Runs `pass` from `from` into `to`, buffers holding every element the
// pass reads or writes. Overlap rule: `from` and `to` share no element
// (asserted in debug builds), the promise across_rows's restricted
// parameters make.
void box_across(std::span<const std::uint32_t> from, std::span<std::uint32_t> to, const AcrossPass& pass,
                const BoxAverage& average) noexcept {
    gui_forms::detail::require_disjoint(from, to);
    across_rows(from.data(), to.data(), pass, average);
}

// One pass down: `length` rows of `elements` elements each (the columns
// left times the lanes), rows `stride` elements apart in both buffers,
// averaged over windows of 2h + 1 rows into length − 2h rows. It reads and
// writes row by row, keeping each column's window sum in `sums` (at least
// `elements` long).
struct DownPass final {
    std::size_t elements{};
    std::size_t length{};
    std::size_t stride{};
};

// Runs `pass` from `source` into `target`, keeping the window sums in
// `column_sums`. Restricted parameters: no element is reached through two
// of them (box_down checks it).
void down_rows(const std::uint32_t* GUI_FORMS_RESTRICT source, std::uint32_t* GUI_FORMS_RESTRICT target,
               std::uint32_t* GUI_FORMS_RESTRICT column_sums, const DownPass& pass,
               const BoxAverage& average) noexcept {
    const std::size_t width = static_cast<std::size_t>(average.half) * 2U + 1U;
    const std::size_t outputs = pass.length - (width - 1U);
    const std::size_t elements = pass.elements;
    std::fill(column_sums, column_sums + elements, 0U);
    for (std::size_t node = 0U; node < width; ++node) {
        const std::size_t at = node * pass.stride;
        for (std::size_t element = 0U; element < elements; ++element) {
            column_sums[element] = column_sums[element] + source[at + element];
        }
    }
    for (std::size_t output = 0U; output < outputs; ++output) {
        const std::size_t write = output * pass.stride;
        for (std::size_t element = 0U; element < elements; ++element) {
            target[write + element] = averaged(column_sums[element], average);
        }
        if (output + 1U == outputs) break;
        // Slide every column's window one row: add the next, drop the first.
        const std::size_t entering = (output + width) * pass.stride;
        const std::size_t leaving = output * pass.stride;
        for (std::size_t element = 0U; element < elements; ++element) {
            column_sums[element] = column_sums[element] + source[entering + element] - source[leaving + element];
        }
    }
}

// Overlap rule: `from`, `to` and the first pass.elements of `sums` share no
// element (asserted in debug builds), the promise down_rows's restricted
// parameters make.
void box_down(std::span<const std::uint32_t> from, std::span<std::uint32_t> to, std::span<std::uint32_t> sums,
              const DownPass& pass, const BoxAverage& average) noexcept {
    const std::span<std::uint32_t> column_sums = sums.first(pass.elements);
    gui_forms::detail::require_disjoint(from, to);
    gui_forms::detail::require_disjoint(from, column_sums);
    gui_forms::detail::require_disjoint(to, column_sums);
    down_rows(from.data(), to.data(), column_sums.data(), pass, average);
}

} // namespace

std::uint32_t box_blur_half(double blur, double scale) noexcept {
    const double sigma = blur * scale * 0.5;
    const double width = std::sqrt(4.0 * sigma * sigma + 1.0);
    const double half = std::round((width - 1.0) * 0.5);
    if (!(half > 0.0)) return 0U;
    const std::uint32_t pixels = static_cast<std::uint32_t>(std::min(half, 1024.0));
    return pixels;
}

void prepare_box_blur(BoxBlurWork& work, std::size_t columns, std::size_t rows, std::size_t lanes) {
    const std::size_t elements = columns * rows * lanes;
    work.first.assign(elements, 0U);
    work.second.assign(elements, 0U);
    work.sums.resize(columns * lanes);
}

void blur_three_boxes(BoxBlurWork& work, std::size_t columns, std::size_t rows, std::size_t lanes,
                      std::uint32_t half) noexcept {
    if (half == 0U) return;
    const BoxAverage average = box_average(half);
    const std::size_t stride = columns * lanes;
    const std::size_t step = static_cast<std::size_t>(half) * 2U;
    // Across, three times: first → second → first → second.
    AcrossPass across{rows, columns, stride, lanes};
    box_across(work.first, work.second, across, average);
    across.length = across.length - step;
    box_across(work.second, work.first, across, average);
    across.length = across.length - step;
    box_across(work.first, work.second, across, average);
    // Down, three times over the columns left: second → first → second →
    // first.
    DownPass down{(columns - step * 3U) * lanes, rows, stride};
    box_down(work.second, work.first, work.sums, down, average);
    down.length = down.length - step;
    box_down(work.first, work.second, work.sums, down, average);
    down.length = down.length - step;
    box_down(work.second, work.first, work.sums, down, average);
}

} // namespace gui_forms::engine_primitives
