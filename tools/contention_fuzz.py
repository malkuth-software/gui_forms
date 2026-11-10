"""Seeded contention fuzzer for a GUI.Forms Windows program.

Drives one program (the menu probe, which is the Gallery under
Application::run, or a product such as Plan Paint started with the
automation channel) from three directions at once, in an order fixed by a
seed:

- automation commands through WM_COPYDATA (snapshot, capture, click-at,
  activate, move, key-focused, overlay on/off), as an agent's probe sends
  them;
- injected input, posted to the program's windows (presses and releases at
  points in the main window or in its Popup windows, keys, frame presses on
  every edge and corner released or abandoned mid-flight, activation lost
  and regained);
- erratic manipulation: menus and submenus opened and closed as fast as the
  queue takes them, clicks while they open, the invalidation overlay and
  captures toggled while they show.

Each step is one action. After every step the program must answer within
`--answer-ms` (SendMessageTimeout of WM_NULL). Every `--settle-every` steps
the fuzzer releases every button, sends Escape four times and waits; then:

- no Popup window of the program is visible (no menu, submenu, drop-down or
  list outlives its root; a tooltip may take up to `--tooltip-ms` to go);
- the UI thread holds no capture and is in no move/size loop
  (GetGUIThreadInfo);
- the program shows as many top-level windows as it did at the start;
- no automation command that must succeed (snapshot) was refused.

On a failure it writes, under `--out`/seed-<seed>: the step log
(steps.jsonl), failure.json, the last composite capture when the program
still answers, and a minidump of the program (dbghelp MiniDumpWriteDump);
then, unless --no-shrink, it shrinks the step list (removing chunks that
are not needed to fail again, at most `--shrink-runs` replays) and writes
replay.json, which `--replay` runs again.

Private desktop only (never the owner's):

    python -B D:/malkuth/projects/tools/run_on_desktop.py -- \\
        python -B tools/contention_fuzz.py <program.exe> --seed 1 --steps 400

A profile names the program's automation targets: `--profile gallery`
(built in) or `--profile <file.json>` with {"title": ..., "menu": <stable
id of a control that opens a menu when clicked at (18, 10)>, "targets":
[<stable ids to click, activate and move over>]}.

Exit status: 0 when every invariant held, 1 on a failure (seed and files
printed), 2 on a usage error or a program that never showed its window.
"""

import argparse
import ctypes
import json
import os
import random
import subprocess
import sys
import time
from ctypes import wintypes

WM_NULL = 0x0000
WM_ACTIVATE = 0x0006
WM_ACTIVATEAPP = 0x001C
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_MOUSEMOVE = 0x0200
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_NCLBUTTONDOWN = 0x00A1
WM_NCMOUSEMOVE = 0x00A0
WM_NCLBUTTONUP = 0x00A2
WM_COPYDATA = 0x004A
MK_LBUTTON = 0x0001
SMTO_ABORTIFHUNG = 0x0002
GUI_INMOVESIZE = 0x00000002
GWL_EXSTYLE = -20
WS_EX_TOPMOST = 0x00000008
WINDOW_CLASS = 'GUIForms.Window.v1'
AUTOMATION_TAG = 0x47464131
# USB HID usages, as key-focused takes them.
KEY_DOWN = 81
KEY_UP = 82
KEY_RIGHT = 79
KEY_LEFT = 80
KEY_ESCAPE = 41
KEY_ENTER = 40
VK_ESCAPE = 0x1B
VK_DOWN = 0x28
VK_RIGHT = 0x27
VK_RETURN = 0x0D
VK_MENU = 0x12
# Frame parts a press may start a drag on (WM_NCHITTEST codes).
FRAME_PARTS = [2, 10, 11, 12, 13, 14, 15, 16, 17]

PROFILES = {
    'gallery': {
        'title': 'GUI.Forms menu probe',
        'menu': 'gallery.commands.menu',
        'targets': ['gallery.commands.menu', 'gallery.commands.context.open',
                    'gallery.commands.ribbon.bar'],
    },
}


class COPYDATASTRUCT(ctypes.Structure):
    _fields_ = [('dwData', ctypes.c_size_t), ('cbData', wintypes.DWORD), ('lpData', ctypes.c_void_p)]


