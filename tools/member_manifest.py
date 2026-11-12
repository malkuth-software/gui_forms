"""Build and check the member manifest (manifests/members.json), 1.0 condition 3.

The manifest lists every GUI.Forms control and every property, event and method
on each against its Windows Forms (.NET 8 or later) counterpart, with a status:
``supported``, ``deferred`` or ``excluded``, and ``unclassified`` while nobody
has decided. Deferred and excluded entries carry a reason. A member with no
Windows Forms counterpart is marked ``gui_forms_only``.

Where each side comes from:

- GUI.Forms: the registry (manifests/controls.json), the schema
  (manifests/control-schema.json: properties and interactions) and the public
  headers (methods, read through tools/api_docs_parse.py and the accessor
  rules of tools/api_docs_rst.py's ``shape_of``).
- Windows Forms: the stage W catalogue (notes:planning/generated/
  WINFORMS_API_CATALOGUE.tsv, built from dotnet/winforms' PublicAPI.Shipped.txt
  for .NET 8, 9 and 10). The catalogue lives on the notes branch, so its rows
  are copied into the manifest when ``generate --catalogue`` reads it; a
  plain ``generate`` keeps the copied rows and rebuilds everything else.

How a status is seeded (a proposal for the owner, not a settled rule; see
``seed_status``):

1. a row of ``W_MEMBER_EVIDENCE`` (stage W's brief, with its source) wins;
2. a member the catalogue excludes in every listing is ``excluded`` with the
   catalogue's reason; every member of a type stage W excludes is ``excluded``
   with that type's reason;
3. a member GUI.Forms declares and a test calls is ``supported``, with the test
   named in ``evidence``;
4. anything else is ``unclassified``, with a ``question`` naming why.

Statuses whose ``basis`` is ``w-brief``, ``owner`` or ``coordinator`` are kept
when the manifest is generated again; the rest are recomputed.

Commands:

    python3 tools/member_manifest.py generate [--catalogue PATH]
    python3 tools/member_manifest.py check

``tools/control_registry.py members [--check]`` runs the same two jobs.

Standard library only; Python 3.12+.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from api_docs_parse import TypeRecord, declarations  # noqa: E402
import api_docs_rst as rst  # noqa: E402

ROOT: Path = Path(__file__).resolve().parent.parent
MANIFEST_PATH: Path = ROOT / "manifests" / "members.json"
REGISTRY_PATH: Path = ROOT / "manifests" / "controls.json"
SCHEMA_PATH: Path = ROOT / "manifests" / "control-schema.json"
TESTS_ROOT: Path = ROOT / "tests"
MANIFEST_SCHEMA: int = 1

STATUSES: tuple[str, ...] = ("supported", "deferred", "excluded", "unclassified")
KINDS: tuple[str, ...] = ("constructor", "property", "event", "method", "field")
# Bases a person set; generate keeps them. The others are recomputed.
MANUAL_BASES: frozenset[str] = frozenset({"w-brief", "owner", "coordinator"})
AUTOMATIC_BASES: frozenset[str] = frozenset({"test", "catalogue", "type", "none"})
SCOPES: tuple[str, ...] = ("admitted", "excluded", "review")
# Catalogue statuses that stage W leaves to a review (ADR-001's platform
# extensions, the package review), so a member carrying one is a question.
REVIEW_STATUSES: frozenset[str] = frozenset(
    {"platform_extension_review", "platform_extension_partial", "package_review"})
FORMS_PREFIX: str = "System.Windows.Forms."
# Types GUI.Forms has outside the registry but in the schema, with their
# counterpart (W brief, "Containers and windows": the top level is Window).
SCHEMA_ONLY_COUNTERPARTS: Mapping[str, str] = {"Window": "Form"}
# The two roots every registry type derives from. Their members are the ones
# every schema record of their kind shares (see ``root_schema_members``).
ROOT_TYPES: Mapping[str, str] = {"Control": "control", "Component": "component"}
ROOT_COUNTERPARTS: Mapping[str, str] = {"Control": "Control", "Component": "Component"}
GUI_FORMS_ONLY: str = "GUI.Forms-only"
# Component's counterpart is System.ComponentModel.Component, which the Forms
# catalogue does not list; its rows come from dotnet/runtime's reference source.
ROOT_NOTES: Mapping[str, str] = {
    "Component": "System.ComponentModel.Component (dotnet/runtime, not the Windows Forms catalogue)",
}
# Types read from dotnet/runtime's reference assemblies' sources
# (src/libraries/<library>/ref/<library>.cs), not from the Forms catalogue.
RUNTIME_TYPES: Mapping[str, str] = {
    "Component": "System.ComponentModel.Primitives",
    "BackgroundWorker": "System.ComponentModel.EventBasedAsync",
}
RUNTIME_VERSIONS: tuple[str, ...] = ("8.0", "9.0", "10.0")
# The release branch heads the reference sources were read at (2026-11-12).
RUNTIME_COMMITS: Mapping[str, str] = {
    "release/8.0": "0f366281b3e6cc5dfad6853c8aea2386a7ba8f6e",
    "release/9.0": "4647bc655507f0420a665c43703f8fa1c398de5f",
    "release/10.0": "56d5bb652aced6bb4fed74ab43fbdd4c39b6b7ed",
}
# Item, cell and band types the controls hold, listed with their nested types
# (collections, sub-items) though no GUI.Forms type stands for them yet. The
# nested types of every WINFORMS_TYPES entry are listed as well.
ITEM_TYPES: tuple[str, ...] = (
    "ListViewItem", "ListViewGroup", "TreeNode",
    "DataGridViewBand", "DataGridViewRow", "DataGridViewColumn", "DataGridViewCell", "DataGridViewCellStyle",
    "DataGridViewTextBoxCell", "DataGridViewCheckBoxCell", "DataGridViewComboBoxCell", "DataGridViewButtonCell",
    "DataGridViewImageCell", "DataGridViewLinkCell", "DataGridViewHeaderCell", "DataGridViewColumnHeaderCell",
    "DataGridViewRowHeaderCell",
)


@dataclass(frozen=True)
class WinFormsType:
    """One Windows Forms control or UI component, from stage W's coverage tables."""

    base: str
    state: str  # done, partial, excluded or review, as the W brief records it
    note: str = ""


