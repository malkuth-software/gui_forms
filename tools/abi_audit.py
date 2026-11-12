#!/usr/bin/env python3
"""Record and audit the GUI.Forms C ABI (include/gui_forms/c/gf_abi.h).

The header is written in a restricted dialect, which this tool parses itself:
integer-literal constants, scalar typedefs, struct typedefs, function-pointer
typedefs and exported functions. The pinned Clang (any host) supplies the
record layouts for every supported target, and the parse is cross-checked
against them. The result is the ABI manifest, manifests/abi/gf_abi.json,
which binding generators read.

Commands:

- ``extract``: write the manifest (``--check``: fail when it is stale).
- ``check``: apply the ABI rules to the header, require the manifest to be
  current, and compare it with the baseline the audit policy names
  (manifests/abi/audit_policy.json) in the policy's mode. Exit status 1 on
  any finding.
- ``probe``: write the C11/C++ layout probe, a file of static assertions on
  every size and offset, compiled on every host by the real compiler
  (``--check``: fail when it is stale).
- ``exports``: require the symbols a built library exports (and, on Windows,
  those its MSVC import library declares) to equal the allowlist.
- ``coverage``: require every table slot and export in the manifest to be
  called from the given sources (the conformance kit). A slot counts when a
  source calls it through its table, spelled ``.objects.create(`` for
  gf_objects_table's create.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT: Path = Path(__file__).resolve().parent.parent
HEADER: Path = ROOT / "include" / "gui_forms" / "c" / "gf_abi.h"
INCLUDE: Path = ROOT / "include"
MANIFEST: Path = ROOT / "manifests" / "abi" / "gf_abi.json"
ALLOWLIST: Path = ROOT / "manifests" / "abi" / "gf_abi_exports.txt"
PROBE: Path = ROOT / "tests" / "abi" / "gf_abi_layout_probe.c"
POLICY: Path = ROOT / "manifests" / "abi" / "audit_policy.json"
POLICY_FORMAT: int = 1
# The audit's modes (owner decision C-1, 2026-11-01). "report", from the 0.5
# baseline to 1.0: a break passes once the policy's ledger records it with
# its reason, and an unrecorded break fails. "blocking", from 1.0: every
# break fails and the major is frozen. Rule findings on the header fail in
# both modes.
MODES: tuple[str, ...] = ("report", "blocking")
MANIFEST_FORMAT: int = 1

# The targets whose layouts must agree. Clang computes each from any host
# (-ffreestanding needs no target sysroot).
TARGETS: tuple[str, ...] = (
    "x86_64-w64-windows-gnu",
    "x86_64-pc-windows-msvc",
    "x86_64-unknown-linux-gnu",
    "aarch64-apple-darwin",
)

# Scalar types admitted in the header, with their sizes in bytes.
SCALARS: dict[str, int] = {
    "int8_t": 1, "uint8_t": 1, "int16_t": 2, "uint16_t": 2,
    "int32_t": 4, "uint32_t": 4, "int64_t": 8, "uint64_t": 8, "double": 8,
}
POINTER_SIZE: int = 8
# Words that name a type the ABI excludes: their size, signedness or
# representation is implementation-defined, or they cannot cross a binding.
FORBIDDEN_WORDS: tuple[str, ...] = (
    "enum", "union", "bool", "_Bool", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
    "long", "short", "int", "unsigned", "signed", "float", "wchar_t", "char16_t", "char32_t",
)
# A record passed by value may be at most this large; larger ones go by pointer.
BY_VALUE_LIMIT: int = 16


@dataclass
class Finding:
    rule: str
    text: str


@dataclass
class TypeRef:
    """A spelled type: a base name, constness of the pointee and pointer depth."""
    base: str
    const: bool
    pointers: int

    def spelling(self) -> str:
        prefix: str = "const " if self.const else ""
        return prefix + self.base + "*" * self.pointers


@dataclass
class Parameter:
    name: str
    type: TypeRef


@dataclass
class Signature:
    returns: TypeRef
    parameters: list[Parameter]


@dataclass
class Member:
    name: str
    type: TypeRef | None = None
    count: int = 0
    signature: Signature | None = None


@dataclass
class Record:
    name: str
    members: list[Member]


@dataclass
class Constant:
    name: str
    value: int
    type: str


@dataclass
class Declarations:
    constants: list[Constant] = field(default_factory=list)
    scalars: dict[str, str] = field(default_factory=dict)
    records: list[Record] = field(default_factory=list)
    callbacks: dict[str, Signature] = field(default_factory=dict)
    exports: dict[str, Signature] = field(default_factory=dict)


# ---------------------------------------------------------------------------
# The header dialect.

def strip_comments(text: str) -> str:
    without_blocks: str = re.sub(r"/\*[\s\S]*?\*/", " ", text)
    without_lines: str = re.sub(r"//[^\n]*", " ", without_blocks)
    return without_lines


def remove_platform_block(text: str, findings: list[Finding]) -> str:
    begin: int = text.find("/* gf:platform-begin */")
    end: int = text.find("/* gf:platform-end */")
    if begin < 0 or end < begin:
        findings.append(Finding("dialect", "the header needs one gf:platform-begin/end block"))
        return text
    return text[:begin] + text[end:]


def remove_conditional_blocks(text: str) -> str:
    """Remove the C++ linkage block and the 64-bit guard; both are fixed forms."""
    lines: list[str] = text.split("\n")
    kept: list[str] = []
    skipping: bool = False
    line: str = ""
    for line in lines:
        stripped: str = line.strip()
        if stripped == "#ifdef __cplusplus" or stripped.startswith("#if UINTPTR_MAX"):
            skipping = True
            continue
        if skipping:
            if stripped == "#endif":
                skipping = False
            continue
        kept.append(line)
    return "\n".join(kept)


def parse_constant_literal(name: str, literal: str, findings: list[Finding]) -> Constant | None:
    match: re.Match[str] | None = re.fullmatch(r"(-?)(0x[0-9a-fA-F]+|[0-9]+)(u|ull)?", literal)
    if match is None:
        findings.append(Finding("constant-literal", f"{name}: {literal!r} is not a plain integer literal"))
        return None
    magnitude: int = int(match.group(2), 0)
    value: int = -magnitude if match.group(1) == "-" else magnitude
    suffix: str = match.group(3) or ""
    kind: str = "int32"
    low: int = -(2 ** 31)
    high: int = 2 ** 31 - 1
    if suffix == "u":
        kind = "uint32"
        low = 0
        high = 2 ** 32 - 1
    elif suffix == "ull":
        kind = "uint64"
        low = 0
        high = 2 ** 64 - 1
    if value < low or value > high:
        findings.append(Finding("constant-literal", f"{name}: {literal} does not fit {kind}"))
        return None
    return Constant(name, value, kind)


def parse_directives(text: str, declarations: Declarations, findings: list[Finding]) -> str:
    kept: list[str] = []
    line: str = ""
    for line in text.split("\n"):
        stripped: str = line.strip()
        if not stripped.startswith("#"):
            kept.append(line)
            continue
        define: re.Match[str] | None = re.fullmatch(r"#define\s+(\w+)(?:\s+(.+))?", stripped)
        if define is not None and define.group(1).startswith("GF_"):
            literal: str = (define.group(2) or "").strip()
            constant: Constant | None = parse_constant_literal(define.group(1), literal, findings)
            if constant is not None:
                declarations.constants.append(constant)
            continue
        if re.fullmatch(r"#(ifndef|define)\s+GUI_FORMS_C_GF_ABI(_CLASSES)?_H|#endif|#include <stdint\.h>", stripped):
            continue
        findings.append(Finding("dialect", f"directive outside the header dialect: {stripped}"))
    return "\n".join(kept)


def split_statements(text: str) -> list[str]:
    statements: list[str] = []
    depth: int = 0
    current: list[str] = []
    character: str = ""
    for character in text:
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
        if character == ";" and depth == 0:
            statement: str = " ".join("".join(current).split())
            if statement:
                statements.append(statement)
            current = []
            continue
        current.append(character)
    trailing: str = " ".join("".join(current).split())
    if trailing:
        statements.append(trailing)
    return statements


def forbidden_word(text: str) -> str | None:
    word: str = ""
    for word in FORBIDDEN_WORDS:
        if re.search(r"\b" + word + r"\b", text):
            return word
    return None


def parse_type(text: str, context: str, findings: list[Finding]) -> TypeRef | None:
    spelled: str = " ".join(text.replace("*", " * ").split())
    tokens: list[str] = spelled.split(" ")
    const: bool = False
    if tokens and tokens[0] == "const":
        const = True
        tokens = tokens[1:]
    # A record names itself by its tag (a gf_value's elements point at
    # gf_values); the tag and the typedef share a name.
    if len(tokens) >= 2 and tokens[0] == "struct" and re.fullmatch(r"gf_\w+", tokens[1]):
        tokens = tokens[1:]
    pointers: int = 0
    while tokens and tokens[-1] == "*":
        pointers += 1
        tokens = tokens[:-1]
    if len(tokens) != 1 or not re.fullmatch(r"\w+", tokens[0]):
        findings.append(Finding("type", f"{context}: {text!r} is outside the header dialect"))
        return None
    return TypeRef(tokens[0], const, pointers)


def parse_parameters(text: str, context: str, findings: list[Finding]) -> list[Parameter] | None:
    stripped: str = text.strip()
    if stripped == "void":
        return []
    parameters: list[Parameter] = []
    piece: str = ""
    for piece in stripped.split(","):
        match: re.Match[str] | None = re.fullmatch(r"(.+?)\s*\b(\w+)", piece.strip())
        if match is None:
            findings.append(Finding("dialect", f"{context}: parameter {piece.strip()!r} needs a type and a name"))
            return None
        parsed: TypeRef | None = parse_type(match.group(1), context + " parameter " + match.group(2), findings)
        if parsed is None:
            return None
        parameters.append(Parameter(match.group(2), parsed))
    return parameters


FUNCTION_POINTER: re.Pattern[str] = re.compile(r"(.+?)\(\s*GF_CALL\s*\*\s*(\w+)\s*\)\s*\((.*)\)")


def parse_signature(returns: str, parameters: str, context: str,
                    findings: list[Finding]) -> Signature | None:
    returned: TypeRef | None = parse_type(returns, context + " result", findings)
    parsed: list[Parameter] | None = parse_parameters(parameters, context, findings)
    if returned is None or parsed is None:
        return None
    return Signature(returned, parsed)


def parse_member(text: str, record: str, findings: list[Finding]) -> Member | None:
    context: str = record + "." + text
    if ":" in text:
        findings.append(Finding("bit-field", f"{record}: bit-field {text!r}"))
        return None
    if "(" in text:
        pointer: re.Match[str] | None = FUNCTION_POINTER.fullmatch(text)
        if pointer is None:
            findings.append(Finding("calling-convention", f"{record}: function pointer {text!r} is not spelled (GF_CALL* name)"))
            return None
        signature: Signature | None = parse_signature(pointer.group(1), pointer.group(3),
                                                      record + "." + pointer.group(2), findings)
        if signature is None:
            return None
        return Member(pointer.group(2), None, 0, signature)
    array: re.Match[str] | None = re.fullmatch(r"(.+?)\s*\b(\w+)\s*\[\s*([0-9]*)\s*\]", text)
    if array is not None:
        if not array.group(3):
            findings.append(Finding("flexible-array", f"{record}: {text!r} has no fixed extent"))
            return None
        element: TypeRef | None = parse_type(array.group(1), context, findings)
        if element is None:
            return None
        return Member(array.group(2), element, int(array.group(3)), None)
    plain: re.Match[str] | None = re.fullmatch(r"(.+?)\s*\b(\w+)", text)
    if plain is None:
        findings.append(Finding("dialect", f"{record}: member {text!r} needs a type and a name"))
        return None
    member_type: TypeRef | None = parse_type(plain.group(1), context, findings)
    if member_type is None:
        return None
    return Member(plain.group(2), member_type, 0, None)


def parse_statement(statement: str, declarations: Declarations, findings: list[Finding]) -> None:
    word: str | None = forbidden_word(statement)
    if word is not None:
        findings.append(Finding("forbidden-type", f"{word!r} in: {statement}"))
        return
    record: re.Match[str] | None = re.fullmatch(r"typedef struct (\w+) \{(.*)\} (\w+)", statement)
    if record is not None:
        if record.group(1) != record.group(3):
            findings.append(Finding("dialect", f"struct {record.group(1)} must be typedef'd to the same name"))
            return
        members: list[Member] = []
        text: str = ""
        for text in split_statements(record.group(2)):
            member: Member | None = parse_member(text, record.group(1), findings)
            if member is not None:
                members.append(member)
        declarations.records.append(Record(record.group(1), members))
        return
    callback: re.Match[str] | None = re.fullmatch(r"typedef " + FUNCTION_POINTER.pattern, statement)
    if callback is not None:
        signature: Signature | None = parse_signature(callback.group(1), callback.group(3), callback.group(2), findings)
        if signature is not None:
            declarations.callbacks[callback.group(2)] = signature
        return
    scalar: re.Match[str] | None = re.fullmatch(r"typedef (\w+) (\w+)", statement)
    if scalar is not None:
        if scalar.group(1) not in SCALARS:
            findings.append(Finding("type", f"typedef {scalar.group(2)}: {scalar.group(1)} is not an admitted scalar"))
            return
        declarations.scalars[scalar.group(2)] = scalar.group(1)
        return
    exported: re.Match[str] | None = re.fullmatch(r"GF_ABI_EXPORT (.+?) GF_CALL (\w+)\((.*)\)", statement)
    if exported is not None:
        signature = parse_signature(exported.group(1), exported.group(3), exported.group(2), findings)
        if signature is not None:
            declarations.exports[exported.group(2)] = signature
        return
    if "(" in statement and "GF_CALL" not in statement:
        findings.append(Finding("calling-convention", f"function without GF_CALL: {statement}"))
        return
    findings.append(Finding("dialect", f"declaration outside the header dialect: {statement}"))


# The headers gf_abi.h includes, each once, by the include line it spells:
# their text is read in its place (since 1.3, the class tables).
INCLUDED_HEADERS: tuple[str, ...] = ("gui_forms/c/gf_abi_classes.h",)


def splice_includes(text: str, header: Path, findings: list[Finding]) -> tuple[str, list[str]]:
    """Replace each admitted include line with the included header's text.

    The included header lives beside gf_abi.h; it is part of the audited
    interface, so its records, constants and slots enter the manifest. Any
    other quoted include is outside the dialect (parse_directives reports it).
    """
    spliced: list[str] = []
    included: list[str] = []
    line: str = ""
    for line in text.split("\n"):
        found: re.Match[str] | None = re.fullmatch(r'\s*#include "([\w/.]+)"\s*', line)
        if found is None or found.group(1) not in INCLUDED_HEADERS:
            spliced.append(line)
            continue
        path: Path = header.parent / Path(found.group(1)).name
        if found.group(1) in included or not path.exists():
            findings.append(Finding("dialect", f"#include \"{found.group(1)}\": included twice or missing"))
            continue
        included.append(found.group(1))
        spliced.append(path.read_text(encoding="utf-8"))
    return "\n".join(spliced), included


def parse_header(text: str, findings: list[Finding]) -> Declarations:
    declarations: Declarations = Declarations()
    without_platform: str = remove_platform_block(text, findings)
    without_comments: str = strip_comments(without_platform)
    without_conditionals: str = remove_conditional_blocks(without_comments)
    body: str = parse_directives(without_conditionals, declarations, findings)
    statement: str = ""
    for statement in split_statements(body):
        parse_statement(statement, declarations, findings)
    return declarations


# ---------------------------------------------------------------------------
# Layouts from Clang.

@dataclass
class Layout:
    size: int
    align: int
    offsets: list[tuple[str, int]]


def clang_layouts(clang: str, header: Path, include: Path, target: str) -> dict[str, Layout]:
    source: str = f'#include "{header.resolve().as_posix()}"\n'
    command: list[str] = [clang, "-x", "c", "-std=c11", "-ffreestanding", "-target", target,
                          "-I", str(include), "-fsyntax-only",
                          "-Xclang", "-fdump-record-layouts-complete",
                          "-Xclang", "-fdump-record-layouts", "-"]
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command, input=source, text=True, encoding="utf-8", stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"clang ({target}) rejected {header}:\n{result.stderr}")
    layouts: dict[str, Layout] = {}
    name: str = ""
    offsets: list[tuple[str, int]] = []
    line: str = ""
    for line in result.stdout.splitlines():
        header_line: re.Match[str] | None = re.fullmatch(r"\s*0 \| struct (\w+)", line)
        if header_line is not None:
            name = header_line.group(1)
            offsets = []
            continue
        if not name:
            continue
        member: re.Match[str] | None = re.fullmatch(r"\s*(\d+) \|( +)(.*?)(\w+)", line)
        if member is not None and len(member.group(2)) == 3:
            offsets.append((member.group(4), int(member.group(1))))
            continue
        closing: re.Match[str] | None = re.search(r"\[sizeof=(\d+), align=(\d+)", line)
        if closing is not None:
            layouts[name] = Layout(int(closing.group(1)), int(closing.group(2)), offsets)
            name = ""
    return layouts


# ---------------------------------------------------------------------------
# Sizes, rules and the manifest.

def type_size(type_ref: TypeRef, declarations: Declarations, sizes: dict[str, int]) -> int | None:
    if type_ref.pointers > 0:
        return POINTER_SIZE
    if type_ref.base in SCALARS:
        return SCALARS[type_ref.base]
    if type_ref.base in declarations.scalars:
        return SCALARS[declarations.scalars[type_ref.base]]
    if type_ref.base in declarations.callbacks:
        return POINTER_SIZE
    if type_ref.base in sizes:
        return sizes[type_ref.base]
    return None


def known_base(type_ref: TypeRef, declarations: Declarations, records: set[str]) -> bool:
    base: str = type_ref.base
    if base in SCALARS or base in declarations.scalars or base in declarations.callbacks or base in records:
        return True
    # char and void exist only as pointees: text and untyped memory.
    return base in ("char", "void") and type_ref.pointers > 0


def check_signature(signature: Signature, context: str, declarations: Declarations,
                    records: set[str], sizes: dict[str, int], findings: list[Finding],
                    require_status: bool) -> None:
    """Check one function type. Table slots and exports return gf_status; a
    callback the library calls may instead return void or another scalar."""
    types: list[TypeRef] = [] if signature.returns.spelling() == "void" else [signature.returns]
    parameter: Parameter
    for parameter in signature.parameters:
        types.append(parameter.type)
    type_ref: TypeRef
    for type_ref in types:
        if not known_base(type_ref, declarations, records):
            findings.append(Finding("type", f"{context}: unknown type {type_ref.spelling()}"))
            continue
        if type_ref.pointers == 0 and type_ref.base in records:
            size: int = sizes.get(type_ref.base, 0)
            if size > BY_VALUE_LIMIT:
                findings.append(Finding("by-value", f"{context}: {type_ref.base} ({size} bytes) is passed by value"))
    if require_status and signature.returns.spelling() != "gf_status":
        findings.append(Finding("status", f"{context}: returns {signature.returns.spelling()}, not gf_status"))


def table_id_name(record: str) -> str:
    return "GF_TABLE_" + record[len("gf_"):-len("_table")].upper()


def build_records(declarations: Declarations, layouts: dict[str, dict[str, Layout]],
                  findings: list[Finding]) -> list[dict[str, object]]:
    sizes: dict[str, int] = {}
    names: set[str] = set()
    record: Record
    for record in declarations.records:
        names.add(record.name)
    reference: dict[str, Layout] = layouts[TARGETS[0]]
    target: str = ""
    entries: list[dict[str, object]] = []
    for record in declarations.records:
        layout: Layout | None = reference.get(record.name)
        if layout is None:
            findings.append(Finding("layout", f"{record.name}: clang reported no layout"))
            continue
        for target in TARGETS[1:]:
            other: Layout | None = layouts[target].get(record.name)
            if other is None or other.size != layout.size or other.offsets != layout.offsets:
                findings.append(Finding("layout", f"{record.name}: layout on {target} differs from {TARGETS[0]}"))
        member_names: list[str] = []
        member: Member
        for member in record.members:
            member_names.append(member.name)
        layout_names: list[str] = []
        pair: tuple[str, int]
        for pair in layout.offsets:
            layout_names.append(pair[0])
        if member_names != layout_names:
            findings.append(Finding("layout", f"{record.name}: parsed members {member_names} differ from clang's {layout_names}"))
            continue
        fields: list[dict[str, object]] = []
        expected_offset: int = 0
        index: int = 0
        for index, member in enumerate(record.members):
            offset: int = layout.offsets[index][1]
            size: int | None = POINTER_SIZE
            spelled: str = ""
            if member.signature is None and member.type is not None:
                if not known_base(member.type, declarations, names):
                    findings.append(Finding("type", f"{record.name}.{member.name}: unknown type {member.type.spelling()}"))
                    size = None
                else:
                    size = type_size(member.type, declarations, sizes)
                spelled = member.type.spelling()
                if member.count > 0 and size is not None:
                    size = size * member.count
                    spelled = f"{spelled}[{member.count}]"
            else:
                spelled = "function"
            if size is None:
                continue
            if offset != expected_offset:
                findings.append(Finding("padding", f"{record.name}: {offset - expected_offset} implicit padding bytes before {member.name}"))
            expected_offset = offset + size
            entry: dict[str, object] = {"name": member.name, "type": spelled, "offset": offset, "size": size}
            if member.signature is not None:
                entry["signature"] = signature_json(member.signature)
            fields.append(entry)
        if expected_offset != layout.size:
            findings.append(Finding("padding", f"{record.name}: {layout.size - expected_offset} implicit tail padding bytes"))
        if layout.size % 8 != 0:
            findings.append(Finding("size", f"{record.name}: size {layout.size} is not a multiple of 8"))
        sizes[record.name] = layout.size
        kind: str = "record"
        if record.name.endswith("_table"):
            kind = "table"
        elif record.name.endswith("_hooks"):
            # A class table segment (since 1.3): entries only appended.
            kind = "segment"
        elif record.members and record.members[0].name == "header" and record.members[0].type is not None and \
                record.members[0].type.spelling() == "gf_class_header":
            # A class table (since 1.3): segment pointers only appended.
            kind = "class"
        elif record.members and record.members[0].name == "struct_size":
            kind = "extensible"
        entries.append({"name": record.name, "kind": kind, "size": layout.size, "align": layout.align,
                        "fields": fields})
    for record in declarations.records:
        for member in record.members:
            if member.signature is not None:
                check_signature(member.signature, f"{record.name}.{member.name}", declarations, names, sizes,
                                findings, record.name.endswith("_table"))
    return entries


def signature_json(signature: Signature) -> dict[str, object]:
    parameters: list[dict[str, str]] = []
    parameter: Parameter
    for parameter in signature.parameters:
        parameters.append({"name": parameter.name, "type": parameter.type.spelling()})
    return {"returns": signature.returns.spelling(), "parameters": parameters}


def check_tables(declarations: Declarations, records: list[dict[str, object]],
                 constants: dict[str, Constant], findings: list[Finding]) -> None:
    table_names: set[str] = set()
    record: dict[str, object]
    for record in records:
        if record["kind"] != "table":
            continue
        name: str = str(record["name"])
        table_names.add(table_id_name(name))
        fields: list[dict[str, object]] = record["fields"]  # type: ignore[assignment]
        header: list[tuple[object, object]] = []
        entry: dict[str, object]
        for entry in fields[:2]:
            header.append((entry["name"], entry["type"]))
        if header != [("struct_size", "uint32_t"), ("abi_minor", "uint32_t")]:
            findings.append(Finding("table", f"{name}: must begin with uint32_t struct_size, uint32_t abi_minor"))
        for entry in fields[2:]:
            if entry["type"] != "function":
                findings.append(Finding("table", f"{name}.{entry['name']}: a table holds only functions after its header"))
        if table_id_name(name) not in constants:
            findings.append(Finding("table", f"{name}: no {table_id_name(name)} constant"))
    constant_name: str = ""
    for constant_name in constants:
        if constant_name.startswith("GF_TABLE_") and constant_name not in table_names:
            findings.append(Finding("table", f"{constant_name}: no matching gf_*_table record"))


def check_class_tables(records: list[dict[str, object]], constants: dict[str, Constant],
                       findings: list[Finding]) -> None:
    """The class tables' layout rules (decisions/ABI_CLASS_TABLES_DECISION.md in projects)."""
    record: dict[str, object]
    for record in records:
        kind: object = record["kind"]
        if kind not in ("segment", "class"):
            continue
        name: str = str(record["name"])
        fields: list[dict[str, object]] = record["fields"]  # type: ignore[assignment]
        stem: str = name[len("gf_"):].rsplit("_", 1)[0]
        if f"GF_CLASS_{stem.upper()}" not in constants:
            findings.append(Finding("class-table", f"{name}: no GF_CLASS_{stem.upper()} constant"))
        entry: dict[str, object]
        if kind == "segment":
            header: list[tuple[object, object]] = [(entry["name"], entry["type"]) for entry in fields[:2]]
            if header != [("struct_size", "uint32_t"), ("class_id", "gf_class_id")]:
                findings.append(Finding("class-table", f"{name}: a segment begins with uint32_t struct_size, "
                                                       "gf_class_id class_id"))
            for entry in fields[2:]:
                if entry["type"] != "gf_hook_entry":
                    findings.append(Finding("class-table", f"{name}.{entry['name']}: a segment holds only "
                                                           "gf_hook_entry entries after its header"))
            continue
        for entry in fields[1:]:
            if re.fullmatch(r"const gf_\w+_hooks\*", str(entry["type"])) is None:
                findings.append(Finding("class-table", f"{name}.{entry['name']}: a class table holds only "
                                                       "segment pointers after its header"))
    seen: dict[int, str] = {}
    constant: Constant
    for constant in constants.values():
        if not constant.name.startswith("GF_CLASS_"):
            continue
        if constant.value in seen:
            findings.append(Finding("class-table", f"{constant.name} and {seen[constant.value]} share the value "
                                                   f"{constant.value}"))
        seen[constant.value] = constant.name


