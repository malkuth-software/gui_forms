#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <stxlib/optional.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

// The font family registry (owner decisions of 2026-11-13). Interface text
// draws only the families bundled with the library, since a foreign font can
// break the text engine; an application's own families (owner, 2026-11-15)
// are a separate registry with numbers above maximum_bundled_font_family,
// admitted only inside composites and sub-surfaces
// (gui_forms/typography/app_fonts/app_fonts.hpp). Every
// face a family lists is in assets/fonts/bundled-fonts.sha256 and in the font
// pack compiled into every program (cmake/FontPack.cmake), so a family always
// resolves to shipped bytes.
//
// A family is one of the three roles' faces under its own name (Portsmouth
// Rapids is the control role, Carlito content, Cousine monospace) or a
// family with faces of its own (Selawik, Arimo, Tinos, Portsmouth Mono and
// Selawik Semibold, the last four the GUI.Drawing substitution table's
// families, font pack stage 4). Each has a fixed number
// (FontFamilyId). The table is constant: every function here reads no state
// and any thread may call it. The family stacks below are the one mutable
// part.

// Where a family's faces come from. Bundled is the only kind this table
// holds; application families have no record here (app_fonts.hpp).
enum class FontFamilyKind : std::uint8_t {
    bundled,
};

// One face of a family: its file in the bundled font directory, its weight
// (1 to 1000) and slant.
struct GUI_FORMS_API FontFamilyFace final {
    std::string_view file_name{};
    std::uint16_t weight{400};
    bool italic{};
};

struct GUI_FORMS_API FontFamilyRecord final {
    FontFamilyId id{};
    // The family's name as its faces report it; matched without regard to
    // ASCII case.
    std::string_view name{};
    FontFamilyKind kind{FontFamilyKind::bundled};
    // The role whose faces are this family's, or none for a family whose
    // faces are registered as its own.
    stxlib::Optional<FontRole> role{};
    // The faces, at least one.
    std::span<const FontFamilyFace> faces{};
};

// The fixed numbers. They never change and are never reused: a number
// crosses the C ABI (gf_font.reserved) and is stored by consumers.
inline constexpr FontFamilyId selawik_font_family{1U};
inline constexpr FontFamilyId portsmouth_rapids_font_family{2U};
inline constexpr FontFamilyId carlito_font_family{3U};
inline constexpr FontFamilyId cousine_font_family{4U};
inline constexpr FontFamilyId arimo_font_family{5U};
inline constexpr FontFamilyId tinos_font_family{6U};
inline constexpr FontFamilyId portsmouth_mono_font_family{7U};
inline constexpr FontFamilyId selawik_semibold_font_family{8U};

// The largest number a bundled family may have; numbers above it are
// application families (app_fonts.hpp).
inline constexpr std::uint16_t maximum_bundled_font_family = 255U;