# The Windows Forms controls and UI components stage W surveyed
# (notes:planning/stages/W-winforms-controls/README.md, "Coverage, 2026-10-25",
# recounted 2026-10-31), with their Windows Forms base and W's state. Bases are
# Windows Forms' own (BindableComponent is folded into Component); an empty
# base is a type deriving from object.
WINFORMS_TYPES: Mapping[str, WinFormsType] = {
    "Component": WinFormsType("", "done", "System.ComponentModel.Component, read from dotnet/runtime's reference source"),
    "Control": WinFormsType("Component", "done"),
    "ButtonBase": WinFormsType("Control", "done"),
    "Button": WinFormsType("ButtonBase", "done"),
    "CheckBox": WinFormsType("ButtonBase", "done"),
    "RadioButton": WinFormsType("ButtonBase", "done"),
    "Label": WinFormsType("Control", "done"),
    "LinkLabel": WinFormsType("Label", "done"),
    "PictureBox": WinFormsType("Control", "done"),
    "TextBoxBase": WinFormsType("Control", "partial", "TextBoxBase is public since I2 and carries nothing yet; TextBox carries the surface"),
    "TextBox": WinFormsType("TextBoxBase", "done"),
    "MaskedTextBox": WinFormsType("TextBoxBase", "done"),
    "RichTextBox": WinFormsType("TextBoxBase", "done"),
    "ListControl": WinFormsType("Control", "partial", "ListControl is public since I2 and carries nothing yet; the list controls carry the surface"),
    "ListBox": WinFormsType("ListControl", "done"),
    "CheckedListBox": WinFormsType("ListBox", "done"),
    "ComboBox": WinFormsType("ListControl", "done"),
    "ListView": WinFormsType("Control", "done"),
    "ColumnHeader": WinFormsType("Component", "done", "ListViewColumn records, not a control"),
    "TreeView": WinFormsType("Control", "done"),
    "DataGridView": WinFormsType("Control", "partial", "provisional; the remaining breadth is in DATAGRIDVIEW_AUDIT.md"),
    "DataGridViewTextBoxColumn": WinFormsType("", "done", "DataGridViewColumnKind"),
    "DataGridViewCheckBoxColumn": WinFormsType("", "done", "DataGridViewColumnKind"),
    "DataGridViewComboBoxColumn": WinFormsType("", "done", "DataGridViewColumnKind"),
    "DataGridViewButtonColumn": WinFormsType("", "done", "DataGridViewColumnKind"),
    "DataGridViewImageColumn": WinFormsType("", "done", "DataGridViewColumnKind::image"),
    "DataGridViewLinkColumn": WinFormsType("", "done", "DataGridViewColumnKind::link; LinkBehavior not supported"),
    "DataGridViewTextBoxEditingControl": WinFormsType("", "partial", "the grid's own editors, not public types"),
    "DataGridViewComboBoxEditingControl": WinFormsType("", "partial", "the grid's own editors, not public types"),
    "PropertyGrid": WinFormsType("ContainerControl", "done"),
    "UpDownBase": WinFormsType("ContainerControl", "partial", "UpDownBase is public since I2; the spin parts are shared"),
    "NumericUpDown": WinFormsType("UpDownBase", "done"),
    "DomainUpDown": WinFormsType("UpDownBase", "done"),
    "TrackBar": WinFormsType("Control", "done"),
    "ProgressBar": WinFormsType("Control", "done"),
    "ScrollBar": WinFormsType("Control", "done"),
    "HScrollBar": WinFormsType("ScrollBar", "done"),
    "VScrollBar": WinFormsType("ScrollBar", "done"),
    "DateTimePicker": WinFormsType("Control", "done"),
    "MonthCalendar": WinFormsType("Control", "done"),
    "ScrollableControl": WinFormsType("Control", "done"),
    "ContainerControl": WinFormsType("ScrollableControl", "done"),
    "UserControl": WinFormsType("ContainerControl", "done"),
    "Form": WinFormsType("ContainerControl", "partial", "the top level is Window, not a control; MDI is admitted as a presentation of DK's document area, not built"),
    "Panel": WinFormsType("ScrollableControl", "done"),
    "GroupBox": WinFormsType("Control", "done"),
    "FlowLayoutPanel": WinFormsType("Panel", "done"),
    "TableLayoutPanel": WinFormsType("Panel", "done"),
    "SplitContainer": WinFormsType("ContainerControl", "done"),
    "SplitterPanel": WinFormsType("Panel", "done"),
    "Splitter": WinFormsType("Control", "done"),
    "TabControl": WinFormsType("Control", "partial", "Multiline wrapping is deferred by the owner"),
    "TabPage": WinFormsType("Panel", "done"),
    "ToolStrip": WinFormsType("ScrollableControl", "done"),
    "MenuStrip": WinFormsType("ToolStrip", "done"),
    "StatusStrip": WinFormsType("ToolStrip", "done"),
    "ToolStripDropDown": WinFormsType("ToolStrip", "partial", "no public drop-down that hosts arbitrary content"),
    "ToolStripDropDownMenu": WinFormsType("ToolStripDropDown", "partial", "no public drop-down that hosts arbitrary content"),
    "ContextMenuStrip": WinFormsType("ToolStripDropDownMenu", "done"),
    "ToolStripContainer": WinFormsType("ContainerControl", "done"),
    "ToolStripPanel": WinFormsType("ContainerControl", "done"),
    "ToolStripContentPanel": WinFormsType("Panel", "done", "ToolStripContainer::content_panel(), a Panel"),
    "ToolStripPanelRow": WinFormsType("", "partial", "not a public type; several strips share a row"),
    "ToolStripItem": WinFormsType("Component", "done", "item records (ToolStripItemKind)"),
    "ToolStripDropDownItem": WinFormsType("ToolStripItem", "done", "item records (ToolStripItemKind)"),
    "ToolStripButton": WinFormsType("ToolStripItem", "done", "item kinds"),
    "ToolStripLabel": WinFormsType("ToolStripItem", "done", "item kinds"),
    "ToolStripSeparator": WinFormsType("ToolStripItem", "done", "item kinds"),
    "ToolStripDropDownButton": WinFormsType("ToolStripDropDownItem", "done", "item kinds"),
    "ToolStripSplitButton": WinFormsType("ToolStripDropDownItem", "done", "item kinds"),
    "ToolStripControlHost": WinFormsType("ToolStripItem", "partial", "the hosted kinds are fixed; an arbitrary hosted control is not supported"),
    "ToolStripComboBox": WinFormsType("ToolStripControlHost", "done", "hosted item kinds"),
    "ToolStripTextBox": WinFormsType("ToolStripControlHost", "done", "hosted item kinds"),
    "ToolStripProgressBar": WinFormsType("ToolStripControlHost", "done", "hosted item kinds"),
    "ToolStripStatusLabel": WinFormsType("ToolStripLabel", "done", "StatusStrip labels"),
    "ToolStripMenuItem": WinFormsType("ToolStripDropDownItem", "done", "menu items"),
    "ToolStripOverflow": WinFormsType("ToolStripDropDown", "done", "the strip's overflow menu"),
    "BindingNavigator": WinFormsType("ToolStrip", "done"),
    "ToolTip": WinFormsType("Component", "done"),
    "ErrorProvider": WinFormsType("Component", "done"),
    "HelpProvider": WinFormsType("Component", "done"),
    "ImageList": WinFormsType("Component", "done"),
    "Timer": WinFormsType("Component", "done"),
    "BindingSource": WinFormsType("Component", "done"),
    "NotifyIcon": WinFormsType("Component", "done"),
    "Binding": WinFormsType("", "done"),
    "BindingContext": WinFormsType("", "done"),
    "Help": WinFormsType("", "done"),
    "BackgroundWorker": WinFormsType("Component", "done", "System.ComponentModel.BackgroundWorker, read from dotnet/runtime's reference source"),
    "CommonDialog": WinFormsType("Component", "done", "no public base by design; the dialogs carry the members"),
    "FileDialog": WinFormsType("CommonDialog", "done", "no public base by design; OpenFileDialog and SaveFileDialog carry the members"),
    "OpenFileDialog": WinFormsType("FileDialog", "done"),
    "SaveFileDialog": WinFormsType("FileDialog", "done"),
    "FolderBrowserDialog": WinFormsType("CommonDialog", "done"),
    "ColorDialog": WinFormsType("CommonDialog", "done"),
    "FontDialog": WinFormsType("CommonDialog", "done"),
    "TaskDialog": WinFormsType("", "done"),
    "MessageBox": WinFormsType("", "done", "show_message_box (the host's message dialog)"),
    "ThreadExceptionDialog": WinFormsType("Form", "done"),
    "AxHost": WinFormsType("Control", "excluded", "ActiveX hosting"),
    "WebBrowserBase": WinFormsType("Control", "excluded", "browser hosting"),
    "WebBrowser": WinFormsType("WebBrowserBase", "excluded", "browser hosting"),
    "MdiClient": WinFormsType("Control", "excluded", "MDI (stage W's exclusion list; MDI was since admitted as a presentation of DK's document area, not as MdiClient)"),
    "PrintPreviewControl": WinFormsType("Control", "excluded", "printing"),
    "PrintPreviewDialog": WinFormsType("Form", "excluded", "printing"),
    "PrintDialog": WinFormsType("CommonDialog", "excluded", "printing"),
    "PageSetupDialog": WinFormsType("CommonDialog", "excluded", "printing"),
    "ComponentEditorPage": WinFormsType("Panel", "excluded", "design time"),
    "NativeWindow": WinFormsType("", "review", "a platform-extension review row under ADR-001"),
}


