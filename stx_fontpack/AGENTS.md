# stx_fontpack guardrails

This package follows the stx repository's rules (malkuth-software/stx
`AGENTS.md`), because it is meant to move there unchanged:

- It is self-contained: no GUI.Forms header and no source outside this
  directory. It depends on stx_zlib only through the `stx_zlib::stx_zlib`
  target, either the sibling's or an installed config.
- C++20 only. Public headers carry no third-party types.
- LICENSE and PROVENANCE.md stay current. A change derived from other work
  names it in PROVENANCE.md.
- The loader allocates only through the caller's `Allocator`, and releases
  with the size it allocated. It runs work only on the caller's `Executor`.
  It reads no clock, starts no thread and keeps no global state.
- Every size a pack claims is checked against `stx_fontpack/limits.hpp`
  before anything is allocated for it. The packer enforces the same limits.
- Lane code stays portable C++: no intrinsics, no compiler builtins and no
  platform forks.
- The house style applies (GUI.Forms `AGENTS.md`). Run the checkers over this
  directory before committing:

```text
python3 -B tools/check_house_style.py stx_fontpack
python3 -B tools/check_lifetime_rules.py stx_fontpack
```

Memory errors and undefined behaviour are defects. Run `stx_fontpack_tests`
under the sanitizers (`tools/ci_build.py --sanitize --tests 'stx_fontpack_.*'`
in GUI.Forms).
