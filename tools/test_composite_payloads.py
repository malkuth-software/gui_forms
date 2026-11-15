"""Composite event payloads speak the C++ events' payload words (cp33).

The control schema writes a composite event's payload in the C++ spelling
(``bool``, not the control language's ``boolean``), and the C ABI maps
payloads through ``manifests/abi/interaction_fields.json``. These checks keep
one vocabulary:

- every composite event payload in ``manifests/control-schema.json`` has an
  entry in ``interaction_fields.json``;
- every word ``control_language::control_payload_type_name`` can give
  (``payload_type_words`` in control_model.cpp) has one too, and
  ``tools/abi_event_catalog.py``'s COMPOSITE_PAYLOADS lists exactly those
  words, so the generator keeps them mapped.

Run: ``python3 -B -m unittest discover -s tools -p test_composite_payloads.py``.
"""

from __future__ import annotations

import json
import re
import sys
import unittest
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))

import abi_event_catalog  # noqa: E402

SCHEMA: Path = ROOT / 'manifests' / 'control-schema.json'
FIELDS: Path = ROOT / 'manifests' / 'abi' / 'interaction_fields.json'
CONTROL_MODEL: Path = ROOT / 'src' / 'core' / 'control_language' / 'control_model' / 'control_model.cpp'


def mapped_payloads() -> dict[str, str]:
    with FIELDS.open(encoding='utf-8') as handle:
        manifest: dict[str, object] = json.load(handle)
    payloads: object = manifest['payloads']
    assert isinstance(payloads, dict)
    return payloads


def payload_type_words() -> list[str]:
    """The strings of control_model.cpp's payload_type_words array, in order."""
    text: str = CONTROL_MODEL.read_text(encoding='utf-8')
    found: re.Match[str] | None = re.search(r'payload_type_words\s*=\s*\{(.*?)\};', text, re.DOTALL)
    if found is None:
        raise AssertionError('payload_type_words is not in control_model.cpp')
    words: list[str] = re.findall(r'"([^"]*)"', found.group(1))
    return words


class CompositePayloadTests(unittest.TestCase):
    def test_schema_composite_payloads_are_mapped(self) -> None:
        with SCHEMA.open(encoding='utf-8') as handle:
            schema: dict[str, object] = json.load(handle)
        composites: object = schema.get('composites', [])
        self.assertIsInstance(composites, list)
        mapped: dict[str, str] = mapped_payloads()
        checked: int = 0
        composite: object
        for composite in composites:
            self.assertIsInstance(composite, dict)
            event: object
            for event in composite.get('events', []):
                self.assertIsInstance(event, dict)
                payload: object = event.get('payload')
                if payload is None:
                    continue
                self.assertIn(payload, mapped,
                              f'{composite.get("name")}: event {event.get("name")} payload {payload!r} '
                              'has no entry in interaction_fields.json')
                checked += 1
        self.assertGreater(checked, 0, 'the schema lists no composite payload to check')

    def test_every_payload_word_is_mapped(self) -> None:
        words: list[str] = payload_type_words()
        self.assertEqual(len(words), 14, 'one word for each ControlPropertyKind')
        mapped: dict[str, str] = mapped_payloads()
        word: str
        for word in words:
            self.assertIn(word, mapped, f'payload word {word!r} has no entry in interaction_fields.json')

    def test_generator_lists_the_payload_words(self) -> None:
        self.assertEqual(set(payload_type_words()), set(abi_event_catalog.COMPOSITE_PAYLOADS))

    def test_no_control_language_kind_words(self) -> None:
        # The kind words a document writes never reach the schema as payloads.
        kind_words: set[str] = {'boolean', 'number', 'signed-integer', 'unsigned-integer', 'text',
                                'enumeration', 'color', 'font', 'image', 'size', 'point', 'rectangle',
                                'insets', 'command'}
        self.assertFalse(kind_words & set(payload_type_words()))


if __name__ == '__main__':
    unittest.main()
