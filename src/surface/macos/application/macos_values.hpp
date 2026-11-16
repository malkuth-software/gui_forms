#pragma once

// Values that cross the AppKit boundary: event timestamps in host
// nanoseconds and UTF-8 text to and from Foundation strings.

#include "macos_host_private.hpp"

namespace gui_forms::host::macos {

// The steady clock now, in nanoseconds since its epoch.
std::uint64_t host_now_nanoseconds();

// The event's own timestamp in nanoseconds; the steady clock now for a
// missing event or a timestamp that is not finite or is negative.
std::uint64_t host_event_nanoseconds(NSEvent* event);

// A new NSString holding `text` (UTF-8); nil when the bytes are not UTF-8.
NSString* native_string(std::string_view text);

// The UTF-8 bytes of an NSString or NSAttributedString; empty for anything
// else.
std::string utf8_string(id value);

} // namespace gui_forms::host::macos

using gui_forms::host::macos::host_event_nanoseconds;
using gui_forms::host::macos::host_now_nanoseconds;
using gui_forms::host::macos::native_string;
using gui_forms::host::macos::utf8_string;
