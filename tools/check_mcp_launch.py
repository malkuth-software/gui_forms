#!/usr/bin/env python3
"""Check that GUI.Forms programs serve MCP when launched with --mcp.

    python3 tools/check_mcp_launch.py <program> [<program> ...] [--timeout 60]

Each program is started with --mcp in its own directory. The check sends
initialize and expects an answer naming the server, then sends tools/list and
expects at least one tool. It then closes the program's standard input, which
is the automation's portable shutdown (every window is asked to close), and
expects the program to exit. Prints one line per program and exits 1 when
any program fails.

A program run through Application::run or through its host's single-window
entry (host::run_macos, run_windows, run_linux) serves MCP in every build
with GUI_FORMS_MCP on. Until 1.0.1 the demonstrators called their host
directly and lost --mcp without a word. tools/smoke_package.py runs this
check on every packaged program, and examples/reference runs it on the
installed examples that open a window. Both need a window server: on Linux
CI they run under xvfb-run.
"""
from __future__ import annotations

import argparse
import os
import json
from pathlib import Path
import subprocess
import sys
import threading
import time

PROTOCOL_VERSION: str = '2025-06-18'


class Exchange:
    """One program over stdio: requests out, replies collected by id on a thread."""

    def __init__(self, program: Path, environment: dict[str, str] | None) -> None:
        self.process = subprocess.Popen([str(program), '--mcp'], cwd=str(program.parent),
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, env=environment)
        self.replies: dict[int, dict] = {}
        self.arrived = threading.Condition()
        self.errors: list[str] = []
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._read_errors, daemon=True).start()

    def _read(self) -> None:
        for raw in iter(self.process.stdout.readline, b''):
            try:
                message = json.loads(raw.decode('utf-8'))
            except ValueError:
                continue
            if isinstance(message, dict) and 'id' in message:
                with self.arrived:
                    self.replies[message['id']] = message
                    self.arrived.notify_all()

    def _read_errors(self) -> None:
        for raw in iter(self.process.stderr.readline, b''):
            self.errors.append(raw.decode('utf-8', 'replace').rstrip())
            del self.errors[:-5]

    def send(self, message: dict) -> bool:
        try:
            self.process.stdin.write((json.dumps(message) + '\n').encode('utf-8'))
            self.process.stdin.flush()
            return True
        except OSError:
            return False

    def request(self, identity: int, method: str, params: dict, timeout: float) -> dict | None:
        if not self.send({'jsonrpc': '2.0', 'id': identity, 'method': method, 'params': params}):
            return None
        with self.arrived:
            self.arrived.wait_for(lambda: identity in self.replies, timeout=timeout)
            return self.replies.get(identity)

    def close(self, timeout: float) -> int | None:
        try:
            self.process.stdin.close()
        except OSError:
            pass
        try:
            return self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            return None


def check(program: Path, timeout: float, environment: dict[str, str] | None = None,
          require_library_allocator: bool = False) -> tuple[bool, str]:
    """(passed, detail) for one program; a failure ends with its last lines of standard error.

    `environment` is the program's whole environment (None inherits this one's).
    With `require_library_allocator`, the program also runs with
    GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR=1, so one whose operator new is not
    STXlib's allocator (gui_forms_use_library_allocator) exits at startup and
    fails the check.
    """
    if not program.is_file():
        return False, 'no such program'
    if require_library_allocator:
        environment = dict(os.environ if environment is None else environment)
        environment['GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR'] = '1'
    exchange = Exchange(program, environment)
    passed, detail = served(exchange, timeout)
    if not passed and exchange.errors:
        detail += '; stderr: ' + ' | '.join(exchange.errors)
    return passed, detail


def served(exchange: Exchange, timeout: float) -> tuple[bool, str]:
    try:
        reply = exchange.request(1, 'initialize', {
            'protocolVersion': PROTOCOL_VERSION, 'capabilities': {},
            'clientInfo': {'name': 'gui-forms-check-mcp-launch', 'version': '1'}}, timeout)
        server = ((reply or {}).get('result') or {}).get('serverInfo') or {}
        if not server.get('name'):
            if reply is not None:
                return False, 'initialize answered ' + json.dumps(reply)[:200]
            # Whether the program ended or kept running without answering:
            # the last lines of standard error do not always say.
            status: int | None = exchange.process.poll()
            if status is None:
                return False, f'no answer to initialize within {timeout:g} s; still running'
            return False, f'no answer to initialize; exited with status {status}'
        exchange.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
        # The window's tools appear when it joins the automation, once its
        # host is ready, which can come after the answer to initialize: the
        # list is asked again until it has tools or the time is up.
        tools: list = []
        identity: int = 2
        deadline: float = time.monotonic() + timeout
        while True:
            listed = exchange.request(identity, 'tools/list', {}, max(deadline - time.monotonic(), 0.1))
            tools = ((listed or {}).get('result') or {}).get('tools') or []
            identity += 1
            if tools or listed is None or time.monotonic() >= deadline:
                break
            time.sleep(0.25)
        if not tools:
            return False, f'server {server["name"]} listed no tools'
        detail = f'server {server["name"]}, {len(tools)} tools'
    finally:
        code = exchange.close(timeout)
    if code is None:
        return False, detail + '; did not exit after its input closed'
    return True, detail + f'; exited {code} after its input closed'


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('programs', nargs='+', type=Path)
    parser.add_argument('--timeout', type=float, default=60.0,
                        help='seconds for each answer and for the exit (default 60)')
    parser.add_argument('--require-library-allocator', action='store_true',
                        help="also require each program's operator new to be STXlib's allocator "
                             '(gui_forms_use_library_allocator)')
    arguments = parser.parse_args(argv)
    failed = 0
    for program in arguments.programs:
        passed, detail = check(program.resolve(), arguments.timeout,
                               require_library_allocator=arguments.require_library_allocator)
        print(f'{"ok" if passed else "FAIL"}: {program.name}: {detail}')
        failed += 0 if passed else 1
    if failed:
        print(f'check_mcp_launch: {failed} of {len(arguments.programs)} programs do not serve --mcp')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
