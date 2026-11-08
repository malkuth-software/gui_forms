#!/usr/bin/env python3
"""Archive the GUI.Forms source SDK: the release's tree plus its pinned third-party sources.

One platform-neutral archive, gui-forms-sdk-<version>-source.tar.gz, from
which a consumer builds GUI.Forms for its own host with tools/build_sdk.py:

  gui-forms-sdk-<version>/
    ...                         the release commit's tree (git archive)
    stx/                        the stx repository at stx.lock's commit: the stx packages,
                                stx_skia and every third-party source (third_party/), which
                                build_sdk.py hands to the configure (FETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX)
    third_party/miniaudio/      miniaudio at cmake/Audio.cmake's pin
    SDK_MANIFEST.json           the build entry point, every vendored source with
                                its pin and tree digest, what a build still
                                downloads (network_inputs) or needs installed
                                (prebuilt_inputs), and the compiler-cache
                                release built from the same tree (compiler_cache)

Each vendored source sits at the in-tree path its fetch script or CMake file
reads first, so configuring from the archive fetches none of them. Pins are
read from those files, never repeated here. The archive is reproducible:
entries in name order, one fixed time, no owner.

compiler_cache names the build-<revision> pre-release whose per-platform
ccache archives CI compiled from this tree, and the layout a build must use
to hit them. Each platform's native job of the push to main that tested the
revision publishes its archive there as soon as it passes, and the release
that carries this SDK is published only after all four have passed, so a
release packaged in that run (--compiler-cache) has all four. A packaging
outside that run (a tag build) cannot promise them, and records release null.

tools/publish_tested_release.py expects the archive under sdk_archive_name()
and lists it in SHA256SUMS.txt. --check reads an existing archive and checks
its shape and every vendored digest.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import re
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

ROOT: Path = Path(__file__).resolve().parents[1]
MANIFEST_NAME: str = "SDK_MANIFEST.json"
MANIFEST_SCHEMA: int = 1
BUILD_ENTRY: str = "tools/build_sdk.py"
# The compiler-cache pre-release of a revision, and its per-platform asset
# (template_harness harness/publish_cache.py with projects/gui_forms.toml).
CACHE_RELEASE_PREFIX: str = "build-"
CACHE_ASSET_PATTERN: str = "gui-forms-cache-{platform}.tar.gz"
CACHE_PLATFORMS: tuple[str, ...] = ("macos-arm64", "windows-x64", "linux-x64", "linux-arm64")
# Every entry gets this time stamp and no owner, so two packagings of the same
# commit and pins are byte-identical (1980-01-01, as zip's earliest time).
ARCHIVE_EPOCH: int = 315532800
# What an archive may never carry: a Git directory or anything a build writes.
FORBIDDEN_COMPONENTS: frozenset[str] = frozenset({".git", ".build", "CMakeFiles"})
FORBIDDEN_NAMES: frozenset[str] = frozenset({"CMakeCache.txt", "build.ninja", "install_manifest.txt"})
FORBIDDEN_SUFFIXES: frozenset[str] = frozenset({".o", ".obj", ".a", ".lib", ".dll", ".exe", ".dylib", ".so", ".pdb"})
REVISION_PATTERN: str = r"[0-9a-f]{40}"


@dataclass(frozen=True)
class VendoredSource:
    """One pinned third-party source the archive carries.

    path is where the build reads it, relative to the top directory;
    pin_file and pin_pattern locate its pinned commit (the pattern's one
    group); excluded lists path prefixes inside the source left out because
    no GUI.Forms build reads them.
    """
    name: str
    path: str
    upstream: str
    pin_file: str
    pin_pattern: str
    licence: str
    excluded: tuple[str, ...]


# stx carries stx_skia and every third-party source GUI.Forms builds (owner,
# 2026-11-05); its workflows are left out, as no build reads them.
VENDORED: tuple[VendoredSource, ...] = (
    VendoredSource("stx", "stx", "https://github.com/malkuth-software/stx.git",
                   "stx.lock", r'^commit = "(' + REVISION_PATTERN + r')"$',
                   "LICENSE", (".github/",)),
    VendoredSource("miniaudio", "third_party/miniaudio", "https://github.com/mackron/miniaudio.git",
                   "cmake/Audio.cmake", r"^\s*GIT_TAG (" + REVISION_PATTERN + r")$",
                   "LICENSE", ()),
)


@dataclass
class Entry:
    """One archive member: a regular file (data) or a symbolic link (link)."""
    data: bytes | None
    link: str | None
    executable: bool


def sdk_directory_name(version: str) -> str:
    """The archive's single top directory."""
    return "gui-forms-sdk-" + version


