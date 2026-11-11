# Performance focus triage, 2026-11-11

Triage rows for the focus studies in `PERFORMANCE_FOCUS_STUDIES_2026-11-11.md`.
Kept apart from the 2026-11-10 audit addendum and its triage, which are
unchanged by this work. Audit only: every row is an option for the owner.

Status words as in the audit triage (confirmed, narrowed, withdrawn,
unproven). Size: S small, C contained, B broad.

## Focus studies, measured (main at 66a7f2e5b)

Full text, tables and method: `PERFORMANCE_FOCUS_STUDIES_2026-11-11.md`. Lab
code: `D:\gui_forms_perf_lab\lab`. "Measured" means the project's own source
was the baseline and the output was compared. Ratios were taken under heavy
machine load, same process, best of several repeats.

| Id | Status | Recommendation | Severity | Size | Guard |
|---|---|---|---|---|---|
| FS1 | confirmed, measured: 2.5 to 14 times, same bytes on 12,000 blits | Work the compositor's interior in fixed blocks with flat byte passes; record "wholly opaque" on a raster at bake. Supersedes S1, whose "divisions" claim was wrong | high | C | byte-for-byte compositor test, identity hashes |
| FS2a | confirmed, measured: 129 to 170 times, same bytes | The raster writes heat-map cells as rows of words when they are opaque and on whole pixels; otherwise as now | high for plots | S | pixel comparison against `fill_rect` per cell |
| FS2b | confirmed, measured: 3.0 to 4.1 times, all colours equal | Prepare the colour map once per `sample_colormap_span` call | medium | S | colour-for-colour comparison |
| FS2c | measured: 24 to 36 times, at most one level different | A colour table. Changes output: owner decides | n/a | S | n/a |
| FS2d | measured in isolation: 464 to 538 times less copying | A waterfall on a sliding row store. New surface area: needs a design | n/a | B | n/a |
| FS3 | writer: refuted (no gain with atomics kept). Reader: confirmed, 2.1 to 4.4 times, same contents | Resolve the wrap once per block in `consume` and `push_overwrite`. Leave the writer | low | S | column contents, gap and dropped counts |
| FS4 | confirmed, measured: 2.0 to 2.2 times, same values | Blur across per row in cache-resident byte rows; bytes between passes; lane count fixed before the loop | medium | C | value comparison over the result region |
| FS5 | measured: 6 times, rims differ by up to 8 levels | Stamp one rasterised marker. Changes output: owner decides | n/a | C | n/a |
| FS6 | unproven (read, not measured) | Measure the drawing executor with SDR# as workload: two image copies per `draw_image`, state reset per command, clone and copy-back of owned targets | unknown | C | to be chosen with the measurement |
| FS7 | confirmed, measured: record is 320 bytes; 111 ns per command, 32 with `reserve` | Reserve `commands_` from the previous recording's count; the compact record is a separate, larger option | low | S (reserve), B (record) | display trace digests |
| FS1r | confirmed, measured idle: 20 to 46 times, same bytes on 40,000 blits | Rounded clips have no interior path on main (16 ns per pixel for everything inside a rounded control). A pixel inside both rectangles with its centre outside the corner squares has whole coverage: give it the interior kernel. Scaled sources: 3.4 to 5.1 times by sampling a block into bytes for the same kernel | high | C | as FS1 |
| FS10 | confirmed, measured idle: baseline 17 to 78 ns per pixel; strip prototype 3 to 25 times, no row differs | Material bake: when the resolved look does not vary across its interior, bake the side strips and one column and copy across (prototype used `bake_material_region` unchanged). The same down the rows is not yet prototyped | high | C | baked raster bytes before and after |
| FS6m | confirmed, measured idle (supersedes FS6's "unproven") | Drawing executor: an opaque 1:1 `draw_image` as a row copy (18 times, same bytes); set canvas state only when it changes (0.7 to 1.3 us per command today; batches are 1.5 to 4.7 times cheaper); draw owned targets without clone and copy-back (2 times) | high for SDR# | C | byte comparison per command kind |
| FS11 | confirmed, measured idle | Text: `measure_text_utf8` is 1.5 to 9.6 us per call, a memo of sizes 12 to 64 ns. Unretained draws are 29 to 266 us (3.7 us per glyph); retained 1.4 to 18 us. S4 is minor (1.5 us per probe). Whether a size memo falls under the recorded decision against a global cache is the owner's call | medium | S | measured sizes equal |
| FS8i | confirmed, measured idle | 48 compiles of the umbrella header: 64.7 s at 8 jobs, 205 s at 48 (16 GB of memory, 712 MB per compile). One job pool across worktrees | high | S | wall time of the same job list |
| FS8 | confirmed, measured under load | One job pool across worktrees; tests include what they use; heavy value types' special members out of line; fewer statically linked executables. Supersedes S9 (main has a test precompiled header) | high | S to B | build times before and after, on an idle machine |

