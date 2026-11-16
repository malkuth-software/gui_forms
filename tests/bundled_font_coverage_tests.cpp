// The compiled coverage tables of the bundled fallback faces (item 4, step 5). For each fallback
// face, the table must be exactly the set of Unicode scalars STXlib's font reader maps to a
// glyph in the face's Unicode character map (glyph_of, which the text engine's coverage
// reads), so loading a fallback face only when its table covers a cluster selects the same
// face the engine would have selected with every face loaded. Checked two ways: the table's
// ranges equal the map's enumeration (next_stretch and glyph_in_stretch), and every scalar
// from 0 to 0x10FFFF agrees with glyph_of. The table must also name the manifest digest of the
// face it describes.
//
// `gui_forms_bundled_font_coverage_tests --write <bundled_font_coverage.inc>` regenerates the
// tables from the faces in assets/fonts (as does `tools/fallback_font_subsets.py coverage`).
//
// The tables are also checked against the slim fallback layout (owner, 2026-11-16;
// tools/fallback_font_subsets.py): the fallback order, a proper Japanese face (kana and JIS X
// 0208 kanji, no Hangul), a proper Korean face (every Hangul syllable and the conjoining jamo),
// a minimal Chinese face holding only hanzi neither of those holds, and minimal Arabic and
// Indic faces with no presentation forms and nothing outside their scripts.

#include "../src/core/resources/fonts/bundled_font_set.hpp"

#include <stxlib/font_file.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gui_forms::detail::BundledFaceRecord;
using gui_forms::detail::BundledFaceUse;
using gui_forms::detail::CodepointRange;

constexpr char32_t last_scalar = 0x10FFFFU;

// One face read with its Unicode character map, as the engine opens it: get() is null when
// STXlib refuses the face or it has no Unicode map. It borrows `bytes`.
class Face final {
public:
    explicit Face(const std::vector<unsigned char>& bytes) {
        const stxlib::View<stxlib::u8> view(bytes.data(), bytes.size());
        const stxlib::Status opened = stxlib::glyphs::open_font(view, 0U, font_);
        open_ = opened.succeeded() && font_.character_map.length != 0U &&
                font_.character_map_kind == stxlib::glyphs::character_map_unicode;
    }
    Face(const Face&) = delete;
    Face& operator=(const Face&) = delete;
    [[nodiscard]] const stxlib::glyphs::Font* get() const noexcept { return open_ ? &font_ : nullptr; }

private:
    stxlib::glyphs::Font font_{};
    bool open_{};
};

std::vector<unsigned char> read_face(std::string_view file_name) {
    const std::filesystem::path path =
        std::filesystem::path(GUI_FORMS_TEST_FONT_DIRECTORY) / std::filesystem::path(file_name);
    std::ifstream stream(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());
    return bytes;
}

// The scalars the face's character map maps to a glyph, as sorted disjoint ranges: each
// stretch the map has an entry for, without the scalars whose glyph is zero.
std::vector<CodepointRange> enumerate(const stxlib::glyphs::Font& font) {
    std::vector<CodepointRange> ranges;
    stxlib::glyphs::MappedStretch stretch{};
    std::uint32_t from = 0U;
    while (from <= last_scalar && stxlib::glyphs::next_stretch(font, from, stretch)) {
        const std::uint32_t last = stretch.last < last_scalar ? stretch.last : static_cast<std::uint32_t>(last_scalar);
        for (std::uint32_t code = stretch.first < from ? from : stretch.first; code <= last; ++code) {
            if (stxlib::glyphs::glyph_in_stretch(font, stretch, code) == 0U) continue;
            const char32_t scalar = static_cast<char32_t>(code);
            if (!ranges.empty() && ranges.back().last + 1U == scalar) {
                ranges.back().last = scalar;
            } else {
                ranges.push_back(CodepointRange{scalar, scalar});
            }
        }
        if (stretch.last >= last_scalar) break;
        from = stretch.last + 1U;
    }
    return ranges;
}

