#pragma once

#include <cstdint>

namespace gui_forms {

// How much an InfoBanner's message matters (W-4). Each value is also the
// banner's custom state of the same name, and its index among the banner's
// custom states is the value's number.
enum class InfoBannerSeverity : std::uint8_t {
    information,  // something to know; nothing is wrong
    warning,      // something may go wrong or needs attention
    success,      // something finished as asked
    error,        // something failed
};

} // namespace gui_forms
