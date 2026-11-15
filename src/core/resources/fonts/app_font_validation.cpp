// The table checks an application font passes before it is loaded
// (validate_app_font, gui_forms/typography/app_fonts/app_fonts.hpp, which
// lists them). Every read is bounds-checked against the bytes first; nothing
// here allocates, and no font engine sees the bytes.
//
// The sfnt layout read here (OpenType 1.9, "Organization of an OpenType
// font"): a 12-byte header (sfntVersion, numTables, three search fields),
// then numTables 16-byte table records (tag, checksum, offset, length), all
// big-endian. Table checksums are not verified: a wrong checksum is common in
// usable fonts and proves nothing about safety.

#include "gui_forms/typography/app_fonts/app_fonts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms {
namespace {

// A table tag from its four characters.
constexpr std::uint32_t tag(char first, char second, char third, char fourth) noexcept {
    const std::uint32_t value = (static_cast<std::uint32_t>(static_cast<unsigned char>(first)) << 24U) |
                                (static_cast<std::uint32_t>(static_cast<unsigned char>(second)) << 16U) |
                                (static_cast<std::uint32_t>(static_cast<unsigned char>(third)) << 8U) |
                                static_cast<std::uint32_t>(static_cast<unsigned char>(fourth));
    return value;
}

constexpr std::uint32_t truetype_version = 0x00010000U;
constexpr std::uint32_t apple_truetype_version = tag('t', 'r', 'u', 'e');
constexpr std::uint32_t cff_version = tag('O', 'T', 'T', 'O');
constexpr std::uint32_t collection_version = tag('t', 't', 'c', 'f');
constexpr std::uint32_t head_magic = 0x5F0F3CF5U;
constexpr std::size_t header_bytes = 12U;
constexpr std::size_t table_record_bytes = 16U;
constexpr std::uint16_t maximum_tables = 64U;

constexpr std::uint32_t head_tag = tag('h', 'e', 'a', 'd');
constexpr std::uint32_t hhea_tag = tag('h', 'h', 'e', 'a');
constexpr std::uint32_t hmtx_tag = tag('h', 'm', 't', 'x');
constexpr std::uint32_t maxp_tag = tag('m', 'a', 'x', 'p');
constexpr std::uint32_t cmap_tag = tag('c', 'm', 'a', 'p');
constexpr std::uint32_t name_tag = tag('n', 'a', 'm', 'e');
constexpr std::uint32_t post_tag = tag('p', 'o', 's', 't');
constexpr std::uint32_t glyf_tag = tag('g', 'l', 'y', 'f');
constexpr std::uint32_t loca_tag = tag('l', 'o', 'c', 'a');
constexpr std::uint32_t cff_tag = tag('C', 'F', 'F', ' ');
constexpr std::uint32_t cff2_tag = tag('C', 'F', 'F', '2');

// One table's place in the bytes, already checked to lie inside them.
struct TableSpan final {
    std::size_t offset{};
    std::size_t length{};
    bool present{};
};

// Big-endian reads at `offset`; the caller has checked offset + width <= size.
[[nodiscard]] std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    const std::uint16_t high = static_cast<std::uint16_t>(bytes[offset]);
    const std::uint16_t low = static_cast<std::uint16_t>(bytes[offset + 1U]);
    const std::uint16_t value = static_cast<std::uint16_t>((high << 8U) | low);
    return value;
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    const std::uint32_t high = read_u16(bytes, offset);
    const std::uint32_t low = read_u16(bytes, offset + 2U);
    const std::uint32_t value = (high << 16U) | low;
    return value;
}

// Whether [offset, offset + length) lies inside `size` bytes, without overflow.
[[nodiscard]] bool inside(std::size_t offset, std::size_t length, std::size_t size) noexcept {
    if (offset > size) return false;
    const bool fits = length <= size - offset;
    return fits;
}

[[nodiscard]] AppFontValidation refused(AppFontStatus status, std::uint32_t table = 0U) noexcept {
    AppFontValidation result{};
    result.status = status;
    result.table = table;
    return result;
}

