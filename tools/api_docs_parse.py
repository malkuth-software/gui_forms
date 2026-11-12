"""Parse the GUI.Forms headers into type, enumeration and namespace records.

The parser is deliberately conservative: it reads ``class``, ``struct`` and
``enum class`` declarations, their member functions and public data members,
namespace-scope functions, constants and aliases, and the ``//`` comment block
directly above each declaration. It never infers behaviour from a signature.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

GUI_FORMS: Path = Path(__file__).resolve().parents[1]
PUBLIC_PREFIX: str = "include/gui_forms/"




@dataclass
class Method:
    name: str
    signature: str
    access: str = "public"
    comment: str = ""


@dataclass
class Declaration:
    """A data member, constant or type alias, with its leading comment."""
    text: str
    comment: str = ""


@dataclass
class TypeRecord:
    name: str
    kind: str
    header: str
    line: int
    declaration: str = ""
    namespace: str = ""
    owner: str = ""
    bases: list[str] = field(default_factory=list)
    methods: list[Method] = field(default_factory=list)
    fields: list[Declaration] = field(default_factory=list)
    comment: str = ""
    nested_access: str = "public"

    @property
    def display_name(self) -> str:
        return f"{self.owner}::{self.name}" if self.owner else self.name

    @property
    def qualified_name(self) -> str:
        parts = [part for part in (self.namespace, self.owner, self.name) if part]
        return "::".join(parts)


@dataclass
class EnumRecord:
    name: str
    header: str
    line: int
    namespace: str = ""
    owner: str = ""
    values: list[str] = field(default_factory=list)
    nested_access: str = "public"

    @property
    def display_name(self) -> str:
        return f"{self.owner}::{self.name}" if self.owner else self.name


@dataclass
class HeaderScope:
    """Namespace-scope functions, constants and aliases declared in one header."""
    header: str
    namespace: str
    functions: list[Method] = field(default_factory=list)
    declarations: list[Declaration] = field(default_factory=list)


@dataclass
class Report:
    groups: int = 0
    types: int = 0
    enums: int = 0
    described_members: int = 0
    signature_only_members: int = 0


def without_localizable_marks(text: str) -> str:
    """The header without its GUI_FORMS_LOCALIZABLE(property) and
    GUI_FORMS_API marks.

    The first is an attribute for the localizable-literal plugin
    (gui_forms/text/localizable/localizable_property.hpp), the second the
    export macro (gui_forms/export/application_export.hpp); neither is part of
    a signature the reference shows. Only the mark and the blanks after it go,
    so every declaration keeps its line.
    """
    unmarked = re.sub(r"\bGUI_FORMS_LOCALIZABLE\(\w+\)[ \t]*", "", text)
    return re.sub(r"\bGUI_FORMS_(?:THREADING_)?API\b[ \t]*", "", unmarked)


def sanitized(text: str) -> str:
    result = list(text)
    index = 0
    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = len(text) if end < 0 else end
            for offset in range(index, end):
                result[offset] = " "
            index = end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = len(text) - 2 if end < 0 else end
            for offset in range(index, end + 2):
                if result[offset] != "\n":
                    result[offset] = " "
            index = end + 2
        elif (text[index] == "'" and index > 0 and index + 1 < len(text) and
              text[index - 1].isalnum() and text[index + 1].isalnum()):
            # C++ digit separators are not character-literal delimiters.
            index += 1
        elif text[index] in {'"', "'"}:
            quote = text[index]
            result[index] = " "
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    result[index] = " "
                    if index + 1 < len(text):
                        result[index + 1] = " "
                    index += 2
                    continue
                if text[index] == quote:
                    result[index] = " "
                    index += 1
                    break
                if result[index] != "\n":
                    result[index] = " "
                index += 1
        else:
            index += 1
    return "".join(result)


def matching_brace(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def normalize_signature(value: str) -> str:
    value = re.sub(r"\s+", " ", value).strip()
    return value[:-1].rstrip() if value.endswith("{") else value


def method_name(signature: str, owner: str) -> str | None:
    function_pointer = re.search(
        r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)", signature)
    if function_pointer:
        return function_pointer.group(1)
    prefix = signature.split("(", 1)[0].strip()
    if not prefix or prefix.startswith(("if ", "for ", "while ", "switch ")):
        return None
    operator = re.search(r"(operator\s*[^\s(]+)\s*$", prefix)
    if operator:
        return operator.group(1).replace(" ", "")
    match = re.search(r"(~?[A-Za-z_]\w*)\s*$", prefix)
    if not match:
        return None
    name = match.group(1)
    if name in {"return", "requires", "sizeof"}:
        return None
    return name


def base_names(raw: str) -> list[str]:
    if not raw:
        return []
    bases = []
    for part in raw.split(","):
        cleaned = re.sub(r"\b(public|protected|private|virtual)\b", "", part)
        cleaned = cleaned.strip().split("<", 1)[0].split("::")[-1]
        if re.fullmatch(r"[A-Za-z_]\w*", cleaned):
            bases.append(cleaned)
    return bases


def is_field(declaration: str) -> bool:
    """True for a data member such as ``Size initial_size{1120.0, 680.0}``."""
    core = re.split(r"\s*(?:\{|=)", declaration, maxsplit=1)[0].strip()
    return bool(re.search(r"[\w>*&\]]\s+[*&]*[A-Za-z_]\w*(?:\[[^\]]*\])?$", core))


NON_MEMBER_PREFIXES = re.compile(
    r"^(class|struct|enum|union|using|typedef|static_assert|namespace)\b")
NON_FIELD_PREFIXES = re.compile(r"^(friend|template|return)\b")


def declaration_head(candidate: str) -> str:
    """Text before a function body or a constructor's member initializer list.

    Braces and colons inside the parameter list (default arguments, qualified
    names) are ignored.
    """
    depth = 0
    closed = False
    for index, character in enumerate(candidate):
        if character == "(":
            depth += 1
        elif character == ")":
            depth = max(0, depth - 1)
            closed = closed or depth == 0
        elif depth == 0 and character == "{":
            return candidate[:index].strip()
        elif (depth == 0 and closed and character == ":" and
              candidate[index - 1:index] != ":" and candidate[index + 1:index + 2] != ":"):
            return candidate[:index].strip()
    return candidate.strip()


def comment_text(lines: list[str]) -> str:
    text = " ".join(re.sub(r"^/+!?\s?", "", line.strip()) for line in lines)
    text = re.sub(r"\s+", " ", text).strip()
    return "" if re.fullmatch(r"[-=*#\s]*", text) else text


def split_trailing_comment(line: str) -> tuple[str, str]:
    """Split ``code // comment`` outside string and character literals."""
    quote = ""
    index = 0
    while index < len(line):
        character = line[index]
        if quote:
            if character == "\\":
                index += 2
                continue
            if character == quote:
                quote = ""
        elif character == '"' or (character == "'" and not (
                index > 0 and line[index - 1].isalnum() and index + 1 < len(line)
                and line[index + 1].isalnum())):
            quote = character
        elif line.startswith("//", index):
            return line[:index].rstrip(), comment_text([line[index:]])
        index += 1
    return line.rstrip(), ""


