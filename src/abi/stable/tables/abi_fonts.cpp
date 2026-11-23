// GF_TABLE_FONTS (since 1.2): the bundled font families a gf_font names by
// id in its reserved word (owner decision D2 a, audit K1; the family
// registry, owner decisions of 2026-11-13). See gf_abi.h.
//
// The families are the library's constant registry
// (gui_forms/typography/font_families/font_families.hpp) and names resolve
// through the constant substitution table
// (gui_forms/drawing/font_substitution/font_substitution.hpp), so these
// functions read no state and take no lock; any thread may call them.
//
// load_family and unload_family (since 1.5, provisional) reach the
// application font registry (gui_forms/typography/app_fonts/app_fonts.hpp).
// The ABI owns each loaded family's AppFontFamily handle in `held`, a table
// guarded by its own lock, until unload_family or the library's unload; the
// registry is constant-initialized, so it outlives this table at exit.

#include "abi_records.hpp"
#include "abi_tables.hpp"

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/drawing/font_substitution/font_substitution.hpp"
#include "gui_forms/typography/app_fonts/app_fonts.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <stxlib/threads.hpp>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity family_id_slot{GF_TABLE_FONTS, 0U};
constexpr SlotIdentity family_name_slot{GF_TABLE_FONTS, 1U};
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
constexpr SlotIdentity load_family_slot{GF_TABLE_FONTS, 2U};
constexpr SlotIdentity unload_family_slot{GF_TABLE_FONTS, 3U};

// The 1.5 size of gf_app_font_face: every field through bytes.
constexpr std::uint32_t app_font_face_size_1_5 = 32U;
static_assert(sizeof(gf_app_font_face) == app_font_face_size_1_5);
#endif
static_assert(GF_APP_FONT_MAXIMUM_FACES == maximum_app_font_faces);
static_assert(GF_APP_FONT_MAXIMUM_BYTES == maximum_app_font_bytes);

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

// load_family and unload_family are not compiled in a high-integrity build
// (cmake/HighIntegrity.cmake): their slots are null, and no application font
// reaches the font reader.
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// The application families load_family gave and unload_family has not taken back.
struct HeldAppFonts final {
    stxlib::Mutex lock{};
    std::vector<AppFontFamily> families{};
};

[[nodiscard]] HeldAppFonts& held_app_fonts() {
    // Never destroyed (termination releases none of our state, rule 1;
    // A-gf-abi-03-F2): the held families are not unloaded by a static
    // destructor at exit, and another thread's load or unload never meets a
    // destroyed lock.
    static HeldAppFonts* const held = new HeldAppFonts();
    return *held;
}

[[nodiscard]] const char* app_font_status_name(AppFontStatus status) noexcept {
    switch (status) {
    case AppFontStatus::loaded: return "loaded";
    case AppFontStatus::no_faces: return "no faces";
    case AppFontStatus::too_many_faces: return "more faces than GF_APP_FONT_MAXIMUM_FACES";
    case AppFontStatus::weight_invalid: return "a weight outside 1 to 1000";
    case AppFontStatus::name_invalid: return "an empty, over-long or unnameable family name";
    case AppFontStatus::name_taken: return "a name a bundled family, a CSS generic or a loaded family has";
    case AppFontStatus::capacity_exhausted: return "no application family number left in this process";
    case AppFontStatus::empty: return "empty face bytes";
    case AppFontStatus::too_large: return "face bytes larger than GF_APP_FONT_MAXIMUM_BYTES";
    case AppFontStatus::not_sfnt: return "not a TrueType or OpenType face";
    case AppFontStatus::collection_refused: return "a font collection";
    case AppFontStatus::table_directory_invalid: return "an unsound table directory";
    case AppFontStatus::table_missing: return "a required table missing";
    case AppFontStatus::table_invalid: return "a required table that fails its check";
    default: break;
    }
    return "an unknown refusal";
}

// The refusal as the failure's message: the reason, the face and, for a table check, the tag.
gf_status refuse_load(const AppFontLoad& loaded) {
    const AppFontValidation& validation = loaded.validation;
    std::array<char, 192> message{};
    const std::uint32_t tag = validation.table;
    const std::array<char, 5> tag_text{static_cast<char>((tag >> 24U) & 0xFFU),
                                       static_cast<char>((tag >> 16U) & 0xFFU),
                                       static_cast<char>((tag >> 8U) & 0xFFU), static_cast<char>(tag & 0xFFU), '\0'};
    if (tag != 0U) {
        std::snprintf(message.data(), message.size(), "load_family refused face %llu: %s ('%s')",
                      static_cast<unsigned long long>(loaded.face_index), app_font_status_name(validation.status),
                      tag_text.data());
    } else {
        std::snprintf(message.data(), message.size(), "load_family refused face %llu: %s",
                      static_cast<unsigned long long>(loaded.face_index), app_font_status_name(validation.status));
    }
    const gf_status status = validation.status == AppFontStatus::capacity_exhausted ? GF_STATUS_LIMIT_EXCEEDED
                                                                                    : GF_STATUS_INVALID_ARGUMENT;
    return fail(status, load_family_slot, std::string_view(message.data()));
}

