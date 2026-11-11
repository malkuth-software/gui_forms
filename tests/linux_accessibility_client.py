"""A real AT-SPI client of the Linux accessibility fixture, over D-Bus.

Registered with ctest as ``gui_forms_linux_accessibility_tests`` (stage A,
A-L1). Run by the system Python, which has PyGObject and the Atspi 2.0
typelib (Ubuntu: python3-gi, gir1.2-atspi-2.0):

    linux_accessibility_client.py FIXTURE
    linux_accessibility_client.py --unsupported REASON

The first form re-executes itself inside a private session bus
(``dbus-run-session``), starts the AT-SPI bus launcher there, waits until the
launcher owns ``org.a11y.Bus``, then starts FIXTURE and drives it as an
assistive client would: it finds the fixture's application under the AT-SPI
registry's desktop and uses the Action, Value, EditableText and Text
interfaces (A-L3: caret, selection, strings by granularity, character boxes
and hit testing, on the A-T1 text core).
The second form is registered when the build found no ATK or X11.

Results. Exit 0 when every check passes; 1 on a mismatch or a failure.
A missing prerequisite (no display, no ``dbus-run-session``, no AT-SPI bus
launcher, no PyGObject or Atspi typelib, no ATK at build time) fails the
test on CI (the ``CI`` environment variable is ``true``, as GitHub sets it,
or ``GUI_FORMS_REQUIRE_ACCESSIBILITY`` is 1); elsewhere it prints
``unsupported`` and exits 77, which ctest reports as skipped.

Standard library and PyGObject only; Python 3.10+.
"""

from __future__ import annotations

from collections.abc import Callable
from functools import partial
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import traceback
from types import ModuleType
from typing import IO

SKIP_EXIT: int = 77
APPLICATION_NAME: str = 'Plan accessibility fixture'
SESSION_MARKER: str = 'GUI_FORMS_ATSPI_SESSION'
# Seconds. The launcher and the fixture each get this long to appear.
STARTUP_SECONDS: float = 15.0
POLL_SECONDS: float = 0.1
# Seconds a published change (the fixture's next update) may take to reach
# the client; a named failure after that.
PUBLISH_SECONDS: float = 10.0
LAUNCHER_NAME: str = 'at-spi-bus-launcher'
# Where distributions install the launcher outside PATH: Debian and Ubuntu
# (/usr/libexec), older Debian (/usr/lib/at-spi2-core) and multiarch layouts.
LAUNCHER_DIRECTORIES: tuple[str, ...] = (
    '/usr/libexec',
    '/usr/lib/at-spi2-core',
    '/usr/lib/x86_64-linux-gnu/at-spi2-core',
    '/usr/lib/aarch64-linux-gnu/at-spi2-core',
)


class Mismatch(Exception):
    """What the AT-SPI client saw differs from what the fixture published."""


def check(condition: bool, message: str) -> None:
    if not condition:
        raise Mismatch(message)


def accessibility_required() -> bool:
    ci: str = os.environ.get('CI', '').strip().lower()
    required: str = os.environ.get('GUI_FORMS_REQUIRE_ACCESSIBILITY', '').strip()
    result: bool = ci == 'true' or required == '1'
    return result


def unsupported(reason: str) -> int:
    """Fails on CI; elsewhere reports the platform as unsupported."""
    if accessibility_required():
        print('linux accessibility: prerequisite missing on CI: ' + reason, file=sys.stderr)
        return 1
    print('linux accessibility: unsupported: ' + reason)
    return SKIP_EXIT


def find_launcher() -> str | None:
    found: str | None = shutil.which(LAUNCHER_NAME)
    if found is not None:
        return found
    directory: str
    for directory in LAUNCHER_DIRECTORIES:
        candidate: Path = Path(directory) / LAUNCHER_NAME
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def missing_prerequisite() -> str | None:
    """The first missing prerequisite, or None when all are present."""
    if not os.environ.get('DISPLAY'):
        return 'no X display (DISPLAY is unset); run under Xvfb'
    if shutil.which('dbus-run-session') is None:
        return 'dbus-run-session is not installed (Ubuntu: dbus)'
    if find_launcher() is None:
        return 'the AT-SPI bus launcher is not installed (Ubuntu: at-spi2-core)'
    try:
        import gi
        gi.require_version('Atspi', '2.0')
        gi.require_version('Gio', '2.0')
    except (ImportError, ValueError) as error:
        return ('PyGObject or the Atspi 2.0 typelib is missing '
                '(Ubuntu: python3-gi, gir1.2-atspi-2.0): ' + str(error))
    return None