def ends_at_depth_zero(clean: str) -> bool:
    """True when ``;`` or ``{`` occurs outside parentheses: a declaration's end."""
    depth = 0
    for character in clean:
        if character == "(":
            depth += 1
        elif character == ")":
            depth = max(0, depth - 1)
        elif depth == 0 and character in ";{":
            return True
    return False


def assigned_before_call(head: str) -> bool:
    """True for ``T name = f(...)``: an initialised variable, not a function."""
    depth = 0
    for index, character in enumerate(head):
        if character == "(":
            if depth == 0:
                return False
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == "<":
            depth += 1
        elif character == ">":
            depth = max(0, depth - 1)
        elif character == "=" and depth == 0:
            before = head[:index].rstrip()
            if before.endswith("operator") or head[index + 1:index + 2] == "=" or before[-1:] in "=!<>":
                continue
            return True
    return False


def declared_members(body: str, kind: str) -> tuple[list[Method], list[Declaration]]:
    """Return the methods and public declarations made directly in a body.

    A contiguous ``//`` comment block immediately above a declaration, or a
    ``//`` comment at the end of its last line, is kept as its comment.
    Friend declarations only grant access: they are recorded with access
    ``friend`` and never published, since the befriended function is declared
    at namespace scope.
    """
    clean = sanitized(body)
    access = "public" if kind in {"struct", "namespace"} else "private"
    depth = 0
    buffer: list[str] = []
    clean_buffer: list[str] = []
    comment: list[str] = []
    pending_comment = ""
    methods: list[Method] = []
    declarations_found: list[Declaration] = []
    for original_line, clean_line in zip(body.splitlines(), clean.splitlines()):
        stripped = clean_line.strip()
        original = original_line.strip()
        if original.startswith("#"):
            # Preprocessor lines are not declarations; the declarations they
            # guard are still read.
            continue
        if depth == 0 and re.fullmatch(r"(public|protected|private)\s*:", stripped):
            access = stripped.split(":", 1)[0]
            buffer.clear()
            clean_buffer.clear()
            comment.clear()
            continue
        if depth == 0 and not stripped:
            if original.startswith("//") and not buffer:
                comment.append(original)
            elif not original:
                comment.clear()
            continue
        if depth == 0:
            if not buffer:
                pending_comment = comment_text(comment)
                comment.clear()
            code, trailing = split_trailing_comment(original_line)
            buffer.append(code.strip())
            clean_buffer.append(stripped)
            if ends_at_depth_zero(" ".join(clean_buffer)):
                pending_comment = pending_comment or trailing
                record_declaration(normalize_signature(" ".join(buffer)), clean_line, access,
                                   pending_comment, methods, declarations_found)
                buffer.clear()
                clean_buffer.clear()
                pending_comment = ""
        depth += clean_line.count("{") - clean_line.count("}")
        if depth < 0:
            depth = 0
        if depth > 0 and buffer:
            buffer.clear()
            clean_buffer.clear()
            pending_comment = ""

    unique: dict[str, Method] = {}
    for method in methods:
        unique.setdefault(method.signature, method)
    seen: dict[str, Declaration] = {}
    for declaration in declarations_found:
        seen.setdefault(declaration.text, declaration)
    return list(unique.values()), list(seen.values())


