"""The whole Gallery through AT-SPI, from a client's point of view (stage A, A-L2).

Registered with ctest as ``gui_forms_linux_accessibility_gallery_tests``:

    linux_accessibility_gallery_client.py FIXTURE REGISTRY
    linux_accessibility_gallery_client.py --unsupported REASON

FIXTURE is tests/linux_accessibility_gallery_fixture.cpp's program, which
publishes the Gallery and a supplement window through the ATK adapter and
answers line commands on its standard input with the semantic tree as JSON
(tests/linux_accessibility_gallery_session.hpp). REGISTRY is
manifests/controls.json. The session bus, the AT-SPI bus launcher, the
prerequisite checks and the CI rule (a missing prerequisite fails on CI and
is reported as unsupported, exit 77, elsewhere) are
tests/linux_accessibility_client.py's.

The test, in order:

1. Every Gallery page, then the supplement window: the published AT-SPI tree
   under the fixture's frame is walked over D-Bus, and compared node by node with the semantic tree: child order,
   accessible id (the stable identity), role, name, description, states,
   interfaces, actions in published order, numeric value and range, value
   text, position in set and set size, and the tab-to-panel relation.
2. Every registry control type is accounted for: compared on a page, opened
   by a default action (dialogs and overlays), nonvisual (a component without
   a node of its own), or not exposed by the semantic tree.
3. For one instance of each type, the default action (AT-SPI action 0) is
   performed through the Action interface; the client asks for the semantic
   tree again, compares the whole published tree with it, and requires an
   observed change (a click, the node's own state or value, another node,
   an opened overlay or dialog, or a retained mutation).
4. Generational identity: a Button is disposed and replaced by one with the
   same stable identity. The old object must be refused (an error or a
   false result, never a crash or an action on the new button); the new
   object answers to its own action.

Caching: libatspi answers a property from its cache only while its own
event main loop runs or once a cache mask has been set; this client does
neither, so every read except the object's interfaces (which never change
for one published object) is a D-Bus call to the fixture. No cache mask is
set: with one set (to none), the client's first name read of the fixture's
application did not return within 14 minutes on CI (the run of e9e5ed08),
while A-L1's client, which sets none, reads the same property at once.

Exit 0 when everything matches; 1 with every mismatch listed otherwise.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import faulthandler
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
import traceback
from types import ModuleType
from typing import IO, Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import linux_accessibility_client as session_support  # noqa: E402

APPLICATION_NAME: str = 'GUI.Forms Gallery accessibility fixture'
SUPPLEMENT_NAME: str = 'GUI.Forms accessibility supplement'
ANSWER_SECONDS: float = 60.0
# The client starts no new page or action after this many seconds, so it
# reports what it found, and that it ran out of time, before ctest's TIMEOUT
# for the test (900 s, CMakeLists.txt) stops it without output.
BUDGET_SECONDS: float = 780.0
# After this many seconds the client writes every thread's Python stack
# to standard error and exits with status 1 (faulthandler), so a call that
# never returns is named instead of ending in ctest's silent timeout.
WATCHDOG_SECONDS: float = 840.0
# AT-SPI round trips timed once at the start, to report the bus latency.
LATENCY_PROBES: int = 200
# libatspi's method-call timeout (atspi_set_timeout), and the time it allows
# an application to start, in milliseconds: a call the fixture does not
# answer fails with a GLib error instead of waiting.
CALL_TIMEOUT_MS: int = 10000
STARTUP_TIMEOUT_MS: int = 15000
VIRTUAL_RUNTIME_BIT: int = 1 << 63

# Registry entries without a semantic node of their own: components, and the
# LiveSurface service type. The same list as
# tests/gallery_pages/anatomy_coverage_tests.cpp's nonvisual_entries; a
# component that shows something does so through an internal control.
NONVISUAL: tuple[str, ...] = (
    'command', 'gallery_command', 'font_command', 'color_command', 'context_menu', 'tool_tip',
    'ribbon_tool_tip', 'timer', 'background_worker', 'notify_icon', 'thread_exception_dialog',
    'image_list', 'error_provider',
    'help_provider', 'semantic_feedback', 'binding', 'binding_source', 'binding_context', 'live_surface',
    'dock_workspace', 'open_file_dialog', 'save_file_dialog', 'folder_browser_dialog',
    'color_dialog',
    # A PlotView's item children, which it paints as its own parts; the
    # About box and the help system, components shown through their dialog
    # and viewer; and GaugeBase, the gauges' abstract base.
    'plot_axis_item', 'plot_series_item', 'plot_annotation_item',
    'about_dialog', 'help_book_system', 'gauge_base',
)

# Registry types whose controls still publish their base class's anatomy
# (their own anatomies are pending), so no record names their own type: the
# supplement places one of each. The semantic gallery test lists them as
# unexposed containers.
BASE_ANATOMY: tuple[str, ...] = ('wizard_page', 'help_book_viewer')

# Registry types the Gallery opens on demand, and the node whose default
# action opens one: (type, page, opener stable identity).
OPENERS: tuple[tuple[str, str, str], ...] = (
    ('font_dialog', 'dialogs-and-host-services', 'gallery.host.font.choose'),
    ('task_dialog', 'dialogs-and-host-services', 'gallery.host.task-dialog.show'),
    ('anchored_overlay_layer', 'anchored-overlays', 'gallery.overlays.anchor.0'),
    ('toast', 'notices-and-badges', 'gallery.notices.show-toast'),
)

# Defects this test finds and the ATK adapter cannot fix: the default action
# a target publishes is refused by the window. Listed so the test stays green
# while each is open; a listed target whose action starts to show an effect
# fails the test until it leaves the list. Empty: the five composites listed
# here until the window left their focus action to their own
# on_semantic_action (TokenBox, MaskedTextBox, NumericUpDown, DomainUpDown,
# SplitContainer) are now ordinary targets.
KNOWN_REFUSALS: dict[str, str] = {}

# Targets whose default action the window accepts (in the headless model)
# and which changes nothing the semantic tree or the fixture's counters show,
# with the reason. A listed target that starts to show a change fails the
# test until it leaves the list.
ACCEPTED_WITHOUT_CHANGE: dict[str, str] = {
    'gallery.value-editors.rack.lamp':
        'the first rack module asks to move earlier on decrement; the rack keeps the order',
    'gallery.commands.ribbon-file-area.colour':
        'RibbonColorPicker press applies the current colour (Automatic) again',
    'supplement.callout':
        'the supplement places the callout bare and never shows it, so close leaves it closed',
}

# The Gallery's own frame (navigation, header, command row), which every
# page repeats; a type's action target is chosen outside it.
SHELL_PREFIXES: tuple[str, ...] = ('gallery.command.', 'gallery.page.', 'gallery.nav', 'gallery.header')

# SemanticState bits (include/gui_forms/semantics/types/semantic_types.hpp).
ENABLED: int = 1 << 0
VISIBLE: int = 1 << 1
FOCUSABLE: int = 1 << 2
FOCUSED: int = 1 << 3
SELECTED: int = 1 << 4
CHECKED: int = 1 << 5
MIXED: int = 1 << 6
READ_ONLY: int = 1 << 7
EXPANDED: int = 1 << 8
BUSY: int = 1 << 9
PROTECTED_CONTENT: int = 1 << 10
INVALID: int = 1 << 11
CHECKABLE: int = 1 << 12
MULTISELECTABLE: int = 1 << 13

# The semantic role and the AT-SPI role name the ATK adapter publishes it as
# (native_role in src/host/linux/accessibility/linux_accessibility.cpp).
ROLE_NAMES: dict[str, str] = {
    'generic': 'PANEL', 'group': 'PANEL', 'static_text': 'LABEL', 'button': 'PUSH_BUTTON',
    'check_box': 'CHECK_BOX', 'radio_button': 'RADIO_BUTTON', 'link': 'LINK', 'text_box': 'ENTRY',
    'list': 'LIST', 'list_item': 'LIST_ITEM', 'check_list_item': 'CHECK_BOX', 'combo_box': 'COMBO_BOX',
    'slider': 'SLIDER', 'scroll_bar': 'SCROLL_BAR', 'progress_bar': 'PROGRESS_BAR', 'image': 'IMAGE',
    'tab_group': 'PAGE_TAB_LIST', 'tab': 'PAGE_TAB', 'split_pane': 'SPLIT_PANE', 'numeric_field': 'SPIN_BUTTON',
    'tool_tip': 'TOOL_TIP', 'date_picker': 'DATE_EDITOR', 'calendar': 'CALENDAR', 'date_cell': 'TABLE_CELL',
    'property_grid': 'LIST', 'property_group': 'GROUPING', 'property_row': 'LIST_ITEM', 'menu_bar': 'MENU_BAR',
    'menu_bar_item': 'MENU_ITEM', 'menu': 'MENU', 'menu_item': 'MENU_ITEM', 'separator': 'SEPARATOR',
    'toolbar': 'TOOL_BAR', 'radio_group': 'PANEL', 'split_button': 'PUSH_BUTTON', 'menu_button': 'PUSH_BUTTON',
    'pane': 'PANEL', 'meter': 'LEVEL_BAR', 'tab_panel': 'SCROLL_PANE', 'tree': 'TREE', 'tree_item': 'TREE_ITEM', 'table': 'TABLE',
    'row': 'TABLE_ROW', 'cell': 'TABLE_CELL', 'column_header': 'COLUMN_HEADER', 'row_header': 'ROW_HEADER',
    'tree_grid': 'TREE_TABLE', 'dialog': 'DIALOG', 'status_bar': 'STATUS_BAR', 'live_region': 'PANEL',
    'toggle_switch': 'TOGGLE_BUTTON', 'search_box': 'ENTRY', 'figure': 'CHART',
}

# The role whose states and interfaces a newer role shares
# (semantic_role_fallback in semantic_types.hpp); every other role is its own.
ROLE_FALLBACKS: dict[str, str] = {'toggle_switch': 'check_box', 'search_box': 'text_box', 'figure': 'image'}


def acting_role(node: dict[str, Any]) -> str:
    role: str = ROLE_FALLBACKS.get(node['role'], node['role'])
    return role

# Semantic state bits and the AT-SPI state names that publish them.
STATE_NAMES: tuple[tuple[int, str], ...] = (
    (ENABLED, 'ENABLED'), (ENABLED, 'SENSITIVE'), (VISIBLE, 'VISIBLE'), (VISIBLE, 'SHOWING'),
    (FOCUSABLE, 'FOCUSABLE'), (FOCUSED, 'FOCUSED'), (SELECTED, 'SELECTED'), (CHECKED, 'CHECKED'),
    (MIXED, 'INDETERMINATE'), (EXPANDED, 'EXPANDED'), (BUSY, 'BUSY'), (READ_ONLY, 'READ_ONLY'),
    (INVALID, 'INVALID_ENTRY'), (CHECKABLE, 'CHECKABLE'), (MULTISELECTABLE, 'MULTISELECTABLE'),
)
# States the adapter derives from the role and actions, and DEFUNCT; with
# STATE_NAMES, the states a comparison checks in both directions.
DERIVED_STATE_NAMES: tuple[str, ...] = (
    'EDITABLE', 'CHECKABLE', 'EXPANDABLE', 'SELECTABLE', 'HAS_POPUP', 'HORIZONTAL', 'VERTICAL', 'DEFUNCT',
    'SINGLE_LINE', 'MULTI_LINE', 'MODAL',
)


class ClientFailure(Exception):
    """The fixture or the bus stopped answering as the protocol requires."""


class Progress:
    """Elapsed time since the client started, and the time budget.

    note() writes one line to standard output at once, so a run that ctest
    stops still shows how far it got.
    """

    def __init__(self) -> None:
        self.started: float = time.monotonic()

    def elapsed(self) -> float:
        seconds: float = time.monotonic() - self.started
        return seconds

    def exhausted(self) -> bool:
        over: bool = self.elapsed() > BUDGET_SECONDS
        return over

    def note(self, text: str) -> None:
        print('[' + format(self.elapsed(), '7.1f') + ' s] ' + text, flush=True)


def probe_latency(frame: Any, progress: Progress) -> None:
    """Times LATENCY_PROBES name reads of `frame` (D-Bus calls) and notes the mean."""
    started: float = time.monotonic()
    index: int
    for index in range(LATENCY_PROBES):
        frame.get_name()
    mean_ms: float = (time.monotonic() - started) * 1000.0 / LATENCY_PROBES
    progress.note('AT-SPI round trip: ' + format(mean_ms, '.3f') + ' ms mean over ' + str(LATENCY_PROBES) + ' name reads')


# --- what the ATK adapter publishes for a semantic node ----------------------


def is_virtual(runtime_id: int) -> bool:
    """is_virtual_semantic_runtime_id: the top bit. The window numbers every
    virtual child, so no node has runtime identity 0."""
    result: bool = (runtime_id & VIRTUAL_RUNTIME_BIT) != 0
    return result


def expected_role(node: dict[str, Any]) -> str:
    if (node['states'] & PROTECTED_CONTENT) != 0:
        return 'PASSWORD_TEXT'
    name: str = ROLE_NAMES.get(node['role'], 'PANEL')
    return name


def expected_states(node: dict[str, Any]) -> set[str]:
    """The AT-SPI state names of a live node (state_bits in the adapter)."""
    states: set[str] = set()
    bit: int
    name: str
    for bit, name in STATE_NAMES:
        if (node['states'] & bit) != 0:
            states.add(name)
    actions: list[str] = node['actions']
    acting: str = acting_role(node)
    if acting == 'text_box' and (node['states'] & READ_ONLY) == 0:
        states.add('EDITABLE')
    if acting == 'text_box':
        states.add('MULTI_LINE' if node['multiline'] else 'SINGLE_LINE')
    if acting in ('check_box', 'check_list_item', 'radio_button'):
        states.add('CHECKABLE')
    if (node['states'] & EXPANDED) != 0 or 'expand' in actions or 'collapse' in actions:
        states.add('EXPANDABLE')
    if 'select' in actions:
        states.add('SELECTABLE')
    if node['has_popup']:
        states.add('HAS_POPUP')
    if node['modal']:
        states.add('MODAL')
    if node['orientation'] == 'horizontal':
        states.add('HORIZONTAL')
    if node['orientation'] == 'vertical':
        states.add('VERTICAL')
    return states


ACTIVATING: tuple[str, ...] = ('press', 'select', 'expand', 'collapse', 'show_menu')


def default_action(actions: list[str]) -> int:
    """Action 0's index (default_action in the adapter): the first activating
    action; else focus; else the first action other than set_value; else 0."""
    index: int
    for index in range(len(actions)):
        if actions[index] in ACTIVATING:
            return index
    for index in range(len(actions)):
        if actions[index] == 'focus':
            return index
    for index in range(len(actions)):
        if actions[index] != 'set_value':
            return index
    return 0


def published_actions(actions: list[str]) -> list[str]:
    """The default action first, then the rest in semantic order."""
    published: list[str] = []
    if not actions:
        return published
    primary: int = default_action(actions)
    published.append(actions[primary])
    index: int
    for index in range(len(actions)):
        if index != primary:
            published.append(actions[index])
    return published


def expected_interfaces(node: dict[str, Any]) -> set[str]:
    interfaces: set[str] = {'Accessible', 'Component'}
    if node['actions']:
        interfaces.add('Action')
    text: bool = acting_role(node) == 'text_box'
    if node['numeric_value'] is not None or (node['value'] != '' and not text):
        interfaces.add('Value')
    if text:
        interfaces.add('Text')
        interfaces.add('EditableText')
    return interfaces


# --- reports ------------------------------------------------------------------


@dataclass
class Located:
    """A semantic node of a report, where it is, and who owns it."""
    node: dict[str, Any]
    path: list[int]
    owner: int


def node_key(located: Located) -> tuple[int, str]:
    if is_virtual(located.node['runtime_id']):
        return (located.owner, located.node['stable_id'])
    return (located.node['runtime_id'], '')


def locate_all(report: dict[str, Any]) -> list[Located]:
    """Every node of the report's roots, depth first, with its index path."""
    found: list[Located] = []
    pending: list[Located] = []
    index: int
    for index in range(len(report['roots']) - 1, -1, -1):
        pending.append(Located(report['roots'][index], [index], 0))
    while pending:
        current: Located = pending.pop()
        found.append(current)
        owner: int = current.owner
        if not is_virtual(current.node['runtime_id']):
            owner = current.node['runtime_id']
        children: list[dict[str, Any]] = current.node['children']
        for index in range(len(children) - 1, -1, -1):
            pending.append(Located(children[index], current.path + [index], owner))
    return found


