# CONV kernel collection

The enhanced streaming path, instruction kernels and transform queries are
source-owned library components, built and installed by `stx_conv/CMakeLists.txt`.
There is no runtime BFFT, Plan Paint, codec, graphics or thread-pool dependency.
This buildout follows BFFT revision 6e64386's separation of backend primitives,
small arithmetic kernels, prepared schedules, public owners and independent
instruction probes. It does not copy its FFT transform or FMA rounding policy.

## Where to edit instructions

| Source | Responsibility |
|---|---|
| `src/detail/conv_simd.hpp` | Scalar, NEON f32x4/f64x2, SSE2 f32x4/f64x2 primitives |
| `src/detail/conv_jets.hpp` | Five-point interior first/second jet terms |
| `src/detail/conv_raw.hpp` | Endpoint jets into five quintic currents |
| `src/detail/conv_box_admission.hpp` | Fixed capacity threshold and fused admission |
| `src/detail/conv_bubble.hpp` | Reduced residual functional and sign-preserving amplitude |
| `src/detail/conv_synthesis.hpp` | Zero/active bubble and opening/accumulating synthesis kernels |
| `src/detail/conv_tensor.hpp` | Batch tensor evaluation and fused RGBA patch/curve/node queries |
| `src/detail/conv_mapping.hpp` | Affine and projective coordinate kernels |
| `src/stream.cpp` | Plan construction, validation, node rings, pass ordering and output ownership |
| `src/kernels.cpp` | Public batch wrappers and checked mapping/atlas boundaries |

Geometry, sampling policy and backend selection happen outside arithmetic lane
loops. Scalar tails remain explicit. There is no fast-math or FP contraction.
The generic tensor batch uses SoA planes for independent positions; the atlas
patch kernel uses RGBA as its contiguous lane dimension. Geometry dispatch on
exact grid lines happens once per query, outside channel-vector loops. A grid
line uses six controls; an exact source node copies one RGBA control; a generic
query uses 36. No epsilon snapping changes the requested coordinate.

## Standard streaming use

```cpp
#include <stx_conv/stream.hpp>
stx_conv::stream::Workspace<double> work(sw, sh, tw, th, channels,
                                        stx_conv::Sampling::pixel_area);
// Bounded bubble correction is enabled by default.
// work.set_bubble_enabled(false); // optional five-current base comparison
const stx_conv::Status status = stx_conv::stream::resize<double>(
    work, {input, input_stride}, {output, output_stride});
```

Link `stx_conv::stream`. Input/output spans are disjoint, strides count Scalar
elements. Workspace construction allocates all repeated-use storage. A workspace
is fixed-shape, noncopyable/nonmovable and admits one caller at a time. The toggle
may change between calls without allocation; changing it during a call races.
Plans retain their prepared weights, and the disabled path skips amplitude
computation and correction loads. Identity/unchanged axes and original short-axis
closures remain. Input validation failure preserves output; numerical failure
may leave partial output. Existing finite-range limits apply.

The pre-existing `conv.hpp` profile/image API is retained for compatibility and
as a reference. The enhanced streaming API is the recommended path for this
buildout. The old ingest bubble target forwards to it; it no longer owns a copy
of the resizer. The base ingest retains its numerical comparison implementation.

## Kernels and transforms

Link `stx_conv::kernels`, include `stx_conv/kernels.hpp`. Leaf calls are low-level,
allocation-free borrowed-buffer kernels with explicit count, stride, finite
intermediate and disjoint-output preconditions. They intentionally do not scan
all input streams for validation. Checked mapping and atlas-query wrappers
validate shape/access before output mutation. Keep compiler arithmetic choices
consistent with the library when comparing numerical results.

An inverse 3x3 homography maps target centres to source coordinates. Its affine
specialization covers resize/stretch, rotation, skew and translation without
perspective divisions. Perspective correction uses the projective denominator;
zero horizons or nonfinite coordinate results fail before writing outputs.
These are point-coordinate kernels. Area sampling additionally needs target
footprints and positive quadrature; coordinate mapping alone is not an
antialiasing method.

