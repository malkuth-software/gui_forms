#include "gui_forms/text/localizable/localizable.hpp"

#include <cstring>
#include <optional>
#include <string_view>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <elf.h>
#include <link.h>
#endif

namespace gui_forms {
namespace {

// The table format the plugin writes (tools/localizable_plugin): an
// ELF-note-shaped blob per translation unit, little-endian 32-bit integers.
constexpr std::uint32_t note_name_size = 9U;
constexpr std::uint32_t note_type = 0x47464c01U;
constexpr std::string_view note_name{"GUIForms\0", 9U};
constexpr std::size_t note_header_size = 12U;
// The smallest record: five lengths, the line and the column.
constexpr std::size_t minimum_record_size = 28U;

constexpr std::size_t padded(std::size_t size, std::size_t alignment) noexcept {
    return (size + alignment - 1U) / alignment * alignment;
}

// Reads the table's integers and strings within bounds.
class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::optional<std::uint32_t> u32() noexcept {
        if (bytes_.size() - offset_ < 4U) return std::nullopt;
        std::uint32_t value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes_[offset_ + index]))
                     << (8U * index);
        }
        offset_ += 4U;
        return value;
    }

    [[nodiscard]] bool text(std::string& out) {
        const std::optional<std::uint32_t> size = u32();
        if (!size || bytes_.size() - offset_ < *size) return false;
        out.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), *size);
        offset_ += *size;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

bool parse_description(std::span<const std::byte> description, std::vector<LocalizableLiteral>& out) {
    Reader reader(description);
    const std::optional<std::uint32_t> count = reader.u32();
    if (!count || *count > reader.remaining() / minimum_record_size) return false;
    for (std::uint32_t index = 0U; index < *count; ++index) {
        LocalizableLiteral literal;
        if (!reader.text(literal.property) || !reader.text(literal.name) || !reader.text(literal.file)) return false;
        const std::optional<std::uint32_t> line = reader.u32();
        const std::optional<std::uint32_t> column = reader.u32();
        if (!line || !column) return false;
        literal.line = *line;
        literal.column = *column;
        if (!reader.text(literal.function) || !reader.text(literal.text)) return false;
        out.push_back(std::move(literal));
    }
    return reader.remaining() == 0U;
}

// Decodes a run of notes padded to `alignment`, keeping only GUI.Forms
// tables. With `others_allowed` (an ELF PT_NOTE segment, which also holds
// the build ID and other notes) a foreign note is skipped; otherwise (a
// section of our own) it makes the whole run malformed.
bool parse_notes(std::span<const std::byte> bytes, std::size_t alignment, bool others_allowed,
                 std::vector<LocalizableLiteral>& out) {
    std::vector<LocalizableLiteral> found;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        Reader header(bytes.subspan(offset));
        const std::optional<std::uint32_t> name_size = header.u32();
        const std::optional<std::uint32_t> description_size = header.u32();
        const std::optional<std::uint32_t> type = header.u32();
        // In a section of our own a zero word where a note would start is
        // padding (no table has an empty name).
        if (!others_allowed && name_size && *name_size == 0U) {
            offset += 4U;
            continue;
        }
        if (!name_size || !description_size || !type) {
            // Trailing zero padding shorter than a header.
            for (std::size_t index = offset; index < bytes.size(); ++index) {
                if (bytes[index] != std::byte{0}) return false;
            }
            break;
        }
        const std::size_t name_end = offset + note_header_size + padded(*name_size, alignment);
        if (*name_size > bytes.size() || name_end > bytes.size() ||
            *description_size > bytes.size() - name_end) {
            return false;
        }
        const std::size_t description_end = name_end + *description_size;
        const std::span<const std::byte> name = bytes.subspan(offset + note_header_size, *name_size);
        const bool ours = *name_size == note_name_size && *type == note_type &&
                          std::memcmp(name.data(), note_name.data(), note_name.size()) == 0;
        if (ours) {
            if (!parse_description(bytes.subspan(name_end, *description_size), found)) return false;
        } else if (!others_allowed) {
            return false;
        }
        offset = std::min(bytes.size(), padded(description_end, alignment));
    }
    out.insert(out.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
    return true;
}

#if !defined(__APPLE__) && !defined(_WIN32)
int collect_image(dl_phdr_info* info, std::size_t, void* data) {
    std::vector<LocalizableLiteral>& out = *static_cast<std::vector<LocalizableLiteral>*>(data);
    for (ElfW(Half) index = 0; index < (*info).dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = (*info).dlpi_phdr[index];
        if (header.p_type != PT_NOTE) continue;
        const std::byte* start = reinterpret_cast<const std::byte*>((*info).dlpi_addr + header.p_vaddr);
        const std::size_t alignment = header.p_align == 8U ? 8U : 4U;
        std::vector<LocalizableLiteral> image;
        if (parse_notes({start, static_cast<std::size_t>(header.p_memsz)}, alignment, true, image)) {
            out.insert(out.end(), std::make_move_iterator(image.begin()), std::make_move_iterator(image.end()));
        }
    }
    return 0;
}
#endif

} // namespace

bool parse_localizable_tables(std::span<const std::byte> section, std::vector<LocalizableLiteral>& out) {
    // Tables in a section of our own are padded to 4 bytes; nothing else
    // lives there.
    return parse_notes(section, 4U, false, out);
}

std::vector<LocalizableLiteral> localizable_literals() {
    std::vector<LocalizableLiteral> out;
#if defined(__APPLE__)
    const std::uint32_t count = _dyld_image_count();
    for (std::uint32_t index = 0U; index < count; ++index) {
        const mach_header* header = _dyld_get_image_header(index);
        if (header == nullptr || (*header).magic != MH_MAGIC_64) continue;
        unsigned long size = 0UL;
        const std::uint8_t* data = getsectiondata(reinterpret_cast<const mach_header_64*>(header), "__DATA",
                                                  "__gui_forms_loc", &size);
        if (data == nullptr || size == 0UL) continue;
        std::vector<LocalizableLiteral> image;
        if (parse_localizable_tables({reinterpret_cast<const std::byte*>(data), static_cast<std::size_t>(size)},
                                     image)) {
            out.insert(out.end(), std::make_move_iterator(image.begin()), std::make_move_iterator(image.end()));
        }
    }
#elif defined(_WIN32)
    // The program's own image only: COFF groups .gfloc$m into .gfloc.
    const HMODULE module = GetModuleHandleW(nullptr);
    const std::byte* base = reinterpret_cast<const std::byte*>(module);
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + (*dos).e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < (*nt).FileHeader.NumberOfSections; ++index, ++section) {
        if (std::memcmp((*section).Name, ".gfloc", 6U) != 0 || ((*section).Name[6] != 0 && (*section).Name[6] != '$')) {
            continue;
        }
        std::vector<LocalizableLiteral> image;
        if (parse_localizable_tables({base + (*section).VirtualAddress, (*section).Misc.VirtualSize}, image)) {
            out.insert(out.end(), std::make_move_iterator(image.begin()), std::make_move_iterator(image.end()));
        }
    }
#else
    dl_iterate_phdr(collect_image, &out);
#endif
    return out;
}

} // namespace gui_forms
