#!/bin/sh
# Installs a CI profile's Ubuntu packages through template_harness:
# harness/install_linux_ci_packages.sh, whose package sets are the
# [linux_packages.*] tables of projects/gui_forms.toml (harness.lock pins the
# commit). It fails clearly when the harness is not beside this checkout
# (docs/building/template-harness.md). Every GUI.Forms Linux job calls this
# script, as do the commands that name this path (File Manager's among them).
#
# Then LLVM's libc++ 22, libc++abi and libunwind, which every Linux build
# compiles against and links statically (cmake/llvm22.cmake: their .a
# archives; owner decisions, 2026-11-15), from the
# apt.llvm.org repository the harness script has just added. They are here
# rather than in the harness's package tables until the next harness.lock
# bump moves them there. Like the harness's other apt.llvm.org packages
# they are fetched each run, not kept in the archive cache.
#
#   sh tools/install_linux_ci_packages.sh <profile> <archive-directory>
set -eu

tools=$(cd "$(dirname "$0")" && pwd)
workspace=$(dirname "$(dirname "$tools")")
harness="$workspace/template_harness"
if [ ! -f "$harness/harness/install_linux_ci_packages.sh" ] || [ ! -f "$harness/projects/gui_forms.toml" ]; then
    commit=$(sed -n 's/^commit[[:space:]]*=[[:space:]]*"\([0-9a-f]\{40\}\)".*$/\1/p' "$tools/../harness.lock")
    echo "GUI.Forms: template_harness is not beside this checkout. Expected $harness at commit ${commit:-(unknown)} (gui_forms/harness.lock); docs/building/template-harness.md." >&2
    exit 1
fi
HARNESS_PROJECT=${HARNESS_PROJECT:-$harness/projects/gui_forms.toml}
HARNESS_WORKSPACE=${HARNESS_WORKSPACE:-$workspace}
export HARNESS_PROJECT HARNESS_WORKSPACE
sh "$harness/harness/install_linux_ci_packages.sh" "$@"
sudo apt-get install -y --no-install-recommends libc++-22-dev libc++abi-22-dev libunwind-22-dev