def control_types(report: dict[str, Any]) -> dict[str, str]:
    """Stable identity to registry type, for every control in the report."""
    types: dict[str, str] = {}
    record: dict[str, Any]
    for record in report['controls']:
        types[record['stable_id']] = record['type']
    return types


def in_shell(stable_id: str) -> bool:
    prefix: str
    for prefix in SHELL_PREFIXES:
        if stable_id.startswith(prefix):
            return True
    return False


# --- the fixture process --------------------------------------------------------


class Fixture:
    """The fixture program: one command line in, one answer line out."""

    def __init__(self, path: str) -> None:
        self.errors: IO[str] = tempfile.TemporaryFile('w+', encoding='utf-8')
        self.process: subprocess.Popen[str] = subprocess.Popen(
            [path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.errors,
            text=True, encoding='utf-8', bufsize=1)
        self.pages: list[str] = []

    def read_line(self) -> str:
        assert self.process.stdout is not None
        ready: list[Any]
        ready, _, _ = select.select([self.process.stdout], [], [], ANSWER_SECONDS)
        if not ready:
            raise ClientFailure('the fixture did not answer within ' + str(ANSWER_SECONDS) + ' s\n' + self.error_text())
        line: str = self.process.stdout.readline()
        if line == '':
            raise ClientFailure('the fixture exited (status ' + str(self.process.poll()) + ')\n' + self.error_text())
        return line.rstrip('\n')

    def start(self) -> None:
        line: str = self.read_line()
        if not line.startswith('ready '):
            raise ClientFailure('the fixture did not start: ' + line[:200])
        self.pages = json.loads(line[len('ready '):])['pages']

    def ask(self, command: str) -> str:
        assert self.process.stdin is not None
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()
        answer: str = self.read_line()
        if answer.startswith('error '):
            raise ClientFailure('the fixture refused "' + command + '": ' + answer[len('error '):])
        return answer

    def report(self, command: str) -> dict[str, Any]:
        answer: str = self.ask(command)
        if not answer.startswith('report '):
            raise ClientFailure('"' + command + '" answered ' + answer[:200])
        result: dict[str, Any] = json.loads(answer[len('report '):])
        return result

    def error_text(self) -> str:
        self.errors.seek(0)
        text: str = self.errors.read()
        return text

    def finish(self) -> int:
        """Asks the fixture to quit; returns its exit status."""
        answer: str = self.ask('quit')
        if answer != 'bye':
            raise ClientFailure('quit answered ' + answer[:200])
        status: int = self.process.wait(timeout=30)
        return status

    def stop(self) -> None:
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=10)
        self.errors.close()