`tensor_patch_rgba` consumes six rows of six RGBA controls with an explicit row
stride. It assumes the caller has already located and validated the patch.
`sample_rgba` additionally handles a shared five-controls-per-cell atlas,
half-pixel source boundaries and transparent exterior. Its optional physical
projection clamps opacity and then color at each query, before any positive
filter weights. The view borrows controls for the call; it does not allocate or
recompile the source. These functions evaluate the existing quintic atlas;
bubble enrichment of a 2D warp atlas is not silently inferred from five currents.

Plan Paint's consumer adapter uses exact grid-boundary/node reductions after
its existing bounds/cell lookup. The affine point path selects this before filtering. Dense minification
and area queries retain its existing tensor evaluator: routing dense minification through the standalone patch kernel
regressed application timings. Atlas compilation, clipped footprint quadrature
and per-sample physical projection remain. `integrate_patch_rgba` is available
for further instruction optimization; it accumulates known-patch samples with
physical projection before positive weighting, but is not selected by this
adapter. Resize distributes independent rows or column bands over up to eight
workers, one workspace each, and defaults bubble on. Build options are
`PLAN_PAINT_CONV_KERNELS=AUTO|ON|OFF` and `PLAN_PAINT_CONV_BUBBLE=ON|OFF`.
The homography kernel is available for true perspective point queries; this
change does not add a new perspective-area rasterizer or UI control.

## Instruction workflow

Build with `STX_CONV_BUILD_KERNEL_PROBES=ON` for stable C probe symbols covering
jets, raw currents, capacity, admission, bubble, synthesis, tensor, patch, affine
and projective kernels, each in float and double. The probe is an instruction
inspection target, not a separate supported application ABI.

On macOS with an installed Clang supporting both targets:

```sh
CXX=clang++ sh stx_conv/tools/kernels/inspect.sh build/conv-kernel-assembly
python3 stx_conv/tools/kernels/audit.py build/conv-kernel-assembly
```

The script records compiler version and emits NEON and baseline SSE2 assembly,
using -O3 and -ffp-contract=off, with no AVX/FMA in the x86 target. Inspect full
functions as well as counts: stack arguments are not automatically spills,
constant broadcasts are not signal-lane permutations, and instruction count is
not throughput. Keep the numerical and sanitizer suite alongside each change.

Build `STX_CONV_BUILD_KERNEL_BENCHMARKS=ON` for `stx_conv_kernel_benchmark`.
It prepares 65,536 positions, warms each kernel, then reports a median of five
batches of eight calls. Allocation is excluded. Checked coordinate mapping
includes validation. The scalar-backend control is `STX_CONV_FORCE_SCALAR=ON`;
the compiler may still auto-vectorize scalar code. Isolated kernel timings do
not establish complete-image or threaded-application performance.

Use the same C++ toolchain for library and installed consumer. Our verified SDK
uses LLVM 22.1.8; mixing it with Apple Clang 21 produced incompatible mangling of
constrained function templates. The installed-consumer test selects LLVM22.

## Verification scope

The ingest regression harness retains 2,112 base bitwise fixtures and 2,112
bubble numerical fixtures, both precisions and sampling modes, including mixed
pass orders, short/singleton axes, padding and high-range values. New kernel
checks include tails, source-node/cardinal queries, an independent long-double
de Casteljau tensor oracle, analytic transformed ramps, perspective horizons,
overlap rejection and repeatable bubble toggling. Boundary specialization is
compared with the generic tensor evaluator.

The focused Plan Paint build tests atlas range/continuity, affine/mesh sampling,
physical alpha before filtering, identity, invalid transforms, resize cardinality,
aliasing, parallel row/column cardinality and destination preservation. It is not the full application/package
suite. No universal image-quality or speed claim follows from these tests.
