"""Property-capability family codemod (property-capabilities study, section 15.2).

One JSON config per family (tools/property_families/<family>.json) drives the
migration of a declaring class's properties onto capabilities:

    python3 -B tools/property_codemod.py check CONFIG...
        Validates configs; lists placeholder comments and hand work.
    python3 -B tools/property_codemod.py generate CONFIG
        Writes the capabilities header; inserts the declaring class's
        generated block (friends, hooks, virtuals, table declarations) and
        the source's generated block (descriptors, entries, generated hooks,
        enumeration traits); deletes the migrated getters, setters, clear
        functions and fields, the generated-hook setters' definitions and the
        migrated registrations. Rerunning replaces the generated blocks.
    python3 -B tools/property_codemod.py rewrite --log LOG --build-dir DIR
            [--build-command CMD] [--rounds N] CONFIG...
        The compiler-driven caller rewrite: builds with CMD (when given),
        then at each position the compiler names rewrites `x()` to
        `x.read()`, `set_x(v)` to `x = v`, `clear_x()` to `x = std::nullopt`,
        a read of a deleted field `x_` to `x.read()`, and an ambiguous
        `x = {}` to `x = Value{}`; repeats until a build is clean or a round
        rewrites nothing.
    python3 -B tools/property_codemod.py audit CONFIG...
        Lists parameters and locals named after a property (the rule: no
        parameter shares a property's name) in the family's sources.
    python3 -B tools/property_codemod.py selftest
        Runs the header field deletion over
        tools/property_codemod_fixtures/field_initializers.hpp (initializers
        holding `;`, quotes and nested braces) and compares the result with
        field_initializers.expected.hpp; then checks that generation refuses
        a header whose migrated field survives.
    python3 -B tools/property_codemod.py tests [CONFIG...]
        Writes tests/property_family_generated_tests.cpp from every config
        with a "test" block: per property, a write stores, an equal write
        raises nothing and a changed write raises the change event once.

Config keys are the study's (section 15.2) plus these, all optional:
  top level  "base": the nearest base class with a property table (default
             "Control"); its table is <Base>::<base_snake>_property_entries()
             with <base_snake>_property_count entries.
             "includes": headers the capabilities header includes.
             "overrides": hand-written PropertyEntry initializers replacing
             base entries of the same name (override_property_entries).
             "table_source": "hand" when the entries and descriptors are
             written by hand (only the capabilities header is generated).
             "test": {"type": concrete class, "include": its header}.
             "mutation_check": the statement a generated prepare_write runs
             first (default "require_mutable();", Control's check); null for
             a component without one (study section 14, G7 point 4).
  table      "binding_direction": a form binding's default direction
             (PropertyBindingDirection; "two_way" for what the user edits).
  property   "coerce": false only (the study's key; a coerce_<name> virtual is
             written by hand).
             "prepare": C++ statements a generated prepare_write runs after
             require_mutable and before the compare (`incoming` is the incoming value).
             "written_effects": the dirty mask a generated written touches
             when it differs from the descriptor's (table.effects).
             "traits": "hand" when the value type's PropertyValueTraits are
             declared elsewhere (an enumeration shared between families).
             "sample": a C++ expression the generated test writes.
Paths in a config are relative to the gui_forms checkout (the current
directory). Outputs use LF line endings.
"""

import argparse
import json
import pathlib
import re
import shlex
import subprocess
import sys

PROPERTY_KEYS = {
    "name", "capability", "value", "initial", "comment", "getter", "setter", "clear", "field", "event",
    "on_changed", "localizable", "table", "hooks", "storage", "effects", "thunks", "hand",
    "comment_generated", "prepare", "written_effects", "traits", "sample", "coerce",
}
TOP_KEYS = {
    "family", "declaring", "header", "source", "capabilities_header", "properties", "step", "shared",
    "not_properties", "base", "includes", "table_source", "test", "shared_header", "mutation_check",
    "overrides", "extra_entries",
}
TABLE_KEYS = {
    "kind", "category", "description", "default", "effects", "subtree", "bindable", "browsable", "nullable",
    "enumeration", "reset", "registry", "serialization", "binding_direction", "content_value",
}
THUNK_KEYS = {"read", "write", "reset", "should_serialize", "origin"}
SHARED_HEADER = "include/gui_forms/control/shared_capabilities/shared_capabilities.hpp"
BEGIN = "// property_codemod begin "
END = "// property_codemod end "


# Names and configs ------------------------------------------------------

def snake(name):
    """ButtonBase -> button_base."""
    first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    second = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first)
    return second.lower()


def load(path):
    config_path = pathlib.Path(path)
    config = json.loads(config_path.read_text(encoding="utf-8"))
    config["_path"] = config_path.as_posix()
    return config


def capability_struct(prop):
    return prop["capability"] + "Capability"


def is_shared(config, prop):
    return capability_struct(prop) in (config.get("shared") or [])


def table_properties(config):
    """The properties with a table entry (not instance registrations)."""
    entries = []
    for prop in config["properties"]:
        table = prop.get("table")
        if table is None or table.get("registry"):
            continue
        entries.append(prop)
    return entries


def written_mask(prop):
    if prop.get("written_effects"):
        return prop["written_effects"]
    table = prop.get("table")
    if table is not None:
        return table.get("effects")
    return prop.get("effects")


def validate(config):
    """Returns (errors, notes) for one config."""
    errors = []
    notes = []
    where = config["_path"]
    for key in config:
        if key != "_path" and key not in TOP_KEYS:
            errors.append(f"{where}: unknown key {key!r}")
    required = ("family", "declaring", "header", "source", "capabilities_header", "properties")
    if config.get("family") == "Shared":
        required = ("family", "properties")
    for key in required:
        if key not in config:
            errors.append(f"{where}: missing {key!r}")
    if errors:
        return errors, notes
    config.setdefault("declaring", "Declaring")
    names = set()
    capabilities = set()
    for prop in config["properties"]:
        label = f"{where}: {config['declaring']}.{prop.get('name')}"
        for key in prop:
            if key not in PROPERTY_KEYS:
                errors.append(f"{label}: unknown key {key!r}")
        for key in ("name", "capability", "value", "comment"):
            if not prop.get(key):
                errors.append(f"{label}: missing {key!r}")
        if prop.get("name") in names:
            errors.append(f"{label}: property listed twice")
        names.add(prop.get("name"))
        if prop.get("capability") in capabilities:
            errors.append(f"{label}: capability {prop.get('capability')!r} listed twice")
        capabilities.add(prop.get("capability"))
        if prop.get("initial") == "":
            errors.append(f"{label}: an empty initial is refused (null is value initialization)")
        if prop.get("hand") and prop.get("hooks") != "hand":
            errors.append(f"{label}: hand work listed but hooks are not 'hand'")
        if prop.get("hooks") not in ("hand", "generated"):
            errors.append(f"{label}: hooks must be 'hand' or 'generated'")
        if prop.get("table") is not None and prop.get("effects"):
            errors.append(f"{label}: 'effects' with a table (use table.effects or written_effects)")
        if prop.get("storage", "cell") not in ("cell", "view"):
            errors.append(f"{label}: storage must be 'cell' or 'view'")
        if prop.get("storage") == "view" and prop.get("hooks") == "generated":
            errors.append(f"{label}: a view's hooks are hand-written")
        # The study's coerce key (section 15.2): only false is generated; a
        # coerce_<name> virtual is written by hand.
        if prop.get("coerce", False) is not False:
            errors.append(f"{label}: coerce must be false (a coerce_<name> virtual is written by hand)")
        table = prop.get("table")
        if table is not None:
            for key in table:
                if key not in TABLE_KEYS:
                    errors.append(f"{label}: unknown table key {key!r}")
            for key in ("kind", "category", "description"):
                if not table.get(key):
                    errors.append(f"{label}: table without {key!r}")
            if table.get("kind") == "enumeration" and not table.get("enumeration"):
                errors.append(f"{label}: an enumeration table names no choice set")
        for key in prop.get("thunks") or {}:
            if key not in THUNK_KEYS:
                errors.append(f"{label}: unknown thunk {key!r}")
        if prop.get("hooks") == "generated" and written_mask(prop) is None and not prop.get("event"):
            notes.append(f"{label}: a generated written touches nothing and raises nothing")
        if prop.get("comment_generated"):
            notes.append(f"{label}: placeholder comment {prop.get('comment')!r} (not committable)")
        for reason in prop.get("hand") or []:
            notes.append(f"{label}: hand: {reason}")
        if prop.get("name") in ("incoming", "cell", "capability", "control", "owner", "current"):
            errors.append(f"{label}: the name collides with a hook parameter")
    return errors, notes