def run_in_session(arguments: list[str], script: str | None = None) -> int:
    """Re-executes `script` (by default this one) inside a private session bus.

    Returns its exit status.
    """
    if script is None:
        script = str(Path(__file__).resolve())
    environment: dict[str, str] = dict(os.environ)
    environment[SESSION_MARKER] = '1'
    # The fixture and the client must find the bus this script starts, not
    # an address inherited from a desktop session or a previous launcher.
    environment.pop('AT_SPI_BUS_ADDRESS', None)
    command: list[str] = ['dbus-run-session', '--', sys.executable, script]
    command.extend(arguments)
    completed: subprocess.CompletedProcess[bytes] = subprocess.run(command, env=environment, check=False)
    return completed.returncode


class Session:
    """The AT-SPI bus launcher this script owns inside its session bus.

    start() launches it and waits until it owns org.a11y.Bus; stop()
    terminates it. The accessibility bus address it reports is exported as
    AT_SPI_BUS_ADDRESS, so the fixture and this client use that bus.
    """

    def __init__(self) -> None:
        self.launcher: subprocess.Popen[bytes] | None = None
        self.log: IO[bytes] = tempfile.TemporaryFile()
        self.address: str = ''

    def start(self, gio: ModuleType, glib: ModuleType) -> None:
        launcher: str | None = find_launcher()
        check(launcher is not None, 'AT-SPI bus launcher present')
        self.launcher = subprocess.Popen([str(launcher), '--launch-immediately'],
                                         stdout=self.log, stderr=subprocess.STDOUT)
        connection: object = gio.bus_get_sync(gio.BusType.SESSION, None)
        deadline: float = time.monotonic() + STARTUP_SECONDS
        while time.monotonic() < deadline and self.launcher.poll() is None:
            try:
                # NO_AUTO_START: only the launcher started here may answer.
                reply: object = connection.call_sync(
                    'org.a11y.Bus', '/org/a11y/bus', 'org.a11y.Bus', 'GetAddress', None,
                    glib.VariantType.new('(s)'), gio.DBusCallFlags.NO_AUTO_START, 1000, None)
                self.address = reply.unpack()[0]
            except glib.Error:
                self.address = ''
            if self.address != '':
                break
            time.sleep(POLL_SECONDS)
        check(self.address != '',
              'the AT-SPI bus launcher owns org.a11y.Bus and reports an address\n' + self.launcher_output())
        os.environ['AT_SPI_BUS_ADDRESS'] = self.address
        # This client is the assistive client: it says so, as a screen
        # reader does, so the fixture keeps its tree current (the host
        # skips its updates while org.a11y.Status says no one listens).
        connection.call_sync(
            'org.a11y.Bus', '/org/a11y/bus', 'org.freedesktop.DBus.Properties', 'Set',
            glib.Variant('(ssv)', ('org.a11y.Status', 'IsEnabled', glib.Variant('b', True))),
            None, gio.DBusCallFlags.NO_AUTO_START, 1000, None)

    def launcher_output(self) -> str:
        self.log.seek(0)
        text: str = self.log.read().decode('utf-8', 'replace')
        return text

    def stop(self) -> None:
        if self.launcher is not None and self.launcher.poll() is None:
            self.launcher.terminate()
            try:
                self.launcher.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.launcher.kill()
                self.launcher.wait(timeout=5)
        self.log.close()


def require_registry(gio: ModuleType, glib: ModuleType, address: str) -> None:
    """The AT-SPI registry daemon owns its name on the accessibility bus."""
    flags: int = gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
    bus: object = gio.DBusConnection.new_for_address_sync(address, flags, None, None)
    reply: object = bus.call_sync(
        'org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'NameHasOwner',
        glib.Variant('(s)', ('org.a11y.atspi.Registry',)), glib.VariantType.new('(b)'),
        gio.DBusCallFlags.NONE, 1000, None)
    owned: bool = reply.unpack()[0]
    bus.close_sync(None)
    check(owned, 'the AT-SPI registry (at-spi2-registryd) owns org.a11y.atspi.Registry on the accessibility bus')


