"""Load, validate, and render the GUI.Forms control registry (manifests/controls.json).

The registry is the single list of public control and component types that the
Gallery navigation, the docs by-function/alphabetical indexes, and the README
controls table consume. This module has three jobs:

- ``check``: validate every record and prove coverage both ways against the
  public headers (see ``scan_public_control_types`` for the exact rule);
- ``render``: write ``docs/controls/generated/by-function.md`` and
  ``docs/controls/generated/alphabetical.md``;
- ``readme-table``: print a compact table for the README, or splice it between
  ``<!-- controls:start -->`` and ``<!-- controls:end -->`` with ``--write``;
- ``emit-cpp``: write ``demo/gallery_catalog.inc``, the constexpr C++ array the
  GUI.Forms Gallery builds its navigation and page headers from, or prove the
  committed file is current with ``--check``;
- ``emit-abi``: write ``src/abi/stable/schema/abi_schema_catalog.hpp``, the
  C ABI's table of every type with the recipes it describes and creates the
  type by, or prove the committed file is current with ``--check``;
- ``emit-forms``: write ``tools/form_compiler/form_compiler_catalog.hpp``, the
  form compiler's table of every type with its C++ class and header, or prove
  the committed file is current with ``--check``.

Standard library only; Python 3.12+.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

ROOT: Path = Path(__file__).resolve().parent.parent
REGISTRY_PATH: Path = ROOT / "manifests" / "controls.json"
INCLUDE_ROOT: Path = ROOT / "include"
HEADER_ROOT: Path = INCLUDE_ROOT / "gui_forms"
GENERATED_DIR: Path = ROOT / "docs" / "controls" / "generated"
BY_FUNCTION_NAME: str = "by-function.md"
ALPHABETICAL_NAME: str = "alphabetical.md"
# The published documentation site; the README links each control to its page there.
SITE_URL: str = "https://gui-forms.readthedocs.io/"
README_START: str = "<!-- controls:start -->"
README_END: str = "<!-- controls:end -->"
GALLERY_CATALOG_PATH: Path = ROOT / "demo" / "gallery_catalog.inc"
ABI_CATALOG_PATH: Path = ROOT / "src" / "abi" / "stable" / "schema" / "abi_schema_catalog.hpp"
FORMS_CATALOG_PATH: Path = ROOT / "tools" / "form_compiler" / "form_compiler_catalog.hpp"

# Fixed WinForms-style by-function categories, in render order.
CATEGORIES: tuple[str, ...] = (
    "Commands",
    "Text editing",
    "Information display",
    "Selection from a list",
    "Value setting",
    "Date setting",
    "Grouping",
    "Layout",
    "Scrolling",
    "Menus and commands",
    "Dialogs and popups",
    "Drawing",
    "Diagnostics",
    "Components",
)
STATUSES: tuple[str, ...] = ("stable", "provisional", "lab")
REQUIRED_FIELDS: tuple[str, ...] = (
    "id",
    "type",
    "header",
    "category",
    "purpose",
    "winforms",
    "status",
    "gallery_page",
    "doc",
)
# `windows_forms_name`: the Windows Forms type name an importer maps to this
# control when it differs from `type` (owner decision, 2026-10-26: an
# imported FlowLayoutPanel becomes FlowLayoutPanelCompat). Every importer
# reads this one table; a record without it is imported by its own name.
OPTIONAL_FIELDS: tuple[str, ...] = ("windows_forms_name",)

# Coverage rule roots: a header class counts as a public control/component type
# when its base-class chain reaches one of these (the roots themselves are not
# registry entries).
HIERARCHY_ROOTS: frozenset[str] = frozenset({"Control", "Component"})
# Types admitted to the registry although they derive from neither root. Each
# entry carries the reason so the exception stays auditable.
ADMITTED_SERVICE_TYPES: Mapping[str, str] = {
    "LiveSurface": "renderer-neutral latest-frame surface consumed by Window; not a Component by design",
}

KEBAB_CASE: re.Pattern[str] = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
# Registry ids are registered names (owner decision 8): snake_case, the words
# of the C++ type name. CSS-family documents write them in kebab-case.
SNAKE_CASE: re.Pattern[str] = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
_TYPE_WORD: re.Pattern[str] = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z0-9]+|[A-Z]+")


def registered_id(type_name: str) -> str:
    """The registry id a type's name spells: "HScrollBar" is "h_scroll_bar"."""
    return "_".join(word.lower() for word in _TYPE_WORD.findall(type_name))