@dataclass(frozen=True)
class MemberEvidence:
    """One stage W brief row that classifies Windows Forms members by name."""

    winforms_type: str
    pattern: str  # a full-match regular expression over the member name
    status: str
    reason: str
    source: str


W_BRIEF: str = "notes:planning/stages/W-winforms-controls/README.md"
W_RESUME: str = "notes:planning/stages/W-winforms-controls/RESUME_PLAN.md"
W_MEMBER_EVIDENCE: tuple[MemberEvidence, ...] = (
    MemberEvidence("ListView", "ShowItemToolTips", "excluded",
                   "Data items have no tooltips; the list reveals cut-off text in place (owner, 2026-10-28).", W_RESUME),
    MemberEvidence("TreeView", "ShowNodeToolTips", "excluded",
                   "Data items have no tooltips; the tree reveals cut-off text in place (owner, 2026-10-28).", W_RESUME),
    MemberEvidence("TabControl", "ShowToolTips", "excluded",
                   "A TabPage's own tooltip text shows over its header; there is no switch (owner, 2026-10-28).", W_RESUME),
    MemberEvidence("TabControl", "Multiline", "deferred",
                   "Multiline wrapping is deferred by the owner (2026-10-28); tabs that do not fit scroll.", W_BRIEF),
    MemberEvidence("CommonDialog", "HookProc|OwnerWndProc|RunDialog", "excluded",
                   "A Win32 dialog hook.", W_BRIEF),
    MemberEvidence("CommonDialog", "HelpRequest|OnHelpRequest", "deferred",
                   "Waits for a host help button, which no host dialog has.", W_BRIEF),
    MemberEvidence("FileDialog", "ClientGuid|CustomPlaces|ShowPinnedPlaces|AutoUpgradeEnabled|OkRequiresInteraction|AddToRecent",
                   "excluded", "An IFileDialog-only option.", W_BRIEF),
    MemberEvidence("FileDialog", "RestoreDirectory", "excluded",
                   "No dialog changes the working directory.", W_BRIEF),
    MemberEvidence("Form", "(On)?MdiChildActivate|IsMdiContainer|IsMdiChild|MdiChildren|MdiParent|ActiveMdiChild"
                   "|LayoutMdi|ActivateMdiChild|MdiChildrenMinimizedAnchorBottom", "deferred",
                   "MDI is admitted as a presentation of DK's document area (DK decisions 60 to 62, 2026-11-02), not built yet.",
                   W_BRIEF),
)
# Members stage W's brief names as open questions rather than classifying.
W_OPEN_MEMBERS: Mapping[str, tuple[str, str]] = {
    "FileDialog": ("ShowHiddenFiles|DereferenceLinks|CheckFileExists|CheckPathExists|ValidateNames",
                   "w-open: the W brief lists these as open (the hosts apply fixed rules)"),
}


class ManifestError(Exception):
    """A manifest, registry or catalogue input that cannot be read."""


# ------------------------------------------------------------------- names


_WORD: re.Pattern[str] = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z0-9]+|[A-Z]+")


def snake_name(winforms_name: str) -> str:
    """The GUI.Forms spelling of a Windows Forms name: "SelectedIndexChanged" is "selected_index_changed"."""
    return "_".join(word.lower() for word in _WORD.findall(winforms_name))


# --------------------------------------------------------- Windows Forms side


@dataclass
class WinFormsMember:
    kind: str
    name: str
    scope: str = "admitted"
    reason: str = ""
    overloads: int = 0
    statics: bool = False


def classify_catalogue_member(type_name: str, member: str, api: str) -> tuple[str, str] | None:
    """(kind, name) for one catalogue member text, or None for an unreadable one."""
    accessor: re.Match[str] | None = re.fullmatch(r"(.+)\.(get|set)", member)
    if accessor is not None:
        name: str = accessor.group(1)
        if name.startswith("this["):
            return ("property", "this[]")
        return ("property", name)
    call: re.Match[str] | None = re.fullmatch(r"([A-Za-z_]\w*)\((.*)\)", member)
    if call is not None:
        if call.group(1) == type_name.rsplit(".", 1)[-1]:
            return ("constructor", call.group(1))
        return ("method", call.group(1))
    if re.fullmatch(r"[A-Za-z_]\w*", member) is None:
        return None
    returned: str = api.rsplit("->", 1)[-1] if "->" in api else ""
    if "EventHandler" in returned:
        return ("event", member)
    return ("field", member)


def read_catalogue(path: Path, type_names: Iterable[str], nested: bool = True) -> dict[str, list[WinFormsMember]]:
    """Each named type's own members from the stage W catalogue TSV, and with
    nested those of the types nested in them (keyed "ListView.ListViewItemCollection").

    Override rows are left out: Windows Forms lists an override on the
    deriving type, and the member belongs to the type that declares it. A
    member whose rows are all excluded keeps the first exclusion's reason.
    """
    wanted: dict[str, str] = {FORMS_PREFIX + name: name for name in type_names}
    heads: frozenset[str] = frozenset(wanted.values())
    found: dict[str, dict[tuple[str, str], WinFormsMember]] = {name: {} for name in wanted.values()}
    with path.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t", quoting=csv.QUOTE_NONE)
        for row in reader:
            if row.get("surface") != "System.Windows.Forms" or row.get("kind") != "member":
                continue
            full_owner: str = row.get("owner", "")
            owner: str | None = wanted.get(full_owner)
            if owner is None and nested and full_owner.startswith(FORMS_PREFIX):
                # A nested type (ListView.ListViewItemCollection) of a wanted type.
                short: str = full_owner[len(FORMS_PREFIX):]
                if "." in short and short.split(".", 1)[0] in heads:
                    owner = short
                    found.setdefault(owner, {})
            if owner is None:
                continue
            marker: str = row.get("source_marker", "")
            if "override" in marker:
                continue
            classified: tuple[str, str] | None = classify_catalogue_member(owner, row["member"], row.get("api", ""))
            if classified is None:
                continue
            key: tuple[str, str] = classified
            record: WinFormsMember | None = found[owner].get(key)
            admitted: bool = row.get("disposition") == "admitted"
            review: bool = row.get("status", "") in REVIEW_STATUSES
            if record is None:
                record = WinFormsMember(key[0], key[1], "excluded", row.get("reason", ""))
                found[owner][key] = record
            if admitted and record.scope == "excluded":
                record.scope = "admitted"
                record.reason = ""
            if admitted and review:
                record.scope = "review"
            if key[0] in ("method", "constructor"):
                record.overloads += 1
            if "static" in marker:
                record.statics = True
    result: dict[str, list[WinFormsMember]] = {}
    for name, members in found.items():
        result[name] = sorted(members.values(), key=member_order)
    return result


