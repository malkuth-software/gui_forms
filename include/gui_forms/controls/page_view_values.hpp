#pragma once

// The value types of PageView's properties, apart from the control so its
// capabilities header can name them.

#include <cstdint>

namespace gui_forms {

// What Ctrl+Tab and Ctrl+Shift+Tab (the cycle_forward and cycle_backward key
// commands) do while focus is inside the view (owner decisions DK-30 and
// DK-38): select the next or previous page (a view on its own), or pass the
// key on (a view inside docking, whose navigator takes it). Ctrl+PageDown and
// Ctrl+PageUp select in every view.
enum class PageViewCtrlTab : std::uint8_t { select_next, pass };

// What a view does with a close request no handler took (owner decision
// DK-38). `remove_when_unhandled`, the default: the view removes the page.
// `request_only`, for an owner that decides (a docking workspace): the view
// never removes a page itself.
enum class PageViewClosePolicy : std::uint8_t { remove_when_unhandled, request_only };

} // namespace gui_forms
