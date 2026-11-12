#!/usr/bin/env python3
"""Rewrites the retired `layout_direction` property spelling to `right_to_left`.

Owner ruling of 2026-11-11: GUI.Forms spells Windows Forms' RightToLeft as
`right_to_left` (values no, yes, inherit) in the property grid, serialization
and forms. `layout_direction` is internal storage, and the form compiler
refuses it (E820 form_property_renamed). This script migrates sources that
still write the old spelling, so that consumers (the products, through I6)
apply the same change mechanically.

Rewritten, with the value mapping left_to_right -> no, right_to_left -> yes,
inherit -> inherit:

- form documents (*.form): `layout-direction: <value>;`, values in kebab case
  (`left-to-right`) or snake case;
- JSON documents (*.json, *.dml): a `"layout_direction": "<value>"` member;
- C++ registry writes: a `PropertyEnumValue{"layout_direction", "<choice>", N}`
  value becomes RightToLeft's choice and number, and a `"layout_direction"`
  property name in a (set_)property_value call on the same line moves with
  it.

Reported, not rewritten (they need a reading): any other line naming
`"layout_direction"` as a string. Typed C++ writes of the storage cell
(`control.layout_direction = LayoutDirection::...`) are storage and stay.

Usage:
  python3 -B tools/migrate_right_to_left.py [--write] PATH...

Without --write the script reports what it would change and exits 1 when
anything would; with --write it rewrites files in place and exits 0. A
directory is walked for *.form, *.json, *.dml, *.cpp, *.hpp and *.h files,
skipping .git and build trees.
"""

import argparse
import pathlib
import re
import sys

# The stored LayoutDirection choice -> RightToLeft's choice and number.
VALUE_MAP = {
    "left_to_right": ("no", 0),
    "right_to_left": ("yes", 1),
    "inherit": ("inherit", 2),
}

SUFFIXES = {".form", ".json", ".dml", ".cpp", ".hpp", ".h"}
SKIPPED_DIRECTORIES = {".git", "build", ".build", "node_modules"}

FORM_PATTERN = re.compile(r"(?P<lead>(?<![\w-]))layout-direction(?P<gap>\s*:\s*)(?P<value>[a-z_-]+)")
JSON_PATTERN = re.compile(r'"layout_direction"(?P<gap>\s*:\s*)"(?P<value>[a-z_-]+)"')
ENUM_VALUE_PATTERN = re.compile(
    r'PropertyEnumValue\{\s*"layout_direction"\s*,\s*"(?P<value>[a-z_]+)"\s*,\s*-?\d+\s*\}')
NAME_CALL_PATTERN = re.compile(
    r'(?P<call>(?:set_)?property_(?:value|descriptor)\s*\(\s*)"layout_direction"')


def mapped(value):
    """RightToLeft's (choice, number) for a stored choice, or None."""
    registered = value.replace("-", "_")
    return VALUE_MAP.get(registered)


def rewrite_form(line):
    def replace(match):
        target = mapped(match.group("value"))
        if target is None:
            return match.group(0)
        return match.group("lead") + "right-to-left" + match.group("gap") + target[0]
    return FORM_PATTERN.sub(replace, line)


def rewrite_json(line):
    def replace(match):
        target = mapped(match.group("value"))
        if target is None:
            return match.group(0)
        return '"right_to_left"' + match.group("gap") + '"' + target[0] + '"'
    return JSON_PATTERN.sub(replace, line)


def rewrite_cpp(line):
    def replace_value(match):
        target = mapped(match.group("value"))
        if target is None:
            return match.group(0)
        return 'PropertyEnumValue{"right_to_left", "%s", %d}' % target
    def replace_name(match):
        return match.group("call") + '"right_to_left"'
    rewritten = ENUM_VALUE_PATTERN.sub(replace_value, line)
    # The name moves only with its value: a read, or a write whose value is
    # spelled elsewhere, compares or passes the stored enumeration and is
    # left for review.
    if rewritten != line:
        rewritten = NAME_CALL_PATTERN.sub(replace_name, rewritten)
    return rewritten


def rewriter_for(path):
    suffix = path.suffix
    if suffix == ".form":
        return rewrite_form
    if suffix in (".json", ".dml"):
        return rewrite_json
    return rewrite_cpp


def candidate_files(paths):
    for root in paths:
        root_path = pathlib.Path(root)
        if root_path.is_file():
            yield root_path
            continue
        for path in sorted(root_path.rglob("*")):
            if not path.is_file() or path.suffix not in SUFFIXES:
                continue
            if any(part in SKIPPED_DIRECTORIES for part in path.parts):
                continue
            yield path


def migrate(path, write):
    """Returns (changed line count, review lines) for one file."""
    try:
        text = path.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        return 0, []
    if "layout_direction" not in text and "layout-direction" not in text:
        return 0, []
    rewrite = rewriter_for(path)
    lines = text.splitlines(keepends=True)
    output = []
    changed = 0
    review = []
    for number, line in enumerate(lines, start=1):
        rewritten = rewrite(line)
        if rewritten != line:
            changed += 1
            print("%s:%d: %s -> %s" % (path, number, line.strip(), rewritten.strip()))
        elif '"layout_direction"' in line and path.suffix in (".cpp", ".hpp", ".h"):
            review.append("%s:%d: review: %s" % (path, number, line.strip()))
        output.append(rewritten)
    if changed and write:
        path.write_text("".join(output), encoding="utf-8")
    return changed, review


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--write", action="store_true", help="rewrite files in place")
    parser.add_argument("paths", nargs="+", help="files or directories to migrate")
    arguments = parser.parse_args(argv)
    total = 0
    reviews = []
    for path in candidate_files(arguments.paths):
        changed, review = migrate(path, arguments.write)
        total += changed
        reviews.extend(review)
    for line in reviews:
        print(line)
    verb = "rewrote" if arguments.write else "would rewrite"
    print("migrate_right_to_left: %s %d line(s); %d line(s) to review" % (verb, total, len(reviews)))
    if total and not arguments.write:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