_ATTRIBUTE_LINE: re.Pattern[str] = re.compile(r"^\s*\[[^\n]*\]\s*$")
_ACCESS: re.Pattern[str] = re.compile(r"^(public|protected)\b")


def read_runtime_class(source: str, type_name: str) -> list[WinFormsMember] | None:
    """A class's public and protected members from a dotnet/runtime reference source.

    The reference sources declare one member per line with stub bodies
    (``{ get { throw null; } }``, ``{ }``); attribute lines are skipped, an
    ``ObsoleteAttribute`` marks the next member excluded, and overrides are
    left out as in the catalogue. None when the class is absent.
    """
    head: re.Match[str] | None = re.search(r"^\s*public (?:[a-z]+ )*partial class " + re.escape(type_name) + r"\b[^\n]*\n\s*\{",
                                           source, re.MULTILINE)
    if head is None:
        return None
    lines: list[str] = []
    for line in source[head.end():].splitlines():
        stripped: str = line.strip()
        if stripped == "}":
            break
        lines.append(stripped)
    found: dict[tuple[str, str], WinFormsMember] = {}
    obsolete: bool = False
    for line in lines:
        if not line:
            continue
        if _ATTRIBUTE_LINE.match(line):
            obsolete = obsolete or "ObsoleteAttribute" in line
            continue
        if not _ACCESS.match(line) or re.search(r"\boverride\b", line):
            obsolete = False
            continue
        declaration: str = re.sub(r"\[[^\]]*\]", "", line)
        kind: str
        name: str
        event: re.Match[str] | None = re.search(r"\bevent\s+\S+\s+([A-Za-z_]\w*)", declaration)
        call: re.Match[str] | None = re.search(r"([A-Za-z_]\w*)\s*(?:<[^>]*>)?\(", declaration)
        accessor: re.Match[str] | None = re.search(r"([A-Za-z_]\w*|this\[[^\]]*\])\s*\{\s*(?:get|set)", declaration)
        if event is not None:
            kind, name = "event", event.group(1)
        elif accessor is not None and (call is None or accessor.start() < call.start()):
            kind, name = "property", "this[]" if accessor.group(1).startswith("this[") else accessor.group(1)
        elif call is not None:
            name = call.group(1)
            kind = "constructor" if name == type_name else "method"
        else:
            field_name: re.Match[str] | None = re.search(r"([A-Za-z_]\w*)\s*(?:=[^;]*)?;", declaration)
            if field_name is None:
                obsolete = False
                continue
            kind, name = "field", field_name.group(1)
        record: WinFormsMember | None = found.get((kind, name))
        if record is None:
            record = WinFormsMember(kind, name, "excluded" if obsolete else "admitted",
                                    "obsolete in the reference source" if obsolete else "")
            found[(kind, name)] = record
        elif not obsolete:
            record.scope, record.reason = "admitted", ""
        if kind in ("method", "constructor"):
            record.overloads += 1
        if re.search(r"\bstatic\b", declaration):
            record.statics = True
        obsolete = False
    return sorted(found.values(), key=member_order)


def read_runtime(directory: Path) -> dict[str, list[WinFormsMember]]:
    """RUNTIME_TYPES' members, merged over every version directory under directory.

    The directory holds <version>/<library>.cs for each of RUNTIME_VERSIONS; a
    member listed by any version counts, and is admitted when any version
    lists it without ObsoleteAttribute.
    """
    result: dict[str, list[WinFormsMember]] = {}
    for type_name, library in RUNTIME_TYPES.items():
        merged: dict[tuple[str, str], WinFormsMember] = {}
        for version in RUNTIME_VERSIONS:
            path: Path = directory / version / f"{library}.cs"
            if not path.is_file():
                raise ManifestError(f"{path}: missing; fetch {library}'s reference source for .NET {version}")
            members: list[WinFormsMember] | None = read_runtime_class(path.read_text(encoding="utf-8"), type_name)
            if members is None:
                raise ManifestError(f"{path}: no class {type_name}")
            for member in members:
                kept: WinFormsMember | None = merged.get((member.kind, member.name))
                if kept is None:
                    merged[(member.kind, member.name)] = member
                    continue
                kept.overloads = max(kept.overloads, member.overloads)
                kept.statics = kept.statics or member.statics
                if member.scope == "admitted":
                    kept.scope, kept.reason = "admitted", ""
        result[type_name] = sorted(merged.values(), key=member_order)
    return result


def member_order(member: WinFormsMember) -> tuple[int, str]:
    return (KINDS.index(member.kind), member.name.lower())


def winforms_chain(name: str) -> list[str]:
    """The type and its Windows Forms bases, nearest first."""
    chain: list[str] = []
    current: str = name
    while current and current in WINFORMS_TYPES and current not in chain:
        chain.append(current)
        current = WINFORMS_TYPES[current].base
    return chain


# --------------------------------------------------------------- GUI.Forms side


@dataclass
class OurType:
    """A GUI.Forms type with a manifest entry and the members it declares."""

    name: str
    winforms: str  # "" for a GUI.Forms-only type
    parent: str  # the nearest base with an entry, "" for a root
    declared: dict[str, set[str]] = field(default_factory=dict)
    full: dict[str, set[str]] = field(default_factory=dict)
    has_constructor: bool = False
    registry_status: str = ""


@dataclass
class Model:
    types: dict[str, OurType]
    order: list[str]
    tests: "TestIndex"


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"{path}: {error}") from error


def header_members(record: TypeRecord) -> tuple[set[str], set[str], set[str], bool]:
    """(properties, events, methods, has a public constructor) a header class declares itself."""
    shape: rst.Shape = rst.shape_of(record)
    properties: set[str] = {prop.name for prop in shape.properties}
    events: set[str] = {event.name for event in shape.events}
    methods: set[str] = {method.name for method in shape.methods}
    # The extension surface: public virtuals, and protected virtuals a
    # subclass overrides (the on_<property>_changed raisers of P-8 among
    # them, Windows Forms' protected On* hooks' counterparts).
    for method in shape.extension:
        if rst.is_override(method):
            continue
        if method.access == "public" or re.search(r"\bvirtual\b", method.signature):
            methods.add(method.name)
    return properties, events, methods, bool(shape.constructors)


def root_schema_members(schema_types: Sequence[dict], kind: str) -> tuple[set[str], set[str]]:
    """The properties and interactions every schema record of one kind shares.

    The schema has no record for Control or Component, so their members are
    taken as the ones every control (or component) record lists.
    """
    properties: set[str] | None = None
    interactions: set[str] | None = None
    for record in schema_types:
        if record.get("kind") != kind:
            continue
        names: set[str] = {prop["name"] for prop in record.get("properties", [])}
        events: set[str] = {event["name"] for event in record.get("interactions", [])}
        properties = names if properties is None else properties & names
        interactions = events if interactions is None else interactions & events
    return properties or set(), interactions or set()


