// The colour role atoms (T3 EC4) and the text roles (P8c), apart from the
// rest of the rule tables so that the theme language can name them in its
// own fuzz target without the engine.

#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace gui_forms {

namespace {

constexpr std::array<std::string_view, colour_role_count> colour_role_names{
    "ink-on-paper",
    "muted-ink-on-paper",
    "ink-on-soft-paper",
    "ink-on-chrome",
    "ink-on-chrome-low",
    "ink-on-near-white-stock",
    "selection-ink-on-selection",
    "ink-on-gold",
    "on-accent-on-accent",
    "signal-on-paper",
    "success-on-paper",
    "pending-ink-on-tint",
    "invalid-ink-on-tint",
    "disabled-ink-on-chrome",
    "disabled-ink-on-paper",
    "phosphor-on-instrument-black",
    "command-link-ink-on-paper",
    "chrome-highlight",
    "chrome",
    "chrome-middle",
    "chrome-low",
    "ordinary-line",
    "dark-line",
    "shadow",
    "selection-line",
    "focus",
};

constexpr std::array<std::string_view, text_role_count> text_role_names{
    "ui", "field", "caption", "heading", "title", "monospace",
};

// The first role that is a single colour.
constexpr ColourRole first_single_role = ColourRole::chrome_highlight;

} // namespace

std::string_view colour_role_name(ColourRole role) noexcept {
    const std::size_t position = static_cast<std::size_t>(role);
    if (position >= colour_role_count) return {};
    return colour_role_names[position];
}

std::optional<ColourRole> colour_role_from_name(std::string_view name) noexcept {
    for (std::size_t position = 0U; position < colour_role_count; ++position) {
        if (colour_role_names[position] == name) return static_cast<ColourRole>(position);
    }
    return std::nullopt;
}

bool application_colour_role_name_valid(std::string_view name) noexcept {
    if (name.empty() || name.size() > maximum_application_colour_role_name) return false;
    if (name.front() < 'a' || name.front() > 'z') return false;
    for (const char character : name) {
        const bool letter = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!letter && !digit && character != '-') return false;
    }
    const bool built_in = colour_role_from_name(name).has_value();
    return !built_in;
}

bool colour_role_paired(ColourRole role) noexcept {
    const bool paired = static_cast<std::size_t>(role) < static_cast<std::size_t>(first_single_role);
    return paired;
}

std::string_view text_role_name(TextRole role) noexcept {
    const std::size_t position = static_cast<std::size_t>(role);
    if (position >= text_role_count) return {};
    return text_role_names[position];
}

std::optional<TextRole> text_role_from_name(std::string_view name) noexcept {
    for (std::size_t position = 0U; position < text_role_count; ++position) {
        if (text_role_names[position] == name) return static_cast<TextRole>(position);
    }
    return std::nullopt;
}

} // namespace gui_forms
