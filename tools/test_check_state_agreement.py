"""Unit tests for check_state_agreement.py (state whose agreement is held only by statement order)."""
from __future__ import annotations

import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_state_agreement  # noqa: E402

SOURCE: Path = Path('src/controls/example/example.cpp')


def found(text: str, rules: str = 'abc') -> list[tuple[int, str]]:
    findings: list[check_state_agreement.Finding] = check_state_agreement.check_state_agreement(SOURCE, text, rules)
    pairs: list[tuple[int, str]] = [(finding.line, finding.rule) for finding in findings]
    return pairs


class WriteBeforeRefusingCallTests(unittest.TestCase):
    def test_member_written_before_add_child_is_a_candidate(self) -> None:
        # The hunt's C11 shape: the member names a child before the child is added.
        text: str = '\n'.join([
            'void Layer::set_content(Control::Ptr content) {',
            '    content_ = content;',
            '    add_child(content_);',
            '}',
        ])
        self.assertEqual(found(text, 'a'), [(3, 'a')])

    def test_commit_last_passes(self) -> None:
        text: str = '\n'.join([
            'void Layer::set_content(Control::Ptr content) {',
            '    add_child(content);',
            '    content_ = content;',
            '}',
        ])
        self.assertEqual(found(text, 'a'), [])

    def test_a_try_block_or_a_reviewed_marker_admits_it(self) -> None:
        guarded: str = '\n'.join([
            'void Layer::set_content(Control::Ptr content) {',
            '    content_ = content;',
            '    try { add_child(content_); } catch (...) { content_.reset(); throw; }',
            '}',
        ])
        reviewed: str = '\n'.join([
            'void Layer::set_content(Control::Ptr content) {',
            '    content_ = content;',
            '    // state-agreement: reviewed add_child cannot fail for a fresh child here',
            '    add_child(content_);',
            '}',
        ])
        self.assertEqual(found(guarded, 'a'), [])
        self.assertEqual(found(reviewed, 'a'), [])

    def test_the_calls_own_result_and_a_closed_branch_are_not_writes_before_it(self) -> None:
        text: str = '\n'.join([
            'void Box::handle(Event& event) {',
            '    if (event.wheel) {',
            '        offset_ = 1.0;',
            '        return;',
            '    }',
            '    token_ = attach(link);',
            '    request_focus(lifetime);',
            '}',
        ])
        self.assertEqual(found(text, 'a'), [(7, 'a')])

    def test_capture_counts_only_when_taken_and_constructors_are_skipped(self) -> None:
        text: str = '\n'.join([
            'void Box::release() {',
            '    pressed_ = false;',
            '    set_pointer_capture(false);',
            '}',
            'Box::Box(StableId id) : Control(std::move(id)) {',
            '    editor_ = make_editor();',
            '    add_child(editor_);',
            '}',
            'void Box::press() {',
            '    pressed_ = true;',
            '    set_pointer_capture(true);',
            '}',
        ])
        self.assertEqual(found(text, 'a'), [(11, 'a')])

    def test_comments_and_strings_are_not_read(self) -> None:
        text: str = '\n'.join([
            'void Box::press() {',
            '    // pressed_ = true; add_child(x);',
            '    log("content_ = x; add_child(y)");',
            '}',
        ])
        self.assertEqual(found(text, 'a'), [])


class TeardownPathTests(unittest.TestCase):
    def test_two_teardowns_clearing_different_sets_are_a_candidate(self) -> None:
        text: str = '\n'.join([
            'void Popup::close() {',
            '    shown_ = false;',
            '    layer_.reset();',
            '    scope_ = {};',
            '}',
            'void Popup::on_overlay_revoked() {',
            '    shown_ = false;',
            '    overlay_.reset();',
            '}',
        ])
        self.assertEqual(found(text, 'b'), [(6, 'b')])

    def test_a_narrower_teardown_or_an_equal_one_passes(self) -> None:
        text: str = '\n'.join([
            'void Popup::close() {',
            '    shown_ = false;',
            '    layer_.reset();',
            '}',
            'void Popup::on_overlay_revoked() {',
            '    shown_ = false;',
            '    layer_.reset();',
            '}',
            'void Popup::hide() {',
            '    shown_ = false;',
            '}',
        ])
        self.assertEqual(found(text, 'b'), [])


class CachedEarlyReturnTests(unittest.TestCase):
    def test_apply_returning_on_its_cache_is_a_candidate(self) -> None:
        # The hunt's C24 shape.
        text: str = '\n'.join([
            'void View::apply_display_mode(Mode mode) {',
            '    if (effective_mode_ == mode) return;',
            '    effective_mode_ = mode;',
            '}',
        ])
        self.assertEqual(found(text, 'c'), [(2, 'c')])

    def test_a_setter_no_op_guard_passes(self) -> None:
        text: str = '\n'.join([
            'void View::set_mode(Mode mode) {',
            '    if (mode_ == mode) return;',
            '    mode_ = mode;',
            '}',
        ])
        self.assertEqual(found(text, 'c'), [])


class ReportOnlyTests(unittest.TestCase):
    def test_exit_status_is_zero_unless_strict(self) -> None:
        with tempfile.TemporaryDirectory() as held:
            directory: Path = Path(held)
            sample: Path = directory / 'sample.cpp'
            sample.write_text('void A::set(P p) {\n    p_ = p;\n    add_child(p_);\n}\n', encoding='utf-8')
            with contextlib.redirect_stdout(io.StringIO()):
                quiet: int = check_state_agreement.main([str(directory)])
                strict: int = check_state_agreement.main(['--strict', str(directory)])
            self.assertEqual(quiet, 0)
            self.assertEqual(strict, 1)

if __name__ == '__main__':
    unittest.main()