def sdk_archive_name(version: str) -> str:
    """The release asset name of the source SDK."""
    return sdk_directory_name(version) + "-source.tar.gz"


def pinned_revision(root: Path, source: VendoredSource) -> str:
    """source's pinned commit, read from its pin file under root."""
    text: str = (root / source.pin_file).read_text(encoding="utf-8")
    pins: list[str] = re.findall(source.pin_pattern, text, re.MULTILINE)
    if len(pins) != 1:
        raise ValueError(f"{source.pin_file} pins {source.name} {len(pins)} times, not once")
    return pins[0]


def is_build_output(relative: str) -> bool:
    """Whether a path inside the top directory is a Git directory or build output."""
    parts: tuple[str, ...] = PurePosixPath(relative).parts
    part: str
    for part in parts:
        if part in FORBIDDEN_COMPONENTS:
            return True
    name: str = parts[-1]
    return name in FORBIDDEN_NAMES or PurePosixPath(name).suffix in FORBIDDEN_SUFFIXES


def read_tar_members(tar: tarfile.TarFile, where: str) -> dict[str, Entry]:
    """The files and links of an open tar, by name; directories left out."""
    entries: dict[str, Entry] = {}
    member: tarfile.TarInfo
    for member in tar.getmembers():
        if member.isdir():
            continue
        if member.issym():
            entries[member.name] = Entry(None, member.linkname, False)
            continue
        if not member.isfile():
            raise ValueError(f"{where}: {member.name} is neither a file nor a link")
        stream: Any = tar.extractfile(member)
        entries[member.name] = Entry(stream.read(), None, bool(member.mode & 0o111))
    return entries


def git_tree(checkout: Path, revision: str) -> dict[str, Entry]:
    """The files and links of revision in the Git checkout, by path.

    Reads `git archive`, so the result is the commit's tree whatever the
    checkout's working files are; submodules are not included.
    """
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(
        ["git", "-C", str(checkout), "archive", "--format=tar", revision], check=True, capture_output=True)
    with tarfile.open(fileobj=io.BytesIO(completed.stdout), mode="r:") as tar:
        return read_tar_members(tar, f"{checkout} {revision}")


def tree_digest(entries: dict[str, Entry]) -> str:
    """SHA-256 over every path, kind, executable bit and content of entries, in path order."""
    digest: Any = hashlib.sha256()
    path: str
    for path in sorted(entries):
        entry: Entry = entries[path]
        digest.update(path.encode("utf-8") + b"\0")
        if entry.link is not None:
            digest.update(b"link\0" + entry.link.encode("utf-8") + b"\0")
        else:
            digest.update(b"file\0" + (b"x" if entry.executable else b"-") + b"\0")
            digest.update(hashlib.sha256(entry.data or b"").digest())
    return digest.hexdigest()


def without_excluded(entries: dict[str, Entry], excluded: tuple[str, ...]) -> dict[str, Entry]:
    """entries less those under any excluded prefix."""
    kept: dict[str, Entry] = {}
    path: str
    for path in entries:
        if not path.startswith(excluded):
            kept[path] = entries[path]
    return kept