def previous_since(previous: dict[str, object] | None) -> dict[str, str]:
    """Map 'table.slot' to the version that added it, from the last manifest."""
    since: dict[str, str] = {}
    if previous is None:
        return since
    tables: list[dict[str, object]] = previous.get("structs", [])  # type: ignore[assignment]
    table: dict[str, object]
    for table in tables:
        if table.get("kind") != "table":
            continue
        slot: dict[str, object]
        for slot in table["fields"]:  # type: ignore[union-attr]
            if "since" in slot:
                since[f"{table['name']}.{slot['name']}"] = str(slot["since"])
    return since


def build_manifest(header: Path, include: Path, clang: str, previous: dict[str, object] | None,
                   findings: list[Finding]) -> dict[str, object]:
    text: str = ""
    included: list[str] = []
    text, included = splice_includes(header.read_text(encoding="utf-8"), header, findings)
    declarations: Declarations = parse_header(text, findings)
    if findings:
        # A header outside the dialect is not compiled: its findings are the result.
        return {}
    layouts: dict[str, dict[str, Layout]] = {}
    target: str = ""
    for target in TARGETS:
        try:
            layouts[target] = clang_layouts(clang, header, include, target)
        except RuntimeError as error:
            findings.append(Finding("compile", str(error)))
            return {}
    records: list[dict[str, object]] = build_records(declarations, layouts, findings)
    constants: dict[str, Constant] = {}
    constant: Constant
    for constant in declarations.constants:
        if constant.name in constants:
            findings.append(Finding("constant", f"{constant.name} is defined twice"))
        constants[constant.name] = constant
    major: int = constants["GF_ABI_MAJOR"].value if "GF_ABI_MAJOR" in constants else 0
    minor: int = constants["GF_ABI_MINOR"].value if "GF_ABI_MINOR" in constants else 0
    if major == 0:
        findings.append(Finding("version", "GF_ABI_MAJOR and GF_ABI_MINOR must be defined"))
    check_tables(declarations, records, constants, findings)
    check_class_tables(records, constants, findings)
    names: set[str] = set()
    record: dict[str, object]
    for record in records:
        names.add(str(record["name"]))
    sizes: dict[str, int] = {}
    for record in records:
        sizes[str(record["name"])] = int(record["size"])  # type: ignore[arg-type]
    name: str = ""
    signature: Signature
    for name, signature in declarations.callbacks.items():
        check_signature(signature, name, declarations, names, sizes, findings, False)
    for name, signature in declarations.exports.items():
        check_signature(signature, name, declarations, names, sizes, findings, True)
    current_version: str = f"{major}.{minor}"
    previous_major: int = int(previous["abi"]["major"]) if previous is not None else 0  # type: ignore[index]
    since: dict[str, str] = previous_since(previous) if previous_major == major else {}
    for record in records:
        if record["kind"] != "table":
            continue
        slot: dict[str, object]
        for slot in record["fields"][2:]:  # type: ignore[index]
            slot["since"] = since.get(f"{record['name']}.{slot['name']}", current_version)
    constant_entries: list[dict[str, object]] = []
    for constant in declarations.constants:
        constant_entries.append({"name": constant.name, "value": constant.value, "type": constant.type})
    scalar_entries: list[dict[str, str]] = []
    base: str = ""
    for name, base in declarations.scalars.items():
        scalar_entries.append({"name": name, "type": base})
    callback_entries: list[dict[str, object]] = []
    for name, signature in declarations.callbacks.items():
        entry: dict[str, object] = {"name": name}
        entry.update(signature_json(signature))
        callback_entries.append(entry)
    export_entries: list[dict[str, object]] = []
    for name, signature in declarations.exports.items():
        exported: dict[str, object] = {"name": name}
        exported.update(signature_json(signature))
        export_entries.append(exported)
    relative: str = header.resolve().relative_to(ROOT).as_posix() if header.resolve().is_relative_to(ROOT) else header.name
    manifest: dict[str, object] = {
        "format": MANIFEST_FORMAT,
        "generator": "tools/abi_audit.py extract",
        "header": relative,
        "includes": ["include/" + name for name in included],
        "abi": {"major": major, "minor": minor},
        "targets": list(TARGETS),
        "constants": constant_entries,
        "scalars": scalar_entries,
        "callbacks": callback_entries,
        "structs": records,
        "exports": export_entries,
    }
    return manifest