# --- comparison over AT-SPI -------------------------------------------------------


@dataclass
class Results:
    mismatches: list[str] = field(default_factory=list)
    nodes_compared: int = 0
    # Registry type to the stable identities of its instances compared.
    compared: dict[str, set[str]] = field(default_factory=dict)
    # Registry type to the instances the semantic tree does not expose.
    unexposed: dict[str, set[str]] = field(default_factory=dict)
    # Registry type to its default-action outcome.
    actions: dict[str, str] = field(default_factory=dict)


class Comparer:
    """Compares published AT-SPI subtrees with report nodes."""

    def __init__(self, atspi: ModuleType, glib: ModuleType, results: Results, progress: Progress) -> None:
        self.atspi: ModuleType = atspi
        self.glib: ModuleType = glib
        self.results: Results = results
        self.progress: Progress = progress
        self.value_text: bool = hasattr(atspi.Value, 'get_text')

    def mismatch(self, where: str, message: str) -> None:
        self.results.mismatches.append(where + ': ' + message)

    def state_names(self, obj: Any) -> set[str]:
        names: set[str] = set()
        state: Any
        for state in obj.get_state_set().get_states():
            names.add(state.value_nick.upper().replace('-', '_'))
        return names

    def compare_node(self, obj: Any, node: dict[str, Any], where: str, ids: set[str]) -> None:
        """Compares one node; `ids` holds the report's live stable identities."""
        atspi: ModuleType = self.atspi
        self.results.nodes_compared += 1
        accessible_id: str = obj.get_accessible_id() or ''
        if accessible_id != node['stable_id']:
            self.mismatch(where, 'accessible id ' + repr(accessible_id) + ', expected ' + repr(node['stable_id']))
        # Roles compare by value: at-spi2-core renamed some nicks (PUSH_BUTTON
        # reads back as BUTTON on Alpine 3.24), and the old names stay aliases.
        role: str = obj.get_role().value_nick.upper().replace('-', '_').replace(' ', '_')
        wanted_role: str = expected_role(node)
        wanted_value: Any = getattr(atspi.Role, wanted_role, None)
        same_role: bool = obj.get_role() == wanted_value if wanted_value is not None else role == wanted_role
        if not same_role:
            self.mismatch(where, 'role ' + role + ', expected ' + wanted_role + ' for ' + node['role'])
        if (obj.get_name() or '') != node['name']:
            self.mismatch(where, 'name ' + repr(obj.get_name()) + ', expected ' + repr(node['name']))
        if (obj.get_description() or '') != node['description']:
            self.mismatch(where, 'description ' + repr(obj.get_description()) + ', expected ' + repr(node['description']))
        considered: set[str] = set(DERIVED_STATE_NAMES)
        name: str
        for _, name in STATE_NAMES:
            considered.add(name)
        actual_states: set[str] = self.state_names(obj) & considered
        wanted_states: set[str] = expected_states(node)
        if actual_states != wanted_states:
            self.mismatch(where, 'states extra ' + str(sorted(actual_states - wanted_states)) +
                          ' missing ' + str(sorted(wanted_states - actual_states)))
        interfaces: set[str] = set(obj.get_interfaces())
        wanted_interfaces: set[str] = expected_interfaces(node)
        if (interfaces & {'Action', 'Value', 'Text', 'EditableText'}) != (wanted_interfaces - {'Accessible', 'Component'}):
            self.mismatch(where, 'interfaces ' + str(sorted(interfaces)) + ', expected ' + str(sorted(wanted_interfaces)))
        if 'Action' in interfaces:
            count: int = atspi.Action.get_n_actions(obj)
            names: list[str] = []
            index: int
            for index in range(count):
                names.append(atspi.Action.get_action_name(obj, index))
            if names != published_actions(node['actions']):
                self.mismatch(where, 'actions ' + str(names) + ', expected ' + str(published_actions(node['actions'])))
        if 'Value' in interfaces:
            self.compare_value(obj, node, where)
        if 'Text' in interfaces and node['role'] == 'text_box':
            text: str = atspi.Text.get_text(obj, 0, -1) or ''
            if text != node['value']:
                self.mismatch(where, 'text ' + repr(text) + ', expected ' + repr(node['value']))
            self.compare_text_selection(obj, node, where)
        attributes: dict[str, str] = obj.get_attributes() or {}
        if node['position_in_set'] is not None and node['set_size'] is not None:
            if attributes.get('posinset') != str(node['position_in_set']) or attributes.get('setsize') != str(node['set_size']):
                self.mismatch(where, 'position ' + str(attributes.get('posinset')) + ' of ' + str(attributes.get('setsize')) +
                              ', expected ' + str(node['position_in_set']) + ' of ' + str(node['set_size']))
        elif 'posinset' in attributes or 'setsize' in attributes:
            self.mismatch(where, 'position published without one in the semantic tree')
        # The stage A fields published as object attributes (attributes() in
        # the adapter): present exactly when the node has the field.
        field: str
        attribute: str
        for field, attribute in (('level', 'level'), ('row_index', 'rowindex'), ('column_index', 'colindex'),
                                 ('row_count', 'rowcount'), ('column_count', 'colcount')):
            wanted: str | None = None if node[field] is None else str(node[field])
            if attributes.get(attribute) != wanted:
                self.mismatch(where, attribute + ' ' + repr(attributes.get(attribute)) + ', expected ' + repr(wanted))
        live: str | None = 'polite' if node['role'] == 'live_region' else None
        if attributes.get('live') != live or attributes.get('container-live') != live:
            self.mismatch(where, 'live ' + repr(attributes.get('live')) + ', expected ' + repr(live))
        if 'Action' in interfaces and node['actions']:
            binding: str = atspi.Action.get_key_binding(obj, 0) or ''
            wanted_binding: str = '<Alt>' + node['access_key'].lower() + ';;' if node['access_key'] else ''
            if binding != wanted_binding:
                self.mismatch(where, 'key binding ' + repr(binding) + ', expected ' + repr(wanted_binding))
        if node['controlled_panel'] != '' and node['controlled_panel'] in ids:
            self.compare_controller(obj, node, where)

    def compare_text_selection(self, obj: Any, node: dict[str, Any], where: str) -> None:
        """The caret and the selection of a text node (A-L3), in characters,
        against text_selection in UTF-8 bytes (0 and none without one)."""
        atspi: ModuleType = self.atspi
        selection: dict[str, int] | None = node['text_selection']
        encoded: bytes = node['value'].encode('utf-8')
        wanted_caret: int = 0
        wanted_range: tuple[int, int] | None = None
        if selection is not None:
            anchor: int = len(encoded[:selection['anchor']].decode('utf-8'))
            wanted_caret = len(encoded[:selection['caret']].decode('utf-8'))
            if anchor != wanted_caret:
                wanted_range = (min(anchor, wanted_caret), max(anchor, wanted_caret))
        caret: int = atspi.Text.get_caret_offset(obj)
        if caret != wanted_caret:
            self.mismatch(where, 'caret ' + str(caret) + ', expected ' + str(wanted_caret))
        count: int = atspi.Text.get_n_selections(obj)
        published: tuple[int, int] | None = None
        if count == 1:
            span: Any = atspi.Text.get_selection(obj, 0)
            published = (span.start_offset, span.end_offset)
        if count > 1 or published != wanted_range:
            self.mismatch(where, 'selection ' + str(published) + ' of ' + str(count) + ', expected ' + str(wanted_range))

    def compare_value(self, obj: Any, node: dict[str, Any], where: str) -> None:
        atspi: ModuleType = self.atspi
        if node['numeric_value'] is not None:
            current: float = atspi.Value.get_current_value(obj)
            if current != node['numeric_value']:
                self.mismatch(where, 'value ' + repr(current) + ', expected ' + repr(node['numeric_value']))
            if node['minimum_value'] is not None and node['maximum_value'] is not None:
                minimum: float = atspi.Value.get_minimum_value(obj)
                maximum: float = atspi.Value.get_maximum_value(obj)
                if minimum != node['minimum_value'] or maximum != node['maximum_value']:
                    self.mismatch(where, 'range ' + repr(minimum) + '..' + repr(maximum) + ', expected ' +
                                  repr(node['minimum_value']) + '..' + repr(node['maximum_value']))
            # The small step is the value's increment (12.13); zero without one.
            step: float = atspi.Value.get_minimum_increment(obj)
            wanted_step: float = 0.0 if node['small_step'] is None else node['small_step']
            if step != wanted_step:
                self.mismatch(where, 'increment ' + repr(step) + ', expected ' + repr(wanted_step))
        if self.value_text:
            text: str = atspi.Value.get_text(obj) or ''
            if text != node['value']:
                self.mismatch(where, 'value text ' + repr(text) + ', expected ' + repr(node['value']))

    def compare_controller(self, obj: Any, node: dict[str, Any], where: str) -> None:
        relation: Any
        for relation in obj.get_relation_set():
            if relation.get_relation_type() != self.atspi.RelationType.CONTROLLER_FOR:
                continue
            index: int
            for index in range(relation.get_n_targets()):
                target: Any = relation.get_target(index)
                if (target.get_accessible_id() or '') == node['controlled_panel']:
                    return
        self.mismatch(where, 'no controller-for relation to ' + node['controlled_panel'])

    def compare_tree(self, frame: Any, report: dict[str, Any], label: str) -> None:
        """Compares everything under `frame` with the report's roots."""
        located: list[Located] = locate_all(report)
        ids: set[str] = set()
        item: Located
        for item in located:
            ids.add(item.node['stable_id'])
        types: dict[str, str] = control_types(report)
        pending: list[tuple[Any, dict[str, Any], str]] = []
        roots: list[dict[str, Any]] = report['roots']
        self.compare_children(frame, roots, label, pending)
        while pending:
            if self.progress.exhausted():
                raise ClientFailure('time budget of ' + str(BUDGET_SECONDS) + ' s spent while comparing ' + label +
                                    ' after ' + str(self.results.nodes_compared) + ' nodes in all')
            obj: Any
            node: dict[str, Any]
            where: str
            obj, node, where = pending.pop()
            self.compare_node(obj, node, where, ids)
            if not is_virtual(node['runtime_id']) and node['stable_id'] in types:
                kind: str = types[node['stable_id']]
                self.results.compared.setdefault(kind, set()).add(node['stable_id'])
            self.compare_children(obj, node['children'], where, pending)
        published: set[str] = ids
        record: dict[str, Any]
        for record in report['controls']:
            if record['stable_id'] in published:
                continue
            # A visible control without a node, or one whose descriptor is not
            # exposed (it would have none when shown either).
            if record['visible'] or not record['exposed']:
                self.results.unexposed.setdefault(record['type'], set()).add(record['stable_id'])

    def compare_children(self, obj: Any, children: list[dict[str, Any]], where: str,
                         pending: list[tuple[Any, dict[str, Any], str]]) -> None:
        count: int = obj.get_child_count()
        if count != len(children):
            self.mismatch(where, str(count) + ' children, expected ' + str(len(children)))
            return
        index: int
        for index in range(count - 1, -1, -1):
            child: Any = obj.get_child_at_index(index)
            label: str = where + ' > ' + children[index]['stable_id']
            if child is None:
                self.mismatch(label, 'child ' + str(index) + ' is missing')
                continue
            pending.append((child, children[index], label))