CPP_IDENTIFIER: re.Pattern[str] = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Namespace-level class/struct declarations with a base-clause. Column 0 only:
# nested (indented) declarations are implementation detail. Templates are
# captured so the scan can exclude them (a class template is not a toolbox type
# until instantiated).
_DECLARATION: re.Pattern[str] = re.compile(
    r"^(?P<template>template\s*<[^\n]*>\s*\n)?"
    r"(?:class|struct)\s+(?P<name>[A-Za-z_]\w*)\s*(?:final\s*)?"
    r"(?P<bases>:\s*[^{;]*?)?\s*\{",
    re.MULTILINE,
)
_BLOCK_COMMENT: re.Pattern[str] = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT: re.Pattern[str] = re.compile(r"//[^\n]*")
_BASE_NAME: re.Pattern[str] = re.compile(r"(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)")


class RegistryError(Exception):
    """Raised when the registry file cannot be loaded or is malformed."""


@dataclass(frozen=True)
class ControlRecord:
    id: str
    type: str
    header: str
    category: str
    purpose: str
    winforms: str
    status: str
    gallery_page: str | None
    doc: str
    windows_forms_name: str | None = None

    @staticmethod
    def from_mapping(raw: Mapping[str, object], index: int) -> ControlRecord:
        missing: list[str] = [field for field in REQUIRED_FIELDS if field not in raw]
        if missing:
            raise RegistryError(f"record {index}: missing field(s) {', '.join(missing)}")
        extra: list[str] = sorted(set(raw) - set(REQUIRED_FIELDS) - set(OPTIONAL_FIELDS))
        if extra:
            raise RegistryError(f"record {index}: unknown field(s) {', '.join(extra)}")
        values: dict[str, object] = dict(raw)
        for field in REQUIRED_FIELDS:
            value: object = values[field]
            if field == "gallery_page":
                if value is not None and not isinstance(value, str):
                    raise RegistryError(f"record {index}: gallery_page must be a string or null")
            elif not isinstance(value, str):
                raise RegistryError(f"record {index}: {field} must be a string")
        alias: object = values.get("windows_forms_name")
        if alias is not None and not isinstance(alias, str):
            raise RegistryError(f"record {index}: windows_forms_name must be a string")
        return ControlRecord(
            id=str(values["id"]),
            type=str(values["type"]),
            header=str(values["header"]),
            category=str(values["category"]),
            purpose=str(values["purpose"]),
            winforms=str(values["winforms"]),
            status=str(values["status"]),
            gallery_page=values["gallery_page"] if values["gallery_page"] is None else str(values["gallery_page"]),
            doc=str(values["doc"]),
            windows_forms_name=None if alias is None else str(alias),
        )


@dataclass(frozen=True)
class Registry:
    records: tuple[ControlRecord, ...]
    notes: tuple[str, ...]

    def by_category(self) -> dict[str, list[ControlRecord]]:
        groups: dict[str, list[ControlRecord]] = {category: [] for category in CATEGORIES}
        for record in self.records:
            groups.setdefault(record.category, []).append(record)
        for members in groups.values():
            members.sort(key=lambda record: record.type.lower())
        return groups


@dataclass(frozen=True)
class ScannedType:
    name: str
    header: str  # relative to include/
    bases: tuple[str, ...]
    is_template: bool


def load_registry(path: Path = REGISTRY_PATH) -> Registry:
    try:
        document: object = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise RegistryError(f"registry not found: {path}") from error
    except json.JSONDecodeError as error:
        raise RegistryError(f"registry is not valid JSON: {path}: {error}") from error
    if not isinstance(document, dict):
        raise RegistryError("registry root must be an object")
    controls: object = document.get("controls")
    if not isinstance(controls, list):
        raise RegistryError("registry must contain a 'controls' array")
    notes_raw: object = document.get("notes", [])
    notes: tuple[str, ...] = tuple(str(note) for note in notes_raw) if isinstance(notes_raw, list) else ()
    records: list[ControlRecord] = []
    for index, raw in enumerate(controls):
        if not isinstance(raw, dict):
            raise RegistryError(f"record {index}: must be an object")
        records.append(ControlRecord.from_mapping(raw, index))
    return Registry(records=tuple(records), notes=notes)