def command_check(arguments):
    failed = False
    for path in arguments.configs:
        config = load(path)
        errors, notes = validate(config)
        for line in errors:
            print("error: " + line)
        for line in notes:
            print("note: " + line)
        failed = failed or bool(errors)
        print(f"{path}: {len(config.get('properties', []))} properties, {len(errors)} errors, {len(notes)} notes")
    return 1 if failed else 0


# The capabilities header ------------------------------------------------

def cell_initializer(prop):
    initial = prop.get("initial")
    if initial is None:
        return "{}"
    return "{" + initial + "}"


def localizable_prefix(prop):
    if prop.get("localizable"):
        return "GUI_FORMS_LOCALIZABLE(%s) " % prop["localizable"]
    return ""


def capability_text(prop, shared):
    """One capability template. A shared one has constructors, so a bundle
    can give it a family's initial value."""
    cap = capability_struct(prop)
    member = prop["name"]
    value = prop["value"]
    lines = ["", "// " + prop["comment"], "template <typename Declaring>", f"struct {cap} {{"]
    if prop.get("storage") == "view":
        lines.append(f"    PropertyView<{value}, {cap}<Declaring>> {member};")
        lines.append("")
        lines.append(f"    static void assign({cap}& capability, {value} incoming) {{")
        lines.append("        Declaring& control = static_cast<Declaring&>(capability);")
        lines.append(f"        control.write_view(std::as_const(capability.{member}), std::move(incoming));")
        lines.append("    }")
        lines.append("")
        lines.append(f"    static {value} read(const {cap}& capability) {{")
        lines.append("        const Declaring& control = static_cast<const Declaring&>(capability);")
        lines.append(f"        {value} current = control.read_view(capability.{member});")
        lines.append("        return current;")
        lines.append("    }")
    else:
        if shared:
            lines.append(f"    {cap}() = default;")
            lines.append(f"    explicit {cap}({value} initial) : {member}(std::move(initial)) {{}}")
            lines.append("")
        initializer = "{}" if shared else cell_initializer(prop)
        lines.append(f"    {localizable_prefix(prop)}PropertyCell<{value}, {cap}<Declaring>> {member}{initializer};")
        lines.append("")
        lines.append(f"    static void assign({cap}& capability, {value} incoming) {{")
        lines.append("        Declaring& control = static_cast<Declaring&>(capability);")
        lines.append(f"        control.prepare_write(std::as_const(capability.{member}), incoming);")
        lines.append(f"        if (!capability.{member}.replace(std::move(incoming))) return;")
        lines.append(f"        control.written(std::as_const(capability.{member}));")
        lines.append("    }")
    lines.append("};")
    return "\n".join(lines) + "\n"


HEADER_COMMENT = """
// {family}'s property capabilities (property-capabilities study, sections
// 2, 14 and 15). Generated by tools/property_codemod.py from
// {config}: edit the config, not this file.
//
// Each is a standard-layout struct whose only member is its property's
// cell, taken by the declaring class as a base. A write
// `(*control).x = value;` runs `assign`: the declaring class's
// prepare_write hook (require_mutable first, then the value's checks), the
// compare and store, then the written hook (the touch, side effects and the
// change event, through the protected virtual on_<property>_changed() where
// Windows Forms has an overridable On<Property>Changed; other properties
// have no hook, and a subclass subscribes to their change events instead). A read is
// `(*control).x.read()`.
"""


def enum_traits_needed(config):
    """The enumeration value types whose traits this family declares."""
    seen = []
    for prop in table_properties(config):
        table = prop["table"]
        if table.get("kind") != "enumeration" or prop.get("traits") == "hand":
            continue
        if prop["value"] not in [value for value, _ in seen]:
            seen.append((prop["value"], table["enumeration"]))
    return seen


def capabilities_header(config):
    includes = ['"gui_forms/binding/property_cell/property_cell.hpp"']
    if any(prop.get("localizable") for prop in config["properties"]):
        includes.append('"gui_forms/text/localizable/localizable_property.hpp"')
    if config.get("shared"):
        includes.append('"%s"' % SHARED_HEADER[len("include/"):])
    system = ["<cstdint>", "<optional>", "<string>", "<utility>"]
    # An include spelled with angle brackets is a standard header.
    for extra in config.get("includes") or []:
        if extra.startswith("<"):
            if extra not in system:
                system.append(extra)
        else:
            includes.append('"%s"' % extra)
    system.sort()
    out = ["#pragma once", ""]
    out += ["#include " + include for include in includes]
    out.append("")
    out += ["#include " + include for include in system]
    out += ["", "namespace gui_forms {"]
    out.append(HEADER_COMMENT.format(family=config["family"], config=config["_path"]).rstrip("\n"))
    text = "\n".join(out) + "\n"
    for prop in config["properties"]:
        if is_shared(config, prop):
            continue
        text += capability_text(prop, False)
    for value, _ in enum_traits_needed(config):
        text += f"""
// {value} as a property value; defined beside {config['declaring']}'s table.
template <>
struct PropertyValueTraits<{value}> {{
    static PropertyType type();
    static BindingValue to_binding({value} value);
    static {value} from_binding(const BindingValue& value);
}};
"""
    bundle = config["family"] + "Capabilities"
    bases = ",\n".join(
        " " * (len(f"struct {bundle} : ")) + "public " + capability_struct(prop) + "<Declaring>"
        for prop in config["properties"])
    bases = bases.lstrip()
    text += f"\n// Every {config['family']} capability, in the order the declaring class takes them.\n"
    text += f"template <typename Declaring>\nstruct {bundle} : {bases} {{"
    shared_initials = [prop for prop in config["properties"]
                       if is_shared(config, prop) and prop.get("initial") is not None]
    if shared_initials:
        initializers = ", ".join(
            "%s<Declaring>(%s{%s})" % (capability_struct(prop), prop["value"], prop["initial"])
            for prop in shared_initials)
        text += f"\n    {bundle}() : {initializers} {{}}\n}};\n"
    else:
        text += "};\n"
    text += "\n} // namespace gui_forms\n"
    return text


