# Reusing CI caches

[Fast, reproducible builds](fast-builds.md) describes the pinned configuration.
This page covers what CI does with it: how a native job avoids repeating work,
what it publishes, how you restore a published cache, and what must match
exactly if you copy the method.

## Identity

A native job identifies its inputs with four values (`tools/ci_reuse.py
identity`, from `tools/compiler_identity.py`):

| Value | Made from |
|---|---|
| `tree` | `git show -s --format=%T HEAD`: the complete tracked tree, not the commit |
| `compiler` | SHA-256 of the `clang` binary's hash, the runner `ImageVersion`, the architecture, `clang --version`, `ccache --version` and, on macOS, the SDK version |
| `environment` | the same, plus `cmake --version`, `ninja --version` and the installed package list (`brew list --versions`, `pacman -Q` or `dpkg-query -W`) |
| `platform` | `macos-arm64`, `linux-x64`, `linux-arm64` or `windows-x64` |

Two commits with the same tree, such as a pull request head and the merge that
lands it unchanged, are the same input. A new runner image is a new compiler
identity even when the `clang` binary is unchanged.

Every job checks out the commit its workflow files come from. For a pull
request, that is the test merge commit (`refs/pull/N/merge`); for a push, the
pushed commit. Jobs used to check out a pull request's head while running the
merge commit's workflow, and when `main` had added a tool that its workflow
calls, a branch behind `main` failed on the missing file. An up-to-date
branch's merge commit has the head's tree, so reuse is unchanged there. For a
branch that is behind, the receipt records the merge commit. `ci_reuse.py`
accepts it only if that commit has the producer run's head as a parent and
has the claimed tree (`merge_of_head`).

## What a native job tries, in order

| Step | When | Result |
|---|---|---|
| Promotion, `ci_promote.py lookup` | on `main`, before any tool is installed | the same tree passed on this platform: publish those bytes, install nothing, build and test nothing |
| Exact reuse, `ci_reuse.py lookup` | after tools and identity | same tree, compiler and environment passed: restore that archive, skip build and tests |
| Actions caches | otherwise | `.ccache` from `gui-objects-v2-<platform>-<compiler>-<tree>`, else the newest for the same compiler, else the same platform; miniaudio from a key over `cmake/Audio.cmake` (the stx checkout, with stx_skia and the text stack, is fetched at configure time from `stx.lock`) |
| Seed, `ci_promote.py seed` | otherwise | objects from the first acceptable one of at most eight recent validated archives, on any branch, with the same compiler identity; every test still runs |
| Build and test | otherwise | `ci_build.py`, the development text profiles and the native host tests |

Exact reuse accepts only an older run of `.github/workflows/native.yml` in
this repository, not a fork, whose commit has the same actual tree (asked of
the Git API, not taken from the receipt), whose single job for the platform
succeeded, and whose receipt (schema 2, at most 256 KiB, artifact digest
checked) names the same tree, compiler, environment and platform and a
`gui-forms-cache-<platform>` archive from that same run. If an older identical
job is still running, the lookup waits for it, up to 30 minutes, rather than
compile the same tree twice. A missing or rejected receipt means a fresh
build. A downloaded archive whose SHA-256, member paths or manifest disagree
with the receipt fails the job. The `force_validation` dispatch input skips
reuse and promotion.

## Promotion across the pull request boundary

Actions caches saved by a pull request are not visible to `main`, and `main`
can receive a newer runner image than the pull request had, which changes the
compiler identity. Promotion handles both. It matches on platform and tree
alone, across at most 500 recent artifacts, then applies the full trust check
above to the receipt's own identity. It downloads the tested archive, checks
its digest and copies every object and runtime file unchanged into a new
archive. Only `cache-manifest.json` is rewritten: the revision and run become
the `main` commit's, `validation_provenance` names the run that tested it,
and compiler, SDK, runtime and runner fields stay those of the producer. It
does not claim tests ran on the publishing runner. On Windows it also fetches
the toolchain archive the cache records, from a release or the producer's
unexpired artifact, and fails if neither exists.

Promotion applies when the `main` commit has exactly the tested tree, as when
an up-to-date branch is merged. Any other merge builds fresh, usually from a
seed.

## Tests the programs job does not repeat

The native job's `ci_build.py` run writes CTest's JUnit report
(`ctest-native.xml` in the toolkit build directory). `cache_manifest.py`
records the passed tests from it as `validated_tests` in
`cache-manifest.json`. A reused validation carries the producer's list over.
On a pull request, the programs job reads that list from its platform's
archive and runs only the tests of its own build that are not on it
(`tools/native_tests.py select`). Those are the Gallery and demoboard tests,
which the native configuration leaves out, and any test the native job
skipped. A manifest without the list runs every test. Direct runs of the
programs workflow and `native.yml`'s daily scheduled run on `main` run every
test.

## Jobs that carry an earlier run of the same tree

