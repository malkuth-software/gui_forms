#!/usr/bin/env python3
"""Tests for profile_hot_paths.py: reading perf report listings and naming areas.

The listings are in the shape `run` asks perf for (`perf report --stdio -t TAB
--sort dso,symbol`): comment lines, then one row per function with its
percentages, object and symbol. The inclusive listing (--children) also
carries call-tree lines, which have no percentage column of their own.
"""
from __future__ import annotations
import unittest

import profile_hot_paths
from profile_hot_paths import HotFunction

SELF_LISTING: str = '\n'.join([
    '# To display the perf.data header info, please use --header/--header-only options.',
    '#',
    '# Samples: 41K of event \'cpu-clock:ppp\'',
    '# Overhead\tShared Object\tSymbol',
    '#',
    '    21.50%\tgui_forms_paint_profile_workload\t[.] portable::lowp::blit_row_s32a_opaque',
    '    12.25%\tgui_forms_paint_profile_workload\t[.] SkRasterPipeline::run',
    '     8.00%\tgui_forms_paint_profile_workload\t[.] gui_forms::Window::paint(gui_forms::Painter&, gui_forms::Rect)',
    '     4.10%\tgui_forms_paint_profile_workload\t[.] stxlib::glyphs::Shaper::shape',
    '     3.00%\tgui_forms_paint_profile_workload\t[.] stxlib::glyphs::Typeface::draw',
    '     2.00%\tlibc.so.6\t[.] __memmove_avx_unaligned_erms',
    '     1.50%\t[kernel.kallsyms]\t[k] clear_page_erms',
    '     0.75%\tgui_forms_paint_profile_workload\t[.] std::vector<int, std::allocator<int> >::push_back',
    '     0.50%\tgui_forms_paint_profile_workload\t[.] std::vector<gui_forms::detail::DisplayCommand>::_M_realloc_append',
    '     0.40%\tgui_forms_paint_profile_workload\t[.] (anonymous namespace)::measure(gui_forms::Window&)',
    '     0.30%\tgui_forms_paint_profile_workload\t[.] aaa_fill_path(SkPathRaw const&)',
    '',
])

CHILDREN_LISTING: str = '\n'.join([
    '# Children\tSelf\tShared Object\tSymbol',
    '    64.00%\t8.00%\tgui_forms_paint_profile_workload\t[.] gui_forms::Window::paint(gui_forms::Painter&, gui_forms::Rect)',
    '            |',
    '            ---gui_forms::Window::paint(gui_forms::Painter&, gui_forms::Rect)',
    '                     --52.00%--SkRasterPipeline::run',
    '    33.75%\t12.25%\tgui_forms_paint_profile_workload\t[.] SkRasterPipeline::run',
    '',
])


class ParseTests(unittest.TestCase):
    def test_rows_keep_perf_order_and_strip_the_privilege_mark(self) -> None:
        rows: list[HotFunction] = profile_hot_paths.parse_report(SELF_LISTING)
        self.assertEqual(len(rows), 11)
        self.assertEqual(rows[0].symbol, 'portable::lowp::blit_row_s32a_opaque')
        self.assertEqual(rows[0].self_percent, 21.5)
        self.assertEqual(rows[0].inclusive_percent, 21.5)
        self.assertEqual(rows[6].dso, '[kernel.kallsyms]')
        self.assertEqual(rows[6].symbol, 'clear_page_erms')

    def test_children_rows_read_inclusive_then_self(self) -> None:
        rows: list[HotFunction] = profile_hot_paths.parse_report(CHILDREN_LISTING)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0].inclusive_percent, 64.0)
        self.assertEqual(rows[0].self_percent, 8.0)

    def test_merge_gives_self_rows_their_inclusive_share(self) -> None:
        merged: list[HotFunction] = profile_hot_paths.merge_inclusive(
            profile_hot_paths.parse_report(SELF_LISTING), profile_hot_paths.parse_report(CHILDREN_LISTING))
        self.assertEqual(merged[2].symbol, 'gui_forms::Window::paint(gui_forms::Painter&, gui_forms::Rect)')
        self.assertEqual(merged[2].self_percent, 8.0)
        self.assertEqual(merged[2].inclusive_percent, 64.0)
        # A function the inclusive listing left out keeps its self share.
        self.assertEqual(merged[3].inclusive_percent, merged[3].self_percent)

    def test_split_inclusive_rows_are_summed_and_held_above_self(self) -> None:
        # perf --children can list one function twice; the self listing once.
        self_rows: list[HotFunction] = [HotFunction('stxlib::glyphs::fit_outline', 'w', 12.35, 12.35, 'stxlib'),
                                        HotFunction('stxlib::glyphs::Typeface::draw', 'w', 3.0, 3.0, 'stxlib')]
        children_rows: list[HotFunction] = [HotFunction('stxlib::glyphs::fit_outline', 'w', 11.86, 13.56, 'stxlib'),
                                            HotFunction('stxlib::glyphs::fit_outline', 'w', 0.48, 0.61, 'stxlib'),
                                            HotFunction('stxlib::glyphs::Typeface::draw', 'w', 0.5, 1.0, 'stxlib')]
        merged: list[HotFunction] = profile_hot_paths.merge_inclusive(self_rows, children_rows)
        self.assertEqual(merged[0].inclusive_percent, 14.17)
        # An inclusive share below the self share (a split the listing cut off) is raised to it.
        self.assertEqual(merged[1].inclusive_percent, 3.0)

    def test_sample_count_reads_the_header_with_its_scale(self) -> None:
        self.assertEqual(profile_hot_paths.sample_count(SELF_LISTING), 41000)
        self.assertEqual(profile_hot_paths.sample_count("# Samples: 81  of event 'cpu-clock'"), 81)
        self.assertEqual(profile_hot_paths.sample_count('no header'), 0)

    def test_symbol_padding_and_later_columns_are_dropped(self) -> None:
        # perf 6.17 pads the symbol column, and an IPC column follows it.
        line: str = ' 30.83% \tgui_forms_paint_profile_workload\t[.] sse2::rect_memset32(unsigned int*)      \t-      -'
        rows: list[HotFunction] = profile_hot_paths.parse_report(line)
        self.assertEqual(rows[0].symbol, 'sse2::rect_memset32(unsigned int*)')
        self.assertEqual(rows[0].self_percent, 30.83)

    def test_a_listing_without_rows_is_empty(self) -> None:
        self.assertEqual(profile_hot_paths.parse_report('# no samples\n\n'), [])