def network_inputs(root: Path) -> list[dict[str, Any]]:
    """What a source build still downloads, by platform, with each pin."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from macos_runtime_manifest import SOURCE_SHA256, VERSION
    return [
        {"name": "llvm-runtimes-source", "platforms": ["macos-arm64"],
         "fetched_by": "template_harness harness/build_macos_runtimes.py (the commit harness.lock pins)",
         "upstream": "https://github.com/llvm/llvm-project/releases/download/llvmorg-" + VERSION +
                     "/llvm-project-" + VERSION + ".src.tar.xz",
         "sha256": SOURCE_SHA256,
         "note": "only to build the macOS 14 libc++ runtime once (GUI_FORMS_LLVM_RUNTIME)"},
    ]


PREBUILT_INPUTS: tuple[dict[str, Any], ...] = (
    {"name": "llvm-22.1-toolchain", "platforms": ["macos-arm64", "windows-x64", "linux-x64", "linux-arm64"],
     "source": "macOS: Homebrew llvm@22; Linux: apt.llvm.org clang-22; Windows: MSYS2 CLANG64 with the "
               "packages in template_harness harness/windows_toolchain.json (the commit harness.lock pins)",
     "note": "cmake/llvm22.cmake requires LLVM 22.1; CMake 3.25 or newer, Ninja, Git and Python 3.12 or "
             "newer are installed beside it"},
)


def compiler_cache(revision: str, published: bool) -> dict[str, Any]:
    """The manifest's compiler_cache record: the build-<revision> pre-release when this packaging runs in the
    push to main that publishes it (published), else release null."""
    release: str | None = CACHE_RELEASE_PREFIX + revision if published else None
    note: str = ("published by each platform's native job of the push to main that tested this revision, "
                 "before this release; the source stays authoritative, so a missing or mismatched archive "
                 "only means compiling more")
    if not published:
        note = "this SDK was packaged outside the push to main that publishes the revision's compiler caches"
    return {"release": release, "asset": CACHE_ASSET_PATTERN, "platforms": list(CACHE_PLATFORMS),
            "contents": ".ccache/ (CCACHE_DIR) and cache-manifest.json, for extraction into the workspace",
            "layout": {"ccache_basedir": "the workspace", "source": "gui_forms",
                       "work": ".build/native-{platform}",
                       "note": "unpack the SDK's top directory as <workspace>/gui_forms and run build_sdk.py "
                               "with --work <workspace>/.build/native-<platform>, CCACHE_BASEDIR=<workspace> "
                               "and CCACHE_COMPILERCHECK=content, as CI compiles"},
            "note": note}


def manifest(root: Path, version: str, revision: str, vendored: list[dict[str, Any]],
             published_cache: bool = False) -> dict[str, Any]:
    """The SDK_MANIFEST.json record."""
    return {"schema": MANIFEST_SCHEMA, "version": version, "revision": revision,
            "top_directory": sdk_directory_name(version), "build": BUILD_ENTRY,
            "third_party": vendored, "network_inputs": network_inputs(root),
            "prebuilt_inputs": list(PREBUILT_INPUTS),
            "compiler_cache": compiler_cache(revision, published_cache)}


def has_commit(checkout: Path, revision: str) -> bool:
    """Whether checkout is a Git repository holding revision."""
    if not checkout.is_dir():
        return False
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(
        ["git", "-C", str(checkout), "cat-file", "-e", revision + "^{commit}"], capture_output=True)
    return completed.returncode == 0


def fetch_pin(source: VendoredSource, revision: str, work: Path) -> Path:
    """A new shallow Git repository under work holding source's pinned revision from its upstream."""
    checkout: Path = work / source.name
    subprocess.run(["git", "init", "-q", str(checkout)], check=True)
    subprocess.run(["git", "-C", str(checkout), "fetch", "-q", "--depth=1", source.upstream, revision], check=True)
    return checkout


