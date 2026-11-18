#!/usr/bin/env python3
"""Refuse platform headers outside the platform hosts.

GUI.Forms talks to the operating system through STXlib (the standard
augmentation and, later, the surface; owner, 2026-11-16) and through its
surface (src/surface/<platform>/, which becomes STXlib's surface after
1.0; the host consolidation, owner 2026-11-16). Only a file under
src/surface/<platform>/ may include an
operating-system, window-system or platform-framework header: pthread.h,
windows.h (and the rest of the Windows SDK), unistd.h, sys/*, dlfcn.h,
mach/*, mach-o/*, X11/*, Wayland, Cocoa, AppKit, Foundation, Core*
frameworks, emscripten/* and the others PLATFORM_HEADERS lists. The C and
C++ standard headers and compiler headers (sanitizer/*, the SIMD
intrinsics) are not platform headers.

Scanned: the library itself, include/ and src/, except
src/surface/<platform>/ for the platforms in PLATFORM_DIRECTORIES. All of
src/host/ is scanned: after the consolidation it is platform-neutral.
Tests, demonstrations, examples, tools and benchmarks are programs that may
drive a platform directly and are not scanned (check_std_rules governs
their standard-library use). So is src/abi/conformance/, the C ABI
conformance kit the SDK ships for consumers' own tests: it starts threads
and processes directly (moved from tests/ with the tests' move to
gui_forms_tests, 2026-11-17).

An include is a preprocessor line `#include`, `#include_next` or `#import`
naming a header in angle brackets or quotes; block comments are skipped.
The path prefixes (sys/, mach/, X11/ and the rest) are matched in angle
brackets only, the exact names in either form.
Matching ignores case for the Windows SDK's names (windows.h and
Windows.h are one file there).

The allowance (manifests/platform_header_allowance.json) lists files that
may still include platform headers, with their counts. It started empty
(2026-11-16) and only shrinks:
- `check` fails (exit 1) when a file has more platform includes than its
  allowance, or any and no entry, and also when an entry is larger than
  the file's count (lower it with `update`), so a moved include cannot
  come back;
- `update` rewrites the allowance to the tree's counts, and refuses to
  raise or add an entry;
- `scan` prints every platform include found.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parents[1]
DEFAULT_ALLOWANCE: Path = ROOT / "manifests" / "platform_header_allowance.json"
SCANNED: tuple[str, ...] = ("include", "src")
PLATFORM_DIRECTORIES: tuple[str, ...] = ("src/surface/windows", "src/surface/macos", "src/surface/linux",
                                         "src/surface/posix", "src/surface/web")
SOURCE_SUFFIXES: frozenset[str] = frozenset({".c", ".cc", ".cpp", ".h", ".hpp", ".inc", ".ipp", ".m", ".mm"})
DESCRIPTION: str = (
    "Files outside src/surface/<platform>/ that may still include a platform header, with their counts "
    "(tools/check_platform_headers.py). Shrink-only: lower or drop an entry as includes move to a host "
    "or to STXlib; never raise or add one.")

# Exact header names (compared in lower case).
PLATFORM_HEADERS: frozenset[str] = frozenset({
    # POSIX and the BSD and GNU extensions
    "pthread.h", "unistd.h", "dlfcn.h", "fcntl.h", "poll.h", "semaphore.h", "sched.h", "dirent.h", "termios.h",
    "pwd.h", "grp.h", "spawn.h", "link.h", "elf.h", "execinfo.h", "ucontext.h", "langinfo.h", "iconv.h",
    "netdb.h", "strings.h", "syslog.h", "glob.h", "fnmatch.h", "libgen.h", "aio.h", "mqueue.h", "utmpx.h",
    "nl_types.h", "malloc.h", "alloca.h", "features.h", "endian.h", "err.h", "getopt.h", "pty.h", "util.h",
    "libproc.h", "crt_externs.h", "cxxabi.h", "unwind.h",
    # The Windows SDK and the Windows C runtime's own headers
    "windows.h", "windef.h", "winbase.h", "winuser.h", "wingdi.h", "winnt.h", "winerror.h", "winreg.h",
    "winsock.h", "winsock2.h", "ws2tcpip.h", "psapi.h", "shlobj.h", "shellapi.h", "shlwapi.h", "objbase.h",
    "combaseapi.h", "ole2.h", "oleacc.h", "oleauto.h", "olectl.h", "comdef.h", "uiautomation.h", "dwmapi.h",
    "uxtheme.h", "commctrl.h", "commdlg.h", "wincodec.h", "d2d1.h", "dwrite.h", "d3d11.h", "dxgi.h",
    "tchar.h", "winternl.h", "ntstatus.h", "bcrypt.h", "wtsapi32.h", "versionhelpers.h", "processthreadsapi.h",
    "synchapi.h", "handleapi.h", "fileapi.h", "memoryapi.h", "sysinfoapi.h", "libloaderapi.h", "dbghelp.h",
    "tlhelp32.h", "io.h", "direct.h", "process.h", "conio.h", "share.h", "imm.h", "mmsystem.h", "timeapi.h",
    "avrt.h", "mmdeviceapi.h", "audioclient.h", "propvarutil.h", "propkey.h", "shobjidl.h", "knownfolders.h",
    "wrl.h", "intsafe.h", "strsafe.h", "lmcons.h", "sddl.h", "aclapi.h", "userenv.h", "powrprof.h",
    # Emscripten and Android
    "emscripten.h", "jni.h",
})

# Header path prefixes (compared in lower case).
PLATFORM_PREFIXES: tuple[str, ...] = (
    "sys/", "arpa/", "net/", "netinet/", "linux/", "asm/", "mach/", "mach-o/", "machine/", "libkern/", "os/",
    "dispatch/", "objc/", "x11/", "xcb/", "wayland-", "xkbcommon/", "libdrm/", "drm/", "gbm.h", "egl/", "gl/",
    "glx/", "vulkan/", "gtk/", "gdk/", "glib", "gio/", "gobject/", "atk/", "atspi/", "dbus/", "pango/",
    "cairo/", "fontconfig/", "cocoa/", "appkit/", "foundation/", "corefoundation/", "coretext/",
    "coregraphics/", "coreservices/", "coreaudio/", "coremedia/", "corevideo/", "quartzcore/",
    "applicationservices/", "iokit/", "carbon/", "metal/", "metalkit/", "uniformtypeidentifiers/",
    "security/", "systemconfiguration/", "avfoundation/", "audiotoolbox/", "accessibility/", "emscripten/",
    "android/", "winrt/",
)

INCLUDE_LINE: re.Pattern[str] = re.compile(
    r'^[ \t]*#[ \t]*(?:include|include_next|import)[ \t]*([<"])([^>"\n]+)[>"]', re.MULTILINE)
BLOCK_COMMENT: re.Pattern[str] = re.compile(r"/\*.*?\*/", re.DOTALL)


def is_platform_header(name: str, angle: bool = True) -> bool:
    """True when `name`, as written between the brackets, is a platform header.
    The prefixes apply to angle-bracket includes only: a quoted "net/..." or
    "os/..." may be a project's own directory."""
    lowered: str = name.strip().lower()
    if lowered in PLATFORM_HEADERS:
        return True
    if not angle:
        return False
    return any(lowered.startswith(prefix) for prefix in PLATFORM_PREFIXES)


