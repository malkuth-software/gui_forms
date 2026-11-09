"""Check and render the GUI.Forms control schema (manifests/control-schema.json).

The control schema is the stage T1 projection of the run-time registry: every
registry type's parts, states, item systems, properties and interactions.
``gui_forms_gallery_pages_tests`` writes it from a laid-out Gallery and fails
when the committed copy is stale. This module has two jobs:

- ``check``: prove the manifest names exactly the types of
  ``manifests/controls.json`` in the same order, that every property and
  part name is snake_case, and that every type's
  interactions are exactly the public events its header declares, base types
  first (so a new event cannot be left out of the schema);
- ``render``: write ``docs/controls/generated/schema.md`` from the manifest,
  or prove the committed page is current with ``--check``.

Standard library only; Python 3.12+.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

import control_registry

ROOT: Path = Path(__file__).resolve().parent.parent
SCHEMA_PATH: Path = ROOT / "manifests" / "control-schema.json"
INCLUDE_ROOT: Path = ROOT / "include"
PAGE_PATH: Path = ROOT / "docs" / "controls" / "generated" / "schema.md"

_CLASS_HEAD: re.Pattern[str] = re.compile(r"^(class|struct)\s+(\w+)(\s+final)?\s*(:[^{;]*)?\{", re.M)
_ACCESS: re.Pattern[str] = re.compile(r"(public|protected|private)\s*:")
_ACCESSOR_TAIL: re.Pattern[str] = re.compile(r"\s*&\s*(\w+)\s*\(\s*\)")
_SNAKE: re.Pattern[str] = re.compile(r"[a-z][a-z0-9]*(_[a-z0-9]+)*")


class SchemaError(Exception):
    """The manifest cannot be read."""


@dataclass(frozen=True)
class Interaction:
    name: str
    payload: str
    mutable_payload: bool


@dataclass(frozen=True)
class ScannedClass:
    bases: tuple[str, ...]
    events: tuple[Interaction, ...]


# --------------------------------------------------------------------------- scan


def _matching(text: str, start: int, opening: str, closing: str) -> int:
    """Index of the bracket closing the one at ``start``, or -1."""
    depth: int = 0
    for index in range(start, len(text)):
        if text[index] == opening:
            depth += 1
        elif text[index] == closing:
            depth -= 1
            if depth == 0:
                return index
    return -1


def _split_arguments(text: str) -> list[str]:
    arguments: list[str] = []
    depth: int = 0
    current: list[str] = []
    for character in text:
        if character == "<":
            depth += 1
        elif character == ">":
            depth -= 1
        if character == "," and depth == 0:
            arguments.append("".join(current).strip())
            current = []
        else:
            current.append(character)
    tail: str = "".join(current).strip()
    if tail:
        arguments.append(tail)
    return arguments


def interaction_from_accessor(accessor: str, arguments: str) -> Interaction:
    """The schema record of ``Event<arguments>& accessor()``.

    The name is the accessor's name; the payload lists the argument
    types without ``const`` or reference marks; the payload is mutable when an
    argument is a non-const reference.
    """
    spelled: list[str] = []
    mutable: bool = False
    for argument in _split_arguments(" ".join(arguments.split())):
        constant: bool = argument.startswith("const ")
        bare: str = argument[len("const "):] if constant else argument
        reference: bool = bare.endswith("&")
        bare = bare.rstrip("&").strip()
        if reference and not constant:
            mutable = True
        spelled.append(bare)
    return Interaction(accessor, ", ".join(spelled), mutable)


def _public_events(body: str, public_by_default: bool) -> list[Interaction]:
    """Public ``Event<...>& name()`` accessors declared directly in a class body."""
    events: list[Interaction] = []
    public: bool = public_by_default
    depth: int = 0
    index: int = 0
    while index < len(body):
        character: str = body[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
        elif depth == 0 and (index == 0 or not (body[index - 1].isalnum() or body[index - 1] == "_")):
            access: re.Match[str] | None = _ACCESS.match(body, index)
            if access is not None:
                public = access.group(1) == "public"
                index = access.end()
                continue
            if body.startswith("Event<", index):
                close: int = _matching(body, index + len("Event"), "<", ">")
                tail: re.Match[str] | None = _ACCESSOR_TAIL.match(body, close + 1)
                if tail is not None and public:
                    events.append(interaction_from_accessor(tail.group(1), body[index + len("Event<"):close]))
                index = close + 1
                continue
        index += 1
    return events


def scan_classes(include_root: Path = INCLUDE_ROOT) -> dict[str, ScannedClass]:
    """Every namespace-level class under include/gui_forms with its bases and public events."""
    classes: dict[str, ScannedClass] = {}
    for header in sorted((include_root / "gui_forms").rglob("*.hpp")):
        source: str = control_registry._strip_comments(header.read_text(encoding="utf-8"))
        for match in _CLASS_HEAD.finditer(source):
            name: str = match.group(2)
            close: int = _matching(source, match.end() - 1, "{", "}")
            body: str = source[match.end():close]
            events: list[Interaction] = _public_events(body, match.group(1) == "struct")
            bases: tuple[str, ...] = control_registry._base_names(match.group(4))
            if name not in classes or events or bases:
                classes[name] = ScannedClass(bases=bases, events=tuple(events))
    return classes


def expected_interactions(type_name: str, classes: Mapping[str, ScannedClass]) -> list[Interaction]:
    """A type's interactions as its headers declare them: every base's, then its own."""
    scanned: ScannedClass | None = classes.get(type_name)
    if scanned is None:
        return []
    collected: list[Interaction] = []
    for base in scanned.bases:
        collected.extend(expected_interactions(base, classes))
    collected.extend(scanned.events)
    return collected


# -------------------------------------------------------------------------- check


def load_schema(path: Path = SCHEMA_PATH) -> dict[str, object]:
    try:
        raw: object = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SchemaError(f"{path}: {error}") from error
    if not isinstance(raw, dict) or not isinstance(raw.get("types"), list):
        raise SchemaError(f"{path}: expected an object with a 'types' list")
    return raw


def _interactions_of(record: Mapping[str, object]) -> list[Interaction]:
    found: list[Interaction] = []
    for raw in record.get("interactions", []):
        found.append(Interaction(str(raw["name"]), str(raw["payload"]), bool(raw["mutable_payload"])))
    return found


_VERSION = re.compile(r"[0-9]+\.[0-9]+")
_CASCADE_LEVELS: tuple[str, ...] = ("built_in", "base_sheet", "user_theme", "theme_sheet", "class_rule",
                                    "stable_id_rule", "inline_style", "important_base_sheet",
                                    "important_user_theme", "important_theme_sheet", "forced_high_contrast",
                                    "important_inline_style")


def _engine_findings(engine: object, vocabulary: object) -> list[str]:
    """The engine section (T2 E4): the version, the cascade levels in order, the
    budgets, and every primitive declaring against the vocabulary within budget."""
    if not isinstance(engine, dict):
        return ["the schema has no engine section (version, cascade levels, budgets and primitives)"]
    findings: list[str] = []
    if not _VERSION.fullmatch(str(engine.get("version"))):
        findings.append(f"engine: version {engine.get('version')!r} is not major.minor")
    if tuple(engine.get("cascade_levels", [])) != _CASCADE_LEVELS:
        findings.append("engine: cascade_levels are not " + ", ".join(_CASCADE_LEVELS) + " in that order")
    budgets: object = engine.get("budgets")
    per_pixel_budget: int = 0
    if not isinstance(budgets, dict) or not isinstance(budgets.get("operations_per_device_pixel"), int):
        findings.append("engine: budgets are missing")
    else:
        per_pixel_budget = int(budgets["operations_per_device_pixel"])
    roles: set[str] = set()
    states: set[str] = set()
    inputs: set[str] = set()
    if isinstance(vocabulary, dict):
        roles = {str(name) for name in vocabulary.get("part_roles", [])}
        states = {str(name) for name in vocabulary.get("states", [])}
        inputs = {str(name) for name in vocabulary.get("engine_inputs", [])}
    primitives: object = engine.get("primitives")
    if not isinstance(primitives, list) or not primitives:
        findings.append("engine: no primitives are registered")
        return findings
    seen: set[str] = set()
    for primitive in primitives:
        name: str = str(primitive.get("name"))
        if not _SNAKE.fullmatch(name) or name in seen:
            findings.append(f"engine: primitive name {name!r} is not snake_case or repeats")
        seen.add(name)
        if not _VERSION.fullmatch(str(primitive.get("introduced"))):
            findings.append(f"engine: {name}: introduced {primitive.get('introduced')!r} is not major.minor")
        painted: list[object] = list(primitive.get("roles", []))
        if not painted:
            findings.append(f"engine: {name} paints no part role")
        for role in painted:
            if roles and str(role) not in roles:
                findings.append(f"engine: {name} paints {role!r}, which is not a vocabulary part role")
        for state in primitive.get("states_read", []):
            if states and str(state) not in states:
                findings.append(f"engine: {name} reads {state!r}, which is not a vocabulary state")
        for read in primitive.get("inputs_read", []):
            if inputs and str(read) not in inputs:
                findings.append(f"engine: {name} reads {read!r}, which is not a vocabulary engine input")
        cost: Mapping[str, object] = primitive.get("cost", {})
        per_pixel: object = cost.get("per_device_pixel")
        if not isinstance(per_pixel, int) or per_pixel <= 0 or (per_pixel_budget and per_pixel > per_pixel_budget):
            findings.append(f"engine: {name}: per_device_pixel cost {per_pixel!r} is not within the budget")
        parameter_names: list[str] = [str(item.get("name")) for item in primitive.get("parameters", [])]
        if len(set(parameter_names)) != len(parameter_names):
            findings.append(f"engine: {name} repeats a parameter")
        for parameter in parameter_names:
            if not _SNAKE.fullmatch(parameter):
                findings.append(f"engine: {name}: parameter {parameter!r} is not snake_case")
        for item in primitive.get("parameters", []):
            if item.get("kind") != "choice":
                continue
            # A choice parameter (E8) names its choices once each, in
            # snake_case, and defaults to one of them.
            choices: list[str] = [str(choice) for choice in item.get("choices", [])]
            label: str = f"engine: {name}: choice {item.get('name')!r}"
            if not choices or len(set(choices)) != len(choices):
                findings.append(f"{label} needs distinct choices")
            for choice in choices:
                if not _SNAKE.fullmatch(choice):
                    findings.append(f"{label}: {choice!r} is not snake_case")
            if str(item.get("default")) not in choices:
                findings.append(f"{label} defaults to {item.get('default')!r}, which is not a choice")
    findings.extend(_expression_findings(engine.get("expressions")))
    findings.extend(_motion_findings(engine.get("transitions"), engine.get("ambient")))
    return findings


def _motion_findings(transitions: object, ambient: object) -> list[str]:
    """Presentational state machines and ambient effects (T2 E7, E8): opacity and
    scale are the engine properties, the machine bounds are positive, and the ambient caps are
    the accepted starting tokens (at most 30 frames per second over at most 10 %
    of the window)."""
    findings: list[str] = []
    if not isinstance(transitions, dict) or not {"opacity", "scale"} <= set(transitions.get("engine_properties", [])):
        findings.append("engine: transitions do not list opacity and scale as the engine properties")
    elif not all(isinstance(transitions.get(key), int) and int(transitions[key]) > 0
                 for key in ("maximum_states", "maximum_per_part")):
        findings.append("engine: transitions: the machine bounds are not positive")
    if not isinstance(ambient, dict):
        findings.append("engine: no ambient section (frame and area caps)")
        return findings
    rate: object = ambient.get("maximum_frames_per_second")
    fraction: object = ambient.get("maximum_window_fraction")
    if not isinstance(rate, (int, float)) or not 0 < float(rate) <= 30:
        findings.append(f"engine: ambient: maximum_frames_per_second {rate!r} is not within 30")
    if not isinstance(fraction, (int, float)) or not 0 < float(fraction) <= 0.1:
        findings.append(f"engine: ambient: maximum_window_fraction {fraction!r} is not within 10 %")
    return findings


_EFFECT_TIME_MODELS: frozenset[str] = frozenset({"static_pose", "transition", "ambient"})
_EFFECT_INPUTS: frozenset[str] = frozenset({"none", "pointer", "pointer_and_keys"})


def _effect_findings(effects: object, type_ids: Sequence[str]) -> list[str]:
    """The effect programs (design note section 7): each once by a snake_case
    name, hosted by a schema type, with a known time model and input,
    non-negative integer costs, a positive state budget and frame rate, and
    four non-negative outsets."""
    if not isinstance(effects, list):
        return ["engine: no effects list (the effect programs)"]
    findings: list[str] = []
    names: list[str] = []
    effect: object
    for effect in effects:
        if not isinstance(effect, dict):
            findings.append(f"engine: effects: {effect!r} is not a record")
            continue
        name: str = str(effect.get("name"))
        names.append(name)
        label: str = f"engine: effect {name!r}"
        if not _SNAKE.fullmatch(name):
            findings.append(f"{label} is not snake_case")
        if str(effect.get("parent")) not in type_ids:
            findings.append(f"{label}: parent {effect.get('parent')!r} is not a schema type")
        if effect.get("time_model") not in _EFFECT_TIME_MODELS:
            findings.append(f"{label}: unknown time model {effect.get('time_model')!r}")
        if effect.get("input") not in _EFFECT_INPUTS:
            findings.append(f"{label}: unknown input {effect.get('input')!r}")
        cost: object = effect.get("cost")
        counts: list[object] = [effect.get("engine_version"), effect.get("setup_cost")]
        if isinstance(cost, dict):
            counts.extend([cost.get("fixed"), cost.get("per_device_pixel")])
        else:
            findings.append(f"{label} has no cost record")
        if not all(isinstance(count, int) and not isinstance(count, bool) and count >= 0 for count in counts):
            findings.append(f"{label}: a version or cost is not a non-negative integer")
        budget: object = effect.get("state_budget")
        if not isinstance(budget, int) or isinstance(budget, bool) or budget <= 0:
            findings.append(f"{label}: state_budget {budget!r} is not positive")
        rate: object = effect.get("frames_per_second")
        if not isinstance(rate, (int, float)) or isinstance(rate, bool) or not float(rate) > 0:
            findings.append(f"{label}: frames_per_second {rate!r} is not positive")
        if not isinstance(effect.get("setup_on_geometry"), bool):
            findings.append(f"{label}: setup_on_geometry is not a boolean")
        outsets: object = effect.get("outsets")
        if not (isinstance(outsets, list) and len(outsets) == 4 and
                all(isinstance(side, (int, float)) and not isinstance(side, bool) and float(side) >= 0
                    for side in outsets)):
            findings.append(f"{label}: outsets {outsets!r} are not four non-negative numbers")
    if len(set(names)) != len(names):
        findings.append("engine: effects repeat a name")
    return findings


def _expression_findings(expressions: object) -> list[str]:
    """The bounded expressions (T2 E5): a positive operation budget and stack
    bound, and the operations by snake_case name, each once."""
    if not isinstance(expressions, dict):
        return ["engine: no expressions section (budget and operations)"]
    findings: list[str] = []
    for key in ("maximum_operations", "maximum_stack"):
        bound: object = expressions.get(key)
        if not isinstance(bound, int) or bound <= 0:
            findings.append(f"engine: expressions: {key} {bound!r} is not a positive bound")
    operations: list[str] = [str(name) for name in expressions.get("operations", [])]
    if not operations:
        findings.append("engine: expressions list no operations")
    if len(set(operations)) != len(operations):
        findings.append("engine: expressions repeat an operation")
    for operation in operations:
        if not _SNAKE.fullmatch(operation):
            findings.append(f"engine: expressions: operation {operation!r} is not snake_case")
    return findings


# The window (windows.create's object) follows the registry's types: a
# component the registry does not list, whose interactions are the subset of
# Window's events the C ABI declares (window.cpp), not every public event.
WINDOW_TYPE_ID: str = "window"


def validate(schema: Mapping[str, object], registry: control_registry.Registry,
             classes: Mapping[str, ScannedClass]) -> list[str]:
    """Every finding; an empty list means the manifest is consistent."""
    findings: list[str] = []
    types: list[Mapping[str, object]] = list(schema["types"])  # type: ignore[arg-type]
    registry_ids: list[str] = [record.id for record in registry.records]
    schema_ids: list[str] = [str(record.get("id")) for record in types]
    if not schema_ids or schema_ids[-1] != WINDOW_TYPE_ID or schema_ids.count(WINDOW_TYPE_ID) != 1:
        findings.append(f"the schema's last type is not the one {WINDOW_TYPE_ID!r} type")
    else:
        window: Mapping[str, object] = types[-1]
        if window.get("type") != "Window" or window.get("kind") != "component":
            findings.append(f"{WINDOW_TYPE_ID}: not the component Window")
        schema_ids = schema_ids[:-1]
    if schema_ids != registry_ids:
        missing: list[str] = [item for item in registry_ids if item not in schema_ids]
        extra: list[str] = [item for item in schema_ids if item not in registry_ids]
        findings.append("the schema's types differ from manifests/controls.json "
                        f"(missing: {missing or 'none'}; extra: {extra or 'none'}; "
                        "or the order differs)")
    vocabulary: object = schema.get("vocabulary")
    if not isinstance(vocabulary, dict):
        findings.append("the schema has no vocabulary (part roles, states, custom states, marks, engine inputs, values, "
                        "families, semantic roles, actions, states and fields)")
    else:
        for key in ("part_roles", "states", "custom_states", "marks", "engine_inputs", "values", "families",
                    "semantic_roles", "semantic_actions", "semantic_states", "semantic_fields"):
            names: object = vocabulary.get(key)
            if not isinstance(names, list) or not names:
                findings.append(f"vocabulary: {key} is missing or empty")
                continue
            if len(set(names)) != len(names):
                findings.append(f"vocabulary: {key} repeats a name")
            for name in names:
                if not _SNAKE.fullmatch(str(name)):
                    findings.append(f"vocabulary: {key} name {name!r} is not snake_case")
    findings.extend(_engine_findings(schema.get("engine"), vocabulary))
    engine: object = schema.get("engine")
    findings.extend(_effect_findings(engine.get("effects") if isinstance(engine, dict) else None, schema_ids))
    by_id: dict[str, control_registry.ControlRecord] = {record.id: record for record in registry.records}
    # A child property may not share a name with a property of every control (study section 10.2).
    every_control: set[str] = set(_common_names(types, "properties"))
    for record in types:
        identity: str = str(record.get("id"))
        registered: control_registry.ControlRecord | None = by_id.get(identity)
        if registered is not None and record.get("type") != registered.type:
            findings.append(f"{identity}: type {record.get('type')!r} is not {registered.type!r}")
        kind: object = record.get("kind")
        if kind == "control" and not record.get("parts"):
            findings.append(f"{identity}: a control declares no parts")
        for item in record.get("properties", []):  # type: ignore[union-attr]
            if not _SNAKE.fullmatch(str(item.get("name"))):
                findings.append(f"{identity}: property {item.get('name')!r} is not snake_case")
        for item in record.get("child_properties", []):  # type: ignore[union-attr]
            if not _SNAKE.fullmatch(str(item.get("name"))):
                findings.append(f"{identity}: child property {item.get('name')!r} is not snake_case")
            if str(item.get("name")) in every_control:
                findings.append(f"{identity}: child property {item.get('name')!r} is also every control's property")
        for part in record.get("parts", []):  # type: ignore[union-attr]
            if not _SNAKE.fullmatch(str(part.get("name"))):
                findings.append(f"{identity}: part {part.get('name')!r} is not snake_case")
        if identity == WINDOW_TYPE_ID:
            continue
        expected: list[Interaction] = expected_interactions(str(record.get("type")), classes)
        exported: list[Interaction] = _interactions_of(record)
        if exported != expected:
            missing_names: list[str] = [item.name for item in expected if item not in exported]
            extra_names: list[str] = [item.name for item in exported if item not in expected]
            findings.append(f"{identity}: interactions differ from the public events in the headers "
                            f"(not exported: {missing_names or 'none'}; not in headers: "
                            f"{extra_names or 'none'}; or the order or a payload differs)")
    return findings


# ------------------------------------------------------------------------- render


def _code(text: str) -> str:
    return f"`{text}`"


def _states_text(states: Sequence[object]) -> str:
    return ", ".join(_code(str(state)) for state in states) if states else "none"


def _parts_table(parts: Sequence[Mapping[str, object]]) -> list[str]:
    lines: list[str] = ["| Part | Role | Reuses | Own states |", "|---|---|---|---|"]
    for part in parts:
        reuses: object = part.get("composed_from")
        own: Sequence[object] = part.get("states", [])  # type: ignore[assignment]
        lines.append(f"| {_code(str(part['name']))} | {part['role']} | "
                     f"{_code(str(reuses)) if reuses else ''} | "
                     f"{', '.join(str(state) for state in own)} |")
    return lines


def _common_names(types: Sequence[Mapping[str, object]], key: str) -> list[str]:
    """Names every control lists under ``key``, in the first control's order."""
    controls: list[Mapping[str, object]] = [record for record in types if record.get("kind") == "control"]
    if not controls:
        return []
    first: list[str] = [str(item["name"]) for item in controls[0].get(key, [])]  # type: ignore[union-attr]
    common: list[str] = []
    for name in first:
        if all(any(str(item["name"]) == name for item in record.get(key, [])) for record in controls):  # type: ignore[union-attr]
            common.append(name)
    return common