class AreaTests(unittest.TestCase):
    def test_areas_by_symbol_then_object(self) -> None:
        rows: list[HotFunction] = profile_hot_paths.parse_report(SELF_LISTING)
        areas: list[str] = []
        row: HotFunction
        for row in rows:
            areas.append(row.area)
        self.assertEqual(areas, ['skia', 'skia', 'gui_forms', 'stxlib', 'stxlib', 'runtime', 'kernel', 'other',
                                 'gui_forms', 'workload', 'skia'])

    def test_area_shares_sum_self_shares_largest_first(self) -> None:
        shares: dict[str, float] = profile_hot_paths.area_shares(profile_hot_paths.parse_report(SELF_LISTING))
        self.assertEqual(list(shares)[0], 'skia')
        self.assertEqual(shares['skia'], 34.05)
        self.assertEqual(shares['gui_forms'], 8.5)

    def test_markdown_table_escapes_pipes_and_lists_top_rows(self) -> None:
        rows: list[HotFunction] = [HotFunction('operator|(a, b)', '/x/libfoo.so', 5.0, 9.0, 'other')]
        table: str = profile_hot_paths.markdown_table('paint', rows, 10)
        self.assertIn('operator\\|(a, b)', table)
        self.assertIn('| 1 | 5.00% | 9.00% | other |', table)
        self.assertIn('libfoo.so', table)
        self.assertNotIn('samples', table)

    def test_markdown_table_flags_a_workload_too_small_to_rank(self) -> None:
        rows: list[HotFunction] = [HotFunction('validate_utf8', 'w', 32.1, 32.1, 'gui_forms')]
        self.assertIn('too few to rank', profile_hot_paths.markdown_table('document_view', rows, 10, 81))
        self.assertNotIn('too few to rank', profile_hot_paths.markdown_table('paint', rows, 10, 13000))


class ConfigureTests(unittest.TestCase):
    def test_profile_flags_keep_the_toolchain_language_flags(self) -> None:
        # CMAKE_<LANG>_FLAGS would replace cmake/llvm22.cmake's _INIT flags
        # (the macOS libc++ runtime); the build-type flags carry the profile flags.
        options: list[str] = profile_hot_paths.profile_flag_options()
        option: str
        for option in options:
            self.assertNotRegex(option, r'^-DCMAKE_[A-Z]+_FLAGS=')
        self.assertEqual(options, [
            '-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG ' + profile_hot_paths.PROFILE_FLAGS,
            '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG ' + profile_hot_paths.PROFILE_FLAGS,
            '-DCMAKE_OBJCXX_FLAGS_RELEASE=-O3 -DNDEBUG ' + profile_hot_paths.PROFILE_FLAGS,
        ])


if __name__ == '__main__':
    unittest.main()
