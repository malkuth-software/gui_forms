#pragma once

#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstdint>

namespace gui_forms {

// How much a notice matters: an InfoBanner's or a Toast's (W-4). Each value
// is also the notice's custom state of the same name, and its index among
// the notice's custom states is the value's number.
enum class NoticeSeverity : std::uint8_t {
    information,  // something to know; nothing is wrong
    warning,      // something may go wrong or needs attention
    success,      // something finished as asked
    error,        // something failed
};

// Whether a severity is one of the four values. A severity read from an
// integer (a binding, a file) can be any byte, and InfoBanner and Toast use
// it as a custom state index, so both refuse an unknown one.
[[nodiscard]] constexpr bool notice_severity_known(NoticeSeverity severity) noexcept {
    const bool known = severity == NoticeSeverity::information || severity == NoticeSeverity::warning ||
                       severity == NoticeSeverity::success || severity == NoticeSeverity::error;
    return known;
}

// A severity's colour, which InfoBanner's keyline and icon, Toast's icon
// and InstrumentRack's module keyline draw as an authored foreground over
// the notice's role, under the severity's custom state (`:state(warning)`).
// Mid-tone, so that each reads on a light face and a dark one; high
// contrast replaces it with the role's own colour. Provisional until T4's
// floor gives the status colours roles.
[[nodiscard]] constexpr Color notice_severity_colour(NoticeSeverity severity) noexcept {
    switch (severity) {
    case NoticeSeverity::information: return Color::rgba(38, 114, 185);
    case NoticeSeverity::warning: return Color::rgba(196, 132, 0);
    case NoticeSeverity::success: return Color::rgba(46, 139, 87);
    case NoticeSeverity::error: return Color::rgba(196, 43, 28);
    default: break;
    }
    // A value outside the enumeration: information's colour.
    return Color::rgba(38, 114, 185);
}

} // namespace gui_forms
