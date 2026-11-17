# The blending rule (blend and opaque study, part 2)

Draft for `planning/` on the notes `gui_forms` branch, written in the study
branch `claude/blend-study-1` (`build-notes/BLENDING_RULE.md`). The raster
coder (patterns and geometry, `claude/raster-patterns-1`) builds on the rule
in section 3. It describes the code at `claude/batch-cp33` 051e41ac5; the
numbers the study measures come later, from `tools/blend_study.py`.

## 1. What GUI.Forms blends today

**Representation.** Every frame GUI.Forms paints is premultiplied BGRA8,
8 bits a channel, in sRGB-encoded values. The window raster
(`SkiaRaster`, contract SK2), part rasters, paint-group scratch images,
images after decode and live-surface frames all use that layout. Straight
alpha appears only at the edges: PNG files (decoded straight to
premultiplied by `stx_png`), the PNG encoder's input option, and the
clipboard.

**Space.** Blending happens on the encoded sRGB values, not in linear light.
Gradients are the exception the owner chose on 2026-11-16 ("im okay with
linear"): their stops interpolate in linear light (section 3).
Every source is sRGB: the Skia surface, images and gradients are tagged
`SkColorSpace::MakeSRGB()`, so Skia converts nothing. PNG decode converts
gAMA/cHRM/cICP content to sRGB once, at decode (`src/core/image/png_color`).
The only linear-light work is inside the theme engine: lighting and finish
kernels decode sRGB bytes to linear floats through a table, compute, and
encode back to sRGB bytes through the reference's 4097-step table
(`finish_kernel`, `light_planes`), before the result becomes a part raster.
That is computation of a colour, not blending of two layers.

**Who blends, and how:**

| Path | Arithmetic | Deterministic across hosts and CPUs |
|---|---|---|
| Text coverage (`composite_text_coverage`, ADR-004) | integer, premultiplied, each channel rounded once: `(s·a + d·(255−a) + 127) / 255` | yes |
| Part rasters, baked materials, isolated paint groups (`composite_part_raster`) | integer: weight `w` = opacity·clip coverage, `s' = (s·w + 32640) / 65280`, then source-over; plus, multiply, screen and copy as `text_composite.hpp` states; an opaque raster at full weight is copied exactly | yes |
| Rectangle fills, rounded rectangles, paths, strokes, shadows, gradients, images, patterns (Skia CPU raster pipeline) | Skia's: float (highp) or 16-bit fixed (lowp) stages, chosen per draw; source-over unless a copy | no: see section 2 |
| Gradients (Skia: Painter's linear, radial, focal, conic and spread fills; the executor's GUI.Drawing brushes) | the stops expanded first (`expand_linear_light_stops`, `gui_forms/types/linear_light`): colours added between stops wherever the linear-light interpolation leaves Skia's premultiplied sRGB chord by more than half a step, computed in integers; Skia then interpolates the expanded stops premultiplied in sRGB (`fInPremul = kYes`, `ColorSpace::kSRGB`), within 1.3 steps of the exact colour | stop colours yes; Skia's per-pixel interpolation no |
| Gradients (engine: material kernel, presets, relief faces, authored gradients' clipped stops and averages, duotone and tritone tables, GUI.Drawing blend factors; not the colour picker, section 5) | premultiplied in linear light by `interpolate_linear_light`: integer tables both ways, no libm | yes |
| Opaque live surfaces, opaque device tiles | row copies (`writePixels`, `copy_opaque_tile`) | yes |
| Theme engine colour computation | float in linear light, tables both ways; the authored gradient avoids fused multiply-add on purpose | yes, where FMA is kept out |

**Where conversions happen:** straight to premultiplied at PNG decode;
sRGB curve both ways inside the theme engine's kernels; colour profile to
sRGB at PNG decode; none at blend time; at present, macOS hands the frame to
the window server tagged sRGB (`kCGColorSpaceSRGB`), which colour-manages it
to the display, while the Windows (GDI DIB) and Linux (X11 shared memory) hosts present
the bytes as they are (not checked for the Linux host's other paths).

## 2. Where results depend on the host or the CPU

1. **Skia's pipeline choice.** The same draw can run through the lowp
   (16-bit fixed) or highp (float) pipeline depending on the blend, the
   shader and the CPU, and the SIMD width differs by CPU (SSE2, AVX2,
   NEON). Rounding then differs by up to one step per channel, mostly on
   antialiased edges, gradients and filtered images. Pixel hashes of
   Skia-drawn content are per-architecture today.
2. **Fused multiply-add.** Skia's float stages use `mad`, which is a fused
   operation on arm64 and on x86 only where the build enables FMA, so float
   results differ between architectures. Our own float code follows the
   build (owner, 2026-11-16: "turn it off for referencing, and leave it on
   for actual deployment"): with `GUI_FORMS_REFERENCE_ARITHMETIC=ON` (the
   reference and conformance builds: `tools/ci_build.py`'s native and
   sanitizer trees, the engine gate, the pixel-hash tests and the
   cross-host comparisons) every first-party target compiles with
   `-ffp-contract=off` and the raster, engine, material, theme-kernel and
   effect sources add the pragma; with it OFF (deployment: the SDK, the
   release packages' consumers, profiling) the compiler contracts as it
   chooses (the SDK, profiling, and the release programs job that builds
   what ships, section 5, item 5). `engine_math` (its error bounds) and the
   value paths (parsed text, layout lengths, theme values, motion) stay
   unfused in every build (section 5, item 4);
   stx_skia sets its own `-ffp-contract=off`. Skia's explicit fused stages
   are logged, not fixed, before 1.0.
3. **Text where pixels cannot be addressed.** Where the canvas pixels are
   not addressable, Skia blends text coverage as a mask instead of
   `composite_text_coverage` (`skia_raster.hpp`). The window raster is
   always addressable; offscreen captures may not be.
4. **Presentation.** The macOS window server colour-manages the sRGB frame
   to the display, and for a non-opaque backdrop composites its
   premultiplied alpha over the desktop in its own space. Windows and Linux
   show the bytes. So what reaches the eye differs by host even when the
   bytes are equal; the bytes are what GUI.Forms owns and tests.
5. **Opacity quantization** is ours and fixed: `floor(o·255 + 0.5)`
   (`part_composite_opacity`).

## 3. The proposed rule (one rule)

> **GUI.Forms composites premultiplied, sRGB-encoded 8-bit BGRA. Every
> blend of one layer over another is source-over (or a named group
> composite) in integer arithmetic with the rounding of
> `composite_part_raster`: the weighted source rounded once,
> `s' = (s·w + 32640) / 65280`, then `out = s' + (d·(255 − α') + 127) / 255`.
> A source whose every pixel the writer has proved opaque, at full weight
> and full coverage, is a copy, and must produce the same bytes as the
> blend would. Colour is computed (lighting, gradients, colour profiles) in
> linear light where a stage says so, and stored back as sRGB bytes before
> it is composited; every gradient interpolates its stops premultiplied in
> linear light, by integer tables. Our engine's output is the reference;
> Skia's, a host's and a GPU's are compared against it, never the other
> way round. The reference is computed with the reference arithmetic (no
> floating-point contraction); deployment builds may fuse. Remaining
> floating-point differences between architectures (Skia's fused stages,
> SIMD width) are logged against the reference before 1.0, not fixed.**

What follows from it, for the raster coder and the apply step:

- A new raster kernel (patterns, geometry) produces coverage and a
  premultiplied source, and composites through the integer blend; it does
  not invent its own rounding. Coverage is computed in 1/256-pixel fixed
  point, as the text and clip paths are.
- Opaque is a property the producer proves and records (a part raster's
  `opaque()`, a decoded image's `every_pixel_opaque`, an alpha-255 fill),
  never one the compositor guesses per pixel. Proving it lets the
  compositor choose the copy kernel before the loop (house style §8), and
  the copy is exact by the rule above, so it moves no bytes.
- Gradients interpolate premultiplied in linear light (owner, 2026-11-16):
  each pair of neighbouring stops is decoded from sRGB to 16-bit linear
  light by table, mixed premultiplied, and encoded back to the nearest sRGB
  byte by binary search over 255 exact thresholds
  (`tools/linear_light_tables.py`, rational arithmetic, no libm). A kernel
  that draws a gradient itself calls `interpolate_linear_light`; one that
  hands stops to a renderer interpolating in sRGB hands it
  `expand_linear_light_stops`'s stops. Moving a gradient from Skia to our
  engine keeps its look within the expansion's 1.3 steps. Gradient pixels
  moved once with this change.
- Hosts present our bytes. Colour management for the display is the host's
  business and outside the pixel contract; tests hash bytes, not screens.

## 4. What the study's counters say about the rule

The counters (`gui_forms/diagnostics/blend_study`) measure how much of each
frame the copy branch of the rule would take: `copied_pixels` against
`blended_pixels`, the overdraw that copies hide, opaque fills issued
source-over (which a kernel following the rule turns into copies), and
opaque images drawn blended (which the decode flag removes). The numbers
come from `tools/blend_study.py` on the M4; this note is updated with them
once the integrator has run it.

## 5. Owner decisions (2026-11-16)

1. Linear light: "im okay with linear." Every gradient interpolates in
   linear light (section 3); gradient pixels moved once.
2. Contraction: "turn it off for referencing, and leave it on for actual
   deployment." `GUI_FORMS_REFERENCE_ARITHMETIC` (default OFF) compiles the
   reference and conformance builds without contraction; deployment builds
   contract (section 2, item 2; CMakeLists.txt, "Floating-point
   contraction").

Follow-ups (coordinator, 2026-11-16):

3. `duotone()` lowers as the luminance matrix and a two-colour table
   (`MaterialColourTable::duotone`), as `tritone()` does, so it mixes in
   linear light too. The `MaterialColourMatrix::duotone` matrix remains for
   C++ callers, as a straight line on sRGB-encoded values.
4. The value paths stay unfused in every build, accepted: `engine_math`
   (its stated error bounds) and `engine_expression`, `theme_value`,
   `value_text`, `scrub_gesture`, `layout_style_reader`,
   `theme_motion_lowering` and `motion_value`, which produce numbers
   (parsed text, theme values, layout lengths, motion samples), not
   pixels, so a deployment build computes the same values as a reference
   one.
5. Shipped programs are deployment builds: the release programs job
   configures `GUI_FORMS_REFERENCE_ARITHMETIC=OFF` and runs every test but
   those labelled `reference_arithmetic` (pinned pixel hashes, cross-host
   comparisons), which the native, sanitizer and engine-gate jobs run in
   reference builds.
6. The colour picker is exempt from linear light: a picker must show the
   exact colour under the pointer, so its saturation/value field and hue
   strip are drawn per pixel from HSV, not as gradients.
