#!/usr/bin/env python3
"""Convert a CSS-family form document (.form, before cp34) to the XML form.

A one-time migration tool (cp34: forms move to XML). It reads the old
syntax, which the form compiler no longer reads, and writes the XML
vocabulary that docs/user/forms.md describes. The output replaces the
input in place, keeping the `.form` extension.

    python3 -B tools/convert_form_to_xml.py <document.form>... [--check]

Rules (the design in docs/user/forms.md):

- One element per object, named by its type; `id` first, then each
  declaration as an attribute, in the order written, so the model's property
  order (and the generated header) is unchanged.
- A text value is the decoded string, XML-escaped; line feed, carriage
  return and tab as character references; a leading `bind(` is escaped with
  one backslash.
- A text collection (`items`, `classes`, or any value of several strings)
  is one attribute of whitespace-separated items, an item quoted when it
  holds whitespace or begins with a quote.
- A binding `bind(...)` is kept, its double-quoted strings rewritten with
  single quotes.
- Any other value is its tokens joined by single spaces.
- Span blocks become `<span name=... anchor=... />`.
- Comments are kept: one between an object's declarations moves before the
  element (XML has no comment inside a tag); one among children stays in
  place. A `--` inside a comment, which XML refuses, becomes `- -`.
- An object written on one line keeps its attributes on one line; otherwise
  each attribute takes a line of its own, four spaces deeper than the tag.
- A blank line before an object is kept.

A document whose object declares a property after one of its child objects
cannot keep its property order in XML and is refused, as is anything the
tool cannot read. Exit status: 0 done, 1 refused or (with --check) a
document still in the old syntax, 2 usage.
"""
from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path

FORM_NAMESPACE = "urn:gui-forms:form:1"
COLLECTION_PROPERTIES = {"items", "classes"}
INDENT = "  "
ATTRIBUTE_INDENT = "    "


class ConversionError(Exception):
    pass


# --- reading the old syntax ----------------------------------------------------

@dataclass
class Token:
    kind: str  # word, string, comment, punct, space
    text: str
    line: int


def tokenize(text: str) -> list[Token]:
    tokens: list[Token] = []
    index = 0
    line = 1
    length = len(text)
    while index < length:
        character = text[index]
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                raise ConversionError(f"line {line}: unterminated comment")
            body = text[index:end + 2]
            tokens.append(Token("comment", body, line))
            line += body.count("\n")
            index = end + 2
        elif character in " \t\r\n﻿":
            start = index
            while index < length and text[index] in " \t\r\n﻿":
                index += 1
            body = text[start:index]
            tokens.append(Token("space", body, line))
            line += body.count("\n")
        elif character in "\"'":
            start = index
            index += 1
            while index < length and text[index] != character:
                if text[index] == "\\":
                    index += 1
                if index < length and text[index] == "\n":
                    raise ConversionError(f"line {line}: a string across lines")
                index += 1
            if index >= length:
                raise ConversionError(f"line {line}: unterminated string")
            index += 1
            tokens.append(Token("string", text[start:index], line))
        elif character in "{};:(),":
            tokens.append(Token("punct", character, line))
            index += 1
        else:
            start = index
            while index < length and text[index] not in " \t\r\n{};:(),\"'" and not text.startswith("/*", index):
                index += 1
            tokens.append(Token("word", text[start:index], line))
    return tokens


def read_string(token: str) -> str:
    """The text a quoted string spells (form_model.cpp read_form_string)."""
    content = token[1:-1]
    out: list[str] = []
    index = 0
    while index < len(content):
        character = content[index]
        if character != "\\":
            out.append(character)
            index += 1
            continue
        escaped = content[index + 1] if index + 1 < len(content) else ""
        simple = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "'": "'", "\\": "\\"}
        if escaped in simple:
            out.append(simple[escaped])
            index += 2
            continue
        if escaped == "u" and index + 2 < len(content) and content[index + 2] == "{":
            close = content.find("}", index + 3)
            digits = content[index + 3:close] if close > 0 else ""
            if 1 <= len(digits) <= 6 and all(c in "0123456789abcdefABCDEF" for c in digits):
                out.append(chr(int(digits, 16)))
                index = close + 1
                continue
        raise ConversionError(f"a refused escape in {token}")
    return "".join(out)


@dataclass
class Declaration:
    name: str
    value: list[Token]
    comments_before: list[str] = field(default_factory=list)


