#!/usr/bin/env python3
"""The C5 compatibility gate: the 0.x native surface the Windows Forms facade
uses, mapped onto the stable C ABI.

The facade (generated/facade-v1) reaches native code only through the 0.x
table gf_api_v0 and the gf_windows_paint_endpoint_*_v1 exports, both declared
in include/gui_forms/c_api.h. manifests/abi/compat-map.json maps each of them
to stable slots (manifests/abi/gf_abi.json), schema properties or schema
interactions (manifests/control-schema.json), or names the family still to
carry it.

    abi_compat_map.py            check the map; pending entries are listed
    abi_compat_map.py --strict   also fail while any entry is pending

Exit status 1 for an entry missing from the map, a map entry the 0.x surface
does not have, a target that does not exist, or (with --strict) a pending
entry.
"""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import re
import sys
from typing import Any

ROOT: pathlib.Path = pathlib.Path(__file__).resolve().parents[1]
LEGACY_HEADER: pathlib.Path = ROOT / "include" / "gui_forms" / "c_api.h"
MAP: pathlib.Path = ROOT / "manifests" / "abi" / "compat-map.json"
ABI_MANIFEST: pathlib.Path = ROOT / "manifests" / "abi" / "gf_abi.json"
SCHEMA: pathlib.Path = ROOT / "manifests" / "control-schema.json"


def legacy_surface() -> list[str]:
    """The gf_api_v0 members after struct_size and abi_version, then the
    paint endpoint exports, in declaration order."""
    text: str = LEGACY_HEADER.read_text(encoding="utf-8")
    start: int = text.index("typedef struct gf_api_v0")
    end: int = text.index("} gf_api_v0;", start)
    members: list[str] = re.findall(r"\(\s*\*\s*(\w+)\s*\)\s*\(", text[start:end])
    exports: list[str] = re.findall(r"GF_C_API_EXPORT gf_result (gf_windows_paint_endpoint_\w+_v1)\(", text)
    return members + exports


def stable_slots() -> set[str]:
    manifest: dict[str, Any] = json.loads(ABI_MANIFEST.read_text(encoding="utf-8"))
    slots: set[str] = set()
    for record in manifest["structs"]:
        name: str = record["name"]
        if not (name.startswith("gf_") and name.endswith("_table")):
            continue
        table: str = name[len("gf_"):-len("_table")]
        for field in record["fields"]:
            slots.add(table + "." + field["name"])
    return slots


def schema_names() -> tuple[set[str], set[str]]:
    schema: dict[str, Any] = json.loads(SCHEMA.read_text(encoding="utf-8"))
    properties: set[str] = set()
    interactions: set[str] = set()
    for control_type in schema["types"]:
        for property_record in control_type.get("properties", []):
            properties.add(property_record["name"])
        for interaction in control_type.get("interactions", []):
            interactions.add(interaction["name"] if isinstance(interaction, dict) else interaction)
    return properties, interactions


def target_exists(target: str, slots: set[str], properties: set[str], interactions: set[str]) -> bool:
    kind, _, name = target.partition(":")
    if kind == "slot":
        return name in slots
    if kind == "property":
        return name in properties
    if kind == "interaction":
        return name in interactions
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--strict", action="store_true", help="fail while any entry is pending")
    arguments = parser.parse_args()

    surface: list[str] = legacy_surface()
    compat_map: dict[str, Any] = json.loads(MAP.read_text(encoding="utf-8"))
    entries: list[dict[str, Any]] = compat_map["entries"]
    slots: set[str] = stable_slots()
    properties, interactions = schema_names()

    findings: list[str] = []
    mapped: dict[str, dict[str, Any]] = {}
    for entry in entries:
        name: str = entry["v0"]
        if name in mapped:
            findings.append(f"{name}: mapped twice")
        mapped[name] = entry
        if name not in surface:
            findings.append(f"{name}: not in the 0.x surface")
        if "targets" not in entry and "pending" not in entry and "deferred" not in entry:
            findings.append(f"{name}: has no target, pending family or deferral")
        for target in entry.get("targets", []):
            if not target_exists(target, slots, properties, interactions):
                findings.append(f"{name}: target {target} does not exist")
    for name in surface:
        if name not in mapped:
            findings.append(f"{name}: unmapped")

    pending: collections.Counter[str] = collections.Counter(
        entry["pending"] for entry in entries if "pending" in entry)
    deferred: list[str] = [entry["v0"] for entry in entries if "deferred" in entry]
    carried: int = sum(1 for entry in entries if "targets" in entry and "pending" not in entry)
    print(f"abi_compat_map: {len(surface)} entries, {carried} carried, {sum(pending.values())} pending, "
          f"{len(deferred)} deferred, {len(findings)} findings")
    for family, count in sorted(pending.items()):
        print(f"  pending {family}: {count}")
    for name in deferred:
        print(f"  deferred {name}: {mapped[name]['deferred']}")
    for finding in findings:
        print(f"abi_compat_map: {finding}")
    if findings:
        return 1
    if arguments.strict and pending:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