# --- the session --------------------------------------------------------------------


def find_frames(atspi: ModuleType, fixture: Fixture) -> tuple[Any, Any]:
    deadline: float = time.monotonic() + session_support.STARTUP_SECONDS
    desktop: Any = atspi.get_desktop(0)
    while time.monotonic() < deadline and fixture.process.poll() is None:
        index: int
        for index in range(desktop.get_child_count()):
            application: Any = desktop.get_child_at_index(index)
            if application is None or application.get_name() != APPLICATION_NAME:
                continue
            gallery: Any = None
            supplement: Any = None
            child: int
            for child in range(application.get_child_count()):
                frame: Any = application.get_child_at_index(child)
                if frame is not None and frame.get_name() == APPLICATION_NAME:
                    gallery = frame
                if frame is not None and frame.get_name() == SUPPLEMENT_NAME:
                    supplement = frame
            if gallery is not None and supplement is not None:
                return gallery, supplement
        time.sleep(session_support.POLL_SECONDS)
    raise ClientFailure('application "' + APPLICATION_NAME + '" with both frames on the AT-SPI desktop')


def resolve(frame: Any, path: list[int]) -> Any:
    current: Any = frame
    index: int
    for index in path:
        current = current.get_child_at_index(index)
        if current is None:
            raise ClientFailure('no published object at path ' + str(path))
    return current


