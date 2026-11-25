#!/usr/bin/env python3
"""Extract exact C++ declarations; compiler facts never infer behavioral contracts."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from collections.abc import Iterator
from typing import cast
from api_reference_model import AstNode, SourceLocation, SourceRange, Symbol, Inventory, Parameter, Coverage, Contract, BaseDescription

ROOT: Path = Path(__file__).resolve().parents[1]
REFERENCE: Path = ROOT / 'docs/reference'
RECORD_KINDS: set[str] = {'CXXRecordDecl', 'RecordDecl', 'EnumDecl'}
CALLABLE_KINDS: set[str] = {'CXXMethodDecl', 'CXXConstructorDecl', 'CXXDestructorDecl', 'FunctionDecl', 'FunctionTemplateDecl'}
MEMBER_KINDS: set[str] = CALLABLE_KINDS | {'FieldDecl', 'VarDecl', 'TypeAliasDecl', 'TypedefDecl', 'EnumConstantDecl'}


def roots_from_json(text: str) -> Iterator[AstNode]:
    decoder: json.JSONDecoder = json.JSONDecoder()
    offset: int = 0
    while offset < len(text):
        while offset < len(text) and text[offset].isspace():
            offset += 1
        if offset == len(text):
            break
        decoded: tuple[object, int] = decoder.raw_decode(text, offset)
        node: AstNode = cast(AstNode, decoded[0])
        offset = decoded[1]
        yield node


def node_file(node: AstNode, inherited: str) -> str:
    location: SourceLocation = node.get('loc', {})
    spelling: SourceLocation = location.get('spellingLoc', {})
    fallback: str = spelling.get('file', inherited)
    result: str = location.get('file', fallback)
    return result


def callable_node(node: AstNode) -> AstNode:
    if node.get('kind') == 'FunctionTemplateDecl':
        child: AstNode
        for child in node.get('inner', []):
            if child.get('kind') in CALLABLE_KINDS:
                return child
    return node


def collect_comments(node: AstNode, parts: list[str]) -> None:
    if node.get('kind') == 'TextComment':
        text: str = node.get('text', '')
        parts.append(text.strip())
    child: AstNode
    for child in node.get('inner', []):
        collect_comments(child, parts)


def comment_text(node: AstNode) -> str:
    parts: list[str] = []
    child: AstNode
    for child in node.get('inner', []):
        if child.get('kind') == 'FullComment':
            collect_comments(child, parts)
    result: str = ' '.join(parts)
    return result


def slug(identifier: str) -> str:
    lowercase: str = identifier.lower()
    replaced: str = re.sub(r'[^a-z0-9]+', '-', lowercase)
    stripped: str = replaced.strip('-')
    readable: str = stripped[:110]
    encoded: bytes = identifier.encode('utf-8')
    digest: str = hashlib.sha256(encoded).hexdigest()
    result: str = readable + '-' + digest[:10]
    return result


def symbol_identifier(symbol: Symbol) -> str:
    return symbol['id']


class InventoryBuilder:
    """Own source bytes and extracted records for one synchronous inventory.