def find_application(atspi: ModuleType, fixture: subprocess.Popen[str]) -> object:
    deadline: float = time.monotonic() + STARTUP_SECONDS
    while time.monotonic() < deadline and fixture.poll() is None:
        desktop: object = atspi.get_desktop(0)
        count: int = desktop.get_child_count()
        index: int
        for index in range(count):
            candidate: object | None = desktop.get_child_at_index(index)
            if candidate is not None and candidate.get_name() == APPLICATION_NAME:
                return candidate
        time.sleep(POLL_SECONDS)
    raise Mismatch('application "' + APPLICATION_NAME + '" registered on the AT-SPI desktop')


def collect_named(node: object, named: dict[str, object]) -> None:
    """Records every descendant of node, node included, by accessible name."""
    pending: list[object] = [node]
    while pending:
        current: object = pending.pop()
        named[current.get_name()] = current
        count: int = current.get_child_count()
        index: int
        for index in range(count):
            child: object | None = current.get_child_at_index(index)
            if child is not None:
                pending.append(child)


def action_index(atspi: ModuleType, node: object, name: str) -> int:
    """The index of the action called name, or -1."""
    count: int = atspi.Action.get_n_actions(node)
    index: int
    for index in range(count):
        if atspi.Action.get_action_name(node, index) == name:
            return index
    return -1


def wait_for(read: Callable[[], object], wanted: object, message: str) -> None:
    """Reads until read() returns wanted, for at most PUBLISH_SECONDS."""
    deadline: float = time.monotonic() + PUBLISH_SECONDS
    seen: object = read()
    while seen != wanted and time.monotonic() < deadline:
        time.sleep(POLL_SECONDS)
        seen = read()
    check(seen == wanted, message + ': wanted ' + repr(wanted) + ', got ' + repr(seen))


def exercise_text(atspi: ModuleType, text: object) -> None:
    """The Text interface on the A-T1 core (A-L3), after the edit to
    'مرحبا ABC': nine characters, the second word at 6..9."""
    wait_for(partial(atspi.Text.get_character_count, text), 9, 'the edit is published')
    check(atspi.Text.get_text(text, 0, -1) == 'مرحبا ABC', 'the text reads back')
    check(atspi.Text.set_caret_offset(text, 6), 'the caret moves to character 6')
    check(atspi.Text.get_caret_offset(text) == 6, 'the caret reads back at 6')
    check(atspi.Text.set_selection(text, 0, 6, 9), 'characters 6..9 are selected')
    check(atspi.Text.get_n_selections(text) == 1, 'one selection')
    selected: object = atspi.Text.get_selection(text, 0)
    check(selected.start_offset == 6 and selected.end_offset == 9,
          'the selection reads back as 6..9, got ' + str(selected.start_offset) + '..' + str(selected.end_offset))
    check(atspi.Text.remove_selection(text, 0), 'the selection is removed')
    check(atspi.Text.get_n_selections(text) == 0, 'no selection after removing it')
    word: object = atspi.Text.get_string_at_offset(text, 7, atspi.TextGranularity.WORD)
    check(word.content == 'ABC' and word.start_offset == 6 and word.end_offset == 9,
          'the word at 7 is ABC at 6..9, got ' + repr(word.content))
    first: object = atspi.Text.get_string_at_offset(text, 0, atspi.TextGranularity.WORD)
    check(first.content == 'مرحبا ' and first.start_offset == 0 and first.end_offset == 6,
          'the first word runs to the next word start, got ' + repr(first.content))
    line: object = atspi.Text.get_string_at_offset(text, 3, atspi.TextGranularity.LINE)
    check(line.content == 'مرحبا ABC' and line.start_offset == 0 and line.end_offset == 9,
          'a single-line box is one line, got ' + repr(line.content))
    sentence: object = atspi.Text.get_string_at_offset(text, 3, atspi.TextGranularity.SENTENCE)
    check(sentence.content == '', 'sentences are unsupported, got ' + repr(sentence.content))
    box: object = atspi.Text.get_character_extents(text, 6, atspi.CoordType.WINDOW)
    check(box.width > 0 and box.height > 0, 'character 6 has a box, got ' + str(box.width) + 'x' + str(box.height))
    hit: int = atspi.Text.get_offset_at_point(text, box.x + box.width // 2, box.y + box.height // 2,
                                              atspi.CoordType.WINDOW)
    check(hit == 6, 'the middle of character 6 hits character 6, got ' + str(hit))
    # 'e', a combining acute accent and 'x': two graphemes, three characters.
    check(atspi.EditableText.set_text_contents(text, 'e\u0301x'), 'a combining sequence is typed')
    wait_for(partial(atspi.Text.get_character_count, text), 3, 'the combining edit is published')
    check(not atspi.Text.set_caret_offset(text, 1), 'a caret inside a combining sequence is refused')
    # The last request: the fixture finishes once the caret is after the
    # accent (byte 3), so nothing is read after it.
    check(atspi.Text.set_caret_offset(text, 2), 'a caret after the combining sequence is taken')


