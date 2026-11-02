"""Check and render the recorded Windows Forms divergences (manifests/winforms-divergences.json).

GUI.Forms matches the documented observable behaviour of Windows Forms, not
its internals, and records every difference (owner rule). Each entry of the
manifest names the control, the member, the Windows Forms behaviour, the
GUI.Forms behaviour, the reason, the decision's source (``owner`` or
``coordinator``) and its date (``YYYY-MM-DD``). This module has two jobs:

- ``check``: prove the manifest parses and that every entry is complete:
  exactly the fields above, each a non-empty string, a known source, a real
  date, and no control and member recorded twice;
- ``render``: write ``docs/controls/generated/winforms-divergences.md`` from
  the manifest, or prove the committed page is current with ``--check``.

Standard library only; Python 3.12+.
"""

from __future__ import annotations

import argparse
import datetime
import json
import sys
from collections.abc import Sequence
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parent.parent
MANIFEST_PATH: Path = ROOT / "manifests" / "winforms-divergences.json"
PAGE_PATH: Path = ROOT / "docs" / "controls" / "generated" / "winforms-divergences.md"

FIELDS: tuple[str, ...] = ("control", "member", "winforms", "gui_forms", "reason", "source", "date")
SOURCES: tuple[str, ...] = ("owner", "coordinator")


class ManifestError(Exception):
    """The manifest cannot be read as JSON or has no list of divergences."""


def load_manifest(path: Path) -> dict[str, object]:
    try:
        manifest: object = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"{path}: {error}") from error
    if not isinstance(manifest, dict) or not isinstance(manifest.get("divergences"), list):
        raise ManifestError(f"{path}: expected an object with a \"divergences\" list")
    return manifest


def valid_date(text: str) -> bool:
    try:
        parsed: datetime.date = datetime.date.fromisoformat(text)
    except ValueError:
        return False
    return parsed.isoformat() == text


def validate(manifest: dict[str, object]) -> list[str]:
    findings: list[str] = []
    seen: set[tuple[str, str]] = set()
    entries: list[object] = manifest["divergences"]  # type: ignore[assignment]
    for index, entry in enumerate(entries):
        where: str = f"entry {index + 1}"
        if not isinstance(entry, dict):
            findings.append(f"{where}: not an object")
            continue
        missing: list[str] = [field for field in FIELDS if field not in entry]
        extra: list[str] = sorted(field for field in entry if field not in FIELDS)
        if missing:
            findings.append(f"{where}: missing {', '.join(missing)}")
        if extra:
            findings.append(f"{where}: unknown {', '.join(extra)}")
        empty: list[str] = [field for field in FIELDS
                            if field in entry and (not isinstance(entry[field], str) or not entry[field].strip())]
        if empty:
            findings.append(f"{where}: empty or not text: {', '.join(empty)}")
            continue
        if missing:
            continue
        if entry["source"] not in SOURCES:
            findings.append(f"{where}: source must be one of {', '.join(SOURCES)}")
        if not valid_date(entry["date"]):
            findings.append(f"{where}: date must be YYYY-MM-DD")
        key: tuple[str, str] = (entry["control"], entry["member"])
        if key in seen:
            findings.append(f"{where}: {entry['control']} {entry['member']} is recorded twice")
        seen.add(key)
    return findings


def cell(text: str) -> str:
    escaped: str = text.replace("|", "\\|").replace("\n", " ")
    return escaped


def render_page(manifest: dict[str, object]) -> str:
    entries: list[dict[str, str]] = sorted(
        manifest["divergences"],  # type: ignore[arg-type]
        key=entry_order)
    lines: list[str] = [
        "# Windows Forms divergences",
        "",
        "Generated from `manifests/winforms-divergences.json` by",
        "`python3 tools/winforms_divergences.py render`; do not edit. GUI.Forms matches the",
        "documented observable behaviour of Windows Forms, not its internals or timings, and",
        "records every difference here with its reason and the decision behind it.",
        "",
    ]
    if not entries:
        lines.append("No divergences are recorded.")
        lines.append("")
        return "\n".join(lines)
    control: str = ""
    for entry in entries:
        if entry["control"] != control:
            control = entry["control"]
            lines.append(f"## {control}")
            lines.append("")
        lines.append(f"### {entry['member']}")
        lines.append("")
        lines.append("| | |")
        lines.append("|---|---|")
        lines.append(f"| Windows Forms | {cell(entry['winforms'])} |")
        lines.append(f"| GUI.Forms | {cell(entry['gui_forms'])} |")
        lines.append(f"| Reason | {cell(entry['reason'])} |")
        lines.append(f"| Decision | {cell(entry['source'])}, {cell(entry['date'])} |")
        lines.append("")
    return "\n".join(lines)


def entry_order(entry: dict[str, str]) -> tuple[str, str]:
    order: tuple[str, str] = (entry["control"], entry["member"])
    return order


# ---------------------------------------------------------------------------- cli


def _command_check(args: argparse.Namespace) -> int:
    manifest: dict[str, object] = load_manifest(args.manifest)
    findings: list[str] = validate(manifest)
    for finding in findings:
        print(f"winforms divergences: {finding}", file=sys.stderr)
    if findings:
        return 1
    count: int = len(manifest["divergences"])  # type: ignore[arg-type]
    print(f"winforms divergences: {count} {'entry' if count == 1 else 'entries'} OK")
    return 0


def _command_render(args: argparse.Namespace) -> int:
    manifest: dict[str, object] = load_manifest(args.manifest)
    findings: list[str] = validate(manifest)
    if findings:
        for finding in findings:
            print(f"winforms divergences: {finding}", file=sys.stderr)
        return 1
    page: str = render_page(manifest)
    if args.check:
        current: str = args.out.read_text(encoding="utf-8") if args.out.exists() else ""
        if current.replace("\r\n", "\n") != page:
            print(f"winforms divergences: {args.out} is stale; run `python3 tools/winforms_divergences.py render`",
                  file=sys.stderr)
            return 1
        print(f"winforms divergences: {args.out} is current")
        return 0
    args.out.write_text(page, encoding="utf-8", newline="\n")
    print(f"winforms divergences: wrote {args.out}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", type=Path, default=MANIFEST_PATH, help="divergence manifest")
    subparsers = parser.add_subparsers(dest="command", required=True)
    check = subparsers.add_parser("check", help="validate the manifest's entries")
    check.set_defaults(run=_command_check)
    render = subparsers.add_parser("render", help="write or check docs/controls/generated/winforms-divergences.md")
    render.add_argument("--out", type=Path, default=PAGE_PATH, help="output page")
    render.add_argument("--check", action="store_true", help="fail when the committed page is stale")
    render.set_defaults(run=_command_render)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args: argparse.Namespace = build_parser().parse_args(argv)
    try:
        return int(args.run(args))
    except ManifestError as error:
        print(f"winforms divergences: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