def in_platform_directory(relative: str) -> bool:
    """True for a path (POSIX, relative to the root) under src/surface/<platform>/."""
    return any(relative == directory or relative.startswith(directory + "/") for directory in PLATFORM_DIRECTORIES)


def platform_includes(text: str) -> list[tuple[int, str]]:
    """The platform includes in a source text, as (line, header) pairs."""
    # Block comments become blank lines of the same count, so line numbers stay.
    def blank(match: re.Match[str]) -> str:
        return "\n" * match.group(0).count("\n")

    uncommented: str = BLOCK_COMMENT.sub(blank, text)
    found: list[tuple[int, str]] = []
    for match in INCLUDE_LINE.finditer(uncommented):
        angle: bool = match.group(1) == "<"
        header: str = match.group(2)
        if not is_platform_header(header, angle):
            continue
        line: int = uncommented.count("\n", 0, match.start()) + 1
        found.append((line, header))
    return found


# The C ABI conformance kit: a consumer-side test kit, not the library.
CONFORMANCE_KIT: str = "src/abi/conformance/"


def scanned_files(root: Path) -> list[Path]:
    """Every source file the check reads, sorted."""
    files: list[Path] = []
    for top in SCANNED:
        base: Path = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            relative: str = path.relative_to(root).as_posix()
            if in_platform_directory(relative) or relative.startswith(CONFORMANCE_KIT):
                continue
            files.append(path)
    files.sort()
    return files


