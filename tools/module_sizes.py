#!/usr/bin/env python3
"""Module sizes of a built tree (CP29 handoff section 2.2).

Reads the shared libraries a build tree made and records, for each, its file
size, its code and data bytes and how many symbols it exports, so the sizes
before and after the export annotations can be compared. It parses the
files itself (ELF, Mach-O, PE/COFF) and needs no compiler or binary tools.

    python3 -B tools/module_sizes.py BUILD_DIR [--json OUT.json]
        Prints a table of the tree's modules; with --json also writes it.
    python3 -B tools/module_sizes.py BUILD_DIR --compare BEFORE.json
        Prints each module's sizes beside the recorded ones, with the change.

A module is a regular file (not a symbolic link) under BUILD_DIR whose name
names a first-party library (gui_forms, gui_drawing or gf_abi) and ends in
.dylib, .so, .so.N... or .dll. Code bytes are the executable sections'
sizes, data bytes the other allocated sections' (zero-filled sections
included), both from the section headers. Exports count:
- ELF: defined global or weak symbols of default or protected visibility in
  .dynsym;
- Mach-O: the terminal entries of the export trie (the first architecture of
  a universal file);
- PE: the export directory's named entries.
"""

import argparse
import json
import pathlib
import re
import struct
import sys

LIBRARY_NAME = re.compile(r'(gui_forms|gui_drawing|gf_abi)[A-Za-z0-9_.-]*\.(dylib|dll|so(\.[0-9]+)*)$')


class ModuleSizes:
    """One module's record."""

    def __init__(self, name, file_bytes, code_bytes, data_bytes, exports, format_name):
        self.name = name
        self.file_bytes = file_bytes
        self.code_bytes = code_bytes
        self.data_bytes = data_bytes
        self.exports = exports
        self.format_name = format_name

    def as_json(self):
        return {'name': self.name, 'format': self.format_name, 'file_bytes': self.file_bytes,
                'code_bytes': self.code_bytes, 'data_bytes': self.data_bytes,
                'exports': self.exports}


class FormatError(Exception):
    """The file is not a module this tool reads."""


# ELF ------------------------------------------------------------------------

SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_DYNSYM = 11
STB_GLOBAL = 1
STB_WEAK = 2
STV_DEFAULT = 0
STV_PROTECTED = 3


def read_elf(data):
    if data[4] != 2:
        raise FormatError('only 64-bit ELF is read')
    order = '<' if data[5] == 1 else '>'
    section_offset = struct.unpack_from(order + 'Q', data, 0x28)[0]
    section_entry_size, section_count = struct.unpack_from(order + 'HH', data, 0x3A)
    sections = []
    for index in range(section_count):
        base = section_offset + index * section_entry_size
        (_, section_type, flags, _, offset, size, link, _, _, entry_size) = \
            struct.unpack_from(order + 'IIQQQQIIQQ', data, base)
        sections.append((section_type, flags, offset, size, link, entry_size))
    code = 0
    allocated = 0
    exports = 0
    for section_type, flags, offset, size, link, entry_size in sections:
        if flags & SHF_ALLOC:
            if flags & SHF_EXECINSTR:
                code += size
            else:
                allocated += size
        if section_type == SHT_DYNSYM and entry_size:
            for symbol in range(1, size // entry_size):
                base = offset + symbol * entry_size
                _, info, other, section_index = struct.unpack_from(order + 'IBBH', data, base)
                binding = info >> 4
                visibility = other & 0x3
                if section_index != 0 and binding in (STB_GLOBAL, STB_WEAK) \
                        and visibility in (STV_DEFAULT, STV_PROTECTED):
                    exports += 1
    return code, allocated, exports, 'ELF'


# Mach-O ---------------------------------------------------------------------

MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
LC_SEGMENT_64 = 0x19
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x80000022
LC_DYLD_EXPORTS_TRIE = 0x80000033
S_ATTR_PURE_INSTRUCTIONS = 0x80000000
S_ATTR_SOME_INSTRUCTIONS = 0x00000400


def read_uleb(data, offset):
    """Returns (value, offset after it)."""
    value = 0
    shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value, offset


def count_trie_terminals(data, start, size):
    """Counts the export trie's terminal nodes, visiting each node once."""
    if size == 0:
        return 0
    count = 0
    pending = [0]
    visited = set()
    while pending:
        node = pending.pop()
        if node in visited or node >= size:
            continue
        visited.add(node)
        offset = start + node
        terminal_size, offset = read_uleb(data, offset)
        if terminal_size:
            count += 1
        offset += terminal_size
        children = data[offset]
        offset += 1
        for _ in range(children):
            end = data.index(b'\0', offset)
            offset = end + 1
            child, offset = read_uleb(data, offset)
            pending.append(child)
    return count


def read_macho(data, base=0):
    magic = struct.unpack_from('>I', data, base)[0]
    if magic == FAT_MAGIC:
        first_offset = struct.unpack_from('>IIIII', data, base + 8)[2]
        return read_macho(data, first_offset)
    magic = struct.unpack_from('<I', data, base)[0]
    if magic != MH_MAGIC_64:
        raise FormatError('only 64-bit Mach-O is read')
    command_count = struct.unpack_from('<I', data, base + 16)[0]
    offset = base + 32
    code = 0
    allocated = 0
    exports = 0
    for _ in range(command_count):
        command, command_size = struct.unpack_from('<II', data, offset)
        if command == LC_SEGMENT_64:
            section_count = struct.unpack_from('<I', data, offset + 64)[0]
            section = offset + 72
            for _ in range(section_count):
                size = struct.unpack_from('<Q', data, section + 40)[0]
                flags = struct.unpack_from('<I', data, section + 64)[0]
                if flags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS):
                    code += size
                else:
                    allocated += size
                section += 80
        elif command in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            export_offset, export_size = struct.unpack_from('<II', data, offset + 40)
            exports = count_trie_terminals(data, base + export_offset, export_size)
        elif command == LC_DYLD_EXPORTS_TRIE:
            trie_offset, trie_size = struct.unpack_from('<II', data, offset + 8)
            exports = count_trie_terminals(data, base + trie_offset, trie_size)
        offset += command_size
    return code, allocated, exports, 'Mach-O'