def shared_header(shared_config):
    """The shared capability templates (Text, Font, ...), from
    tools/property_families/shared.json."""
    out = ["#pragma once", "", '#include "gui_forms/binding/property_cell/property_cell.hpp"',
           '#include "gui_forms/text/localizable/localizable_property.hpp"']
    for extra in shared_config.get("includes") or []:
        out.append('#include "%s"' % extra)
    out += ["", "#include <string>", "#include <utility>", "", "namespace gui_forms {", ""]
    out.append("// Capabilities several families take (property-capabilities study, section")
    out.append("// 15.3), generated by tools/property_codemod.py from " + shared_config["_path"] + ".")
    out.append("// The template is only the assign shape: what a write does lives in each")
    out.append("// declaring class's hooks, and a family's initial value is given by its")
    out.append("// bundle's constructor. Nothing generic may treat having a capability as")
    out.append("// having a behaviour.")
    text = "\n".join(out) + "\n"
    for prop in shared_config["properties"]:
        text += capability_text(prop, True)
    text += "\n} // namespace gui_forms\n"
    return text


# Source editing helpers ---------------------------------------------------

def matching(text, open_index, opening, closing):
    """The index of the bracket closing the one at open_index; skips string
    and character literals and comments."""
    depth = 0
    index = open_index
    while index < len(text):
        character = text[index]
        if character in "\"'":
            quote = character
            index += 1
            while index < len(text) and text[index] != quote:
                if text[index] == "\\":
                    index += 1
                index += 1
        elif text.startswith("//", index):
            index = text.find("\n", index)
            if index < 0:
                break
        elif text.startswith("/*", index):
            index = text.find("*/", index) + 1
        elif character == opening:
            depth += 1
        elif character == closing:
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise ValueError("unbalanced %s at %d" % (opening, open_index))


def class_body(text, declaring):
    """(open brace, closing brace) of the class definition."""
    head = re.search(r"\bclass\s+%s\b(?:\s+final)?\s*(?::[^;{]*)?\{" % re.escape(declaring), text)
    if head is None:
        raise ValueError("no definition of class " + declaring)
    open_index = head.end() - 1
    close_index = matching(text, open_index, "{", "}")
    return head, open_index, close_index


def line_start(text, index):
    return text.rfind("\n", 0, index) + 1


def line_end(text, index):
    end = text.find("\n", index)
    return len(text) if end < 0 else end + 1


def declaration_range(text, match_index, body_open, body_close):
    """The member declaration containing match_index: from its first line to
    the end of the line ending it (a `;`, or a body's closing brace). Lines
    above that end without `;`, `{`, `}` or `:` belong to it."""
    start = line_start(text, match_index)
    while start > body_open + 1:
        previous_end = start - 1
        previous_start = line_start(text, previous_end)
        previous = text[previous_start:previous_end].strip()
        if not previous or previous.startswith("//") or previous[-1] in ";{}:":
            break
        start = previous_start
    index = match_index
    while index < body_close:
        character = text[index]
        if character == "(":
            index = matching(text, index, "(", ")")
        elif character == ";":
            return start, line_end(text, index)
        elif character == "{":
            close = matching(text, index, "{", "}")
            after = close + 1
            if text[after:after + 1] == ";":
                after += 1
            return start, line_end(text, close)
        index += 1
    raise ValueError("declaration without an end at %d" % match_index)


def with_attached_comment(text, start):
    """Extends start upward over comment lines directly above."""
    while start > 0:
        previous_start = line_start(text, start - 1)
        if not text[previous_start:start].strip().startswith("//"):
            break
        start = previous_start
    return start


def top_level_commas(text):
    """The number of commas outside brackets, braces and literals."""
    depth = 0
    commas = 0
    index = 0
    while index < len(text):
        character = text[index]
        if character in "\"'":
            quote = character
            index += 1
            while index < len(text) and text[index] != quote:
                if text[index] == "\\":
                    index += 1
                index += 1
        elif character in "([{<":
            depth += 1
        elif character in ")]}>":
            depth -= 1
        elif character == "," and depth == 0:
            commas += 1
        index += 1
    return commas


def statement_end(text, index, limit):
    """The index of the `;` ending the statement at index: outside
    brackets, braces, string and character literals and comments."""
    while index < limit:
        character = text[index]
        if character in "\"'":
            quote = character
            index += 1
            while index < limit and text[index] != quote:
                if text[index] == "\\":
                    index += 1
                index += 1
        elif text.startswith("//", index):
            index = text.find("\n", index)
            if index < 0:
                break
        elif text.startswith("/*", index):
            index = text.find("*/", index) + 1
        elif character == "(":
            index = matching(text, index, "(", ")")
        elif character == "[":
            index = matching(text, index, "[", "]")
        elif character == "{":
            index = matching(text, index, "{", "}")
        elif character == ";":
            return index
        index += 1
    raise ValueError("statement without an end at %d" % index)


FIELD_HEAD = r"^[ \t]*(?!return\b)[\w:<>,\*&][\w:<>, \t\*&]*?\b%s\s*(?=[{=;])"


def field_declarations(text, field, body_open, body_close):
    """(start, end) of each declaration of the data member `field` in the
    class body, whole lines: its type and name at a line's start, then a
    brace or `=` initializer (any content: literals, nested braces, `;`
    inside quotes) or none, then the `;` and the rest of its line."""
    pattern = re.compile(FIELD_HEAD % re.escape(field), re.M)
    found = []
    for match in pattern.finditer(text, body_open + 1, body_close):
        end = statement_end(text, match.end(), body_close)
        found.append((match.start(), line_end(text, end)))
    return found


def strip_comments(text):
    """text with comments and string and character literals blanked."""
    out = []
    index = 0
    while index < len(text):
        character = text[index]
        if character in "\"'":
            quote = character
            out.append(" ")
            index += 1
            while index < len(text) and text[index] != quote:
                if text[index] == "\\":
                    index += 1
                index += 1
            out.append(" ")
            index += 1
        elif text.startswith("//", index):
            newline = text.find("\n", index)
            index = len(text) if newline < 0 else newline
        elif text.startswith("/*", index):
            close = text.find("*/", index)
            index = len(text) if close < 0 else close + 2
        else:
            out.append(character)
            index += 1
    return "".join(out)


def remaining_fields(text, config):
    """The migrated fields still named in the declaring class's body after
    generation: each would be stale storage beside its cell."""
    head, body_open, body_close = class_body(text, config["declaring"])
    body = strip_comments(text[body_open + 1:body_close])
    left = []
    for prop in config["properties"]:
        field = prop.get("field")
        if field and prop.get("storage", "cell") == "cell" and re.search(r"\b%s\b" % re.escape(field), body):
            left.append(field)
    return left


def remove_ranges(text, ranges):
    """Removes the ranges, merging overlapping ones first."""
    merged = []
    for start, end in sorted(set(ranges)):
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    for start, end in reversed(merged):
        text = text[:start] + text[end:]
    return text


def collapse_blank_lines(text):
    return re.sub(r"\n{3,}", "\n\n", text)


