#include "gui_forms/text/localizable/localizable.hpp"

#include "gui_forms_notes.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h; declares K32EnumProcessModules (exported by kernel32).
#include <psapi.h>
#else
#include <elf.h>
#include <link.h>
#endif

namespace gui_forms {
namespace {

// The table format the plugin writes (tools/localizable_plugin): an
// ELF-note-shaped blob per translation unit, little-endian 32-bit integers.
constexpr std::uint32_t note_name_size = 9U;
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

// Decodes a run of notes padded to `alignment`, keeping the descriptions
// of GUI.Forms notes of `type`. With `others_allowed` (an ELF PT_NOTE
// segment, which also holds the build ID and other notes) a foreign note is
// skipped; otherwise (a section of our own) only GUI.Forms notes of other
// types may sit beside them, and anything else makes the whole run
// malformed.
bool parse_notes(std::span<const std::byte> bytes, std::size_t alignment, bool others_allowed, std::uint32_t type,
                 std::vector<std::span<const std::byte>>& out) {
    std::vector<std::span<const std::byte>> found;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        Reader header(bytes.subspan(offset));
        const std::optional<std::uint32_t> name_size = header.u32();
        const std::optional<std::uint32_t> description_size = header.u32();
        const std::optional<std::uint32_t> note_type = header.u32();
        // In a section of our own a zero word where a note would start is
        // padding (no table has an empty name).
        if (!others_allowed && name_size && *name_size == 0U) {
            offset += 4U;
            continue;
        }
        if (!name_size || !description_size || !note_type) {
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
        const bool named_ours = *name_size == note_name_size &&
                                std::memcmp(name.data(), note_name.data(), note_name.size()) == 0;
        if (named_ours && *note_type == type) {
            found.push_back(bytes.subspan(name_end, *description_size));
        } else if (!named_ours && !others_allowed) {
            return false;
        }
        offset = std::min(bytes.size(), padded(description_end, alignment));
    }
    out.insert(out.end(), found.begin(), found.end());
    return true;
}

// Copies the descriptions of `type` in one section of our own (padded to 4
// bytes) or one note segment, appending them to `out` only when the whole
// run is well formed.
void collect_notes(std::span<const std::byte> bytes, std::size_t alignment, bool others_allowed, std::uint32_t type,
                   std::vector<std::vector<std::byte>>& out) {
    std::vector<std::span<const std::byte>> found;
    if (!parse_notes(bytes, alignment, others_allowed, type, found)) return;
    for (const std::span<const std::byte> description : found) {
        out.emplace_back(description.begin(), description.end());
    }
}

#if defined(_WIN32)
// Collects the tables in one loaded module's .gfloc section (COFF groups
// .gfloc$m into .gfloc). The module stays loaded while the caller holds it.
void collect_module(HMODULE module, std::uint32_t type, std::vector<std::vector<std::byte>>& out) {
    const std::byte* base = reinterpret_cast<const std::byte*>(module);
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + (*dos).e_lfanew);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < (*nt).FileHeader.NumberOfSections; ++index, ++section) {
        if (std::memcmp((*section).Name, ".gfloc", 6U) != 0 || ((*section).Name[6] != 0 && (*section).Name[6] != '$')) {
            continue;
        }
        collect_notes({base + (*section).VirtualAddress, (*section).Misc.VirtualSize}, 4U, false, type, out);
    }
}
#endif

#if !defined(__APPLE__) && !defined(_WIN32)
// What dl_iterate_phdr's callback collects into.
struct ImageSearch final {
    std::uint32_t type{};
    std::vector<std::vector<std::byte>>* out{};
};

int collect_image(dl_phdr_info* info, std::size_t, void* data) {
    const ImageSearch& search = *static_cast<const ImageSearch*>(data);
    for (ElfW(Half) index = 0; index < (*info).dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = (*info).dlpi_phdr[index];
        if (header.p_type != PT_NOTE) continue;
        const std::byte* start = reinterpret_cast<const std::byte*>((*info).dlpi_addr + header.p_vaddr);
        const std::size_t alignment = header.p_align == 8U ? 8U : 4U;
        collect_notes({start, static_cast<std::size_t>(header.p_memsz)}, alignment, true, search.type,
                      *search.out);
    }
    return 0;
}
#endif

} // namespace

bool parse_gui_forms_notes(std::span<const std::byte> section, std::uint32_t type,
                           std::vector<std::span<const std::byte>>& out) {
    return parse_notes(section, 4U, false, type, out);
}

std::vector<std::vector<std::byte>> gui_forms_note_descriptions(std::uint32_t type, const void* within) {
    std::vector<std::vector<std::byte>> out;
#if defined(__APPLE__)
    static_cast<void>(within);
    const std::uint32_t count = _dyld_image_count();
    for (std::uint32_t index = 0U; index < count; ++index) {
        const mach_header* header = _dyld_get_image_header(index);
        if (header == nullptr || (*header).magic != MH_MAGIC_64) continue;
        unsigned long size = 0UL;
        const std::uint8_t* data = getsectiondata(reinterpret_cast<const mach_header_64*>(header), "__DATA",
                                                  "__gui_forms_loc", &size);
        if (data == nullptr || size == 0UL) continue;
        collect_notes({reinterpret_cast<const std::byte*>(data), static_cast<std::size_t>(size)}, 4U, false, type,
                      out);
    }
#elif defined(_WIN32)
    // The module holding `within`, or, without one, every module loaded in
    // the process (as dyld and dl_iterate_phdr give on the other hosts).
    if (within != nullptr) {
        HMODULE module = nullptr;
        const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if (GetModuleHandleExW(flags, static_cast<LPCWSTR>(within), &module) == 0) return out;
        collect_module(module, type, out);
        return out;
    }
    // K32EnumProcessModules lives in kernel32, so no psapi import. The list
    // is sized by a first call; modules loaded between the calls are missed,
    // as a dyld image added during the walk would be.
    const HANDLE process = GetCurrentProcess();
    DWORD needed = 0U;
    if (K32EnumProcessModules(process, nullptr, 0U, &needed) == 0) return out;
    std::vector<HMODULE> modules(needed / sizeof(HMODULE) + 8U);
    const DWORD capacity = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
    if (K32EnumProcessModules(process, modules.data(), capacity, &needed) == 0) return out;
    const std::size_t listed = std::min<std::size_t>(modules.size(), needed / sizeof(HMODULE));
    for (std::size_t index = 0U; index < listed; ++index) {
        collect_module(modules[index], type, out);
    }
#else
    static_cast<void>(within);
    ImageSearch search{type, &out};
    dl_iterate_phdr(collect_image, &search);
#endif
    return out;
}

bool parse_localizable_tables(std::span<const std::byte> section, std::vector<LocalizableLiteral>& out) {
    std::vector<std::span<const std::byte>> descriptions;
    if (!parse_gui_forms_notes(section, localizable_note_type, descriptions)) return false;
    std::vector<LocalizableLiteral> found;
    for (const std::span<const std::byte> description : descriptions) {
        if (!parse_description(description, found)) return false;
    }
    out.insert(out.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
    return true;
}

std::vector<LocalizableLiteral> localizable_literals() {
    std::vector<LocalizableLiteral> out;
    const std::vector<std::vector<std::byte>> descriptions =
        gui_forms_note_descriptions(localizable_note_type, nullptr);
    for (const std::vector<std::byte>& description : descriptions) {
        std::vector<LocalizableLiteral> table;
        if (parse_description(description, table)) {
            out.insert(out.end(), std::make_move_iterator(table.begin()), std::make_move_iterator(table.end()));
        }
    }
    return out;
}

} // namespace gui_forms