namespace detail {

// Selawik 1.01, the bundled near metric match for Segoe UI
// (assets/fonts/ATTRIBUTION.md). It has no italic: an italic request draws
// its nearest upright face.
inline constexpr std::array<FontFamilyFace, 3> selawik_faces{{
    {"Selawik-Regular.ttf", 400, false},
    {"Selawik-Semibold.ttf", 600, false},
    {"Selawik-Bold.ttf", 700, false},
}};

// The roles' faces, as bundled_face_uses registers them.
inline constexpr std::array<FontFamilyFace, 2> portsmouth_rapids_faces{{
    {"PortsmouthRapids.ttf", 400, false},
    {"PortsmouthRapids-Bold.ttf", 700, false},
}};

inline constexpr std::array<FontFamilyFace, 4> carlito_faces{{
    {"Carlito-Regular.ttf", 400, false},
    {"Carlito-Bold.ttf", 700, false},
    {"Carlito-Italic.ttf", 400, true},
    {"Carlito-BoldItalic.ttf", 700, true},
}};

inline constexpr std::array<FontFamilyFace, 4> cousine_faces{{
    {"Cousine-Regular.ttf", 400, false},
    {"Cousine-Bold.ttf", 700, false},
    {"Cousine-Italic.ttf", 400, true},
    {"Cousine-BoldItalic.ttf", 700, true},
}};

// Arimo and Tinos (Chrome OS core fonts, assets/fonts/ATTRIBUTION.md), the
// metric matches for Arial and Times New Roman in GUI.Drawing's substitution
// table (assets/fonts/font-substitution.json).
inline constexpr std::array<FontFamilyFace, 4> arimo_faces{{
    {"Arimo-Regular.ttf", 400, false},
    {"Arimo-Bold.ttf", 700, false},
    {"Arimo-Italic.ttf", 400, true},
    {"Arimo-BoldItalic.ttf", 700, true},
}};

inline constexpr std::array<FontFamilyFace, 4> tinos_faces{{
    {"Tinos-Regular.ttf", 400, false},
    {"Tinos-Bold.ttf", 700, false},
    {"Tinos-Italic.ttf", 400, true},
    {"Tinos-BoldItalic.ttf", 700, true},
}};

// Portsmouth Mono, the nearest face to Consolas in the substitution table. It
// has one face: every weight and slant draws it.
inline constexpr std::array<FontFamilyFace, 1> portsmouth_mono_faces{{
    {"PortsmouthMono.ttf", 400, false},
}};

// Selawik Semibold, the substitution table's family for Segoe UI Semibold:
// Selawik's semibold face as its regular and Selawik's bold as its bold, the
// styles the table gives them.
inline constexpr std::array<FontFamilyFace, 2> selawik_semibold_faces{{
    {"Selawik-Semibold.ttf", 400, false},
    {"Selawik-Bold.ttf", 700, false},
}};

} // namespace detail

// The bundled families, in the order of their numbers.
inline constexpr std::array<FontFamilyRecord, 8> bundled_font_families{{
    {selawik_font_family, "Selawik", FontFamilyKind::bundled, stxlib::Optional<FontRole>::none(), detail::selawik_faces},
    {portsmouth_rapids_font_family, "Portsmouth Rapids", FontFamilyKind::bundled, stxlib::Optional<FontRole>::of(FontRole::control),
     detail::portsmouth_rapids_faces},
    {carlito_font_family, "Carlito", FontFamilyKind::bundled, stxlib::Optional<FontRole>::of(FontRole::content), detail::carlito_faces},
    {cousine_font_family, "Cousine", FontFamilyKind::bundled, stxlib::Optional<FontRole>::of(FontRole::monospace), detail::cousine_faces},
    {arimo_font_family, "Arimo", FontFamilyKind::bundled, stxlib::Optional<FontRole>::none(), detail::arimo_faces},
    {tinos_font_family, "Tinos", FontFamilyKind::bundled, stxlib::Optional<FontRole>::none(), detail::tinos_faces},
    {portsmouth_mono_font_family, "Portsmouth Mono", FontFamilyKind::bundled, stxlib::Optional<FontRole>::none(),
     detail::portsmouth_mono_faces},
    {selawik_semibold_font_family, "Selawik Semibold", FontFamilyKind::bundled, stxlib::Optional<FontRole>::none(),
     detail::selawik_semibold_faces},
}};

// Whether `left` and `right` are the same ASCII text, ignoring the case of
// Latin letters.
[[nodiscard]] constexpr bool same_font_family_name(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        char left_character = left[index];
        char right_character = right[index];
        if (left_character >= 'A' && left_character <= 'Z') {
            left_character = static_cast<char>(left_character - 'A' + 'a');
        }
        if (right_character >= 'A' && right_character <= 'Z') {
            right_character = static_cast<char>(right_character - 'A' + 'a');
        }
        if (left_character != right_character) return false;
    }
    return true;
}

