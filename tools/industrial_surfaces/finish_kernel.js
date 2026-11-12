'use strict';
// Stateless surface finishes and layered industrial paint.
//
// Every device pixel is a pure function of its absolute surface position, the
// display scale (device pixels per logical unit) and the finish's parameters.
// Nothing is baked: no tile, no texture, no mip chain. Repainting any damage
// rectangle reproduces exactly the bytes a full repaint would, so the finish is
// written straight into the framebuffer and costs parameter memory only. The
// one scratch buffer is two rows of heights (the row being shaded and the one
// below it, for the vertical slope), sized to the damage width.
//
// This file is the reference for cpp/industrial_surfaces.cpp; keep them in step.
//
// GUI.Forms copy (2026-11-12): vendored from the notes repository's
// gui_forms branch, experiments/industrial-surfaces/finish_kernel.js at
// c9bef599e1fef10ca676cb456d108bfcfbddd8d3, as the byte reference for the
// engine's finish kernel (src/core/engine/primitives/finish/). Two changes,
// both marked "GUI.Forms": the cloth_weave finish, whose second colour is
// the weft's and whose albedo shades both threads (`weft`), and the `grain`
// option, which multiplies every feature size and the relief with it
// (default 1, which leaves the original finishes' bytes unchanged).

const Finish = (() => {

  // ---- integer hash and noise --------------------------------------------

  // 32-bit integer hash of a lattice point and a stream id, in [0, 1).
  function hash(x, y, s) {
    let h = Math.imul(x | 0, 0x27d4eb2d) ^ Math.imul(y | 0, 0x165667b1) ^ Math.imul(s | 0, 0x9e3779b1);
    h = Math.imul(h ^ (h >>> 15), 0x85ebca6b);
    h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35);
    return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
  }

  // Value noise with smoothstep interpolation; `cell` is in device pixels and is
  // never smaller than one, so no octave aliases at any scale.
  function noise(x, y, cell, s) {
    const c = cell < 1 ? 1 : cell;
    const fx0 = x / c, fy0 = y / c;
    const ix = Math.floor(fx0), iy = Math.floor(fy0);
    const fx = fx0 - ix, fy = fy0 - iy;
    const ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    const a = hash(ix, iy, s), b = hash(ix + 1, iy, s), cc = hash(ix, iy + 1, s), d = hash(ix + 1, iy + 1, s);
    return a + (b - a) * ux + (cc - a) * uy + (a - b - cc + d) * ux * uy;
  }

  // Jittered-cell (Worley) distances in cell units: nearest, second nearest and
  // the nearest cell's identity hash. Written to module state, not returned, so
  // the per-pixel path allocates nothing.
  let w1 = 0, w2 = 0, wid = 0;
  function cells(x, y, cell, s) {
    const c = cell < 1 ? 1 : cell;
    const gx = x / c, gy = y / c;
    const ix = Math.floor(gx), iy = Math.floor(gy);
    let d1 = 9, d2 = 9, id = 0;
    for (let j = -1; j <= 1; j++) {
      for (let i = -1; i <= 1; i++) {
        const cx = ix + i, cy = iy + j;
        const dx = gx - (cx + 0.15 + 0.7 * hash(cx, cy, s));
        const dy = gy - (cy + 0.15 + 0.7 * hash(cx, cy, s + 1));
        const d = dx * dx + dy * dy;
        if (d < d1) { d2 = d1; d1 = d; id = hash(cx, cy, s + 2); }
        else if (d < d2) { d2 = d; }
      }
    }
    w1 = Math.sqrt(d1); w2 = Math.sqrt(d2); wid = id;
  }

  // GUI.Forms: a whole-number power by repeated squaring, in this order, in
  // place of Math.pow, so that the engine's kernel computes it alike with
  // IEEE multiplications alone. The finishes' shininess is a whole number.
  function power(base, exponent) {
    let result = 1, square = base, e = exponent;
    while (e > 0) {
      if (e & 1) result *= square;
      square *= square;
      e >>= 1;
    }
    return result;
  }

  function smooth(a, b, x) {
    let t = (x - a) / (b - a);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
  }

  // ---- finishes ------------------------------------------------------------
  //
  // A finish supplies a height field (in logical units) and an albedo
  // modulation. Its shading constants are chosen once, before the loop:
  //   relief     slope gain of the height field
  //   gloss      specular weight; shininess its exponent
  //   metallic   0 = dielectric (white sheen), 1 = sheen takes the base colour
  //   dust       how far fine specks pull towards the dust colour
  // `k` is device pixels per logical unit.

  let fh = 0;   // height
  let fa = 1;   // albedo multiplier
  let fd = 0;   // dust mix
  let fs = 1;   // specular multiplier (flakes)

  const FINISHES = {
    dusty_matte: {
      label: 'Dusty matte', note: 'Flat enamel under a fine film of settled dust; soft mottling, pale specks.',
      relief: 0.35, gloss: 0.015, shininess: 6, metallic: 0,
      eval(x, y, k, s) {
        fh = 0.6 * noise(x, y, 3.0 * k, s) + 0.4 * noise(x, y, 1.2 * k, s + 1);
        fa = 1 + (noise(x, y, 48 * k, s + 2) - 0.5) * 0.05;
        const speck = hash(Math.floor(x / Math.max(1, k * 0.8)), Math.floor(y / Math.max(1, k * 0.8)), s + 3);
        const haze = noise(x, y, 90 * k, s + 4);
        fd = 0.10 * haze + (speck > 0.985 ? 0.35 * (speck - 0.985) / 0.015 : 0);
        fs = 1;
      },
    },
    sand_matte: {
      // Grit blasting leaves overlapping impact pits whose rims meet in sharp
      // ridges (the anchor profile). Primer and topcoat level it but do not
      // hide it: the tooth survives as a dense, even micro-relief that shows
      // mostly in the sheen, with almost no change of colour. Pits, not domes.
      label: 'Grit-blasted under paint (superseded: see blast lab)', note: 'Anchor profile of blasted steel showing through a satin coat: fine pits and ridges, read in the sheen.',
      relief: 0.9, gloss: 0.14, shininess: 18, metallic: 0,
      eval(x, y, k, s) {
        // coarse grit impacts
        // A pit must span a few device pixels to show a lit rim and a
        // shadowed cup; anything finer only reads as pixel noise.
        cells(x, y, 4.2 * k, s);
        const pitA = 1 - smooth(0.0, 0.8, w1);
        const ridgeA = 1 - smooth(0.0, 0.10, w2 - w1);
        const sizeA = 0.6 + 0.4 * wid;
        // fine grit impacts overlapping them
        cells(x, y, 2.3 * k, s + 11);
        const pitB = 1 - smooth(0.0, 0.8, w1);
        const ridgeB = 1 - smooth(0.0, 0.12, w2 - w1);
        // The film levels the finest relief more than the coarse.
        const film = 0.35 * noise(x, y, 16 * k, s + 12);
        fh = -1.6 * pitA * pitA * sizeA + 0.35 * ridgeA
           - (0.7 * pitB * pitB - 0.15 * ridgeB) * (1 - film);
        // Colour barely moves: thinner film over ridges reads a hair lighter.
        fa = 1 + 0.025 * ridgeA - 0.02 * pitA + (noise(x, y, 70 * k, s + 13) - 0.5) * 0.02;
        fd = 0;
        fs = 0.75 + 0.5 * ridgeA;
      },
    },
    orange_peel: {
      label: 'Powder coat (orange peel)', note: 'Satin powder coat that flowed and cured with a soft peel.',
      relief: 0.85, gloss: 0.22, shininess: 38, metallic: 0,
      eval(x, y, k, s) {
        fh = 0.7 * noise(x, y, 5.5 * k, s) + 0.3 * noise(x, y, 2.6 * k, s + 1);
        fa = 1 + (noise(x, y, 60 * k, s + 2) - 0.5) * 0.025;
        fd = 0; fs = 1;
      },
    },
    hammertone: {
      label: 'Hammertone', note: 'Hammered-metal enamel: shallow bowls, raised rims, aluminium flake.',
      relief: 2.1, gloss: 0.32, shininess: 22, metallic: 0.65,
      eval(x, y, k, s) {
        // Two sizes of soft dimple, the larger dominant, then fine flake.
        cells(x, y, 8.5 * k, s);
        const big = smooth(0.0, 0.95, w1);
        const tone = wid;
        cells(x, y, 4.1 * k, s + 9);
        const small = smooth(0.0, 0.95, w1);
        fh = 0.7 * big * big + 0.3 * small * small + 0.06 * noise(x, y, 1.5 * k, s + 5);
        fa = 1 + (tone - 0.5) * 0.05 + (noise(x, y, 40 * k, s + 4) - 0.5) * 0.06;
        const flake = hash(Math.floor(x / Math.max(1, k)), Math.floor(y / Math.max(1, k)), s + 6);
        fs = flake > 0.93 ? 2.6 : 0.8;
        fd = 0;
      },
    },
    wrinkle: {
      label: 'Wrinkle (crinkle) finish', note: 'Skinned-over enamel that shrank into fine ridges as it cured.',
      relief: 1.7, gloss: 0.10, shininess: 14, metallic: 0,
      eval(x, y, k, s) {
        const wx = x + (noise(x, y, 7 * k, s + 7) - 0.5) * 6 * k;
        const wy = y + (noise(x, y, 7 * k, s + 8) - 0.5) * 6 * k;
        const r1 = 1 - Math.abs(2 * noise(wx, wy, 2.3 * k, s) - 1);
        const r2 = 1 - Math.abs(2 * noise(wx, wy, 1.1 * k, s + 1) - 1);
        fh = 0.75 * r1 * r1 + 0.25 * r2;
        fa = 1 - (1 - r1) * 0.05;
        fd = 0; fs = 1;
      },
    },
    bead_blast: {
      label: 'Bead-blasted steel', note: 'Bare metal peened with glass beads: an even, very fine scatter.',
      ownColour: '#8b9194', relief: 0.65, gloss: 0.45, shininess: 7, metallic: 1,
      eval(x, y, k, s) {
        fh = 0.5 * noise(x, y, 0.9 * k, s) + 0.5 * noise(x, y, 1.7 * k, s + 1);
        fa = 1 + (noise(x, y, 30 * k, s + 2) - 0.5) * 0.04;
        fd = 0; fs = 1;
      },
    },
    sand_cast: {
      label: 'Sand-cast iron, painted', note: 'Casting skin under paint: coarse grain with scattered pits.',
      relief: 1.35, gloss: 0.05, shininess: 9, metallic: 0,
      eval(x, y, k, s) {
        cells(x, y, 3.2 * k, s);
        const pit = wid > 0.72 ? 1 - smooth(0.0, 0.32, w1) : 0;
        fh = 0.55 * noise(x, y, 1.1 * k, s + 1) + 0.45 * noise(x, y, 2.8 * k, s + 2) - 0.9 * pit;
        fa = 1 - pit * 0.18 + (noise(x, y, 25 * k, s + 3) - 0.5) * 0.05;
        fd = pit * 0.15; fs = 1;
      },
    },
    // GUI.Forms: a plain-woven cloth. Threads 3 logical units apart; the
    // thread over each cell alternates between the warp (vertical) and the
    // weft (horizontal) with the parity of the cell's column plus row, as
    // in a plain weave. A thread's section is rounded, it dips where it
    // passes under the crossing threads at the cell's ends, and its
    // thickness drifts along it (slub) with a 7-unit noise of its own.
    // The second colour is the weft's.
    cloth_weave: {
      label: 'Cloth (plain weave)', note: 'Woven cloth: warp and weft over and under, with slubbed yarn.',
      relief: 1.1, gloss: 0.03, shininess: 8, metallic: 0, weft: true,
      eval(x, y, k, s) {
        const c = 3 * k < 1 ? 1 : 3 * k;
        const gx = x / c, gy = y / c;
        const ix = Math.floor(gx), iy = Math.floor(gy);
        const fx = gx - ix, fy = gy - iy;
        const weftOver = ((ix + iy) & 1) !== 0;
        const across = weftOver ? fy : fx;
        const along = weftOver ? fx : fy;
        const thread = weftOver ? iy : ix;
        const slub = weftOver ? noise(x, thread * 31 * c, 7 * k, s + 21) : noise(thread * 31 * c, y, 7 * k, s + 20);
        const profile = 4 * across * (1 - across);
        const dip = 2 * along - 1;
        fh = profile * (0.8 + 0.4 * slub) * (1 - 0.35 * dip * dip);
        fa = (0.82 + 0.18 * profile) * (1 + (hash(thread, weftOver ? 1 : 0, s + 22) - 0.5) * 0.08) * (0.94 + 0.12 * slub);
        fd = weftOver ? 1 : 0;
        fs = 1;
      },
    },
    flat_baseline: {
      label: 'Flat fill (reference)', note: 'The same colour as a plain solid fill, for comparison.',
      relief: 0, gloss: 0, shininess: 1, metallic: 0,
      eval() { fh = 0; fa = 1; fd = 0; fs = 1; },
    },
  };

  // ---- colour --------------------------------------------------------------

  const TO_LINEAR = new Float32Array(256);
  for (let i = 0; i < 256; i++) {
    const c = i / 255;
    TO_LINEAR[i] = c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
  }
  // Linear [0, 1] to sRGB byte through a 4096-entry table.
  const TO_SRGB = new Uint8Array(4097);
  for (let i = 0; i <= 4096; i++) {
    const c = i / 4096;
    const v = c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055;
    TO_SRGB[i] = Math.round(Math.min(1, Math.max(0, v)) * 255);
  }
  function encode(v) {
    const i = (v <= 0 ? 0 : v >= 1 ? 1 : v) * 4096;
    return TO_SRGB[i | 0];
  }

  function hexToRgb(hex) {
    const n = parseInt(hex.replace('#', ''), 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
  }
  function linearOf(hex) {
    const c = hexToRgb(hex);
    return [TO_LINEAR[c[0]], TO_LINEAR[c[1]], TO_LINEAR[c[2]]];
  }

  // Oklab (Björn Ottosson, 2020) from and to linear sRGB.
  function linearToOklab(r, g, b, out) {
    const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    out[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    out[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    out[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
  }
  function oklabToLinear(L, a, b, out) {
    const l = L + 0.3963377774 * a + 0.2158037573 * b;
    const m = L - 0.1055613458 * a - 0.0638541728 * b;
    const s = L - 0.0894841775 * a - 1.2914855480 * b;
    const l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
    out[0] = 4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3;
    out[1] = -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3;
    out[2] = -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3;
  }
  function hexToOklab(hex) {
    const c = linearOf(hex);
    const o = [0, 0, 0];
    linearToOklab(c[0], c[1], c[2], o);
    return o;
  }
  function oklabToHex(L, a, b) {
    const o = [0, 0, 0];
    oklabToLinear(L, a, b, o);
    let s = '#';
    for (let i = 0; i < 3; i++) s += encode(o[i]).toString(16).padStart(2, '0');
    return s;
  }

  // ---- light ---------------------------------------------------------------

  function lightOf(azimuthDeg, elevationDeg) {
    const az = azimuthDeg * Math.PI / 180, el = elevationDeg * Math.PI / 180;
    const L = [Math.cos(el) * Math.cos(az), -Math.cos(el) * Math.sin(az), Math.sin(el)];
    const H = [L[0], L[1], L[2] + 1];
    // GUI.Forms: the length as the square root of the sum of squares (an
    // IEEE operation every host rounds alike), not Math.hypot.
    const hn = Math.sqrt(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
    return { L, H: [H[0] / hn, H[1] / hn, H[2] / hn] };
  }

  // ---- painting ------------------------------------------------------------
  //
  // paint(fb, target, rect, damage, options)
  //   fb       ImageData-like {data: Uint8ClampedArray RGBA, width}
  //   rect     the surface's own box in device pixels; noise is anchored to
  //            its origin plus the seed, so the surface carries its finish
  //            when it moves and any damage repaint is seam-free
  //   damage   the device rectangle to write (clipped to rect)
  //   options  { finish, k, seed, colour (hex), light, ambient, paint? }
  //
  // `paint` (optional) layers industrial wear over the base colour; see
  // paintLayers below. Returns the count of pixels written.

  // A row of the finish: height, albedo multiplier, dust mix and specular
  // multiplier for each pixel of the damage span plus one to the right.
  function makeRow(n) {
    return { h: new Float32Array(n), a: new Float32Array(n), d: new Float32Array(n), s: new Float32Array(n) };
  }
  let scratchTop = makeRow(0), scratchBottom = makeRow(0);

  function reserve(width) {
    if (scratchTop.h.length < width + 1) {
      scratchTop = makeRow(width + 1);
      scratchBottom = makeRow(width + 1);
    }
  }

  function heightRow(finish, row, y, x0, count, ox, oy, k, seed, paintState) {
    for (let i = 0; i <= count; i++) {
      finish.eval(x0 + i - ox, y - oy, k, seed);
      let v = fh;
      if (paintState) v += paintHeight(paintState, x0 + i - ox, y - oy);
      row.h[i] = v; row.a[i] = fa; row.d[i] = fd; row.s[i] = fs;
    }
  }

  const lab = [0, 0, 0], lin = [0, 0, 0];

  function paint(fb, rect, damage, options) {
    const finish = FINISHES[options.finish];
    // GUI.Forms: `grain` multiplies every feature size (and with it the relief).
    const grain = options.grain ?? 1;
    const k = options.k * grain, seed = options.seed | 0;
    const x0 = Math.max(rect.x, damage.x), y0 = Math.max(rect.y, damage.y);
    const x1 = Math.min(rect.x + rect.w, damage.x + damage.w), y1 = Math.min(rect.y + rect.h, damage.y + damage.h);
    if (x1 <= x0 || y1 <= y0) return 0;
    const count = x1 - x0;
    reserve(count);

    // Chosen once, before the loop.
    const base = linearOf(finish.ownColour || options.colour);
    const dust = linearOf(options.dustColour || '#bdb6a8');
    const { L, H } = options.light;
    const ambient = options.ambient;
    const kd = 1 - ambient;
    const relief = finish.relief * k * (options.reliefScale ?? 1);
    const flatDiffuse = ambient + kd * L[2];
    const gloss = finish.gloss * (options.glossScale ?? 1);
    const shin = finish.shininess;
    const flatSpec = gloss * power(H[2], shin);
    const metal = finish.metallic;
    const paintState = options.paint ? preparePaint(options.paint, rect, k, seed) : null;
    const ox = rect.x, oy = rect.y;

    let top = scratchTop, bottom = scratchBottom;
    heightRow(finish, top, y0, x0, count, ox, oy, k, seed, paintState);
    const data = fb.data, stride = fb.width * 4;

    for (let y = y0; y < y1; y++) {
      heightRow(finish, bottom, y + 1, x0, count, ox, oy, k, seed, paintState);
      const al = top;
      let p = y * stride + x0 * 4;
      for (let i = 0; i < count; i++, p += 4) {
        const ht = top.h[i];
        const gx = (top.h[i + 1] - ht) * relief;
        const gy = (bottom.h[i] - ht) * relief;
        const inv = 1 / Math.sqrt(gx * gx + gy * gy + 1);
        const nx = -gx * inv, ny = -gy * inv, nz = inv;
        const ndl = nx * L[0] + ny * L[1] + nz * L[2];
        const diffuse = (ambient + kd * (ndl > 0 ? ndl : 0)) / flatDiffuse;
        const ndh = nx * H[0] + ny * H[1] + nz * H[2];
        const spec = (gloss * (ndh > 0 ? power(ndh, shin) : 0) - flatSpec * 0.6) * al.s[i];

        let r, g, b, m = metal;
        if (paintState) {
          paintColour(paintState, x0 + i - ox, y - oy);
          r = pc[0]; g = pc[1]; b = pc[2];
          m = Math.max(m, pcMetal);
        } else {
          r = base[0]; g = base[1]; b = base[2];
        }
        const a = al.a[i];
        const d = al.d[i];
        r = (r * a) * (1 - d) + dust[0] * d;
        g = (g * a) * (1 - d) + dust[1] * d;
        b = (b * a) * (1 - d) + dust[2] * d;
        if (finish.weft) {
          // GUI.Forms: the cloth's second colour is the weft's, shaded by the albedo.
          r = (base[0] * (1 - d) + dust[0] * d) * a;
          g = (base[1] * (1 - d) + dust[1] * d) * a;
          b = (base[2] * (1 - d) + dust[2] * d) * a;
        }
        const sr = spec * (1 - m + m * r * 2.2), sg = spec * (1 - m + m * g * 2.2), sb = spec * (1 - m + m * b * 2.2);
        data[p] = encode(r * diffuse + sr);
        data[p + 1] = encode(g * diffuse + sg);
        data[p + 2] = encode(b * diffuse + sb);
        data[p + 3] = 255;
      }
      const t = top; top = bottom; bottom = t;
    }
    return count * (y1 - y0);
  }

  // ---- industrial paint layers -------------------------------------------
  //
  // The colour of a painted machine surface is not one colour. In order of
  // scale, from broad to fine:
  //   batch      very low-frequency hue/chroma drift: touch-ups and later coats
  //              from a slightly different batch (±hue, ±chroma)
  //   film       film-thickness variation: thicker paint reads a touch darker
  //              and more saturated
  //   fade       UV chalking: lighter, greyer towards the exposed top
  //   runs       sags where a heavy coat ran: a darker vertical tongue with a
  //              raised bead at its foot
  //   grime      oil and dirt settling into edges, seams and round fasteners,
  //              warm and dark
  //   wear       chipped edges: paint gone to red-oxide primer, then to steel
  // Each is a strength in [0, 1]; all are evaluated per pixel from position.

  const pc = [0, 0, 0];
  let pcMetal = 0;

  function preparePaint(p, rect, k, seed) {
    const baseLab = hexToOklab(p.colour);
    return {
      p, k, seed, w: rect.w, h: rect.h,
      L: baseLab[0], A: baseLab[1], B: baseLab[2],
      primer: linearOf(p.primer || '#7b3a26'),
      steel: linearOf('#8d9193'),
      grimeColour: hexToOklab('#3a3027'),
      features: p.features || [],
    };
  }

  // Distance in device pixels from a point to the nearest feature: the
  // surface edge, a fastener's rim and, when `seams` is set, a seam line.
  // Grime collects at all three; wear happens on exposed edges and fasteners
  // only, since a seam is a recess.
  function featureDistance(st, x, y, seams) {
    let d = Math.min(x, y, st.w - 1 - x, st.h - 1 - y);
    const f = st.features;
    for (let i = 0; i < f.length; i++) {
      const e = f[i];
      if (e.kind === 'seam_h') { if (seams) d = Math.min(d, Math.abs(y - e.y * st.h)); }
      else if (e.kind === 'seam_v') { if (seams) d = Math.min(d, Math.abs(x - e.x * st.w)); }
      else if (e.kind === 'bolt') {
        const r = e.r * st.k;
        d = Math.min(d, Math.abs(Math.hypot(x - e.x * st.w, y - e.y * st.h) - r));
      }
    }
    return d;
  }

  function insideBolt(st, x, y) {
    const f = st.features;
    for (let i = 0; i < f.length; i++) {
      const e = f[i];
      if (e.kind === 'bolt') {
        const r = e.r * st.k;
        const dx = x - e.x * st.w, dy = y - e.y * st.h;
        const q = Math.hypot(dx, dy) / r;
        if (q < 1) return Math.sqrt(1 - q * q) * r * 0.5;
      }
    }
    return 0;
  }

  function runAt(st, x, y) {
    // One candidate run per 26-unit column; its start depth, length and width
    // are hashed from the column.
    const p = st.p, k = st.k;
    if (p.runs <= 0) return 0;
    const col = Math.floor(x / (26 * k));
    const hs = hash(col, 7, st.seed + 40);
    if (hs > p.runs * 0.55) return 0;
    const cx = (col + 0.25 + 0.5 * hash(col, 8, st.seed + 40)) * 26 * k;
    const y0 = hash(col, 9, st.seed + 40) * st.h * 0.5;
    const len = (14 + 50 * hash(col, 10, st.seed + 40)) * k;
    const t = (y - y0) / len;
    if (t < 0 || t > 1.06) return 0;
    const half = (1.6 + 2.2 * Math.sqrt(Math.min(t, 1))) * k;
    const dx = Math.abs(x - cx);
    if (t > 1) {
      // the bead at the foot
      const bead = Math.hypot(dx, (t - 1) * len) / (half * 1.15);
      return bead < 1 ? 1 - bead * bead : 0;
    }
    return dx < half ? (1 - (dx / half) * (dx / half)) * (0.35 + 0.65 * t) : 0;
  }

  function seamDistance(st, x, y) {
    let d = 1e9;
    const f = st.features;
    for (let i = 0; i < f.length; i++) {
      const e = f[i];
      if (e.kind === 'seam_h') d = Math.min(d, Math.abs(y - e.y * st.h));
      else if (e.kind === 'seam_v') d = Math.min(d, Math.abs(x - e.x * st.w));
    }
    return d;
  }

  function paintHeight(st, x, y) {
    const p = st.p;
    let h = 0;
    // a seam is a groove about a unit wide
    const sd = seamDistance(st, x, y) / st.k;
    if (sd < 2.5) h -= 1.4 * Math.exp(-sd * sd / 0.8);
    h += runAt(st, x, y) * 0.9 * p.runs;
    h += insideBolt(st, x, y) / st.k;
    const wear = wearAt(st, x, y);
    if (wear > 0) h -= wear * 0.6;
    return h;
  }

  function wearAt(st, x, y) {
    const p = st.p;
    if (p.wear <= 0) return 0;
    const d = featureDistance(st, x, y, false) / st.k;
    const n = noise(x, y, 5 * st.k, st.seed + 50) * 0.7 + noise(x, y, 1.6 * st.k, st.seed + 51) * 0.3;
    const reach = p.wear * 9;
    const v = (reach - d) / Math.max(reach, 0.001) + (n - 0.62) * 1.6;
    return v > 0 ? Math.min(1, v * 2.5) : 0;
  }

  function paintColour(st, x, y) {
    const p = st.p, k = st.k, s = st.seed;
    let L = st.L, A = st.A, B = st.B;
    // batch: rotate hue by up to ±2.5° and scale chroma by ±6 %
    const batch = noise(x, y, 220 * k, s + 30) - 0.5;
    const ang = batch * p.batch * 0.087;
    const ca = Math.cos(ang), sa = Math.sin(ang);
    const scale = 1 + batch * p.batch * 0.12;
    let a2 = (A * ca - B * sa) * scale, b2 = (A * sa + B * ca) * scale;
    // film: thicker reads darker and slightly richer
    const film = (noise(x, y, 38 * k, s + 31) - 0.5) * 0.7 + (noise(x, y, 11 * k, s + 32) - 0.5) * 0.3;
    L -= film * p.film * 0.045;
    a2 *= 1 + film * p.film * 0.10; b2 *= 1 + film * p.film * 0.10;
    // runs: the sag is thicker paint
    const run = runAt(st, x, y);
    L -= run * p.runs * 0.035;
    // fade: towards the exposed top, lighter and greyer
    const exposure = Math.pow(1 - y / st.h, 1.6) * (0.75 + 0.25 * noise(x, y, 70 * k, s + 33));
    L += exposure * p.fade * 0.07;
    a2 *= 1 - exposure * p.fade * 0.35; b2 *= 1 - exposure * p.fade * 0.35;
    // grime in edges, seams, round fasteners, and settling towards the bottom
    const fd = featureDistance(st, x, y, true) / k;
    const settle = Math.pow(y / st.h, 3) * 0.35;
    let g = (Math.exp(-fd / 5) + settle) * (0.55 + 0.45 * noise(x, y, 9 * k, s + 34)) * p.grime;
    g = Math.min(0.85, g);
    L = L + (st.grimeColour[0] - L) * g * 0.8;
    a2 = a2 + (st.grimeColour[1] - a2) * g * 0.6;
    b2 = b2 + (st.grimeColour[2] - b2) * g * 0.6;
    oklabToLinear(L, a2, b2, lin);
    pc[0] = lin[0]; pc[1] = lin[1]; pc[2] = lin[2];
    pcMetal = 0;
    // wear: primer first, then bare steel at the heart of the chip
    const wear = wearAt(st, x, y);
    if (wear > 0) {
      const toPrimer = smooth(0.0, 0.25, wear);
      const toSteel = smooth(0.55, 0.85, wear);
      for (let i = 0; i < 3; i++) {
        const prim = st.primer[i] * (0.9 + 0.2 * noise(x, y, 2 * k, s + 52));
        pc[i] = pc[i] + (prim - pc[i]) * toPrimer;
        pc[i] = pc[i] + (st.steel[i] * (0.85 + 0.3 * noise(x, y * 0.25, 2 * k, s + 53)) - pc[i]) * toSteel;
      }
      pcMetal = toSteel;
    }
  }

  return {
    FINISHES, paint, lightOf, hash, noise,
    hexToOklab, oklabToHex, linearOf, linearToOklab, oklabToLinear, encode, TO_LINEAR,
  };
})();
