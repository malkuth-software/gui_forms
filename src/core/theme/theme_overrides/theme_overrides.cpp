#include "gui_forms/theme/theme_overrides/theme_overrides.hpp"

#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <span>

namespace gui_forms {

namespace {

// The standard pseudo-classes the theme language writes for a state
// (theme_selector.cpp); every other state is written `:state(name)`.
struct PseudoClassName final {
    AnatomyState state{AnatomyState::normal};
    std::string_view name{};
};

constexpr std::array<PseudoClassName, 12> pseudo_class_names{{
    {AnatomyState::hot, "hover"},
    {AnatomyState::pressed, "active"},
    {AnatomyState::focused, "focus"},
    {AnatomyState::focus_visible, "focus-visible"},
    {AnatomyState::checked, "checked"},
    {AnatomyState::indeterminate, "indeterminate"},
    {AnatomyState::disabled, "disabled"},
    {AnatomyState::enabled, "enabled"},
    {AnatomyState::read_only, "read-only"},
    {AnatomyState::invalid, "invalid"},
    {AnatomyState::open, "open"},
    {AnatomyState::preselect, "default"},
}};

[[nodiscard]] bool colour_property(ThemeOverrideProperty property) noexcept {
    const bool colour = property == ThemeOverrideProperty::background_colour ||
                        property == ThemeOverrideProperty::border_colour ||
                        property == ThemeOverrideProperty::text_colour;
    return colour;
}

// The largest value a length property takes (its smallest is 0).
[[nodiscard]] double length_maximum(ThemeOverrideProperty property) noexcept {
    double maximum = 0.0;
    switch (property) {
    case ThemeOverrideProperty::corner_radius:
        maximum = 64.0;
        break;
    case ThemeOverrideProperty::border_width:
        maximum = 16.0;
        break;
    case ThemeOverrideProperty::opacity:
        maximum = 1.0;
        break;
    case ThemeOverrideProperty::background_colour:
    case ThemeOverrideProperty::border_colour:
    case ThemeOverrideProperty::text_colour:
        // Colour properties have no length (colour_property routes them).
        maximum = 0.0;
        break;
    default:
        // No other property exists; a value outside the enumeration takes no length.
        maximum = 0.0;
        break;
    }
    return maximum;
}

[[nodiscard]] std::string_view property_name(ThemeOverrideProperty property) noexcept {
    std::string_view name{};
    switch (property) {
    case ThemeOverrideProperty::background_colour:
        name = "background-color";
        break;
    case ThemeOverrideProperty::border_colour:
        name = "border-color";
        break;
    case ThemeOverrideProperty::text_colour:
        name = "color";
        break;
    case ThemeOverrideProperty::corner_radius:
        name = "border-radius";
        break;
    case ThemeOverrideProperty::border_width:
        name = "border-width";
        break;
    case ThemeOverrideProperty::opacity:
        name = "opacity";
        break;
    default:
        name = "opacity";
        break;
    }
    return name;
}

// A registered snake_case name in the theme language's kebab case.
void append_kebab(std::string& out, std::string_view name) {
    for (const char character : name) out.push_back(character == '_' ? '-' : character);
}

// `#rrggbb`, or `rgb(r g b / a%)` for a translucent colour.
void append_colour(std::string& out, Color colour) {
    std::array<char, 48> buffer{};
    int written = 0;
    if (colour.alpha == 255U) {
        written = std::snprintf(buffer.data(), buffer.size(), "#%02x%02x%02x", colour.red, colour.green, colour.blue);
    } else {
        const double percent = static_cast<double>(colour.alpha) * 100.0 / 255.0;
        written = std::snprintf(buffer.data(), buffer.size(), "rgb(%u %u %u / %.2f%%)", colour.red, colour.green,
                                colour.blue, percent);
    }
    if (written > 0) out.append(buffer.data(), static_cast<std::size_t>(written));
}

void append_length(std::string& out, ThemeOverrideProperty property, double value) {
    std::array<char, 48> buffer{};
    const char* const format = property == ThemeOverrideProperty::opacity ? "%.4g" : "%.4gpx";
    const int written = std::snprintf(buffer.data(), buffer.size(), format, value);
    if (written > 0) out.append(buffer.data(), static_cast<std::size_t>(written));
}

void append_state(std::string& out, AnatomyState state) {
    for (const PseudoClassName& entry : pseudo_class_names) {
        if (entry.state != state) continue;
        out.push_back(':');
        out.append(entry.name);
        return;
    }
    out.append(":state(");
    append_kebab(out, anatomy_state_name(state));
    out.push_back(')');
}

[[nodiscard]] bool holds_state(std::span<const AnatomyState> states, AnatomyState state) noexcept {
    for (const AnatomyState candidate : states) {
        if (candidate == state) return true;
    }
    return false;
}

} // namespace

ThemeOverrideCheck ThemeOverrides::check_target(const ThemeOverrideTarget& target) noexcept {
    const std::span<const ControlAnatomy> anatomies = stock_control_anatomies();
    const ControlAnatomy* anatomy = nullptr;
    for (const ControlAnatomy& candidate : anatomies) {
        if (candidate.control_id == target.control) {
            anatomy = &candidate;
            break;
        }
    }
    if (anatomy == nullptr) return ThemeOverrideCheck::unknown_control;
    const PartDeclaration* part = nullptr;
    if (!target.part.empty()) {
        for (const PartDeclaration& candidate : (*anatomy).parts) {
            if (candidate.name == target.part) {
                part = &candidate;
                break;
            }
        }
        if (part == nullptr) return ThemeOverrideCheck::unknown_part;
    }
    if (target.state.has_value()) {
        const bool control_state = holds_state((*anatomy).states, *target.state);
        const bool part_state = part != nullptr && holds_state((*part).states, *target.state);
        if (!control_state && !part_state) return ThemeOverrideCheck::unknown_state;
    }
    return ThemeOverrideCheck::accepted;
}

ThemeOverrideCheck ThemeOverrides::set_role_colour(ColourRole role, Color foreground, std::optional<Color> background) {
    if (role == ColourRole::count) return ThemeOverrideCheck::wrong_value_kind;
    const bool paired = colour_role_paired(role);
    if (paired != background.has_value()) return ThemeOverrideCheck::wrong_value_kind;
    for (RoleEntry& entry : roles_) {
        if (entry.role != role) continue;
        entry.foreground = foreground;
        entry.background = background;
        return ThemeOverrideCheck::accepted;
    }
    if (size() >= maximum_entries) return ThemeOverrideCheck::too_many;
    roles_.push_back(RoleEntry{role, foreground, background});
    return ThemeOverrideCheck::accepted;
}

ThemeOverrideCheck ThemeOverrides::set_colour(const ThemeOverrideTarget& target, ThemeOverrideProperty property,
                                              Color colour) {
    if (!colour_property(property)) return ThemeOverrideCheck::wrong_value_kind;
    const ThemeOverrideCheck check = check_target(target);
    if (check != ThemeOverrideCheck::accepted) return check;
    PropertyEntry entry{};
    entry.target = target;
    entry.property = property;
    entry.colour = colour;
    const ThemeOverrideCheck stored = store(std::move(entry));
    return stored;
}

ThemeOverrideCheck ThemeOverrides::set_length(const ThemeOverrideTarget& target, ThemeOverrideProperty property,
                                              double value) {
    if (colour_property(property)) return ThemeOverrideCheck::wrong_value_kind;
    if (!std::isfinite(value) || value < 0.0 || value > length_maximum(property)) {
        return ThemeOverrideCheck::value_out_of_range;
    }
    const ThemeOverrideCheck check = check_target(target);
    if (check != ThemeOverrideCheck::accepted) return check;
    PropertyEntry entry{};
    entry.target = target;
    entry.property = property;
    entry.length = value;
    const ThemeOverrideCheck stored = store(std::move(entry));
    return stored;
}

ThemeOverrideCheck ThemeOverrides::store(PropertyEntry entry) {
    for (PropertyEntry& existing : properties_) {
        if (existing.target == entry.target && existing.property == entry.property) {
            existing = std::move(entry);
            return ThemeOverrideCheck::accepted;
        }
    }
    if (size() >= maximum_entries) return ThemeOverrideCheck::too_many;
    properties_.push_back(std::move(entry));
    return ThemeOverrideCheck::accepted;
}

void ThemeOverrides::clear() noexcept {
    roles_.clear();
    properties_.clear();
}

bool ThemeOverrides::empty() const noexcept {
    const bool none = roles_.empty() && properties_.empty();
    return none;
}

std::size_t ThemeOverrides::size() const noexcept {
    const std::size_t count = roles_.size() + properties_.size();
    return count;
}

std::string ThemeOverrides::sheet_text() const {
    std::string out{};
    if (!roles_.empty()) {
        out.append(":root {\n");
        for (const RoleEntry& entry : roles_) {
            out.append("  ");
            out.append(colour_role_name(entry.role));
            out.append(": ");
            append_colour(out, entry.foreground);
            if (entry.background.has_value()) {
                out.append(" on ");
                append_colour(out, *entry.background);
            }
            out.append(";\n");
        }
        out.append("}\n");
    }
    // One rule per target, in the order targets were first set: a target's
    // properties are gathered from every later entry naming it.
    std::vector<bool> written(properties_.size(), false);
    for (std::size_t first = 0U; first < properties_.size(); ++first) {
        if (written[first]) continue;
        const ThemeOverrideTarget& target = properties_[first].target;
        append_kebab(out, target.control);
        if (!target.part.empty()) {
            out.append("::part(");
            append_kebab(out, target.part);
            out.push_back(')');
        }
        if (target.state.has_value()) append_state(out, *target.state);
        out.append(" {\n");
        for (std::size_t index = first; index < properties_.size(); ++index) {
            const PropertyEntry& entry = properties_[index];
            if (written[index] || !(entry.target == target)) continue;
            written[index] = true;
            out.append("  ");
            out.append(property_name(entry.property));
            out.append(": ");
            if (colour_property(entry.property)) {
                append_colour(out, entry.colour);
            } else {
                append_length(out, entry.property, entry.length);
            }
            out.append(";\n");
        }
        out.append("}\n");
    }
    return out;
}

} // namespace gui_forms
