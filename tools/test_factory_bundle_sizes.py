#!/usr/bin/env python3
"""Tests for factory_bundle_sizes.py's file-only parts: the faces it reads
come from the font set in the tree, so a face the set drops cannot crash it."""
from __future__ import annotations

import unittest

import factory_bundle_sizes


class RepresentativeFacesTests(unittest.TestCase):
    def test_one_face_per_family(self) -> None:
        faces: list[str] = ['PortsmouthRapids.ttf', 'PortsmouthRapids-Bold.ttf', 'Carlito-Bold.ttf',
                            'Carlito-Regular.ttf', 'NotoSansArabic-Minimal.ttf', 'Selawik-Semibold.ttf',
                            'Selawik-Bold.ttf', 'PortsmouthMono.ttf']
        self.assertEqual(factory_bundle_sizes.representative_faces(faces),
                         ['PortsmouthRapids.ttf', 'Carlito-Regular.ttf', 'NotoSansArabic-Minimal.ttf',
                          'Selawik-Semibold.ttf', 'PortsmouthMono.ttf'])

    def test_family_and_style(self) -> None:
        self.assertEqual(factory_bundle_sizes.family_and_style('NotoSansCJKjp-Subset.otf'),
                         ('NotoSansCJKjp', 'Subset'))
        self.assertEqual(factory_bundle_sizes.family_and_style('Portsmouth.ttf'), ('Portsmouth', ''))

    def test_tree_faces_exist_and_have_groups(self) -> None:
        faces: list[str] = factory_bundle_sizes.read_faces()
        self.assertTrue(faces)
        face: str
        for face in faces:
            self.assertTrue((factory_bundle_sizes.FONTS / face).is_file(), face)
        groups: list[dict] = factory_bundle_sizes.read_groups()
        factory_bundle_sizes.assign(groups, faces)
        self.assertEqual(sum(len(group['faces']) for group in groups), len(faces))
        representative: str
        for representative in factory_bundle_sizes.representative_faces(faces):
            self.assertIn(representative, faces)


if __name__ == '__main__':
    unittest.main()