std::optional<std::string_view> manifest_digest(std::string_view file_name) {
    for (const BundledFaceRecord& record : gui_forms::detail::bundled_face_manifest()) {
        if (record.file_name == file_name) return record.sha256_hex;
    }
    return std::nullopt;
}

struct FaceCoverage final {
    std::string_view file_name;
    std::string_view digest;
    std::vector<CodepointRange> ranges;
};

// Every fallback face's coverage, in registration order; empty on a failure.
std::vector<FaceCoverage> measure() {
    std::vector<FaceCoverage> result;
    for (const BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        if (use.role) continue;
        const std::optional<std::string_view> digest = manifest_digest(use.file_name);
        const std::vector<unsigned char> bytes = read_face(use.file_name);
        const Face face(bytes);
        if (!digest || bytes.empty() || face.get() == nullptr) {
            std::printf("bundled_font_coverage: cannot open %.*s\n",
                        static_cast<int>(use.file_name.size()), use.file_name.data());
            return {};
        }
        result.push_back(FaceCoverage{use.file_name, *digest, enumerate(*face.get())});
    }
    return result;
}

bool write_tables(const std::vector<FaceCoverage>& coverages, const char* path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "// Generated by `gui_forms_bundled_font_coverage_tests --write <this file>`; do not edit.\n";
    out << "// Each table: the Unicode scalars the face's Unicode character map maps to a glyph (STXlib glyph_of).\n";
    for (std::size_t index = 0; index < coverages.size(); ++index) {
        const FaceCoverage& coverage = coverages[index];
        out << "// " << coverage.file_name << ", " << coverage.ranges.size() << " ranges\n";
        out << "constexpr CodepointRange coverage_" << index << "[] = {\n";
        char line[64];
        for (std::size_t range = 0; range < coverage.ranges.size(); ++range) {
            std::snprintf(line, sizeof(line), "%s{0x%lXU, 0x%lXU},", range % 4U == 0U ? "    " : " ",
                          static_cast<unsigned long>(coverage.ranges[range].first),
                          static_cast<unsigned long>(coverage.ranges[range].last));
            out << line;
            if (range % 4U == 3U || range + 1U == coverage.ranges.size()) out << "\n";
        }
        out << "};\n";
    }
    out << "constexpr std::array<BundledFaceCoverage, " << coverages.size() << "> generated_coverages{{\n";
    for (std::size_t index = 0; index < coverages.size(); ++index) {
        out << "    {\"" << coverages[index].file_name << "\", \"" << coverages[index].digest
            << "\", coverage_" << index << "},\n";
    }
    out << "}};\n";
    out.close();
    const bool written = !out.fail();
    return written;
}

bool same_ranges(std::span<const CodepointRange> left, const std::vector<CodepointRange>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].first != right[index].first || left[index].last != right[index].last) return false;
    }
    return true;
}

bool check_tables(const std::vector<FaceCoverage>& coverages) {
    if (gui_forms::detail::bundled_face_coverages().size() != coverages.size()) {
        std::printf("bundled_font_coverage: %zu tables for %zu fallback faces; regenerate\n",
                    gui_forms::detail::bundled_face_coverages().size(), coverages.size());
        return false;
    }
    for (const FaceCoverage& measured : coverages) {
        const std::optional<std::span<const CodepointRange>> table =
            gui_forms::detail::bundled_face_coverage(measured.file_name);
        if (!table || !same_ranges(*table, measured.ranges)) {
            std::printf("bundled_font_coverage: %.*s's table is missing, stale or wrong; regenerate\n",
                        static_cast<int>(measured.file_name.size()), measured.file_name.data());
            return false;
        }
        const std::vector<unsigned char> bytes = read_face(measured.file_name);
        const Face face(bytes);
        if (face.get() == nullptr) return false;
        for (char32_t scalar = 0; scalar <= last_scalar; ++scalar) {
            const bool mapped = stxlib::glyphs::glyph_of(*face.get(), static_cast<std::uint32_t>(scalar)) != 0U;
            if (mapped != gui_forms::detail::coverage_contains(*table, scalar)) {
                std::printf("bundled_font_coverage: %.*s disagrees with glyph_of at U+%04lX\n",
                            static_cast<int>(measured.file_name.size()), measured.file_name.data(),
                            static_cast<unsigned long>(scalar));
                return false;
            }
        }
    }
    // A face without a table, and a primary, have no coverage.
    const bool primaries_absent = !gui_forms::detail::bundled_face_coverage("Carlito-Regular.ttf") &&
                                  !gui_forms::detail::bundled_face_coverage("no-such-face.ttf");
    if (!primaries_absent) std::printf("bundled_font_coverage: a face without a table reported coverage\n");
    return primaries_absent;
}

