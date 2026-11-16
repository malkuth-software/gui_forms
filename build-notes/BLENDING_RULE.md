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
| Gradients (Skia) | interpolated premultiplied, in sRGB-encoded space (`fInPremul = kYes`, `ColorSpace::kSRGB`) | no |
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
   results differ between architectures. Our own float code admits the same
   difference wherever the compiler contracts (`-ffp-contract` is not
   pinned in our flags). Before 1.0 these differences are logged, not
   fixed (owner direction on FMA determinism).
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
> blend would. Colour is computed (lighting, gradients' stops, colour
> profiles) in linear light where a stage says so, and stored back as sRGB
> bytes before it is composited. Our engine's output is the reference;
> Skia's, a host's and a GPU's are compared against it, never the other
> way round. Floating-point differences between architectures (fused
> multiply-add, SIMD width) are logged against the reference before 1.0,
> not fixed.**

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
- Gradients interpolate premultiplied in sRGB-encoded space (as Skia is
  configured today), so moving a gradient from Skia to our engine keeps its
  look; interpolation in linear light would be a visible change and an
  owner decision.
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

## 5. Open points for the owner

1. Whether gradients should ever interpolate in linear light (a visible
   change; today they interpolate in sRGB-encoded space).
2. Whether to pin `-ffp-contract=off` for our own float code now, ahead of
   the post-1.0 FMA work, so only Skia's differences remain to be logged.
