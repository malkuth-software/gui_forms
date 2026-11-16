#!/usr/bin/env python3
"""Runs the d15 rewriter over its before/after fixtures (README.md, "Fixtures").

Each fixture directory holds input/ and expected/ trees of `.in` files (the
suffix keeps the fixtures out of the house-style scan of tools/), an `args`
file of tool arguments and, optionally, a `residue` file of substrings that
must each appear in the residue report. For each fixture the runner copies
input/ into a fresh gui_forms-shaped root with the `.in` suffix removed,
adds the real src/platform/stxlib_bridge.hpp, writes compile_commands.json
for every .cpp, runs the tool in apply mode, compares every file with
expected/, checks the residue, and runs the tool again in check mode, which
must find nothing to change (idempotence). With --compile-results it also
runs `clang++ -fsyntax-only` over the rewritten units (on the M4 only).
"""

import argparse
import difflib
import json
import os
import shutil
import subprocess
import sys
import tempfile

TOOL_DIRECTORY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUI_FORMS_ROOT = os.path.dirname(os.path.dirname(TOOL_DIRECTORY))
FIXTURE_ROOT = os.path.join(TOOL_DIRECTORY, 'tests', 'fixtures')
BRIDGE = os.path.join('src', 'platform', 'stxlib_bridge.hpp')


def copy_stripped(source_tree, target_tree):
    for directory, _, names in os.walk(source_tree):
        for name in sorted(names):
            if not name.endswith('.in'):
                continue
            source = os.path.join(directory, name)
            relative = os.path.relpath(source, source_tree)[:-3]
            target = os.path.join(target_tree, relative)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(source, target)


def tree_files(tree):
    found = {}
    for directory, _, names in os.walk(tree):
        for name in names:
            path = os.path.join(directory, name)
            found[os.path.relpath(path, tree)] = path
    return found


def write_database(root, options):
    commands = []
    for relative in sorted(tree_files(root)):
        if not relative.endswith('.cpp'):
            continue
        arguments = [options.compiler, '-std=c++20', '-I', os.path.join(root, 'include'),
                     '-I', os.path.join(root, 'src'), '-I', options.stxlib_include]
        arguments.extend(options.extra_arg)
        arguments.extend(['-c', relative])
        commands.append({'directory': root, 'file': os.path.join(root, relative), 'arguments': arguments})
    with open(os.path.join(root, 'compile_commands.json'), 'w', encoding='utf-8') as stream:
        json.dump(commands, stream, indent=1)
    return [entry['file'] for entry in commands]


def read_arguments(fixture):
    with open(os.path.join(fixture, 'args'), encoding='utf-8') as stream:
        return stream.read().split()


def run_tool(options, root, arguments, mode, residue_path):
    command = [options.tool, '-p', root, '--source-root', root, '--mode', mode, '--residue', residue_path,
               '--jobs', '1', '--quiet']
    command.extend(arguments)
    for extra in options.extra_arg:
        command.append('--extra-arg=' + extra)
    return subprocess.run(command, capture_output=True, text=True, check=False)


def compare_trees(root, expected_tree, name):
    failures = []
    expected = {}
    for relative, path in tree_files(expected_tree).items():
        if relative.endswith('.in'):
            expected[relative[:-3]] = path
    for relative in sorted(expected):
        actual_path = os.path.join(root, relative)
        with open(expected[relative], encoding='utf-8') as stream:
            wanted = stream.read()
        if not os.path.exists(actual_path):
            failures.append(f'{name}: {relative} is missing')
            continue
        with open(actual_path, encoding='utf-8') as stream:
            actual = stream.read()
        if actual != wanted:
            diff = difflib.unified_diff(wanted.splitlines(True), actual.splitlines(True),
                                        'expected/' + relative, 'actual/' + relative)
            failures.append(f'{name}: {relative} differs\n' + ''.join(diff))
    return failures


def check_residue(fixture, residue_path, name):
    wanted_path = os.path.join(fixture, 'residue')
    if not os.path.exists(wanted_path):
        return []
    with open(residue_path, encoding='utf-8') as stream:
        residue = stream.read()
    failures = []
    with open(wanted_path, encoding='utf-8') as stream:
        for line in stream:
            needle = line.strip()
            if needle and needle not in residue:
                failures.append(f'{name}: residue lacks "{needle}"\n{residue}')
    return failures


def compile_results(options, root, units, name):
    failures = []
    for unit in units:
        command = [options.compiler, '-std=c++20', '-fsyntax-only', '-I', os.path.join(root, 'include'),
                   '-I', os.path.join(root, 'src'), '-I', options.stxlib_include]
        command.extend(options.extra_arg)
        command.append(unit)
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            failures.append(f'{name}: {os.path.relpath(unit, root)} does not compile\n{result.stderr}')
    return failures


def run_fixture(options, name, scratch):
    fixture = os.path.join(FIXTURE_ROOT, name)
    root = os.path.join(scratch, name)
    os.makedirs(root)
    copy_stripped(os.path.join(fixture, 'input'), root)
    os.makedirs(os.path.join(root, 'src', 'platform'), exist_ok=True)
    shutil.copyfile(os.path.join(GUI_FORMS_ROOT, BRIDGE), os.path.join(root, BRIDGE))
    units = write_database(root, options)
    arguments = read_arguments(fixture)
    residue_path = os.path.join(scratch, name + '.residue')
    applied = run_tool(options, root, arguments, 'apply', residue_path)
    if applied.returncode != 0:
        return [f'{name}: apply exited {applied.returncode}\n{applied.stdout}{applied.stderr}']
    failures = compare_trees(root, os.path.join(fixture, 'expected'), name)
    failures.extend(check_residue(fixture, residue_path, name))
    checked = run_tool(options, root, arguments, 'check', residue_path + '.again')
    if checked.returncode != 0:
        failures.append(f'{name}: second run is not idempotent (check exited {checked.returncode})\n'
                        f'{checked.stdout}{checked.stderr}')
    if options.compile_results:
        failures.extend(compile_results(options, root, units, name))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--tool', required=True, help='the built gui_forms_d15_rewrite')
    parser.add_argument('--stxlib-include', required=True, help="STXlib's include directory")
    parser.add_argument('--compiler', default='/opt/homebrew/opt/llvm@22/bin/clang++')
    parser.add_argument('--extra-arg', action='append', default=[],
                        help='compiler argument for the units (repeat), e.g. -resource-dir=..., -isysroot...')
    parser.add_argument('--only', action='append', default=[], help='run only this fixture (repeat)')
    parser.add_argument('--compile-results', action='store_true',
                        help='also syntax-check the rewritten units (M4 only)')
    parser.add_argument('--keep', action='store_true', help='keep the scratch roots and print where')
    options = parser.parse_args()

    names = sorted(entry for entry in os.listdir(FIXTURE_ROOT)
                   if os.path.isdir(os.path.join(FIXTURE_ROOT, entry)))
    if options.only:
        names = [name for name in names if name in options.only]
    scratch = tempfile.mkdtemp(prefix='d15-fixtures-')
    failures = []
    for name in names:
        result = run_fixture(options, name, scratch)
        print(f'{"FAIL" if result else "ok  "} {name}')
        failures.extend(result)
    for failure in failures:
        print(failure, file=sys.stderr)
    if options.keep:
        print(f'scratch: {scratch}')
    else:
        shutil.rmtree(scratch, ignore_errors=True)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