@dataclass
class Node:
    type: str
    identity: str  # the stable ID, or a span's name ("" when anonymous)
    line: int
    one_line: bool = False
    declarations: list[Declaration] = field(default_factory=list)
    # Children in order: Node, or ("comment", text), or ("blank",).
    children: list = field(default_factory=list)
    comments_before: list[str] = field(default_factory=list)
    blank_before: bool = False


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.tokens = tokens
        self.position = 0

    def peek(self, skip_space: bool = True) -> Token | None:
        position = self.position
        while position < len(self.tokens) and skip_space and self.tokens[position].kind == "space":
            position += 1
        return self.tokens[position] if position < len(self.tokens) else None

    def next(self) -> Token:
        while self.position < len(self.tokens) and self.tokens[self.position].kind == "space":
            self.position += 1
        if self.position >= len(self.tokens):
            raise ConversionError("unexpected end")
        token = self.tokens[self.position]
        self.position += 1
        return token

    def leading(self) -> tuple[list[str], bool]:
        """Comments before the next significant token, and whether a blank line precedes it."""
        comments: list[str] = []
        blank = False
        while self.position < len(self.tokens):
            token = self.tokens[self.position]
            if token.kind == "space":
                if token.text.count("\n") >= 2:
                    blank = True
                self.position += 1
            elif token.kind == "comment":
                comments.append(token.text)
                blank = False
                self.position += 1
            elif token.kind == "punct" and token.text == ";":
                self.position += 1
            else:
                break
        return comments, blank

    def document(self) -> tuple[list[str], Node, list[str]]:
        before, _ = self.leading()
        root = self.object()
        after, _ = self.leading()
        if self.position != len(self.tokens):
            raise ConversionError(f"line {self.tokens[self.position].line}: content after the root")
        return before, root, after

    def object(self) -> Node:
        type_token = self.next()
        if type_token.kind != "word":
            raise ConversionError(f"line {type_token.line}: an object type expected")
        identity_parts: list[str] = []
        prelude_comments: list[str] = []
        while True:
            token = self.peek(skip_space=False)
            if token is None:
                raise ConversionError("unexpected end in a prelude")
            if token.kind == "punct" and token.text == "{":
                break
            self.position += 1
            if token.kind == "space":
                continue
            if token.kind == "comment":
                prelude_comments.append(token.text)
                continue
            identity_parts.append(token.text)
        open_brace = self.next()
        node = Node(type_token.text, "".join(identity_parts), type_token.line)
        node.comments_before.extend(prelude_comments)
        seen_child = False
        while True:
            comments, blank = self.leading()
            token = self.peek()
            if token is None:
                raise ConversionError(f"line {open_brace.line}: unterminated block")
            if token.kind == "punct" and token.text == "}":
                close = self.next()
                node.one_line = close.line == open_brace.line
                for comment in comments:
                    node.children.append(("comment", comment))
                return node
            # A declaration is a name and ':'; anything else is an object.
            save = self.position
            name = self.next()
            after = self.peek()
            if name.kind == "word" and after is not None and after.kind == "punct" and after.text == ":":
                self.next()
                if seen_child:
                    raise ConversionError(f"line {name.line}: {node.identity} declares {name.text} after a child object")
                value = self.value()
                declaration = Declaration(name.text, value, comments)
                node.declarations.append(declaration)
                continue
            self.position = save
            child = self.object()
            child.comments_before = comments + child.comments_before
            child.blank_before = blank
            seen_child = True
            node.children.append(child)

    def value(self) -> list[Token]:
        value: list[Token] = []
        depth = 0
        while True:
            token = self.peek(skip_space=False)
            if token is None:
                raise ConversionError("unexpected end in a value")
            if depth == 0 and token.kind == "punct" and token.text in ";}":
                if token.text == ";":
                    self.position += 1
                return [t for t in value if t.kind not in ("space", "comment")] if value else value
            self.position += 1
            if token.kind == "punct" and token.text == "(":
                depth += 1
            elif token.kind == "punct" and token.text == ")":
                depth -= 1
            value.append(token)


# --- writing XML ---------------------------------------------------------------

def escape_attribute(text: str) -> str:
    out: list[str] = []
    for character in text:
        if character == "&":
            out.append("&amp;")
        elif character == "<":
            out.append("&lt;")
        elif character == ">":
            out.append("&gt;")
        elif character == '"':
            out.append("&quot;")
        elif character == "\n":
            out.append("&#10;")
        elif character == "\r":
            out.append("&#13;")
        elif character == "\t":
            out.append("&#9;")
        else:
            out.append(character)
    return "".join(out)


def single_quoted(text: str) -> str:
    """`text` as a single-quoted form string (form_model.cpp read_form_string reads it back)."""
    out = ["'"]
    for character in text:
        if character in "'\\":
            out.append("\\" + character)
        elif character == "\n":
            out.append("\\n")
        elif character == "\r":
            out.append("\\r")
        elif character == "\t":
            out.append("\\t")
        else:
            out.append(character)
    out.append("'")
    return "".join(out)