@dataclass
class Target:
    """One default action to perform: on a page (or the supplement) and a node."""
    kind: str
    page: str
    stable_id: str
    opens: str = ''


def choose_targets(registry: list[dict[str, Any]], reports: dict[str, dict[str, Any]],
                   supplement: dict[str, Any]) -> tuple[list[Target], dict[str, str]]:
    """One action target per registry type, and the types without one with the reason.

    A type's target is an enabled instance with actions, preferably on the
    type's own Gallery page and outside the Gallery's frame; a type whose
    instances publish no actions has none (its reason says so).
    """
    targets: list[Target] = []
    reasons: dict[str, str] = {}
    candidates: dict[str, list[tuple[int, Target]]] = {}
    page: str
    report: dict[str, Any]
    all_reports: list[tuple[str, dict[str, Any]]] = list(reports.items())
    all_reports.append(('supplement', supplement))
    for page, report in all_reports:
        types: dict[str, str] = control_types(report)
        item: Located
        for item in locate_all(report):
            node: dict[str, Any] = item.node
            if is_virtual(node['runtime_id']) or node['stable_id'] not in types:
                continue
            if not node['actions'] or (node['states'] & ENABLED) == 0:
                continue
            kind: str = types[node['stable_id']]
            rank: int = 2
            if not in_shell(node['stable_id']):
                rank = 1
            candidates.setdefault(kind, []).append((rank, Target(kind, page, node['stable_id'])))
    by_type: dict[str, dict[str, Any]] = {}
    entry: dict[str, Any]
    for entry in registry:
        by_type[entry['id']] = entry
    kind: str
    page_name: str
    opener: str
    openers: dict[str, Target] = {}
    for kind, page_name, opener in OPENERS:
        openers[kind] = Target(kind, page_name, opener, kind)
    for entry in registry:
        kind = entry['id']
        if kind in NONVISUAL:
            continue
        if kind in openers:
            targets.append(openers[kind])
            continue
        options: list[tuple[int, Target]] = candidates.get(kind, [])
        if not options:
            reasons[kind] = 'no enabled instance publishes an action'
            continue
        best: tuple[int, Target] = options[0]
        option: tuple[int, Target]
        for option in options:
            own_page: bool = option[1].page == entry.get('gallery_page')
            best_own: bool = best[1].page == entry.get('gallery_page')
            if (option[0], not own_page) < (best[0], not best_own):
                best = option
        targets.append(best[1])
    return targets, reasons


