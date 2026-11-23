# Math you can reproduce

An interface does more floating-point work than it appears to: every
rotated shape, gradient, curve, colour conversion, eased animation and
plotted axis. When that arithmetic gives different bits on different
machines, the difference is usually invisible. Sometimes it is not: an
edge lands on the next pixel, a value rounds the other way at a threshold,
a golden image matches on one host and not on another, and a test that
passed in the lab fails in the field for reasons nobody can reproduce.
GUI.Forms' math gives the same bits on every host, processor tier and
compiler, within bounds that are stated and, for the central functions,
proved.

## The usual approach, and how it fails

Toolkits call the platform's `<cmath>`, and through it the C library's
math library, which differs from one host to the next.

- **The math library is not correctly rounded, and its errors are its
  own.** The GNU C Library "does not aim for correctly rounded results"
  outside a few exactly specified functions, and its test suite flags
  results only when they are more than 9 ulp out
  ([glibc manual, *Errors in Math Functions*](https://sourceware.org/glibc/manual/latest/html_node/Errors-in-Math-Functions.html)).
  macOS, Windows and musl each ship their own implementations, with their
  own errors, so `sin(x)` is not the same function on each.
- **The compiler may change the arithmetic.** GCC fuses multiplies and
  adds into fused multiply-adds by default outside strict ISO modes
  (`-ffp-contract=fast`;
  [GCC, *Optimize Options*](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)),
  so the same source gives different bits on a processor with fused
  multiply-add, or from a different compiler. Options such as
  `-ffast-math` go further and reassociate sums.
- **Subnormal numbers behave differently.** Some code turns on
  flush-to-zero for speed, a library built with `-ffast-math` can turn it
  on when it is loaded, and the same expression then gives a tiny number
  in one process or thread and zero in another.

None of this is a bug in any one library. It is the result of a toolkit
borrowing arithmetic it does not own.

## What GUI.Forms does instead

GUI.Forms' raster, theme, plot and control code calls STXlib's math
(STXlib `include/stxlib/math.hpp`) directly, and
{src}`tools/host_trig_check.py` refuses the host's `sin`, `cos`, `tan`,
`atan2` and `hypot` in the core, the controls and the renderer, with an
allowance file that only shrinks. Angles in degrees go through `sincosd`,
which is exact at every multiple of 90 degrees, so a quarter turn gives
exactly zero and one, not a residue that moves an edge.

% Not on main yet: the move from engine_math to stxlib::math is gui_forms branch claude/shield-math,
% landing in checkpoint 37. Until then the same paths call engine_math's deterministic functions.

STXlib's math has one contract for every function.

- **The same bits on every architecture and compiler.** The kernels
  compile only with STXlib's own flags: contraction off, no fast-math, no
  reassociation. A program's own flags never reach them. The one exact
  product they use gives the same result from a hardware fused
  multiply-add or from Dekker's split.
- **Every input is owned.** Rare inputs leave the fast path through one
  comparison; no function ever returns the C library's result.
- **No subnormals, without touching the environment.** A subnormal
  argument is read as zero of its sign, and a result that would be
  subnormal is returned as zero of its sign. This is done in software, on
  the bits; the floating-point environment (flush-to-zero, rounding mode,
  flags) is never changed, so nothing depends on which thread set what.
- **Bounds are stated per function,** measured against double-double
  references, with the method named.

**Why it holds.** `stxlib_bit_identity` hashes each promised output over
fixed inputs (FNV-1a over each value's bits, NaN hashed as one canonical
NaN), and every host, SIMD level and product form must print the same line;
`tools/compare_bit_identity.py` compares the lines with a frozen file, and a
frozen item's inputs never change. The math tests also carry frozen output
hashes, run once per forced SIMD level and once with Dekker's product. The
FFT kernels give every SIMD tier the scalar tier's value for every output,
except possibly the sign of a zero.

## Stated bounds

Each bound is quoted from the function's contract or from STXlib's proof
report. "Proved" means a deductive argument or a certificate in exact
arithmetic; "sampled" means a test over many inputs, which is evidence,
not proof.

| Function | Bound | How it is established |
|---|---|---|
| `sincos(float)`, 2^-27 ≤ \|x\| ≤ 2^32 | correctly rounded | proved: an error bound with all 989,855,746 floats in range screened against it, and the 44 it leaves decided exactly |
| `sincos(double)`, same range | within 0.51 ulp | proved by certificate (`tools/math/sincos_certificate.py`); largest measured error 0.500415 ulp over 120 million inputs |
| `exp(float)` | correctly rounded | exhaustive: all 4,278,190,080 finite floats |
| `exp(double)` | within 0.500001 ulp | sampled: 100 million inputs |
| `log`, `log2`, `log10` (double) | within 0.500012 ulp | sampled: 240 million inputs per function |
| `pow(double)`, normal results | within 0.51 ulp | an exact rational budget gives 0.509424 ulp for a model of the kernel (`tools/math/pow_grid_certificate.py`); that the model matches the code is argued, not machine-checked |
| `sinpi`, `cospi`, `sincpi`, `cosm1` | within 0.5005 ulp (test limit) | sampled, not proved |

`sinpi`, `cospi` and `sincospi` take the angle in half turns, so whole and
half turns give exact results with stated signs; the exactness of the
head of each result over all 256 table cells is checked at compile time and
by a certificate. `cosm1` computes cos − 1 without the cancellation that
`cos(x) - 1` suffers near zero.

Faster or more accurate kernels found in outside studies are not pasted in.
They are reimplemented in the house style with the study's code as a
reference: the faster `pow`'s 1,024-cell table is generated, checked cell
for cell against the study's, and every cell's reduction is checked by a
`static_assert` (STXlib `PROVENANCE.md`).

## Limits

- Several of the large counts (the exhaustive float checks among them)
  were run on the same kernels in STXlib's predecessor library. STXlib's
  own build reruns the frozen-hash tests, and its proof report says which
  larger runs have been repeated on it.
- Angles for `sin`, `cos` and `tan` are supported up to 2^32 radians;
  larger ones return NaN. A phase that grows without bound must reset
  itself.
- Speed is not a reliability claim, and none is made here.
