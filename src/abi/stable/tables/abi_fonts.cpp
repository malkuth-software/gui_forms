// GF_TABLE_FONTS (since 1.2): the bundled font families a gf_font names by
// id in its reserved word (owner decision D2 a, audit K1; the family
// registry, owner decisions of 2026-11-13). See gf_abi.h.
//
// The families are the library's constant registry
// (gui_forms/typography/font_families/font_families.hpp) and names resolve
// through the constant substitution table
// (gui_forms/drawing/font_substitution/font_substitution.hpp), so these
// functions read no state and take no lock; any thread may call them.

#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/drawing/font_substitution/font_substitution.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"


#include <cstdint>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity family_id_slot{GF_TABLE_FONTS, 0U};
constexpr SlotIdentity family_name_slot{GF_TABLE_FONTS, 1U};

gf_status fonts_family_id_body(gf_utf8_view name, std::uint32_t* family) {
    if (family == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, family_id_slot, "family_id needs an output");
    }
    std::string_view text;
    const gf_status read = read_text(name, family_id_slot, text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    // A family by its own name (since 1.3 a role's family too: Carlito has an id), then a
    // name the substitution table maps to a bundled family (Segoe UI to Selawik, Arial to
    // Arimo, Segoe UI Semibold to Selawik Semibold): that family by its name, else the
    // family of the face the table chose. A name the table does not know is unmapped.
    const FontFamilyRecord* found = font_family_named(text);
    if (found == nullptr) {
        const gui_drawing::FontFamilyResolution resolution =
            gui_drawing::resolve_font_family(text, gui_drawing::FontFaceStyle::regular);
        if (resolution.source != gui_drawing::FontResolutionSource::unmapped && !resolution.family.empty()) {
            found = font_family_named(resolution.family);
        }
        if (found == nullptr && resolution.source != gui_drawing::FontResolutionSource::unmapped &&
            !resolution.face_file.empty()) {
            found = font_family_with_face(resolution.face_file);
        }
    }
    if (found == nullptr) {
        return fail(GF_STATUS_NOT_FOUND, family_id_slot, "no bundled font family answers that name");
    }
    *family = static_cast<std::uint32_t>((*found).id.value);
    return GF_STATUS_OK;
}

gf_status GF_CALL fonts_family_id(gf_utf8_view name, std::uint32_t* family) noexcept {
    return call_slot(family_id_slot, fonts_family_id_body, name, family);
}

gf_status fonts_family_name_body(std::uint32_t family, char* buffer, std::uint64_t capacity,
                                 std::uint64_t* required) {
    const FontFamilyRecord* found = nullptr;
    if (family != 0U && family <= maximum_bundled_font_family) {
        found = font_family_record(FontFamilyId{static_cast<std::uint16_t>(family)});
    }
    if (found != nullptr) {
        const gf_status written = write_text((*found).name, buffer, capacity, required, family_name_slot);
        return written;
    }
    return fail(GF_STATUS_NOT_FOUND, family_name_slot, "no bundled font family has that id");
}

gf_status GF_CALL fonts_family_name(std::uint32_t family, char* buffer, std::uint64_t capacity,
                                    std::uint64_t* required) noexcept {
    return call_slot(family_name_slot, fonts_family_name_body, family, buffer, capacity, required);
}

constexpr gf_fonts_table table{
    .struct_size = sizeof(gf_fonts_table),
    .abi_minor = GF_ABI_MINOR,
    .family_id = &fonts_family_id,
    .family_name = &fonts_family_name,
};

} // namespace

const gf_fonts_table& fonts_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