class RECT(ctypes.Structure):
    _fields_ = [('left', wintypes.LONG), ('top', wintypes.LONG), ('right', wintypes.LONG),
                ('bottom', wintypes.LONG)]


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [('cbSize', wintypes.DWORD), ('flags', wintypes.DWORD), ('hwndActive', wintypes.HWND),
                ('hwndFocus', wintypes.HWND), ('hwndCapture', wintypes.HWND), ('hwndMenuOwner', wintypes.HWND),
                ('hwndMoveSize', wintypes.HWND), ('hwndCaret', wintypes.HWND), ('rcCaret', RECT)]


user32 = ctypes.WinDLL('user32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [EnumWindowsProc, wintypes.LPARAM]
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindow.argtypes = [wintypes.HWND, wintypes.UINT]
user32.GetWindow.restype = wintypes.HWND
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(RECT)]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(RECT)]
user32.GetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetWindowLongPtrW.restype = ctypes.c_ssize_t
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM,
                                       wintypes.UINT, wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
user32.SendMessageTimeoutW.restype = ctypes.c_ssize_t
user32.GetGUIThreadInfo.argtypes = [wintypes.DWORD, ctypes.POINTER(GUITHREADINFO)]
GW_OWNER = 4


def pack(x, y):
    return ((int(y) & 0xFFFF) << 16) | (int(x) & 0xFFFF)


class Program:
    """One run of the program under test, and its windows."""

    def __init__(self, executable, profile, scale, answer_ms):
        self.executable = executable
        self.profile = profile
        self.answer_ms = answer_ms
        environment = dict(os.environ)
        environment['GUI_FORMS_WINDOWS_AUTOMATION'] = '1'
        environment['GUI_FORMS_WINDOWS_SCALE'] = scale
        self.process = subprocess.Popen([executable], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                        env=environment)
        self.main = None
        self.thread = 0
        deadline = time.monotonic() + 30.0
        while self.main is None and time.monotonic() < deadline and self.process.poll() is None:
            time.sleep(0.2)
            self.main = self.find_main()
        if self.main is not None:
            self.thread = user32.GetWindowThreadProcessId(self.main, None)
            time.sleep(1.0)
        self.refusals = 0

    def windows(self):
        found = []

        def visit(window, _):
            name = ctypes.create_unicode_buffer(128)
            user32.GetClassNameW(window, name, 128)
            if name.value != WINDOW_CLASS or not user32.IsWindowVisible(window):
                return True
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
            if owner.value == self.process.pid:
                found.append(window)
            return True

        user32.EnumWindows(EnumWindowsProc(visit), 0)
        return found

    def find_main(self):
        for window in self.windows():
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(window, title, 256)
            if title.value == self.profile['title']:
                return window
        return None

    def popups(self):
        """Visible windows of the program other than its main window, owned by it."""
        shown = []
        for window in self.windows():
            if window == self.main:
                continue
            if user32.GetWindow(window, GW_OWNER):
                shown.append(window)
        return shown

    def alive(self):
        return self.process.poll() is None

    def answers(self):
        result = ctypes.c_size_t()
        sent = user32.SendMessageTimeoutW(self.main, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, self.answer_ms,
                                          ctypes.byref(result))
        return sent != 0

    def automation(self, command):
        payload = ('GUI.Forms.Automation/1 ' + command).encode('utf-8')
        buffer = ctypes.create_string_buffer(payload, len(payload))
        data = COPYDATASTRUCT(AUTOMATION_TAG, len(payload), ctypes.cast(buffer, ctypes.c_void_p))
        result = ctypes.c_size_t()
        sent = user32.SendMessageTimeoutW(self.main, WM_COPYDATA, 0, ctypes.addressof(data), SMTO_ABORTIFHUNG,
                                          self.answer_ms, ctypes.byref(result))
        return sent != 0 and result.value != 0

    def client_size(self):
        rect = RECT()
        user32.GetClientRect(self.main, ctypes.byref(rect))
        return rect.right, rect.bottom

    def thread_state(self):
        info = GUITHREADINFO()
        info.cbSize = ctypes.sizeof(GUITHREADINFO)
        if not user32.GetGUIThreadInfo(self.thread, ctypes.byref(info)):
            return None
        return {'capture': bool(info.hwndCapture), 'move_size': bool(info.flags & GUI_INMOVESIZE)}

    def minidump(self, path):
        dbghelp = ctypes.WinDLL('dbghelp', use_last_error=True)
        process_all_access = 0x1F0FFF
        handle = kernel32.OpenProcess(process_all_access, False, self.process.pid)
        if not handle:
            return False
        generic_write = 0x40000000
        create_always = 2
        file = kernel32.CreateFileW(path, generic_write, 0, None, create_always, 0, None)
        if file == ctypes.c_void_p(-1).value:
            kernel32.CloseHandle(handle)
            return False
        # MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory
        dump_type = 0x1000 | 0x40 | 0x10
        written = dbghelp.MiniDumpWriteDump(handle, self.process.pid, file, dump_type, None, None, None)
        kernel32.CloseHandle(file)
        kernel32.CloseHandle(handle)
        return bool(written)

    def stop(self):
        if self.alive() and self.main is not None:
            self.automation('close')
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                pass
        if self.alive():
            self.process.kill()
            self.process.wait(timeout=10)


