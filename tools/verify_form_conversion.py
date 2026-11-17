#!/usr/bin/env python3
"""Checks that the XML form documents compile to the same headers as the CSS
documents they were converted from (cp34; tools/convert_form_to_xml.py).

Every form-compiler invocation the build runs is read from a configured
Ninja tree at the new head (`--build-dir`). For each, the document as it was
at `--base` (the CSS form) is compiled with the compiler built at `--base`,
the current document (XML) with the compiler of the build tree, both into a
scratch directory, and the two headers are compared byte for byte. The old
document's scratch path is replaced by the document's own path in the old
header first, so a header that names its source compares equal.

The old compilers are found by name in `--old-bin-dir` (a build tree of
`--base`: gui_forms_form_compiler and gui_forms_form_compiler_lab). A
document that did not exist at the base is reported and skipped.

Usage:
  python3 -B tools/verify_form_conversion.py --build-dir BUILD --old-bin-dir OLD_BUILD/bin [--base 96d3696]

Exit status: 0 when every header matches, 1 when any differs or a compiler
fails, 2 on a usage error (no invocation found, a missing compiler).
"""

import argparse
import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile

REPOSITORY = pathlib.Path(__file__).resolve().parent.parent


class Invocation:
    """One form-compiler command: its directory and its words."""

    def __init__(self, directory, words):
        self.directory = directory
        self.words = words


def ninja_files(build_dir):
    """The build tree's Ninja files: build.ninja and every file it includes."""
    pending = [build_dir / 'build.ninja']
    seen = []
    while pending:
        path = pending.pop()
        if path in seen or not path.exists():
            continue
        seen.append(path)
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            if line.startswith('include ') or line.startswith('subninja '):
                pending.append(build_dir / line.split(None, 1)[1].strip())
    return seen


def invocations(build_dir):
    """Every form-compiler command in the build tree, in file order."""
    found = []
    for path in ninja_files(build_dir):
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            stripped = line.strip()
            if not stripped.startswith('COMMAND = ') or 'form_compiler' not in stripped:
                continue
            directory = build_dir
            for part in stripped[len('COMMAND = '):].split('&&'):
                words = shlex.split(part)
                if not words:
                    continue
                if words[0] == 'cd' and len(words) == 2:
                    directory = pathlib.Path(words[1])
                    continue
                if 'form_compiler' in pathlib.Path(words[0]).name and '--output' in words:
                    found.append(Invocation(directory, words))
    return found


def document_index(words):
    for index, word in enumerate(words[1:], start=1):
        if word.endswith('.form'):
            return index
    return None


def run_compiler(directory, words):
    """(exit status, stderr) of one compiler run."""
    completed = subprocess.run(words, cwd=directory, capture_output=True, text=True, check=False)
    return completed.returncode, completed.stderr


def verify(invocation, old_bin_dir, base, scratch, number):
    """True when the old and new headers match; prints what it finds."""
    words = list(invocation.words)
    position = document_index(words)
    if position is None:
        print(f'skipped: no document in {shlex.join(words)}')
        return True
    document = (invocation.directory / words[position]).resolve()
    try:
        relative = document.relative_to(REPOSITORY).as_posix()
    except ValueError:
        print(f'skipped: {document} is outside the repository')
        return True
    shown = subprocess.run(['git', '-C', str(REPOSITORY), 'show', f'{base}:{relative}'],
                           capture_output=True, check=False)
    if shown.returncode != 0:
        print(f'skipped: {relative} is new since {base}')
        return True
    case = scratch / str(number)
    case.mkdir()
    old_document = case / pathlib.Path(relative).name
    old_document.write_bytes(shown.stdout)
    output_index = words.index('--output') + 1
    old_compiler = old_bin_dir / pathlib.Path(words[0]).name
    if not old_compiler.exists():
        print(f'refused: no old compiler {old_compiler}')
        return False

    old_words = list(words)
    old_words[0] = str(old_compiler)
    old_words[position] = str(old_document)
    old_words[output_index] = str(case / 'old.hpp')
    old_status, old_errors = run_compiler(invocation.directory, old_words)

    new_words = list(words)
    new_words[position] = str(document)
    new_words[output_index] = str(case / 'new.hpp')
    new_status, new_errors = run_compiler(invocation.directory, new_words)

    if old_status != 0 or new_status != 0:
        print(f'FAILED {relative}: old exit {old_status}, new exit {new_status}')
        sys.stdout.write(old_errors + new_errors)
        return False
    old_header = (case / 'old.hpp').read_text(encoding='utf-8').replace(str(old_document), str(document))
    new_header = (case / 'new.hpp').read_text(encoding='utf-8')
    if old_header != new_header:
        print(f'DIFFERS {relative}: compare {case}/old.hpp and {case}/new.hpp')
        return False
    print(f'same    {relative}')
    return True


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--build-dir', required=True, type=pathlib.Path, help='a configured Ninja tree of the new head')
    parser.add_argument('--old-bin-dir', required=True, type=pathlib.Path, help='where the base\'s compilers are')
    parser.add_argument('--base', default='96d3696', help='the commit the CSS documents are read from')
    parser.add_argument('--keep', action='store_true', help='keep the scratch directory')
    arguments = parser.parse_args(argv)

    found = invocations(arguments.build_dir.resolve())
    if not found:
        print(f'verify_form_conversion: no form-compiler command in {arguments.build_dir}', file=sys.stderr)
        return 2
    scratch = pathlib.Path(tempfile.mkdtemp(prefix='verify_form_conversion.'))
    failures = 0
    for number, invocation in enumerate(found):
        if not verify(invocation, arguments.old_bin_dir.resolve(), arguments.base, scratch, number):
            failures += 1
    print(f'verify_form_conversion: {len(found)} invocation(s), {failures} failure(s); scratch {scratch}')
    if failures == 0 and not arguments.keep:
        shutil.rmtree(scratch)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
