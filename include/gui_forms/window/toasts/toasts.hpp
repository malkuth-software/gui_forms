#pragma once

#include <chrono>
#include <cstdint>

namespace gui_forms {

// Why a window's toast closed (W-4; coordinator ruling: one presenter per
// window, on the tooltip presenter's pattern).
enum class ToastDismissReason : std::uint8_t {
    timed_out,   // its duration ran out
    closed,      // the user closed it (its close mark or close action)
    action,      // the user took its action
    replaced,    // another toast took its place
    hidden,      // the application hid it (Window::hide_toast)
    owner_gone,  // its owner left the window, was hidden or was disposed
};

// A window's toast closed: `id` is the one show_toast returned.
struct ToastDismissal final {
    std::uint64_t id{};
    ToastDismissReason reason{ToastDismissReason::hidden};
};

// How long a toast stays unless the call says otherwise.
inline constexpr std::chrono::milliseconds default_toast_duration{5000};

} // namespace gui_forms