# PE/COFF --------------------------------------------------------------------

IMAGE_SCN_CNT_CODE = 0x20
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_DISCARDABLE = 0x02000000


def read_pe(data):
    header = struct.unpack_from('<I', data, 0x3C)[0]
    if data[header:header + 4] != b'PE\0\0':
        raise FormatError('no PE signature')
    section_count = struct.unpack_from('<H', data, header + 6)[0]
    optional_size = struct.unpack_from('<H', data, header + 20)[0]
    optional = header + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20B:
        raise FormatError('only PE32+ is read')
    export_rva = struct.unpack_from('<I', data, optional + 112)[0]
    sections = []
    table = optional + optional_size
    for index in range(section_count):
        base = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = \
            struct.unpack_from('<IIII', data, base + 8)
        characteristics = struct.unpack_from('<I', data, base + 36)[0]
        sections.append((virtual_size, virtual_address, raw_size, raw_offset, characteristics))
    code = 0
    allocated = 0
    exports = 0
    for virtual_size, virtual_address, raw_size, raw_offset, characteristics in sections:
        if characteristics & IMAGE_SCN_MEM_DISCARDABLE:
            continue
        size = max(virtual_size, raw_size)
        if characteristics & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE):
            code += size
        else:
            allocated += size
        if export_rva and virtual_address <= export_rva < virtual_address + size:
            directory = raw_offset + export_rva - virtual_address
            exports = struct.unpack_from('<I', data, directory + 24)[0]
    return code, allocated, exports, 'PE'


def read_module(path):
    data = path.read_bytes()
    if data[:4] == b'\x7fELF':
        code, allocated, exports, format_name = read_elf(data)
    elif data[:2] == b'MZ':
        code, allocated, exports, format_name = read_pe(data)
    else:
        code, allocated, exports, format_name = read_macho(data)
    return ModuleSizes(path.name, len(data), code, allocated, exports, format_name)


def find_modules(build_dir):
    modules = []
    for path in sorted(build_dir.rglob('*')):
        if path.is_symlink() or not path.is_file():
            continue
        if not LIBRARY_NAME.search(path.name):
            continue
        if 'CMakeFiles' in path.parts:
            continue
        try:
            modules.append(read_module(path))
        except (FormatError, struct.error, IndexError, ValueError) as failure:
            print(f'{path}: not read ({failure})', file=sys.stderr)
    return modules


def change(after, before):
    if before == 0:
        return '-'
    return f'{(after - before) * 100.0 / before:+.1f}%'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('build_dir', type=pathlib.Path)
    parser.add_argument('--json', type=pathlib.Path, help='write the records here')
    parser.add_argument('--compare', type=pathlib.Path, help='records written before')
    arguments = parser.parse_args()
    modules = find_modules(arguments.build_dir)
    if not modules:
        print(f'no first-party shared libraries under {arguments.build_dir}', file=sys.stderr)
        return 1
    if arguments.compare:
        recorded = {}
        for entry in json.loads(arguments.compare.read_text())['modules']:
            recorded[entry['name']] = entry
        print(f'{"module":44} {"file":>12} {"change":>8} {"code":>12} {"change":>8} '
              f'{"exports":>8} {"before":>8}')
        for module in modules:
            before = recorded.get(module.name)
            if before is None:
                print(f'{module.name:44} {module.file_bytes:12} {"new":>8} {module.code_bytes:12} '
                      f'{"new":>8} {module.exports:8} {"-":>8}')
                continue
            print(f'{module.name:44} {module.file_bytes:12} '
                  f'{change(module.file_bytes, before["file_bytes"]):>8} {module.code_bytes:12} '
                  f'{change(module.code_bytes, before["code_bytes"]):>8} {module.exports:8} '
                  f'{before["exports"]:8}')
    else:
        print(f'{"module":44} {"format":7} {"file":>12} {"code":>12} {"data":>12} {"exports":>8}')
        for module in modules:
            print(f'{module.name:44} {module.format_name:7} {module.file_bytes:12} '
                  f'{module.code_bytes:12} {module.data_bytes:12} {module.exports:8}')
    if arguments.json:
        records = {'build_dir': str(arguments.build_dir),
                   'modules': [module.as_json() for module in modules]}
        arguments.json.write_text(json.dumps(records, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