// The fallback faces in the order bundled_face_uses registers them (FALLBACK_ORDER in
// tools/fallback_font_subsets.py).
constexpr std::array<std::string_view, 9> fallback_order{
    "NotoSansCJKjp-Subset.otf",       "NotoSansCJKkr-Subset.otf",   "NotoSansCJKsc-Minimal.otf",
    "NotoSansArabic-Minimal.ttf",     "NotoSansHebrew-Regular.ttf", "NotoSansDevanagari-Minimal.ttf",
    "NotoSansBengali-Minimal.ttf",    "NotoSansGurmukhi-Minimal.ttf", "NotoEmoji-Regular.ttf"};

// The separators every minimal face keeps beside its script: space, no-break space, ZWNJ, ZWJ.
constexpr std::array<CodepointRange, 3> separators{{
    {0x20U, 0x20U}, {0xA0U, 0xA0U}, {0x200CU, 0x200DU}}};

std::span<const CodepointRange> table_of(std::string_view file_name) {
    const std::optional<std::span<const CodepointRange>> table = gui_forms::detail::bundled_face_coverage(file_name);
    if (!table) return {};
    return *table;
}

bool report(bool passed, const char* what) {
    if (!passed) std::printf("bundled_font_coverage: %s\n", what);
    return passed;
}

// Whether every scalar of first..last is in `table`.
bool covers_all(std::span<const CodepointRange> table, char32_t first, char32_t last) {
    for (char32_t scalar = first; scalar <= last; ++scalar) {
        if (!gui_forms::detail::coverage_contains(table, scalar)) return false;
    }
    return true;
}

// Whether no scalar of first..last is in `table`.
bool covers_none(std::span<const CodepointRange> table, char32_t first, char32_t last) {
    for (char32_t scalar = first; scalar <= last; ++scalar) {
        if (gui_forms::detail::coverage_contains(table, scalar)) return false;
    }
    return true;
}

// Whether every scalar of `table` lies in `script` (first..last) or is a separator.
bool covers_only(std::span<const CodepointRange> table, char32_t first, char32_t last) {
    const std::span<const CodepointRange> kept(separators);
    for (const CodepointRange& range : table) {
        for (char32_t scalar = range.first; scalar <= range.last; ++scalar) {
            const bool in_script = scalar >= first && scalar <= last;
            if (!in_script && !gui_forms::detail::coverage_contains(kept, scalar)) return false;
        }
    }
    return true;
}

// Whether no scalar of `table` is in `other`.
bool disjoint(std::span<const CodepointRange> table, std::span<const CodepointRange> other) {
    for (const CodepointRange& range : table) {
        for (char32_t scalar = range.first; scalar <= range.last; ++scalar) {
            if (gui_forms::detail::coverage_contains(other, scalar)) return false;
        }
    }
    return true;
}