Sources are read once per header. Recursive traversal borrows nodes from the
parsed Clang tree; no node, callback or source buffer survives this builder.
"""
    def __init__(self, root: Path) -> None:
        self.root: Path = root
        self.sources: dict[str, bytes] = {}
        self.records: dict[str, Symbol] = {}
        self.functions: dict[str, Symbol] = {}
        self.aliases: dict[str, Symbol] = {}

    def source_filename(self, filename: str) -> str:
        if not filename:
            return ''
        path: Path = Path(filename)
        if path.is_absolute():
            try:
                path = path.relative_to(self.root)
            except ValueError:
                pass
        result: str = path.as_posix()
        return result

    def source_bytes(self, filename: str) -> bytes:
        if filename not in self.sources:
            path: Path = self.root / filename
            if not path.is_file():
                raise RuntimeError('Missing source for compiler declaration: ' + filename)
            self.sources[filename] = path.read_bytes()
        return self.sources[filename]

    def source_line(self, node: AstNode, filename: str) -> int:
        location: SourceLocation = node.get('loc', {})
        if 'line' in location:
            return location['line']
        source: bytes = self.source_bytes(filename)
        offset: int = location.get('offset', 0)
        result: int = source.count(b'\n', 0, offset) + 1
        return result

    def declaration(self, node: AstNode, filename: str) -> str:
        source: bytes = self.source_bytes(filename)
        bounds: SourceRange = node.get('range', {})
        begin: SourceLocation = bounds.get('begin', {})
        end: SourceLocation = bounds.get('end', {})
        begin = begin.get('spellingLoc', begin)
        end = end.get('spellingLoc', end)
        first: int | None = begin.get('offset')
        last: int | None = end.get('offset')
        if first is None or last is None:
            return ''
        # Clang offsets are UTF-8 byte offsets, not Python character positions.
        prefix_bytes: bytes = source[:first]
        prefix: str = prefix_bytes.decode('utf-8')
        attributes: re.Match[str] | None = re.search(r'(?:\[\[[^\]]*\]\]\s*)+$', prefix)
        if attributes is not None:
            preceding: str = prefix[:attributes.start()]
            preceding_bytes: bytes = preceding.encode('utf-8')
            first = len(preceding_bytes)
        last += end.get('tokLen', 1)
        actual: AstNode = callable_node(node)
        child: AstNode
        for child in actual.get('inner', []):
            if child.get('kind') == 'CompoundStmt':
                last = child['range']['begin']['offset']
                break
        declaration_bytes: bytes = source[first:last]
        declaration: str = declaration_bytes.decode('utf-8')
        declaration = declaration.strip()
        declaration = declaration.rstrip(';')
        if node.get('kind') in RECORD_KINDS:
            declaration = declaration.split('{', 1)[0]
            declaration = declaration.strip()
        if node.get('kind') == 'CXXConstructorDecl':
            depth: int = 0
            index: int = 0
            for index in range(len(declaration)):
                character: str = declaration[index]
                if character == '(':
                    depth += 1
                elif character == ')':
                    depth -= 1
                elif character == ':' and depth == 0 and declaration[index:index + 2] != '::' and declaration[index - 1:index] != ':':
                    declaration = declaration[:index]
                    declaration = declaration.rstrip()
                    break
        result: str = re.sub(r'\s+', ' ', declaration)
        return result

    def member(self, node: AstNode, scope: list[str], filename: str,
               access: str, owner_name: str = '') -> Symbol:
        actual: AstNode = callable_node(node)
        name: str = actual.get('name', '')
        if actual.get('kind') == 'CXXConstructorDecl':
            name = owner_name
        if actual.get('kind') == 'CXXDestructorDecl':
            name = '~' + owner_name
        signature: str = actual.get('type', {}).get('qualType', '')
        identifier: str = '::'.join(scope + [name]) + '|' + signature
        parameters: list[Parameter] = []
        parameter_node: AstNode
        for parameter_node in actual.get('inner', []):
            if parameter_node.get('kind') == 'ParmVarDecl':
                parameter: Parameter = {'name': parameter_node.get('name', ''),
                    'type': parameter_node.get('type', {}).get('qualType', ''),
                    'declaration': self.declaration(parameter_node, filename)}
                parameters.append(parameter)
        result: Symbol = {'id': identifier, 'name': name, 'type': signature,
            'namespace': '::'.join(scope), 'kind': node.get('kind', ''), 'access': access,
            'parameters': parameters, 'declaration': self.declaration(node, filename),
            'comment': comment_text(node), 'header': filename.removeprefix('include/'),
            'source': filename, 'line': self.source_line(actual, filename),
            'page': slug(identifier) + '.html'}
        return result

    def visit(self, node: AstNode, scope: list[str], filename: str,
              access: str = 'public', template: AstNode | None = None,
              namespace_scope: list[str] | None = None) -> None:
        if namespace_scope is None:
            namespace_scope = []
        inherited: str = node_file(node, filename)
        filename = self.source_filename(inherited)
        kind: str = node.get('kind', '')
        name: str = node.get('name', '')
        child: AstNode
        if kind == 'NamespaceDecl':
            if not name or name == 'detail':
                return
            for child in node.get('inner', []):
                self.visit(child, scope + [name], filename, namespace_scope=namespace_scope + [name])
            return
        if kind == 'ClassTemplateDecl':
            for child in node.get('inner', []):
                if child.get('kind') == 'CXXRecordDecl' and child.get('completeDefinition'):
                    self.visit(child, scope, filename, access, node, namespace_scope)
            return
        entry: Symbol = {}
        if filename.startswith('include/gui_forms/') and access != 'private' and not node.get('isImplicit') and name:
            if kind in {'FunctionDecl', 'FunctionTemplateDecl'}:
                entry = self.member(node, scope, filename, access)
                self.functions[entry['id']] = entry
                return
            if kind in {'TypeAliasDecl', 'TypedefDecl', 'VarDecl'}:
                entry = self.member(node, scope, filename, access)
                self.aliases[entry['id']] = entry
                return
        if kind not in RECORD_KINDS or not name or node.get('isImplicit') or access == 'private':
            return
        if kind != 'EnumDecl' and not node.get('completeDefinition'):
            return
        if not filename.startswith('include/gui_forms/'):
            return
        # An opaque enum declaration (`enum class E : std::uint8_t;`) names a
        # type defined in another header; only the definition is recorded.
        if kind == 'EnumDecl' and not node.get('inner') and '{' not in self.declaration(node, filename):
            return
        identifier: str = '::'.join(scope + [name])
        record_kind: str = node.get('tagUsed', 'class')
        if kind == 'EnumDecl':
            record_kind = 'enum'
        declaration_node: AstNode = node
        if template is not None:
            declaration_node = template
        declaration: str = self.declaration(declaration_node, filename)
        declaration = declaration.split('{', 1)[0]
        declaration = declaration.strip()
        bases: list[str] = []
        base: BaseDescription
        for base in node.get('bases', []):
            bases.append(base['type'].get('desugaredQualType', base['type']['qualType']))
        record: Symbol = {'id': identifier, 'name': name, 'namespace': '::'.join(scope),
            'kind': record_kind, 'header': filename.removeprefix('include/'), 'source': filename,
            'line': self.source_line(node, filename), 'declaration': declaration,
            'comment': comment_text(declaration_node), 'access': access, 'bases': bases,
            'members': [], 'page': slug(identifier) + '.html'}
        record['namespace'] = '::'.join(namespace_scope)
        record['enclosing_type'] = None
        if scope != namespace_scope:
            record['enclosing_type'] = '::'.join(scope)
        current_access: str = 'public'
        if record_kind == 'class':
            current_access = 'private'
        for child in node.get('inner', []):
            child_kind: str = child.get('kind', '')
            if child_kind == 'AccessSpecDecl':
                current_access = child['access']
                continue
            if child_kind == 'FriendDecl':
                friend: AstNode
                for friend in child.get('inner', []):
                    if friend.get('kind') in {'FunctionDecl', 'FunctionTemplateDecl'}:
                        entry = self.member(friend, namespace_scope, filename, 'public')
                        entry['associated_type'] = identifier
                        pattern: str = r'(?<![\w:])' + re.escape(name) + r'\b'
                        signature: str = re.sub(pattern, identifier, entry['type'])
                        entry['id'] = '::'.join(namespace_scope + [entry['name']]) + '|' + signature
                        entry['page'] = slug(entry['id']) + '.html'
                        self.functions[entry['id']] = entry
                continue
            if current_access == 'private' or child.get('isImplicit'):
                continue
            if child_kind in RECORD_KINDS or child_kind == 'ClassTemplateDecl':
                self.visit(child, scope + [name], filename, current_access, namespace_scope=namespace_scope)
                continue
            if child_kind not in MEMBER_KINDS:
                continue
            entry = self.member(child, scope + [name], filename, current_access, name)
            record['members'].append(entry)
        previous: Symbol | None = self.records.get(identifier)
        if previous is not None and previous['source'] != filename:
            raise RuntimeError('Ambiguous API identity: ' + identifier)
        self.records[identifier] = record


def inventory(ast_text: str) -> Inventory:
    builder: InventoryBuilder = InventoryBuilder(ROOT)
    root: AstNode
    for root in roots_from_json(ast_text):
        builder.visit(root, [], '')
    result: Inventory = {'schema': 2,
        'types': sorted(builder.records.values(), key=symbol_identifier),
        'functions': sorted(builder.functions.values(), key=symbol_identifier),
        'aliases': sorted(builder.aliases.values(), key=symbol_identifier)}
    return result


INCLUDE_DIRECTORIES_FILE: str = 'api_reference_include_directories.txt'


def build_include_directories(build: Path) -> list[str]:
    """The public headers' include directories, as the CMake build at `build` wrote them.

    CMake writes gui_forms_core's transitive INTERFACE_INCLUDE_DIRECTORIES
    (its own and its public dependencies') one per line at configure time, so
    a library added to the public headers needs no change here.
    """
    listing: Path = build / INCLUDE_DIRECTORIES_FILE
    if not listing.exists():
        raise SystemExit(f'{listing} is missing; configure GUI.Forms with tests on (it is written beside '
                         'the gui_forms_api_reference target) and pass that tree as --build')
    directories: list[str] = []
    line: str
    for line in listing.read_text(encoding='utf-8').splitlines():
        # CMake lists a directory once per target that publishes it; one -I each is enough.
        if line.strip() and line.strip() not in directories:
            directories.append(line.strip())
    if not directories:
        raise SystemExit(f'{listing} lists no include directories')
    return directories


def extract(compiler: str, include_directories: list[str]) -> Inventory:
    scratch: str
    with tempfile.TemporaryDirectory(prefix='gui-forms-api-') as scratch:
        translation: Path = Path(scratch) / 'public.cpp'
        include_root: Path = ROOT / 'include'
        public_headers: Path = include_root / 'gui_forms'
        includes: list[str] = []
        path: Path
        for path in sorted(public_headers.rglob('*.hpp')):
            relative: Path = path.relative_to(include_root)
            includes.append('#include "' + str(relative) + '"\n')
        translation_text: str = ''.join(includes)
        translation.write_text(translation_text, encoding='utf-8')
        command: list[str] = [compiler, '-std=c++20', '-x', 'c++', '-I' + str(include_root)]
        directory: str
        for directory in include_directories:
            # A tests tree lists the test cache's copy of GUI.Forms; its
            # headers would answer first and name files outside this
            # checkout, which the inventory skips (an empty inventory, then
            # "Stale contract"). This checkout's include/ stands in for any
            # other copy's.
            if Path(directory, 'gui_forms').is_dir() and Path(directory).resolve() != include_root.resolve():
                continue
            command.append('-I' + directory)
        command.extend(['-fsyntax-only', '-Xclang', '-ast-dump=json', '-Xclang', '-ast-dump-filter=gui_',
                        str(translation)])
        compiled: subprocess.CompletedProcess[str] = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True, check=False, encoding='utf-8')
        if compiled.returncode:
            raise RuntimeError(compiled.stderr)
        result: Inventory = inventory(compiled.stdout)
    return result


def main() -> None:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default='clang++')
    parser.add_argument('--ast', type=Path, help='Read an already captured Clang AST')
    parser.add_argument('--build', type=Path, default=None,
        help='A configured GUI.Forms build tree; the public headers compile with the include directories '
             'it records (required unless --ast)')
    parser.add_argument('--output', type=Path, default=None,
        help='Also render the standalone HTML site into this directory (off by default)')
    parser.add_argument('--markdown', type=Path, default=None,
        help='Also render MyST Markdown pages (index.md, coverage.md, api/*.md) into this directory '
             '(off by default; the documentation site uses docs/api/ from tools/generate_library_docs.py)')
    parser.add_argument('--check', action='store_true',
        help='Fail if the committed declaration inventory is stale or a contract is invalid')
    parser.add_argument('--inventory-only', action='store_true')
    args: argparse.Namespace = parser.parse_args()
    data: Inventory
    if args.ast:
        ast_text: str = args.ast.read_text(encoding='utf-8')
        data = inventory(ast_text)
    elif args.build is None:
        raise SystemExit('pass --build <configured GUI.Forms build tree> (or --ast)')
    else:
        data = extract(args.clang, build_include_directories(args.build))
    REFERENCE.mkdir(exist_ok=True)
    encoded: str = json.dumps(data, indent=2) + '\n'
    inventory_path: Path = REFERENCE / 'inventory.json'
    if args.check:
        if not inventory_path.exists() or inventory_path.read_text(encoding='utf-8') != encoded:
            raise SystemExit('API inventory is stale; run tools/api_reference.py --build <build tree>')
    else:
        inventory_path.write_text(encoded, encoding='utf-8')
    from api_reference_render import load_contracts, render, render_markdown
    contracts: dict[str, Contract] = load_contracts(REFERENCE / 'contracts', data, ROOT)
    coverage: Coverage
    if args.check:
        print('API reference check: PASS (inventory is current and every contract is valid)')
    elif not args.inventory_only:
        if args.markdown is not None:
            coverage = render_markdown(data, contracts, args.markdown, ROOT)
            print(str(coverage['reviewed_contracts']) + ' reviewed contracts / ' + str(coverage['symbols']) + ' declarations')
            print('Markdown reference: ' + str(args.markdown / 'index.md'))
        if args.output is not None:
            coverage = render(data, contracts, args.output, ROOT)
            print('HTML reference: ' + str(args.output / 'index.html'))
    member_count: int = 0
    record: Symbol
    for record in data['types']:
        member_count += len(record['members'])
    print(str(len(data['types'])) + ' exact public/protected C++ types; ' +
          str(member_count) + ' declared members; ' + str(len(data['functions'])) + ' namespace functions')


if __name__ == '__main__':
    main()