def collect(root: Path, version: str, revision: str, checkouts: dict[str, Path],
            fetch_work: Path | None = None, published_cache: bool = False) -> dict[str, Entry]:
    """Every member of the archive, by its full name under the top directory.

    A vendored source is read from checkouts[name], else from its in-tree
    path; when neither holds its pin, fetch_work (if given) receives a
    shallow fetch of the pin from its upstream, else ValueError is raised.
    published_cache: this packaging runs where the revision's compiler caches
    are published (manifest's compiler_cache).
    """
    top: str = sdk_directory_name(version)
    tree: dict[str, Entry] = git_tree(root, revision)
    members: dict[str, Entry] = {}
    path: str
    for path in tree:
        members[top + "/" + path] = tree[path]
    vendored: list[dict[str, Any]] = []
    source: VendoredSource
    for source in VENDORED:
        pin: str = pinned_revision(root, source)
        checkout: Path = checkouts.get(source.name, root / source.path)
        if not has_commit(checkout, pin):
            if fetch_work is None:
                raise ValueError(f"{checkout} does not hold {source.name}'s pin {pin}; pass --checkout "
                                 "or --fetch-missing")
            checkout = fetch_pin(source, pin, fetch_work)
        entries: dict[str, Entry] = without_excluded(git_tree(checkout, pin), source.excluded)
        if source.licence not in entries:
            raise ValueError(f"{source.name} at {pin} has no {source.licence}")
        inner: str
        for inner in entries:
            name: str = top + "/" + source.path + "/" + inner
            if name in members:
                raise ValueError(f"{name} is in the release tree and in {source.name}")
            members[name] = entries[inner]
        vendored.append({"name": source.name, "path": source.path, "upstream": source.upstream,
                         "revision": pin, "pinned_in": source.pin_file,
                         "licence": source.path + "/" + source.licence,
                         "excluded": list(source.excluded), "files": len(entries),
                         "tree_sha256": tree_digest(entries)})
    record: dict[str, Any] = manifest(root, version, revision, vendored, published_cache)
    text: str = json.dumps(record, indent=2, sort_keys=True) + "\n"
    members[top + "/" + MANIFEST_NAME] = Entry(text.encode("utf-8"), None, False)
    return members


def write_archive(members: dict[str, Entry], archive: Path) -> None:
    """Write members, in name order, as a reproducible gzip tar."""
    archive.unlink(missing_ok=True)
    with archive.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as tar:
                name: str
                for name in sorted(members):
                    entry: Entry = members[name]
                    info: tarfile.TarInfo = tarfile.TarInfo(name)
                    info.mtime = ARCHIVE_EPOCH
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    if entry.link is not None:
                        info.type = tarfile.SYMTYPE
                        info.linkname = entry.link
                        info.mode = 0o777
                        tar.addfile(info)
                        continue
                    data: bytes = entry.data or b""
                    info.size = len(data)
                    info.mode = 0o755 if entry.executable else 0o644
                    tar.addfile(info, io.BytesIO(data))


def read_archive(archive: Path) -> dict[str, Entry]:
    """The members of a written archive, by name (directories left out)."""
    with tarfile.open(archive, "r:gz") as tar:
        return read_tar_members(tar, archive.name)


def check_members(members: dict[str, Entry], version: str) -> dict[str, Any]:
    """Raise ValueError unless members have the source SDK's shape; the parsed manifest.

    Every member lies under the one top directory and none is a Git
    directory or build output; CMakeLists.txt, the build entry point, the
    toolchain file and the manifest are present; the manifest lists exactly
    the vendored sources, and each is present with its recorded tree digest.
    """
    top: str = sdk_directory_name(version) + "/"
    name: str
    for name in members:
        if not name.startswith(top):
            raise ValueError(f"{name} is outside the top directory {top}")
        inner: str = name[len(top):]
        if inner.startswith("/") or ".." in inner.split("/"):
            raise ValueError(f"{name} escapes the top directory")
        if is_build_output(inner):
            raise ValueError(f"{name} is a Git directory or build output")
    required: str
    for required in ("CMakeLists.txt", BUILD_ENTRY, "cmake/llvm22.cmake", MANIFEST_NAME):
        if top + required not in members:
            raise ValueError("the source SDK lacks " + required)
    record: dict[str, Any] = json.loads((members[top + MANIFEST_NAME].data or b"").decode("utf-8"))
    if record.get("top_directory") != sdk_directory_name(version) or record.get("build") != BUILD_ENTRY:
        raise ValueError(f"{MANIFEST_NAME} names another top directory or build entry point")
    cache: Any = record.get("compiler_cache")
    if type(cache) is not dict or cache.get("release") not in (None, CACHE_RELEASE_PREFIX + str(record.get("revision"))):
        raise ValueError(f"{MANIFEST_NAME}'s compiler_cache must name build-<revision> or release null")
    names: list[str] = []
    source: dict[str, Any]
    for source in record.get("third_party", []):
        names.append(source["name"])
        prefix: str = top + source["path"] + "/"
        entries: dict[str, Entry] = {}
        for name in members:
            if name.startswith(prefix):
                entries[name[len(prefix):]] = members[name]
        if not entries:
            raise ValueError(f"{source['name']} is absent from {source['path']}")
        if tree_digest(entries) != source["tree_sha256"]:
            raise ValueError(f"{source['name']} at {source['path']} differs from its recorded digest")
    expected: list[str] = []
    vendored: VendoredSource
    for vendored in VENDORED:
        expected.append(vendored.name)
    if names != expected:
        raise ValueError(f"{MANIFEST_NAME} lists {names}, not {expected}")
    return record


