"""Unit tests for naming_audit.py (the sensible-naming audit)."""
from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import naming_audit  # noqa: E402

HEADER: str = 'include/gui_forms/platform/example_host.hpp'


def findings_for(text: str, path: str = HEADER) -> list[naming_audit.Finding]:
    declarations: list[naming_audit.Declaration] = naming_audit.gather_declarations(path, text)
    found: list[naming_audit.Finding] = naming_audit.audit_declarations(declarations)
    return found


def names(found: list[naming_audit.Finding]) -> list[str]:
    listed: list[str] = [finding.declaration.name for finding in found]
    return listed


def declared(text: str) -> dict[str, str]:
    declarations: list[naming_audit.Declaration] = naming_audit.gather_declarations(HEADER, text)
    kinds: dict[str, str] = {}
    for declaration in declarations:
        kinds[declaration.name] = declaration.kind
    return kinds


class GatheringTests(unittest.TestCase):
    def test_declaration_kinds(self) -> None:
        text: str = '\n'.join([
            'namespace gui_forms::host {',
            'enum class Side : std::uint8_t { near, center, far };',
            'struct Record final {',
            '    int width{};',
            '    double scale() const;',
            '};',
            'using Alias = Record;',
            'int run(std::vector<Record> windows, const Record& record = {}) {',
            '    const int total = windows.size();',
            '    return total;',
            '}',
            '} // namespace gui_forms::host',
        ])
        kinds: dict[str, str] = declared(text)
        self.assertEqual(kinds['gui_forms::host'], 'namespace')
        self.assertEqual(kinds['Side'], 'type')
        self.assertEqual(kinds['near'], 'enumerator')
        self.assertEqual(kinds['far'], 'enumerator')
        self.assertEqual(kinds['Record'], 'type')
        self.assertEqual(kinds['width'], 'member')
        self.assertEqual(kinds['scale'], 'function')
        self.assertEqual(kinds['Alias'], 'type')
        self.assertEqual(kinds['run'], 'function')
        self.assertEqual(kinds['windows'], 'parameter')
        self.assertEqual(kinds['record'], 'parameter')
        self.assertEqual(kinds['total'], 'local')

    def test_comments_literals_and_directives_are_masked(self) -> None:
        text: str = '\n'.join([
            '#define linux_value(x) x',
            '// int linux;',
            'const char* text = "int linux;";',
            '/* std::vector<int> linux; */',
        ])
        self.assertNotIn('linux', declared(text))

    def test_calls_and_expressions_are_not_declarations(self) -> None:
        text: str = '\n'.join([
            'void f() {',
            '    const bool ready = alive && is_alive(state);',
            '    g(mask & NSWindowStyleMaskTitled);',
            '    return value;',
            '}',
        ])
        kinds: dict[str, str] = declared(text)
        self.assertNotIn('is_alive', kinds)
        self.assertNotIn('NSWindowStyleMaskTitled', kinds)
        self.assertEqual(kinds['ready'], 'local')


