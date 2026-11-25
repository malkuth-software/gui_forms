#!/usr/bin/env python3
"""The third-party notice and runtime rules GUI.Forms' own packaging shares.

The program packager (package_programs.py) moved to
malkuth-software/gui_forms_devkit with the programs; what this tree still
packages needs its rules: tools/ci_build.py checks the installed SDK's
notices and Windows imports against them, and tools/package_abi_library.py
copies the notices into the C ABI library archive (copy_notices). The
devkit's copy of package_programs.py keeps the same rules; change both
together.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Callable

ROOT: Path = Path(__file__).resolve().parents[1]
LICENSES_PATH: Path = ROOT / "packaging/licenses.json"
WINDOWS_SYSTEM_DLLS: frozenset[str] = frozenset({
    "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "comdlg32.dll", "ole32.dll",
    "oleaut32.dll", "oleacc.dll", "comctl32.dll", "shell32.dll", "shlwapi.dll", "usp10.dll",
    "windowscodecs.dll", "ws2_32.dll", "version.dll", "imm32.dll", "dwmapi.dll", "ntdll.dll",
    "ucrtbase.dll", "bcrypt.dll", "secur32.dll", "rpcrt4.dll", "setupapi.dll",
    "winspool.drv", "winmm.dll", "crypt32.dll", "normaliz.dll", "uxtheme.dll", "msimg32.dll",
    "shcore.dll", "dbghelp.dll", "psapi.dll", "userenv.dll", "iphlpapi.dll",
    "uiautomationcore.dll", "avrt.dll", "icuin.dll", "icuuc.dll", "icu.dll", "dxgi.dll", "combase.dll",
})
# Runtime DLLs no Windows binary may import. Windows builds
# x86_64-pc-windows-msvc on STXlib's own runtime (owner, 2026-11-24;
# cmake/llvm22-windows-msvc-own.cmake): libc++ and STXlib's C++ runtime are
# linked statically and the one C library is ucrtbase.dll, imported by name.
# So no Microsoft C or C++ runtime (msvcrt, vcruntime, msvcp, concrt, the
# debug UCRT or the api-ms-win-crt forwarders), no GCC runtime (libgcc_s,
# libstdc++, libgomp, libatomic, libssp, libquadmath), and no LLVM or
# winpthreads DLL.
WINDOWS_FORBIDDEN_RUNTIME_DLL: re.Pattern[str] = re.compile(
    r"(libgcc_s_[a-z0-9_]+-\d+|libstdc\+\+-\d+|libgomp-\d+|libatomic-\d+|libssp-\d+|libquadmath-\d+"
    r"|libc\+\+|libc\+\+abi|libunwind|libwinpthread-\d+"
    r"|msvcrt|msvcr\d+|msvcp\d+[a-z0-9_]*|vcruntime\d+[a-z0-9_]*|concrt\d+|ucrtbased"
    r"|api-ms-win-crt-[a-z0-9-]+)\.dll", re.IGNORECASE)
NOTICES_NAME: str = "THIRD_PARTY_NOTICES.md"
# The title every GNU GPL, LGPL and AGPL text begins with; a package carries
# none of them.
GPL_TITLE: re.Pattern[str] = re.compile(r"GNU (LESSER |LIBRARY |AFFERO )?GENERAL PUBLIC LICENSE")


# The stx libraries' notices ($STX/<package>/...) come from the stx checkout the build
# consumed (stx.lock; cmake/Stx.cmake), never from this tree, which holds no stx sources.
STX_PREFIX: str = "$STX"
# The cache variables naming the stx package a build linked (cmake/Stx.cmake).
STX_PACKAGE_VARIABLES: tuple[str, ...] = ("GUI_FORMS_STX_PREFIX", "GUI_FORMS_STX_PACKAGE_DIR")
STX_LOCK_PATH: Path = ROOT / "stx.lock"
STX_OVERRIDE_VARIABLE: str = "FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX"


# STXlib's notices ($STXLIB/...: STXlib, stx_btree, stx_minmallocpp, tzdata,
# STXlib's Unicode licence) come from the
# licence directory the stx package installed beside its packages
# (<package>/stx/share/licenses/stxlib; stx's cmake/Stxlib.cmake), since STXlib is
# fetched inside the package build and no stx checkout holds it.
STXLIB_PREFIX: str = "$STXLIB"
STXLIB_LICENSE_PARTS: tuple[str, ...] = ("stx", "share", "licenses", "stxlib")


def _is_stxlib_licenses(directory: Path) -> bool:
    marker: Path = directory / "stx_btree-LICENSE"
    return marker.is_file()


def stxlib_license_directory(build_dir: Path | None) -> Path | None:
    """The STXlib licence directory whose notices a package ships, or None.

    In order: the GUI_FORMS_STXLIB_LICENSES environment variable, then for
    the build directory and its toolkit/ subdirectory the stx package the
    build linked (GUI_FORMS_STX_PREFIX or GUI_FORMS_STX_PACKAGE_DIR), whose
    stx/share/licenses/stxlib holds them. A named directory without them
    raises.
    """
    if os.environ.get("GUI_FORMS_STXLIB_LICENSES"):
        named: Path = Path(os.environ["GUI_FORMS_STXLIB_LICENSES"])
        if not _is_stxlib_licenses(named):
            raise RuntimeError(f"{named} is not STXlib's licence directory (no stx_btree-LICENSE)")
        return named
    if build_dir is None:
        return None
    build: Path
    for build in (build_dir, build_dir / "toolkit"):
        variable: str
        for variable in STX_PACKAGE_VARIABLES:
            value: str = _cached_value(build, variable)
            if not value:
                continue
            licenses: Path = Path(value).joinpath(*STXLIB_LICENSE_PARTS)
            if _is_stxlib_licenses(licenses):
                return licenses
    return None


def _is_stx_checkout(directory: Path) -> bool:
    marker: Path = directory / "stx_zlib" / "CMakeLists.txt"
    return marker.is_file()


def _cached_value(build_dir: Path, variable: str) -> str:
    """A cache variable's value in a build's CMakeCache.txt, or "" when it is absent or empty."""
    cache: Path = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return ""
    line: str
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(variable + ":"):
            value: str = line.split("=", 1)[1].strip()
            return value
    return ""


def _cached_stx_override(build_dir: Path) -> Path | None:
    """The local stx checkout a build was configured with (CMakeCache.txt), or None."""
    value: str = _cached_value(build_dir, STX_OVERRIDE_VARIABLE)
    if value:
        return Path(value)
    return None


def _cached_stx_package_notices(build_dir: Path) -> Path | None:
    """The licence files of the stx package a build linked, at their stx source paths, or None.

    The package (cmake/stx_package/) installs them at build/share/stx_text/notices
    of the directory GUI_FORMS_STX_PREFIX or GUI_FORMS_STX_PACKAGE_DIR names; a
    build that reused its package fetched no stx checkout to read them from.
    """
    variable: str
    for variable in STX_PACKAGE_VARIABLES:
        value: str = _cached_value(build_dir, variable)
        if not value:
            continue
        notices: Path = Path(value) / "build" / "share" / "stx_text" / "notices"
        if notices.is_dir():
            return notices
    return None


def stx_lock_commit() -> str:
    """The stx commit stx.lock pins."""
    text: str = STX_LOCK_PATH.read_text(encoding="utf-8")
    found: re.Match[str] | None = re.search(r'^commit = "([0-9a-f]{40})"$', text, re.MULTILINE)
    if found is None:
        raise RuntimeError("stx.lock has no 40-character commit line")
    commit: str = found.group(1)
    return commit


def stx_source_directory(build_dir: Path | None, explicit: Path | None) -> Path | None:
    """The stx checkout whose notices a package ships, or None when there is none.

    In order: `explicit` (--stx-source), the GUI_FORMS_STX_SOURCE_DIR variable,
    then for the build directory and its toolkit/ subdirectory the local
    checkout the build was configured with (FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX),
    the checkout cmake/Stx.cmake fetched (_deps/gui_forms_stx-src) and, when
    the build reused its stx package and fetched nothing, the package's copy of
    the licence files at their stx paths (not a checkout, but every $STX notice
    resolves in it). The fetched checkout must be at stx.lock's commit; a named
    checkout is the caller's choice and is not compared. A named directory
    that is not an stx checkout raises.
    """
    named: list[Path] = []
    if explicit is not None:
        named.append(explicit)
    if os.environ.get("GUI_FORMS_STX_SOURCE_DIR"):
        named.append(Path(os.environ["GUI_FORMS_STX_SOURCE_DIR"]))
    builds: list[Path] = []
    if build_dir is not None:
        builds = [build_dir, build_dir / "toolkit"]
    build: Path
    for build in builds:
        override: Path | None = _cached_stx_override(build)
        if override is not None:
            named.append(override)
    candidate: Path
    for candidate in named:
        if not _is_stx_checkout(candidate):
            raise RuntimeError(f"{candidate} is not an stx checkout (no stx_zlib/CMakeLists.txt)")
        return candidate
    for build in builds:
        candidate = build / "_deps" / "gui_forms_stx-src"
        if not _is_stx_checkout(candidate):
            continue
        head: str = subprocess.check_output(["git", "-C", str(candidate), "rev-parse", "HEAD"], text=True).strip()
        pinned: str = stx_lock_commit()
        if head != pinned:
            raise RuntimeError(f"the stx checkout {candidate} is at {head}, not stx.lock's {pinned}")
        return candidate
    for build in builds:
        notices: Path | None = _cached_stx_package_notices(build)
        if notices is not None:
            return notices
    return None


def copy_notices(platform: str, build_dir: Path | None, destination: Path, runtime: Path | None = None,
                 runtime_dir: Path | None = None, stx_source: Path | None = None,
                 log: Callable[[str], None] = print) -> None:
    """Copy packaging/licenses.json's notices for platform into destination.

    runtime is the macOS LLVM runtime prefix ($GUI_FORMS_LLVM_RUNTIME),
    runtime_dir the Windows runtime directory (its parent is $MSYS_PREFIX);
    $STX and $STXLIB resolve through stx_source_directory and
    stxlib_license_directory. A missing optional notice is logged and
    skipped; a missing required one raises FileNotFoundError.
    """
    destination.mkdir(parents=True, exist_ok=True)
    data = json.loads(LICENSES_PATH.read_text(encoding="utf-8"))
    prefix_env: dict[str, Path | None] = {
        "$GUI_FORMS_LLVM_RUNTIME": runtime,
        "$MSYS_PREFIX": (runtime_dir.parent if runtime_dir else None),
        STX_PREFIX: stx_source_directory(build_dir, stx_source),
        STXLIB_PREFIX: stxlib_license_directory(build_dir),
    }
    for notice in data["notices"]:
        platforms = notice.get("platforms")
        if platforms and platform not in platforms:
            continue
        source_text: str = notice["source"]
        source: Path | None = ROOT / source_text
        for prefix, base in prefix_env.items():
            # The separator keeps $STX from matching $STXLIB paths.
            if source_text.startswith(prefix + "/"):
                source = (base / source_text[len(prefix) + 1:]) if base else None
        if source is None or not source.is_file():
            if notice.get("optional"):
                log(f"  notice {notice['name']} skipped: {source_text} is absent")
                continue
            raise FileNotFoundError(f"required notice {notice['name']} missing: {source_text}")
        target = destination / notice["name"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