// The family numbered `family`, or null for zero or a number no family has.
[[nodiscard]] constexpr const FontFamilyRecord* font_family_record(FontFamilyId family) noexcept {
    for (const FontFamilyRecord& record : bundled_font_families) {
        if (record.id == family) return &record;
    }
    return nullptr;
}

// A CSS generic family name and the bundled family it draws. The mapping
// is GUI.Forms' choice (owner, 2026-11-13: the theme language follows CSS
// closely), not a CSS rule: CSS leaves a generic's face to the user agent.
// The serif generics draw Tinos; until a cursive or fantasy face is bundled,
// those generics draw the content family. `emoji` names no family (zero): the bundled emoji
// fallback face already draws emoji for every font. A generic is matched
// only unquoted, as CSS does (a quoted "serif" is a family name).
struct GUI_FORMS_API GenericFontFamily final {
    std::string_view name{};
    FontFamilyId family{};
};

inline constexpr std::array<GenericFontFamily, 13> generic_font_families{{
    {"sans-serif", carlito_font_family},
    {"serif", tinos_font_family},
    {"monospace", cousine_font_family},
    {"cursive", carlito_font_family},
    {"fantasy", carlito_font_family},
    {"system-ui", carlito_font_family},
    {"ui-sans-serif", carlito_font_family},
    {"ui-serif", tinos_font_family},
    {"ui-monospace", cousine_font_family},
    {"ui-rounded", carlito_font_family},
    {"math", carlito_font_family},
    {"fangsong", carlito_font_family},
    {"emoji", FontFamilyId{}},
}};

// The generic family called `name` (ASCII case ignored), or null.
[[nodiscard]] constexpr const GenericFontFamily* generic_font_family(std::string_view name) noexcept {
    for (const GenericFontFamily& generic : generic_font_families) {
        if (same_font_family_name(generic.name, name)) return &generic;
    }
    return nullptr;
}

// Whether `family` is zero or a family the registry has.
[[nodiscard]] constexpr bool valid_font_family(FontFamilyId family) noexcept {
    const bool valid = family.value == 0U || font_family_record(family) != nullptr;
    return valid;
}

// The family called `name` by its own name, or null. Other names (Segoe UI,
// Arial) are the substitution table's (gui_drawing::resolve_font_family);
// the C ABI resolves them through it (fonts.family_id).
[[nodiscard]] constexpr const FontFamilyRecord* font_family_named(std::string_view name) noexcept {
    for (const FontFamilyRecord& record : bundled_font_families) {
        if (same_font_family_name(record.name, name)) return &record;
    }
    return nullptr;
}

// The family one of whose faces is the bundled file `file_name`, or null:
// how a substitution's answer (a face file) finds its registered family. A
// face two families share (Selawik's semibold and bold faces, which Selawik
// Semibold lists too) finds the lower-numbered family; a caller that knows
// the substitution's family name finds it by name first.
[[nodiscard]] constexpr const FontFamilyRecord* font_family_with_face(std::string_view file_name) noexcept {
    for (const FontFamilyRecord& record : bundled_font_families) {
        for (const FontFamilyFace& face : record.faces) {
            if (face.file_name == file_name) return &record;
        }
    }
    return nullptr;
}

// The family whose faces are `role`'s.
[[nodiscard]] constexpr FontFamilyId role_font_family(FontRole role) noexcept {
    for (const FontFamilyRecord& record : bundled_font_families) {
        if (record.role.present() && (*record.role.value()) == role) return record.id;
    }
    return FontFamilyId{};
}