def build_model(registry_path: Path = REGISTRY_PATH, schema_path: Path = SCHEMA_PATH,
                tests_root: Path = TESTS_ROOT) -> Model:
    registry: list[dict] = load_json(registry_path)["controls"]
    schema_types: list[dict] = load_json(schema_path)["types"]
    schema_by_type: dict[str, dict] = {record["type"]: record for record in schema_types}
    parsed, _, _ = declarations()
    records: dict[str, TypeRecord] = {}
    for record in parsed:
        if record.namespace.split("::")[-1:] == ["detail"] or record.owner:
            continue
        records.setdefault(record.name, record)

    order: list[str] = list(ROOT_TYPES)
    counterpart: dict[str, str] = {name: ROOT_COUNTERPARTS[name] for name in ROOT_TYPES}
    status_of: dict[str, str] = {}
    for entry in registry:
        order.append(entry["type"])
        winforms: str = entry.get("winforms", "")
        counterpart[entry["type"]] = "" if winforms == GUI_FORMS_ONLY else winforms
        status_of[entry["type"]] = entry.get("status", "")
    for name, winforms in SCHEMA_ONLY_COUNTERPARTS.items():
        if name in schema_by_type and name not in counterpart:
            order.append(name)
            counterpart[name] = winforms
    entries: frozenset[str] = frozenset(order)

    root_sets: dict[str, tuple[set[str], set[str]]] = {
        name: root_schema_members(schema_types, kind) for name, kind in ROOT_TYPES.items()}

    def own_header(name: str) -> tuple[set[str], set[str], set[str], bool, str]:
        """A class's header members with those of bases that have no entry
        (capability mixins), and the nearest base with an entry."""
        properties: set[str] = set()
        events: set[str] = set()
        methods: set[str] = set()
        constructor: bool = False
        parent: str = ""
        pending: list[str] = [name]
        seen: set[str] = set()
        while pending:
            current: str = pending.pop(0)
            if current in seen or current not in records:
                continue
            seen.add(current)
            record: TypeRecord = records[current]
            found = header_members(record)
            properties |= found[0]
            events |= found[1]
            methods |= found[2]
            if current == name:
                constructor = found[3]
            for base in record.bases:
                short: str = base.split("::")[-1].split("<")[0]
                if short in entries and short != name:
                    if not parent:
                        parent = short
                    continue
                pending.append(short)
        return properties, events, methods, constructor, parent

    types: dict[str, OurType] = {}

    def resolve(name: str) -> OurType:
        if name in types:
            return types[name]
        properties, events, methods, constructor, parent = own_header(name)
        if name in ROOT_TYPES:
            schema_properties, schema_events = root_sets[name]
            parent = ""
        else:
            schema_record: dict = schema_by_type.get(name, {})
            schema_properties = {prop["name"] for prop in schema_record.get("properties", [])}
            schema_events = {event["name"] for event in schema_record.get("interactions", [])}
        if parent == name:
            parent = ""
        parent_full: dict[str, set[str]] = {kind: set() for kind in KINDS}
        if parent:
            parent_full = resolve(parent).full
        all_properties: set[str] = schema_properties | properties
        all_events: set[str] = schema_events | events
        known_properties: set[str] = all_properties | parent_full["property"]
        setters: set[str] = {"set_" + prop for prop in known_properties}
        all_methods: set[str] = {method for method in methods
                                 if method not in setters and method not in known_properties
                                 and method not in all_events}
        declared: dict[str, set[str]] = {
            "constructor": set(),
            "property": all_properties - parent_full["property"],
            "event": all_events - parent_full["event"],
            "method": all_methods - parent_full["method"] - parent_full["property"] - parent_full["event"],
            "field": set(),
        }
        full: dict[str, set[str]] = {kind: declared[kind] | parent_full[kind] for kind in KINDS}
        result = OurType(name, counterpart.get(name, ""), parent, declared, full, constructor,
                         status_of.get(name, ""))
        types[name] = result
        return result

    for name in order:
        resolve(name)
    return Model(types, order, TestIndex.build(tests_root))


def descendants(model: Model, name: str) -> set[str]:
    """The type and every type with an entry whose parent chain reaches it."""
    found: set[str] = {name}
    changed: bool = True
    while changed:
        changed = False
        for candidate in model.types.values():
            if candidate.name not in found and candidate.parent in found:
                found.add(candidate.name)
                changed = True
    return found


# --------------------------------------------------------------------- tests


@dataclass
class TestIndex:
    """Which test files call a member and which name a type."""

    calls: dict[str, set[str]]
    mentions: dict[str, set[str]]

    @staticmethod
    def build(root: Path) -> "TestIndex":
        calls: dict[str, set[str]] = {}
        mentions: dict[str, set[str]] = {}
        call_pattern: re.Pattern[str] = re.compile(r"\.\s*([A-Za-z_]\w*)\s*\(")
        word_pattern: re.Pattern[str] = re.compile(r"\b[A-Z][A-Za-z0-9_]*\b")
        # A test subclass overriding a virtual exercises it as a call does.
        override_pattern: re.Pattern[str] = re.compile(
            r"\b([a-z_]\w*)\s*\([^()]*\)\s*(?:const\s*)?(?:noexcept\s*)?override\b")
        if not root.is_dir():
            return TestIndex(calls, mentions)
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".cpp", ".hpp", ".mm") or not path.is_file():
                continue
            relative: str = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.as_posix()
            text: str = path.read_text(encoding="utf-8", errors="replace")
            for name in set(call_pattern.findall(text)) | set(override_pattern.findall(text)):
                calls.setdefault(name, set()).add(relative)
            for word in set(word_pattern.findall(text)):
                mentions.setdefault(word, set()).add(relative)
        return TestIndex(calls, mentions)

    def first_test(self, member: str, kind: str, type_names: Iterable[str]) -> str:
        """The first test file (by path) that calls the member and names one of the types."""
        calling: set[str] = set(self.calls.get(member, set()))
        if kind == "property":
            calling |= self.calls.get("set_" + member, set())
        if kind == "constructor":
            calling = set()
            for name in type_names:
                calling |= self.mentions.get(name, set())
            return min(calling) if calling else ""
        naming: set[str] = set()
        for name in type_names:
            naming |= self.mentions.get(name, set())
        both: set[str] = calling & naming
        return min(both) if both else ""


# --------------------------------------------------------------- the manifest


def evidence_for(winforms_type: str, name: str) -> MemberEvidence | None:
    for evidence in W_MEMBER_EVIDENCE:
        if evidence.winforms_type == winforms_type and re.fullmatch(evidence.pattern, name):
            return evidence
    return None


def open_question(winforms_type: str, name: str) -> str:
    entry: tuple[str, str] | None = W_OPEN_MEMBERS.get(winforms_type)
    if entry is not None and re.fullmatch(entry[0], name):
        return entry[1]
    return ""