# --------------------------------------------------------------------------- scan


def _strip_comments(source: str) -> str:
    without_blocks: str = _BLOCK_COMMENT.sub(lambda match: "\n" * match.group(0).count("\n"), source)
    return _LINE_COMMENT.sub("", without_blocks)


def _base_names(clause: str | None) -> tuple[str, ...]:
    if clause is None:
        return ()
    body: str = clause.lstrip(":")
    names: list[str] = []
    depth: int = 0
    current: list[str] = []
    for character in body:
        if character == "<":
            depth += 1
        elif character == ">":
            depth -= 1
        if character == "," and depth == 0:
            names.append("".join(current))
            current = []
        else:
            current.append(character)
    names.append("".join(current))
    result: list[str] = []
    for item in names:
        tokens: list[str] = [token for token in item.replace("\n", " ").split(" ") if token]
        tokens = [token for token in tokens if token not in {"public", "protected", "private", "virtual"}]
        if not tokens:
            continue
        head: str = tokens[0]
        template_cut: int = head.find("<")
        if template_cut >= 0:
            head = head[:template_cut]
        match: re.Match[str] | None = _BASE_NAME.fullmatch(head.strip())
        if match is not None:
            result.append(match.group(1))
    return tuple(result)


def scan_declarations(include_root: Path = INCLUDE_ROOT, header_root: Path | None = None) -> list[ScannedType]:
    """Return every namespace-level class/struct declaration under header_root."""
    root: Path = header_root if header_root is not None else include_root / "gui_forms"
    found: list[ScannedType] = []
    for header in sorted(root.rglob("*.hpp")):
        source: str = _strip_comments(header.read_text(encoding="utf-8"))
        relative: str = header.relative_to(include_root).as_posix()
        for match in _DECLARATION.finditer(source):
            found.append(
                ScannedType(
                    name=match.group("name"),
                    header=relative,
                    bases=_base_names(match.group("bases")),
                    is_template=match.group("template") is not None,
                )
            )
    return found


def scan_public_control_types(include_root: Path = INCLUDE_ROOT, header_root: Path | None = None) -> dict[str, str]:
    """Map type name -> header (relative to include/) for every registry-eligible type.

    Rule: a non-template class or struct declared at namespace level (column 0)
    in a header under include/gui_forms/ whose base-class chain, resolved across
    all scanned headers, reaches ``Control`` or ``Component``. The two roots are
    excluded. Types listed in ``ADMITTED_SERVICE_TYPES`` are added explicitly.
    """
    declarations: list[ScannedType] = scan_declarations(include_root, header_root)
    bases_of: dict[str, tuple[str, ...]] = {}
    header_of: dict[str, str] = {}
    templates: set[str] = set()
    for declaration in declarations:
        if declaration.name in header_of and not declaration.bases:
            # Keep the first full declaration; a later base-less match is a nested/forward helper.
            continue
        bases_of[declaration.name] = declaration.bases
        header_of[declaration.name] = declaration.header
        if declaration.is_template:
            templates.add(declaration.name)

    reach_cache: dict[str, bool] = {}

    def reaches_root(name: str, trail: frozenset[str]) -> bool:
        if name in HIERARCHY_ROOTS:
            return True
        if name in reach_cache:
            return reach_cache[name]
        if name in trail:
            return False
        result: bool = any(reaches_root(base, trail | {name}) for base in bases_of.get(name, ()))
        reach_cache[name] = result
        return result

    eligible: dict[str, str] = {}
    for name, header in header_of.items():
        if name in HIERARCHY_ROOTS or name in templates:
            continue
        if reaches_root(name, frozenset()):
            eligible[name] = header
    for name in ADMITTED_SERVICE_TYPES:
        if name in header_of:
            eligible[name] = header_of[name]
    return eligible


# -------------------------------------------------------------------------- check


def _type_declared_in(header_text: str, type_name: str) -> bool:
    pattern: re.Pattern[str] = re.compile(r"\b(?:class|struct)\s+" + re.escape(type_name) + r"\b\s*(?:final\b)?\s*[:{]")
    return pattern.search(_strip_comments(header_text)) is not None