// Whether a face of `left_weight` and `left_italic` is nearer a request for
// `weight` and `italic` than one of `right_weight` and `right_italic`
// (stage F decision 7: nearest, never synthesized). The slant decides
// first; then the smaller weight distance; at an equal distance, the
// lighter face for a request of 500 or less and the heavier above (CSS
// Fonts' order). Equal faces are not nearer.
[[nodiscard]] constexpr bool font_face_nearer(std::uint16_t left_weight, bool left_italic,
                                              std::uint16_t right_weight, bool right_italic,
                                              std::uint16_t weight, bool italic) noexcept {
    const bool left_slant = left_italic == italic;
    const bool right_slant = right_italic == italic;
    if (left_slant != right_slant) return left_slant;
    const int left_distance = left_weight > weight ? left_weight - weight : weight - left_weight;
    const int right_distance = right_weight > weight ? right_weight - weight : weight - right_weight;
    if (left_distance != right_distance) return left_distance < right_distance;
    if (left_weight == right_weight) return false;
    const bool lighter = left_weight < right_weight;
    const bool nearer = weight <= 500U ? lighter : !lighter;
    return nearer;
}

// The index in `record.faces` of the face nearest `weight` and `italic`.
[[nodiscard]] constexpr std::size_t nearest_font_family_face(const FontFamilyRecord& record,
                                                             std::uint16_t weight, bool italic) noexcept {
    std::size_t nearest = 0U;
    for (std::size_t index = 1U; index < record.faces.size(); ++index) {
        const FontFamilyFace& candidate = record.faces[index];
        const FontFamilyFace& best = record.faces[nearest];
        if (font_face_nearer(candidate.weight, candidate.italic, best.weight, best.italic, weight, italic)) {
            nearest = index;
        }
    }
    return nearest;
}

// -- Family stacks ---------------------------------------------------------

// The most families one stack lists, and the most stacks a process holds.
inline constexpr std::size_t maximum_font_stack_families = 4U;
inline constexpr std::size_t maximum_font_family_stacks = 1024U;

// A stack's families in order; `count` of `families` are live, the rest
// zero.
struct GUI_FORMS_API FontFamilyStack final {
    std::array<FontFamilyId, maximum_font_stack_families> families{};
    std::size_t count{};
    friend constexpr bool operator==(const FontFamilyStack& left, const FontFamilyStack& right) noexcept {
        return left.families == right.families && left.count == right.count;
    }
};

// Registers the stack `families` and returns its number; the same list
// registered again returns the same number. A family may be bundled or a
// live application family (font_family_drawable). A list that is empty,
// longer than maximum_font_stack_families, names zero, an unknown family or one
// family twice, or that arrives once maximum_font_family_stacks are held,
// is refused with zero: the font then draws its own family and its role.
// Thread-safe: registrations are serialized, and a stack once numbered
// never changes or goes away (the registry lives for the process).
[[nodiscard]] GUI_FORMS_API FontFamilyStackId register_font_family_stack(std::span<const FontFamilyId> families);

// The families of `stack`; empty for zero or a number not given. Lock-free
// and thread-safe.
[[nodiscard]] GUI_FORMS_API FontFamilyStack font_family_stack(FontFamilyStackId stack) noexcept;

// Whether `stack` is zero or a number register_font_family_stack gave.
[[nodiscard]] GUI_FORMS_API bool valid_font_family_stack(FontFamilyStackId stack) noexcept;

// The families a font draws before its role, in order: its family, then
// its stack's families not already listed. Families that cannot draw now
// (unknown, or an application family quarantined or unloaded:
// font_family_drawable) are left out.
struct GUI_FORMS_API FontFamilyOrder final {
    std::array<FontFamilyId, 1U + maximum_font_stack_families> families{};
    std::size_t count{};
};

[[nodiscard]] GUI_FORMS_API FontFamilyOrder font_family_order(const FontSpec& font) noexcept;

// The position of `family` in `order`, or none.
[[nodiscard]] constexpr stxlib::Optional<std::size_t> font_family_position(const FontFamilyOrder& order,
                                                                        FontFamilyId family) noexcept {
    stxlib::Optional<std::size_t> result{};
    if (family.value == 0U) return result;
    for (std::size_t index = 0U; index < order.count; ++index) {
        if (order.families[index] == family) {
            result.set(index);
            break;
        }
    }
    return result;
}

} // namespace gui_forms
