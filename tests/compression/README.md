# Compression tests

`malformed_sweep_tests.cpp` sweeps malformed inputs through stx_zlib in this
suite. The inflate contract tests, the inflate and PNG decode fuzz harnesses
and the oracle experiments are stx's (`stx_zlib/tests/README.md` and
`tools/CODEC_EXPERIMENTS.md` in malkuth-software/stx).

## Fuzzing and sweeps

The sustained campaign is retired (owner revision of decision 6A,
2026-10-20). Evidence comes from the deterministic malformed-input sweeps in
the normal suite (`gui_forms_malformed_sweep_tests`,
`gui_forms_png_malformed_sweep_tests`), run under the sanitizers, and from the
whole-suite sanitizer job. `.github/workflows/fuzz-smoke.yml` runs a few
minutes of libFuzzer per target per change on GitHub's runners, including
the pinned-zlib oracle in ordinary and strict modes. That run is a build
check, not a certificate, and it never runs on the owner's machine.