def validate(registry: Registry, include_root: Path = INCLUDE_ROOT) -> list[str]:
    """Return a list of problems; empty means the registry passes."""
    problems: list[str] = []
    seen_ids: dict[str, int] = {}
    seen_types: dict[str, int] = {}
    seen_aliases: dict[str, int] = {}
    all_types: set[str] = {record.type for record in registry.records}
    for index, record in enumerate(registry.records):
        label: str = f"record {index} ({record.type or record.id or '?'})"
        if not SNAKE_CASE.match(record.id):
            problems.append(f"{label}: id '{record.id}' is not snake_case")
        elif CPP_IDENTIFIER.match(record.type) and record.id != registered_id(record.type):
            problems.append(f"{label}: id '{record.id}' is not the words of '{record.type}' "
                            f"('{registered_id(record.type)}'); renaming a type must not move its id silently")
        if record.id in seen_ids:
            problems.append(f"{label}: duplicate id '{record.id}' (also record {seen_ids[record.id]})")
        seen_ids.setdefault(record.id, index)
        if not CPP_IDENTIFIER.match(record.type):
            problems.append(f"{label}: type '{record.type}' is not a C++ identifier")
        if record.type in seen_types:
            problems.append(f"{label}: duplicate type '{record.type}' (also record {seen_types[record.type]})")
        seen_types.setdefault(record.type, index)
        if record.category not in CATEGORIES:
            problems.append(f"{label}: category '{record.category}' is not one of {', '.join(CATEGORIES)}")
        if record.status not in STATUSES:
            problems.append(f"{label}: status '{record.status}' is not one of {', '.join(STATUSES)}")
        if not record.purpose.strip():
            problems.append(f"{label}: purpose is empty")
        elif not record.purpose.rstrip().endswith("."):
            problems.append(f"{label}: purpose must be one sentence ending in a period")
        elif ". " in record.purpose.strip():
            problems.append(f"{label}: purpose must be a single sentence")
        if not record.winforms.strip():
            problems.append(f"{label}: winforms is empty (use 'GUI.Forms-only')")
        alias: str | None = record.windows_forms_name
        if alias is not None:
            if not CPP_IDENTIFIER.match(alias):
                problems.append(f"{label}: windows_forms_name '{alias}' is not a C++ identifier")
            if alias == record.type:
                problems.append(f"{label}: windows_forms_name '{alias}' is the type itself; leave it out")
            elif alias in all_types:
                problems.append(f"{label}: windows_forms_name '{alias}' is also a registry type, so an importer "
                                "would have two targets")
            if alias != record.winforms:
                problems.append(f"{label}: windows_forms_name '{alias}' must be the record's winforms "
                                f"counterpart ('{record.winforms}')")
            if alias in seen_aliases:
                problems.append(f"{label}: duplicate windows_forms_name '{alias}' (also record {seen_aliases[alias]})")
            seen_aliases.setdefault(alias, index)
        expected_doc = f"api/generated/gui_forms.{record.type}.rst"
        if record.doc != expected_doc:
            problems.append(f"{label}: doc must be '{expected_doc}', got '{record.doc}'")
        if record.gallery_page is not None and not KEBAB_CASE.match(record.gallery_page):
            problems.append(f"{label}: gallery_page '{record.gallery_page}' must be null or a kebab-case page id")
        if not record.header.startswith("gui_forms/") or not record.header.endswith(".hpp"):
            problems.append(f"{label}: header '{record.header}' must be a gui_forms/...hpp path relative to include/")
        header_path: Path = include_root / record.header
        if not header_path.is_file():
            problems.append(f"{label}: header does not exist: {record.header}")
        elif not _type_declared_in(header_path.read_text(encoding="utf-8"), record.type):
            problems.append(f"{label}: 'class {record.type}' or 'struct {record.type}' not declared in {record.header}")

    scanned: dict[str, str] = scan_public_control_types(include_root)
    registered: set[str] = {record.type for record in registry.records}
    for name in sorted(set(scanned) - registered):
        problems.append(f"coverage: public type {name} ({scanned[name]}) is missing from the registry")
    for record in registry.records:
        if record.type not in scanned:
            problems.append(
                f"coverage: registry type {record.type} is not a public control/component type by the scan rule"
            )
        elif scanned[record.type] != record.header:
            problems.append(
                f"coverage: {record.type} is declared in {scanned[record.type]}, registry says {record.header}"
            )
    return problems