def signature_keys(block):
    """The function heads and friend declarations of a generated block, each
    cut at its first `)` with its spacing collapsed, so `{}` against `{` and
    `}` on two lines, or a parameter's name, does not tell them apart."""
    keys = set()
    # A source block's indented lines are statements of the bodies above them.
    source = re.search(r"^[^\s/][^\n]*::\w+\(", block, re.M) is not None
    for line in block.split("\n"):
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        if stripped.startswith("friend "):
            keys.add(" ".join(stripped.split()))
            continue
        head = re.match(r"(?:[\w:<>,&*~\[\] ]+\s)?[\w:~]+\([^)]*\)?", stripped)
        if head is None:
            continue
        # A definition starts at the line's first column; a declaration is a
        # member line of the class (one indent) that ends its statement.
        if line[:1].isspace() and (source or not (line.startswith("    ") and not line.startswith("     ")
                                                  and stripped.endswith((";", ",")))):
            continue
        if not line[:1].isspace() and "::" not in stripped.split("(", 1)[0]:
            continue
        keys.add(" ".join(head.group(0).split(")", 1)[0].split()))
    return keys


def replace_block(text, family, block, anchor_index):
    """Replaces the generated block for `family`, or inserts it at anchor_index.

    Refuses (ValueError) to replace a block holding a function or friend the
    new block would not have: a hand-written hook belongs outside the
    generated block, where regeneration leaves it alone."""
    begin = text.find(BEGIN + family + "\n")
    if begin < 0:
        begin = text.find(BEGIN + family + " ")
    if begin >= 0:
        begin = line_start(text, begin)
        end_marker = text.index(END + family, begin)
        end = line_end(text, end_marker)
        lost = sorted(signature_keys(text[begin:end]) - signature_keys(block))
        if lost:
            raise ValueError(f"{family}: the generated block holds hand-written code the generator "
                             f"would drop; move it outside the block: " + "; ".join(lost))
        return text[:begin] + block + text[end:]
    return text[:anchor_index] + block + text[anchor_index:]


def add_include(text, include):
    line = '#include "%s"\n' % include
    if line in text:
        return text
    includes = list(re.finditer(r'^#include "[^"]+"\n', text, re.M))
    if not includes:
        raise ValueError("no include to follow")
    index = includes[-1].end()
    return text[:index] + line + text[index:]


# The declaring class's header ---------------------------------------------

def header_block(config):
    declaring = config["declaring"]
    family = config["family"]
    base = config.get("base", "Control")
    own = table_properties(config)
    extra = config.get("extra_entries") or []
    lines = [f"    {BEGIN}{family} ({config['_path']}; generated, do not edit)"]
    if config.get("table_source") != "hand" and (own or extra):
        count = f"{snake(declaring)}_property_count"
        lines += ["public:",
                  f"    // {declaring}'s property table: {base}'s entries, then its own.",
                  f"    static constexpr std::size_t {count} = {base}::{snake(base)}_property_count + {len(own) + len(extra)}U;",
                  f"    [[nodiscard]] static const std::array<PropertyEntry, {count}>& "
                  f"{snake(declaring)}_property_entries() noexcept;",
                  "    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;",
                  ""]
    virtuals = []
    for prop in config["properties"]:
        if prop.get("on_changed"):
            virtuals.append(f"    virtual void on_{prop['name']}_changed();")
    if virtuals:
        lines += ["protected:",
                  "    // on_<property>_changed runs after a changed write's store and touch and",
                  "    // raises the change event; an override calls the base. Only the",
                  "    // properties with a Windows Forms On<Property>Changed have one.",
                  *virtuals, ""]
    if not config["properties"]:
        # A family of hand entries only (extra_entries) has no cells, so no
        # capabilities, friends or hooks.
        if lines[-1] == "":
            lines.pop()
        lines.append(f"    {END}{family}")
        return "\n".join(lines) + "\n"
    lines.append("private:")
    for prop in config["properties"]:
        lines.append(f"    friend struct {capability_struct(prop)}<{declaring}>;")
    lines += ["    // The capabilities' hooks, chosen by the cell's type (study section 14)."]
    for prop in config["properties"]:
        member = prop["name"]
        value = prop["value"]
        if prop.get("storage") == "view":
            lines.append(f"    void write_view(const decltype({member})& cell, {value} incoming);")
            lines.append(f"    [[nodiscard]] {value} read_view(const decltype({member})& cell) const;")
        else:
            lines.append(f"    void prepare_write(const decltype({member})& cell, {value}& incoming);")
            lines.append(f"    void written(const decltype({member})& cell);")
    lines.append(f"    {END}{family}")
    return "\n".join(lines) + "\n"


def delete_members(text, config, body_open, body_close, report):
    """Ranges of the getters, setters, clear functions and fields to delete."""
    body = text[body_open:body_close]
    ranges = []
    for prop in config["properties"]:
        names = []
        if prop.get("getter"):
            pattern = re.compile(r"\b%s\(\)\s*const\b" % re.escape(prop["getter"]))
            for match in pattern.finditer(body):
                start, end = declaration_range(text, body_open + match.start(), body_open, body_close)
                ranges.append((with_attached_comment(text, start), end))
                names.append(prop["getter"] + "()")
        for key in ("setter", "clear"):
            if not prop.get(key):
                continue
            pattern = re.compile(r"^[ \t]*(?:virtual\s+)?void\s+%s\(" % re.escape(prop[key]), re.M)
            for match in pattern.finditer(body):
                open_index = body_open + match.end() - 1
                close = matching(text, open_index, "(", ")")
                if top_level_commas(text[open_index + 1:close]):
                    # An overload with several parameters is a method, kept.
                    continue
                start, end = declaration_range(text, open_index, body_open, body_close)
                ranges.append((with_attached_comment(text, start), end))
                names.append(prop[key])
        if prop.get("field") and prop.get("storage", "cell") == "cell":
            for start, end in field_declarations(text, prop["field"], body_open, body_close):
                ranges.append((start, end))
                names.append(prop["field"])
        report.append(f"{prop['name']}: deleted {', '.join(names) if names else 'nothing'} in the header")
    return ranges


def generate_header(config, report):
    path = pathlib.Path(config["header"])
    text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    cells = bool(config["properties"])
    if cells:
        capabilities_include = config["capabilities_header"][len("include/"):]
        text = add_include(text, capabilities_include)
    bundle = f"{config['family']}Capabilities<{config['declaring']}>"
    head, body_open, body_close = class_body(text, config["declaring"])
    if cells and bundle not in head.group(0):
        head_text = head.group(0)
        if ":" in head_text:
            new_head = head_text[:-1].rstrip() + f", public {bundle} {{"
        else:
            new_head = head_text[:-1].rstrip() + f" : public {bundle} {{"
        text = text[:head.start()] + new_head + text[head.end():]
        head, body_open, body_close = class_body(text, config["declaring"])
    ranges = delete_members(text, config, body_open, body_close, report)
    text = remove_ranges(text, ranges)
    left = remaining_fields(text, config)
    if left:
        raise ValueError("%s: migrated fields still in class %s: %s"
                         % (config["header"], config["declaring"], ", ".join(left)))
    head, body_open, body_close = class_body(text, config["declaring"])
    text = replace_block(text, config["family"], header_block(config), line_start(text, body_close))
    path.write_bytes(collapse_blank_lines(text).encode("utf-8"))


# The declaring class's source ---------------------------------------------

def cpp_string(value):
    return json.dumps(value, ensure_ascii=False)


def kind_expression(kind):
    return "BindingValueKind::" + kind


