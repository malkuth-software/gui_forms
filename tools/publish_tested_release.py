#!/usr/bin/env python3
"""Publish an already-tested GUI.Forms SDK as one GitHub release.

Workflow job dependencies are the test gate: this runs only after every
platform built and tested its deployment tree. The demonstration programs
are released from malkuth-software/gui_forms_devkit, not here. It

  1. checks that the source SDK archive is present with its shape
     (tools/package_sdk.py) and that nothing else is among the artifacts;
  2. creates tag v<version> at exactly the tested commit through the API, or
     verifies an existing tag names that commit;
  3. creates a DRAFT release titled "GUI.Forms <version>", uploads every
     artifact, SHA256SUMS.txt and the test receipts, then un-drafts it.

Reruns are idempotent: a published (non-draft) release is never modified; a
draft accepts an already-uploaded asset only when its bytes are equal
(tools/publish_cache.py's rule). Notes are a generated header (version, commit,
the SDK) followed by packaging/RELEASE_NOTES.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parents[1]
MAIN_REF: str = "refs/heads/main"
RECEIPT_PATTERN: str = r"ctest-[a-z0-9-]+\.xml"
def gh(*arguments: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(["gh", *arguments], text=True, capture_output=True)
    if check and result.returncode != 0:
        raise RuntimeError(f"gh {' '.join(arguments)} failed: {result.stderr.strip()}")
    return result


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def expected_sdk_archive(version: str) -> str:
    """The one source SDK archive every release carries (tools/package_sdk.py's name)."""
    from package_sdk import sdk_archive_name
    return sdk_archive_name(version)


def release_notes(version: str, sha: str, repository: str, test_receipts: list[str]) -> str:
    lines = [f"GUI.Forms {version}, built and tested from [commit `{sha[:12]}`](https://github.com/{repository}/commit/{sha}).", ""]
    lines += ["## SDK", "",
              f"`{expected_sdk_archive(version)}` is this commit's source with the pinned third-party sources "
              "it builds from. Unpack it and run `python3 tools/build_sdk.py --target <platform> --prefix <dir>` "
              "to build and install GUI.Forms for your host; `SDK_MANIFEST.json` lists what it carries and what "
              "a build still downloads (docs/user/install.md). The installed SDK carries the Gallery model "
              "(`GUIForms::GalleryModel`)."]
    lines += ["", "## Programs", "",
              "The Gallery, the demonstrators and Explore are released from "
              "[gui_forms_devkit](https://github.com/malkuth-software/gui_forms_devkit), built against this "
              "revision."]
    lines += ["", "`SHA256SUMS.txt` lists every download's digest."]
    if test_receipts:
        lines += ["", "Test receipts attached: " + ", ".join(f"`{name}`" for name in test_receipts) + "."]
    lines += ["", (ROOT / "packaging/RELEASE_NOTES.md").read_text(encoding="utf-8").strip(), ""]
    return "\n".join(lines)


def upload_assets(tag: str, repository: str, files: list[Path], dry_run: bool) -> None:
    for path in files:
        if dry_run:
            print(f"  would upload {path.name}")
            continue
        uploaded = gh("release", "upload", tag, str(path), "--repo", repository, check=False)
        if uploaded.returncode == 0:
            continue
        # A rerun never silently replaces an asset; only equal bytes are accepted.
        with tempfile.TemporaryDirectory() as directory:
            gh("release", "download", tag, "--repo", repository, "--pattern", path.name, "--dir", directory)
            existing = Path(directory) / path.name
            if not existing.is_file() or sha256_file(existing) != sha256_file(path):
                raise ValueError(f"refusing to replace the already-uploaded {path.name} with different bytes")
        print(f"  {path.name}: identical asset already present")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--artifacts", type=Path, required=True, help="directory with the downloaded platform artifacts")
    parser.add_argument("--receipts", type=Path, default=None, help="directory with ctest-*.xml (default: --artifacts)")
    parser.add_argument("--dry-run", action="store_true", help="validate and print the plan and notes; call no gh command")
    args = parser.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from package_sdk import check_archive
    from publication_version import validate

    repository, sha = os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_SHA"]
    ref = os.environ.get("GITHUB_REF", "")
    version, tag = os.environ["RELEASE_VERSION"], os.environ["RELEASE_TAG"]
    validate(version)
    if tag != "v" + version:
        raise ValueError(f"release tag {tag} does not match version {version}")
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("GITHUB_SHA must be a full commit sha")
    receipts_dir = args.receipts or args.artifacts
    expected = [expected_sdk_archive(version)]
    files = [args.artifacts / name for name in expected]
    missing = [path.name for path in files if not path.is_file()]
    if missing:
        raise ValueError("the release's artifacts are missing: " + ", ".join(missing))
    check_archive(args.artifacts / expected_sdk_archive(version), version)
    unexpected = sorted(path.name for path in args.artifacts.iterdir()
                        if path.is_file() and path.name not in expected and path.name != "SHA256SUMS.txt"
                        and not re.fullmatch(RECEIPT_PATTERN, path.name))
    if unexpected:
        raise ValueError("unexpected files among the artifacts: " + ", ".join(unexpected))
    sums = args.artifacts / "SHA256SUMS.txt"
    sums.write_text("".join(sha256_file(path) + "  " + path.name + "\n" for path in sorted(files, key=lambda p: p.name)), encoding="utf-8")
    files.append(sums)
    candidates: list[Path] = list(receipts_dir.iterdir()) if receipts_dir.is_dir() else []
    test_receipts = sorted(path for path in candidates if path.is_file() and re.fullmatch(RECEIPT_PATTERN, path.name))
    files.extend(test_receipts)
    notes = release_notes(version, sha, repository, [path.name for path in test_receipts])
    title = "GUI.Forms " + version
    print(f"Release {title} ({tag}) at {sha[:12]} with {len(files)} assets")
    if args.dry_run:
        print("--- notes ---")
        print(notes)
        print("--- assets ---")
        upload_assets(tag, repository, files, dry_run=True)
        print(f"would create tag {tag}, a draft release titled {title!r}, upload, then publish"
              f" (latest={'true' if ref == MAIN_REF else 'false'})")
        return 0

    # An older, slower main run must not publish over a newer main revision.
    if ref == MAIN_REF and gh("api", f"repos/{repository}/commits/main", "--jq", ".sha").stdout.strip() != sha:
        print("A newer main revision exists; this run's tested artifacts stay unpublished.")
        return 0
    if gh("api", f"repos/{repository}/commits/{sha}", "--jq", ".sha").stdout.strip() != sha:
        raise ValueError("the tested commit cannot be resolved in the repository")
    probe = gh("api", f"repos/{repository}/git/ref/tags/{tag}", check=False)
    if probe.returncode == 0:
        if gh("api", f"repos/{repository}/commits/{tag}", "--jq", ".sha").stdout.strip() != sha:
            raise ValueError(f"existing tag {tag} points at a different, untested commit")
    elif "404" not in probe.stderr:
        raise RuntimeError("cannot safely check the release tag: " + probe.stderr.strip())
    else:
        gh("api", f"repos/{repository}/git/refs", "--method", "POST", "-f", "ref=refs/tags/" + tag, "-f", "sha=" + sha)
    if gh("api", f"repos/{repository}/commits/{tag}", "--jq", ".sha").stdout.strip() != sha:
        raise ValueError("the release tag changed before publication")
    view = gh("release", "view", tag, "--repo", repository, "--json", "isDraft,targetCommitish", check=False)
    if view.returncode == 0:
        existing = json.loads(view.stdout)
        if not existing["isDraft"]:
            print(f"{title} is already published; leaving it immutable.")
            return 0
        if existing["targetCommitish"] not in (sha, tag, "main"):
            raise RuntimeError(f"the existing draft targets {existing['targetCommitish']}, not the tested commit")
        print("Continuing the existing draft.")
    else:
        with tempfile.TemporaryDirectory(prefix="gui-forms-release-") as directory:
            notes_path = Path(directory) / "notes.md"
            notes_path.write_text(notes, encoding="utf-8")
            gh("release", "create", tag, "--repo", repository, "--target", sha, "--draft",
               "--title", title, "--notes-file", str(notes_path))
    upload_assets(tag, repository, files, dry_run=False)
    gh("release", "edit", tag, "--repo", repository, "--draft=false", "--latest=" + str(ref == MAIN_REF).lower())
    print(f"Published {title}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