# ------------------------------------------------------------------------- render


def _cell(text: str) -> str:
    return text.replace("|", "\\|").replace("\n", " ").strip()


def _code(text: str) -> str:
    return f"`{text}`"


def _page_link(record: ControlRecord) -> str:
    """The control's name linked to its reference page, relative to docs/controls/generated/."""
    return f"[`{record.type}`](../../{record.doc})"


def _site_link(record: ControlRecord) -> str:
    """The control's name linked to its reference page on the published site."""
    page: str = PurePosixPath(record.doc).with_suffix(".html").as_posix()
    return f"[`{record.type}`]({SITE_URL}{page})"


def _table(headers: Sequence[str], rows: Iterable[Sequence[str]]) -> list[str]:
    lines: list[str] = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    for row in rows:
        lines.append("| " + " | ".join(_cell(cell) for cell in row) + " |")
    return lines


def render_by_function(registry: Registry) -> str:
    rows: list[Sequence[str]] = []
    for category, members in registry.by_category().items():
        for position, record in enumerate(members):
            rows.append((category if position == 0 else "", _page_link(record), record.winforms, record.purpose))
    lines: list[str] = [
        "# Controls by function",
        "",
        "Generated from `manifests/controls.json` by `python3 tools/control_registry.py render`. Do not edit.",
        "",
        *_table(("Function", "Control", "WinForms counterpart", "Description"), rows),
        "",
    ]
    return "\n".join(lines)


def render_alphabetical(registry: Registry) -> str:
    ordered: list[ControlRecord] = sorted(registry.records, key=lambda record: record.type.lower())
    rows: list[Sequence[str]] = [
        (_page_link(record), record.category, _code(record.header), record.status, record.purpose) for record in ordered
    ]
    lines: list[str] = [
        "# Controls, alphabetical",
        "",
        "Generated from `manifests/controls.json` by `python3 tools/control_registry.py render`. Do not edit.",
        "",
        *_table(("Control", "Function", "Header", "Status", "Description"), rows),
        "",
    ]
    return "\n".join(lines)


def render_readme_table(registry: Registry) -> str:
    rows: list[Sequence[str]] = []
    for category, members in registry.by_category().items():
        for position, record in enumerate(members):
            rows.append((_site_link(record), category if position == 0 else "", record.purpose))
    return "\n".join(_table(("Control", "Function", "Description"), rows)) + "\n"


def _cpp_string(text: str) -> str:
    """A C++ narrow string literal; JSON escaping is a subset of C++ escaping."""
    return json.dumps(text, ensure_ascii=False)


def render_gallery_catalog(registry: Registry) -> str:
    """The C++ include the Gallery consumes (see demo/gallery_catalog.hpp).

    Records keep registry order so the Gallery's page headers follow the
    by-function order; a null gallery_page is emitted as the empty string.
    """
    lines: list[str] = [
        "// Generated from manifests/controls.json by `python3 tools/control_registry.py emit-cpp`.",
        "// Do not edit; `python3 tools/control_registry.py emit-cpp --check` fails when stale.",
        "// Included by demo/gallery_catalog.hpp inside namespace gui_forms::gallery after",
        "// CatalogEntry is declared. An empty gallery_page means no Gallery page demonstrates",
        "// the type yet.",
        "",
        f"inline constexpr std::array<std::string_view, {len(CATEGORIES)}> catalog_categories {{{{",
    ]
    for category in CATEGORIES:
        lines.append(f"    {_cpp_string(category)},")
    lines.append("}};")
    lines.append("")
    lines.append(f"inline constexpr std::array<CatalogEntry, {len(registry.records)}> control_catalog {{{{")
    for record in registry.records:
        fields: tuple[str, ...] = (
            record.id,
            record.type,
            record.header,
            record.category,
            record.purpose,
            record.winforms,
            record.status,
            record.gallery_page or "",
        )
        lines.append("    {" + ", ".join(_cpp_string(field) for field in fields) + "},")
    lines.append("}};")
    lines.append("")
    return "\n".join(lines)