def quote_item(text: str) -> str:
    """A collection item, quoted when it is empty, holds whitespace or begins with a quote."""
    needs = text == "" or any(c in " \t\r\n" for c in text) or text[0] in "\"'"
    return single_quoted(text) if needs else text


def requote(string_token: str) -> str:
    """A string token in a binding, single-quoted so the attribute keeps its double quotes."""
    return single_quoted(read_string(string_token))


def attribute_value(declaration: Declaration) -> str:
    tokens = declaration.value
    if not tokens:
        raise ConversionError(f"{declaration.name}: an empty value")
    if tokens[0].kind == "word" and tokens[0].text == "bind" and len(tokens) > 1 and tokens[1].text == "(":
        parts: list[str] = []
        for token in tokens:
            if token.kind == "string":
                parts.append(requote(token.text))
            elif token.text == ",":
                parts.append(", ")
            elif token.text in "()":
                parts.append(token.text)
            else:
                if parts and parts[-1] not in ("(", ", "):
                    parts.append(" ")
                parts.append(token.text)
        return "".join(parts)
    strings = [token for token in tokens if token.kind == "string"]
    if strings and len(strings) == len(tokens):
        items = [read_string(token.text) for token in strings]
        if declaration.name in COLLECTION_PROPERTIES or len(items) > 1:
            return " ".join(quote_item(item) for item in items)
        text = items[0]
        stripped = text.lstrip("\\")
        if stripped.startswith("bind("):
            text = "\\" + text
        return text
    if strings:
        raise ConversionError(f"{declaration.name}: strings mixed with other tokens")
    # Numbers, keywords, a bare identity: the tokens as written, a bare
    # identity's parts (`settings.ok`) are one word token.
    return " ".join(token.text for token in tokens)


def xml_comment(comment: str) -> str:
    body = comment[2:-2]
    while "--" in body:
        body = body.replace("--", "- -")
    if body.endswith("-"):
        body += " "
    return "<!--" + body + "-->"


def write_node(node: Node, depth: int, out: list[str], root: bool) -> None:
    indent = INDENT * depth
    for comment in node.comments_before:
        out.append(indent + xml_comment(comment) + "\n")
    for declaration in node.declarations:
        for comment in declaration.comments_before:
            out.append(indent + xml_comment(comment) + "\n")
    attributes: list[tuple[str, str]] = []
    if root:
        attributes.append(("xmlns", FORM_NAMESPACE))
    if node.type == "span":
        if node.identity:
            attributes.append(("name", node.identity))
    else:
        attributes.append(("id", node.identity))
    for declaration in node.declarations:
        attributes.append((declaration.name, attribute_value(declaration)))
    head = indent + "<" + node.type
    # The tag's first line holds the namespace and the ID; the properties
    # follow on it for a one-line object, else one per line.
    fixed = 1 + (1 if root else 0) if node.type != "span" else (1 if node.identity else 0)
    line = head
    for name, value in attributes[:fixed]:
        line += f' {name}="{escape_attribute(value)}"'
    rest = attributes[fixed:]
    if node.one_line or not rest:
        for name, value in rest:
            line += f' {name}="{escape_attribute(value)}"'
    else:
        for name, value in rest:
            line += "\n" + indent + ATTRIBUTE_INDENT + f'{name}="{escape_attribute(value)}"'
    child_items = node.children
    if not child_items:
        out.append(line + "/>\n")
        return
    out.append(line + ">\n")
    for item in child_items:
        if isinstance(item, Node):
            if item.blank_before:
                out.append("\n")
            write_node(item, depth + 1, out, False)
        else:
            out.append(INDENT * (depth + 1) + xml_comment(item[1]) + "\n")
    out.append(indent + "</" + node.type + ">\n")


def convert(text: str) -> str:
    if text.lstrip("﻿").startswith("<?xml"):
        raise ConversionError("already XML")
    before, root, after = Parser(tokenize(text)).document()
    out: list[str] = ['<?xml version="1.0" encoding="UTF-8"?>\n']
    for comment in before:
        out.append(xml_comment(comment) + "\n")
    write_node(root, 0, out, True)
    for comment in after:
        out.append(xml_comment(comment) + "\n")
    return "".join(out)


def main(arguments: list[str]) -> int:
    check = "--check" in arguments
    paths = [Path(argument) for argument in arguments if argument != "--check"]
    if not paths:
        print(__doc__.splitlines()[0], file=sys.stderr)
        return 2
    status = 0
    for path in paths:
        text = path.read_text(encoding="utf-8")
        if check:
            if not text.lstrip("﻿").startswith("<?xml"):
                print(f"{path}: still in the CSS-family syntax", file=sys.stderr)
                status = 1
            continue
        try:
            converted = convert(text)
        except ConversionError as error:
            print(f"{path}: {error}", file=sys.stderr)
            status = 1
            continue
        path.write_text(converted, encoding="utf-8")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