def manifest_text(manifest: dict[str, object]) -> str:
    return json.dumps(manifest, indent=2) + "\n"


def load_json(path: Path) -> dict[str, object] | None:
    if not path.exists():
        return None
    loaded: dict[str, object] = json.loads(path.read_text(encoding="utf-8"))
    return loaded


# ---------------------------------------------------------------------------
# Baseline comparison.

def by_name(entries: list[dict[str, object]]) -> dict[str, dict[str, object]]:
    found: dict[str, dict[str, object]] = {}
    entry: dict[str, object]
    for entry in entries:
        found[str(entry["name"])] = entry
    return found


def compare_struct(old: dict[str, object], new: dict[str, object], version: str,
                   findings: list[Finding]) -> bool:
    """Report breaks in one record; return whether it grew."""
    name: str = str(old["name"])
    if old["kind"] != new["kind"]:
        findings.append(Finding("break", f"{name}: kind changed from {old['kind']} to {new['kind']}"))
    old_fields: list[dict[str, object]] = old["fields"]  # type: ignore[assignment]
    new_fields: list[dict[str, object]] = new["fields"]  # type: ignore[assignment]
    index: int = 0
    old_field: dict[str, object]
    for index, old_field in enumerate(old_fields):
        if index >= len(new_fields):
            findings.append(Finding("break", f"{name}.{old_field['name']}: removed"))
            return False
        new_field: dict[str, object] = new_fields[index]
        key: str = ""
        for key in ("name", "type", "offset", "size", "signature"):
            if old_field.get(key) != new_field.get(key):
                findings.append(Finding("break", f"{name}.{old_field['name']}: {key} changed from "
                                                 f"{old_field.get(key)!r} to {new_field.get(key)!r}"))
    if len(new_fields) == len(old_fields):
        if old["size"] != new["size"]:
            findings.append(Finding("break", f"{name}: size changed from {old['size']} to {new['size']}"))
        return False
    if old["kind"] == "record":
        findings.append(Finding("break", f"{name}: a fixed record without struct_size cannot grow"))
    added: dict[str, object]
    for added in new_fields[len(old_fields):]:
        if old["kind"] == "table" and added.get("since") != version:
            findings.append(Finding("break", f"{name}.{added['name']}: a new slot must be since {version}"))
    return True