// The table directory's records for the tags the checks read, found by a scan
// that also refuses a record outside the bytes or a tag listed twice.
struct TableDirectory final {
    TableSpan head{};
    TableSpan hhea{};
    TableSpan hmtx{};
    TableSpan maxp{};
    TableSpan cmap{};
    TableSpan name{};
    TableSpan post{};
    TableSpan glyf{};
    TableSpan loca{};
    TableSpan cff{};
    TableSpan cff2{};
};

// The directory's slot for `table_tag`, or null for a tag the checks do not read.
[[nodiscard]] TableSpan* directory_slot(TableDirectory& directory, std::uint32_t table_tag) noexcept {
    switch (table_tag) {
    case head_tag: return &directory.head;
    case hhea_tag: return &directory.hhea;
    case hmtx_tag: return &directory.hmtx;
    case maxp_tag: return &directory.maxp;
    case cmap_tag: return &directory.cmap;
    case name_tag: return &directory.name;
    case post_tag: return &directory.post;
    case glyf_tag: return &directory.glyf;
    case loca_tag: return &directory.loca;
    case cff_tag: return &directory.cff;
    case cff2_tag: return &directory.cff2;
    default: break;
    }
    return nullptr;
}

// A table every face needs, whatever its outlines: its tag and its place in the directory.
struct RequiredTable final {
    std::uint32_t tag{};
    TableSpan TableDirectory::*member{};
};

constexpr std::array<RequiredTable, 7> common_tables{{
    {head_tag, &TableDirectory::head},
    {hhea_tag, &TableDirectory::hhea},
    {hmtx_tag, &TableDirectory::hmtx},
    {maxp_tag, &TableDirectory::maxp},
    {cmap_tag, &TableDirectory::cmap},
    {name_tag, &TableDirectory::name},
    {post_tag, &TableDirectory::post},
}};

// Checks 3: reads the records into `directory`.
[[nodiscard]] AppFontValidation read_directory(std::span<const std::byte> bytes, std::uint16_t table_count,
                                               TableDirectory& directory) noexcept {
    const std::size_t records_bytes = static_cast<std::size_t>(table_count) * table_record_bytes;
    if (!inside(header_bytes, records_bytes, bytes.size())) {
        return refused(AppFontStatus::table_directory_invalid);
    }
    std::array<std::uint32_t, maximum_tables> seen{};
    for (std::size_t index = 0U; index < table_count; ++index) {
        const std::size_t record = header_bytes + index * table_record_bytes;
        const std::uint32_t table_tag = read_u32(bytes, record);
        const std::size_t offset = read_u32(bytes, record + 8U);
        const std::size_t length = read_u32(bytes, record + 12U);
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (seen[earlier] == table_tag) return refused(AppFontStatus::table_directory_invalid, table_tag);
        }
        seen[index] = table_tag;
        if (!inside(offset, length, bytes.size())) {
            return refused(AppFontStatus::table_directory_invalid, table_tag);
        }
        TableSpan* const slot = directory_slot(directory, table_tag);
        if (slot == nullptr) continue;
        (*slot).offset = offset;
        (*slot).length = length;
        (*slot).present = true;
    }
    return AppFontValidation{};
}

// Check 10: a Unicode subtable inside cmap.
[[nodiscard]] bool unicode_cmap(std::span<const std::byte> bytes, const TableSpan& cmap) noexcept {
    if (cmap.length < 4U) return false;
    if (read_u16(bytes, cmap.offset) != 0U) return false;
    const std::size_t record_count = read_u16(bytes, cmap.offset + 2U);
    if (record_count == 0U || !inside(4U, record_count * 8U, cmap.length)) return false;
    for (std::size_t index = 0U; index < record_count; ++index) {
        const std::size_t record = cmap.offset + 4U + index * 8U;
        const std::uint16_t platform = read_u16(bytes, record);
        const std::uint16_t encoding = read_u16(bytes, record + 2U);
        const std::size_t subtable = read_u32(bytes, record + 4U);
        const bool unicode = platform == 0U || (platform == 3U && (encoding == 1U || encoding == 10U));
        // The subtable's format field (two bytes) must lie inside the table.
        if (unicode && inside(subtable, 2U, cmap.length)) return true;
    }
    return false;
}

