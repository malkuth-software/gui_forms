#!/usr/bin/env python3
"""Compares gui_forms::SurfaceRoot's layout as two libraries compile it.

#483's surface pan failed because the surface host library and the core
library disagreed on SurfaceRoot's layout: the host's inline
set_scroll_copies_supported wrote its bool where core reads the begin
pointer of scroll_copies_ (UBSan: a ScrollCopy* of 0x1). This probe finds
the first member whose offset differs and the compile options that differ.

It reads the build tree's compile_commands.json and takes the commands of
two sources (by default src/core/window/window.cpp, compiled into
gui_forms_core, and src/host/surface/manager/surface_manager.cpp, compiled
into gui_forms_host_surface). It writes one probe source that lists
sizeof, alignof and the offset of every data member of SurfaceRoot (read
from the header; the probe is the class's friend
detail::SurfaceRootLayoutProbe), compiles it to assembly with each command,
and reads the emitted values back. Nothing is linked or run.

Run on the build host (never on Neo), from the checkout's root:

    python3 -B tools/layout_probe/surface_root_layout_probe.py --build <build tree>

The build tree needs compile_commands.json
(-DCMAKE_EXPORT_COMPILE_COMMANDS=ON). Exit status: 0 when the layouts
agree, 1 when they differ, 2 on a usage or compile problem.
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

HEADER = 'include/gui_forms/surface_root/surface_root.hpp'
CLASS_LINE = 'class GUI_FORMS_API SurfaceRoot {'
MEMBER = re.compile(r'^\s*(?!static\b|friend\b|using\b|return\b|typedef\b|enum\b|struct\b|class\b)'
                    r'[^/()=]*?[\w>\]*&]\s+([a-z_][a-z0-9_]*_)\s*(\{[^;]*\})?;\s*(//.*)?$')


def member_names(root):
    """The data members declared at the class's own brace depth, in order."""
    with open(os.path.join(root, HEADER), encoding='utf-8') as source:
        lines = source.read().split('\n')
    start = next(index for index, line in enumerate(lines) if line.startswith(CLASS_LINE))
    names = []
    depth = 0
    for line in lines[start:]:
        code = line.split('//')[0]
        if depth == 1:
            found = MEMBER.match(line)
            if found:
                names.append(found.group(1))
        depth += code.count('{') - code.count('}')
        if depth == 0 and line.startswith('};'):
            break
    return names


def probe_source(names):
    entries = ['sizeof(SurfaceRoot)', 'alignof(SurfaceRoot)']
    entries += ['offsetof(SurfaceRoot, %s)' % name for name in names]
    body = ',\n        '.join(entries)
    return ('#include "gui_forms/surface_root/surface_root.hpp"\n'
            '#include <cstddef>\n'
            'namespace gui_forms::detail {\n'
            'struct SurfaceRootLayoutProbe final {\n'
            '    static constexpr std::size_t values[] = {\n        %s,\n    };\n'
            '};\n'
            '} // namespace gui_forms::detail\n'
            'extern "C" [[gnu::used]] const std::size_t* const gui_forms_surface_root_layout_probe =\n'
            '    gui_forms::detail::SurfaceRootLayoutProbe::values;\n' % body)


def command_for(commands, root, relative):
    wanted = os.path.normpath(os.path.join(root, relative))
    for entry in commands:
        path = entry['file']
        if not os.path.isabs(path):
            path = os.path.join(entry['directory'], path)
        if os.path.normpath(path) == wanted:
            arguments = entry.get('arguments') or shlex.split(entry['command'])
            return entry['directory'], arguments
    return None, None


def probe_arguments(arguments, source, output):
    """The command with its source and output replaced, compiled to assembly."""
    result = []
    skip = False
    for argument in arguments:
        if skip:
            skip = False
            continue
        if argument in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if argument in ('-c', '-MD', '-MMD') or argument.endswith(('.cpp', '.cc', '.cxx', '.mm')):
            continue
        result.append(argument)
    # -fno-lto: an LTO build's -S would write LLVM IR (parse_values reads
    # both, but target assembly is the layout the linker sees).
    result += ['-Wno-error', '-Wno-invalid-offsetof', '-fno-lto', '-S', '-o', output, source]
    return result


VALUES_SYMBOL = 'SurfaceRootLayoutProbe6valuesE'
# An LLVM IR definition of the values (an LTO build's -S writes IR): the
# first i64 array in its initializer (ASan wraps it in a struct with a
# redzone array after it).
IR_VALUES = re.compile(r'^@\S*' + VALUES_SYMBOL + r'\s*=[^\n]*?\[(i64 -?\d+(?:, i64 -?\d+)*)\]', re.MULTILINE)
# The values' label in target assembly (Mach-O adds a leading underscore).
ASM_LABEL = re.compile(r'^\s*"?_*\w*' + VALUES_SYMBOL + r'"?:', re.MULTILINE)
ASM_WORD = re.compile(r'^\.(?:quad|xword|8byte|dword)\s+(0x[0-9a-fA-F]+|-?\d+)$')
ASM_ZEROS = re.compile(r'^\.(?:zero|space|skip)\s+(\d+)$')
ASM_SKIPPED = ('.p2align', '.align', '.balign', '.loc', '.cfi_')