def seed_status(row: dict, model: Model, entry_type: str, winforms_type: str,
                member: WinFormsMember | None) -> None:
    """Fills status, reason, basis, evidence and question for one automatic row."""
    for key in ("status", "reason", "basis", "evidence", "question"):
        row.pop(key, None)
    if member is not None:
        evidence: MemberEvidence | None = evidence_for(winforms_type, member.name)
        if evidence is not None:
            row.update(status=evidence.status, reason=evidence.reason, basis="w-brief", evidence=evidence.source)
            return
        if member.scope == "excluded":
            row.update(status="excluded", reason=f"Out of scope in the stage W catalogue: {member.reason}.",
                       basis="catalogue")
            return
        # A nested type follows its owner: WebBrowser.WebBrowserSite is excluded with WebBrowser.
        declared_by: WinFormsType | None = WINFORMS_TYPES.get(winforms_type) or WINFORMS_TYPES.get(
            winforms_type.split(".", 1)[0])
        if declared_by is not None and declared_by.state == "excluded":
            row.update(status="excluded", reason=f"Stage W excludes {winforms_type}: {declared_by.note}.",
                       basis="type")
            return
    carriers: list[str] = list(row.get("carried_by", []))
    ours: str | None = row.get("gui_forms")
    if ours is not None and entry_type:
        test: str = model.tests.first_test(ours, row["kind"], descendants(model, entry_type))
        if test:
            row.update(status="supported", basis="test", evidence=test)
            return
    question: str = ""
    if member is not None:
        question = open_question(winforms_type, member.name)
        if not question and member.scope == "review":
            question = "review: the catalogue leaves this to ADR-001's platform-extension or package review"
    if not question:
        if ours is not None:
            question = "untested: declared, but no test calls it"
        elif carriers:
            question = "carried: a deriving GUI.Forms type declares it; see carried_by"
        elif entry_type == "" and winforms_type not in WINFORMS_TYPES:
            question = "nested: an item, cell or nested Windows Forms type; no GUI.Forms type stands for it yet"
        elif entry_type == "":
            question = "no-entry: no GUI.Forms type stands for the Windows Forms type"
        elif member is not None and re.fullmatch(r"On[A-Z]\w*", member.name) and member.kind == "method":
            question = "hook: a protected On* raiser; GUI.Forms raises events without an overridable raiser"
        else:
            question = "unmatched: no GUI.Forms member has this name"
    row.update(status="unclassified", basis="none", question=question)


def winforms_row(member: WinFormsMember, qualified: str) -> dict:
    row: dict = {"kind": member.kind, "winforms": qualified, "scope": member.scope}
    if member.overloads > 1:
        row["overloads"] = member.overloads
    if member.statics:
        row["static"] = True
    if member.scope == "excluded":
        row["catalogue_reason"] = member.reason
    return row


def find_ours(our: OurType | None, name: str, kind: str) -> str | None:
    if our is None:
        return None
    if kind == "constructor":
        return our.name if our.has_constructor else None
    snake: str = snake_name(name)
    for candidate_kind in (kind, "property", "event", "method"):
        if snake in our.full.get(candidate_kind, set()):
            return snake
    return None


def carriers_of(model: Model, winforms_type: str, name: str, kind: str, exclude: str) -> list[str]:
    found: list[str] = []
    for type_name in model.order:
        candidate: OurType = model.types[type_name]
        if type_name == exclude or not candidate.winforms:
            continue
        if winforms_type not in winforms_chain(candidate.winforms):
            continue
        if find_ours(candidate, name, kind) is not None:
            found.append(type_name)
    return found


def kind_of(our: OurType, name: str) -> str:
    for kind in ("property", "event", "method", "constructor"):
        if name in our.declared.get(kind, set()):
            return kind
    return "method"


def manual_rows(previous: Mapping) -> dict[tuple[str, str, str, str], dict]:
    """Rows of the committed manifest whose basis a person set, by entry and member."""
    kept: dict[tuple[str, str, str, str], dict] = {}
    for entry in previous.get("controls", []):
        key_type: str = entry.get("type") or ("winforms:" + entry.get("winforms", ""))
        for row in entry.get("members", []):
            if row.get("basis") in MANUAL_BASES:
                kept[(key_type, row.get("kind", ""), row.get("winforms") or "", row.get("gui_forms") or "")] = row
    return kept


def previous_winforms(previous: Mapping) -> dict[str, list[WinFormsMember]]:
    """The Windows Forms rows a committed manifest carries, by declaring type."""
    found: dict[str, dict[tuple[str, str], WinFormsMember]] = {}
    for entry in previous.get("controls", []):
        own: str = entry.get("winforms") or ""
        if own:
            found.setdefault(own, {})
        for row in entry.get("members", []):
            qualified: str | None = row.get("winforms")
            if not qualified:
                continue
            declaring, _, name = qualified.rpartition(".")
            declaring = declaring or own
            if not declaring or row.get("kind") not in KINDS:
                continue
            member = WinFormsMember(row["kind"], name, row.get("scope", "admitted"), row.get("catalogue_reason", ""),
                                    int(row.get("overloads", 1 if row["kind"] in ("method", "constructor") else 0)),
                                    bool(row.get("static", False)))
            found.setdefault(declaring, {})[(member.kind, member.name)] = member
    return {name: sorted(members.values(), key=member_order) for name, members in found.items()}