def record_declaration(candidate: str, clean_line: str, access: str, comment: str,
                       methods: list[Method], found: list[Declaration]) -> None:
    is_friend = re.search(r"\bfriend\b", candidate.split("(", 1)[0]) is not None
    is_function_object = candidate.startswith("std::function<")
    function_pointer = re.search(r"\(\s*\*\s*[A-Za-z_]\w*\s*\)\s*\(", candidate) is not None
    head = declaration_head(candidate)
    if is_friend:
        name = method_name(re.sub(r"^.*?\bfriend\s+", "", head), "")
        if name and "(" in head:
            methods.append(Method(name, head.rstrip("; "), "friend", comment))
        return
    if NON_MEMBER_PREFIXES.match(head) and not head.startswith("using "):
        return
    if head.startswith("using "):
        if access == "public" and "=" in head:
            found.append(Declaration(head.rstrip("; "), comment))
        return
    if ("(" in head and not is_function_object and not function_pointer
            and not assigned_before_call(head)):
        name = method_name(head, "")
        if name:
            methods.append(Method(name, head.rstrip("; "), access, comment))
        return
    if access == "public" and not NON_FIELD_PREFIXES.match(candidate):
        declaration = candidate.rstrip("; ").strip()
        if is_field(declaration) or function_pointer:
            found.append(Declaration(declaration, comment))


def scopes(clean: str) -> list[tuple[int, int, str, str]]:
    """Return (start, end, kind, name) for every namespace and class body."""
    found: list[tuple[int, int, str, str]] = []
    pattern = re.compile(
        r"\b(namespace)\s+([A-Za-z_][\w:]*)\s*\{|"
        r"(?<!enum )\b(class|struct)\s+([A-Za-z_]\w*)\s*(?:final\s*)?(?::\s*[^\{;]+)?\s*\{")
    for match in pattern.finditer(clean):
        line_prefix = clean[clean.rfind("\n", 0, match.start()) + 1:match.start()]
        if re.search(r"\bfriend\s*$", line_prefix):
            continue
        end = matching_brace(clean, match.end() - 1)
        if end < 0:
            continue
        if match.group(1):
            found.append((match.end(), end, "namespace", match.group(2)))
        else:
            found.append((match.end(), end, "type", match.group(4)))
    return found


def enclosing(position: int, ranges: list[tuple[int, int, str, str]]) -> tuple[str, str]:
    """Return the namespace path and owning class path around ``position``."""
    namespaces: list[str] = []
    owners: list[str] = []
    for start, end, kind, name in sorted(ranges):
        if start <= position < end:
            (namespaces if kind == "namespace" else owners).append(name)
    return "::".join(namespaces), "::".join(owners)


def nested_access(clean: str, position: int, ranges: list[tuple[int, int, str, str]]) -> str:
    """Access of a declaration nested in a class: the last specifier before it.

    Members of a ``struct`` default to public, members of a ``class`` to
    private. Specifiers inside deeper nested bodies are ignored.
    """
    owners = [start for start, end, kind, _ in ranges if kind == "type" and start <= position < end]
    if not owners:
        return "public"
    body_start = max(owners)
    head = clean[max(0, body_start - 400):body_start]
    keyword = re.findall(r"\b(class|struct)\b", head)
    access = "public" if keyword and keyword[-1] == "struct" else "private"
    depth = 0
    for match in re.finditer(r"[{}]|\b(public|protected|private)\s*:(?!:)", clean[body_start:position]):
        token = match.group(0)
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
        elif depth == 0:
            access = match.group(1)
    return access


