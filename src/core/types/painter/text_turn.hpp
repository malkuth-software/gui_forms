#pragma once

#include <stxlib/glyph_face.hpp>

#include <cmath>
#include <cstdint>

namespace gui_forms::detail {

// The turn turned text is drawn with: STXlib's (stxlib::glyphs::turn_matrix),
// so that what is told of a turned run's place (rotated_text_bounds,
// rotated_text_anchor, a part's turned origin) and the matrix it is drawn
// through are the same numbers. Its sine and cosine are worked out in whole
// numbers, the same on every host; each is within one 65536th of the true
// value and the quarter turns are exact.

// The angle as turn_matrix takes it: degrees reduced to (-360, 360), in 16.16
// (rounded to the nearest 65536th, halves away from zero). 0 for an angle
// that is not finite.
[[nodiscard]] inline std::int64_t text_turn_degrees_16_16(const double degrees) noexcept {
    if (!std::isfinite(degrees)) return 0;
    const double reduced = std::fmod(degrees, 360.0);
    const long long fixed = std::llround(reduced * 65536.0);
    const std::int64_t result = static_cast<std::int64_t>(fixed);
    return result;
}

// The turn's cosine and sine for text turned anticlockwise as seen by
// `degrees`: on the y-down surface, x' = cosine x + sine y and
// y' = -sine x + cosine y.
struct TextTurnEntries final {
    double cosine{1.0};
    double sine{0.0};
};

[[nodiscard]] inline TextTurnEntries text_turn_entries(const double degrees) noexcept {
    const stxlib::glyphs::TextMatrix matrix = stxlib::glyphs::turn_matrix(text_turn_degrees_16_16(degrees));
    TextTurnEntries entries{};
    entries.cosine = static_cast<double>(matrix.xx) / 65536.0;
    entries.sine = static_cast<double>(matrix.yx) / 65536.0;
    return entries;
}

} // namespace gui_forms::detail