def compare_manifests(old: dict[str, object], new: dict[str, object], findings: list[Finding],
                      major_frozen: bool = False) -> None:
    old_abi: dict[str, int] = old["abi"]  # type: ignore[assignment]
    new_abi: dict[str, int] = new["abi"]  # type: ignore[assignment]
    if old_abi["major"] != new_abi["major"] and major_frozen:
        findings.append(Finding("break", f"GF_ABI_MAJOR changed from {old_abi['major']} to {new_abi['major']}; "
                                         "the major is frozen"))
        return
    if old_abi["major"] != new_abi["major"]:
        print(f"abi_audit: major {new_abi['major']} differs from the baseline's {old_abi['major']}; "
              "a new major has no baseline to keep", flush=True)
        return
    if new_abi["minor"] < old_abi["minor"]:
        findings.append(Finding("break", f"GF_ABI_MINOR went back from {old_abi['minor']} to {new_abi['minor']}"))
    version: str = f"{new_abi['major']}.{new_abi['minor']}"
    additions: list[str] = []
    section: str = ""
    for section in ("constants", "scalars", "callbacks", "exports"):
        old_entries: dict[str, dict[str, object]] = by_name(old[section])  # type: ignore[arg-type]
        new_entries: dict[str, dict[str, object]] = by_name(new[section])  # type: ignore[arg-type]
        name: str = ""
        entry: dict[str, object]
        for name, entry in old_entries.items():
            if name == "GF_ABI_MINOR":
                continue
            if name not in new_entries:
                findings.append(Finding("break", f"{section}: {name} removed"))
            elif new_entries[name] != entry:
                findings.append(Finding("break", f"{section}: {name} changed from {entry} to {new_entries[name]}"))
        for name in new_entries:
            if name not in old_entries:
                additions.append(name)
    old_structs: dict[str, dict[str, object]] = by_name(old["structs"])  # type: ignore[arg-type]
    new_structs: dict[str, dict[str, object]] = by_name(new["structs"])  # type: ignore[arg-type]
    struct_name: str = ""
    old_struct: dict[str, object]
    for struct_name, old_struct in old_structs.items():
        if struct_name not in new_structs:
            findings.append(Finding("break", f"structs: {struct_name} removed"))
            continue
        if compare_struct(old_struct, new_structs[struct_name], version, findings):
            additions.append(struct_name)
    for struct_name in new_structs:
        if struct_name not in old_structs:
            additions.append(struct_name)
    if additions and new_abi["minor"] == old_abi["minor"]:
        findings.append(Finding("version", f"additions ({', '.join(additions)}) need GF_ABI_MINOR above the baseline's {old_abi['minor']}"))


