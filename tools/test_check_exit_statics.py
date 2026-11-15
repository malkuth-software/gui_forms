"""Unit tests for check_exit_statics.py (the termination rule for src/abi's static state)."""
from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_exit_statics  # noqa: E402

ABI: Path = Path('src/abi/stable/tables/example.cpp')


def rules(text: str, path: Path = ABI) -> list[int]:
    found: list[check_exit_statics.Finding] = check_exit_statics.check_exit_statics(path, text)
    lines: list[int] = [finding.line for finding in found]
    return lines


class ExitStaticsTests(unittest.TestCase):
    def test_audited_forms_are_findings(self) -> None:
        # A-gf-abi-03-F2 (abi_windows.cpp, abi_fonts.cpp) and A-gf-abi-01-F5
        # (drawing_c_api.cpp, drawing_platform_windows.cpp) as they were.
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

    def test_outside_the_abi_is_skipped(self) -> None:
        text: str = 'void f() {\n    static std::vector<int> values;\n}\nstd::mutex lock;\n'
        self.assertEqual(rules(text, Path('src/core/example.cpp')), [])
        self.assertEqual(rules(text, Path('src/abi/drawing_c_api.cpp')), [2, 4])

    def test_the_abi_sources_pass(self) -> None:
        # The tree after the fixes: every static of src/abi is never destroyed or trivial.
        root: Path = Path(__file__).resolve().parent.parent
        sys.path.insert(0, str(root / 'tools'))
        findings: list[check_exit_statics.Finding] = []
        for path in sorted((root / 'src' / 'abi').rglob('*.cpp')):
            relative: Path = path.relative_to(root)
            findings.extend(check_exit_statics.check_exit_statics(relative, path.read_text(encoding='utf-8')))
        self.assertEqual([f'{finding.path}:{finding.line}' for finding in findings], [])


if __name__ == '__main__':
    unittest.main()
