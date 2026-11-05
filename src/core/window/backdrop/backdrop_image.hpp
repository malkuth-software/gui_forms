#pragma once

#include "../../engine/primitives/kernel/box_blur.hpp"

#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/window/backdrop/backdrop_chain.hpp"

#include <cstddef>
#include <cstdint>

// The backdrop's pixels (owner, 2026-11-05;
// gui_forms/window/backdrop/backdrop_chain.hpp): a chain's operations
// applied in order to a source image, renderer-neutral and deterministic
// (the blur is the engine's integer box blur; the per-pixel operations run
// in double precision without fused multiply-add, rounded once per pixel).
namespace gui_forms::detail {

// How far past each side of a destination the chain reads its source, in
// device pixels at `scale`: three box half widths for its blur, 0 without.
[[nodiscard]] std::uint32_t backdrop_margin(const BackdropChain& chain, double scale) noexcept;

// Storage reused from one computation to the next; owned by the caller.
struct BackdropWork final {
    engine_primitives::BoxBlurWork blur{};
};

// Applies `chain` to `source` (premultiplied BGRA8, the destination grown by
// `margin` = backdrop_margin(chain, scale) on every side) and writes the
// result into every pixel of `destination` (premultiplied BGRA8). Reads
// `source` only; `source` and `destination` must not overlap. False,
// writing nothing, when the extents disagree or the work storage cannot be
// held (std::bad_alloc is caught).
[[nodiscard]] bool compute_backdrop(const BackdropChain& chain, double scale, const PartRasterView& source,
                                    std::uint32_t margin, const PartRasterView& destination,
                                    BackdropWork& work) noexcept;

} // namespace gui_forms::detail