def exercise_fixture(atspi: ModuleType, fixture: subprocess.Popen[str]) -> None:
    application: object = find_application(atspi, fixture)
    named: dict[str, object] = {}
    collect_named(application, named)
    check('Draw mark' in named, 'button "Draw mark" published')
    check('Canvas X' in named, 'numeric field "Canvas X" published')
    check('Artwork text' in named, 'text box "Artwork text" published')

    button: object = named['Draw mark']
    check(button.get_role() == atspi.Role.PUSH_BUTTON, 'button role is push button, got ' + button.get_role_name())
    press: int = action_index(atspi, button, 'press')
    check(press >= 0, 'button publishes the press action')
    check(atspi.Action.do_action(button, press), 'button press action succeeds')

    number: object = named['Canvas X']
    check(number.get_role() == atspi.Role.SPIN_BUTTON,
          'numeric field role is spin button, got ' + number.get_role_name())
    minimum: float = atspi.Value.get_minimum_value(number)
    maximum: float = atspi.Value.get_maximum_value(number)
    check(minimum == 0.0 and maximum == 200.0,
          'numeric range exported as 0..200, got ' + str(minimum) + '..' + str(maximum))
    check(atspi.Value.set_current_value(number, 37.0), 'numeric value set succeeds')

    text: object = named['Artwork text']
    check(text.get_role() == atspi.Role.ENTRY, 'text box role is entry, got ' + text.get_role_name())
    check(atspi.EditableText.set_text_contents(text, 'مرحبا ABC'), 'Unicode text edit succeeds')
    exercise_text(atspi, text)


def stop_fixture(fixture: subprocess.Popen[str]) -> None:
    if fixture.poll() is None:
        fixture.terminate()
        try:
            fixture.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            fixture.kill()
            fixture.communicate(timeout=10)


def run_client(fixture_path: str) -> int:
    import gi
    gi.require_version('Atspi', '2.0')
    gi.require_version('Gio', '2.0')
    from gi.repository import Gio, GLib
    session: Session = Session()
    fixture: subprocess.Popen[str] | None = None
    try:
        session.start(Gio, GLib)
        # Atspi reads AT_SPI_BUS_ADDRESS when it first connects, so it is
        # imported only after the session exported it.
        from gi.repository import Atspi
        fixture = subprocess.Popen([fixture_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        exercise_fixture(Atspi, fixture)
        require_registry(Gio, GLib, session.address)
        stdout: str
        stderr: str
        stdout, stderr = fixture.communicate(timeout=30)
        print(stdout, end='')
        check(fixture.returncode == 0,
              'fixture observed every client action (exit ' + str(fixture.returncode) + ')\n' + stderr)
        check('CRITICAL' not in stderr and 'WARNING' not in stderr, 'fixture printed no GLib warnings\n' + stderr)
        print('linux accessibility: AT-SPI client checks passed')
        return 0
    except Mismatch as error:
        print('linux accessibility: FAILED: ' + str(error), file=sys.stderr)
        return 1
    except Exception:
        # A D-Bus error (GLib.Error) or a client defect: a failure, with its trace.
        print('linux accessibility: FAILED with an exception', file=sys.stderr)
        traceback.print_exc()
        return 1
    finally:
        if fixture is not None:
            stop_fixture(fixture)
        session.stop()


def main(arguments: list[str]) -> int:
    if len(arguments) == 2 and arguments[0] == '--unsupported':
        return unsupported(arguments[1])
    if len(arguments) != 1:
        print('usage: linux_accessibility_client.py FIXTURE | --unsupported REASON', file=sys.stderr)
        return 2
    if os.environ.get(SESSION_MARKER) == '1':
        return run_client(arguments[0])
    missing: str | None = missing_prerequisite()
    if missing is not None:
        return unsupported(missing)
    return run_in_session(arguments)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