def build_manifest(model: Model, winforms: Mapping[str, list[WinFormsMember]], previous: Mapping,
                   provenance: Mapping) -> dict:
    kept: dict[tuple[str, str, str, str], dict] = manual_rows(previous)
    controls: list[dict] = []
    covered: set[str] = set()

    def finish(row: dict, key_type: str) -> dict:
        key = (key_type, row["kind"], row.get("winforms") or "", row.get("gui_forms") or "")
        manual: dict | None = kept.get(key)
        if manual is not None:
            for name in ("status", "reason", "basis", "evidence", "question"):
                row.pop(name, None)
                if name in manual:
                    row[name] = manual[name]
        return row

    for type_name in model.order:
        our: OurType = model.types[type_name]
        counterpart: str = our.winforms
        own_members: list[WinFormsMember] = list(winforms.get(counterpart, [])) if counterpart else []
        rows: list[dict] = []
        used: set[str] = set()
        for member in own_members:
            row: dict = winforms_row(member, member.name)
            ours: str | None = find_ours(our, member.name, member.kind)
            row["gui_forms"] = ours
            if ours is None:
                carriers = carriers_of(model, counterpart, member.name, member.kind, type_name)
                if carriers:
                    row["carried_by"] = carriers
            else:
                used.add(ours)
            seed_status(row, model, type_name, counterpart, member)
            rows.append(finish(row, type_name))
        inherited_lookup: dict[str, tuple[str, WinFormsMember]] = {}
        for base in winforms_chain(counterpart)[1:] if counterpart else []:
            for member in winforms.get(base, []):
                if member.kind == "constructor":
                    continue
                inherited_lookup.setdefault(snake_name(member.name), (base, member))
        for kind in ("constructor", "property", "event", "method"):
            for name in sorted(our.declared[kind]):
                if name in used:
                    continue
                match: tuple[str, WinFormsMember] | None = inherited_lookup.get(name)
                if match is not None:
                    row = winforms_row(match[1], f"{match[0]}.{match[1].name}")
                    row["gui_forms"] = name
                    seed_status(row, model, type_name, match[0], match[1])
                else:
                    row = {"kind": kind, "winforms": None, "gui_forms": name, "gui_forms_only": True}
                    seed_status(row, model, type_name, "", None)
                rows.append(finish(row, type_name))
        if counterpart:
            covered.add(counterpart)
        type_test: str = model.tests.first_test("", "constructor", [type_name])
        entry: dict = {"type": type_name, "winforms": counterpart or None, "gui_forms_base": our.parent or None}
        if counterpart and counterpart in WINFORMS_TYPES:
            entry["winforms_base"] = WINFORMS_TYPES[counterpart].base or None
        if type_name in ROOT_NOTES:
            entry["counterpart_note"] = ROOT_NOTES[type_name]
        if type_test:
            entry.update(status="supported", basis="test", evidence=type_test)
        else:
            entry.update(status="unclassified", basis="none", question="untested: no test names the type")
        entry["members"] = rows
        controls.append(entry)

    extra: list[str] = [name for name in ITEM_TYPES if name in winforms]
    extra += sorted(name for name in winforms if name not in WINFORMS_TYPES and name not in ITEM_TYPES)
    listed: list[tuple[str, WinFormsType]] = list(WINFORMS_TYPES.items())
    for name in extra:
        owner: str = name.split(".", 1)[0]
        note: str = f"nested in {owner}" if "." in name else "an item, cell or band type the controls hold"
        owner_type: WinFormsType | None = WINFORMS_TYPES.get(owner)
        if owner_type is not None and owner_type.state == "excluded":
            listed.append((name, WinFormsType("", "excluded", f"nested in {owner}: {owner_type.note}")))
        else:
            listed.append((name, WinFormsType("", "nested", note)))
    for winforms_type, described in listed:
        if winforms_type in covered:
            continue
        rows = []
        for member in winforms.get(winforms_type, []):
            row = winforms_row(member, member.name)
            row["gui_forms"] = None
            carriers = carriers_of(model, winforms_type, member.name, member.kind, "")
            if carriers:
                row["carried_by"] = carriers
            seed_status(row, model, "", winforms_type, member)
            rows.append(finish(row, "winforms:" + winforms_type))
        entry = {"type": None, "winforms": winforms_type, "winforms_base": described.base or None}
        if described.note:
            entry["counterpart_note"] = described.note
        if described.state == "excluded":
            entry.update(status="excluded", reason=f"Stage W excludes it: {described.note}.", basis="w-brief",
                         evidence=W_BRIEF)
        elif described.state == "done":
            entry.update(status="supported", basis="w-brief", evidence=W_BRIEF)
        elif described.state == "nested":
            entry.update(status="unclassified", basis="none",
                         question=f"nested: {described.note}; no GUI.Forms type stands for it yet")
        else:
            entry.update(status="unclassified", basis="w-brief", evidence=W_BRIEF,
                         question=f"w-{described.state}: {described.note}")
        entry["members"] = rows
        controls.append(entry)

    return {
        "schema": MANIFEST_SCHEMA,
        "notes": [
            "Generated by tools/member_manifest.py generate (or tools/control_registry.py members); "
            "check with tools/control_registry.py members --check. Statuses whose basis is w-brief, owner or "
            "coordinator are kept when it is generated again; edit those by hand.",
            "One entry per GUI.Forms registry type (plus Control, Component and Window), then one per Windows "
            "Forms control or UI component with no GUI.Forms type. Members are the type's own: inherited ones "
            "are on the base's entry. A Windows Forms name qualified with a type (TextBoxBase.SelectAll) is "
            "declared by a Windows Forms base and declared by this GUI.Forms type.",
            "status: supported, deferred, excluded or unclassified. basis: test (evidence names the test file "
            "that calls the member or overrides it in a subclass), catalogue (the stage W catalogue's exclusion), type (the type is "
            "excluded), w-brief, owner or coordinator (a written decision; evidence names it), none "
            "(unclassified; question says why). scope is the catalogue's: admitted, excluded or review.",
            "Override rows of the Windows Forms catalogue are not listed: the member belongs to the declaring "
            "type. Nested Windows Forms types (collections, sub-items) and the item, cell and band types "
            "(ITEM_TYPES) follow the controls, unclassified until a GUI.Forms type stands for them.",
        ],
        "statuses": list(STATUSES),
        "sources": dict(provenance),
        "controls": controls,
    }


# --------------------------------------------------------------- validation


def validate(manifest: Mapping, model: Model) -> list[str]:
    """Problems where the manifest and the registry, schema or headers disagree."""
    problems: list[str] = []
    if manifest.get("schema") != MANIFEST_SCHEMA:
        problems.append(f"schema must be {MANIFEST_SCHEMA}")
    entries: list[dict] = list(manifest.get("controls", []))
    by_type: dict[str, dict] = {}
    for entry in entries:
        name: str | None = entry.get("type")
        if name is None:
            if not entry.get("winforms"):
                problems.append("an entry has neither a GUI.Forms nor a Windows Forms type")
            continue
        if name in by_type:
            problems.append(f"{name}: listed twice")
        by_type[name] = entry
    for name in model.order:
        if name not in by_type:
            problems.append(f"{name}: in the registry or schema but not in the manifest")
    for name in by_type:
        if name not in model.types:
            problems.append(f"{name}: in the manifest but not in the registry or schema")
    for entry in entries:
        label: str = entry.get("type") or ("Windows Forms " + str(entry.get("winforms")))
        problems += check_status(label, entry)
        name = entry.get("type")
        our: OurType | None = model.types.get(name) if name else None
        if our is not None and (entry.get("winforms") or "") != our.winforms:
            problems.append(f"{label}: winforms is {entry.get('winforms')!r}, the registry says {our.winforms or None!r}")
        listed: dict[str, set[str]] = {kind: set() for kind in KINDS}
        for row in entry.get("members", []):
            member_label: str = f"{label}.{row.get('gui_forms') or row.get('winforms')}"
            kind: str = row.get("kind", "")
            if kind not in KINDS:
                problems.append(f"{member_label}: unknown kind {kind!r}")
                continue
            problems += check_status(member_label, row)
            if row.get("scope", "admitted") not in SCOPES:
                problems.append(f"{member_label}: unknown scope {row.get('scope')!r}")
            only: bool = bool(row.get("gui_forms_only", False))
            if only != (row.get("winforms") is None):
                problems.append(f"{member_label}: gui_forms_only must be set exactly when winforms is null")
            ours: str | None = row.get("gui_forms")
            if ours is None:
                continue
            if our is None:
                problems.append(f"{member_label}: names a GUI.Forms member on an entry with no GUI.Forms type")
                continue
            if kind == "constructor":
                if not our.has_constructor:
                    problems.append(f"{member_label}: {our.name} has no public constructor")
                continue
            if all(ours not in our.full[candidate] for candidate in ("property", "event", "method")):
                problems.append(f"{member_label}: {our.name} has no member {ours}")
            listed[kind_of(our, ours)].add(ours)
        if our is None:
            continue
        seen: set[str] = set().union(*listed.values())
        for kind in ("property", "event", "method"):
            for missing in sorted(our.declared[kind] - seen):
                problems.append(f"{label}: {kind} {missing} is declared but not listed")
    return problems


def check_status(label: str, record: Mapping) -> list[str]:
    problems: list[str] = []
    status: str | None = record.get("status")
    if status not in STATUSES:
        problems.append(f"{label}: unknown status {status!r}")
    basis: str | None = record.get("basis")
    if basis not in MANUAL_BASES and basis not in AUTOMATIC_BASES:
        problems.append(f"{label}: unknown basis {basis!r}")
    if status in ("deferred", "excluded") and not record.get("reason"):
        problems.append(f"{label}: a {status} entry needs a reason")
    if status == "unclassified" and not record.get("question"):
        problems.append(f"{label}: an unclassified entry needs a question")
    if basis == "test":
        evidence: str = record.get("evidence") or ""
        if not evidence or not (ROOT / evidence).is_file():
            problems.append(f"{label}: test evidence {evidence!r} is not a file")
    if basis in MANUAL_BASES and not record.get("evidence"):
        problems.append(f"{label}: a {basis} decision needs evidence naming it")
    return problems