Branch protection merges a pull request only when it is up to date with
`main`, so a push to `main` has the tree that pull request's last run tested.
Since 2026-10-23 (the owner's choice), the push carries that run's results
instead of repeating them, and every job still passes under its own name:

| Job | Carried with | What the push then does |
|---|---|---|
| native | `ci_promote.py lookup`, `ci_reuse.py lookup` | promotes or restores the tested archive |
| programs | `release_tests.py merged` | copies the pull request's programs ctest receipt; builds, packages and smoke tests under its own version; runs no tests |
| docs | `ci_tree_receipt.py lookup --kind docs` | builds nothing |

`release_tests.py merged` takes the merge commit's second parent as the pull
request's head, which must have this tree, and a `pull_request` run of
`native.yml` on that head, in this repository, whose programs job for the
platform succeeded. The packages receipt of that job must name a revision of
this tree, and its ctest receipt must report at least one test and no
failure. The carried receipt gains a comment naming the run and saying what
it covers: the tests not in the validated list of the tree's native job,
which passed the rest. A release tag carries `main`'s receipt the same way
(`release_tests.py find`), comment and all.

`ci_tree_receipt.py` records, for each passing docs job (and Sanitizers job, when one is requested by hand), a
receipt artifact `gui-tree-<kind>-<tree>`: the tree, the revision checked
out, the run, what it covered (for the sanitizers, `ci_build.py`'s build
record: `everything`, `selected` or `nothing`) and, for a job that carried
one, the run it carried. A lookup accepts only an older `native.yml` run of
this repository, not a fork's, whose job of that name succeeded, whose
artifact digest and receipt match this tree and run, and whose head has the
tree or whose recorded revision merges its head (`merge_of_head`). A pull
request's docs run is carried by the push to `main` the same way. A failed
lookup runs the job. The Sanitizers job no longer runs on pull requests,
pushes, merge groups or the schedule (owner ruling, 2026-11-03): it runs the
whole suite only when requested by hand (`workflow_dispatch` with
`run_sanitizers`), and the local sanitized run at checkpoint fold time is the
gate.

The daily scheduled run on `main` (03:41 UTC) carries nothing: the native
jobs validate afresh (`force_validation`), and the programs and docs jobs
build and test everything. It is the backstop for the two rules
the carried results rely on, `native_tests.py`'s name match and
`change_selection.py`'s selection, and it publishes nothing. It has its own
concurrency group, so it and a push to `main` never cancel each other.

## The sanitizer job's cache

Its compiler cache crosses the branch boundary through an artifact:
each successful job uploads its `.ccache` as `gui-sanitize-ccache-linux-x64`
(kept seven days), and the next job takes the newest one of the nearest
branch (`tools/ci_sanitizer_seed.py`): a pull request's own earlier push,
then the branch it is stacked on; on `main`, the merged pull request's head.
Without one it keeps `main`'s Actions cache. Only `main` saves Actions
caches for the sanitizer, native object and programs object caches (a
sanitizer job that carried a pull request's result built nothing, so the
daily run and pushes that carried nothing refresh it);
pull requests restore `main`'s, so per-run entries no longer evict the
dependency caches from the repository's 10 GB.

## Publication

On `main`, each platform publishes as soon as its own job passes
(`tools/publish_cache.py --platform <platform>`). It creates or reuses the
pre-release `build-<full-sha>`, checks that the tag names the tested commit,
and uploads `gui-forms-cache-<platform>.tar.gz`. Windows first uploads
`gui-forms-toolchain-windows-x64.tar.zst` and its `.json` manifest, after
checking they are the toolchain the cache records. A rerun is accepted only
if it would upload identical bytes; a published asset is never replaced. A
platform missing from a release has not been published; it has not passed.

An archive holds `.ccache/` and `cache-manifest.json`; the macOS archive
also holds `.build/toolchain/llvm-22.1.8-macos14/`. The manifest records the
revision, compiler version text and binary SHA-256, launchers, runner and
image version, layout, `ccache` settings and version, minimum OS, build
options, SDK and runtime manifest on macOS, and the toolchain record on
Windows. `tools/prune_cache_releases.py` reports weekly which `build-*`
releases are neither among the five newest nor listed in
`packaging/pinned_cache_revisions.json`; deleting them needs `--apply`.

## Restore a published cache

Check out the pinned revision at `gui_forms/` in an empty workspace, then:

```sh
gh release download build-<full-sha> --repo malkuth-software/gui_forms \
    --pattern gui-forms-cache-linux-x64.tar.gz
sha256sum gui-forms-cache-linux-x64.tar.gz   # compare with the digest you pinned
tar -xzf gui-forms-cache-linux-x64.tar.gz
export CC=clang CXX=clang++ OBJCXX=clang++
export CCACHE_DIR="$PWD/.ccache" CCACHE_BASEDIR="$PWD" CCACHE_COMPILERCHECK=content
export CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache \
       CMAKE_OBJCXX_COMPILER_LAUNCHER=ccache
python3 gui_forms/tools/ci_build.py --jobs 4   # Linux: inside xvfb-run -a
```

`ci_build.py` configures exactly as CI does and fetches the pinned sources.
On macOS, set and verify the runtime prefix below first; use `shasum -a 256`
there. A hit needs the same compiler binary, SDK headers and options;
anything else compiles normally. Never loosen `ccache` checks to force hits,
and still run your own tests. `ccache --show-stats` shows what was reused.

## Matching the toolchain

**macOS.** Set `GUI_FORMS_LLVM_RUNTIME="$PWD/.build/toolchain/llvm-22.1.8-macos14"`
and run `python3 gui_forms/tools/macos_runtime_manifest.py "$GUI_FORMS_LLVM_RUNTIME"`.
It compares the three dylibs, their symlinks, the configured libc++ headers,
the SDK version and the `clang++` binary with `runtime-manifest.json`, and
fails on any difference; then rebuild with `tools/build_macos_runtimes.py`.
After a fresh build, CI extracts the archive in a new directory and runs a
native test with all three relocated dylibs loaded through its own rpath.

**Windows.** Download the toolchain archive and manifest from the same
release and check `archive_sha256`. Extract it with Python 3.14
(`tarfile.open(path, 'r:zst')`) so `msys64/` lands at the manifest's
`producer.msys_root`, `D:\a\_temp\msys64` on hosted runners: system header
paths enter the hash and lie outside `CCACHE_BASEDIR`. Prepend
`msys64/clang64/bin` and `msys64/usr/bin` to `PATH`; `clang.exe` must match
`compiler_sha256`. Create `msys64/tmp` if the archive lacks it (the first,
layout 1 archive did). CI restores this archive instead of contacting the
MSYS2 mirrors: first from the release `tools/windows_toolchain_release.json`
names, read by its tag, then from the recent releases. When neither holds an
archive for `tools/windows_toolchain.json` and the archive layout, the job
fails with `ToolchainUnavailable` rather than install the current MSYS2
packages, because the mirrors carry only the current LLVM. A deliberate
specification change is packaged by running `native.yml` by hand with
`package_windows_toolchain`. Packing then refuses a clang whose version is not
the specification's `llvm`. Once the new toolchain is published from main,
point `tools/windows_toolchain_release.json` at that release and add its
revision to `packaging/pinned_cache_revisions.json`.

## CI job times

Hosted job durations from the Actions API, excluding queue time, recorded
in [`docs/notes/CI_TIMING_2026-10-08.md`](https://github.com/malkuth-software/notes/blob/gui_forms/docs-notes/CI_TIMING_2026-10-08.md). A fresh job builds the Release
toolkit, runs all tests, installs the SDK, builds and tests the installed
examples and runs the development text profiles. The workflow has gained
steps since; these are the recorded numbers, not a promise.

| Situation | Platform | Run | Job |
|---|---|---|---:|
| `main`, same tree as the pull request, exact reuse | macOS arm64 | 37754166288 | 56 s |
| Fresh, objects of an older tree, 246 of 705 compilations missed | Linux x64 | 37758529105 | 10 m 10 s |
| Fresh, cross-branch seed, 0 of 705 missed | Linux x64 | 37760012173 | 1 m 45 s |
| Fresh, same run | macOS, Windows, Linux arm64 | 37760012173 | 4 m 26 s, 4 m 09 s, 3 m 09 s |

The 56-second job compiled and tested nothing; it restored, re-archived and
recorded the earlier result.

## Copying the method

Copy all of it. The code depends on every row, and the last three record
failures seen when the method was varied.

| Must match | Why |
|---|---|
| LLVM 22.1.8 from the same package source; `CC=clang`, `CXX=clang++`, `OBJCXX=clang++`; `cmake/llvm22.cmake` on both sides | the content check hashes the binary; Windows `cc.exe` is another identity; a CMake cache keeps its old compiler, so use a fresh build tree |
| `gui_forms/` and `.build/native-<platform>/gui-forms/` under one root | hits rely on equal paths relative to `CCACHE_BASEDIR` |
| `CCACHE_BASEDIR`, `CCACHE_DIR`, `CCACHE_COMPILERCHECK=content`, no sloppiness | anything weaker reuses objects it should not |
| all three launchers as environment variables | Skia's build scripts read `CMAKE_CXX_COMPILER_LAUNCHER` from the environment |
| the `ci_build.py` configure options | options are part of every hash |
| reuse only older, successful runs of the same workflow and repository, matching tree, compiler and environment | anything looser can turn an untested or failed result green |
| macOS SDK version and runtime manifest; Windows `msys64` location | SDK and MSYS2 headers lie outside the base directory; the runtime is checked byte for byte |
| tree from `git show -s --format=%T HEAD` | MSYS2 rewrote `HEAD^{tree}` to `HEAD^tree` and broke the fingerprint |
| the toolchain archive as published | an archive without `tmp/` failed the audio tests |
| per-image rpaths, not `DYLD_LIBRARY_PATH` | a global override replaced Apple's own C++ runtime and failed |