# ---------------------------------------------------------------------------
# Layout probe.

def probe_text(manifest: dict[str, object]) -> str:
    lines: list[str] = [
        "/* Generated from manifests/abi/gf_abi.json by `python3 tools/abi_audit.py probe`. Do not edit. */",
        "/* Each assertion is a size or offset the audited manifest records; the real",
        "   compiler of every host checks them when it builds this file as C11 and C++. */",
        '#include "gui_forms/c/gf_abi.h"',
        "",
        "#include <stddef.h>",
        "",
        "#if defined(__cplusplus)",
        "#define GF_LAYOUT_ASSERT(condition, message) static_assert(condition, message)",
        "#else",
        "#define GF_LAYOUT_ASSERT(condition, message) _Static_assert(condition, message)",
        "#endif",
        "",
    ]
    structs: list[dict[str, object]] = manifest["structs"]  # type: ignore[assignment]
    record: dict[str, object]
    for record in structs:
        name: str = str(record["name"])
        lines.append(f'GF_LAYOUT_ASSERT(sizeof({name}) == {record["size"]}, "{name} size");')
        entry: dict[str, object]
        for entry in record["fields"]:  # type: ignore[union-attr]
            lines.append(f'GF_LAYOUT_ASSERT(offsetof({name}, {entry["name"]}) == {entry["offset"]}, '
                         f'"{name}.{entry["name"]} offset");')
    lines.append("")
    lines.append("int main(void) {")
    lines.append("    return 0;")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Exports.