def describe_change(before: dict[str, Any], after: dict[str, Any], target: Target) -> list[str]:
    """What a default action changed, as short phrases; empty when nothing did."""
    changes: list[str] = []
    if after['armed']['stable_id'] == target.stable_id and after['armed']['clicks'] > 0:
        changes.append('clicked ' + str(after['armed']['clicks']) + 'x')
    old_nodes: dict[tuple[int, str], dict[str, Any]] = {}
    item: Located
    for item in locate_all(before):
        old_nodes[node_key(item)] = item.node
    new_nodes: dict[tuple[int, str], dict[str, Any]] = {}
    for item in locate_all(after):
        new_nodes[node_key(item)] = item.node
    target_keys: list[tuple[int, str]] = []
    key: tuple[int, str]
    for key in new_nodes:
        if new_nodes[key]['stable_id'] == target.stable_id and key[1] == '':
            target_keys.append(key)
    for key in target_keys:
        old: dict[str, Any] | None = old_nodes.get(key)
        new: dict[str, Any] = new_nodes[key]
        if old is None:
            continue
        if old['states'] != new['states']:
            changes.append('own states ' + str(sorted(expected_states(old))) + ' -> ' + str(sorted(expected_states(new))))
        if old['value'] != new['value'] or old['numeric_value'] != new['numeric_value']:
            changes.append('own value ' + repr(old['value']) + ' -> ' + repr(new['value']))
    before_roots: list[str] = []
    root: dict[str, Any]
    for root in before['roots']:
        before_roots.append(root['stable_id'])
    after_roots: list[str] = []
    for root in after['roots']:
        after_roots.append(root['stable_id'])
    opened: list[str] = []
    for name in after_roots:
        if name not in before_roots:
            opened.append(name)
    closed: list[str] = []
    for name in before_roots:
        if name not in after_roots:
            closed.append(name)
    if opened:
        changes.append('opened ' + ', '.join(opened[:2]) + (' and ' + str(len(opened) - 2) + ' more' if len(opened) > 2 else ''))
    if closed:
        changes.append('closed ' + str(len(closed)) + ' roots')
    others: int = 0
    for key in new_nodes:
        if key in target_keys:
            continue
        old = old_nodes.get(key)
        new = new_nodes[key]
        if old is None or old['states'] != new['states'] or old['value'] != new['value'] or old['name'] != new['name']:
            others += 1
    for key in old_nodes:
        if key not in new_nodes:
            others += 1
    if others > 0:
        changes.append(str(others) + ' other nodes changed')
    mutations: int = after['metrics']['mutations'] - before['metrics']['mutations']
    if mutations > 0:
        changes.append(str(mutations) + ' retained mutations')
    return changes