def render_abi_catalog(registry: Registry) -> str:
    """The C ABI's type catalog (see src/abi/stable/schema/abi_schema_recipes.hpp).

    Records keep registry order, which is the schema's type order. Each entry
    instantiates the type's describe and create recipes; a type that needs a
    hand-written recipe fails to compile until abi_schema_recipes.hpp declares one.
    """
    headers: list[str] = sorted({record.header for record in registry.records})
    lines: list[str] = [
        "// Generated from manifests/controls.json by `python3 tools/control_registry.py emit-abi`.",
        "// Do not edit; `python3 tools/control_registry.py emit-abi --check` fails when stale.",
        "// Included only by abi_schema.cpp: every registry type in registry order, with the",
        "// recipes the C ABI describes and creates it by (abi_schema_recipes.hpp).",
        "",
        "#pragma once",
        "",
        '#include "abi_schema_recipes.hpp"',
        "",
    ]
    for header in headers:
        lines.append(f'#include "{header}"')
    lines.append("")
    lines.append("#include <array>")
    lines.append("")
    lines.append("namespace gui_forms::abi1 {")
    lines.append("")
    lines.append(f"inline constexpr std::array<SchemaCatalogEntry, {len(registry.records)}> schema_catalog{{{{")
    for record in registry.records:
        lines.append(
            f"    {{{_cpp_string(record.id)}, {_cpp_string(record.type)}, "
            f"&describe_type<{record.type}>, &create_type<{record.type}>, &is_exact_type<{record.type}>}},"
        )
    lines.append("}};")
    lines.append("")
    lines.append("} // namespace gui_forms::abi1")
    lines.append("")
    return "\n".join(lines)


def render_forms_catalog(registry: Registry) -> str:
    """The form compiler's type catalog (see tools/form_compiler/form_catalog.hpp).

    Records keep registry order. Each entry names the type's C++ class and
    public header for generated code, and instantiates the recipe that adds
    the type to the form schema when it can be made alone.
    """
    headers: list[str] = sorted({record.header for record in registry.records})
    lines: list[str] = [
        "// Generated from manifests/controls.json by `python3 tools/control_registry.py emit-forms`.",
        "// Do not edit; `python3 tools/control_registry.py emit-forms --check` fails when stale.",
        "// Included only by form_catalog.cpp: every registry type in registry order, with its",
        "// C++ class, its public header and the recipe that adds it to the form schema.",
        "",
        "#pragma once",
        "",
        '#include "form_catalog.hpp"',
        "",
    ]
    for header in headers:
        lines.append(f'#include "{header}"')
    lines.append("")
    lines.append("#include <array>")
    lines.append("")
    lines.append("namespace gui_forms::form_compiler {")
    lines.append("")
    lines.append(f"inline constexpr std::array<FormCatalogEntry, {len(registry.records)}> form_catalog{{{{")
    for record in registry.records:
        lines.append(
            f"    {{{_cpp_string(record.id)}, {_cpp_string(record.type)}, {_cpp_string(record.header)}, "
            f"&add_catalog_type<{record.type}>}},"
        )
    lines.append("}};")
    lines.append("")
    lines.append("} // namespace gui_forms::form_compiler")
    lines.append("")
    return "\n".join(lines)


def render_all(registry: Registry) -> dict[str, str]:
    return {BY_FUNCTION_NAME: render_by_function(registry), ALPHABETICAL_NAME: render_alphabetical(registry)}


def write_rendered(registry: Registry, directory: Path = GENERATED_DIR) -> list[Path]:
    directory.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    for name, content in render_all(registry).items():
        target: Path = directory / name
        target.write_text(content, encoding="utf-8")
        written.append(target)
    return written


def splice_readme(readme_text: str, table: str) -> str:
    """Replace the block between the README markers; raise if either marker is absent."""
    start: int = readme_text.find(README_START)
    end: int = readme_text.find(README_END)
    if start < 0 or end < 0:
        raise RegistryError(f"README markers {README_START} and {README_END} must both be present")
    if end < start:
        raise RegistryError(f"{README_END} appears before {README_START}")
    head: str = readme_text[: start + len(README_START)]
    tail: str = readme_text[end:]
    return head + "\n" + table + tail


# ---------------------------------------------------------------------------- cli


def _command_check(args: argparse.Namespace) -> int:
    registry: Registry = load_registry(args.registry)
    problems: list[str] = validate(registry, args.include)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(f"control registry: {len(problems)} problem(s)", file=sys.stderr)
        return 1
    print(f"control registry: {len(registry.records)} records OK")
    return 0


