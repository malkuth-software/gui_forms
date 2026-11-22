"""Drives the menu probe (gui_forms_menu_probe_windows) and captures its menus.

The probe is the Gallery under Application::run, so its menus open in warm
Popup windows. This script writes an automation script (the library's MCP
automation, run in process with --mcp-script) that opens the
commands-and-menus page's File menu, its Export submenu and the context
menu, capturing the owner at each step, and starts the probe on it with a
fixed device scale.

Run it on a private desktop, never the owner's:

    python -B D:/malkuth/projects/tools/run_on_desktop.py -- \
        python -B tools/menu_probe.py <probe.exe> <scale> <output prefix>

It writes <prefix>_closed.png, <prefix>_file.png, <prefix>_submenu.png and
<prefix>_context.png and prints one line a step: the step and whether it
passed. The captures are the owner window drawn fresh (capture mode
"rendered", overlays included) with its open Popup windows composited over
it in slot order, as the retired channel's capture-composite did.
"""

import json
import os
import subprocess
import sys

MENU = 'gallery.commands.menu'
CONTEXT_OPEN = 'gallery.commands.context.open'
STEPS = ['closed', 'file', 'submenu', 'context']


def script_lines():
    """The script's steps: one JSON object a line, the batch steps of study 15."""
    return [
        {'tool': 'wait', 'until': 'idle', 'timeout_ms': 10000},
        {'tool': 'capture'},
        {'tool': 'pointer', 'action': 'click', 'target': MENU, 'offset': {'x': 18, 'y': 10}},
        {'tool': 'capture'},
        {'tool': 'keyboard', 'keys': ['Down', 'Down', 'Right', 'Down']},
        {'tool': 'capture'},
        {'tool': 'keyboard', 'keys': ['Escape', 'Escape', 'Escape']},
        {'tool': 'act', 'action': 'press', 'target': CONTEXT_OPEN},
        {'tool': 'keyboard', 'keys': ['Down']},
        {'tool': 'capture'},
    ]


def write_script(path):
    with open(path, 'w', encoding='utf-8') as script:
        for line in script_lines():
            script.write(json.dumps(line) + '\n')


def main():
    if len(sys.argv) != 4:
        print('usage: menu_probe.py <probe.exe> <scale> <output prefix>', file=sys.stderr)
        return 2
    probe, scale, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
    script = os.path.abspath(prefix + '_menu_probe.jsonl')
    write_script(script)
    environment = dict(os.environ)
    environment['GUI_FORMS_WINDOWS_SCALE'] = scale
    try:
        completed = subprocess.run([probe, '--mcp-script', script], capture_output=True, text=True,
                                   env=environment, timeout=120)
    except subprocess.TimeoutExpired:
        print('the probe did not finish its script')
        return 1
    failures = 0
    for index, name in enumerate(STEPS, start=1):
        image = script + '.%d.png' % index
        target = os.path.abspath(prefix + '_' + name + '.png')
        ok = os.path.exists(image)
        if ok:
            os.replace(image, target)
        failures += 0 if ok else 1
        print(name, 'ok' if ok else 'FAILED')
    if completed.returncode != 0 or failures:
        sys.stderr.write(completed.stdout + completed.stderr)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