def read_allowlist(path: Path) -> list[str]:
    names: list[str] = []
    line: str = ""
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped: str = line.strip()
        if stripped and not stripped.startswith("#"):
            names.append(stripped)
    return sorted(names)


def tool_output(command: list[str]) -> str:
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command, text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False)
    if result.returncode != 0:
        raise RuntimeError(" ".join(command) + " failed:\n" + result.stdout)
    return result.stdout


def find_tool(names: tuple[str, ...], directory: Path | None) -> str:
    name: str = ""
    for name in names:
        if directory is not None:
            candidate: Path = directory / (name + (".exe" if sys.platform == "win32" else ""))
            if candidate.exists():
                return str(candidate)
        found: str | None = shutil.which(name)
        if found is not None:
            return found
    raise RuntimeError(f"none of {names} found")


def library_exports(library: Path, tool_dir: Path | None) -> list[str]:
    names: list[str] = []
    line: str = ""
    if library.suffix.lower() == ".dll":
        readobj: str = find_tool(("llvm-readobj",), tool_dir)
        for line in tool_output([readobj, "--coff-exports", str(library)]).splitlines():
            match: re.Match[str] | None = re.fullmatch(r"\s*Name: (\S+)", line)
            if match is not None:
                names.append(match.group(1))
        return sorted(names)
    nm: str = find_tool(("llvm-nm", "nm"), tool_dir)
    if library.suffix == ".dylib":
        for line in tool_output([nm, "-gjU", str(library)]).splitlines():
            stripped: str = line.strip()
            if stripped:
                names.append(stripped[1:] if stripped.startswith("_") else stripped)
        return sorted(names)
    for line in tool_output([nm, "-D", "--defined-only", str(library)]).splitlines():
        entry: re.Match[str] | None = re.fullmatch(r"[0-9a-fA-F]+ [A-Za-z] (\S+)", line.strip())
        if entry is not None:
            names.append(entry.group(1))
    return sorted(set(names))