def preceding_comment(lines: list[str], line_index: int) -> str:
    """The contiguous ``//`` block directly above ``lines[line_index]``.

    A ``template <...>`` line between the comment and the declaration is
    skipped.
    """
    index = line_index - 1
    while index >= 0 and lines[index].strip().startswith("template"):
        index -= 1
    block: list[str] = []
    while index >= 0 and lines[index].strip().startswith("//"):
        block.insert(0, lines[index])
        index -= 1
    return comment_text(block)


def template_prefix(clean: str, start: int) -> str:
    """A ``template <...>`` clause that ends right before ``start``."""
    window = clean[max(0, start - 400):start]
    match = re.search(r"(template\s*<[^;{}]*>)\s*$", window)
    return normalize_signature(match.group(1)) + " " if match else ""


def declarations() -> tuple[list[TypeRecord], list[EnumRecord], list[HeaderScope]]:
    headers = sorted((GUI_FORMS / "include" / "gui_forms").rglob("*.h*"))
    headers += sorted((GUI_FORMS / "src").rglob("*.hpp"))
    types: list[TypeRecord] = []
    enums: list[EnumRecord] = []
    header_scopes: list[HeaderScope] = []
    # ``enum class`` declarations are catalogued separately; excluding their
    # ``class`` token keeps the type and enumeration branches disjoint. A base
    # or underlying-type clause stops at ``;``, so that a forward declaration
    # (``enum class E : std::uint8_t;``) is not taken for a definition whose
    # ``{`` comes later in the header.
    type_pattern = re.compile(
        r"(?<!enum )\b(class|struct)\s+([A-Za-z_]\w*)\s*(final\s*)?(?::\s*([^\{;]+))?\s*\{")
    enum_pattern = re.compile(
        r"\benum\s+class\s+([A-Za-z_]\w*)\s*(?::\s*[^\{;]+)?\s*\{")
    for path in headers:
        text = without_localizable_marks(path.read_text(encoding="utf-8"))
        lines = text.splitlines()
        clean = sanitized(text)
        relative = path.relative_to(GUI_FORMS).as_posix()
        ranges = scopes(clean)
        for match in type_pattern.finditer(clean):
            # ``friend class detail::OverlayAttachment`` is not a declaration.
            line_prefix = clean[clean.rfind("\n", 0, match.start()) + 1:match.start()]
            if re.search(r"\bfriend\s*$", line_prefix):
                continue
            end = matching_brace(clean, match.end() - 1)
            if end < 0:
                continue
            line = text.count("\n", 0, match.start()) + 1
            namespace, owner = enclosing(match.start(), ranges)
            methods, fields = declared_members(text[match.end():end], match.group(1))
            head = template_prefix(clean, match.start()) + normalize_signature(
                clean[match.start():match.end() - 1])
            types.append(TypeRecord(
                name=match.group(2),
                kind=match.group(1),
                header=relative,
                line=line,
                declaration=head,
                namespace=namespace,
                owner=owner,
                bases=base_names(match.group(4) or ""),
                methods=methods,
                fields=fields,
                comment=preceding_comment(lines, line - 1),
                nested_access=nested_access(clean, match.start(), ranges),
            ))
        for match in enum_pattern.finditer(clean):
            end = matching_brace(clean, match.end() - 1)
            if end < 0:
                continue
            namespace, owner = enclosing(match.start(), ranges)
            values = []
            for part in clean[match.end():end].split(","):
                value = re.sub(r"=.*", "", part, flags=re.DOTALL).strip()
                if re.fullmatch(r"[A-Za-z_]\w*", value):
                    values.append(value)
            enums.append(EnumRecord(
                name=match.group(1),
                header=relative,
                line=text.count("\n", 0, match.start()) + 1,
                namespace=namespace,
                owner=owner,
                values=values,
                nested_access=nested_access(clean, match.start(), ranges),
            ))
        if not relative.startswith(PUBLIC_PREFIX):
            continue
        for start, end, kind, name in ranges:
            if kind != "namespace":
                continue
            namespace, owner = enclosing(start - 1, ranges)
            if owner:
                continue
            full = "::".join(part for part in (namespace, name) if part)
            functions, scope_declarations = declared_members(text[start:end], "namespace")
            functions = [function for function in functions
                         if not re.search(r"\boperator\b", function.signature)]
            if functions or scope_declarations:
                header_scopes.append(HeaderScope(relative, full, functions, scope_declarations))
    return types, enums, header_scopes


def is_public(header: str, namespace: str) -> bool:
    if not header.startswith(PUBLIC_PREFIX) or "/detail/" in header:
        return False
    return not any(part.endswith("detail") for part in namespace.split("::"))

