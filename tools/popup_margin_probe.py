"""Checks the Popup and Splash windows of the menu probe: size and hit margin.

The probe (gui_forms_menu_probe_windows) is the Gallery under
Application::run. This script opens the commands-and-menus page's File menu
and the context button's tooltip with an automation script (the library's
MCP automation, run in process with --mcp-script; its waits hold each state
for a few seconds), and meanwhile reads every visible window of the probe
other than its main window:

- its size in logical units (window pixels over the scale), which must be the
  face and at most the overlay shadow's reach round it, never a canvas;
- WM_NCHITTEST two pixels inside its top-left corner, in the shadow, which
  must be HTTRANSPARENT (the point reaches the window beneath), and for a
  menu window at its middle, which must be HTCLIENT. A tooltip (Splash)
  window is transparent to the pointer everywhere.

Run it on a private desktop, never the owner's:

    python -B D:/malkuth/projects/tools/run_on_desktop.py -- \
        python -B tools/popup_margin_probe.py <probe.exe> <scale>

It prints one line a window and exits non-zero on any failed check.
"""

import ctypes
import json
import os
import subprocess
import sys
import time
from ctypes import wintypes

WM_NCHITTEST = 0x0084
HTTRANSPARENT = -1
HTCLIENT = 1
# The overlay shadow's reach (box_shadow_visual_outsets of the theme's
# overlay shadow) is at most about 22 units on a side; the largest margin a
# window may have round its face, both sides together.
MAXIMUM_MARGIN = 2 * 24.0
# "Opens the context menu" in one line: a bubble of about 140 by 22 units.
TOOL_TIP_LIMIT = (140.0 + 60.0 + MAXIMUM_MARGIN, 30.0 + MAXIMUM_MARGIN)
BUTTON = 'gallery.commands.context.open'
MENU = 'gallery.commands.menu'
WINDOW_CLASS = 'GUIForms.Window.v1'
PROBE_TITLE = 'GUI.Forms menu probe'
# How long the script holds the open menu, and the shown tooltip, for the
# checks below (milliseconds).
MENU_HOLD = 5000
TIP_HOLD = 7000


class RECT(ctypes.Structure):
    _fields_ = [('left', wintypes.LONG), ('top', wintypes.LONG), ('right', wintypes.LONG),
                ('bottom', wintypes.LONG)]


user32 = ctypes.WinDLL('user32', use_last_error=True)
EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [EnumWindowsProc, wintypes.LPARAM]
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(RECT)]
user32.GetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetWindowLongPtrW.restype = ctypes.c_ssize_t
GWL_EXSTYLE = -20
WS_EX_TOPMOST = 0x00000008


def secondary_windows(process_id, main_window):
    found = []

    def visit(window, _):
        name = ctypes.create_unicode_buffer(128)
        user32.GetClassNameW(window, name, 128)
        if name.value != WINDOW_CLASS or not user32.IsWindowVisible(window) or window == main_window:
            return True
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == process_id:
            found.append(window)
        return True

    user32.EnumWindows(EnumWindowsProc(visit), 0)
    return found


def hit(window, x, y):
    packed = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
    answer = user32.SendMessageW(window, WM_NCHITTEST, 0, packed)
    return answer


def check(window, scale, kind, limit):
    rect = RECT()
    user32.GetWindowRect(window, ctypes.byref(rect))
    width = (rect.right - rect.left) / scale
    height = (rect.bottom - rect.top) / scale
    corner = hit(window, rect.left + 2, rect.top + 2)
    middle = hit(window, (rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2)
    sized = limit is None or (width <= limit[0] and height <= limit[1])
    passes = corner == HTTRANSPARENT and sized
    if kind == 'menu':
        passes = passes and middle == HTCLIENT
    print('%s %.1f x %.1f units, corner hit %d, middle hit %d: %s' %
          (kind, width, height, corner, middle, 'ok' if passes else 'FAILED'))
    return passes


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


def write_script(path):
    steps = [
        {'tool': 'wait', 'until': 'idle', 'timeout_ms': 10000},
        {'tool': 'pointer', 'action': 'click', 'target': MENU, 'offset': {'x': 18, 'y': 10}},
        {'tool': 'wait', 'until': 'ms', 'ms': MENU_HOLD},
        {'tool': 'keyboard', 'keys': ['Escape']},
        {'tool': 'pointer', 'action': 'hover', 'target': BUTTON, 'offset': {'x': 5, 'y': 5}},
        {'tool': 'wait', 'until': 'ms', 'ms': TIP_HOLD},
    ]
    with open(path, 'w', encoding='utf-8') as script:
        for step in steps:
            script.write(json.dumps(step) + '\n')


def poll(process_id, main_window, seconds, keep):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        found = [window for window in secondary_windows(process_id, main_window) if keep(window)]
        if found:
            return found
        time.sleep(0.2)
    return []


def main():
    if len(sys.argv) != 3:
        print('usage: popup_margin_probe.py <probe.exe> <scale>', file=sys.stderr)
        return 2
    probe, scale_text = sys.argv[1], sys.argv[2]
    scale = float(scale_text)
    script = os.path.abspath('popup_margin_probe.jsonl')
    write_script(script)
    environment = dict(os.environ)
    environment['GUI_FORMS_WINDOWS_SCALE'] = scale_text
    process = subprocess.Popen([probe, '--mcp-script', script], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, env=environment)
    window = None
    deadline = time.monotonic() + 30.0
    while window is None and time.monotonic() < deadline and process.poll() is None:
        time.sleep(0.2)
        window = find_probe_window(process.pid)
    if window is None:
        print('probe window not found; exit', process.poll())
        process.kill()
        return 1
    failures = 0
    # The File menu, held open by the script: its window and its margin.
    menus = poll(process.pid, window, MENU_HOLD / 1000.0, lambda menu: True)
    if not menus:
        print('menu: no Popup window: FAILED')
        failures += 1
    for menu in menus:
        failures += 0 if check(menu, scale, 'menu', None) else 1
    # The context button's tooltip, shown by the pointer resting on it.
    time.sleep(1.0)
    tips = poll(process.pid, window, TIP_HOLD / 1000.0,
                lambda tip: user32.GetWindowLongPtrW(tip, GWL_EXSTYLE) & WS_EX_TOPMOST)
    if not tips:
        print('tooltip: no Splash window: FAILED')
        failures += 1
    for tip in tips:
        failures += 0 if check(tip, scale, 'tooltip', TOOL_TIP_LIMIT) else 1
    try:
        process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        process.kill()
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