def make_steps(seed, count, profile):
    """The step list of `seed`: data only, so a replay runs the same steps."""
    rng = random.Random(seed)
    targets = profile['targets']
    steps = []
    for _ in range(count):
        kind = rng.choice(['snapshot', 'capture', 'click_at', 'activate', 'move', 'key', 'overlay',
                           'post_click', 'post_press', 'post_release', 'post_key', 'frame_drag',
                           'activation', 'menu_storm', 'submenu', 'click_in_popup'])
        step = {'kind': kind}
        if kind in ('click_at', 'move'):
            step.update(target=rng.choice(targets), x=rng.randint(1, 40), y=rng.randint(1, 20))
        elif kind == 'activate':
            step.update(target=rng.choice(targets))
        elif kind == 'key':
            step.update(key=rng.choice([KEY_DOWN, KEY_UP, KEY_RIGHT, KEY_LEFT, KEY_ESCAPE, KEY_ENTER]))
        elif kind == 'overlay':
            step.update(state=rng.choice(['on', 'off', 'default']))
        elif kind in ('post_click', 'post_press', 'click_in_popup'):
            step.update(fx=rng.random(), fy=rng.random())
        elif kind == 'post_key':
            step.update(vk=rng.choice([VK_ESCAPE, VK_DOWN, VK_RIGHT, VK_RETURN, VK_MENU]))
        elif kind == 'frame_drag':
            step.update(part=rng.choice(FRAME_PARTS), release=rng.random() < 0.5, dx=rng.randint(-40, 40),
                        dy=rng.randint(-40, 40))
        elif kind == 'menu_storm':
            step.update(times=rng.randint(2, 6))
        steps.append(step)
    return steps


