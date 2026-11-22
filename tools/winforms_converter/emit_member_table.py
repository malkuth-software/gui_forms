"""Write the WinForms converter's member table (tools/winforms_converter/winforms_member_table.hpp).

The converter (stage MCP G7) maps a Windows Forms project's types, properties
and events onto GUI.Forms through the same ledgers every importer reads:

- ``manifests/controls.json``: which registry type a Windows Forms type
  becomes. A record is imported by its own C++ name, or by its
  ``windows_forms_name`` when it has one (owner decision, 2026-10-26: an
  imported FlowLayoutPanel becomes FlowLayoutPanelCompat);
- ``manifests/members.json``: the member manifest, one entry per GUI.Forms
  type (plus Control, Component and Window) and one per Windows Forms type
  with no GUI.Forms type, each Windows Forms member with its GUI.Forms
  counterpart and status;
- ``manifests/winforms-divergences.json``: the recorded behaviour
  differences, so a converted member that behaves differently says so.

Only properties and events are written: they are what a designer file sets
and subscribes. Types are sorted by their Windows Forms name and members by
name within each type, so the converter looks both up with a binary search.

Usage::

    python3 tools/winforms_converter/emit_member_table.py          # write
    python3 tools/winforms_converter/emit_member_table.py --check  # fail when stale

Standard library only; Python 3.12+.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parent.parent.parent
CONTROLS_PATH: Path = ROOT / "manifests" / "controls.json"
MEMBERS_PATH: Path = ROOT / "manifests" / "members.json"
DIVERGENCES_PATH: Path = ROOT / "manifests" / "winforms-divergences.json"
OUTPUT_PATH: Path = ROOT / "tools" / "winforms_converter" / "winforms_member_table.hpp"

KINDS: dict[str, str] = {"property": "property", "event": "event"}
STATUSES: tuple[str, ...] = ("supported", "deferred", "excluded", "unclassified")
IDENTIFIER: re.Pattern[str] = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class TableError(Exception):
    """A ledger cannot be read, or two records claim one Windows Forms type."""


@dataclass
class MemberRow:
    winforms: str
    gui_forms: str
    kind: str
    status: str


@dataclass
class TypeRow:
    winforms: str
    winforms_base: str
    gui_forms_type: str
    registry_id: str
    members: list[MemberRow] = field(default_factory=list)


@dataclass
class DivergenceRow:
    control: str
    member: str
    gui_forms: str


def _load(path: Path) -> Mapping[str, object]:
    try:
        loaded: object = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TableError(f"{path}: {error}") from error
    if not isinstance(loaded, dict):
        raise TableError(f"{path}: expected an object")
    return loaded


def importer_names(controls: Mapping[str, object]) -> dict[str, str]:
    """Each Windows Forms name an importer maps, to its registry id."""
    names: dict[str, str] = {}
    records: object = controls.get("controls")
    if not isinstance(records, list):
        raise TableError("controls.json: no controls list")
    for record in records:
        if not isinstance(record, dict):
            continue
        winforms: str = str(record.get("winforms", ""))
        if winforms == "GUI.Forms-only" or not winforms:
            continue
        alias: object = record.get("windows_forms_name")
        imported_as: str = str(alias) if isinstance(alias, str) else str(record.get("type", ""))
        # A record whose own name is not its Windows Forms counterpart (an
        # ObjectView beside ListView, a CommandBar beside ToolStrip) is not
        # what an importer makes of that counterpart.
        if imported_as != winforms:
            continue
        if winforms in names:
            raise TableError(f"controls.json: two records import {winforms}")
        names[winforms] = str(record["id"])
    return names


def type_rows(members: Mapping[str, object], imported: Mapping[str, str]) -> list[TypeRow]:
    entries: object = members.get("controls")
    if not isinstance(entries, list):
        raise TableError("members.json: no controls list")
    rows: dict[str, TypeRow] = {}
    priorities: dict[str, int] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        winforms: object = entry.get("winforms")
        if not isinstance(winforms, str) or not winforms:
            continue
        gui_type: str = str(entry.get("type") or "")
        # The entry an importer reads: the Windows Forms type's own record
        # (ListView's, not ObjectView's), then an entry for a type with no
        # GUI.Forms counterpart, then any other entry naming it (Form's
        # Window).
        priority: int = 0
        if gui_type == winforms or (gui_type and imported.get(winforms) == registry_id_of(gui_type)):
            priority = 2
        elif not gui_type:
            priority = 1
        if winforms in rows and priorities[winforms] >= priority:
            continue
        priorities[winforms] = priority
        base: str = str(entry.get("winforms_base") or "")
        row: TypeRow = TypeRow(winforms=winforms, winforms_base=base, gui_forms_type=gui_type,
                               registry_id=imported.get(winforms, ""))
        for member in entry.get("members", []):
            if not isinstance(member, dict):
                continue
            kind: str = KINDS.get(str(member.get("kind")), "")
            if not kind:
                continue
            status: str = str(member.get("status", "unclassified"))
            if status not in STATUSES:
                status = "unclassified"
            name: str = str(member.get("winforms", ""))
            # A member a Windows Forms base declares (TextBoxBase.SelectAll)
            # is listed under its own name.
            short: str = name.rsplit(".", 1)[-1]
            if not IDENTIFIER.fullmatch(short):
                continue
            row.members.append(MemberRow(winforms=short, gui_forms=str(member.get("gui_forms") or ""), kind=kind,
                                         status=status))
        row.members.sort(key=_member_key)
        unique: list[MemberRow] = []
        for member_row in row.members:
            if unique and _member_key(unique[-1]) == _member_key(member_row):
                continue
            unique.append(member_row)
        row.members = unique
        rows[winforms] = row
    return sorted(rows.values(), key=_type_key)


def _member_key(row: MemberRow) -> tuple[str, str]:
    return (row.winforms, row.kind)


def _type_key(row: TypeRow) -> str:
    return row.winforms


def registry_id_of(gui_type: str) -> str:
    """The registry id of a C++ type name: its snake case (CheckBox: check_box)."""
    snake: str = re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", gui_type)
    snake = re.sub(r"(?<=[A-Z])([A-Z][a-z])", r"_\1", snake).lower()
    return snake


def divergence_rows(divergences: Mapping[str, object]) -> list[DivergenceRow]:
    entries: object = divergences.get("divergences")
    if not isinstance(entries, list):
        raise TableError("winforms-divergences.json: no divergences list")
    rows: list[DivergenceRow] = []
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        member: str = str(entry.get("member", ""))
        # "back_color, fore_color" and "column_span / row_span / cell
        # (overlap)" name several members; what follows " (" is the case.
        head: str = member.split(" (", 1)[0]
        for word in re.split(r"[,/]", head):
            name: str = word.strip()
            if IDENTIFIER.fullmatch(name):
                rows.append(DivergenceRow(control=str(entry.get("control", "")), member=name,
                                          gui_forms=str(entry.get("gui_forms", ""))))
    rows.sort(key=_divergence_key)
    return rows


def _divergence_key(row: DivergenceRow) -> tuple[str, str]:
    return (row.control, row.member)


def _cpp_string(text: str) -> str:
    escaped: str = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render(types: Sequence[TypeRow], divergences: Sequence[DivergenceRow]) -> str:
    members: list[MemberRow] = []
    lines: list[str] = [
        "// Generated from manifests/controls.json, manifests/members.json and",
        "// manifests/winforms-divergences.json by `python3 tools/winforms_converter/emit_member_table.py`.",
        "// Do not edit; `python3 tools/winforms_converter/emit_member_table.py --check` fails when stale.",
        "// Included only by winforms_mapping.cpp.",
        "",
        "#pragma once",
        "",
        '#include "winforms_mapping.hpp"',
        "",
        "#include <array>",
        "",
        "namespace gui_forms::winforms_converter {",
        "",
    ]
    type_lines: list[str] = []
    for row in types:
        first: int = len(members)
        members.extend(row.members)
        type_lines.append(
            f"    {{{_cpp_string(row.winforms)}, {_cpp_string(row.winforms_base)}, {_cpp_string(row.gui_forms_type)}, "
            f"{_cpp_string(row.registry_id)}, {first}U, {len(row.members)}U}},")
    lines.append(f"inline constexpr std::array<WinFormsMemberRow, {len(members)}> winforms_member_rows{{{{")
    for member in members:
        lines.append(f"    {{{_cpp_string(member.winforms)}, {_cpp_string(member.gui_forms)}, "
                     f"WinFormsMemberKind::{member.kind}, WinFormsMemberStatus::{member.status}}},")
    lines.append("}};")
    lines.append("")
    lines.append("// Sorted by the Windows Forms name.")
    lines.append(f"inline constexpr std::array<WinFormsTypeRow, {len(types)}> winforms_type_rows{{{{")
    lines.extend(type_lines)
    lines.append("}};")
    lines.append("")
    lines.append("// Sorted by control, then member.")
    lines.append(f"inline constexpr std::array<WinFormsDivergenceRow, {len(divergences)}> winforms_divergence_rows{{{{")
    for divergence in divergences:
        lines.append(f"    {{{_cpp_string(divergence.control)}, {_cpp_string(divergence.member)}, "
                     f"{_cpp_string(divergence.gui_forms)}}},")
    lines.append("}};")
    lines.append("")
    lines.append("} // namespace gui_forms::winforms_converter")
    lines.append("")
    return "\n".join(lines)


def main(argv: Sequence[str] | None = None) -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=OUTPUT_PATH, help="output header path")
    parser.add_argument("--check", action="store_true", help="fail when the committed header is stale")
    args: argparse.Namespace = parser.parse_args(argv)
    try:
        imported: dict[str, str] = importer_names(_load(CONTROLS_PATH))
        types: list[TypeRow] = type_rows(_load(MEMBERS_PATH), imported)
        divergences: list[DivergenceRow] = divergence_rows(_load(DIVERGENCES_PATH))
    except TableError as error:
        print(f"emit_member_table: {error}", file=sys.stderr)
        return 1
    content: str = render(types, divergences)
    target: Path = args.out
    if args.check:
        committed: str = target.read_text(encoding="utf-8") if target.is_file() else ""
        if committed != content:
            print(f"emit_member_table: {target} is stale; run `python3 tools/winforms_converter/emit_member_table.py`",
                  file=sys.stderr)
            return 1
        print(f"emit_member_table: {target} is current")
        return 0
    target.write_text(content, encoding="utf-8", newline="\n")
    print(target.relative_to(ROOT) if target.is_relative_to(ROOT) else target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
