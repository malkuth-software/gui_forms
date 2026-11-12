// Values that cross the AppKit boundary (macos_values.hpp).

#include "macos_values.hpp"

namespace gui_forms::host::macos {

std::uint64_t host_now_nanoseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::uint64_t host_event_nanoseconds(NSEvent* event) {
    if (event == nil || !std::isfinite(event.timestamp) || event.timestamp < 0.0) {
        return host_now_nanoseconds();
    }
    return static_cast<std::uint64_t>(event.timestamp * 1'000'000'000.0);
}

NSString* native_string(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

std::string utf8_string(id value) {
    NSString* string = nil;
    if ([value isKindOfClass:[NSAttributedString class]]) {
        string = [(NSAttributedString*)value string];
    } else if ([value isKindOfClass:[NSString class]]) {
        string = (NSString*)value;
    }
    if (string == nil) {
        return {};
    }
    const char* bytes = [string UTF8String];
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

} // namespace gui_forms::host::macos