def descriptor_function(config, prop):
    declaring = config["declaring"]
    table = prop["table"]
    function = f"describe_{snake(declaring)}_{prop['name']}"
    builder = f"{snake(declaring)}_{prop['name']}_descriptor"
    thunks = prop.get("thunks") or {}
    resettable = bool(table.get("reset") or thunks.get("reset"))
    raises = bool(prop.get("event"))
    lines = [f"PropertyDescriptor {builder}() {{",
             "    PropertyDescriptor descriptor;",
             f"    descriptor.name = {cpp_string(prop['name'])};",
             f"    descriptor.kind = {kind_expression(table['kind'])};",
             f"    descriptor.category = {cpp_string(table['category'])};",
             f"    descriptor.description = {cpp_string(table['description'])};"]
    if table.get("default"):
        lines.append(f"    descriptor.default_value = {table['default']};")
    if table.get("effects"):
        lines.append(f"    descriptor.invalidation_effects = {table['effects']};")
    if table.get("subtree"):
        lines.append("    descriptor.invalidates_subtree = true;")
    if table.get("bindable") is False:
        lines.append("    descriptor.bindable = false;")
    if table.get("browsable") is False:
        lines.append("    descriptor.browsable = false;")
    if table.get("nullable"):
        lines.append("    descriptor.nullable = true;")
    if table.get("enumeration"):
        lines.append(f"    descriptor.enumeration = {table['enumeration']};")
    if table.get("binding_direction"):
        direction = table["binding_direction"]
        if "::" not in direction:
            direction = "PropertyBindingDirection::" + direction
        lines.append(f"    descriptor.binding_direction = {direction};")
    if table.get("serialization"):
        visibility = table["serialization"]
        if "::" not in visibility:
            visibility = "PropertySerializationVisibility::" + visibility
        lines.append(f"    descriptor.serialization_visibility = {visibility};")
    if table.get("content_value"):
        # Content a form document may set, not presentation a theme gives.
        lines.append("    descriptor.content_value = true;")
    if not thunks.get("write"):
        # The entry's write is the cell's assignment (write_cell_entry), so
        # the form generator may assign the cell (CLOSURE P-6).
        lines.append("    descriptor.assignable_member = true;")
    lines += ["    return descriptor;", "}", "",
              f"const PropertyDescriptor& {function}() {{",
              f"    static const PropertyDescriptor descriptor = property_table_descriptor(",
              f"        {builder}(), PropertyOperations{{true, true, {str(resettable).lower()}, {str(raises).lower()}}});",
              "    return descriptor;",
              "}", ""]
    return "\n".join(lines) + "\n"


def member_argument(declaring, member):
    """`decltype(&X::m), &X::m`: a member pointer template argument after its type."""
    pointer = f"&{declaring}::{member}"
    return f"decltype({pointer}), {pointer}"


def entry_text(config, prop):
    declaring = config["declaring"]
    cell = member_argument(declaring, prop["name"])
    thunks = prop.get("thunks") or {}
    table = prop["table"]
    read = f"&read_cell_entry<{declaring}, {cell}>"
    write = f"&write_cell_entry<{declaring}, {cell}>"
    if thunks.get("read"):
        read = f"&read_member_entry<{declaring}, {member_argument(declaring, thunks['read'])}>"
    if thunks.get("write"):
        write = f"&write_member_entry<{declaring}, {member_argument(declaring, thunks['write'])}>"
    reset = "nullptr"
    reset_name = thunks.get("reset") or table.get("reset")
    if reset_name:
        reset = f"&reset_member_entry<{declaring}, {member_argument(declaring, reset_name)}>"
    serialize = "nullptr"
    if thunks.get("should_serialize"):
        serialize = (f"&should_serialize_member_entry<{declaring}, "
                     f"{member_argument(declaring, thunks['should_serialize'])}>")
    origin = "nullptr"
    if thunks.get("origin"):
        origin = f"&origin_member_entry<{declaring}, {member_argument(declaring, thunks['origin'])}>"
    connect = "nullptr"
    if prop.get("event"):
        connect = f"&connect_event_entry<{declaring}, {member_argument(declaring, prop['event'])}>"
    describe = f"&describe_{snake(declaring)}_{prop['name']}"
    return (f"        {{{cpp_string(prop['name'])}, {describe},\n"
            f"         {read},\n         {write},\n         {reset}, {serialize}, {origin},\n         {connect}}},")


def generated_hooks(config, prop):
    declaring = config["declaring"]
    member = prop["name"]
    value = prop["value"]
    statements = prop.get("prepare") or []
    uses_value = bool(statements)
    parameter = f"{value}& incoming" if uses_value else f"{value}&"
    lines = [f"void {declaring}::prepare_write(const decltype({member})&, {parameter}) {{"]
    check = config.get("mutation_check", "require_mutable();")
    if check:
        lines.append("    " + check)
    lines += ["    " + statement for statement in statements]
    lines += ["}", "", f"void {declaring}::written(const decltype({member})&) {{"]
    mask = written_mask(prop)
    table = prop.get("table") or {}
    if mask:
        if table.get("subtree"):
            lines.append(f"    invalidate_subtree({mask});")
        else:
            lines.append(f"    invalidate({mask});")
    if prop.get("on_changed"):
        lines.append(f"    on_{member}_changed();")
    elif prop.get("event"):
        lines.append(f"    publish_change({prop['event']}, {member}.read());")
    lines += ["}", ""]
    # A hook with nothing to do (a component's unchecked, untouched write) is
    # spelled `{}` on its signature's line.
    text = "\n".join(lines) + "\n"
    text = re.sub(r"\) \{\n\}\n", ") {}\n", text)
    return text


def virtual_bodies(config, prop):
    declaring = config["declaring"]
    member = prop["name"]
    lines = []
    if prop.get("on_changed"):
        if prop.get("event"):
            lines += [f"void {declaring}::on_{member}_changed() {{",
                      f"    publish_change({prop['event']}, {member}.read());", "}", ""]
        else:
            lines += [f"void {declaring}::on_{member}_changed() {{}}", ""]
    return "\n".join(lines) + ("\n" if lines else "")


def enum_traits_definitions(config):
    out = ""
    for value, enumeration in enum_traits_needed(config):
        out += f"""PropertyType PropertyValueTraits<{value}>::type() {{
    return PropertyType{{BindingValueKind::enumeration, false, {enumeration}}};
}}

BindingValue PropertyValueTraits<{value}>::to_binding({value} value) {{
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = {enumeration};
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{{static_cast<std::int64_t>(value)}}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}}

{value} PropertyValueTraits<{value}>::from_binding(const BindingValue& value) {{
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<{value}>(choice.value);
}}

"""
    return out