def import_library_symbols(library: Path, tool_dir: Path | None) -> list[str]:
    nm: str = find_tool(("llvm-nm",), tool_dir)
    names: set[str] = set()
    line: str = ""
    for line in tool_output([nm, str(library)]).splitlines():
        match: re.Match[str] | None = re.search(r"\b__imp_(\w+)$", line.strip())
        if match is not None:
            names.add(match.group(1))
    return sorted(names)


# ---------------------------------------------------------------------------
# Commands.

def report(findings: list[Finding]) -> int:
    finding: Finding
    for finding in findings:
        print(f"abi_audit: [{finding.rule}] {finding.text}", flush=True)
    return 1 if findings else 0


def command_extract(args: argparse.Namespace) -> int:
    findings: list[Finding] = []
    previous: dict[str, object] | None = load_json(args.manifest)
    manifest: dict[str, object] = build_manifest(args.header, args.include, args.clang, previous, findings)
    if findings:
        return report(findings)
    text: str = manifest_text(manifest)
    if args.check:
        current: str = args.manifest.read_text(encoding="utf-8") if args.manifest.exists() else ""
        if current != text:
            print(f"abi_audit: {args.manifest} is stale; run `python3 tools/abi_audit.py extract`", flush=True)
            return 1
        print(f"abi_audit: {args.manifest.name} is current", flush=True)
        return 0
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(text, encoding="utf-8", newline="\n")
    print(f"abi_audit: wrote {args.manifest}", flush=True)
    return 0


@dataclass(frozen=True)
class Policy:
    mode: str
    # The baseline manifest, or None before the 0.5 snapshot is recorded.
    baseline: Path | None
    # Recorded breaks: the exact finding text and its reason.
    ledger: dict[str, str]


def load_policy(path: Path, findings: list[Finding]) -> Policy | None:
    loaded: dict[str, object] | None = load_json(path)
    if loaded is None:
        findings.append(Finding("policy", f"{path} does not exist"))
        return None
    if loaded.get("format") != POLICY_FORMAT or loaded.get("mode") not in MODES:
        findings.append(Finding("policy", f"{path}: needs format {POLICY_FORMAT} and a mode of {list(MODES)}"))
        return None
    baseline_name: object = loaded.get("baseline")
    baseline: Path | None = None
    if isinstance(baseline_name, str):
        baseline = Path(baseline_name) if Path(baseline_name).is_absolute() else ROOT / baseline_name
    elif baseline_name is not None:
        findings.append(Finding("policy", f"{path}: baseline is a path relative to the repository, or null"))
        return None
    entries: object = loaded.get("breaks", [])
    if not isinstance(entries, list):
        findings.append(Finding("policy", f"{path}: breaks is a list"))
        return None
    ledger: dict[str, str] = {}
    entry: object
    for entry in entries:
        if (not isinstance(entry, dict) or not isinstance(entry.get("finding"), str) or
                not isinstance(entry.get("reason"), str) or not str(entry.get("reason")).strip() or
                not isinstance(entry.get("recorded"), str)):
            findings.append(Finding("policy", f"{path}: each break has a finding, a non-empty reason "
                                              f"and a recorded date: {entry!r}"))
            return None
        ledger[str(entry["finding"])] = str(entry["reason"])
    return Policy(str(loaded["mode"]), baseline, ledger)


def apply_mode(mode: str, ledger: dict[str, str], comparison: list[Finding],
               findings: list[Finding]) -> None:
    """Move the baseline comparison into findings as the mode says."""
    occurred: set[str] = set()
    finding: Finding
    for finding in comparison:
        if finding.rule != "break" or mode == "blocking":
            findings.append(finding)
            continue
        occurred.add(finding.text)
        if finding.text in ledger:
            print(f"abi_audit: recorded break: {finding.text} (reason: {ledger[finding.text]})", flush=True)
        else:
            findings.append(Finding("break", f"{finding.text} (unrecorded: add this finding and its "
                                             f"reason to the breaks of the audit policy)"))
    if mode == "report":
        text: str = ""
        for text in ledger:
            if text not in occurred:
                print(f"abi_audit: notice: the recorded break {text!r} no longer occurs", flush=True)


def command_check(args: argparse.Namespace) -> int:
    findings: list[Finding] = []
    policy: Policy | None = load_policy(args.policy, findings)
    if policy is None:
        return report(findings)
    mode: str = args.mode if args.mode is not None else policy.mode
    baseline_path: Path | None = args.baseline if args.baseline is not None else policy.baseline
    previous: dict[str, object] | None = load_json(args.manifest)
    manifest: dict[str, object] = build_manifest(args.header, args.include, args.clang, previous, findings)
    if not findings and not args.no_manifest:
        current: str = args.manifest.read_text(encoding="utf-8") if args.manifest.exists() else ""
        if current != manifest_text(manifest):
            findings.append(Finding("stale", f"{args.manifest} differs from the header; run `python3 tools/abi_audit.py extract`"))
    if baseline_path is not None and manifest:
        baseline: dict[str, object] | None = load_json(baseline_path)
        if baseline is None:
            findings.append(Finding("baseline", f"{baseline_path} does not exist"))
        else:
            comparison: list[Finding] = []
            compare_manifests(baseline, manifest, comparison, mode == "blocking")
            apply_mode(mode, policy.ledger, comparison, findings)
    elif manifest:
        print(f"abi_audit: mode {mode}; no baseline recorded yet", flush=True)
    if not findings:
        abi: dict[str, int] = manifest["abi"]  # type: ignore[assignment]
        structs: list[object] = manifest["structs"]  # type: ignore[assignment]
        print(f"abi_audit: ABI {abi['major']}.{abi['minor']}: {len(structs)} records, "
              f"{len(manifest['constants'])} constants, {len(manifest['exports'])} exports; "  # type: ignore[arg-type]
              f"0 findings", flush=True)
    return report(findings)