def opened_types(report: dict[str, Any]) -> set[str]:
    """The registry types of the visible controls in a report, overlays included."""
    found: set[str] = set()
    record: dict[str, Any]
    for record in report['controls']:
        if record['visible']:
            found.add(record['type'])
    return found


def node_path(report: dict[str, Any], stable_id: str) -> list[int] | None:
    item: Located
    for item in locate_all(report):
        if item.node['stable_id'] == stable_id and not is_virtual(item.node['runtime_id']):
            return item.path
    return None


def perform_default(atspi: ModuleType, glib: ModuleType, comparer: Comparer, fixture: Fixture,
                    frames: tuple[Any, Any], target: Target, results: Results) -> None:
    """Performs a target's default action over AT-SPI and checks its effect."""
    supplement: bool = target.page == 'supplement'
    frame: Any = frames[1] if supplement else frames[0]
    ask: str = 'supplement' if supplement else 'page ' + target.page
    before: dict[str, Any] = fixture.report(ask)
    fixture.ask('arm ' + target.stable_id)
    if not supplement:
        before = fixture.report('report')
    path: list[int] | None = node_path(before, target.stable_id)
    where: str = target.kind + ' (' + target.stable_id + ')'
    if path is None:
        results.mismatches.append(where + ': not published on ' + target.page)
        results.actions[target.kind] = 'not published'
        return
    obj: Any = resolve(frame, path)
    if (obj.get_accessible_id() or '') != target.stable_id:
        results.mismatches.append(where + ': path ' + str(path) + ' reached ' + repr(obj.get_accessible_id()))
        return
    names: list[str] = []
    index: int
    for index in range(atspi.Action.get_n_actions(obj)):
        names.append(atspi.Action.get_action_name(obj, index))
    performed: bool = atspi.Action.do_action(obj, 0)
    after: dict[str, Any] = fixture.report('supplement' if supplement else 'report')
    comparer.compare_tree(frame, after, target.page + ' after ' + names[0] + ' on ' + target.stable_id)
    changes: list[str] = describe_change(before, after, target)
    if target.opens != '' and target.opens not in opened_types(after):
        results.mismatches.append(where + ': ' + names[0] + ' did not open a visible ' + target.opens)
    # An action is judged by its observed effect. DoAction's answer does not
    # say whether the window performed it: at-spi2-atk on ubuntu-24.04
    # answered true for five focus actions the window then refused (CI run
    # of fe636c05). A false answer is still a mismatch.
    if not performed:
        results.mismatches.append(where + ': DoAction answered false for ' + names[0] + ' (action 0)')
    known: str = ''
    known_list: str = ''
    if target.stable_id in KNOWN_REFUSALS:
        known = 'refused: ' + KNOWN_REFUSALS[target.stable_id]
        known_list = 'KNOWN_REFUSALS'
    elif target.stable_id in ACCEPTED_WITHOUT_CHANGE:
        known = 'accepted without change: ' + ACCEPTED_WITHOUT_CHANGE[target.stable_id]
        known_list = 'ACCEPTED_WITHOUT_CHANGE'
    if changes and known != '':
        results.mismatches.append(where + ': ' + names[0] + ' now changes ' + '; '.join(changes) +
                                  '; remove it from ' + known_list)
        results.actions[target.kind] = names[0] + ': changed'
    elif changes:
        results.actions[target.kind] = names[0] + ' on ' + target.stable_id + ': ' + '; '.join(changes)
    elif known != '':
        results.actions[target.kind] = names[0] + ' on ' + target.stable_id + ': no change (known, ' + known + ')'
    else:
        results.mismatches.append(where + ': ' + names[0] + ' (action 0) had no observable effect')
        results.actions[target.kind] = names[0] + ': no observable effect'
    if not supplement:
        try:
            fixture.report('reset')
        except ClientFailure as error:
            results.mismatches.append(where + ': ' + str(error))
            fixture.report('page ' + target.page)


def check_generational_identity(atspi: ModuleType, glib: ModuleType, fixture: Fixture, frame: Any,
                                results: Results) -> None:
    """A disposed button's object is refused; its same-named successor answers."""
    report: dict[str, Any] = fixture.report('page control-spectrum')
    types: dict[str, str] = control_types(report)
    chosen: Located | None = None
    item: Located
    for item in locate_all(report):
        node: dict[str, Any] = item.node
        if (types.get(node['stable_id']) == 'button' and not in_shell(node['stable_id']) and
                'press' in node['actions'] and (node['states'] & ENABLED) != 0):
            chosen = item
            break
    if chosen is None:
        results.mismatches.append('generational identity: no Button on control-spectrum')
        return
    stable_id: str = chosen.node['stable_id']
    held: Any = resolve(frame, chosen.path)
    fixture.ask('arm ' + stable_id)
    replaced: dict[str, Any] = fixture.report('replace ' + stable_id)
    outcome: str = ''
    try:
        refused: bool = not atspi.Action.do_action(held, 0)
        outcome = 'refused (false)' if refused else 'performed'
    except glib.Error as error:
        outcome = 'refused (error: ' + error.message + ')'
    try:
        states: set[str] = set()
        state: Any
        for state in held.get_state_set().get_states():
            states.add(state.value_nick.upper())
        state_outcome: str = 'defunct' if 'DEFUNCT' in states else 'states ' + str(sorted(states))
    except glib.Error as error:
        state_outcome = 'error: ' + error.message
    after: dict[str, Any] = fixture.report('report')
    if outcome == 'performed' or after['replacement_clicks'] != 0:
        results.mismatches.append('generational identity: the disposed button\'s object reached a control (' +
                                  outcome + ', replacement clicks ' + str(after['replacement_clicks']) + ')')
    if state_outcome.startswith('states'):
        results.mismatches.append('generational identity: the disposed button\'s object is not defunct: ' + state_outcome)
    path: list[int] | None = node_path(replaced, stable_id)
    if path is None:
        results.mismatches.append('generational identity: the replacement button is not published')
        return
    successor: Any = resolve(frame, path)
    pressed: bool = atspi.Action.do_action(successor, 0)
    final: dict[str, Any] = fixture.report('report')
    if not pressed or final['replacement_clicks'] != 1:
        results.mismatches.append('generational identity: the replacement button did not answer its own action')
    print('generational identity: old object ' + outcome + ', ' + state_outcome +
          '; replacement pressed ' + str(final['replacement_clicks']) + 'x')


