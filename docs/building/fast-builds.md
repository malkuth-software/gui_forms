# Fast, reproducible builds

GUI.Forms builds the same way on macOS, Windows and Linux, and CI keeps those
builds quick by not compiling or testing the same thing twice wherever it can
prove it is the same. That is not one trick, such as a cache or a newer
compiler. It is three parts used together:

- **the configuration**: one pinned LLVM 22 toolchain, one fixed directory
  layout and one set of `ccache` settings, identical on every platform;
- **the reuse**: a source tree that already passed its tests on a platform
  is recognised and not rebuilt;
- **the caching**: each platform's tested object cache is published, so a
  fresh checkout, a later commit or another project starts warm.

Each part depends on the other two. A cache built under a different layout or
compiler identity is just a miss. Reuse is only safe because the configuration
pins everything the identity covers. This page describes the configuration and
the loop it gives you. [Reusing CI caches](reusing-ci-caches.md) covers reuse,
publication, restoring a published cache, and a checklist for copying the
method into your own project. The method only works when it is copied exactly.

## The loop: edit, commit, forget

At its best, you edit, commit and push, and then stop thinking about the
build. `.github/workflows/native.yml` runs one job per platform (`macos-15`,
`ubuntu-24.04`, `ubuntu-24.04-arm`, `windows-2022`). Each job:

1. reuses an earlier successful result for the identical tree if one exists;
2. otherwise restores the closest object cache, compiles only what changed,
   and runs every native, installed-SDK and development-profile test;
3. uploads its tested cache archive and a validation receipt;
4. on `main`, publishes that archive to the `build-<sha>` pre-release at
   once, without waiting for the other platforms.

`.github/workflows/release.yml` builds and smoke-launches the packaged
programs on all four platforms, starting from the object caches the native
jobs saved, and on a push to `main` publishes a tested `v*` release. What you
get back is a result per platform, a published cache per platform, and
release packages. A new push to the same branch cancels the run still in
progress (`concurrency: cancel-in-progress`).

Day to day it is usually less tidy: edit, build locally, debug, commit, debug
again in CI, then forget. The local build uses the same toolchain file and
`ccache` settings, so the second local build of a small edit only recompiles
what changed. A developer machine rarely has a compiler binary and SDK
byte-identical to a hosted runner's, and `CCACHE_COMPILERCHECK=content`
treats a different binary as a different compiler. Expect CI's caches to help
CI and your local cache to help you.

## One toolchain, pinned and checked

Every platform builds with LLVM 22.1.8, selected and checked in three places:

| Check | Where | Rejects |
|---|---|---|
| Configure | root `CMakeLists.txt` | any compiler that is not Clang 22.1.x |
| CI identity | `tools/compiler_identity.py` | any `clang --version` other than 22.1.8 |
| Cache manifest | `tools/cache_manifest.py` | an archive without LLVM 22.1.x; records the SHA-256 of the `clang` binary |

`cmake/llvm22.cmake` is passed to both the toolkit and every consumer. It
selects `clang` and `clang++` from `PATH` for C, C++ and Objective-C++. On
macOS it also fixes the Xcode SDK (`xcrun --show-sdk-path`), arm64, a 14.0
deployment target and the matched libc++ runtime in `GUI_FORMS_LLVM_RUNTIME`.

| Platform | Runner | Compiler source |
|---|---|---|
| macOS arm64 | `macos-15` | Homebrew `llvm@22`, plus libc++, libc++abi and libunwind built from SHA-256-pinned 22.1.8 source for macOS 14 (`tools/build_macos_runtimes.py`) |
| Linux x64, arm64 | `ubuntu-24.04`, `ubuntu-24.04-arm` | apt.llvm.org `llvm-toolchain-noble-22`, package `clang-22` |
| Windows x64 | `windows-2022` | MSYS2 CLANG64 tree from `tools/windows_toolchain.json`, restored from a published archive |