def scan(root: Path) -> dict[str, list[tuple[int, str]]]:
    """Platform includes per file (relative POSIX path), files with none left out."""
    result: dict[str, list[tuple[int, str]]] = {}
    for path in scanned_files(root):
        text: str = path.read_text(encoding="utf-8", errors="replace")
        found: list[tuple[int, str]] = platform_includes(text)
        if found:
            result[path.relative_to(root).as_posix()] = found
    return result


def load_allowance(path: Path) -> dict[str, int]:
    if not path.exists():
        return {}
    data: dict[str, object] = json.loads(path.read_text(encoding="utf-8"))
    files: object = data.get("files", {})
    if not isinstance(files, dict):
        raise ValueError(f"{path}: 'files' is not an object")
    allowance: dict[str, int] = {}
    for name, count in files.items():
        if not isinstance(count, int) or count < 1:
            raise ValueError(f"{path}: {name} has a count that is not a positive integer")
        allowance[str(name)] = count
    return allowance


def write_allowance(path: Path, allowance: dict[str, int]) -> None:
    data: dict[str, object] = {"description": DESCRIPTION, "files": dict(sorted(allowance.items()))}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def check(root: Path, allowance_path: Path) -> list[str]:
    """The findings: lines naming each excess include or stale allowance entry."""
    found: dict[str, list[tuple[int, str]]] = scan(root)
    allowance: dict[str, int] = load_allowance(allowance_path)
    findings: list[str] = []
    for name, includes in sorted(found.items()):
        allowed: int = allowance.get(name, 0)
        if len(includes) <= allowed:
            continue
        for line, header in includes:
            findings.append(f"{name}:{line}: platform header <{header}> outside src/surface/<platform>/ "
                            f"(allowed {allowed}, found {len(includes)}); move the code to a host or use STXlib")
    for name, allowed in sorted(allowance.items()):
        count: int = len(found.get(name, []))
        if count < allowed:
            findings.append(f"{allowance_path.name}: {name} allows {allowed}, finds {count}: lower it (update)")
    return findings


def update(root: Path, allowance_path: Path) -> list[str]:
    """Lowers the allowance to the tree's counts; returns refusals (raises or additions)."""
    found: dict[str, list[tuple[int, str]]] = scan(root)
    allowance: dict[str, int] = load_allowance(allowance_path)
    refusals: list[str] = []
    lowered: dict[str, int] = {}
    for name, includes in sorted(found.items()):
        allowed: int = allowance.get(name, 0)
        if len(includes) > allowed:
            refusals.append(f"{name}: {len(includes)} platform includes, allowed {allowed}; the allowance only shrinks")
            continue
        lowered[name] = len(includes)
    if refusals:
        return refusals
    write_allowance(allowance_path, lowered)
    return refusals


def main(arguments: list[str]) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=("check", "update", "scan"))
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--allowance", type=Path, default=None)
    options: argparse.Namespace = parser.parse_args(arguments)
    root: Path = options.root.resolve()
    allowance_path: Path = options.allowance if options.allowance is not None else (
        root / "manifests" / "platform_header_allowance.json")
    if options.command == "scan":
        for name, includes in sorted(scan(root).items()):
            for line, header in includes:
                print(f"{name}:{line}: <{header}>")
        return 0
    if options.command == "update":
        refusals: list[str] = update(root, allowance_path)
        for refusal in refusals:
            print(refusal)
        return 1 if refusals else 0
    findings: list[str] = check(root, allowance_path)
    for finding in findings:
        print(finding)
    print(f"check_platform_headers: {len(scanned_files(root))} files, {len(findings)} findings", file=sys.stderr)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