class PlatformWordTests(unittest.TestCase):
    def test_model_case_is_flagged_with_a_proposal(self) -> None:
        # include/gui_forms/platform/linux_host.hpp:110 before the cleanup.
        text: str = 'int run_linux_application(std::vector<LinuxApplicationWindow> linux, LinuxApplicationOptions application = {});'
        found: list[naming_audit.Finding] = findings_for(text, 'include/gui_forms/platform/linux_host.hpp')
        self.assertEqual(names(found), ['linux'])
        self.assertEqual(found[0].rule, 'platform')
        self.assertEqual(found[0].rank, 100)
        self.assertEqual(found[0].proposal, 'application_windows')

    def test_proposal_comes_from_the_type(self) -> None:
        text: str = 'void close(std::span<const CocoaPanelHandle> cocoa);'
        found: list[naming_audit.Finding] = findings_for(text)
        self.assertEqual(found[0].proposal, 'panel_handles')

    def test_platform_typed_value_ranks_lower(self) -> None:
        text: str = 'void f(HostServices& services) { AppKitHostServices* const appkit = cast(&services); }'
        found: list[naming_audit.Finding] = findings_for(text, 'src/host/macos/services/example.mm')
        self.assertEqual(names(found), ['appkit'])
        self.assertEqual(found[0].rank, 60)
        self.assertEqual(found[0].proposal, 'host_services')

    def test_types_namespaces_enumerators_are_not_flagged(self) -> None:
        text: str = '\n'.join([
            'namespace linux { }',
            'enum class HostPlatform { windows, macos, linux };',
            'struct Wayland final { };',
        ])
        self.assertEqual(findings_for(text), [])

    def test_compound_needs_an_unrelated_subject(self) -> None:
        related: str = 'void f(const X11Connection& x11_connection);'
        self.assertEqual(findings_for(related), [])
        unrelated: str = 'struct Margins final { double gtk_padding{}; };'
        found: list[naming_audit.Finding] = findings_for(unrelated, 'include/gui_forms/controls/margins.hpp')
        self.assertEqual(names(found), ['gtk_padding'])
        self.assertEqual(found[0].rule, 'foreign')

    def test_convention_words_describe_a_style(self) -> None:
        text: str = 'const ThemeSheet macos_style_sheet{};'
        self.assertEqual(findings_for(text, 'src/core/theme/familiar.cpp'), [])

    def test_library_words_are_not_products(self) -> None:
        text: str = 'void apply(const WindowChrome& chrome, double metal);'
        self.assertEqual(findings_for(text), [])

    def test_kept_and_std_names_are_skipped(self) -> None:
        text: str = 'enum class Size { large, small }; struct S { double near{}; double far{}; int max{}; };'
        self.assertEqual(findings_for(text), [])


class HandleAndPromiseTests(unittest.TestCase):
    def test_handle_term_needs_its_type(self) -> None:
        wrong: str = 'void paint(const Rect& hdc);'
        self.assertEqual(names(findings_for(wrong)), ['hdc'])
        right: str = 'void paint(HDC hdc, std::uintptr_t* hbitmap);'
        self.assertEqual(findings_for(right), [])

    def test_predicate_names_promise_a_truth_value(self) -> None:
        wrong: str = 'struct S { int is_visible{}; };'
        found: list[naming_audit.Finding] = findings_for(wrong, 'include/gui_forms/s.hpp')
        self.assertEqual(names(found), ['is_visible'])
        self.assertEqual(found[0].rule, 'promise')
        right: str = 'struct S { bool is_visible{}; ShouldSerialize should_serialize; };'
        self.assertEqual(findings_for(right, 'include/gui_forms/s.hpp'), [])

    def test_c_abi_integer_truth_values_pass(self) -> None:
        text: str = 'typedef struct gf_example { uint32_t has_colour_matrix; } gf_example;'
        self.assertEqual(findings_for(text, 'include/gui_forms/c/gf_abi.h'), [])

    def test_counts_in_floating_point_rank_low(self) -> None:
        text: str = 'void f() { const double row_count = 2.0; }'
        found: list[naming_audit.Finding] = findings_for(text, 'src/core/f.cpp')
        self.assertEqual(names(found), ['row_count'])
        self.assertEqual(found[0].rank, 20)


class RenderTests(unittest.TestCase):
    def test_markdown_table_escapes_pipes(self) -> None:
        declaration: naming_audit.Declaration = naming_audit.Declaration('x', 'local', 'a.cpp', 3, 'A|B', '')
        finding: naming_audit.Finding = naming_audit.Finding(declaration, 'platform', 80, 'reads | wrong', 'y')
        report: str = naming_audit.render([finding], 1, True)
        self.assertIn('`A\\|B`', report)
        self.assertIn('reads \\| wrong', report)


if __name__ == '__main__':
    unittest.main()