def asm_values(text):
    """The 8-byte words after the values' label, or None."""
    found = ASM_LABEL.search(text)
    if not found:
        return None
    values = []
    for line in text[found.end():].split('\n'):
        code = re.split(r'\s(?:;|//|#|@)|^(?:;|//|#|@)', line.strip())[0].strip()
        if not code or code.startswith(ASM_SKIPPED):
            if values:
                break
            continue
        word = ASM_WORD.match(code)
        zeros = ASM_ZEROS.match(code)
        if word:
            values.append(int(word.group(1), 0))
        elif zeros and int(zeros.group(1)) % 8 == 0:
            values += [0] * (int(zeros.group(1)) // 8)
        else:
            break
    return values or None


def parse_values(text):
    """The values from assembly or from LLVM IR, or None."""
    found = IR_VALUES.search(text)
    if found:
        return [int(entry.split()[1]) for entry in found.group(1).split(', ')]
    return asm_values(text)


def compile_values(directory, arguments, source, scratch, label, keep):
    output = os.path.join(scratch, label + '.s')
    command = probe_arguments(arguments, source, output)
    completed = subprocess.run(command, cwd=directory, capture_output=True, text=True)
    if completed.returncode != 0:
        sys.stderr.write('%s: the probe did not compile:\n%s\n' % (label, completed.stderr[-4000:]))
        return None
    with open(output, encoding='utf-8', errors='replace') as assembly:
        text = assembly.read()
    values = parse_values(text)
    if values is None:
        # Kept beside the build tree, since the scratch directory goes.
        kept = os.path.join(keep, 'surface_root_layout_probe_%s.s' % label)
        shutil.copyfile(output, kept)
        sys.stderr.write('%s: the values were not found in the compiler output (kept at %s)\n' % (label, kept))
        return None
    return values


def option_set(arguments):
    kept = set()
    for argument in arguments:
        if argument.startswith(('-D', '-U', '-f', '-m', '-std', '-stdlib', '-include', '--target', '-isysroot')):
            kept.add(argument)
    return kept


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--build', required=True, help='build tree holding compile_commands.json')
    parser.add_argument('--first', default='src/core/window/window.cpp')
    parser.add_argument('--second', default='src/host/surface/manager/surface_manager.cpp')
    options = parser.parse_args()
    root = os.getcwd()
    database = os.path.join(options.build, 'compile_commands.json')
    if not os.path.exists(database):
        sys.stderr.write('no %s: configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n' % database)
        return 2
    with open(database, encoding='utf-8') as handle:
        commands = json.load(handle)
    names = member_names(root)
    labels = ['sizeof', 'alignof'] + names
    with tempfile.TemporaryDirectory() as scratch:
        source = os.path.join(scratch, 'surface_root_layout_probe.cpp')
        with open(source, 'w', encoding='utf-8') as handle:
            handle.write(probe_source(names))
        results = []
        option_sets = []
        for label, relative in (('first', options.first), ('second', options.second)):
            directory, arguments = command_for(commands, root, relative)
            if arguments is None:
                sys.stderr.write('%s is not in %s\n' % (relative, database))
                return 2
            values = compile_values(directory, arguments, source, scratch, label, options.build)
            if values is None:
                return 2
            if len(values) != len(labels):
                sys.stderr.write('%s: %d values for %d labels\n' % (label, len(values), len(labels)))
                return 2
            results.append(values)
            option_sets.append(option_set(arguments))
    print('sizeof(SurfaceRoot): %d (%s) and %d (%s)' % (results[0][0], options.first, results[1][0], options.second))
    differing = [index for index in range(len(labels)) if results[0][index] != results[1][index]]
    print('options only in %s: %s' % (options.first, ' '.join(sorted(option_sets[0] - option_sets[1])) or 'none'))
    print('options only in %s: %s' % (options.second, ' '.join(sorted(option_sets[1] - option_sets[0])) or 'none'))
    if not differing:
        print('one layout: every member at the same offset')
        return 0
    first = differing[0]
    print('first difference: %s (%d and %d); the member before it changes size' %
          (labels[first], results[0][first], results[1][first]))
    for index in differing[:12]:
        print('  %-40s %8d %8d' % (labels[index], results[0][index], results[1][index]))
    return 1


def run():
    """main's status, never 0 unless main returned 0 (an exception is 2)."""
    try:
        status = main()
    except Exception as error:  # Reported, then a usage or compile problem.
        sys.stderr.write('surface_root_layout_probe: %s: %s\n' % (type(error).__name__, error))
        return 2
    return 2 if status is None else status


if __name__ == '__main__':
    sys.exit(run())
