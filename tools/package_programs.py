#!/usr/bin/env python3
"""Package the built GUI.Forms programs for one platform.

Reads packaging/programs.json, finds what the build directory produced for each
enabled program, and writes per-program artifacts plus SHA256SUMS.txt and a
packages-<platform>.json receipt that tools/smoke_package.py and
tools/publish_tested_release.py consume.

  macOS    <display>.app → ditto zip and a pkgbuild installer (ad hoc signed)
  Windows  <display>/ folder → zip and an NSIS per-user installer
  Linux    <display>/ folder → tar.gz and a Debian package

A program whose binary the build directory lacks is skipped with a log line,
never an error: Linux mains are still arriving (W5.5) and a planned target may
not exist yet. --dry-run prints the plan without running any external tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform as host_platform
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import zipfile
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT: Path = Path(__file__).resolve().parents[1]
MANIFEST_PATH: Path = ROOT / "packaging/programs.json"
LICENSES_PATH: Path = ROOT / "packaging/licenses.json"
PLATFORMS: tuple[str, ...] = ("macos-arm64", "windows-x64", "linux-x64", "linux-arm64")
FONT_MODES: tuple[str, ...] = ("bundle-resources", "sibling")
IDENTIFIER_PATTERN: str = r"[a-z][a-z0-9-]*"
MACOS_MINIMUM: str = "14.0"
RECEIPT_SCHEMA: int = 1

WINDOWS_SYSTEM_DLLS: frozenset[str] = frozenset({
    "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "comdlg32.dll", "ole32.dll",
    "oleaut32.dll", "oleacc.dll", "comctl32.dll", "shell32.dll", "shlwapi.dll", "usp10.dll",
    "windowscodecs.dll", "ws2_32.dll", "version.dll", "imm32.dll", "dwmapi.dll", "ntdll.dll",
    "msvcrt.dll", "ucrtbase.dll", "bcrypt.dll", "secur32.dll", "rpcrt4.dll", "setupapi.dll",
    "winspool.drv", "winmm.dll", "crypt32.dll", "normaliz.dll", "uxtheme.dll", "msimg32.dll",
    "shcore.dll", "dbghelp.dll", "psapi.dll", "userenv.dll", "iphlpapi.dll",
    "uiautomationcore.dll",
})
# Compiler runtime DLLs no Windows package may import: every Windows binary
# links the LLVM runtimes and winpthreads statically (CMakeLists.txt), and a
# GCC runtime (libgcc_s, libstdc++, libgomp, libatomic, libssp, libquadmath)
# is never part of a GUI.Forms package.
WINDOWS_FORBIDDEN_RUNTIME_DLL: re.Pattern[str] = re.compile(
    r"(libgcc_s_[a-z0-9_]+-\d+|libstdc\+\+-\d+|libgomp-\d+|libatomic-\d+|libssp-\d+|libquadmath-\d+"
    r"|libc\+\+|libc\+\+abi|libunwind|libwinpthread-\d+)\.dll", re.IGNORECASE)
# The credit the FreeType License requires in the documentation of a binary
# that uses FreeType (FTL section 3); THIRD_PARTY_NOTICES.md carries it.
FREETYPE_CREDIT: str = ("Portions of this software are copyright \u00a9 2026 The FreeType Project "
                        "(www.freetype.org). All rights reserved.")
NOTICES_NAME: str = "THIRD_PARTY_NOTICES.md"
# The title every GNU GPL, LGPL and AGPL text begins with; a package carries
# none of them.
GPL_TITLE: re.Pattern[str] = re.compile(r"GNU (LESSER |LIBRARY |AFFERO )?GENERAL PUBLIC LICENSE")
LINUX_FALLBACK_DEPENDS: str = "libc6, libx11-6, libxcursor1, libxfixes3, libxrender1, libatk1.0-0t64 | libatk1.0-0, libatk-bridge2.0-0t64 | libatk-bridge2.0-0"


def check_package_notices(root: Path, platform: str, licenses: dict[str, Any], font_notices: list[str]) -> None:
    """Raise RuntimeError unless the package at `root` is covered by its notices file.

    See Packager.check_notices; a separate function so the tests can call it
    on a fixture package.
    """
    notices_path = root / "licenses" / NOTICES_NAME
    if not notices_path.is_file():
        raise RuntimeError(f"the package lacks licenses/{NOTICES_NAME}")
    notices = notices_path.read_text(encoding="utf-8")
    if FREETYPE_CREDIT not in notices:
        raise RuntimeError(f"licenses/{NOTICES_NAME} lacks the FreeType credit")
    licence_files: list[str] = []
    licences = root / "licenses"
    for path in sorted(licences.rglob("*")):
        if path.is_file() and path.name != NOTICES_NAME:
            licence_files.append("licenses/" + path.relative_to(licences).as_posix())
    for name in font_notices:
        if name != "ATTRIBUTION.md" and (root / "fonts" / name).is_file():
            licence_files.append("fonts/" + name)
    for name in licence_files:
        if name not in notices:
            raise RuntimeError(f"{name} is shipped but not named in {NOTICES_NAME}")
    for notice in licenses["notices"]:
        component = notice.get("component")
        platforms = notice.get("platforms")
        if component is None or (platforms and platform not in platforms):
            continue
        if f"| {component} |" not in notices:
            raise RuntimeError(f"{NOTICES_NAME} has no row for {component!r}")
    for path in sorted(root.rglob("*")):
        if "gpl" in path.name.lower():
            raise RuntimeError(f"the package contains {path.relative_to(root).as_posix()}, whose name says GPL")
        if not path.is_file() or path.suffix.lower() in (".exe", ".dll", ".dylib", ".so", ".ttf", ".otf", ".png", ".icns"):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        if GPL_TITLE.search(text):
            raise RuntimeError(f"{path.relative_to(root).as_posix()} carries a GNU General Public License text")


class ManifestError(ValueError):
    """packaging/programs.json does not describe a packageable program set."""


@dataclass(frozen=True)
class PlatformEntry:
    target: str
    executable: str
    bundle: str | None
    bundle_identifier: str | None
    fonts: str
    planned: bool

    def built_name(self) -> str:
        return self.bundle if self.bundle is not None else self.executable


@dataclass(frozen=True)
class Program:
    id: str
    display_name: str
    description: str
    enabled: bool
    status: str
    required_platforms: tuple[str, ...]
    automation_flag: str | None
    smoke: dict[str, Any]
    platforms: dict[str, PlatformEntry]


@dataclass(frozen=True)
class Manifest:
    vendor: dict[str, str]
    fonts: dict[str, list[str]]
    programs: tuple[Program, ...]

    def program(self, identifier: str) -> Program:
        for program in self.programs:
            if program.id == identifier:
                return program
        raise KeyError(identifier)


@dataclass(frozen=True)
class Plan:
    program: Program
    entry: PlatformEntry | None
    built: Path | None
    reason: str

    @property
    def packages(self) -> bool:
        return self.built is not None


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ManifestError(message)


def _string(record: dict[str, Any], key: str, where: str, optional: bool = False) -> str | None:
    if key not in record:
        _require(optional, f"{where}: missing '{key}'")
        return None
    value = record[key]
    _require(isinstance(value, str) and (optional or value != ""), f"{where}: '{key}' must be a non-empty string")
    return value


def validate_manifest(data: dict[str, Any]) -> Manifest:
    _require(isinstance(data, dict), "manifest must be a JSON object")
    _require(data.get("schema") == 1, "manifest: unsupported schema (expected 1)")
    vendor = data.get("vendor")
    _require(isinstance(vendor, dict), "manifest: 'vendor' must be an object")
    for key in ("name", "maintainer", "repository", "bundle_prefix", "deb_prefix", "windows_install_root",
                "windows_uninstall_prefix", "linux_install_root", "artifact_prefix"):
        _string(vendor, key, "vendor")
    fonts = data.get("fonts")
    _require(isinstance(fonts, dict), "manifest: 'fonts' must be an object")
    _string(fonts, "source", "fonts")
    for key in ("files", "notices"):
        files = fonts.get(key)
        _require(isinstance(files, list) and files and all(isinstance(name, str) and name for name in files),
                 f"fonts: '{key}' must be a non-empty list of file names")
    programs_data = data.get("programs")
    _require(isinstance(programs_data, list) and programs_data, "manifest: 'programs' must be a non-empty list")
    programs: list[Program] = []
    seen: set[str] = set()
    for index, record in enumerate(programs_data):
        where = f"programs[{index}]"
        _require(isinstance(record, dict), where + " must be an object")
        identifier = _string(record, "id", where) or ""
        _require(re.fullmatch(IDENTIFIER_PATTERN, identifier) is not None,
                 f"{where}: id {identifier!r} must match {IDENTIFIER_PATTERN}")
        _require(identifier not in seen, f"{where}: duplicate id {identifier!r}")
        seen.add(identifier)
        where = f"program {identifier!r}"
        display = _string(record, "display_name", where) or ""
        description = _string(record, "description", where) or ""
        _require(isinstance(record.get("enabled"), bool), where + ": 'enabled' must be a boolean")
        status = _string(record, "status", where, optional=True) or ""
        required = record.get("required_platforms", [])
        _require(isinstance(required, list) and all(name in PLATFORMS for name in required),
                 f"{where}: 'required_platforms' must list known platforms {PLATFORMS}")
        _require("automation_flag" in record, where + ": 'automation_flag' must be present (a flag or null)")
        automation = record["automation_flag"]
        _require(automation is None or (isinstance(automation, str) and automation.startswith("--")),
                 where + ": 'automation_flag' must be null or a --flag")
        smoke = record.get("smoke")
        _require(isinstance(smoke, dict), where + ": 'smoke' must be an object")
        _require(set(smoke).issubset({"extra_arguments"}), where + ": smoke holds only extra_arguments (every program runs --smoke-test)")
        extra = smoke.get("extra_arguments", [])
        _require(isinstance(extra, list) and all(isinstance(argument, str) for argument in extra),
                 where + ": smoke.extra_arguments must be a list of strings")
        platforms_data = record.get("platforms")
        _require(isinstance(platforms_data, dict) and platforms_data, where + ": 'platforms' must be a non-empty object")
        entries: dict[str, PlatformEntry] = {}
        for name, entry in platforms_data.items():
            _require(name in PLATFORMS, f"{where}: unknown platform {name!r}")
            entry_where = f"{where} platform {name}"
            _require(isinstance(entry, dict), entry_where + " must be an object")
            target = _string(entry, "target", entry_where) or ""
            executable = _string(entry, "executable", entry_where) or ""
            bundle = _string(entry, "bundle", entry_where, optional=True)
            bundle_identifier = _string(entry, "bundle_identifier", entry_where, optional=True)
            if name.startswith("macos"):
                _require(bundle is not None and bundle.endswith(".app"), entry_where + ": macOS needs a 'bundle' ending in .app")
                _require(bundle_identifier is not None, entry_where + ": macOS needs a 'bundle_identifier'")
                _require(executable.startswith("Contents/MacOS/"), entry_where + ": macOS executable is relative to the bundle (Contents/MacOS/...)")
            else:
                _require(bundle is None, entry_where + ": only macOS entries carry a bundle")
                _require("/" not in executable and "\\" not in executable, entry_where + ": executable is a bare file name")
                if name.startswith("windows"):
                    _require(executable.lower().endswith(".exe"), entry_where + ": Windows executable must end in .exe")
            mode = _string(entry, "fonts", entry_where) or ""
            _require(mode in FONT_MODES, f"{entry_where}: fonts must be one of {FONT_MODES}")
            _require(mode == "bundle-resources" or not name.startswith("macos"), entry_where + ": macOS fonts live in bundle resources")
            planned = entry.get("planned", False)
            _require(isinstance(planned, bool), entry_where + ": 'planned' must be a boolean")
            entries[name] = PlatformEntry(target, executable, bundle, bundle_identifier, mode, planned)
        for name in required:
            _require(name in entries, f"{where}: required platform {name} has no platform entry")
            _require(not entries[name].planned, f"{where}: required platform {name} cannot be a planned target")
        programs.append(Program(identifier, display, description, bool(record["enabled"]), status,
                                tuple(required), automation, dict(smoke), entries))
    return Manifest({key: str(value) for key, value in vendor.items()},
                    {"source": [fonts["source"]], "files": list(fonts["files"]), "notices": list(fonts["notices"])},
                    tuple(programs))


def load_manifest(path: Path = MANIFEST_PATH) -> Manifest:
    return validate_manifest(json.loads(path.read_text(encoding="utf-8")))


def artifact_names(manifest: Manifest, program: Program, version: str, platform: str) -> dict[str, str]:
    stem = f"{manifest.vendor['artifact_prefix']}-{program.id}-{version}-{platform}"
    if platform.startswith("macos"):
        return {"archive": stem + ".zip", "installer": stem + ".pkg"}
    if platform.startswith("windows"):
        return {"archive": stem + ".zip", "installer": stem + "-setup.exe"}
    return {"archive": stem + ".tar.gz", "installer": stem + ".deb"}


def locate_built(build_dir: Path, entry: PlatformEntry) -> Path | None:
    name = entry.built_name()
    direct = build_dir / name
    if direct.exists():
        return direct
    if not build_dir.is_dir():
        return None
    for candidate in sorted(build_dir.rglob(name)):
        if "CMakeFiles" not in candidate.parts and "stage" not in candidate.parts:
            return candidate
    return None


def make_plan(manifest: Manifest, platform: str, build_dir: Path) -> list[Plan]:
    plans: list[Plan] = []
    for program in manifest.programs:
        if not program.enabled:
            plans.append(Plan(program, None, None, "disabled in packaging/programs.json (" + (program.status or "no status") + ")"))
            continue
        entry = program.platforms.get(platform)
        if entry is None:
            plans.append(Plan(program, None, None, f"no {platform} entry in the manifest"))
            continue
        built = locate_built(build_dir, entry)
        if built is None:
            note = " (planned target; another workstream adds it)" if entry.planned else ""
            plans.append(Plan(program, entry, None, f"not built: target {entry.target} produced no {entry.built_name()!r} under {build_dir}{note}"))
            continue
        plans.append(Plan(program, entry, built, "package"))
    return plans


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_sums(out: Path, files: list[Path]) -> Path:
    sums = out / "SHA256SUMS.txt"
    sums.write_text("".join(sha256_file(path) + "  " + path.name + "\n" for path in sorted(files, key=lambda p: p.name)), encoding="utf-8")
    return sums


def placeholder_png(size: int = 256, rgb: tuple[int, int, int] = (0x2F, 0x5D, 0x8A)) -> bytes:
    """A flat-colour PNG icon placeholder until the programs carry real icons."""
    row = b"\x00" + bytes(rgb) * size
    raw = row * size

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def git_revision(explicit: str | None) -> str:
    if explicit:
        return explicit
    for variable in ("GITHUB_SHA",):
        if os.environ.get(variable):
            return os.environ[variable]
    try:
        return subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


# The stx libraries' notices ($STX/<package>/...) come from the stx checkout the build
# consumed (stx.lock; cmake/Stx.cmake), never from this tree, which holds no stx sources.
STX_PREFIX: str = "$STX"
STX_LOCK_PATH: Path = ROOT / "stx.lock"
STX_OVERRIDE_VARIABLE: str = "FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX"


def _is_stx_checkout(directory: Path) -> bool:
    marker: Path = directory / "stx_zlib" / "CMakeLists.txt"
    return marker.is_file()


def _cached_stx_override(build_dir: Path) -> Path | None:
    """The local stx checkout a build was configured with (CMakeCache.txt), or None."""
    cache: Path = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    line: str
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(STX_OVERRIDE_VARIABLE + ":"):
            value: str = line.split("=", 1)[1].strip()
            if value:
                return Path(value)
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
    checkout the build was configured with (FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX)
    and the checkout cmake/Stx.cmake fetched (_deps/gui_forms_stx-src). The
    fetched checkout must be at stx.lock's commit; a named checkout is the
    caller's choice and is not compared. A named directory that is not an stx
    checkout raises.
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
    return None


class Packager:
    def __init__(self, manifest: Manifest, platform: str, build_dir: Path, version: str, out: Path,
                 revision: str, dry_run: bool, runtime: Path | None, runtime_dir: Path | None,
                 installer: bool, log=print, stx_source: Path | None = None) -> None:
        self.manifest = manifest
        self.stx_source = stx_source
        self.platform = platform
        self.build_dir = build_dir
        self.version = version
        self.out = out
        self.revision = revision
        self.dry_run = dry_run
        self.runtime = runtime
        self.runtime_dir = runtime_dir
        self.installer = installer
        self.log = log
        self.produced: list[Path] = []

    # -- helpers -------------------------------------------------------------

    def run(self, command: list[str], **options: Any) -> str:
        self.log("  $ " + " ".join(command))
        if self.dry_run:
            return ""
        return subprocess.check_output(command, text=True, **options).strip()

    def tool(self, name: str) -> str | None:
        return shutil.which(name)

    def stage_root(self) -> Path:
        return self.out / "stage"

    def copy_fonts(self, destination: Path) -> None:
        source = ROOT / self.manifest.fonts["source"][0]
        destination.mkdir(parents=True, exist_ok=True)
        for name in self.manifest.fonts["files"] + self.manifest.fonts["notices"]:
            if (destination / name).is_file():
                continue
            origin = source / name
            if not origin.is_file():
                raise FileNotFoundError(f"font resource {name} missing from {source}")
            shutil.copy2(origin, destination / name)
        missing = [name for name in self.manifest.fonts["files"] if not (destination / name).is_file()]
        if missing:
            raise RuntimeError("fonts missing after staging: " + ", ".join(missing))

    def copy_notices(self, destination: Path) -> None:
        destination.mkdir(parents=True, exist_ok=True)
        data = json.loads(LICENSES_PATH.read_text(encoding="utf-8"))
        prefix_env: dict[str, Path | None] = {
            "$GUI_FORMS_LLVM_RUNTIME": self.runtime,
            "$MSYS_PREFIX": (self.runtime_dir.parent if self.runtime_dir else None),
            STX_PREFIX: stx_source_directory(self.build_dir, self.stx_source),
        }
        for notice in data["notices"]:
            platforms = notice.get("platforms")
            if platforms and self.platform not in platforms:
                continue
            source_text: str = notice["source"]
            source: Path | None = ROOT / source_text
            for prefix, base in prefix_env.items():
                if source_text.startswith(prefix):
                    source = (base / source_text[len(prefix) + 1:]) if base else None
            if source is None or not source.is_file():
                if notice.get("optional"):
                    self.log(f"  notice {notice['name']} skipped: {source_text} is absent")
                    continue
                raise FileNotFoundError(f"required notice {notice['name']} missing: {source_text}")
            target = destination / notice["name"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)

    def check_notices(self, root: Path) -> None:
        """Fail unless the package at `root` is covered by THIRD_PARTY_NOTICES.md.

        `root` holds licenses/ and fonts/ (a bundle, or a macOS app's
        Resources). The notices file must be in licenses/ and carry the
        FreeType credit; every licence file under licenses/ and every font
        licence in fonts/ must be named in it; every packaging/licenses.json
        component that applies to this platform must have a row in its
        table; and no file in the package may carry a GPL text or a name
        containing "GPL".
        """
        check_package_notices(root, self.platform, json.loads(LICENSES_PATH.read_text(encoding="utf-8")),
                              self.manifest.fonts["notices"])
        self.log(f"  notices checked: {NOTICES_NAME}, FreeType credit, licence files named, no GPL text")

    def write_readme(self, destination: Path, program: Program, launch: str) -> None:
        (destination / "README.txt").write_text(
            f"{program.display_name} {self.version}\n\n{program.description}\n\n"
            f"Launch: {launch}\n"
            "Fonts ship beside the program; no host fonts are read.\n"
            f"Source and release notes: https://github.com/{self.manifest.vendor['repository']}\n"
            "Third-party notices: licenses/THIRD_PARTY_NOTICES.md, licenses/ and the fonts' licence files.\n",
            encoding="utf-8")

    def write_package_manifest(self, destination: Path, program: Program, root: Path) -> None:
        files: dict[str, str] = {}
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.name != "package-manifest.json":
                files[path.relative_to(root).as_posix()] = sha256_file(path)
        record = {"program": program.id, "display_name": program.display_name, "version": self.version,
                  "platform": self.platform, "source_revision": self.revision, "status": program.status, "files": files}
        (destination / "package-manifest.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")

    # -- macOS ----------------------------------------------------------------

    def otool_rpaths(self, path: Path) -> list[str]:
        return re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.*?) \(offset", self.run(["otool", "-l", str(path)]))

    def otool_dependencies(self, path: Path) -> list[str]:
        return [line.strip().split(" (", 1)[0] for line in self.run(["otool", "-L", str(path)]).splitlines()[1:]]

    def otool_identifier(self, path: Path) -> str | None:
        lines = self.run(["otool", "-D", str(path)]).splitlines()[1:]
        return lines[0].strip() if lines else None

    def bundle_macos_dependencies(self, app: Path, executable: Path, original_executable: Path) -> set[Path]:
        """Copy the non-system dylib closure into Contents/Frameworks and point every image at it."""
        frameworks = app / "Contents/Frameworks"
        frameworks.mkdir(exist_ok=True)

        def expand(value: str, owner: Path) -> Path:
            return Path(value.replace("@loader_path", str(owner.parent)).replace("@executable_path", str(original_executable.parent)))

        def resolve(value: str, owner: Path) -> Path:
            if value.startswith("@rpath/"):
                relative = value[len("@rpath/"):]
                candidates = [expand(base, owner) / relative for base in self.otool_rpaths(owner)]
                candidates += [expand(base, original_executable) / relative for base in self.otool_rpaths(original_executable)]
                if self.runtime is not None:
                    candidates.append(self.runtime / "lib" / relative)
            else:
                candidates = [expand(value, owner)]
            for candidate in candidates:
                if candidate.is_absolute() and candidate.is_file():
                    return candidate.resolve()
            raise RuntimeError(f"unresolved dependency {value} in {owner}")

        pending: list[tuple[Path, Path]] = [(original_executable, executable)]
        seen: set[Path] = set()
        origins: dict[str, str] = {}
        while pending:
            original, target = pending.pop(0)
            if target in seen:
                continue
            seen.add(target)
            own_id = self.otool_identifier(original)
            needs_rpath = False
            for dependency in self.otool_dependencies(original):
                if dependency == own_id or dependency.startswith(("/System/", "/usr/lib/")):
                    continue
                needs_rpath = needs_rpath or dependency.startswith("@rpath/")
                source = resolve(dependency, original)
                name = Path(dependency).name
                destination = frameworks / name
                digest = sha256_file(source)
                if origins.get(name, digest) != digest:
                    raise RuntimeError("dependency basename collision: " + name)
                if name not in origins:
                    shutil.copy2(source, destination)
                    destination.chmod(0o755)
                    self.run(["install_name_tool", "-id", "@rpath/" + name, str(destination)])
                    origins[name] = digest
                    pending.append((source, destination))
                if not dependency.startswith("@rpath/"):
                    prefix = "@executable_path/../Frameworks/" if target == executable else "@loader_path/"
                    self.run(["install_name_tool", "-change", dependency, prefix + name, str(target)])
            # Build-tree rpaths (the LLVM runtime prefix) never ship; the bundled rpath
            # is only needed by images that reference @rpath/ libraries at all.
            bundled = "@executable_path/../Frameworks" if target == executable else "@loader_path"
            present = self.otool_rpaths(target)
            for path in present:
                if not path.startswith("@"):
                    self.run(["install_name_tool", "-delete_rpath", path, str(target)])
            if needs_rpath and bundled not in present:
                self.run(["install_name_tool", "-add_rpath", bundled, str(target)])
        for library in frameworks.iterdir():
            if library not in seen:
                library.unlink()
        for target in seen:
            own_id = self.otool_identifier(target)
            for dependency in self.otool_dependencies(target):
                if dependency == own_id or dependency.startswith(("/System/", "/usr/lib/")):
                    continue
                if not dependency.startswith(("@rpath/", "@loader_path/", "@executable_path/../Frameworks/")):
                    raise RuntimeError("nonportable dependency: " + dependency)
                if not (frameworks / Path(dependency).name).is_file():
                    raise RuntimeError("missing bundled dependency: " + dependency)
        if not any(frameworks.iterdir()):
            frameworks.rmdir()
        return seen

    def package_macos(self, plan: Plan) -> dict[str, str]:
        program, entry = plan.program, plan.entry
        assert entry is not None and plan.built is not None and entry.bundle is not None
        names = artifact_names(self.manifest, program, self.version, self.platform)
        stage = self.stage_root() / program.id
        app = stage / (program.display_name + ".app")
        self.log(f"  stage {plan.built} -> {app}")
        if self.dry_run:
            for step in ("copy bundle", "bundle dylib closure into Contents/Frameworks (otool/install_name_tool)",
                         "verify Contents/Resources/fonts, add licenses/, README.txt, package-manifest.json",
                         f"Info.plist: version {self.version}, LSMinimumSystemVersion {MACOS_MINIMUM}, display name",
                         "codesign --force --sign - each Mach-O, then --deep on the app; codesign --verify --deep --strict",
                         f"ditto -c -k --keepParent -> {names['archive']}",
                         f"pkgbuild --identifier {self.manifest.vendor['bundle_prefix']}.{program.id} --install-location /Applications -> {names['installer']}"):
                self.log("  would: " + step)
            return names
        if stage.exists():
            shutil.rmtree(stage)
        shutil.copytree(plan.built, app, symlinks=True)
        executable = app / entry.executable
        original_executable = plan.built / entry.executable
        if not executable.is_file():
            raise FileNotFoundError(f"bundle executable missing: {executable}")
        images = self.bundle_macos_dependencies(app, executable, original_executable)
        resources = app / "Contents/Resources"
        self.copy_fonts(resources / "fonts")
        self.copy_notices(resources / "licenses")
        self.write_readme(resources, program, f"open \"{program.display_name}.app\"")
        self.check_notices(resources)
        plist_path = app / "Contents/Info.plist"
        plist = plistlib.loads(plist_path.read_bytes())
        plist["CFBundleShortVersionString"] = self.version
        plist["CFBundleVersion"] = self.version
        plist["LSMinimumSystemVersion"] = MACOS_MINIMUM
        plist["CFBundleDisplayName"] = program.display_name
        plist["CFBundleName"] = program.display_name
        plist_path.write_bytes(plistlib.dumps(plist))
        self.write_package_manifest(resources, program, app)
        for image in sorted(images):
            self.run(["codesign", "--force", "--sign", "-", str(image)])
        audit = ROOT / "tools/audit_macos_minimum.py"
        if self.runtime is not None and audit.is_file():
            self.run([sys.executable, str(audit), str(app)])
        else:
            self.log("  macOS minimum audit skipped: no --runtime prefix (local build without the LLVM 22 toolchain file)")
        self.run(["codesign", "--force", "--deep", "--sign", "-", str(app)])
        self.run(["codesign", "--verify", "--deep", "--strict", str(app)])
        archive = self.out / names["archive"]
        archive.unlink(missing_ok=True)
        # --sequesterRsrc keeps AppleDouble sidecars in __MACOSX/ instead of beside the files.
        self.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive)])
        self.produced.append(archive)
        if self.installer:
            installer_root = stage / "installer-root"
            shutil.copytree(app, installer_root / app.name, symlinks=True)
            components = stage / "components.plist"
            self.run(["pkgbuild", "--analyze", "--root", str(installer_root), str(components)])
            records = plistlib.loads(components.read_bytes())
            for record in records:
                record["BundleIsRelocatable"] = False
            components.write_bytes(plistlib.dumps(records))
            installer = self.out / names["installer"]
            installer.unlink(missing_ok=True)
            self.run(["pkgbuild", "--root", str(installer_root), "--component-plist", str(components),
                      "--install-location", "/Applications",
                      "--identifier", f"{self.manifest.vendor['bundle_prefix']}.{program.id}",
                      "--version", self.version, str(installer)])
            self.produced.append(installer)
        return names

    # -- Windows ---------------------------------------------------------------

    @staticmethod
    def find_makensis() -> str:
        """Prefer the official NSIS (the GitHub Windows image ships 3.10); MSYS2's 3.13 pairs
        32-bit stubs with 64-bit plugins and rejects MUI pages."""
        roots = [os.environ.get("NSIS_HOME", ""), os.environ.get("ProgramFiles(x86)", ""),
                 os.environ.get("ProgramFiles", ""), "C:/Program Files (x86)", "C:/Program Files"]
        for root in roots:
            if not root:
                continue
            for candidate in (Path(root) / "makensis.exe", Path(root) / "NSIS" / "makensis.exe"):
                if candidate.is_file():
                    return str(candidate)
        return shutil.which("makensis") or "makensis"

    def windows_closure(self, bundle: Path, executable: Path) -> None:
        """Copy the program's non-system DLL imports from the build into the bundle.

        Every binary is inspected with objdump; a compiler runtime import
        (WINDOWS_FORBIDDEN_RUNTIME_DLL) fails the package.
        """
        objdump = self.tool("objdump") or self.tool("llvm-objdump")
        if objdump is None:
            raise RuntimeError("objdump (or llvm-objdump) is needed to check a Windows package's imports")
        candidates: dict[str, Path] = {}
        for directory in [self.build_dir, self.build_dir / "toolkit"]:
            if directory.is_dir():
                for path in directory.glob("*.dll"):
                    candidates.setdefault(path.name.lower(), path)
        pending = [executable]
        inspected: set[str] = set()
        while pending:
            binary = pending.pop()
            if binary.name.lower() in inspected:
                continue
            inspected.add(binary.name.lower())
            imports = self.run([objdump, "-p", str(binary)])
            names = re.findall(r"DLL Name:\s*(\S+)", imports)
            own = [name for name in names
                   if name.lower() not in WINDOWS_SYSTEM_DLLS and not name.lower().startswith(("api-ms-win-", "ext-ms-win-"))]
            self.log(f"  imports of {binary.name}: {len(names) - len(own)} system DLLs"
                     + (", and " + ", ".join(own) if own else "; no other DLL"))
            for name in names:
                key = name.lower()
                if key in WINDOWS_SYSTEM_DLLS or key.startswith(("api-ms-win-", "ext-ms-win-")):
                    continue
                if WINDOWS_FORBIDDEN_RUNTIME_DLL.fullmatch(name):
                    raise RuntimeError(f"{binary.name} imports the compiler runtime {name}; Windows binaries link "
                                       "the LLVM runtimes and winpthreads statically and never a GCC runtime")
                if key not in candidates:
                    raise RuntimeError(f"missing runtime import {name} for {binary.name}")
                target = bundle / name
                if not target.exists():
                    shutil.copy2(candidates[key], target)
                pending.append(target)

    def write_nsis_script(self, bundle: Path, program: Program, entry: PlatformEntry, installer: Path, script: Path) -> None:
        template = (ROOT / "packaging/windows_installer.nsi.in").read_text(encoding="utf-8")
        files: list[str] = []
        directories: list[str] = []
        for path in sorted(bundle.rglob("*")):
            relative = str(path.relative_to(bundle)).replace("/", "\\")
            if path.is_file():
                files.append(f'  Delete "$INSTDIR\\{relative}"')
            elif path.is_dir():
                directories.append(relative)
        removals = [f'  RMDir "$INSTDIR\\{relative}"' for relative in sorted(directories, key=len, reverse=True)]
        key = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + self.manifest.vendor["windows_uninstall_prefix"] + program.id
        values = {
            "@DISPLAY_NAME@": program.display_name,
            "@OUTFILE@": str(installer).replace("/", "\\"),
            "@INSTALL_DIR@": self.manifest.vendor["windows_install_root"] + "\\" + program.id,
            "@VERSION@": self.version,
            "@VERSION4@": self.version + ".0",
            "@VENDOR@": self.manifest.vendor["name"],
            "@BUNDLE@": str(bundle).replace("/", "\\"),
            "@EXECUTABLE@": entry.executable,
            "@UNINSTALL_KEY@": key,
            "@UNINSTALL_FILES@": "\n".join(files),
            "@UNINSTALL_DIRECTORIES@": "\n".join(removals),
        }
        for placeholder, value in values.items():
            template = template.replace(placeholder, value)
        leftovers = re.findall(r"@[A-Z0-9_]+@", template.split("\n", 1)[1])
        if leftovers:
            raise RuntimeError("unfilled NSIS placeholders: " + ", ".join(sorted(set(leftovers))))
        script.write_text(template, encoding="utf-8")

    def package_windows(self, plan: Plan) -> dict[str, str]:
        program, entry = plan.program, plan.entry
        assert entry is not None and plan.built is not None
        names = artifact_names(self.manifest, program, self.version, self.platform)
        stage = self.stage_root() / program.id
        bundle = stage / program.display_name
        self.log(f"  stage {plan.built} -> {bundle}")
        if self.dry_run:
            for step in ("copy executable; copy non-system DLL imports (objdump) from the build dir; "
                         "a compiler runtime import (GCC, libc++, libunwind, winpthreads) fails",
                         "strip --strip-unneeded when available",
                         "fonts/ from the build's post-build copy or assets/fonts; licenses/, README.txt, package-manifest.json",
                         f"zip with top-level folder {program.display_name!r} -> {names['archive']}",
                         f"makensis packaging/windows_installer.nsi.in -> {names['installer']} "
                         f"(installs to {self.manifest.vendor['windows_install_root']}\\{program.id}, HKCU uninstall key)"):
                self.log("  would: " + step)
            return names
        if stage.exists():
            shutil.rmtree(stage)
        bundle.mkdir(parents=True)
        executable = bundle / entry.executable
        shutil.copy2(plan.built, executable)
        self.windows_closure(bundle, executable)
        strip = self.tool("strip") or self.tool("llvm-strip")
        if strip:
            for binary in bundle.iterdir():
                if binary.suffix.lower() in (".exe", ".dll"):
                    self.run([strip, "--strip-unneeded", str(binary)])
        built_fonts = plan.built.parent / "fonts"
        if built_fonts.is_dir():
            shutil.copytree(built_fonts, bundle / "fonts", dirs_exist_ok=True)
        self.copy_fonts(bundle / "fonts")
        self.copy_notices(bundle / "licenses")
        self.write_readme(bundle, program, entry.executable)
        self.check_notices(bundle)
        self.write_package_manifest(bundle, program, bundle)
        archive = self.out / names["archive"]
        archive.unlink(missing_ok=True)
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as compressed:
            for path in sorted(bundle.rglob("*")):
                if path.is_file():
                    compressed.write(path, program.display_name + "/" + path.relative_to(bundle).as_posix())
        self.produced.append(archive)
        if self.installer:
            installer = self.out / names["installer"]
            installer.unlink(missing_ok=True)
            script = stage / "installer.nsi"
            self.write_nsis_script(bundle, program, entry, installer, script)
            self.run([self.find_makensis(), str(script)])
            self.produced.append(installer)
        return names

    # -- Linux -----------------------------------------------------------------

    def linux_closure(self, bundle: Path, executable: Path) -> list[Path]:
        patchelf = self.tool("patchelf")
        binaries = [executable]
        if patchelf is None:
            self.log("  shared-library closure not checked: patchelf is not on PATH")
            return binaries
        pending = [executable]
        while pending:
            binary = pending.pop()
            for name in self.run([patchelf, "--print-needed", str(binary)]).splitlines():
                if name.startswith("libgui_forms_") and not (bundle / name).exists():
                    source = next(iter(sorted(self.build_dir.rglob(name))), None)
                    if source is None:
                        raise RuntimeError(f"{binary.name} needs {name}, which the build directory lacks")
                    shutil.copy2(source, bundle / name)
                    binaries.append(bundle / name)
                    pending.append(bundle / name)
        for binary in binaries:
            self.run([patchelf, "--set-rpath", "$ORIGIN", str(binary)])
        strip = self.tool("strip")
        for binary in binaries:
            if strip:
                self.run([strip, "--strip-unneeded", str(binary)])
            if self.tool("ldd") and "not found" in self.run(["ldd", str(binary)]):
                raise RuntimeError(f"{binary.name} has unresolved shared libraries")
        return binaries

    def package_linux(self, plan: Plan) -> dict[str, str]:
        program, entry = plan.program, plan.entry
        assert entry is not None and plan.built is not None
        names = artifact_names(self.manifest, program, self.version, self.platform)
        deb_name = f"{self.manifest.vendor['deb_prefix']}-{program.id}"
        install_dir = f"{self.manifest.vendor['linux_install_root']}/{program.id}"
        arch = "arm64" if self.platform.endswith("arm64") else "amd64"
        stage = self.stage_root() / program.id
        bundle = stage / program.display_name
        self.log(f"  stage {plan.built} -> {bundle}")
        if self.dry_run:
            for step in ("copy executable; follow libgui_forms_*.so imports (patchelf), rpath $ORIGIN, strip, ldd check",
                         "fonts/ from assets/fonts; licenses/, README.txt, package-manifest.json",
                         f"tar.gz with top-level folder {program.display_name!r} -> {names['archive']}",
                         f"deb {deb_name} ({arch}) installing to {install_dir}, launcher /usr/bin/{deb_name}, .desktop entry, icon placeholder -> {names['installer']}"):
                self.log("  would: " + step)
            return names
        if stage.exists():
            shutil.rmtree(stage)
        bundle.mkdir(parents=True)
        executable = bundle / entry.executable
        shutil.copy2(plan.built, executable)
        executable.chmod(0o755)
        binaries = self.linux_closure(bundle, executable)
        self.copy_fonts(bundle / "fonts")
        self.copy_notices(bundle / "licenses")
        self.write_readme(bundle, program, "./" + entry.executable + " (X11 display required)")
        self.check_notices(bundle)
        self.write_package_manifest(bundle, program, bundle)
        archive = self.out / names["archive"]
        archive.unlink(missing_ok=True)
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(bundle, arcname=program.display_name)
        self.produced.append(archive)
        if not self.installer:
            return names
        root = stage / "deb-root"
        installed = root / install_dir.lstrip("/")
        shutil.copytree(bundle, installed)
        launcher = root / "usr/bin" / deb_name
        launcher.parent.mkdir(parents=True)
        launcher.write_text(f'#!/bin/sh\nexec "{install_dir}/{entry.executable}" "$@"\n', encoding="utf-8")
        launcher.chmod(0o755)
        desktop = root / "usr/share/applications" / (deb_name + ".desktop")
        desktop.parent.mkdir(parents=True)
        desktop_template = (ROOT / "packaging/linux_desktop.in").read_text(encoding="utf-8")
        for placeholder, value in {"@DISPLAY_NAME@": program.display_name, "@SUMMARY@": program.description.split(".")[0],
                                   "@LAUNCHER@": deb_name, "@ICON@": deb_name}.items():
            desktop_template = desktop_template.replace(placeholder, value)
        desktop.write_text(desktop_template, encoding="utf-8")
        icon = root / "usr/share/icons/hicolor/256x256/apps" / (deb_name + ".png")
        icon.parent.mkdir(parents=True)
        icon.write_bytes(placeholder_png())
        depends = LINUX_FALLBACK_DEPENDS
        if self.tool("dpkg-shlibdeps"):
            metadata = stage / "debian/control"
            metadata.parent.mkdir()
            metadata.write_text(f"Source: {deb_name}\nSection: x11\nPriority: optional\nMaintainer: {self.manifest.vendor['maintainer']}\n\n"
                                f"Package: {deb_name}\nArchitecture: any\nDescription: {program.display_name}\n", encoding="utf-8")
            output = self.run(["dpkg-shlibdeps", "-O", "-l" + str(installed), "--ignore-missing-info"] +
                              ["-e" + str(installed / binary.relative_to(bundle)) for binary in binaries], cwd=stage)
            depends = output.removeprefix("shlibs:Depends=").strip() or depends
        else:
            self.log("  dpkg-shlibdeps not on PATH; using the fallback Depends list")
        control_template = (ROOT / "packaging/deb_control.in").read_text(encoding="utf-8")
        for placeholder, value in {"@PACKAGE@": deb_name, "@VERSION@": self.version, "@ARCH@": arch,
                                   "@MAINTAINER@": self.manifest.vendor["maintainer"], "@REPOSITORY@": self.manifest.vendor["repository"],
                                   "@DEPENDS@": depends, "@SUMMARY@": program.display_name,
                                   "@DESCRIPTION@": program.description, "@INSTALL_DIR@": install_dir}.items():
            control_template = control_template.replace(placeholder, value)
        control = root / "DEBIAN/control"
        control.parent.mkdir()
        control.write_text(control_template, encoding="utf-8")
        installer = self.out / names["installer"]
        installer.unlink(missing_ok=True)
        self.run(["dpkg-deb", "--root-owner-group", "--build", str(root), str(installer)])
        self.produced.append(installer)
        return names

    # -- driver ------------------------------------------------------------------

    def package(self, plan: Plan) -> dict[str, str]:
        if self.platform.startswith("macos"):
            return self.package_macos(plan)
        if self.platform.startswith("windows"):
            return self.package_windows(plan)
        return self.package_linux(plan)


def required_tools(platform: str) -> list[str]:
    if platform.startswith("macos"):
        return ["otool", "install_name_tool", "codesign", "ditto", "pkgbuild"]
    if platform.startswith("windows"):
        return ["objdump", "strip", "makensis"]
    return ["patchelf", "strip", "ldd", "dpkg-shlibdeps", "dpkg-deb"]


def detect_platform() -> str:
    system = host_platform.system()
    arm = host_platform.machine().lower() in ("arm64", "aarch64")
    if system == "Darwin":
        return "macos-arm64"
    if system == "Windows":
        return "windows-x64"
    return "linux-arm64" if arm else "linux-x64"


def receipt_record(plan: Plan, names: dict[str, str] | None, out: Path, installer: bool, vendor: dict[str, str]) -> dict[str, Any]:
    record: dict[str, Any] = {"id": plan.program.id, "display_name": plan.program.display_name,
                              "status": "packaged" if plan.packages else "skipped", "reason": plan.reason,
                              "automation_flag": plan.program.automation_flag, "smoke": plan.program.smoke}
    if plan.entry is not None:
        record["executable"] = plan.entry.executable
        record["bundle_identifier"] = plan.entry.bundle_identifier
        record["target"] = plan.entry.target
    if names is not None:
        artifacts = {"archive": names["archive"]}
        if installer:
            artifacts["installer"] = names["installer"]
        record["artifacts"] = artifacts
        record["install"] = {
            "macos_app": plan.program.display_name + ".app",
            "windows_dir": vendor["windows_install_root"] + "\\" + plan.program.id,
            "linux_dir": f"{vendor['linux_install_root']}/{plan.program.id}",
            "deb_package": f"{vendor['deb_prefix']}-{plan.program.id}",
            "folder": plan.program.display_name,
        }
    return record


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", choices=PLATFORMS, default=None, help="defaults to the host")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=MANIFEST_PATH)
    parser.add_argument("--revision", default=None, help="source sha recorded in package manifests (default: GITHUB_SHA or git HEAD)")
    parser.add_argument("--runtime", type=Path, default=None,
                        help="macOS: LLVM 22 runtime prefix whose lib/ supplies libc++ (default $GUI_FORMS_LLVM_RUNTIME)")
    parser.add_argument("--runtime-dir", type=Path, default=None,
                        help="Windows: the CLANG64 bin directory (for example /clang64/bin); its prefix supplies the "
                             "licences of the statically linked runtimes ($MSYS_PREFIX in packaging/licenses.json). "
                             "No DLL is copied from it.")
    parser.add_argument("--stx-source", type=Path, default=None,
                        help="the stx checkout whose notices ship (default: the one the build fetched at stx.lock)")
    parser.add_argument("--no-installer", action="store_true", help="archives only; skip pkg/setup.exe/deb")
    parser.add_argument("--dry-run", action="store_true", help="print the plan; run no external tool and write nothing")
    args = parser.parse_args(argv)

    from publication_version import validate
    validate(args.version)
    manifest = load_manifest(args.manifest)
    platform = args.platform or detect_platform()
    runtime = args.runtime or (Path(os.environ["GUI_FORMS_LLVM_RUNTIME"]) if os.environ.get("GUI_FORMS_LLVM_RUNTIME") else None)
    build_dir = args.build_dir.resolve() if args.build_dir.exists() else args.build_dir
    out = args.out.resolve() if not args.dry_run else args.out
    revision = git_revision(args.revision)
    plans = make_plan(manifest, platform, build_dir)

    print(f"GUI.Forms packaging: platform {platform}, version {args.version}, revision {revision[:12]}, build {build_dir}, out {out}"
          + (" [dry run]" if args.dry_run else ""))
    if args.dry_run:
        for tool in required_tools(platform):
            print(f"  tool {tool}: {'present' if shutil.which(tool) else 'absent (only needed for a real run)'}")
    packager = Packager(manifest, platform, build_dir, args.version, out, revision, args.dry_run,
                        runtime, args.runtime_dir, not args.no_installer, stx_source=args.stx_source)
    records: list[dict[str, Any]] = []
    for plan in plans:
        if not plan.packages:
            print(f"- {plan.program.display_name} [{plan.program.id}]: skipped: {plan.reason}")
            records.append(receipt_record(plan, None, out, not args.no_installer, manifest.vendor))
            continue
        print(f"- {plan.program.display_name} [{plan.program.id}]: packaging from {plan.built}")
        names = packager.package(plan)
        records.append(receipt_record(plan, names, out, not args.no_installer, manifest.vendor))
        for kind, name in names.items():
            if kind == "installer" and args.no_installer:
                continue
            print(f"    {kind}: {name}")
    receipt = {"schema": RECEIPT_SCHEMA, "platform": platform, "version": args.version, "revision": revision,
               "programs": records, "artifacts": sorted(path.name for path in packager.produced)}
    if args.dry_run:
        print("receipt (not written):")
        print(json.dumps(receipt, indent=2))
        return 0
    out.mkdir(parents=True, exist_ok=True)
    if packager.produced:
        sums = write_sums(out, packager.produced)
        print(f"wrote {sums}")
    else:
        print("nothing packaged on this platform; no SHA256SUMS.txt written")
    receipt_path = out / f"packages-{platform}.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {receipt_path}")
    stage = packager.stage_root()
    if stage.exists():
        shutil.rmtree(stage)
    return 0


if __name__ == "__main__":
    sys.exit(main())
