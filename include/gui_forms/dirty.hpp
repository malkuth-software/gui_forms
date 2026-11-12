#pragma once

#include <cstdint>
#include <type_traits>

namespace gui_forms {

// What a mutation makes stale. Owner ruling D4 (audit A18): the kinds
// nothing read (text, style, resource; bits 4 to 6) are gone, and their bit
// values are not reused. accessibility, like semantics, retires the
// published semantic snapshot; the sealed catalogue keeps its number.
enum class Dirty : std::uint16_t {
    none = 0,
    measure = 1U << 0U,
    arrange = 1U << 1U,
    paint = 1U << 2U,
    hit_test = 1U << 3U,
    semantics = 1U << 7U,
    accessibility = 1U << 8U,
    layout = (1U << 0U) | (1U << 1U),
};

[[nodiscard]] constexpr Dirty operator|(Dirty left, Dirty right) noexcept {
    using Value = std::underlying_type_t<Dirty>;
    return static_cast<Dirty>(static_cast<Value>(left) | static_cast<Value>(right));
}

[[nodiscard]] constexpr Dirty operator&(Dirty left, Dirty right) noexcept {
    using Value = std::underlying_type_t<Dirty>;
    return static_cast<Dirty>(static_cast<Value>(left) & static_cast<Value>(right));
}

constexpr Dirty& operator|=(Dirty& left, Dirty right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_dirty(Dirty value, Dirty flag) noexcept {
    return (value & flag) != Dirty::none;
}

[[nodiscard]] constexpr Dirty without_dirty(Dirty value, Dirty flag) noexcept {
    using Value = std::underlying_type_t<Dirty>;
    return static_cast<Dirty>(static_cast<Value>(value) & ~static_cast<Value>(flag));
}

namespace invalidation {

inline constexpr Dirty bounds = Dirty::measure | Dirty::arrange | Dirty::paint |
                                Dirty::hit_test | Dirty::semantics |
                                Dirty::accessibility;
inline constexpr Dirty visual_tree = bounds;
inline constexpr Dirty visibility = bounds;
inline constexpr Dirty enabled = Dirty::paint | Dirty::hit_test | Dirty::semantics |
                                 Dirty::accessibility;
inline constexpr Dirty focusability = Dirty::hit_test | Dirty::semantics |
                                      Dirty::accessibility;
inline constexpr Dirty focus = Dirty::paint | Dirty::semantics | Dirty::accessibility;
inline constexpr Dirty paint_only = Dirty::paint;
inline constexpr Dirty text_content = Dirty::measure | Dirty::arrange | Dirty::paint |
                                      Dirty::hit_test | Dirty::semantics | Dirty::accessibility;
inline constexpr Dirty style_only = Dirty::paint;
inline constexpr Dirty resource_content = Dirty::measure | Dirty::arrange | Dirty::paint;
inline constexpr Dirty conservative_subtree = Dirty::measure | Dirty::arrange |
                                              Dirty::paint | Dirty::hit_test |
                                              Dirty::semantics | Dirty::accessibility;

} // namespace invalidation

} // namespace gui_forms