def check_archive(archive: Path, version: str) -> dict[str, Any]:
    """Raise ValueError unless archive is this version's source SDK, by name and shape; its manifest."""
    expected: str = sdk_archive_name(version)
    if archive.name != expected:
        raise ValueError(f"{archive.name} is not named {expected}")
    return check_members(read_archive(archive), version)


def package(root: Path, version: str, revision: str, out: Path, checkouts: dict[str, Path],
            fetch_missing: bool = False, published_cache: bool = False) -> Path:
    """Collect, check and write the source SDK of revision into out; the archive.

    fetch_missing lets collect fetch a pin no local checkout holds;
    published_cache records the revision's compiler-cache release.
    """
    out.mkdir(parents=True, exist_ok=True)
    archive: Path = out / sdk_archive_name(version)
    with tempfile.TemporaryDirectory(prefix="gui-forms-sdk-sources-") as directory:
        fetch_work: Path | None = Path(directory) if fetch_missing else None
        members: dict[str, Entry] = collect(root, version, revision, checkouts, fetch_work, published_cache)
    check_members(members, version)
    write_archive(members, archive)
    check_archive(archive, version)
    print(f"{archive.name}: {archive.stat().st_size} bytes, {len(members)} files", flush=True)
    return archive


def parse_checkouts(values: list[str]) -> dict[str, Path]:
    """--checkout name=path arguments as a map; names must be vendored sources."""
    known: set[str] = set()
    source: VendoredSource
    for source in VENDORED:
        known.add(source.name)
    checkouts: dict[str, Path] = {}
    value: str
    for value in values:
        parts: tuple[str, str, str] = value.partition("=")
        if not parts[1] or parts[0] not in known:
            raise ValueError(f"--checkout {value}: expected one of {sorted(known)}=<path>")
        checkouts[parts[0]] = Path(parts[2])
    return checkouts


def main(argv: list[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True)
    parser.add_argument("--revision", default="HEAD", help="the release commit (default HEAD)")
    parser.add_argument("--out", type=Path, default=Path("dist"))
    parser.add_argument("--checkout", action="append", default=[],
                        help="name=path: a Git checkout holding a vendored source's pin, when it is "
                             "not at its in-tree path (CI passes miniaudio's FetchContent clone)")
    parser.add_argument("--fetch-missing", action="store_true",
                        help="shallow-fetch from its upstream any pin no local checkout holds")
    parser.add_argument("--compiler-cache", action="store_true",
                        help="this run is the push to main whose native jobs publish build-<revision>; "
                             "record that release in the manifest (else compiler_cache.release is null)")
    parser.add_argument("--check", type=Path, default=None,
                        help="check this existing archive's name, shape and digests; write nothing")
    args: argparse.Namespace = parser.parse_args(argv)
    if args.check is not None:
        check_archive(args.check, args.version)
        print(f"{args.check.name}: source SDK shape and digests checked", flush=True)
        return 0
    revision: str = subprocess.run(["git", "-C", str(ROOT), "rev-parse", args.revision + "^{commit}"],
                                   check=True, capture_output=True, text=True).stdout.strip()
    package(ROOT, args.version, revision, args.out, parse_checkouts(args.checkout), args.fetch_missing,
            args.compiler_cache)
    return 0


if __name__ == "__main__":
    sys.exit(main())