def source_block(config):
    declaring = config["declaring"]
    family = config["family"]
    base = config.get("base", "Control")
    own = table_properties(config)
    out = f"{BEGIN}{family} ({config['_path']}; generated, do not edit)\n\n"
    if config.get("table_source") != "hand" and own:
        out += "namespace {\n\n"
        for prop in own:
            out += descriptor_function(config, prop)
        out += "} // namespace\n\n"
    out += enum_traits_definitions(config)
    extra = config.get("extra_entries") or []
    if config.get("table_source") != "hand" and (own or extra):
        count = f"{snake(declaring)}_property_count"
        # Hand-written entries for properties whose storage is not a cell of
        # this family (a base class's cell this class alone publishes, or a
        # compound value): their descriptors and operations are written by
        # hand beside the generated block.
        entries = "\n".join([entry_text(config, prop) for prop in own] +
                            ["        " + entry + "," for entry in extra])
        base_entries = f"{base}::{snake(base)}_property_entries()"
        overrides = config.get("overrides") or []
        refined = ""
        if overrides:
            # Hand-written entries replacing base entries of the same name in
            # their slots (override_property_entries); their operations and
            # descriptors are the declaring class's own, written by hand.
            listed = "\n".join("        " + entry + "," for entry in overrides)
            refined = (f"    static constexpr std::array<PropertyEntry, {len(overrides)}U> overrides{{{{\n"
                       f"{listed}\n    }}}};\n")
            base_entries = f"override_property_entries({base_entries}, overrides)"
        out += f"""const std::array<PropertyEntry, {declaring}::{count}>& {declaring}::{snake(declaring)}_property_entries() noexcept {{
{refined}    static constexpr std::array<PropertyEntry, {len(own) + len(extra)}U> own{{{{
{entries}
    }}}};
    static_assert(property_names_distinct(own), "{declaring}'s property table names a property twice");
    static const std::array<PropertyEntry, {count}> entries =
        join_property_tables({base_entries}, own);
    return entries;
}}

std::span<const PropertyEntry> {declaring}::property_table() const noexcept {{
    const std::array<PropertyEntry, {count}>& entries = {snake(declaring)}_property_entries();
    return std::span<const PropertyEntry>(entries);
}}

"""
    for prop in config["properties"]:
        if prop.get("hooks") == "generated":
            out += generated_hooks(config, prop)
        out += virtual_bodies(config, prop)
    out = out.rstrip("\n") + "\n\n" + f"{END}{family}\n"
    return out


def registration_ranges(text, names, report):
    """Ranges of the define_bindable_property statements registering `names`
    (with their PropertyDescriptor variable, when they use one)."""
    ranges = []
    for match in re.finditer(r"define_bindable_property\(\{", text):
        open_index = match.end() - 2
        close = matching(text, open_index, "(", ")")
        end = line_end(text, close)
        statement = text[match.end():close]
        inline = re.match(r"\s*\{\s*\"(\w+)\"", statement)
        variable = re.match(r"\s*std::move\((\w+)\)", statement)
        start = line_start(text, match.start())
        name = None
        if inline:
            name = inline.group(1)
        elif variable:
            declaration = None
            for candidate in re.finditer(r"^[ \t]*PropertyDescriptor\s+%s;\n" % variable.group(1), text[:start], re.M):
                declaration = candidate
            if declaration is not None:
                named = re.search(r"\b%s\.name = \"(\w+)\";" % variable.group(1), text[declaration.start():start])
                if named:
                    name = named.group(1)
                    start = declaration.start()
        if name in names:
            ranges.append((start, end))
            report.append(f"{name}: deleted its registration")
    return ranges


def definition_ranges(text, declaring, function_names):
    ranges = []
    for name in function_names:
        pattern = re.compile(r"^[^\n;{}#/]*\b%s::%s\(" % (re.escape(declaring), re.escape(name)), re.M)
        for match in pattern.finditer(text):
            open_index = match.end() - 1
            close_parameters = matching(text, open_index, "(", ")")
            if top_level_commas(text[open_index + 1:close_parameters]):
                continue
            after = re.match(r"\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{", text[close_parameters + 1:])
            if after is None:
                # A declaration or a call, not a definition.
                continue
            brace = close_parameters + after.end()
            close = matching(text, brace, "{", "}")
            end = line_end(text, close)
            ranges.append((with_attached_comment(text, match.start()), end))
    return ranges


def generate_source(config, report):
    path = pathlib.Path(config["source"])
    text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    if config.get("table_source") != "hand":
        text = add_include(text, "gui_forms/binding/property_cell_entry/property_cell_entry.hpp")
        names = {prop["name"] for prop in table_properties(config)}
        text = remove_ranges(text, registration_ranges(text, names, report))
    generated = []
    hand = []
    for prop in config["properties"]:
        functions = [prop.get(key) for key in ("setter", "clear", "getter") if prop.get(key)]
        if prop.get("hooks") == "generated":
            generated += functions
        else:
            hand += [f"{config['declaring']}::{name}" for name in functions if name != prop.get("getter")]
    text = remove_ranges(text, definition_ranges(text, config["declaring"], generated))
    for name in hand:
        if re.search(r"\b%s\(" % re.escape(name), text):
            report.append(f"hand: move the body of {name} into its hooks, then delete it")
    namespace_end = text.rfind("} // namespace gui_forms")
    if namespace_end < 0:
        raise ValueError("no closing namespace in " + config["source"])
    text = replace_block(text, config["family"], source_block(config) + "\n", namespace_end)
    path.write_bytes(collapse_blank_lines(text).encode("utf-8"))


def command_generate(arguments):
    config = load(arguments.config)
    errors, notes = validate(config)
    if errors:
        for line in errors:
            print("error: " + line)
        return 1
    report = []
    if config["family"] == "Shared":
        path = pathlib.Path(SHARED_HEADER)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(shared_header(config).encode("utf-8"))
        print(f"wrote {path}")
        return 0
    if config["properties"]:
        header_path = pathlib.Path(config["capabilities_header"])
        header_path.parent.mkdir(parents=True, exist_ok=True)
        header_path.write_bytes(capabilities_header(config).encode("utf-8"))
        report.append(f"wrote {header_path}")
    if config.get("table_source") != "hand" or any(p.get("hooks") == "generated" for p in config["properties"]):
        generate_header(config, report)
        generate_source(config, report)
    for line in report + notes:
        print(line)
    return 0


# The compiler-driven rewrite ----------------------------------------------

DIAGNOSTIC = re.compile(r"^(?P<path>(?:[A-Za-z]:)?[^:]*):(?P<line>\d+):(?P<column>\d+): error: (?P<message>.*)$")
CALLED_OBJECT = ("is not a function or function pointer", "does not provide a call operator")