def command_probe(args: argparse.Namespace) -> int:
    manifest: dict[str, object] | None = load_json(args.manifest)
    if manifest is None:
        print(f"abi_audit: {args.manifest} does not exist", flush=True)
        return 1
    text: str = probe_text(manifest)
    if args.check:
        current: str = args.output.read_text(encoding="utf-8") if args.output.exists() else ""
        if current != text:
            print(f"abi_audit: {args.output} is stale; run `python3 tools/abi_audit.py probe`", flush=True)
            return 1
        print(f"abi_audit: {args.output.name} is current", flush=True)
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8", newline="\n")
    print(f"abi_audit: wrote {args.output}", flush=True)
    return 0


def command_exports(args: argparse.Namespace) -> int:
    allowed: list[str] = read_allowlist(args.allowlist)
    exported: list[str] = library_exports(args.library, args.tool_dir)
    findings: list[Finding] = []
    if exported != allowed:
        findings.append(Finding("exports", f"{args.library.name} exports {exported}; the allowlist is {allowed}"))
    if args.import_library is not None:
        declared: list[str] = import_library_symbols(args.import_library, args.tool_dir)
        if declared != allowed:
            findings.append(Finding("exports", f"{args.import_library.name} declares {declared}; the allowlist is {allowed}"))
    if not findings:
        print(f"abi_audit: {args.library.name} exports exactly {allowed}", flush=True)
    return report(findings)


def command_coverage(args: argparse.Namespace) -> int:
    manifest: dict[str, object] | None = load_json(args.manifest)
    if manifest is None:
        print(f"abi_audit: {args.manifest} does not exist", flush=True)
        return 1
    text: str = ""
    source: Path
    for source in args.sources:
        paths: list[Path] = sorted(source.rglob("*")) if source.is_dir() else [source]
        path: Path
        for path in paths:
            if path.is_file() and path.suffix in (".c", ".h", ".cpp", ".hpp"):
                text += path.read_text(encoding="utf-8") + "\n"
    called: list[str] = []
    missing: list[str] = []
    struct: dict[str, object]
    for struct in manifest["structs"]:  # type: ignore[union-attr]
        if struct["kind"] != "table":
            continue
        name: str = str(struct["name"])
        member: str = name[len("gf_"):-len("_table")]
        entry: dict[str, object]
        for entry in struct["fields"]:  # type: ignore[union-attr]
            if "signature" not in entry:
                continue
            slot: str = f"{member}.{entry['name']}"
            if re.search(r"\." + re.escape(slot) + r"\s*\(", text):
                called.append(slot)
            else:
                missing.append(slot)
    export: dict[str, object]
    for export in manifest["exports"]:  # type: ignore[union-attr]
        export_name: str = str(export["name"])
        if re.search(r"\b" + re.escape(export_name) + r"\s*\(", text):
            called.append(export_name)
        else:
            missing.append(export_name)
    total: int = len(called) + len(missing)
    print(f"abi_audit: slot coverage {len(called)} of {total}", flush=True)
    findings: list[Finding] = []
    if missing:
        findings.append(Finding("coverage", f"not called by the sources: {', '.join(missing)}"))
    return report(findings)


def build_parser() -> argparse.ArgumentParser:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subparsers: argparse._SubParsersAction = parser.add_subparsers(dest="command", required=True)
    extract: argparse.ArgumentParser = subparsers.add_parser("extract", help="write or check the manifest")
    check: argparse.ArgumentParser = subparsers.add_parser("check", help="apply the ABI rules and the baseline")
    parsed: argparse.ArgumentParser
    for parsed in (extract, check):
        parsed.add_argument("--header", type=Path, default=HEADER)
        parsed.add_argument("--include", type=Path, default=INCLUDE)
        parsed.add_argument("--manifest", type=Path, default=MANIFEST)
        parsed.add_argument("--clang", default="clang")
    extract.add_argument("--check", action="store_true")
    extract.set_defaults(run=command_extract)
    check.add_argument("--policy", type=Path, default=POLICY)
    check.add_argument("--mode", choices=MODES, help="override the policy's mode (the fixture tests)")
    check.add_argument("--baseline", type=Path, help="override the policy's baseline (the fixture tests)")
    check.add_argument("--no-manifest", action="store_true",
                       help="do not require the manifest to match (a header outside the repository)")
    check.set_defaults(run=command_check)
    probe: argparse.ArgumentParser = subparsers.add_parser("probe", help="write or check the layout probe")
    probe.add_argument("--manifest", type=Path, default=MANIFEST)
    probe.add_argument("--output", type=Path, default=PROBE)
    probe.add_argument("--check", action="store_true")
    probe.set_defaults(run=command_probe)
    exports: argparse.ArgumentParser = subparsers.add_parser("exports", help="check a built library's exports")
    exports.add_argument("--library", type=Path, required=True)
    exports.add_argument("--allowlist", type=Path, default=ALLOWLIST)
    exports.add_argument("--import-library", type=Path)
    exports.add_argument("--tool-dir", type=Path)
    exports.set_defaults(run=command_exports)
    coverage: argparse.ArgumentParser = subparsers.add_parser("coverage", help="require every slot to be called")
    coverage.add_argument("--manifest", type=Path, default=MANIFEST)
    coverage.add_argument("sources", type=Path, nargs="+")
    coverage.set_defaults(run=command_coverage)
    return parser


def main(argv: list[str] | None = None) -> int:
    args: argparse.Namespace = build_parser().parse_args(argv)
    try:
        status: int = args.run(args)
    except RuntimeError as error:
        print(f"abi_audit: {error}", flush=True)
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
