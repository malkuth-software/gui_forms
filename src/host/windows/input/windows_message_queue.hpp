#pragma once

#include <windows.h>

namespace gui_forms::host::detail {

// Synchronous borrowed reader; neither the callback nor context is retained.
// The native adapter and deterministic queue-category controls use this seam.
using WindowMessageReader = bool(void*, MSG&, UINT, UINT, UINT);

[[nodiscard]] inline bool read_native_window_message(void*, MSG& message,
    const UINT first, const UINT last, const UINT flags) noexcept {
    const BOOL available = PeekMessageW(&message, nullptr, first, last, flags);
    const bool found = available != FALSE;
    return found;
}

// Alternate input-range retrieval with an ordinary queue turn. The caller
// bounds batches and presents afterward; individual callbacks are not timed
// out. Preserve posted-character precedence within the keyboard/mouse range.
// Whether `message` is non-client mouse input: a press, release, double
// click or motion over a window's frame or caption.
[[nodiscard]] constexpr bool non_client_mouse_message(const UINT message) noexcept {
    const bool non_client = message >= WM_NCMOUSEMOVE && message <= WM_NCXBUTTONDBLCLK;
    return non_client;
}

// Whether a primary press on the frame part `hit` (a WM_NCHITTEST code)
// may start DefWindowProc's move or size loop now. That loop ends on the
// button's release; one started after the release has already happened
// (the press was dispatched late, behind a long frame) would wait for a
// second click (owner, 2026-11-10: a resize that took a second click). A
// press on any other part (the system buttons, the menu) is always passed
// on. `primary_down` is the physical primary button's state now.
[[nodiscard]] constexpr bool frame_press_may_drag(const WPARAM hit, const bool primary_down) noexcept {
    const bool drag_part = hit == HTCAPTION || hit == HTLEFT || hit == HTRIGHT || hit == HTTOP ||
                           hit == HTTOPLEFT || hit == HTTOPRIGHT || hit == HTBOTTOM || hit == HTBOTTOMLEFT ||
                           hit == HTBOTTOMRIGHT || hit == HTGROWBOX;
    const bool may = !drag_part || primary_down;
    return may;
}

[[nodiscard]] inline bool take_window_message_with_reader(MSG& message, const bool input_first,
    WindowMessageReader& read, void* const context) {
    // A press on a frame edge or the caption starts DefWindowProc's move or
    // size loop, which ends on the button's release. The key and mouse
    // range read below would take a later release (WM_LBUTTONUP once the
    // pointer is over the client) before that earlier press, and the loop
    // would then wait for a second click. Non-client mouse input that is
    // the next hardware input is taken first, in order, unless a translated
    // character is waiting (it precedes all later input).
    if (input_first) {
        MSG next{};
        MSG character{};
        const bool hardware = read(context, next, 0U, 0U, PM_NOREMOVE | PM_QS_INPUT);
        if (hardware && non_client_mouse_message(next.message) &&
            !read(context, character, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE | PM_QS_POSTMESSAGE) &&
            read(context, message, next.message, next.message, PM_REMOVE | PM_QS_INPUT)) {
            return true;
        }
    }
    // TranslateMessage posts WM_CHAR. A hardware-only read first could take a
    // later Backspace before that character when private wakes fill the posted
    // queue. Range filtering keeps native category precedence for these input
    // messages while excluding application-private render/managed wakes.
    if (input_first &&
        read(context, message, WM_KEYFIRST, WM_MOUSELAST, PM_REMOVE)) {
        return true;
    }
    // Other hardware input (for example non-client input outside the range)
    // still gets a preferred retrieval opportunity before ordinary work.
    if (input_first &&
        read(context, message, 0U, 0U, PM_REMOVE | PM_QS_INPUT)) {
        return true;
    }
    const bool available = read(context, message, 0U, 0U, PM_REMOVE);
    return available;
}

[[nodiscard]] inline bool take_window_message(MSG& message, const bool input_first) {
    const bool available = take_window_message_with_reader(
        message, input_first, read_native_window_message, nullptr);
    return available;
}

} // namespace gui_forms::host::detail