bool check_slim_layout() {
    std::size_t position = 0;
    bool passed = true;
    for (const BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        if (use.role) continue;
        const bool expected = position < fallback_order.size() && use.file_name == fallback_order[position];
        passed = report(expected, "the fallback faces are not registered in the slim layout's order") && passed;
        ++position;
    }
    passed = report(position == fallback_order.size(), "not nine fallback faces") && passed;

    const std::span<const CodepointRange> japanese = table_of("NotoSansCJKjp-Subset.otf");
    const std::span<const CodepointRange> korean = table_of("NotoSansCJKkr-Subset.otf");
    const std::span<const CodepointRange> chinese = table_of("NotoSansCJKsc-Minimal.otf");
    passed = report(covers_all(japanese, 0x3041U, 0x3096U) && covers_all(japanese, 0x30A1U, 0x30FAU),
                    "the Japanese face lacks kana") && passed;
    passed = report(covers_all(japanese, 0x65E5U, 0x65E5U) && covers_all(japanese, 0x672CU, 0x672CU) &&
                        covers_all(japanese, 0x8A9EU, 0x8A9EU) && covers_all(japanese, 0x6F22U, 0x6F22U),
                    "the Japanese face lacks a JIS X 0208 level 1 kanji") && passed;
    passed = report(covers_none(japanese, 0xAC00U, 0xD7A3U), "the Japanese face holds Hangul") && passed;
    passed = report(covers_all(korean, 0xAC00U, 0xD7A3U), "the Korean face lacks a Hangul syllable") && passed;
    passed = report(covers_all(korean, 0x1100U, 0x11FFU) && covers_all(korean, 0x3131U, 0x318EU),
                    "the Korean face lacks a jamo") && passed;
    passed = report(covers_all(korean, 0x97D3U, 0x97D3U), "the Korean face lacks the KS X 1001 hanja for Han") &&
             passed;
    passed = report(covers_all(chinese, 0x4EECU, 0x4EECU) && covers_all(chinese, 0x8FD9U, 0x8FD9U),
                    "the Chinese face lacks a common simplified hanzi") && passed;
    passed = report(!chinese.empty() && disjoint(chinese, japanese) && disjoint(chinese, korean),
                    "the Chinese face repeats a character the Japanese or Korean face holds") && passed;

    const std::span<const CodepointRange> arabic = table_of("NotoSansArabic-Minimal.ttf");
    passed = report(covers_all(arabic, 0x0621U, 0x063AU) && covers_all(arabic, 0x0641U, 0x064AU) &&
                        covers_all(arabic, 0x0660U, 0x0669U),
                    "the Arabic face lacks a basic letter or digit") && passed;
    passed = report(covers_only(arabic, 0x0600U, 0x06FFU), "the Arabic face holds presentation forms or another script") &&
             passed;
    passed = report(covers_all(table_of("NotoSansDevanagari-Minimal.ttf"), 0x0915U, 0x0939U) &&
                        covers_only(table_of("NotoSansDevanagari-Minimal.ttf"), 0x0900U, 0x097FU),
                    "the Devanagari face is not its basic block") && passed;
    passed = report(covers_all(table_of("NotoSansBengali-Minimal.ttf"), 0x0995U, 0x09A8U) &&
                        covers_only(table_of("NotoSansBengali-Minimal.ttf"), 0x0964U, 0x09FFU),
                    "the Bengali face is not its basic block") && passed;
    passed = report(covers_all(table_of("NotoSansGurmukhi-Minimal.ttf"), 0x0A15U, 0x0A28U) &&
                        covers_only(table_of("NotoSansGurmukhi-Minimal.ttf"), 0x0964U, 0x0A7FU),
                    "the Gurmukhi face is not its basic block") && passed;
    return passed;
}

} // namespace

int main(int argument_count, char** arguments) {
    const std::vector<FaceCoverage> coverages = measure();
    if (coverages.empty()) return 1;
    if (argument_count == 3 && std::string_view(arguments[1]) == "--write") {
        const bool written = write_tables(coverages, arguments[2]);
        return written ? 0 : 1;
    }
    if (!check_tables(coverages)) return 1;
    if (!check_slim_layout()) return 1;
    std::printf("bundled_font_coverage: %zu fallback tables match the character maps\n", coverages.size());
    return 0;
}
