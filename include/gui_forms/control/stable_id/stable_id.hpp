#pragma once

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gui_forms {

// Whether `value` is a stable ID: one or more segments separated by '.',
// each one or more runs of lowercase ASCII letters and digits joined by
// single '-' ([a-z0-9]+(-[a-z0-9]+)*). The dots are hierarchy; theme
// selectors and locale files name controls by these segments.
[[nodiscard]] constexpr bool valid_stable_id(std::string_view value) noexcept {
    if (value.empty()) return false;
    bool run_open = false;
    char previous = '.';
    for (const char character : value) {
        const bool alphanumeric = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (alphanumeric) {
            run_open = true;
        } else if (character == '-' || character == '.') {
            if (!run_open) return false;
            run_open = false;
        } else {
            return false;
        }
        previous = character;
    }
    return previous != '-' && previous != '.';
}

namespace detail {
// Not constexpr: calling it from StableIdLiteral's constructor makes an
// invalid literal fail to compile.
void stable_id_literal_is_not_dot_separated_lowercase_dash_segments() noexcept;
} // namespace detail

// A stable ID written as a string literal, checked when the program is
// compiled. It borrows the literal's static storage.
class StableIdLiteral final {
public:
    template <std::size_t Size>
    consteval StableIdLiteral(const char (&literal)[Size]) : value_(literal, Size - 1U) {
        if (literal[Size - 1U] != '\0' || !valid_stable_id(value_)) {
            detail::stable_id_literal_is_not_dot_separated_lowercase_dash_segments();
        }
    }
    [[nodiscard]] constexpr std::string_view value() const& noexcept { return value_; }
    std::string_view value() const&& = delete;

private:
    std::string_view value_;
};

// Immutable authored identity shared by retained lookup, semantics, traces,
// and public construction seams. Runtime identity remains a separate concern.
// Every stable ID satisfies valid_stable_id: a literal is checked when the
// program is compiled, and a std::string when it is constructed.
class StableId final {
public:
    explicit StableId(StableIdLiteral literal) : value_(literal.value()) {}
    // A std::string or std::string_view built at run time. Throws
    // std::invalid_argument when it is not a valid stable ID. A string
    // literal never reaches this constructor (it is not deduced as either
    // type), so a literal is always checked at compile time; a bare
    // `const char*` matches neither constructor and must be wrapped.
    template <typename Text>
        requires std::same_as<std::remove_cvref_t<Text>, std::string> ||
                 std::same_as<std::remove_cvref_t<Text>, std::string_view>
    explicit StableId(Text&& value) : value_(std::forward<Text>(value)) {
        require_valid();
    }
    [[nodiscard]] std::string_view value() const& noexcept { return value_; }
    std::string_view value() const&& = delete;
    friend bool operator==(const StableId& left,
                           const StableId& right) noexcept {
        return left.value_ == right.value_;
    }

private:
    void require_valid() const;

    std::string value_;
};

} // namespace gui_forms
