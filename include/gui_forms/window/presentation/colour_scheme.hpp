#pragma once

// The colour-scheme vocabulary (T3 EC3, T4 theme variants), apart from the
// presentation settings so that the theme rules and the host's compositor
// report can name it without the window's types.

#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace gui_forms {

// The colour scheme a window prefers (T3 EC3): light unless the
// application, or a host that reports one, says dark. A theme reads it
// through `@media (prefers-color-scheme: ...)`. A window whose theme has
// no variant for its scheme shows GUI.Forms' own theme in that scheme
// instead (Window::theme_fallback, T4 theme variants).
enum class ColourScheme : std::uint8_t {
    light,
    dark,
};

[[nodiscard]] constexpr std::string_view colour_scheme_name(ColourScheme scheme) noexcept {
    return scheme == ColourScheme::dark ? std::string_view("dark") : std::string_view("light");
}

// The colour schemes a theme has variants for (T4 theme variants): a
// theme sheet declares them with `:root { color-scheme: light dark }`, a
// definition built in code sets them. Light alone by default. A set with
// neither is invalid (Theme::create refuses it).
struct ColourSchemeSet final {
    bool light{true};
    bool dark{};

    [[nodiscard]] constexpr bool contains(ColourScheme scheme) const noexcept {
        const bool held = scheme == ColourScheme::dark ? dark : light;
        return held;
    }
    friend constexpr bool operator==(const ColourSchemeSet& left, const ColourSchemeSet& right) noexcept {
        return left.light == right.light && left.dark == right.dark;
    }
};

// The host's system colours, as hints a theme may read with
// `env(system-accent, <fallback>)` and `env(system-highlight, <fallback>)`
// (T4 theme variants, owner 2026-11-04): the accent colour and the text
// selection colour. Each is empty where the host reports none, and a
// theme then reads its fallback. They are hints: a theme may use,
// override or ignore them, and GUI.Forms' own theme reads neither.
struct SystemColourHints final {
    std::optional<Color> accent{};
    std::optional<Color> highlight{};

    friend bool operator==(const SystemColourHints& left, const SystemColourHints& right) noexcept {
        return left.accent == right.accent && left.highlight == right.highlight;
    }
};

} // namespace gui_forms
