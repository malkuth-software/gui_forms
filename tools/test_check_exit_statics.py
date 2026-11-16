"""Unit tests for check_exit_statics.py (the termination rule for static state in src/)."""
from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_exit_statics  # noqa: E402
import destructor_index  # noqa: E402

ABI: Path = Path('src/abi/stable/tables/example.cpp')


def rules(text: str, path: Path = ABI) -> list[int]:
    found: list[check_exit_statics.Finding] = check_exit_statics.check_exit_statics(path, text)
    lines: list[int] = [finding.line for finding in found]
    return lines


class ExitStaticsTests(unittest.TestCase):
    def test_audited_forms_are_findings(self) -> None:
        # A-gf-abi-03-F2 (abi_windows.cpp, abi_fonts.cpp) and A-gf-abi-01-F5
        # (drawing_c_api.cpp, src/surface/windows/drawing/drawing_platform_windows.cpp) as they were.
        text: str = '\n'.join([
            'std::vector<std::shared_ptr<OwnedWindow>>& owned_windows() {',
            '    static std::vector<std::shared_ptr<OwnedWindow>> windows;',
            '    return windows;',
            '}',
            'HeldAppFonts& held_app_fonts() {',
            '    static HeldAppFonts held{};',
            '    return held;',
            '}',
            'Registry& registry() {',
            '    static Registry value;',
            '    return value;',
            '}',
            'std::mutex lease_mutex;',
            'std::unordered_map<std::uint64_t, HdcLease> leases;',
        ])
        self.assertEqual(rules(text), [2, 6, 10, 13, 14])

    def test_never_destroyed_and_trivial_forms_pass(self) -> None:
        text: str = '\n'.join([
            'Registry& registry() {',
            '    static Registry* const value = new Registry();',
            '    return *value;',
            '}',
            'std::atomic<std::uint64_t>& counter() noexcept {',
            '    static std::atomic<std::uint64_t> counter{0U};',
            '    return counter;',
            '}',
            'const Schema& schema() {',
            '    static Schema* built = nullptr;',
            '    return *built;',
            '}',
            'bool refused() {',
            '    static const bool borrowing_refused = true;',
            '    return borrowing_refused;',
            '}',
            'static_assert(std::is_trivially_destructible_v<EndpointProbe>);',
            'const EndpointProbe& probe() {',
            '    static const EndpointProbe probe = load_endpoint_probe();',
            '    return probe;',
            '}',
            'struct Limits {',
            '    static constexpr int largest = 4;',
            '};',
            'gd_raster_service_v0 raster_service{};',
            'std::uint64_t next_lease{1U};',
        ])
        self.assertEqual(rules(text), [])

    def test_outside_src_is_skipped(self) -> None:
        text: str = 'void f() {\n    static std::vector<int> values;\n}\nstd::mutex lock;\n'
        self.assertEqual(rules(text, Path('tests/example_tests.cpp')), [])
        self.assertEqual(rules(text, Path('src/core/example.cpp')), [2, 4])
        self.assertEqual(rules(text, Path('src/abi/drawing_c_api.cpp')), [2, 4])

    def test_declarations_pointers_and_windows_scalars_pass(self) -> None:
        # Found when the scope widened to src/: static member functions,
        # a const pointer to a registered record, and Win32 scalars.
        text: str = '\n'.join([
            'struct Access {',
            '    static void run_actions(Control& control, std::span<const Action> actions);',
            '    static LRESULT answer_window_procedure(HWND window, UINT message, WPARAM w, LPARAM l);',
            '};',
            'const ControlAnatomy& anatomy() {',
            '    static const ControlAnatomy* const registered = register_anatomy();',
            '    return *registered;',
            '}',
            'void trace() {',
            '    static LONG count{};',
            '}',
        ])
        self.assertEqual(rules(text, Path('src/core/example.cpp')), [])

    def test_fixtures_mark_their_findings(self) -> None:
        # tools/testdata/exit_statics: namespace-scope objects and local statics of
        # first-party types defined in another file. A line after `// expect` is a
        # finding; every other object passes. Checked under a src/ path (the
        # fixtures' own path is out of scope, so the tree's run skips them).
        directory: Path = Path(__file__).resolve().parent / 'testdata' / 'exit_statics' / 'src'
        sources: dict[Path, str] = {}
        for path in sorted(directory.iterdir()):
            sources[Path('src/fixture') / path.name] = path.read_text(encoding='utf-8')
        types: destructor_index.TypeIndex = destructor_index.TypeIndex()
        for virtual, text in sources.items():
            types.add(virtual, text)
        for virtual, text in sources.items():
            expected: list[int] = []
            lines: list[str] = text.split('\n')
            for number, line in enumerate(lines, start=1):
                if line.strip() == '// expect':
                    expected.append(number + 1)
            found: list[int] = [finding.line for finding in check_exit_statics.check_exit_statics(virtual, text, types)]
            self.assertEqual(found, expected, str(virtual))

    def test_classification(self) -> None:
        text: str = (Path(__file__).resolve().parent / 'testdata' / 'exit_statics' / 'src' /
                     'fixture_types.hpp').read_text(encoding='utf-8')
        path: Path = Path('src/fixture/fixture_types.hpp')
        types: destructor_index.TypeIndex = destructor_index.TypeIndex()
        types.add(path, text)
        cases: dict[str, str] = {
            'Point': destructor_index.TRIVIAL,
            'Mode': destructor_index.TRIVIAL,
            'Defaulted': destructor_index.TRIVIAL,
            'Sides<bool>': destructor_index.TRIVIAL,
            'PointPair': destructor_index.TRIVIAL,
            'std::array<Point, 3>': destructor_index.TRIVIAL,
            'const Named*': destructor_index.TRIVIAL,
            'Named': destructor_index.OWNING,
            'Wrapper': destructor_index.OWNING,
            'Handle': destructor_index.OWNING,
            'Derived': destructor_index.OWNING,
            'Sides<std::string>': destructor_index.OWNING,
            'NamedList': destructor_index.OWNING,
            'std::optional<Named>': destructor_index.OWNING,
            'Sides': destructor_index.UNKNOWN,
            'ThirdParty': destructor_index.UNKNOWN,
        }
        for written, expected in cases.items():
            self.assertEqual(types.classify(written, path), expected, written)

    def test_the_sources_pass(self) -> None:
        # The tree after the fixes: every static in src/ is never destroyed or trivial,
        # with the first-party types of include/ and src/ indexed first.
        root: Path = Path(__file__).resolve().parent.parent
        suffixes: tuple[str, ...] = ('.cpp', '.hpp', '.inc', '.mm', '.h')
        sources: dict[Path, str] = {}
        for top in ('include', 'src'):
            for path in sorted((root / top).rglob('*')):
                if path.suffix in suffixes and path.is_file():
                    sources[path.relative_to(root)] = path.read_text(encoding='utf-8')
        types: destructor_index.TypeIndex = destructor_index.TypeIndex()
        for relative, text in sources.items():
            types.add(relative, text)
        findings: list[check_exit_statics.Finding] = []
        for relative, text in sources.items():
            findings.extend(check_exit_statics.check_exit_statics(relative, text, types))
        self.assertEqual([f'{finding.path}:{finding.line}' for finding in findings], [])

if __name__ == '__main__':
    unittest.main()
