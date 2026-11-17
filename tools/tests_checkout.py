#!/usr/bin/env python3
"""Where GUI.Forms' tools find the gui_forms_tests checkout.

The tests live in malkuth-software/gui_forms_tests (owner, 2026-10-29 and
2026-11-17). A tool here that writes or reads a file of the tests (a
generated test source, a consumer project, a probe) finds that checkout,
in order:

  1. the path the caller passes (a tool's --tests-root);
  2. GUI_FORMS_TESTS_ROOT, which gui_forms_tests sets for every test that
     runs one of these tools from the test cache's source tree;
  3. a gui_forms_tests checkout beside this one (<workspace>/gui_forms_tests).

None when there is none: the tool then leaves the tests' files alone and
says so, and gui_forms_tests' own CI writes or checks them.
"""
from __future__ import annotations

import os
from pathlib import Path

ROOT: Path = Path(__file__).resolve().parents[1]
ENVIRONMENT_NAME: str = 'GUI_FORMS_TESTS_ROOT'


def tests_root(explicit: Path | None = None) -> Path | None:
    """The gui_forms_tests checkout (the directory holding tests/), or None."""
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit)
    named: str = os.environ.get(ENVIRONMENT_NAME, '')
    if named:
        candidates.append(Path(named))
    candidates.append(ROOT.parent / 'gui_forms_tests')
    candidate: Path
    for candidate in candidates:
        if (candidate / 'tests').is_dir():
            return candidate.resolve()
    return None