// Check 9: loca's offsets against glyf.
[[nodiscard]] bool loca_matches(std::span<const std::byte> bytes, const TableSpan& loca, const TableSpan& glyf,
                                std::size_t glyph_count, bool long_offsets) noexcept {
    const std::size_t width = long_offsets ? 4U : 2U;
    const std::size_t entries = glyph_count + 1U;
    if (loca.length != entries * width) return false;
    std::size_t previous = 0U;
    for (std::size_t index = 0U; index < entries; ++index) {
        const std::size_t at = loca.offset + index * width;
        const std::size_t value = long_offsets ? static_cast<std::size_t>(read_u32(bytes, at))
                                               : static_cast<std::size_t>(read_u16(bytes, at)) * 2U;
        if (value < previous) return false;
        previous = value;
    }
    const bool bounded = previous <= glyf.length;
    return bounded;
}

} // namespace

AppFontValidation validate_app_font(std::span<const std::byte> bytes) noexcept {
    // Check 1.
    if (bytes.empty()) return refused(AppFontStatus::empty);
    if (bytes.size() > maximum_app_font_bytes) return refused(AppFontStatus::too_large);
    if (bytes.size() < header_bytes) return refused(AppFontStatus::not_sfnt);
    // Check 2.
    const std::uint32_t version = read_u32(bytes, 0U);
    if (version == collection_version) return refused(AppFontStatus::collection_refused);
    const bool truetype = version == truetype_version || version == apple_truetype_version;
    const bool cff = version == cff_version;
    if (!truetype && !cff) return refused(AppFontStatus::not_sfnt);
    // Check 3.
    const std::uint16_t table_count = read_u16(bytes, 4U);
    if (table_count == 0U || table_count > maximum_tables) return refused(AppFontStatus::table_directory_invalid);
    TableDirectory directory{};
    const AppFontValidation read = read_directory(bytes, table_count, directory);
    if (read.status != AppFontStatus::loaded) return read;
    // Check 4.
    for (const RequiredTable& required : common_tables) {
        const TableSpan& found = directory.*(required.member);
        if (!found.present) return refused(AppFontStatus::table_missing, required.tag);
    }
    if (truetype && !directory.glyf.present) return refused(AppFontStatus::table_missing, glyf_tag);
    if (truetype && !directory.loca.present) return refused(AppFontStatus::table_missing, loca_tag);
    if (cff && !directory.cff.present && !directory.cff2.present) {
        return refused(AppFontStatus::table_missing, cff_tag);
    }
    // Check 5.
    const TableSpan& head = directory.head;
    if (head.length < 54U || read_u32(bytes, head.offset + 12U) != head_magic) {
        return refused(AppFontStatus::table_invalid, head_tag);
    }
    const std::uint16_t units_per_em = read_u16(bytes, head.offset + 18U);
    const std::uint16_t loca_format = read_u16(bytes, head.offset + 50U);
    if (units_per_em < 16U || units_per_em > 16384U || loca_format > 1U) {
        return refused(AppFontStatus::table_invalid, head_tag);
    }
    // Check 6.
    const TableSpan& maxp = directory.maxp;
    if (maxp.length < 6U) return refused(AppFontStatus::table_invalid, maxp_tag);
    const std::size_t glyph_count = read_u16(bytes, maxp.offset + 4U);
    if (glyph_count == 0U) return refused(AppFontStatus::table_invalid, maxp_tag);
    // Check 7.
    const TableSpan& hhea = directory.hhea;
    if (hhea.length < 36U) return refused(AppFontStatus::table_invalid, hhea_tag);
    const std::size_t metric_count = read_u16(bytes, hhea.offset + 34U);
    if (metric_count == 0U || metric_count > glyph_count) return refused(AppFontStatus::table_invalid, hhea_tag);
    // Check 8.
    const std::size_t metrics_bytes = 4U * metric_count + 2U * (glyph_count - metric_count);
    if (directory.hmtx.length < metrics_bytes) return refused(AppFontStatus::table_invalid, hmtx_tag);
    // Check 9.
    if (truetype && !loca_matches(bytes, directory.loca, directory.glyf, glyph_count, loca_format == 1U)) {
        return refused(AppFontStatus::table_invalid, loca_tag);
    }
    // Check 10.
    if (!unicode_cmap(bytes, directory.cmap)) return refused(AppFontStatus::table_invalid, cmap_tag);
    return AppFontValidation{};
}

} // namespace gui_forms
