#pragma once

#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms::detail {

// The bundled font families a theme may name in `font-family` (owner decision, 2026-11-10:
// a theme names any bundled font, never a host font). A family is either one of the three
// roles under its face's own name (Portsmouth Rapids is the control role, Carlito content,
// Cousine monospace), or a numbered family (FontSpec::family) whose faces the text engine
// prefers over the role's. Every face a family lists is in assets/fonts/bundled-fonts.sha256
// and staged with every program, so a name here always resolves to shipped bytes.
struct BundledFamilyName final {
    // The family's name as its faces report it; matched without regard to ASCII case.
    std::string_view name{};
    // The role the name stands for, or none for a numbered family.
    std::optional<FontRole> role{};
    // The numbered family, zero when the name stands for a role.
    BundledFontFamily family{};
};

// Selawik 1.01, the bundled near metric match for Segoe UI (assets/fonts/ATTRIBUTION.md).
inline constexpr BundledFontFamily selawik_family{1U};

inline constexpr std::array<BundledFamilyName, 4> bundled_family_names{{
    {"Portsmouth Rapids", FontRole::control, {}},
    {"Carlito", FontRole::content, {}},
    {"Cousine", FontRole::monospace, {}},
    {"Selawik", std::nullopt, selawik_family},
}};

// One face of a numbered family: its manifest name, family, weight and slant. A host registers
// each lazily (HarfBuzzFontEngine::register_lazy_family_typeface); it is read the first time
// text asks for its family. Selawik has no italic: an italic request draws upright.
struct BundledFamilyFace final {
    std::string_view file_name{};
    BundledFontFamily family{};
    std::uint16_t weight{400};
    bool italic{};
};

inline constexpr std::array<BundledFamilyFace, 3> bundled_family_faces{{
    {"Selawik-Regular.ttf", selawik_family, 400, false},
    {"Selawik-Semibold.ttf", selawik_family, 600, false},
    {"Selawik-Bold.ttf", selawik_family, 700, false},
}};

// Whether `left` and `right` are the same ASCII text, ignoring the case of Latin letters.
[[nodiscard]] constexpr bool same_family_name(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        char left_character = left[index];
        char right_character = right[index];
        if (left_character >= 'A' && left_character <= 'Z') left_character = static_cast<char>(left_character - 'A' + 'a');
        if (right_character >= 'A' && right_character <= 'Z') right_character = static_cast<char>(right_character - 'A' + 'a');
        if (left_character != right_character) return false;
    }
    return true;
}

// The bundled family called `name`, or null when no bundled family has that name.
[[nodiscard]] constexpr const BundledFamilyName* bundled_family_named(std::string_view name) noexcept {
    for (const BundledFamilyName& entry : bundled_family_names) {
        if (same_family_name(entry.name, name)) return &entry;
    }
    return nullptr;
}

// Whether `family` is none or a numbered bundled family.
[[nodiscard]] constexpr bool valid_bundled_family(BundledFontFamily family) noexcept {
    if (family.value == 0U) return true;
    for (const BundledFamilyName& entry : bundled_family_names) {
        if (!entry.role && entry.family == family) return true;
    }
    return false;
}

} // namespace gui_forms::detail
