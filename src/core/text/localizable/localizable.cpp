#include "gui_forms/text/localizable/localizable.hpp"

#include "gui_forms_notes.hpp"

#include <stxlib/images.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/types.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <optional>
#include <string_view>
#include <vector>

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

// Where the tables are, in each image format's terms (STXlib's image
// regions, stxlib/images.hpp, which replace the dyld, PE and
// dl_iterate_phdr walks this file made itself): Mach-O __DATA,__gui_forms_loc;
// COFF .gfloc (grouped from .gfloc$m); ELF PT_NOTE segments, which hold
// other programs' notes too.
constexpr stxlib::ImageRegionQuery note_regions{
    .mach_o_segment = stxlib::TextView("__DATA"),
    .mach_o_section = stxlib::TextView("__gui_forms_loc"),
    .pe_section = stxlib::TextView(".gfloc"),
};

// What the region visitor collects into. `exhausted` is set when a copy
// could not be allocated; the visit then stops and the caller throws.
struct NoteSearch final {
    std::uint32_t type{};
    std::vector<std::vector<std::byte>>* out{};
    bool exhausted{false};
};

// STXlib's region visitor: copies the region's tables of the search's type.
// A section of our own holds only GUI.Forms notes; a note segment may hold
// foreign ones, which are skipped. No exception leaves it (STXlib calls it
// while the host's image list is held).
bool collect_region(const stxlib::ImageRegion& region, void* context) noexcept {
    NoteSearch& search = *static_cast<NoteSearch*>(context);
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(region.bytes.data()),
                                           static_cast<std::size_t>(region.bytes.count()));
    const bool others_allowed = region.form == stxlib::ImageRegionForm::note_segment;
    const std::size_t alignment = region.alignment == 8U ? 8U : 4U;
    try {
        collect_notes(bytes, alignment, others_allowed, search.type, *search.out);
    } catch (const std::bad_alloc&) {
        search.exhausted = true;
        return false;
    }
    return true;
}

} // namespace

bool parse_gui_forms_notes(std::span<const std::byte> section, std::uint32_t type,
                           std::vector<std::span<const std::byte>>& out) {
    return parse_notes(section, 4U, false, type, out);
}

// On Windows a non-null `within` reads only the module holding it, as the
// PE walk this replaced did; elsewhere every image is read, as dyld and
// dl_iterate_phdr gave. A host STXlib cannot walk (unsupported) or a failed
// walk yields what was found before it.
std::vector<std::vector<std::byte>> gui_forms_note_descriptions(std::uint32_t type, const void* within) {
    std::vector<std::vector<std::byte>> out;
    NoteSearch search{type, &out, false};
#if defined(_WIN32)
    const bool only_containing = within != nullptr;
#else
    const bool only_containing = false;
    static_cast<void>(within);
#endif
    // A failed walk's status is not reported: what was found before it is
    // the answer.
    if (only_containing) {
        const stxlib::uptr address = reinterpret_cast<stxlib::uptr>(within);
        const stxlib::Status visited =
            stxlib::visit_image_regions_containing(address, note_regions, collect_region, &search);
        static_cast<void>(visited);
    } else {
        const stxlib::Status visited = stxlib::visit_image_regions(note_regions, collect_region, &search);
        static_cast<void>(visited);
    }
    if (search.exhausted) throw std::bad_alloc();
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
