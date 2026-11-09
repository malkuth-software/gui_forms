#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

// Names (stage T1, owner decisions 7 and 8). Every name GUI.Forms registers
// (control ids, parts, states, item systems, interactions, properties,
// compound fields, enumeration types and values) is a sequence of lowercase
// words with one stored spelling, snake_case (`auto_size_mode`). C++ string
// APIs, the C ABI, the schema and every name the library reports use it.
// Two spellings are derived from it and never stored: CSS-family documents
// (themes, form documents) write the words in kebab-case
// (`auto-size-mode`), and PropertyGrid and C# show them in PascalCase
// (`AutoSizeMode`). Names are a versioned public contract: within a major
// version they are only added.

// True when `name` is lowercase ASCII words joined by single underscores:
// [a-z][a-z0-9]*(_[a-z0-9]+)*.
[[nodiscard]] bool is_registered_name(std::string_view name) noexcept;
// The stored spelling of a name written in any of the three spellings.
// Hyphens become underscores; a word boundary inside PascalCase or camelCase
// becomes an underscore ("HScrollBar" is "h_scroll_bar"); ASCII letters are
// lowercased. Dots and brackets pass through, so a path converts segment by
// segment ("Bounds.Width" is "bounds.width"), and other characters are kept.
// Leading and trailing whitespace is ignored. Throws std::invalid_argument
// for an empty name.
[[nodiscard]] std::string registered_name(std::string_view spelling);
// The kebab-case spelling CSS-family documents write: "auto-size-mode".
[[nodiscard]] std::string css_spelling(std::string_view registered);
// The PascalCase display spelling: "AutoSizeMode", "Bounds.Width".
[[nodiscard]] std::string display_spelling(std::string_view registered);
// How a display shows a name that may not be registered: the display
// spelling of a registered name, and any other text (an application's own
// label) unchanged. A flags value ("top, left") is shown name by name
// ("Top, Left").
[[nodiscard]] std::string display_label(std::string_view text);

// Interactions: the events a control or component type raises, as the one
// schema names them (stage T1). C++ registers a handler through the type's
// dot API (`(*button).clicked().subscribe(...)`); the schema names the same
// interaction by the accessor's name (`clicked`, `value_changed`).

// One interaction. `payload` spells the handler's argument types as the
// event accessor declares them, without `const` or reference marks and
// separated by ", " ("ButtonBase", "std::size_t, CheckState"); it is empty
// for an interaction without arguments. `mutable_payload` is true when some
// argument reaches the handler by mutable reference, as a validation the
// handler may cancel does.
struct InteractionDeclaration final {
    std::string_view name;
    std::string_view payload;
    bool mutable_payload{};

    friend constexpr bool operator==(const InteractionDeclaration& left,
                                     const InteractionDeclaration& right) noexcept {
        return left.name == right.name && left.payload == right.payload &&
               left.mutable_payload == right.mutable_payload;
    }
};

// Upper bound on one type's interactions, its inherited ones included
// (DataGridView declares 66 with the key events).
inline constexpr std::size_t maximum_interactions = 96;

// Reasons a type's interaction list is malformed.
enum class InteractionDefect : std::uint8_t {
    none,
    too_many_interactions,
    name_not_snake_case,
    duplicate_name,
};

// The result of validate_interactions: the first defect found and the index
// of the interaction it concerns, or none.
struct InteractionValidation final {
    InteractionDefect defect{InteractionDefect::none};
    std::size_t index{};
};

[[nodiscard]] std::string_view interaction_defect_name(InteractionDefect defect) noexcept;
[[nodiscard]] InteractionValidation validate_interactions(
    std::span<const InteractionDeclaration> interactions) noexcept;
// The position of the interaction named `name`, or `interactions.size()`.
[[nodiscard]] std::size_t find_interaction(std::span<const InteractionDeclaration> interactions,
                                           std::string_view name) noexcept;

} // namespace gui_forms