The macOS runtime and the Windows toolchain are artefacts in their own right,
with manifests that are verified before use; see
[Reusing CI caches](reusing-ci-caches.md#matching-the-toolchain).

## One layout

CI checks the source out at `gui_forms/` under the job's workspace and builds
three levels below it. `tools/ci_build.py` derives every path from that
layout, so nothing in the job names an absolute directory:

```text
<workspace>/                               CCACHE_BASEDIR
├── gui_forms/                             the checkout
├── .ccache/                               CCACHE_DIR
├── cache-manifest.json                    written beside a cache archive
└── .build/
    ├── native-<platform>/gui-forms/       toolkit build tree
    ├── native-<platform>/gui-forms-sdk/   install prefix
    ├── native-<platform>/skia/            Skia build output
    └── toolchain/llvm-22.1.8-macos14/     macOS runtime prefix
```

`<platform>` is `macos-arm64`, `linux-x64`, `linux-arm64` or `windows-x64`.
`ccache` rewrites absolute paths below `CCACHE_BASEDIR` into paths relative to
the compiler's working directory. With source and build tree at the same
relative positions, two checkouts in different places hash identically. Move
one relative to the other and compilations miss.

## One compiler cache

Every job sets the compilers and launchers in its environment:

```yaml
env:
  OBJCXX: clang++
  CC: clang
  CXX: clang++
  CMAKE_C_COMPILER_LAUNCHER: ccache
  CMAKE_CXX_COMPILER_LAUNCHER: ccache
  CMAKE_OBJCXX_COMPILER_LAUNCHER: ccache
```

and then configures `ccache` from the workspace root, in the same step on
every platform:

```sh
cache_dir="$PWD/.ccache"
cache_base="$PWD"
if command -v cygpath >/dev/null; then      # Windows: mixed-style paths
  cache_dir="$(cygpath -m "$cache_dir")"
  cache_base="$(cygpath -m "$cache_base")"
fi
echo "CCACHE_DIR=$cache_dir" >> "$GITHUB_ENV"
echo "CCACHE_BASEDIR=$cache_base" >> "$GITHUB_ENV"
echo "CCACHE_COMPILERCHECK=content" >> "$GITHUB_ENV"
echo "CCACHE_SLOPPINESS=pch_defines,time_macros" >> "$GITHUB_ENV"
echo "CCACHE_MAXSIZE=500M" >> "$GITHUB_ENV"
```

`CCACHE_COMPILERCHECK=content` hashes the compiler binary itself, not its
path or timestamp. The only sloppiness options are the two a precompiled header
needs: the test programs share one per group of programs with equal options
(`cmake/TestPrecompiledHeader.cmake`), which takes most of the test sources' parse
time away; the daily build compiles the tests without it
(`GUI_FORMS_TEST_PRECOMPILED_HEADER=OFF`), so each test still includes what it
uses. The Objective-C++ launcher
matters on macOS, where the AppKit host is Objective-C++. The CPU renderer and the
text stack come from the stx package (`cmake/Stx.cmake`), which CI restores from an
Actions cache keyed by `stx.lock`, the package recipe, the platform and the compiler;
the build compiles none of it, and builds the package at configure time only when
the key changes. A build then runs as:

```sh
ccache --zero-stats
python gui_forms/tools/ci_build.py --jobs 4   # Linux: inside xvfb-run -a
ccache --show-stats --verbose
```

## What the cache proves

`tools/prove_compiler_cache.py` copies a clean source tree to two different
directories, builds the retained-control event test `gui_forms_owned_event_tests`
in both with one cache, and requires byte-identical executables. It then
edits one source file, one header and one compiler option, and fails unless
each causes misses. `.github/workflows/cache-proof.yml` runs it weekly on
Linux x64 and on demand. Recorded run, LLVM 22.1.8 on an Apple M4 Mac mini
([`experiments/llvm22-platform-contract/cache-proof-summary.json`](https://github.com/malkuth-software/notes/blob/gui_forms/experiments/llvm22-platform-contract/cache-proof-summary.json)):

| Step | Change | Missed compilations | Build time |
|---|---|---:|---:|
| Cold | empty cache | 181 | 52.3 s |
| Relocated | different checkout directory | 0 of 181 | 1.0 s |
| Implementation | one `.cpp` file | 1 | 0.5 s |
| Header | one widely included header | 128 | 40.9 s |
| Option | `-fno-inline-functions` | 180 | 49.8 s |

Cold and relocated executables are the same 2,132,568 bytes. The times cover
only this fixture. The proof's option change must be a real code-generation
change: an earlier trial used an unused preprocessor definition, and `ccache`
correctly reused the identical preprocessed output.

Hosted CI job times, with workload and runs, are under
[Reusing CI caches](reusing-ci-caches.md#ci-job-times).
