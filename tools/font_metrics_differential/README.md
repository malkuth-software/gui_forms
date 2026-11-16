# Font metrics differential (stage F)

This tool measures a reference face and a candidate substitute, and compares
them under the tolerance of the mapping's class. It is the measurement behind
stage F's gate ([`planning/stages/F-font-substitution/README.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/stages/F-font-substitution/README.md), owner
decisions 8 and 9 of 2026-10-23).

The reference is always our own engine. Both faces go through the same
FreeType and HarfBuzz code that GUI.Forms renders with, and no host renderer
is ever the reference. The optional GDI measurement is information for stages
R and S, which compare against Windows Forms. It is never part of F's
gate.

```text
gui_forms_font_metrics_differential --reference <face.ttf> --candidate <face.ttf>
    --class metric_compatible|nearest --corpus <corpus.txt> --out <report.json> [--gdi]
```

The exit status is 0 when the mapping's gate passes and 1 when it does not.
It is 2 when an input is rejected or the measurement fails, and then no report
is written. The tool is built when FreeType is (a build with Skia or the
Win32 host): it reads the faces' name, OS/2 and hhea tables with FreeType,
which STXlib's font reader does not yet give. The executable sits at the build tree's
root as `gui_forms_font_metrics_differential`.

## What it measures

For each face:

- **Facts from FreeType**, in font units: family, version, SHA-256, units per
  em, weight class, the `hhea`, `OS/2` typo and win vertical metrics, and the
  advance of every code point in U+0020..007E and U+00A0..017F.
- **The bundled engine's measurements** at 11, 12, 13, 16, 22 and 24 px (8.25,
  9, 10 and 12 pt at 96 dpi, and 11 and 12 px at scale 2). The engine is the
  one GUI.Forms shapes with: HarfBuzz over FreeType, unhinted advances
  (ADR-004). It records the line metrics (ascent, descent, gap), the width of
  every corpus string, and the greedy line breaks of every corpus paragraph at
  20, 35 and 60 ems.
- **GDI's measurements**, with `--gdi` on Windows, as information for R and
  S: `tmAscent`, `tmDescent`, `tmHeight`, the leadings and
  `GetTextExtentPoint32W` widths, at the same sizes. The face's own bytes are
  added privately (`AddFontMemResourceEx`), so a candidate need not be
  installed. `GetFontData` then proves that GDI measured exactly the file
  (`bytes_match_file`).

## The criteria (owner decision 8)

| Criterion | `metric_compatible` | `nearest` |
|---|---|---|
| Advances | identical in ems (integer cross-multiplication) for every code point both faces map | recorded |
| Line breaks | identical for every paragraph, width and size | recorded |
| Line height | the candidate's engine line height (ascent + descent + gap) within 1 px of the reference's engine line height at every size | recorded |
| Line width | recorded | the mean corpus string-width deviation within 5% at every size |

The gate passes when no gated criterion is `failed` or `not_measured`.

The report also records information that is not gated, from GDI:

- GDI's width deviation between the two faces.
- Each face's engine-versus-GDI width deviation, which shows how far unhinted
  advances sit from GDI's hinted ones.
- GDI's heights for both faces.

## The engine's line metrics

The engine takes its vertical metrics from FreeType 2.14.2, pinned and
unmodified apart from its options header. That rule is:

- the OS/2 typo metrics when the face sets `USE_TYPO_METRICS` (bit 7 of
  `fsSelection`);
- otherwise `hhea`;
- the typo metrics, then the win metrics, only when `hhea`'s ascender and
  descender are both zero.

At a size, FreeType rounds the ascender up, the descender down and the height
to the nearest pixel (`GRID_FIT_METRICS`). The line gap is that height less
the ascender and descender, never below zero.

## Snapshots and the cross-host gate (stage F decision 18)

```text
gui_forms_font_metrics_differential --reference <face.ttf> --candidate <face.ttf>
    --corpus <corpus.txt> --request-family <name>
    --request-style regular|bold|italic|bold_italic --snapshot <snapshot.txt>
```

This writes the reference's engine measurement as a text snapshot
(`metric_snapshot.cpp`, schema `gui_forms.font_metrics_snapshot.v1`). The
snapshot records the table request it stands for, the corpus's content
digest, and the candidate's measurement digest. It holds measurements only:
advances in font units, then each size's line metrics, string widths and line
starts. The tool reads the snapshot back before writing it. The committed
snapshots are in `reference_snapshots/`, one per gated request.

`tests/font_metrics_gate_tests.cpp` (`gui_forms_font_metrics_gate_tests`)
is the gate, and it runs on every host. It measures each bundled candidate
that the substitution table names, and requires the candidate's digest to
equal the recorded one. It then compares the candidate with the snapshot
under the class's criteria and the table's named exceptions. A named
exception must still fail its criterion and stay within its limit. On
Windows, an installed reference whose SHA-256 matches its snapshot is
measured again and must reproduce the snapshot. To re-measure after a
change to the table, the engine or the corpus, rerun the snapshot command for
each row of [`results/2026-10-23-f-font-substitution`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-23-f-font-substitution). Review the differences
before committing them.

## Rules

- Reference fonts are oracles. They are read by path (for example from
  `C:\Windows\Fonts`), and only their file name, version, size and SHA-256
  reach the report. Never commit a Microsoft font file.
- The corpus (`corpus/forms_latin_v1.txt`) is versioned. Changing a line
  changes every recorded result, so add a new version instead of editing one.
- Inputs are validated whole before measurement starts. That covers the
  corpus encoding and sections, and the sfnt table directory with the tables
  the measurement reads. Collections (`ttcf`) are refused.

`tests/font_metrics_differential_tests.cpp` covers the kernel with bundled
faces only, so CI never needs a Microsoft font.