class Rewriter:
    def __init__(self, configs, root):
        self.root = root
        self.members = {}
        self.setters = {}
        self.clears = {}
        self.fields = {}
        for config in configs:
            for prop in config["properties"]:
                self.members[prop["name"]] = prop["value"]
                if prop.get("setter"):
                    self.setters[prop["setter"]] = prop["name"]
                if prop.get("clear"):
                    self.clears[prop["clear"]] = prop["name"]
                if prop.get("field") and prop.get("storage", "cell") == "cell":
                    self.fields[prop["field"]] = prop["name"]
        self.unmatched = []

    def positions(self, log_text, build_dir):
        found = {}
        for raw in log_text.splitlines():
            match = DIAGNOSTIC.match(raw.strip())
            if match is None:
                continue
            path = (build_dir / match.group("path")).resolve()
            try:
                path.relative_to(self.root)
            except ValueError:
                continue
            item = (int(match.group("line")), int(match.group("column")), match.group("message"))
            found.setdefault(path, set()).add(item)
        return found

    def rewrite_call(self, text, start, name, replacement_of):
        open_index = start + len(name)
        if text[open_index:open_index + 1] != "(":
            return None
        close = matching(text, open_index, "(", ")")
        arguments = text[open_index + 1:close].strip()
        replacement = replacement_of(arguments)
        if replacement is None:
            return None
        return text[:start] + replacement + text[close + 1:]

    def rewrite_at(self, text, offset, message):
        """The rewritten text, or None."""
        if any(phrase in message for phrase in CALLED_OBJECT):
            pattern = re.compile(r"\b(%s)\(\)" % "|".join(map(re.escape, self.members)))
            line_stop = line_end(text, offset)
            match = pattern.search(text, offset, line_stop)
            if match is None:
                return None
            return text[:match.start()] + match.group(1) + ".read()" + text[match.end():]
        named = re.match(r"(?:no member named|use of undeclared identifier) '(\w+)'", message)
        if named:
            name = named.group(1)
            if text[offset:offset + len(name)] != name:
                found = text.find(name, offset, line_end(text, offset))
                if found < 0:
                    return None
                offset = found
            if name in self.setters:
                member = self.setters[name]
                return self.rewrite_call(text, offset, name, SetterReplacement(member))
            if name in self.clears:
                member = self.clears[name]
                return self.rewrite_call(text, offset, name, ClearReplacement(member))
            if name in self.fields:
                after = text[offset + len(name):offset + len(name) + 3]
                if re.match(r"\s*(?:[-+*/|&^]?=[^=]|\+\+|--)", after):
                    return None
                return text[:offset] + self.fields[name] + ".read()" + text[offset + len(name):]
            return None
        if "too few arguments to function call" in message and self.setters:
            # A one-argument call of a deleted setter now resolves to a kept
            # overload with more parameters (SetAutoScrollMargin(x, y)).
            pattern = re.compile(r"\b(%s)\(" % "|".join(map(re.escape, self.setters)))
            candidates = list(pattern.finditer(text, line_start(text, offset), line_end(text, offset)))
            if not candidates:
                return None
            name = candidates[-1].group(1)
            return self.rewrite_call(text, candidates[-1].start(), name, SetterReplacement(self.setters[name]))
        if "ambiguous" in message and "operator '='" in message:
            line_stop = line_end(text, offset)
            pattern = re.compile(r"\b(%s)\s*=\s*\{\}" % "|".join(map(re.escape, self.members)))
            match = pattern.search(text, line_start(text, offset), line_stop)
            if match is None:
                return None
            value = self.members[match.group(1)]
            return text[:match.start()] + f"{match.group(1)} = {value}{{}}" + text[match.end():]
        return None

    def apply(self, log_text, build_dir):
        total = 0
        for path, items in sorted(self.positions(log_text, build_dir).items()):
            data = path.read_bytes().decode("utf-8")
            newline = "\r\n" if "\r\n" in data else "\n"
            text = data.replace("\r\n", "\n")
            offsets = [0]
            for line in text.split("\n"):
                offsets.append(offsets[-1] + len(line) + 1)
            changed = 0
            # Last position first, so earlier offsets stay valid.
            for line, column, message in sorted(items, reverse=True):
                if line - 1 >= len(offsets) - 1:
                    continue
                offset = offsets[line - 1] + column - 1
                rewritten = self.rewrite_at(text, offset, message)
                if rewritten is None:
                    if any(word in message for word in ("no member named", "undeclared", "ambiguous")) \
                            or any(phrase in message for phrase in CALLED_OBJECT):
                        self.unmatched.append(f"{path}:{line}:{column}: {message}")
                    continue
                text = rewritten
                changed += 1
            if changed:
                path.write_bytes(text.replace("\n", newline).encode("utf-8"))
                total += changed
        return total


class SetterReplacement:
    """`set_x(arguments)` -> `x = arguments` (a named callable, not a lambda)."""

    def __init__(self, member):
        self.member = member

    def __call__(self, arguments):
        if not arguments or top_level_commas(arguments):
            return None
        return f"{self.member} = {arguments}"


class ClearReplacement:
    """`clear_x()` -> `x = std::nullopt`."""

    def __init__(self, member):
        self.member = member

    def __call__(self, arguments):
        if arguments:
            return None
        return f"{self.member} = std::nullopt"


def count_errors(log_text):
    return sum(1 for line in log_text.splitlines() if ": error: " in line)


def command_rewrite(arguments):
    configs = [load(path) for path in arguments.configs]
    root = pathlib.Path.cwd().resolve()
    rewriter = Rewriter(configs, root)
    build_dir = pathlib.Path(arguments.build_dir).resolve()
    log_path = pathlib.Path(arguments.log)
    for round_number in range(1, arguments.rounds + 1):
        if arguments.build_command:
            # The operator's command, split as a POSIX shell would split it but
            # run without a shell (audit P20).
            subprocess.run(shlex.split(arguments.build_command), check=False)
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        errors = count_errors(log_text)
        if errors == 0:
            print(f"round {round_number}: clean")
            break
        rewritten = rewriter.apply(log_text, build_dir)
        print(f"round {round_number}: {errors} errors, {rewritten} rewritten")
        if rewritten == 0 or not arguments.build_command:
            break
    for line in sorted(set(rewriter.unmatched))[:40]:
        print("unmatched: " + line)
    return 0


# The audit ------------------------------------------------------------------

PARAMETER_LISTS = re.compile(r"\(([^()]*)\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?[;{]")
NEWLINE = "\n"
LOCALS = re.compile(r"^\s+(?:const\s+)?[\w:<>]+[\s\*&]+(\w+)\s*(?:=|\{|\()", re.M)


def named_parameters(parameter_text, names):
    found = []
    for parameter in parameter_text.split(","):
        if "decltype(" in parameter:
            # A hook's cell parameter names the property's type, not a variable.
            continue
        words = re.findall(r"\w+", parameter.split("=")[0])
        if len(words) >= 2 and words[-1] in names:
            found.append(words[-1])
    return found


def command_audit(arguments):
    """Members of the declaring class whose parameters or locals are named
    after one of its properties: inside them the name means the parameter,
    so a write meant for the cell would go to it (clang's -Wshadow does not
    report names inherited from a base)."""
    findings = []
    for path in arguments.configs:
        config = load(path)
        names = {prop["name"] for prop in config["properties"]}
        declaring = config["declaring"]
        header = pathlib.Path(config["header"]).read_text(encoding="utf-8")
        head, body_open, body_close = class_body(header, declaring)
        for match in PARAMETER_LISTS.finditer(header, body_open, body_close):
            for name in named_parameters(match.group(1), names):
                line = header.count(NEWLINE, 0, match.start()) + 1
                findings.append(f"{config['header']}:{line}: parameter {name!r} names a property")
        source = pathlib.Path(config["source"]).read_text(encoding="utf-8")
        for match in re.finditer(r"\b%s::~?\w+\(" % re.escape(declaring), source):
            open_index = match.end() - 1
            close = matching(source, open_index, "(", ")")
            brace = source.find("{", close)
            semicolon = source.find(";", close)
            if brace < 0 or 0 <= semicolon < brace:
                continue
            line = source.count(NEWLINE, 0, match.start()) + 1
            for name in named_parameters(source[open_index + 1:close], names):
                findings.append(f"{config['source']}:{line}: parameter {name!r} names a property")
            body_end = matching(source, brace, "{", "}")
            for local in LOCALS.finditer(source, brace, body_end):
                if local.group(1) in names:
                    local_line = source.count(NEWLINE, 0, local.start()) + 1
                    findings.append(f"{config['source']}:{local_line}: local {local.group(1)!r} names a property")
    for finding in findings:
        print(finding)
    print(f"audit: {len(findings)} findings")
    return 1 if findings else 0