def summarize(registry: list[dict[str, Any]], results: Results, reasons: dict[str, str]) -> None:
    """Prints one line per registry type and records unaccounted types."""
    print('registry type | instances compared over AT-SPI | default action')
    compared: int = 0
    entry: dict[str, Any]
    for entry in registry:
        kind: str = entry['id']
        count: int = len(results.compared.get(kind, set()))
        action: str = results.actions.get(kind, reasons.get(kind, ''))
        if kind in NONVISUAL:
            status: str = 'nonvisual component (no node of its own)'
        elif kind in BASE_ANATOMY and count == 0:
            status = 'published under its base anatomy (own anatomy pending)'
        elif count > 0:
            status = str(count)
            compared += 1
        elif kind in results.unexposed:
            status = 'not exposed by the semantic tree (' + str(len(results.unexposed[kind])) + ' instances)'
        else:
            status = 'NOT FOUND'
            results.mismatches.append(kind + ': no instance was found on any page')
        print(kind + ' | ' + status + ' | ' + action)
    print('linux accessibility gallery: ' + str(len(registry)) + ' registry types; ' + str(compared) +
          ' compared over AT-SPI; ' + str(len(NONVISUAL)) + ' nonvisual; ' + str(results.nodes_compared) +
          ' nodes compared; ' + str(len(results.actions)) + ' default actions; ' +
          str(len(results.mismatches)) + ' mismatches')


def run_client(fixture_path: str, registry_path: str) -> int:
    import gi
    gi.require_version('Atspi', '2.0')
    gi.require_version('Gio', '2.0')
    from gi.repository import Gio, GLib
    registry: list[dict[str, Any]] = json.loads(Path(registry_path).read_text(encoding='utf-8'))['controls']
    session: session_support.Session = session_support.Session()
    fixture: Fixture | None = None
    results: Results = Results()
    progress: Progress = Progress()
    faulthandler.dump_traceback_later(WATCHDOG_SECONDS, exit=True)
    try:
        progress.note('starting the AT-SPI bus')
        session.start(Gio, GLib)
        from gi.repository import Atspi
        Atspi.set_timeout(CALL_TIMEOUT_MS, STARTUP_TIMEOUT_MS)
        fixture = Fixture(fixture_path)
        fixture.start()
        progress.note('fixture ready; finding its frames')
        frames: tuple[Any, Any] = find_frames(Atspi, fixture)
        session_support.require_registry(Gio, GLib, session.address)
        comparer: Comparer = Comparer(Atspi, GLib, results, progress)
        progress.note('fixture published; ' + str(len(fixture.pages)) + ' Gallery pages')
        probe_latency(frames[0], progress)
        reports: dict[str, dict[str, Any]] = {}
        page: str
        for page in fixture.pages:
            if progress.exhausted():
                results.mismatches.append('time budget of ' + str(BUDGET_SECONDS) + ' s spent before page ' + page)
                break
            before_nodes: int = results.nodes_compared
            report: dict[str, Any] = fixture.report('page ' + page)
            reports[page] = report
            comparer.compare_tree(frames[0], report, page)
            progress.note('page ' + page + ': ' + str(results.nodes_compared - before_nodes) + ' nodes compared')
        supplement: dict[str, Any] = fixture.report('supplement')
        comparer.compare_tree(frames[1], supplement, 'supplement')
        targets: list[Target]
        reasons: dict[str, str]
        targets, reasons = choose_targets(registry, reports, supplement)
        progress.note(str(len(targets)) + ' default-action targets')
        target: Target
        for target in targets:
            if progress.exhausted():
                results.mismatches.append('time budget of ' + str(BUDGET_SECONDS) + ' s spent before the action on ' +
                                          target.stable_id)
                break
            perform_default(Atspi, GLib, comparer, fixture, frames, target, results)
            progress.note(target.kind + ': ' + results.actions.get(target.kind, 'no outcome recorded'))
        check_generational_identity(Atspi, GLib, fixture, frames[0], results)
        summarize(registry, results, reasons)
        progress.note('done')
        status: int = fixture.finish()
        errors: str = fixture.error_text()
        if status != 0:
            results.mismatches.append('fixture exit status ' + str(status) + '\n' + errors)
        if 'CRITICAL' in errors or 'WARNING' in errors:
            results.mismatches.append('fixture printed GLib warnings\n' + errors)
        if results.mismatches:
            print('linux accessibility gallery: FAILED', file=sys.stderr)
            message: str
            for message in results.mismatches:
                print('  ' + message, file=sys.stderr)
            return 1
        print('linux accessibility gallery: every node, type and default action matched')
        return 0
    except (session_support.Mismatch, ClientFailure) as error:
        print('linux accessibility gallery: FAILED: ' + str(error), file=sys.stderr)
        return 1
    except Exception:
        print('linux accessibility gallery: FAILED with an exception', file=sys.stderr)
        traceback.print_exc()
        if fixture is not None:
            print(fixture.error_text(), file=sys.stderr)
        return 1
    finally:
        if fixture is not None:
            fixture.stop()
        session.stop()


def main(arguments: list[str]) -> int:
    if len(arguments) == 2 and arguments[0] == '--unsupported':
        return session_support.unsupported(arguments[1])
    if len(arguments) != 2:
        print('usage: linux_accessibility_gallery_client.py FIXTURE REGISTRY | --unsupported REASON', file=sys.stderr)
        return 2
    if os.environ.get(session_support.SESSION_MARKER) == '1':
        return run_client(arguments[0], arguments[1])
    missing: str | None = session_support.missing_prerequisite()
    if missing is not None:
        return session_support.unsupported(missing)
    return session_support.run_in_session(arguments, str(Path(__file__).resolve()))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