def render_page(schema: Mapping[str, object]) -> str:
    types: list[Mapping[str, object]] = list(schema["types"])  # type: ignore[arg-type]
    common_properties: list[str] = _common_names(types, "properties")
    common_interactions: list[str] = _common_names(types, "interactions")
    lines: list[str] = [
        "# Control schema",
        "",
        "Generated from `manifests/control-schema.json` by `python3 tools/control_schema.py render`;",
        "do not edit. The manifest is written by `gui_forms_gallery_pages_tests` from the run-time",
        "registry (stage T1), so this page lists what the library publishes.",
        "",
        "Every name here is a registered name, stored in snake_case (owner decision 8). Theme",
        "and form documents, which are CSS-family, write the same words in kebab-case",
        "(`scroll_thumb` is `::part(scroll-thumb)`, `auto_size_mode` is `auto-size-mode`);",
        "PropertyGrid and C# show them in PascalCase (`AutoSizeMode`). C++ reads the anatomy as",
        "typed records (`(*bar).parts().thumb`) and registers handlers through each type's events",
        "(`(*button).clicked()`). An interaction's payload is the handler's argument types; *by",
        "reference* marks a payload the handler receives by mutable reference.",
        "",
        "Every control also has these properties: "
        + ", ".join(_code(name) for name in common_properties) + ".",
        "",
        "Every control also raises these interactions: "
        + ", ".join(_code(name) for name in common_interactions) + ".",
        "",
    ]
    vocabulary: Mapping[str, object] = schema.get("vocabulary", {})  # type: ignore[assignment]
    if vocabulary:
        lines.append("## Vocabulary")
        lines.append("")
        lines.append("The atoms controls declare and themes address (design language §8.5 and §12.7).")
        lines.append("")
        titles: list[tuple[str, str]] = [("part_roles", "Part roles"), ("states", "States"),
                                         ("custom_states", "Admitted custom states"),
                                         ("marks", "Marks"), ("engine_inputs", "Engine inputs"),
                                         ("values", "Values"),
                                         ("families", "Recipe families"),
                                         ("semantic_roles", "Semantic roles"),
                                         ("semantic_actions", "Semantic actions"),
                                         ("semantic_states", "Semantic states"),
                                         ("semantic_fields", "Semantic node fields")]
        for key, title in titles:
            names: Sequence[object] = vocabulary.get(key, [])  # type: ignore[assignment]
            lines.append(f"{title}: " + ", ".join(_code(str(name)) for name in names) + ".")
            lines.append("")
    engine: Mapping[str, object] = schema.get("engine", {})  # type: ignore[assignment]
    if engine:
        lines.append("## Engine")
        lines.append("")
        lines.append(f"Theme engine {engine.get('version')} (T2): the primitives a theme's rules may select, "
                     "the atoms each declares against, and its cost. Cascade levels, lowest first: "
                     + ", ".join(_code(str(level)) for level in engine.get("cascade_levels", [])) + ".")
        lines.append("")
        lines.append("| Primitive | Since | Paints | Reads | Cost per device pixel | Parameters |")
        lines.append("|---|---|---|---|---|---|")
        for primitive in engine.get("primitives", []):  # type: ignore[union-attr]
            painted: str = ", ".join(_code(str(role)) for role in primitive.get("roles", []))
            read_atoms: list[str] = [str(state) for state in primitive.get("states_read", [])]
            read_atoms.extend(str(item) for item in primitive.get("inputs_read", []))
            read: str = ", ".join(_code(atom) for atom in read_atoms) or "none"
            parameters: str = ", ".join(_code(str(item["name"])) for item in primitive.get("parameters", []))
            cost: Mapping[str, object] = primitive.get("cost", {})
            lines.append(f"| {_code(str(primitive['name']))} | {primitive['introduced']} | {painted} | {read} | "
                         f"{cost.get('per_device_pixel')} | {parameters or 'none'} |")
        lines.append("")
        expressions: Mapping[str, object] = engine.get("expressions", {})  # type: ignore[assignment]
        if expressions:
            operations: str = ", ".join(_code(str(name)) for name in expressions.get("operations", []))
            lines.append(f"Bounded expressions: at most {expressions.get('maximum_operations')} operations and a "
                         f"stack of {expressions.get('maximum_stack')}, from {operations}.")
            lines.append("")
        transitions: Mapping[str, object] = engine.get("transitions", {})  # type: ignore[assignment]
        ambient: Mapping[str, object] = engine.get("ambient", {})  # type: ignore[assignment]
        if transitions and ambient:
            properties: str = ", ".join(_code(str(name)) for name in transitions.get("engine_properties", []))
            lines.append(f"Transitions run on {properties} and on the primitives' animated parameters: at most "
                         f"{transitions.get('maximum_states')} states a machine and {transitions.get('maximum_per_part')} "
                         f"transitions a part. Ambient effects: at most {ambient.get('maximum_frames_per_second')} "
                         f"frames per second over at most {ambient.get('maximum_window_fraction')} of the window on "
                         "everyday surfaces.")
            lines.append("")
    for record in types:
        identity: str = str(record["id"])
        lines.append(f"## {_code(identity)} ({record['type']}, {record['kind']})")
        lines.append("")
        if record.get("kind") == "control":
            lines.extend(_parts_table(record.get("parts", [])))  # type: ignore[arg-type]
            lines.append("")
            states: list[str] = [str(state) for state in record.get("states", [])]  # type: ignore[union-attr]
            custom: list[str] = [str(state) for state in record.get("custom_states", [])]  # type: ignore[union-attr]
            lines.append(f"States: {_states_text(states)}."
                         + (f" Application-defined: {_states_text(custom)}." if custom else ""))
            lines.append("")
            for system in record.get("item_systems", []):  # type: ignore[union-attr]
                lines.append(f"Item system {_code(str(system['name']))}:")
                lines.append("")
                lines.extend(_parts_table(system.get("parts", [])))
                lines.append("")
                item_custom: list[str] = [str(state) for state in system.get("custom_states", [])]
                lines.append(f"Item states: {_states_text(system.get('states', []))}."
                             + (f" Application-defined: {_states_text(item_custom)}." if item_custom else ""))
                lines.append("")
            own_properties: list[str] = [
                f"{_code(str(item['name']))} ({item['kind']})"
                for item in record.get("properties", [])  # type: ignore[union-attr]
                if str(item["name"]) not in common_properties]
            lines.append("Properties beyond every control's: "
                         + (", ".join(own_properties) if own_properties else "none") + ".")
            lines.append("")
            provided: list[str] = [
                f"{_code(str(item['name']))} ({item['kind']})"
                for item in record.get("child_properties", [])]  # type: ignore[union-attr]
            if provided:
                lines.append("Properties it provides to its children: " + ", ".join(provided) + ".")
                lines.append("")
        own_interactions: list[Mapping[str, object]] = [
            item for item in record.get("interactions", [])  # type: ignore[union-attr]
            if record.get("kind") != "control" or str(item["name"]) not in common_interactions]
        if own_interactions:
            lines.append("| Interaction | Payload |")
            lines.append("|---|---|")
            for item in own_interactions:
                payload: str = _code(str(item["payload"])) if item["payload"] else "none"
                if item["mutable_payload"]:
                    payload += ", by reference"
                lines.append(f"| {_code(str(item['name']))} | {payload} |")
        else:
            lines.append("Interactions beyond every control's: none." if record.get("kind") == "control"
                         else "Interactions: none.")
        lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------- cli


