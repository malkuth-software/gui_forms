"""Render API reference pages as reStructuredText in the NumPy layout.

Every public declaration becomes a Sphinx C++ domain object, so the search
box finds each type and member and cross-references resolve. A type with its
own page is laid out like a numpydoc class page: include line, summary,
Parameters, Properties, Events, inherited members, a Methods summary table,
See also, Notes, Examples, then the exact declarations of every member.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field

from api_docs_parse import Declaration, EnumRecord, HeaderScope, Method, TypeRecord
from api_docstrings import Docstring

INDENT = "   "


# --- member shapes -------------------------------------------------------------

@dataclass
class Property:
    name: str
    type: str
    getter: Method
    setters: list[Method] = field(default_factory=list)


@dataclass
class EventMember:
    name: str
    arguments: str
    method: Method


@dataclass
class Shape:
    constructors: list[Method] = field(default_factory=list)
    properties: list[Property] = field(default_factory=list)
    events: list[EventMember] = field(default_factory=list)
    methods: list[Method] = field(default_factory=list)
    extension: list[Method] = field(default_factory=list)


# A const, argument-free member named by one of these verbs, or starting with
# one of these prefixes, does something (show(), request_close()) rather than
# report a property. Prefixes like show_ or cancel_ are left alone: show_ticks
# and cancel_button are properties.
ACTION_NAMES: frozenset[str] = frozenset({
    "activate", "cancel", "clear", "close", "flush", "focus", "hide", "invoke",
    "open", "refresh", "reset", "run", "show", "start", "stop", "update",
})
ACTION_PREFIXES: tuple[str, ...] = (
    "request_", "toggle_", "perform_", "begin_", "end_", "dispatch_", "emit_", "notify_",
    "acquire_", "bring_",
)


def is_action(name: str) -> bool:
    return name in ACTION_NAMES or name.startswith(ACTION_PREFIXES)


SPECIFIERS = re.compile(r"\b(?:inline|constexpr|consteval|virtual|static|explicit|friend)\s+")


def clean_signature(signature: str) -> str:
    signature = re.sub(r"\[\[[^\]]*\]\]\s*", "", signature)
    # The effect pass's cost mark (gui_forms/engine/effect/effect.hpp) is an
    # attribute for the plugin, not part of the signature.
    signature = re.sub(r"\bGUI_FORMS_EFFECT_VOCABULARY\s*\([^()]*\)\s*", "", signature)
    signature = re.sub(r"\s+", " ", signature).strip()
    signature = re.sub(r"\(\s+", "(", re.sub(r"\s+\)", ")", signature))
    return signature.rstrip(";").strip()


def return_type(method: Method) -> str:
    head = clean_signature(method.signature).split("(", 1)[0]
    head = re.sub(r"^template\s*<.*?>\s*", "", head)
    head = SPECIFIERS.sub("", head).strip()
    return head[:-len(method.name)].strip() if head.endswith(method.name) else ""


def parameters(method: Method) -> str:
    signature = clean_signature(method.signature)
    start = signature.find("(")
    depth = 0
    for index in range(start, len(signature)):
        if signature[index] == "(":
            depth += 1
        elif signature[index] == ")":
            depth -= 1
            if depth == 0:
                return signature[start + 1:index].strip()
    return ""


def split_parameters(text: str) -> list[str]:
    parts: list[str] = []
    depth = 0
    current = ""
    for character in text:
        if character in "(<{[":
            depth += 1
        elif character in ")>}]":
            depth -= 1
        if character == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += character
    if current.strip():
        parts.append(current.strip())
    return parts


def parameter_parts(parameter: str) -> tuple[str, str, str]:
    """Return (type, name, default) for one declared parameter."""
    declaration, _, default = parameter.partition("=")
    declaration = declaration.strip()
    match = re.search(r"([A-Za-z_]\w*)\s*$", declaration)
    if not match or declaration == match.group(1):
        return declaration, "", default.strip()
    return declaration[:match.start()].strip(), match.group(1), default.strip()


def display_type(type_text: str) -> str:
    type_text = re.sub(r"^const\s+", "", type_text.strip())
    return type_text[:-1].strip() if type_text.endswith("&") and not type_text.endswith("&&") else type_text


def is_deleted(method: Method) -> bool:
    return re.search(r"=\s*delete\s*$", clean_signature(method.signature)) is not None


def is_override(method: Method) -> bool:
    tail = clean_signature(method.signature).rsplit(")", 1)[-1]
    return re.search(r"\b(override|final)\b", tail) is not None


def is_const(method: Method) -> bool:
    tail = clean_signature(method.signature).rsplit(")", 1)[-1]
    return re.search(r"\bconst\b", tail) is not None


def shape_of(record: TypeRecord) -> Shape:
    shape = Shape()
    getters: dict[str, Property] = {}
    setters: list[Method] = []
    for method in record.methods:
        if method.access not in {"public", "protected"} or is_deleted(method):
            continue
        name = method.name
        if name.startswith("~") or name.startswith("operator"):
            continue
        if name == record.name:
            if method.access == "public":
                shape.constructors.append(method)
            continue
        if (method.access == "protected" or is_override(method) or
                (re.search(r"\bvirtual\b", method.signature) and not name.startswith("set_"))):
            shape.extension.append(method)
            continue
        returned = return_type(method)
        event = re.fullmatch(r"(?:const\s+)?Event<(.*)>\s*&", returned)
        if event and not parameters(method):
            shape.events.append(EventMember(name, event.group(1), method))
            continue
        if (not parameters(method) and returned and returned != "void" and is_const(method)
                and not name.startswith("set_") and not is_action(name)):
            if name not in getters:
                getters[name] = Property(name, display_type(returned), method)
                shape.properties.append(getters[name])
                continue
        if name.startswith("set_"):
            setters.append(method)
            continue
        shape.methods.append(method)
    for setter in setters:
        target = getters.get(setter.name[len("set_"):])
        if target is not None:
            target.setters.append(setter)
        else:
            shape.methods.append(setter)
    return shape


# --- text ------------------------------------------------------------------------

def title(text: str, character: str = "=", overline: bool = False) -> list[str]:
    rule = character * max(len(text), 3)
    return ([rule] if overline else []) + [text, rule]


def literal(text: str) -> str:
    text = text.strip()
    if not text:
        return ""
    if "``" in text:
        return ":code:`" + text.replace("`", "\\`") + "`"
    if text.startswith("`") or text.endswith("`"):
        return ":code:`" + text.replace("`", "\\`") + "`"
    return f"``{text}``"


def indent(text: str, levels: int = 1) -> str:
    prefix = INDENT * levels
    return "\n".join(prefix + line if line.strip() else "" for line in text.splitlines())


class Linker:
    """Turns single-backtick code spans into C++ domain cross-references."""

    def __init__(self, types: dict[str, str], enums: dict[str, str]) -> None:
        self.types = types   # short name -> qualified name
        self.enums = enums

    def span(self, text: str, owner: str = "", members: frozenset[str] = frozenset()) -> str:
        if re.fullmatch(r"[A-Za-z_]\w*", text):
            if text in self.types:
                return f":cpp:class:`{text} <{self.types[text]}>`"
            if text in self.enums:
                return f":cpp:enum:`{text} <{self.enums[text]}>`"
            if owner and text in members:
                return f":cpp:func:`{text} <{owner}::{text}>`"
        enumerator = re.fullmatch(r"([A-Za-z_]\w*)::([A-Za-z_]\w*)", text)
        if enumerator and enumerator.group(1) in self.enums:
            target = f"{self.enums[enumerator.group(1)]}::{enumerator.group(2)}"
            return f":cpp:enumerator:`{text} <{target}>`"
        return literal(text)

    def prose(self, text: str, owner: str = "", members: frozenset[str] = frozenset()) -> str:
        pattern = re.compile(r"(?<![`:\\\w])`([^`\n]+)`(?![`_\w])")
        return pattern.sub(lambda match: self.span(match.group(1), owner, members), text)


# --- C++ domain declarations ---------------------------------------------------------

def class_directive(record: TypeRecord) -> str:
    declaration = record.declaration
    template = ""
    match = re.match(r"(template\s*<.*?>)\s*(class|struct)\s+", declaration)
    if match:
        template = match.group(1) + " "
        rest = declaration[match.end():]
    else:
        rest = re.sub(r"^(class|struct)\s+", "", declaration)
    rest = rest.replace(record.name, record.display_name, 1)
    kind = "struct" if record.kind == "struct" else "class"
    return f".. cpp:{kind}:: {template}{rest}"


# A C function-pointer member, as the C ABI's tables declare their slots:
# `gf_status (GF_CALL* run)(gf_object root, ...)`. The calling-convention
# macro is not C++ the domain can parse.
FUNCTION_POINTER = re.compile(r"^(.+?)\(\s*(?:GF_CALL\s*)?\*\s*([A-Za-z_]\w*)\s*\)\s*(\(.*\))$", re.S)


def function_directive(method: Method) -> str:
    signature = clean_signature(method.signature)
    pointer = FUNCTION_POINTER.match(signature)
    if pointer:
        # A data member holding a function, declared as Sphinx's C++ domain
        # parses it: `void (*name)(parameters)`.
        return f".. cpp:member:: {pointer.group(1).strip()} (*{pointer.group(2)}){pointer.group(3)}"
    signature = re.sub(r"\bfriend\s+", "", signature)
    signature = re.sub(r"\s*=\s*0\s*$", " = 0", signature)
    return f".. cpp:function:: {signature}"


def member_directive(item: Declaration) -> tuple[str, str]:
    """Return the directive and any default-value text for a data member."""
    text = clean_signature(item.text)
    if text.startswith("using "):
        return f".. cpp:type:: {text[len('using '):]}", ""
    default = ""
    braced = re.match(r"^(.*?[\w>\]])\s*(\{.*\})$", text)
    if braced:
        text, default = braced.group(1), braced.group(2)
    elif "=" in text:
        text, _, default = (part.strip() for part in text.partition("="))
    text = re.sub(r"^(?:inline\s+|static\s+)*", "", text)
    if default in {"{}", ""}:
        default = ""
    return f".. cpp:member:: {text}", default


def namespace_directive(namespace: str) -> str:
    return f".. cpp:namespace:: {namespace or '0'}"


# --- sections -------------------------------------------------------------------------

def field_block(name: str, items: list[tuple[str, str, str]]) -> list[str]:
    """A numpydoc field: ``:Name:`` with ``**term** : classifier`` entries."""
    lines = [f":{name}:", ""]
    for term, classifier, description in items:
        head = f"**{term}**" + (f" : {classifier}" if classifier else "")
        lines.append(INDENT + head)
        if description:
            lines.append(indent(description, 2))
        lines.append("")
    return lines


def summary_table(rows: list[tuple[str, str]]) -> list[str]:
    if not rows:
        return []
    lines = [".. list-table::", f"{INDENT}:class: autosummary longtable", f"{INDENT}:widths: 35 65", ""]
    for name, summary in rows:
        lines.append(f"{INDENT}* - {name}")
        lines.append(f"{INDENT}  - {summary or ' '}")
    lines.append("")
    return lines


def first_sentence(text: str) -> str:
    text = " ".join(text.split())
    match = re.match(r"(.+?[.!?])(\s|$)", text)
    return match.group(1) if match else text


# --- descriptions ----------------------------------------------------------------------

@dataclass
class Sources:
    """Reviewed text for one type, in priority order."""
    docstring: Docstring | None
    manual: dict
    contract: dict
    member_summaries: dict[str, str]
    qualified: str

    def member(self, section: str, method: Method) -> str:
        """A member's description.

        A type with a docstring was reviewed against its implementation, so
        its own entries win and the older manual.json texts, which can be
        stale, are not used for it at all.
        """
        if self.docstring is not None:
            for name in (section, "Methods", "Properties", "Events"):
                entry = self.docstring.entry(name, method.name)
                if entry is not None:
                    return entry.description
            return (self.member_summaries.get(f"{self.qualified}::{method.name}", "") or
                    method.comment)
        return (self.manual.get("methods", {}).get(method.name, "") or
                self.member_summaries.get(f"{self.qualified}::{method.name}", "") or
                method.comment)

    def setter(self, prop_name: str, method: Method) -> str:
        if self.docstring is not None:
            entry = self.docstring.entry("Methods", method.name)
            if entry is not None:
                return entry.description
            if self.docstring.entry("Properties", prop_name) is not None:
                return f"Sets `{prop_name}`; see the property's description above."
            return method.comment
        return self.manual.get("methods", {}).get(method.name, "") or method.comment

    def summary(self, registry_entry: dict | None, record_comment: str) -> str:
        if self.docstring is not None:
            return self.docstring.summary
        return (self.manual.get("summary") or self.contract.get("summary") or
                (registry_entry or {}).get("purpose", "") or record_comment)


def member_entries(record: TypeRecord, shape: Shape, sources: Sources, linker: Linker,
                   members: frozenset[str]) -> list[str]:
    """Exact declarations of every public member, nested in the class directive."""
    groups: list[tuple[str, list[tuple[Method, str]]]] = [
        ("Construction", [(method, sources.member("Methods", method)) for method in shape.constructors]),
        ("Properties", [(method, sources.member("Properties", method) if method is prop.getter
                         else sources.setter(prop.name, method))
                        for prop in shape.properties for method in [prop.getter, *prop.setters]]),
        ("Events", [(event.method, sources.member("Events", event.method)) for event in shape.events]),
        ("Methods", [(method, sources.member("Methods", method)) for method in shape.methods]),
        ("Extension points", [(method, sources.member("Methods", method)) for method in shape.extension]),
    ]
    lines: list[str] = []
    for heading, entries in groups:
        if not entries:
            continue
        lines += ["", f".. rubric:: {heading}", ""]
        for method, description in entries:
            lines.append(function_directive(method))
            if method.access == "protected":
                lines += ["", INDENT + "*Protected.*"]
            text = linker.prose(first_paragraph(description), sources.qualified, members)
            if text:
                lines += ["", indent(text)]
            lines.append("")
    for item in record.fields:
        directive, default = member_directive(item)
        lines.append(directive)
        notes = []
        if item.comment:
            notes.append(linker.prose(item.comment, sources.qualified, members))
        if default:
            notes.append(f"Default: {literal(default)}.")
        if notes:
            lines += ["", indent(" ".join(notes))]
        lines.append("")
    return lines


def first_paragraph(text: str) -> str:
    return text.strip().split("\n\n", 1)[0] if text else ""


COMMON_BASES: frozenset[str] = frozenset({"Control", "Component"})


def inherited_line(record: TypeRecord, chain: list[tuple[TypeRecord, Shape]], linker: Linker) -> list[str]:
    own = {prop.name for prop in shape_of(record).properties} | {event.name for event in shape_of(record).events}
    items: list[tuple[str, str, str]] = []
    common = [base for base, _ in chain if base.name in COMMON_BASES]
    for base, base_shape in chain:
        if base.name in COMMON_BASES:
            continue
        properties = [prop.name for prop in base_shape.properties if prop.name not in own]
        events = [event.name for event in base_shape.events if event.name not in own]
        methods = sorted({method.name for method in base_shape.methods})
        own |= set(properties) | set(events)
        parts = []
        if properties:
            parts.append("properties " + ", ".join(
                f":cpp:func:`{name} <{base.qualified_name}::{name}>`" for name in properties))
        if events:
            parts.append("events " + ", ".join(
                f":cpp:func:`{name} <{base.qualified_name}::{name}>`" for name in events))
        if methods:
            parts.append("methods " + ", ".join(
                f":cpp:func:`{name} <{base.qualified_name}::{name}>`" for name in methods))
        if parts:
            items.append((f":cpp:class:`{base.name} <{base.qualified_name}>`", "", "; ".join(parts) + "."))
    if common:
        names = " and ".join(f":cpp:class:`{base.name} <{base.qualified_name}>`" for base in common)
        items.append(("", "", f"It also has every member of {names}: geometry, layout, "
                              "visibility, focus, input routing, painting and disposal."))
    if not items:
        return []
    lines = [":Inherited:", ""]
    for term, _, description in items:
        lines += [INDENT + (f"From {term}: {description}" if term else description), ""]
    return lines


def type_page(record: TypeRecord, sources: Sources, registry_entry: dict | None,
              chain: list[tuple[TypeRecord, Shape]], linker: Linker, label: str) -> str:
    shape = shape_of(record)
    members = frozenset({prop.name for prop in shape.properties} | {event.name for event in shape.events} |
                        {method.name for method in shape.methods} |
                        {setter.name for prop in shape.properties for setter in prop.setters})
    doc = sources.docstring
    owner = record.qualified_name
    out: list[str] = [f".. _{label}:", ""]
    out += title(record.qualified_name)
    out += ["", namespace_directive(record.namespace), "", class_directive(record), ""]
    body: list[str] = [f"``#include <{record.header[len('include/'):]}>``", ""]
    body += [linker.prose(sources.summary(registry_entry, record.comment), owner, members), ""]
    if doc is not None and doc.extended:
        body += [linker.prose(doc.extended, owner, members), ""]
    if registry_entry:
        counterpart = registry_entry.get("winforms", "")
        status = registry_entry.get("status", "provisional")
        facts = f"*Status:* {status}."
        if counterpart and counterpart != "GUI.Forms-only":
            facts += f" *Windows Forms counterpart:* ``{counterpart}``."
        body += [facts, ""]
    contract_remarks = sources.contract.get("remarks", []) if doc is None else []
    for remark in contract_remarks:
        body += [linker.prose(remark, owner, members), ""]

    if shape.constructors:
        constructor = shape.constructors[0]
        items = []
        for parameter in split_parameters(parameters(constructor)):
            type_text, name, default = parameter_parts(parameter)
            if not name:
                continue
            entry = doc.entry("Parameters", name) if doc else None
            classifier = literal(display_type(type_text)) + (f", default {literal(default)}" if default else "")
            items.append((name, classifier, linker.prose(entry.description, owner, members) if entry else ""))
        if items:
            body += field_block("Parameters", items)

    if shape.properties:
        items = []
        for prop in shape.properties:
            entry = doc.entry("Properties", prop.name) if doc else None
            classifier = literal(prop.type)
            if entry and entry.classifier:
                classifier += ", " + entry.classifier
            if not prop.setters:
                classifier += ", read-only"
            description = entry.description if entry else (
                sources.manual.get("methods", {}).get(prop.name, "") or prop.getter.comment)
            items.append((prop.name, classifier, linker.prose(description, owner, members)))
        body += field_block("Properties", items)

    if shape.events:
        items = []
        for event in shape.events:
            entry = doc.entry("Events", event.name) if doc else None
            description = entry.description if entry else sources.member("Events", event.method)
            items.append((event.name, literal(f"Event<{event.arguments}>"),
                          linker.prose(description, owner, members)))
        body += field_block("Events", items)

    body += inherited_line(record, chain, linker)

    if shape.methods:
        rows = []
        for name in dict.fromkeys(method.name for method in shape.methods):
            method = next(method for method in shape.methods if method.name == name)
            rows.append((f":cpp:func:`{name} <{owner}::{name}>`",
                         linker.prose(first_sentence(sources.member("Methods", method)), owner, members)))
        body += [".. rubric:: Methods", ""] + summary_table(rows)

    if doc is not None and doc.see_also:
        body += [".. seealso::", ""]
        for entry in doc.see_also:
            description = entry.classifier or entry.description
            body += [INDENT + linker.span(entry.name), indent(linker.prose(description), 2), ""]
    if doc is not None and "Notes" in doc.text:
        body += [".. rubric:: Notes", "", linker.prose(doc.text["Notes"], owner, members), ""]
    if doc is not None and "Examples" in doc.text:
        body += [".. rubric:: Examples", "", linker.prose(doc.text["Examples"], owner, members), ""]

    body += member_entries(record, shape, sources, linker, members)
    out += [indent("\n".join(body)), ""]
    return "\n".join(out).rstrip() + "\n"


def inline_type(record: TypeRecord, sources: Sources, linker: Linker) -> list[str]:
    shape = shape_of(record)
    members = frozenset({prop.name for prop in shape.properties} | {event.name for event in shape.events} |
                        {method.name for method in shape.methods})
    body = [f"``#include <{record.header[len('include/'):]}>``", ""]
    summary = sources.summary(None, record.comment)
    if summary:
        body += [linker.prose(summary, sources.qualified, members), ""]
    body += member_entries(record, shape, sources, linker, members)
    return [namespace_directive(record.namespace), "", class_directive(record), "",
            indent("\n".join(body)), ""]


def enum_entry(record: EnumRecord, comment: str, linker: Linker) -> list[str]:
    name = record.display_name
    body = [f"``#include <{record.header[len('include/'):]}>``", ""]
    if comment:
        body += [linker.prose(comment), ""]
    for value in record.values:
        body += [f".. cpp:enumerator:: {value}", ""]
    return [namespace_directive(record.namespace), "", f".. cpp:enum-class:: {name}", "",
            indent("\n".join(body)), ""]


def scope_entries(scopes: list[HeaderScope], linker: Linker) -> list[str]:
    lines: list[str] = []
    for scope in scopes:
        lines += [namespace_directive(scope.namespace), "",
                  f"``#include <{scope.header[len('include/'):]}>``", ""]
        for function in scope.functions:
            if is_deleted(function):
                continue
            lines.append(function_directive(function))
            if function.comment:
                lines += ["", indent(linker.prose(function.comment))]
            lines.append("")
        for item in scope.declarations:
            directive, default = member_directive(item)
            if directive.startswith(".. cpp:member::"):
                directive = directive.replace(".. cpp:member::", ".. cpp:var::", 1)
            lines.append(directive)
            notes = [linker.prose(item.comment)] if item.comment else []
            if default:
                notes.append(f"Value: {literal(default)}.")
            if notes:
                lines += ["", indent(" ".join(notes))]
            lines.append("")
    return lines


def linker_safe(text: str) -> str:
    """Markdown-style code spans in configuration text, as reST literals."""
    return re.sub(r"(?<!`)`([^`]+)`(?!`)", r"``\1``", text)