# --------------------------------------------------------------- summaries


def counts(manifest: Mapping) -> dict[str, int]:
    """Member rows by status, and the GUI.Forms-only rows."""
    totals: dict[str, int] = {status: 0 for status in STATUSES}
    totals["gui_forms_only"] = 0
    for entry in manifest.get("controls", []):
        for row in entry.get("members", []):
            totals[row["status"]] = totals.get(row["status"], 0) + 1
            if row.get("gui_forms_only"):
                totals["gui_forms_only"] += 1
    return totals


def questions(manifest: Mapping) -> dict[str, int]:
    """Unclassified member rows by the first word of their question."""
    found: dict[str, int] = {}
    for entry in manifest.get("controls", []):
        for row in entry.get("members", []):
            if row["status"] != "unclassified":
                continue
            tag: str = row.get("question", "").split(":", 1)[0]
            found[tag] = found.get(tag, 0) + 1
    return dict(sorted(found.items()))


def entry_for(manifest: Mapping, type_name: str) -> dict | None:
    for entry in manifest.get("controls", []):
        if entry.get("type") == type_name:
            return entry
    return None


def load_manifest(path: Path = MANIFEST_PATH) -> dict:
    return load_json(path)


def dump_manifest(manifest: Mapping) -> str:
    """JSON with one member row per line, so a diff shows each changed row."""
    lines: list[str] = ["{"]
    head: list[str] = []
    for key in ("schema", "notes", "statuses", "sources"):
        head.append(f"  {json.dumps(key)}: {json.dumps(manifest[key], ensure_ascii=False)}")
    lines.append(",\n".join(head) + ",")
    lines.append('  "controls": [')
    entries: list[str] = []
    for entry in manifest["controls"]:
        fields: list[str] = []
        for key, value in entry.items():
            if key == "members":
                continue
            fields.append(f"{json.dumps(key)}: {json.dumps(value, ensure_ascii=False)}")
        rows: list[str] = [f"      {json.dumps(row, ensure_ascii=False)}" for row in entry["members"]]
        body: str = "    {" + ", ".join(fields) + ', "members": ['
        body += ("\n" + ",\n".join(rows) + "\n    ]}") if rows else "]}"
        entries.append(body)
    lines.append(",\n".join(entries))
    lines.append("  ]")
    lines.append("}")
    return "\n".join(lines) + "\n"


# ----------------------------------------------------------------------- cli


def catalogue_provenance(path: Path) -> dict:
    return {
        "winforms": {
            "catalogue": "notes:planning/generated/WINFORMS_API_CATALOGUE.tsv",
            "read_from": path.name,
            "built_from": "dotnet/winforms PublicAPI.Shipped.txt, release/8.0, release/9.0 and release/10.0, "
                          "with [Obsolete] from Microsoft.WindowsDesktop.App.Ref 8.0.32, 9.0.21 and 10.0.13 "
                          "(notes:planning/WINFORMS_API_CATALOGUE.md)",
            "catalogue_commit": "notes gui_forms bc50948 (last change to the TSV)",
            "repository": "https://github.com/dotnet/winforms",
            "files": ["src/System.Windows.Forms/PublicAPI.Shipped.txt",
                      "src/System.Windows.Forms.Primitives/PublicAPI.Shipped.txt"],
            "commits": {"release/8.0": "edebee640e06f291a2a2e770cfb86d6bd42563f9",
                        "release/9.0": "3e991f572c195fadf3b06e9e06c7c56d8ac4cd48",
                        "release/10.0": "f3adc279ed152f9b88ec678218011168e90b0bdb"},
            "types": "stage W's coverage tables (WINFORMS_TYPES), ITEM_TYPES and every type nested in either "
                     "(tools/member_manifest.py); the nested types' rows are in the same catalogue, so no "
                     "further dotnet/winforms file was fetched",
        },
        "gui_forms": ["manifests/controls.json", "manifests/control-schema.json", "include/gui_forms (methods)",
                      "tests/ (evidence)"],
    }


def runtime_provenance(directory: Path) -> dict:
    return {
        "repository": "https://github.com/dotnet/runtime",
        "files": {library: f"src/libraries/{library}/ref/{library}.cs" for library in RUNTIME_TYPES.values()},
        "commits": dict(RUNTIME_COMMITS),
        "url_pattern": "https://raw.githubusercontent.com/dotnet/runtime/<commit>/<file>",
        "types": sorted(RUNTIME_TYPES),
        "read_from": directory.name,
    }


def generate(catalogue: Path | None, manifest_path: Path = MANIFEST_PATH, runtime: Path | None = None) -> dict:
    previous: dict = load_json(manifest_path) if manifest_path.is_file() else {}
    model: Model = build_model()
    if (catalogue is None or runtime is None) and not previous:
        raise ManifestError("no committed manifest to take the Windows Forms rows from; pass --catalogue and --runtime")
    winforms: dict[str, list[WinFormsMember]] = previous_winforms(previous) if previous else {}
    provenance: dict = dict(previous.get("sources", {}))
    if catalogue is not None:
        catalogue_types: list[str] = [name for name in [*WINFORMS_TYPES, *ITEM_TYPES] if name not in RUNTIME_TYPES]
        for name in [name for name in winforms if name not in RUNTIME_TYPES]:
            del winforms[name]
        winforms.update(read_catalogue(catalogue, catalogue_types))
        provenance.update(catalogue_provenance(catalogue))
    if runtime is not None:
        winforms.update(read_runtime(runtime))
        provenance["runtime"] = runtime_provenance(runtime)
    return build_manifest(model, winforms, previous, provenance)


def command_generate(catalogue: Path | None, check: bool, manifest_path: Path = MANIFEST_PATH,
                     runtime: Path | None = None) -> int:
    if check:
        model: Model = build_model()
        problems: list[str] = validate(load_manifest(manifest_path), model)
        for problem in problems:
            print(problem, file=sys.stderr)
        if problems:
            print(f"member manifest: {len(problems)} problem(s); run `python3 tools/control_registry.py members`",
                  file=sys.stderr)
            return 1
        print(f"member manifest: {len(model.order)} GUI.Forms types agree with the registry")
        return 0
    manifest: dict = generate(catalogue, manifest_path, runtime)
    manifest_path.write_text(dump_manifest(manifest), encoding="utf-8", newline="\n")
    totals: dict[str, int] = counts(manifest)
    print(f"{manifest_path.relative_to(ROOT) if manifest_path.is_relative_to(ROOT) else manifest_path}: "
          + ", ".join(f"{name} {value}" for name, value in totals.items()))
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subparsers = parser.add_subparsers(dest="command", required=True)
    generate_parser = subparsers.add_parser("generate", help="write manifests/members.json")
    generate_parser.add_argument("--catalogue", type=Path, default=None,
                                 help="the stage W catalogue TSV; without it the committed Windows Forms rows are kept")
    generate_parser.add_argument("--runtime", type=Path, default=None,
                                 help="a directory of dotnet/runtime reference sources, <version>/<library>.cs; "
                                      "without it the committed Component and BackgroundWorker rows are kept")
    subparsers.add_parser("check", help="fail when the manifest and the registry, schema or headers disagree")
    args = parser.parse_args(argv)
    try:
        if args.command == "generate":
            return command_generate(args.catalogue, False, MANIFEST_PATH, args.runtime)
        return command_generate(None, True)
    except ManifestError as error:
        print(f"member manifest: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
