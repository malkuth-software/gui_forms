"""Drives the menu probe (gui_forms_menu_probe_windows) and captures its menus.

The probe is the Gallery under Application::run, so its menus open in warm
Popup windows. This script starts it with the automation channel and a fixed
device scale, opens the commands-and-menus page's File menu, its Export
submenu and the context menu through WM_COPYDATA automation commands, and
asks the probe for a composite capture (the owner and its visible Popups in
their desktop places over a checkerboard) at each step.

Run it on a private desktop, never the owner's:

    python -B D:/malkuth/projects/tools/run_on_desktop.py -- \
        python -B tools/menu_probe.py <probe.exe> <scale> <output prefix>

It writes <prefix>_closed.png, <prefix>_file.png, <prefix>_submenu.png and
<prefix>_context.png and prints one line a step: the step, whether the probe
accepted it, and the captured windows' client rectangles.
"""

import ctypes
import json
import os
import queue
import subprocess
import sys
import threading
import time
from ctypes import wintypes

WM_COPYDATA = 0x004A
AUTOMATION_TAG = 0x47464131
WINDOW_CLASS = 'GUIForms.Window.v1'
PROBE_TITLE = 'GUI.Forms menu probe'
MENU = 'gallery.commands.menu'
KEY_DOWN = 81
KEY_RIGHT = 79
KEY_ESCAPE = 41


class COPYDATASTRUCT(ctypes.Structure):
    _fields_ = [('dwData', ctypes.c_size_t), ('cbData', wintypes.DWORD), ('lpData', ctypes.c_void_p)]


user32 = ctypes.WinDLL('user32', use_last_error=True)
EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [EnumWindowsProc, wintypes.LPARAM]
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = ctypes.c_ssize_t


def find_probe_window(process_id):
    found = []

    def visit(window, _):
        name = ctypes.create_unicode_buffer(128)
        user32.GetClassNameW(window, name, 128)
        if name.value != WINDOW_CLASS or not user32.IsWindowVisible(window):
            return True
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        title = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(window, title, 256)
        if owner.value == process_id and title.value == PROBE_TITLE:
            found.append(window)
            return False
        return True

    user32.EnumWindows(EnumWindowsProc(visit), 0)
    return found[0] if found else None


def send(window, command):
    payload = ('GUI.Forms.Automation/1 ' + command).encode('utf-8')
    buffer = ctypes.create_string_buffer(payload, len(payload))
    data = COPYDATASTRUCT(AUTOMATION_TAG, len(payload), ctypes.cast(buffer, ctypes.c_void_p))
    result = user32.SendMessageW(window, WM_COPYDATA, 0, ctypes.addressof(data))
    return result != 0


def read_lines(stream, lines):
    for raw in iter(stream.readline, b''):
        lines.put(raw.decode('utf-8', 'replace').rstrip())


def composite_line(lines):
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        try:
            line = lines.get(timeout=0.2)
        except queue.Empty:
            continue
        if line.startswith('{"automation":"capture-composite"'):
            return json.loads(line)
    return None


def main():
    if len(sys.argv) != 4:
        print('usage: menu_probe.py <probe.exe> <scale> <output prefix>', file=sys.stderr)
        return 2
    probe, scale, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
    environment = dict(os.environ)
    environment['GUI_FORMS_WINDOWS_AUTOMATION'] = '1'
    environment['GUI_FORMS_WINDOWS_SCALE'] = scale
    process = subprocess.Popen([probe], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment)
    lines = queue.Queue()
    threading.Thread(target=read_lines, args=(process.stdout, lines), daemon=True).start()
    window = None
    deadline = time.monotonic() + 30.0
    while window is None and time.monotonic() < deadline and process.poll() is None:
        time.sleep(0.2)
        window = find_probe_window(process.pid)
    if window is None:
        print('probe window not found; exit', process.poll())
        process.kill()
        return 1
    time.sleep(1.0)
    failures = 0

    def step(name, commands):
        nonlocal failures
        accepted = all(send(window, command) for command in commands)
        time.sleep(0.6)
        path = os.path.abspath(prefix + '_' + name + '.png')
        saved = send(window, 'capture-composite ' + path)
        report = composite_line(lines)
        windows = [] if report is None else [w['client'] for w in report['windows']]
        ok = accepted and saved
        failures += 0 if ok else 1
        print(name, 'ok' if ok else 'FAILED', windows)

    step('closed', [])
    step('file', ['click-at %s 18 10' % MENU])
    step('submenu', ['key %s %d down' % (MENU, KEY_DOWN), 'key %s %d down' % (MENU, KEY_DOWN),
                     'key %s %d down' % (MENU, KEY_DOWN), 'key %s %d down' % (MENU, KEY_RIGHT)])
    step('context', ['key %s %d down' % (MENU, KEY_ESCAPE), 'key %s %d down' % (MENU, KEY_ESCAPE),
                     'activate gallery.commands.context.open'])
    send(window, 'close')
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