def run_step(program, step, capture_path):
    """Runs one step; returns False when an automation command that must succeed was refused."""
    kind = step['kind']
    main = program.main
    width, height = program.client_size()
    if kind == 'snapshot':
        return program.automation('snapshot')
    if kind == 'capture':
        program.automation('capture-composite ' + capture_path)
    elif kind == 'click_at':
        program.automation('click-at %s %d %d' % (step['target'], step['x'], step['y']))
    elif kind == 'activate':
        program.automation('activate ' + step['target'])
    elif kind == 'move':
        program.automation('move %s %d %d' % (step['target'], step['x'], step['y']))
    elif kind == 'key':
        program.automation('key-focused %d' % step['key'])
    elif kind == 'overlay':
        program.automation('overlay ' + step['state'])
    elif kind in ('post_click', 'post_press'):
        point = pack(step['fx'] * max(1, width - 1), step['fy'] * max(1, height - 1))
        user32.PostMessageW(main, WM_MOUSEMOVE, 0, point)
        user32.PostMessageW(main, WM_LBUTTONDOWN, MK_LBUTTON, point)
        if kind == 'post_click':
            user32.PostMessageW(main, WM_LBUTTONUP, 0, point)
    elif kind == 'post_release':
        user32.PostMessageW(main, WM_LBUTTONUP, 0, pack(width // 2, height // 2))
    elif kind == 'post_key':
        user32.PostMessageW(main, WM_KEYDOWN, step['vk'], 1)
        user32.PostMessageW(main, WM_KEYUP, step['vk'], 0xC0000001)
    elif kind == 'frame_drag':
        rect = RECT()
        user32.GetWindowRect(main, ctypes.byref(rect))
        start = pack(rect.left + 2, rect.top + 2)
        user32.PostMessageW(main, WM_NCLBUTTONDOWN, step['part'], start)
        user32.PostMessageW(main, WM_NCMOUSEMOVE, step['part'], pack(rect.left + 2 + step['dx'],
                                                                     rect.top + 2 + step['dy']))
        if step['release']:
            user32.PostMessageW(main, WM_LBUTTONUP, 0, pack(2, 2))
    elif kind == 'activation':
        user32.PostMessageW(main, WM_ACTIVATEAPP, 0, 0)
        user32.PostMessageW(main, WM_ACTIVATE, 0, 0)
        user32.PostMessageW(main, WM_ACTIVATEAPP, 1, 0)
        user32.PostMessageW(main, WM_ACTIVATE, 1, 0)
    elif kind == 'menu_storm':
        for _ in range(step['times']):
            program.automation('click-at %s 18 10' % program.profile['menu'])
            program.automation('key-focused %d' % KEY_ESCAPE)
    elif kind == 'submenu':
        program.automation('click-at %s 18 10' % program.profile['menu'])
        for key in (KEY_DOWN, KEY_DOWN, KEY_RIGHT, KEY_DOWN):
            program.automation('key-focused %d' % key)
    elif kind == 'click_in_popup':
        popups = program.popups()
        if popups:
            target = popups[int(step['fx'] * len(popups)) % len(popups)]
            rect = RECT()
            user32.GetClientRect(target, ctypes.byref(rect))
            point = pack(step['fx'] * max(1, rect.right - 1), step['fy'] * max(1, rect.bottom - 1))
            user32.PostMessageW(target, WM_MOUSEMOVE, 0, point)
            user32.PostMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, point)
            user32.PostMessageW(target, WM_LBUTTONUP, 0, point)
    return True


def settle(program, tooltip_ms):
    """Ends every gesture and menu, then reads the invariants; returns the broken ones."""
    width, height = program.client_size()
    user32.PostMessageW(program.main, WM_LBUTTONUP, 0, pack(width // 2, height // 2))
    for _ in range(4):
        program.automation('key-focused %d' % KEY_ESCAPE)
    broken = []
    deadline = time.monotonic() + tooltip_ms / 1000.0
    left = program.popups()
    while left and time.monotonic() < deadline:
        time.sleep(0.05)
        left = program.popups()
    if left:
        kinds = ['tooltip' if user32.GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOPMOST else 'popup' for w in left]
        broken.append('windows outlived their root: ' + ', '.join(kinds))
    state = program.thread_state()
    if state is not None and state['capture']:
        broken.append('the UI thread holds the capture with no button down')
    if state is not None and state['move_size']:
        broken.append('the UI thread is stuck in a move/size loop')
    return broken


def run(program_path, steps, options, log_path=None):
    """Runs `steps`; returns (failure or None, index of the failing step, program)."""
    profile = options.profile_data
    program = Program(program_path, profile, options.scale, options.answer_ms)
    if program.main is None:
        program.stop()
        return {'reason': 'the program never showed its window'}, -1, None
    baseline = len(program.windows())
    log = open(log_path, 'w', encoding='utf-8') if log_path else None
    capture_path = os.path.abspath(os.path.join(options.out, 'step-capture.png'))
    failure = None
    index = -1
    try:
        for index, step in enumerate(steps):
            if log:
                log.write(json.dumps({'step': index, **step}) + '\n')
                log.flush()
            accepted = run_step(program, step, capture_path)
            if not program.alive():
                failure = {'reason': 'the program exited', 'code': program.process.poll()}
                break
            if not program.answers():
                failure = {'reason': 'the program did not answer within %d ms' % options.answer_ms}
                break
            if not accepted:
                failure = {'reason': 'a snapshot command was refused'}
                break
            if (index + 1) % options.settle_every == 0 or index + 1 == len(steps):
                broken = settle(program, options.tooltip_ms)
                shown = len(program.windows())
                if shown != baseline:
                    broken.append('%d top-level windows shown, %d at the start' % (shown, baseline))
                if broken:
                    failure = {'reason': '; '.join(broken)}
                    break
    finally:
        if log:
            log.close()
    return failure, index, program


def keep_evidence(program, folder):
    """The last composite capture and a minidump, where they can be taken."""
    evidence = {}
    if program is None or not program.alive():
        return evidence
    if program.answers():
        capture = os.path.abspath(os.path.join(folder, 'last.png'))
        evidence['capture'] = capture if program.automation('capture-composite ' + capture) else None
    dump = os.path.abspath(os.path.join(folder, 'program.dmp'))
    evidence['minidump'] = dump if program.minidump(dump) else None
    return evidence


def shrink(program_path, steps, options):
    """Removes chunks of `steps` while the run still fails (ddmin, bounded replays)."""
    current = list(steps)
    chunk = max(1, len(current) // 2)
    runs = 0
    while chunk >= 1 and runs < options.shrink_runs:
        removed_any = False
        start = 0
        while start < len(current) and runs < options.shrink_runs:
            trial = current[:start] + current[start + chunk:]
            if not trial:
                start += chunk
                continue
            failure, _, program = run(program_path, trial, options)
            runs += 1
            if program is not None:
                program.stop()
            if failure is not None and failure.get('reason') != 'the program never showed its window':
                current = trial
                removed_any = True
            else:
                start += chunk
        if not removed_any:
            chunk //= 2
    return current, runs


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('program')
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--steps', type=int, default=400)
    parser.add_argument('--settle-every', type=int, default=20)
    parser.add_argument('--answer-ms', type=int, default=3000)
    parser.add_argument('--tooltip-ms', type=int, default=3000)
    parser.add_argument('--scale', default='1')
    parser.add_argument('--profile', default='gallery')
    parser.add_argument('--out', default='contention-fuzz')
    parser.add_argument('--replay', default='')
    parser.add_argument('--no-shrink', action='store_true')
    parser.add_argument('--shrink-runs', type=int, default=40)
    options = parser.parse_args()
    if options.profile in PROFILES:
        options.profile_data = PROFILES[options.profile]
    else:
        with open(options.profile, encoding='utf-8') as file:
            options.profile_data = json.load(file)
    if options.replay:
        with open(options.replay, encoding='utf-8') as file:
            replay = json.load(file)
        options.seed = replay['seed']
        steps = replay['steps']
    else:
        steps = make_steps(options.seed, options.steps, options.profile_data)
    folder = os.path.join(options.out, 'seed-%d' % options.seed)
    os.makedirs(folder, exist_ok=True)
    started = time.monotonic()
    failure, index, program = run(options.program, steps, options, os.path.join(folder, 'steps.jsonl'))
    if failure is None:
        if program is not None:
            program.stop()
        print('seed %d: %d steps, every invariant held (%.1f s)' % (options.seed, len(steps),
                                                                   time.monotonic() - started))
        return 0
    if program is None:
        print('seed %d: %s' % (options.seed, failure['reason']))
        return 2
    failure.update(seed=options.seed, step=index, steps=len(steps))
    failure.update(keep_evidence(program, folder))
    program.stop()
    with open(os.path.join(folder, 'failure.json'), 'w', encoding='utf-8') as file:
        json.dump(failure, file, indent=1)
    print('seed %d FAILED at step %d: %s (%s)' % (options.seed, index, failure['reason'], folder))
    if not options.no_shrink and not options.replay:
        short, runs = shrink(options.program, steps[:index + 1], options)
        with open(os.path.join(folder, 'replay.json'), 'w', encoding='utf-8') as file:
            json.dump({'seed': options.seed, 'reason': failure['reason'], 'steps': short}, file, indent=1)
        print('shrunk to %d steps in %d replays: %s' % (len(short), runs, os.path.join(folder, 'replay.json')))
    return 1


if __name__ == '__main__':
    sys.exit(main())