def _command_check(args: argparse.Namespace) -> int:
    schema: dict[str, object] = load_schema(args.schema)
    registry: control_registry.Registry = control_registry.load_registry()
    findings: list[str] = validate(schema, registry, scan_classes())
    for finding in findings:
        print(f"control schema: {finding}", file=sys.stderr)
    if findings:
        return 1
    print(f"control schema: {len(schema['types'])} types OK")  # type: ignore[arg-type]
    return 0


def _command_render(args: argparse.Namespace) -> int:
    page: str = render_page(load_schema(args.schema))
    if args.check:
        current: str = args.out.read_text(encoding="utf-8") if args.out.exists() else ""
        if current.replace("\r\n", "\n") != page:
            print(f"control schema: {args.out} is stale; run `python3 tools/control_schema.py render`",
                  file=sys.stderr)
            return 1
        print(f"control schema: {args.out} is current")
        return 0
    args.out.write_text(page, encoding="utf-8", newline="\n")
    print(f"control schema: wrote {args.out}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--schema", type=Path, default=SCHEMA_PATH, help="control schema manifest")
    subparsers = parser.add_subparsers(dest="command", required=True)
    check = subparsers.add_parser("check", help="validate the manifest against the registry and headers")
    check.set_defaults(run=_command_check)
    render = subparsers.add_parser("render", help="write or check docs/controls/generated/schema.md")
    render.add_argument("--out", type=Path, default=PAGE_PATH, help="output page")
    render.add_argument("--check", action="store_true", help="fail when the committed page is stale")
    render.set_defaults(run=_command_render)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args: argparse.Namespace = build_parser().parse_args(argv)
    try:
        return int(args.run(args))
    except SchemaError as error:
        print(f"control schema: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