gf_status fonts_load_family_body(gf_utf8_view name, const gf_app_font_face* faces, std::uint64_t face_count,
                                 std::uint32_t* family) {
    if (family == nullptr || (faces == nullptr && face_count != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, load_family_slot, "load_family needs its faces and an output");
    }
    std::string_view text;
    const gf_status read = read_text(name, load_family_slot, text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    if (face_count > GF_APP_FONT_MAXIMUM_FACES) {
        return fail(GF_STATUS_INVALID_ARGUMENT, load_family_slot, "load_family takes 1 to GF_APP_FONT_MAXIMUM_FACES faces");
    }
    // Every record is checked before any byte is copied. The array is
    // stepped by the caller's struct_size (A-gf-abi-03-F10), so a record
    // grown in a later minor is still read at its own offsets.
    std::uint32_t stride = 0U;
    const gf_status arrayed = check_array(faces, face_count, app_font_face_size_1_5, load_family_slot, stride);
    if (arrayed != GF_STATUS_OK) {
        return arrayed;
    }
    const std::size_t count = static_cast<std::size_t>(face_count);
    for (std::size_t index = 0U; index < count; ++index) {
        const gf_app_font_face& face = element_at(faces, stride, index);
        const bool sized = face.reserved == 0U;
        const bool styled = face.weight >= 1U && face.weight <= 1000U && face.italic <= 1U;
        const bool viewed = face.bytes.data != nullptr || face.bytes.size == 0U;
        const bool bounded = face.bytes.size <= GF_APP_FONT_MAXIMUM_BYTES;
        if (!sized || !styled || !viewed || !bounded) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_family_slot,
                        "a gf_app_font_face has at least the 1.5 size, reserved zero, weight 1 to 1000, italic 0 "
                        "or 1 and at most GF_APP_FONT_MAXIMUM_BYTES bytes");
        }
    }
    // The registry shares the copies; the caller's bytes are read during the call only.
    std::array<AppFontFace, maximum_app_font_faces> copied{};
    for (std::size_t index = 0U; index < count; ++index) {
        const gf_app_font_face& face = element_at(faces, stride, index);
        const std::byte* const first = reinterpret_cast<const std::byte*>(face.bytes.data);
        const std::size_t size = static_cast<std::size_t>(face.bytes.size);
        AppFontFace& target = copied[index];
        target.bytes = gui_forms::detail::make_library_shared<const std::vector<std::byte>>(first, first + size);
        target.weight = static_cast<std::uint16_t>(face.weight);
        target.italic = face.italic == 1U;
    }
    const std::span<const AppFontFace> loaded_faces(copied.data(), count);
    AppFontLoad loaded = load_app_font_family(text, loaded_faces);
    if (loaded.validation.status != AppFontStatus::loaded) {
        return refuse_load(loaded);
    }
    const FontFamilyId id = loaded.family.id();
    HeldAppFonts& held = held_app_fonts();
    {
        const stxlib::ScopedLock guard(held.lock);
        held.families.push_back(std::move(loaded.family));
    }
    *family = static_cast<std::uint32_t>(id.value);
    return GF_STATUS_OK;
}

gf_status GF_CALL fonts_load_family(gf_utf8_view name, const gf_app_font_face* faces, std::uint64_t face_count,
                                    std::uint32_t* family) noexcept {
    return call_slot(load_family_slot, fonts_load_family_body, name, faces, face_count, family);
}

// Whether a held handle owns `family`.
struct HoldsFamily final {
    FontFamilyId family{};
    [[nodiscard]] bool operator()(const AppFontFamily& held) const noexcept { return held.id() == family; }
};

gf_status fonts_unload_family_body(std::uint32_t family, std::uint32_t* deferred) {
    const FontFamilyId id{static_cast<std::uint16_t>(family)};
    // Taken out under the lock and unloaded after it, so the registry's lock is never taken
    // inside this table's.
    AppFontFamily taken{};
    if (family <= 0xFFFFU && app_font_family_number(id)) {
        HeldAppFonts& held = held_app_fonts();
        const stxlib::ScopedLock guard(held.lock);
        const std::vector<AppFontFamily>::iterator found =
            std::find_if(held.families.begin(), held.families.end(), HoldsFamily{id});
        if (found != held.families.end()) {
            taken = std::move(*found);
            held.families.erase(found);
        }
    }
    if (taken.id().value == 0U) {
        return fail(GF_STATUS_NOT_FOUND, unload_family_slot, "no loaded application font family has that id");
    }
    const AppFontUnload unloaded = taken.unload();
    if (deferred != nullptr) *deferred = unloaded == AppFontUnload::deferred ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL fonts_unload_family(std::uint32_t family, std::uint32_t* deferred) noexcept {
    return call_slot(unload_family_slot, fonts_unload_family_body, family, deferred);
}
#endif

constexpr gf_fonts_table table{
    .struct_size = sizeof(gf_fonts_table),
    .abi_minor = GF_ABI_MINOR,
    .family_id = &fonts_family_id,
    .family_name = &fonts_family_name,
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    .load_family = nullptr,
    .unload_family = nullptr,
#else
    .load_family = &fonts_load_family,
    .unload_family = &fonts_unload_family,
#endif
};

} // namespace

const gf_fonts_table& fonts_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
