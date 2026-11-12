// GF_TABLE_FONTS (since 1.2): the bundled font families a gf_font names by
// id in its reserved word (owner decision D2 a, audit K1). See gf_abi.h.
//
// The families are the library's constant list
// (src/core/resources/fonts/bundled_font_families.hpp), so these functions
// read no state and take no lock; any thread may call them.

#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"

#include "../../../core/resources/fonts/bundled_font_families.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cstdint>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity family_id_slot{GF_TABLE_FONTS, 0U};
constexpr SlotIdentity family_name_slot{GF_TABLE_FONTS, 1U};

gf_status GF_CALL fonts_family_id(gf_utf8_view name, std::uint32_t* family) noexcept {
    try {
        const ShadowFrameScope shadow_frame(family_id_slot.table_id, family_id_slot.slot_index);
        if (family == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, family_id_slot, "family_id needs an output");
        }
        std::string_view text;
        const gf_status read = read_text(name, family_id_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const detail::BundledFamilyName* found = detail::bundled_family_named(text);
        if (found == nullptr) {
            return fail(GF_STATUS_NOT_FOUND, family_id_slot, "no bundled font family has that name");
        }
        if ((*found).role) {
            // A role's own face is reached through gf_font.role, not by id.
            return fail(GF_STATUS_NOT_FOUND, family_id_slot,
                        "the name is a font role's face: gf_font.role names it");
        }
        *family = static_cast<std::uint32_t>((*found).family.value);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(family_id_slot);
    }
}

gf_status GF_CALL fonts_family_name(std::uint32_t family, char* buffer, std::uint64_t capacity,
                                    std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(family_name_slot.table_id, family_name_slot.slot_index);
        std::size_t index = 0U;
        for (index = 0U; index < detail::bundled_family_names.size(); ++index) {
            const detail::BundledFamilyName& entry = detail::bundled_family_names[index];
            if (!entry.role && family != 0U && static_cast<std::uint32_t>(entry.family.value) == family) {
                const gf_status written = write_text(entry.name, buffer, capacity, required, family_name_slot);
                return written;
            }
        }
        return fail(GF_STATUS_NOT_FOUND, family_name_slot, "no bundled font family has that id");
    } catch (...) {
        return fail_with_current_exception(family_name_slot);
    }
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