def _command_render(args: argparse.Namespace) -> int:
    registry: Registry = load_registry(args.registry)
    for path in write_rendered(registry, args.out):
        print(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path)
    return 0


def _command_readme_table(args: argparse.Namespace) -> int:
    registry: Registry = load_registry(args.registry)
    table: str = render_readme_table(registry)
    if args.write is None:
        sys.stdout.write(table)
        return 0
    target: Path = args.write
    if not target.is_file():
        print(f"readme-table: {target} does not exist", file=sys.stderr)
        return 1
    try:
        updated: str = splice_readme(target.read_text(encoding="utf-8"), table)
    except RegistryError as error:
        print(f"readme-table: {error}", file=sys.stderr)
        return 1
    target.write_text(updated, encoding="utf-8")
    print(f"readme-table: updated {target}")
    return 0


def _emit(command: str, content: str, target: Path, check: bool) -> int:
    """Writes content to target, or with check proves the committed file equals it."""
    if check:
        committed: str = target.read_text(encoding="utf-8") if target.is_file() else ""
        if committed != content:
            print(f"{command}: {target} is stale; run `python3 tools/control_registry.py {command}`", file=sys.stderr)
            return 1
        print(f"{command}: {target} is current")
        return 0
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")
    print(target.relative_to(ROOT) if target.is_relative_to(ROOT) else target)
    return 0


def _command_emit_cpp(args: argparse.Namespace) -> int:
    registry: Registry = load_registry(args.registry)
    return _emit("emit-cpp", render_gallery_catalog(registry), args.out, args.check)


def _command_emit_abi(args: argparse.Namespace) -> int:
    registry: Registry = load_registry(args.registry)
    return _emit("emit-abi", render_abi_catalog(registry), args.out, args.check)


def _command_emit_forms(args: argparse.Namespace) -> int:
    registry: Registry = load_registry(args.registry)
    return _emit("emit-forms", render_forms_catalog(registry), args.out, args.check)


def build_parser() -> argparse.ArgumentParser:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--registry", type=Path, default=REGISTRY_PATH, help="path to controls.json")
    parser.add_argument("--include", type=Path, default=INCLUDE_ROOT, help="include/ root for header checks")
    subparsers = parser.add_subparsers(dest="command", required=True)
    check: argparse.ArgumentParser = subparsers.add_parser("check", help="validate records and header coverage")
    check.set_defaults(run=_command_check)
    render: argparse.ArgumentParser = subparsers.add_parser("render", help="write the generated docs indexes")
    render.add_argument("--out", type=Path, default=GENERATED_DIR, help="output directory")
    render.set_defaults(run=_command_render)
    readme: argparse.ArgumentParser = subparsers.add_parser("readme-table", help="print or splice the README table")
    readme.add_argument("--write", type=Path, default=None, metavar="README.md", help="splice into this file")
    readme.set_defaults(run=_command_readme_table)
    emit: argparse.ArgumentParser = subparsers.add_parser("emit-cpp", help="write or check demo/gallery_catalog.inc")
    emit.add_argument("--out", type=Path, default=GALLERY_CATALOG_PATH, help="output .inc path")
    emit.add_argument("--check", action="store_true", help="fail when the committed .inc is stale")
    emit.set_defaults(run=_command_emit_cpp)
    emit_abi: argparse.ArgumentParser = subparsers.add_parser(
        "emit-abi", help="write or check src/abi/stable/schema/abi_schema_catalog.hpp")
    emit_abi.add_argument("--out", type=Path, default=ABI_CATALOG_PATH, help="output header path")
    emit_abi.add_argument("--check", action="store_true", help="fail when the committed header is stale")
    emit_abi.set_defaults(run=_command_emit_abi)
    emit_forms: argparse.ArgumentParser = subparsers.add_parser(
        "emit-forms", help="write or check tools/form_compiler/form_compiler_catalog.hpp")
    emit_forms.add_argument("--out", type=Path, default=FORMS_CATALOG_PATH, help="output header path")
    emit_forms.add_argument("--check", action="store_true", help="fail when the committed header is stale")
    emit_forms.set_defaults(run=_command_emit_forms)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args: argparse.Namespace = build_parser().parse_args(argv)
    try:
        return int(args.run(args))
    except RegistryError as error:
        print(f"control registry: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
