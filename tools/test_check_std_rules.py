"""Tests that check_std_rules' whole-text reading gives what the line-by-line reading gave.

strip_comments_and_literals became one regular-expression pass, and a rule's
pattern is searched once over the joined lines instead of once per line. Both
must give exactly what the earlier code gave: REFERENCE_STRIP below is the
character loop it replaced, and per_line() the search of each line.
"""
from __future__ import annotations
import importlib.util
from pathlib import Path
import random
import re
import sys
import unittest

TOOL: Path = Path(__file__).resolve().parent / "check_std_rules.py"


def load_tool():
    specification = importlib.util.spec_from_file_location("check_std_rules_under_test", TOOL)
    module = importlib.util.module_from_spec(specification)
    sys.modules["check_std_rules_under_test"] = module
    specification.loader.exec_module(module)
    return module


rules = load_tool()


def reference_strip(text: str) -> str:
    """The character loop strip_comments_and_literals replaced, unchanged."""
    result: list[str] = []
    index: int = 0
    length: int = len(text)
    while index < length:
        character: str = text[index]
        following: str = text[index + 1] if index + 1 < length else ''
        if character == '/' and following == '/':
            while index < length and text[index] != '\n':
                index += 1
            continue
        if character == '/' and following == '*':
            index += 2
            while index < length and not (text[index] == '*' and index + 1 < length and text[index + 1] == '/'):
                if text[index] == '\n':
                    result.append('\n')
                index += 1
            index += 2
            continue
        raw: re.Match[str] | None = None
        if character == 'R' and following == '"' and (index == 0 or not (text[index - 1].isalnum() or
                                                                         text[index - 1] == '_')):
            raw = re.compile(r'R"([^()\\ \n]{0,16})\(').match(text, index)
        if raw is not None:
            closing: str = ')' + raw.group(1) + '"'
            end: int = text.find(closing, raw.end())
            end = length if end < 0 else end + len(closing)
            result.append(' ')
            result.append('\n' * text.count('\n', index, end))
            index = end
            continue
        if character in ('"', "'"):
            if character == "'" and index > 0 and text[index - 1].isalnum() and following.isalnum():
                result.append(' ')
                index += 1
                continue
            quote: str = character
            result.append(' ')
            index += 1
            while index < length and text[index] != quote:
                if text[index] == '\\':
                    index += 1
                elif text[index] == '\n':
                    break
                index += 1
            index += 1
            continue
        result.append(character)
        index += 1
    return ''.join(result)


def per_line(pattern: str, lines: list[str]) -> list[int]:
    compiled: re.Pattern[str] = re.compile(pattern)
    numbers: list[int] = []
    number: int
    line: str
    for number, line in enumerate(lines, start=1):
        if compiled.search(line):
            numbers.append(number)
    return numbers


STRIP_FIXTURES: tuple[str, ...] = (
    'int a = 1\'000; // std::rand()\nchar c = \'x\'; /* std::cout\n << */ auto s = "a\\"b";\n',
    'auto r = R"x(std::rand()\n)" still)x"; u8R"(not raw)"; xR"(not raw either)";\n',
    'unterminated "literal\nnext line std::rand();\n/* open block\nto the end',
    'R"(open raw\nto the end',
    '"escaped newline \\\nkeeps going" std::clock();\n',
    'trailing backslash "\\',
    "u8'a' and 1'2 and x'y' and _'z' and \u00e9'\u00e8' and 'q\n",
    '/*/ not closed by its own star */ code; /**/ more; ///\n',
)
ALPHABET: str = '/*"\'\\R()\n a1_xé\t;:'


class StripTests(unittest.TestCase):
    def test_fixtures_strip_as_the_character_loop(self) -> None:
        fixture: str
        for fixture in STRIP_FIXTURES:
            self.assertEqual(rules.strip_comments_and_literals(fixture), reference_strip(fixture), repr(fixture))

    def test_random_text_strips_as_the_character_loop(self) -> None:
        generator: random.Random = random.Random(20261116)
        trial: int
        for trial in range(4000):
            text: str = ''.join(generator.choice(ALPHABET) for _ in range(generator.randrange(0, 40)))
            self.assertEqual(rules.strip_comments_and_literals(text), reference_strip(text), repr(text))


MATCH_LINES: tuple[str, ...] = (
    '#include <regex>', '  #  include "cstdio"', 'int x = std::rand();', 'int y = std :: rand ();',
    'engine.rand(); widget->system(1); operand(2);', 'std::', ':: rand();', 'rand', '();',
    'std::cout << 1;', 'wchar_t w;', '', '   ', 'auto t = std::clock();', 'clock ()',
    'assert(x); static_assert(y);', 'pthread_create(&t, 0, f, 0);', 'std::\tformat("{}", 1);',
)


class WholeTextTests(unittest.TestCase):
    def test_every_rule_finds_the_lines_a_line_search_finds(self) -> None:
        texts: list[list[str]] = [list(MATCH_LINES), ('x\r\nstd::rand()\r\n\x0cstd::\x0b rand()\n').splitlines(),
                                  'std\n::rand()\nrand\n(\n'.splitlines(), []]
        rule: object
        for rule in rules.RULES:
            pattern: str
            for pattern in rule.names + rule.headers:
                compiled = rules.line_pattern(pattern)
                self.assertIsNotNone(compiled.whole, pattern)
                lines: list[str]
                for lines in texts:
                    joined = rules.joined_lines(lines)
                    self.assertEqual(rules.matching_lines(compiled, lines, joined), per_line(pattern, lines),
                                     pattern + ' over ' + repr(lines))

    def test_patterns_the_rewriting_does_not_cover_stay_per_line(self) -> None:
        unsafe: str
        for unsafe in (r'\Afoo', r'foo\Z', r'\Bx', r'(a)\1', r'\x41', r'(?s)a.b', r'(?x) a', r'[\s,]x',
                       r'(?P<n>a)', 'a\nb', r'[abc'):
            self.assertIsNone(rules.line_safe(unsafe), unsafe)
        self.assertEqual(rules.line_safe(r'[^;]\s\W\D'), r'[^;\n][^\S\n][^\w\n][^\d\n]')
        self.assertEqual(rules.line_safe(r'[]a]x'), r'[]a]x')
        self.assertEqual(rules.line_safe(r'(?i)std'), r'(?i)std')
        lines: list[str] = ['a;b', 'ab', '']
        compiled = rules.line_pattern(r'\Aa;')
        self.assertIsNone(compiled.whole)
        self.assertEqual(rules.matching_lines(compiled, lines, rules.joined_lines(lines)), [1])
        negated = rules.line_pattern(r'b[^;]*$')
        self.assertEqual(rules.matching_lines(negated, lines, rules.joined_lines(lines)), per_line(r'b[^;]*$', lines))


if __name__ == "__main__":
    unittest.main()
