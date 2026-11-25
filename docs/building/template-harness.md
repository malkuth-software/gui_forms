# GUI.Forms builds through template_harness

GUI.Forms' CI tools that are not specific to GUI.Forms live in
[malkuth-software/template_harness](https://github.com/malkuth-software/template_harness):
the native validation reuse and promotion, the compiler cache tools, the
Windows toolchain, the macOS runtimes, the change selection, the Linux
package installer, and the house-style, lifetime and state-machine checkers.
GUI.Forms keeps its own build driver (`tools/ci_build.py`), its packaging
(`tools/package_notices.py` and the SDK tools; the program packager is
gui_forms_devkit's), `tools/stack_frames.py`,
its LeakSanitizer suppressions (`tools/sanitizer/lsan.supp`) and the tests
of those files. The configure options and host preparation are
`tools/toolkit_configure.py`, which needs no harness: the source SDK's
`tools/build_sdk.py` builds with it in an archive that carries none.

## The pin and the layout

`harness.lock`, at the root of this repository, pins one harness commit:

```toml
[harness]
repository = "malkuth-software/template_harness"
commit = "<40 hexadecimal characters>"
```

The harness is checked out beside the GUI.Forms checkout, in the workspace
layout CI has always used:

```text
<workspace>/gui_forms            this repository
<workspace>/template_harness     the harness, at the locked commit
<workspace>/.build, .ccache, .ci the build trees and CI state
```

The harness reads GUI.Forms' names (artifact, cache and release asset
prefixes, environment variables, package sets, test areas) from its
`projects/gui_forms.toml`, found through `HARNESS_PROJECT`, with the workspace
in `HARNESS_WORKSPACE`. The names are the ones GUI.Forms used before the
move, so the caches, validation receipts and published releases of earlier
runs still match.

## In CI

Every job that runs a harness tool checks the harness out with
`.github/actions/harness-checkout` right after the GUI.Forms checkout. The
action reads `harness.lock`, checks the locked commit out at
`template_harness/`, verifies it and sets `HARNESS_PROJECT` and
`HARNESS_WORKSPACE` for the job's later steps. The workflows call the tools
there, for example `python template_harness/harness/ci_reuse.py lookup`.

## Locally

From the workspace that holds `gui_forms/`:

```sh
git clone https://github.com/malkuth-software/template_harness template_harness
python3 template_harness/harness/lock.py --lock gui_forms/harness.lock checkout template_harness
```

`lock.py checkout` moves an existing clone to the locked commit, fetching
it when needed, and refuses a clone with uncommitted changes. The harness's
bootstrap (`bootstrap.sh` or `bootstrap.ps1`) does this for a new workspace.
`python3 template_harness/harness/lock.py --lock gui_forms/harness.lock bump --to <branch, tag or commit>`
moves the pin; commit `harness.lock` like any other change.

## The old tool paths

The moved tools keep forwarding shims at their old paths in `tools/`, so
the commands and imports that name them (File Manager's among them) keep
working: `tools/ci_reuse.py`, run, runs the harness's `harness/ci_reuse.py`
with the same arguments, and imported, is that module. `tools/harness_link.py`
lists the shims (`SHIMS`, plus `tools/install_linux_ci_packages.sh`). A shim
fails with the pinned commit in its message when the harness is not beside
the checkout. `tools/ci_build.py` still exports `configure_tree`,
`sanitizer_options` and `SANITIZER_LINK_FLAGS`, which it now takes from the
harness's `harness/ci_build_steps.py`.

## Changing the harness

A change to a moved tool is made in template_harness, then reaches
GUI.Forms through a `harness.lock` bump in a GUI.Forms pull request, whose
CI runs the pinned harness's own tests (the house-style job) as well as
GUI.Forms' build. A bump changes GUI.Forms' tree, so it is validated afresh
like any other change. The Linux package cache key also names harness
files: it hashes `harness/install_linux_ci_packages.sh` and
`projects/gui_forms.toml`.
