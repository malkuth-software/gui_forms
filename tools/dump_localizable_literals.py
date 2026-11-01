#!/usr/bin/env python3
"""Lists the localizable literals recorded in a linked program or library.

The GUI.Forms localizable-literal plugin (tools/localizable_plugin) writes one
table per translation unit into a section of the object file; the linker
concatenates them. This tool copies that section out of a linked file with
llvm-objcopy and prints its records, one per line:

    file:line:column  property  name  function  "text"

or, with --json, a JSON array of objects with those fields. The program
itself reads the same tables with gui_forms::localizable_literals().

With --require TEXT (repeatable) it fails unless a record holds exactly that
text, which is how the build's test checks a linked program.
"""
from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

NOTE_NAME = b'GUIForms\0'
NOTE_TYPE = 0x47464C01
SECTIONS = {
    'macho': '__DATA,__gui_forms_loc',
    'elf': '.note.gui_forms.loc',
    'coff': '.gfloc',
}


@dataclass(frozen=True)
class Literal:
    """One record: a literal passed to a localizable property."""
    property: str
    name: str
    file: str
    line: int
    column: int
    function: str
    text: str


class TableError(ValueError):
    """The section's bytes are not well-formed tables."""


def binary_format(path: Path) -> str:
    """The object format of a linked file: macho, elf or coff."""
    magic: bytes = path.read_bytes()[:4]
    if magic in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe'):
        return 'macho'
    if magic == b'\x7fELF':
        return 'elf'
    if magic[:2] == b'MZ':
        return 'coff'
    raise TableError(f'{path}: not a Mach-O, ELF or PE file (a universal binary needs lipo -thin first)')


def section_bytes(objcopy: str, path: Path) -> bytes:
    """The tables section of a linked file, empty when it has none."""
    section: str = SECTIONS[binary_format(path)]
    with tempfile.TemporaryDirectory() as scratch:
        dumped: Path = Path(scratch) / 'section.bin'
        copy: Path = Path(scratch) / 'copy'
        result: subprocess.CompletedProcess[str] = subprocess.run(
            [objcopy, f'--dump-section={section}={dumped}', str(path), str(copy)],
            capture_output=True, text=True, check=False)
        if result.returncode != 0:
            if 'not found' in result.stderr or 'no section' in result.stderr:
                return b''
            raise TableError(f'{objcopy} failed: {result.stderr.strip()}')
        return dumped.read_bytes()


class Reader:
    """Reads the tables' little-endian integers and strings within bounds."""

    def __init__(self, data: bytes) -> None:
        self.data: bytes = data
        self.offset: int = 0

    def u32(self) -> int:
        if len(self.data) - self.offset < 4:
            raise TableError('truncated integer')
        value: int = struct.unpack_from('<I', self.data, self.offset)[0]
        self.offset += 4
        return value

    def text(self) -> str:
        size: int = self.u32()
        if len(self.data) - self.offset < size:
            raise TableError('truncated string')
        value: bytes = self.data[self.offset:self.offset + size]
        self.offset += size
        return value.decode('utf-8', errors='surrogateescape')


def parse_description(data: bytes) -> list[Literal]:
    reader: Reader = Reader(data)
    count: int = reader.u32()
    literals: list[Literal] = []
    for _ in range(count):
        property_name: str = reader.text()
        name: str = reader.text()
        file: str = reader.text()
        line: int = reader.u32()
        column: int = reader.u32()
        function: str = reader.text()
        text: str = reader.text()
        literals.append(Literal(property_name, name, file, line, column, function, text))
    if reader.offset != len(data):
        raise TableError('bytes after the last record')
    return literals


def parse_tables(data: bytes) -> list[Literal]:
    """Every record of a section's tables (zero padding between them allowed)."""
    literals: list[Literal] = []
    offset: int = 0
    while offset < len(data):
        if not any(data[offset:offset + 4]):
            # A zero word where a table would start is padding: no table has
            # an empty name.
            offset += 4
            continue
        if len(data) - offset < 12:
            raise TableError('truncated note header')
        name_size, description_size, note_type = struct.unpack_from('<III', data, offset)
        name_end: int = offset + 12 + (name_size + 3) // 4 * 4
        if name_end > len(data) or description_size > len(data) - name_end:
            raise TableError('note past the end of the section')
        if data[offset + 12:offset + 12 + name_size] != NOTE_NAME or note_type != NOTE_TYPE:
            raise TableError('a note that is not a GUI.Forms table')
        literals.extend(parse_description(data[name_end:name_end + description_size]))
        offset = (name_end + description_size + 3) // 4 * 4
    return literals


def default_objcopy() -> str | None:
    for candidate in ('llvm-objcopy', '/opt/homebrew/opt/llvm@22/bin/llvm-objcopy'):
        found: str | None = shutil.which(candidate)
        if found:
            return found
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('binary', type=Path, help='a linked program or shared library')
    parser.add_argument('--objcopy', help='the llvm-objcopy to use (default: llvm-objcopy on PATH)')
    parser.add_argument('--json', action='store_true', help='print a JSON array')
    parser.add_argument('--require', action='append', default=[], metavar='TEXT',
                        help='fail unless a record holds exactly this text (repeatable)')
    args = parser.parse_args()
    objcopy: str | None = args.objcopy or default_objcopy()
    if objcopy is None:
        parser.error('no llvm-objcopy found; pass --objcopy')
    try:
        literals: list[Literal] = parse_tables(section_bytes(objcopy, args.binary))
    except TableError as error:
        print(f'dump_localizable_literals: {error}', file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps([asdict(literal) for literal in literals], ensure_ascii=False, indent=1))
    else:
        for literal in literals:
            print(f'{literal.file}:{literal.line}:{literal.column}  {literal.property}  {literal.name}  '
                  f'{literal.function}  {json.dumps(literal.text, ensure_ascii=False)}')
        print(f'{len(literals)} literal(s)', file=sys.stderr)
    texts: set[str] = {literal.text for literal in literals}
    missing: list[str] = [text for text in args.require if text not in texts]
    for text in missing:
        print(f'dump_localizable_literals: no record of {text!r}', file=sys.stderr)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