# Generated tests ------------------------------------------------------------

DEFAULT_SAMPLES = {
    "bool": None,
    "std::string": 'std::string("codemod sample")',
    "Insets": "Insets{1.0, 2.0, 3.0, 4.0}",
    "std::optional<bool>": "std::optional<bool>{true}",
    "std::optional<Color>": "std::optional<Color>{Color::rgba(10, 20, 30)}",
}

TEST_PROLOGUE = """// Generated by tools/property_codemod.py tests from tools/property_families/;
// do not edit. Per property of each family with a "test" block: a write
// stores (read back), an equal write raises nothing, and a changed write
// raises the change event once (property-capabilities study, section 15.2).
{includes}

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {{

using namespace gui_forms;

void expect(const bool condition, const std::string_view message) {{
    if (condition) return;
    std::cerr << "generated property test failure: " << message << '\\n';
    std::exit(EXIT_FAILURE);
}}

// Counts any change event's raises.
struct RaiseCounter final {{
    int* count{{}};
    template <typename... Arguments>
    void operator()(Arguments&&...) const {{
        ++(*count);
    }}
}};

"""


def test_function(config):
    test = config["test"]
    family = config["family"]
    concrete = test["type"]
    # A control whose constructor needs more than its identity names the
    # extra arguments in "arguments" and the statements declaring them in
    # "setup". Statements in "configure" run on the made control before the
    # properties are written (a sample that needs state, such as a row).
    arguments = "".join(", " + argument for argument in test.get("arguments", []))
    lines = [f"void test_{snake(family)}_properties() {{"]
    lines += ["    " + statement for statement in test.get("setup", [])]
    lines.append(f"    std::shared_ptr<{concrete}> control = make_control<{concrete}>(StableId(\"generated.{family.lower()}\"){arguments});")
    lines += ["    " + statement for statement in test.get("configure", [])]
    for prop in config["properties"]:
        if prop.get("storage") == "view":
            continue
        member = prop["name"]
        value = prop["value"]
        sample = prop.get("sample") or DEFAULT_SAMPLES.get(value)
        if value == "bool" and not prop.get("sample"):
            sample = f"!(*control).{member}.read()"
        if sample is None:
            lines.append(f"    // {member}: no sample for {value}; give the property a \"sample\".")
            continue
        label = f"{family}.{member}"
        lines += ["    {", f"        const {value} sample = {sample};"]
        if prop.get("event"):
            accessor = prop["event"].rstrip("_")
            lines += ["        int raises = 0;",
                      f"        const SubscriptionToken token = (*control).{accessor}().subscribe(RaiseCounter{{&raises}});"]
        lines += [f"        (*control).{member} = sample;",
                  f"        expect((*control).{member}.read() == sample, \"{label}: a write stores\");"]
        if prop.get("event"):
            lines += [f"        expect(raises == 1, \"{label}: a changed write raises once\");",
                      f"        (*control).{member} = sample;",
                      f"        expect(raises == 1, \"{label}: an equal write raises nothing\");",
                      "        static_cast<void>(token);"]
        table = prop.get("table")
        if table is not None and not table.get("registry") and (prop.get("thunks") or {}).get("read"):
            # A read thunk gives the table its own form of the value (seconds
            # for a duration, null for none), not the cell's traits form.
            lines += [f"        expect((*control).property_value(\"{member}\").has_value(), \"{label}: the table reads it\");"]
        elif table is not None and not table.get("registry"):
            lines += [f"        const std::optional<BindingValue> by_name = (*control).property_value(\"{member}\");",
                      f"        expect(by_name && *by_name == PropertyValueTraits<{value}>::to_binding(sample),",
                      f"               \"{label}: the table reads the cell\");"]
        lines.append("    }")
    # A hand entry (extra_entries) is reached by name: the table describes it.
    for entry in config.get("extra_entries") or []:
        name = re.match(r'\{"([a-z_0-9]+)"', entry).group(1)
        lines.append(f"    expect((*control).property_descriptor(\"{name}\").has_value(), "
                     f"\"{family}.{name}: the table holds the hand entry\");")
    lines += ["}", ""]
    return "\n".join(lines) + "\n"


def command_tests(arguments):
    paths = arguments.configs or sorted(str(path) for path in pathlib.Path("tools/property_families").glob("*.json"))
    configs = [load(path) for path in paths]
    configs = [config for config in configs if config.get("test")]
    includes = sorted({'#include "%s"' % config["test"]["include"] for config in configs})
    text = TEST_PROLOGUE.format(includes="\n".join(['#include "gui_forms/gui_forms.hpp"'] + includes))
    for config in configs:
        text += test_function(config)
    text += "} // namespace\n\nint main() {\n"
    for config in configs:
        text += f"    test_{snake(config['family'])}_properties();\n"
    text += "    std::cout << \"generated property tests passed\\n\";\n    return EXIT_SUCCESS;\n}\n"
    path = pathlib.Path("tests/property_family_generated_tests.cpp")
    path.write_bytes(text.encode("utf-8"))
    print(f"wrote {path} ({len(configs)} families)")
    return 0


FIXTURES = pathlib.Path(__file__).resolve().parent / "property_codemod_fixtures"
FIXTURE_FIELDS = ("delimiters_", "separator_", "terminator_", "groups_", "escaped_", "plain_")


def command_selftest(arguments):
    source = (FIXTURES / "field_initializers.hpp").read_text(encoding="utf-8")
    expected = (FIXTURES / "field_initializers.expected.hpp").read_text(encoding="utf-8")
    config = {"declaring": "Fixture", "header": "field_initializers.hpp",
              "properties": [{"name": field.rstrip("_"), "field": field} for field in FIXTURE_FIELDS]}
    failures = []
    head, body_open, body_close = class_body(source, "Fixture")
    report = []
    ranges = delete_members(source, config, body_open, body_close, report)
    result = collapse_blank_lines(remove_ranges(source, ranges))
    if result != expected:
        failures.append("field deletion differs from field_initializers.expected.hpp:\n" + result)
    if remaining_fields(result, config):
        failures.append("remaining_fields reports a deleted field")
    if remaining_fields(source, config) != list(FIXTURE_FIELDS):
        failures.append("remaining_fields misses a surviving field")
    for line in failures:
        print("FAIL " + line)
    if not failures:
        print("selftest: ok")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check")
    check.add_argument("configs", nargs="+")
    generate = commands.add_parser("generate")
    generate.add_argument("config")
    rewrite = commands.add_parser("rewrite")
    rewrite.add_argument("--log", required=True)
    rewrite.add_argument("--build-dir", required=True)
    rewrite.add_argument("--build-command", help="split with shlex and run without a shell; no pipes or redirection")
    rewrite.add_argument("--rounds", type=int, default=8)
    rewrite.add_argument("configs", nargs="+")
    audit = commands.add_parser("audit")
    audit.add_argument("configs", nargs="+")
    selftest = commands.add_parser("selftest")
    tests = commands.add_parser("tests")
    tests.add_argument("configs", nargs="*")
    arguments = parser.parse_args()
    handlers = {"check": command_check, "generate": command_generate, "rewrite": command_rewrite,
                "audit": command_audit, "tests": command_tests, "selftest": command_selftest}
    return handlers[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
