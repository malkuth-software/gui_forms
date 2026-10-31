#pragma once
#include <stx_conv/conv.hpp>

namespace stx_conv::kernels {
// Low-level kernels borrow finite, length-counted SoA streams. All streams
// contain count elements; output is disjoint from inputs. No allocation or
// retained addresses. Zero count permits null streams. Alignment is unrestricted.
// Capacity inputs are nonnegative, mass lies in [0,sum(capacity)], sums finite.
[[nodiscard]] const char* backend() noexcept;
template<Precision Scalar>
void capacity(const Scalar* const capacities[5], const Scalar* mass,
              Scalar* output, std::size_t count) noexcept;
// Five input streams (m2,m1,centre,p1,p2); disjoint output streams store
// first/5, twice-first/5, and second/20. Boundary closures are separate.
template<Precision Scalar>
void jet_terms(const Scalar* const source[5], Scalar* fifths, Scalar* two_fifths,
               Scalar* twentieths, std::size_t count) noexcept;
// Input streams: y0,y1,first0/5,2first0/5,second0/20,
// first1/5,2first1/5,second1/20. Five output current streams are disjoint.
template<Precision Scalar>
void raw_currents(const Scalar* const input[8],Scalar* const output[5],std::size_t count) noexcept;
// raw/admitted are [current][lane]. Output stores w=3a/5; synthesis weights
// must include 5/3. Select interior once per cell, not once per lane.
template<Precision Scalar>
void bubble(const Scalar* raw, const Scalar* admitted, Scalar* output,
            std::size_t stride, std::size_t count, bool interior) noexcept;
// Five-current admission over independent lanes. Carry arrays are mutable;
// other inputs and every output stream must be disjoint. Geometry is prepared.
template<Precision Scalar>
void admit_cell(const Scalar* const raw[5],Scalar* const output[5],const Scalar* y0,
    const Scalar* y1,const Scalar* y2,const Scalar* next,Scalar* left_delta,
    Scalar* left_last,std::size_t count) noexcept;
// Current planes are contiguous with stride >= count. Bubble may be null only
// when bubble_weight is zero. All outputs disjoint from all inputs.
template<Precision Scalar>
void synthesize(const Scalar* anchors,const Scalar* currents,std::size_t stride,
    const std::array<Scalar,5>& weights,Scalar anchor_weight,const Scalar* bubble,
    Scalar bubble_weight,Scalar* output,std::size_t count,bool accumulate) noexcept;
// Each control is a stream over independent positions/channels. Slot index
// is y*6+x. u,v lie in [0,1]; control units and output units are unchanged.
template<Precision Scalar>
void tensor_quintic(const Scalar* const controls[36], const Scalar* u,
                    const Scalar* v, Scalar* output, std::size_t count) noexcept;

// One known RGBA atlas patch: six rows, six RGBA controls per row. Caller
// establishes the patch bounds and finite u,v in [0,1]. Output has four elements.
// No range projection; use before physical color projection and filtering.
template<Precision Scalar>
void tensor_patch_rgba(const Scalar* origin,std::size_t row_stride,Scalar u,Scalar v,Scalar* output) noexcept;

// Positive quadrature within one prepared patch. u,v,weights contain count
// elements, fractions lie in [0,1], weights are finite/nonnegative. Project
// each sample to physical premultiplied RGBA before adding to four finite
// caller-initialized sums. No allocation; all input streams disjoint from sums.
template<Precision Scalar>
void integrate_patch_rgba(const Scalar* origin,std::size_t row_stride,const Scalar* u,
    const Scalar* v,const Scalar* weights,std::size_t count,Scalar* sums) noexcept;

// Inverse target-to-source homography, row major. The affine case has final
// row (0,0,1). Validate all coordinates before mutation; output spans disjoint
// from inputs/each other. Perspective horizons and nonfinite values fail.
template<Precision Scalar>
[[nodiscard]] Status map_points(const std::array<Scalar,9>& inverse,
    std::span<const Scalar> x, std::span<const Scalar> y,
    std::span<Scalar> source_x, std::span<Scalar> source_y) noexcept;

// Shared tensor-quintic atlas, matching Plan Paint's [lattice-y][lattice-x][RGBA]
// layout. Each source cell advances five controls in each direction. RGBA is
// premultiplied and normalized. Source dimensions >=2; finite controls required.
// Borrow lifetime covers the call; projection clamps alpha then RGB per query.
// Outside the half-pixel footprint is transparent. Centres extend half a pixel.
template<Precision Scalar>
struct TensorAtlasView {
    std::span<const Scalar> controls;
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    std::size_t lattice_row_stride = 0; // Scalar elements, including RGBA.
};
enum class ColorProjection { none, premultiplied };
template<Precision Scalar>
[[nodiscard]] Status sample_rgba(TensorAtlasView<Scalar> atlas,
    std::span<const Scalar> source_x, std::span<const Scalar> source_y,
    std::span<Scalar> rgba, ColorProjection projection = ColorProjection::premultiplied) noexcept;
}
