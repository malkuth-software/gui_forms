#pragma once

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

} // namespace gui_forms
