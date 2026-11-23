# Math you can reproduce

STXlib's transcendental functions (STXlib `include/stxlib/math.hpp`) replace
`<cmath>`, whose results differ between C libraries. Inside GUI.Forms, the
raster, theme and control paths call the deterministic functions of
{src}`include/gui_forms/engine/engine_math/engine_math.hpp`. Each states its
error bound. For example, `engine_sine_cosine_degrees` is exact at every
multiple of 90 degrees and within 2^-49 absolute elsewhere. Moving their
bodies to STXlib's kernels is planned. {src}`tools/host_trig_check.py`
refuses the host's `sin`, `cos`, `tan`, `atan2` and `hypot` in
`src/core`, `src/controls` and `src/render`. Its allowance file only
shrinks.

The rest of this page is about STXlib's kernels.

## The contract

- **The same bits on every architecture and compiler.** The kernels compile
  only with STXlib's own flags: `-ffp-contract=off -fno-fast-math
  -fno-math-errno`, never `-ffast-math`, `-Ofast` or reassociation. A
  consumer's flags never reach them. The one exact product the kernels use
  gives the same result from a hardware fused multiply-add or from Dekker's
  split.
- **Every input is owned.** Rare inputs leave the fast path through one
  comparison, and no function returns the C library's result.
- **No subnormals.** A subnormal argument is read as zero of its sign, and a
  result that would be subnormal is returned as zero of its sign. This is
  done in software, on the bits. The floating-point environment
  (flush-to-zero, rounding mode, flags) is never changed (STXlib
  `docs/CODE_STYLE_APPENDIX.md`, A1).
- **Bounds are stated per function** and measured against double-double
  references (`tests/reference/wide.hpp`), with the method named.

## Stated bounds

Each bound below is quoted from the function's contract or from the proof
report. The large counts, for example the exhaustive float checks, were run
on the build of the same kernels in STXlib's predecessor library. STXlib's
own build reruns the frozen-hash tests in CTest, and the proof report says
which of the larger runs have been repeated on it.

| Function | Bound | How it is established |
|---|---|---|
| `sincos(float)`, 2^-27 ≤ \|x\| ≤ 2^32 | correctly rounded | proved: an error bound with all 989,855,746 floats in range screened against it, and the 44 it leaves decided exactly |
| `sincos(double)`, same range | within 0.51 ulp | proved by certificate (`tools/math/sincos_certificate.py`); largest measured error 0.500415 ulp over 120 million inputs |
| `exp(float)` | correctly rounded | exhaustive: 4,278,190,080 finite floats |
| `exp(double)` | within 0.500001 ulp | measured over 100 million inputs |
| `log`, `log2`, `log10` (double) | within 0.500012 ulp | measured over 240 million inputs per function |
| `pow(double)`, normal results | within 0.51 ulp | an exact rational budget of every rounding gives 0.509424 ulp for a model of the kernel (`tools/math/pow_grid_certificate.py`). That the model matches the code is argued, not machine-checked. The tests hold 0.51 (`pow_ulp_limit`) |
| `sinpi`, `cospi`, `sincpi`, `cosm1` | within 0.5005 ulp (test limit) | sampled, not proved: at most 0.500102 to 0.500169 ulp over 4 × 100,000 inputs |
| `cosm1pi` | within 0.502 ulp (test limit) | sampled, not proved |

## Half-turn trigonometry

`sinpi`, `cospi` and `sincospi` take the angle in half turns, so whole and
half numbers give exact results with stated signs. The phase is reduced
modulo 2 in whole numbers from 2^44 up. The head of each result (anchor plus
slope over 256 cells) is exact by construction. This is proved by
compile-time checks over all 256 cells (`half_turn_cells_valid`) and by
`tools/math/half_turn_certificate.py`. The same certificate checks that the
series coefficients are correctly rounded and bounds the truncation: sin
below 2^-75, cos below 2^-83 absolute.

`cosm1(x)` and `cosm1pi(x)` compute cos − 1 without the cancellation that
`cos(x) - 1` suffers near zero. For sin(x)/x, use `sincpi(x / pi)`.

## Bit identity across hosts and SIMD tiers

`stxlib_bit_identity` hashes each promised output over fixed inputs, using
FNV-1a 64 over each value's bits as little-endian bytes, with NaN hashed as
one canonical NaN. Every host, SIMD level and product form must print the
same line. `tools/compare_bit_identity.py` compares the lines with
`tests/bit_identity_expected.txt`, and a frozen item must match exactly. An
item's inputs never change once frozen. The math tests also carry frozen
output hashes, run once per forced SIMD level and once with the Dekker
product.

The FFT kernels give every SIMD tier the scalar tier's value for every
output, except possibly the sign of a zero. Each operation is element-wise,
and every fused multiply-add of the original kernels is spelled as a
separate multiply and add with contraction off.

## Replace, do not replicate

Faster or more accurate kernels found in outside studies are not folded in
as written. They are reimplemented in the house style, with the study's
code used only as a reference. The faster `pow` uses a 1024-cell reciprocal
grid. Its table is generated, the written table equals the study's cell for
cell (`tools/math/gen_pow_grid_table.py --check`), and a `static_assert`
checks every cell's reduction. Each replaced implementation keeps its
reference, and a conformance check against it (STXlib `PROVENANCE.md`).

## How it is tested

- frozen output hashes and the bit-identity check, above;
- double-double references, and reference rows computed with MPFR;
- certificates in exact rational arithmetic (`tools/math/*_certificate.py`),
  each stating what it covers and what is argued;
- STXlib's `proofs/REPORT.md`, which labels every property as proved,
  argued, sampled or not yet run.

Speed is not a reliability claim, and none is made here.
