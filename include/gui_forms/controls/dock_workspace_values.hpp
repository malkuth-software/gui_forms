#pragma once

// DockWorkspace's setting types, apart from the component so its
// capabilities header can name them.

#include <cstdint>

namespace gui_forms {

// How the program is asked before documents close at the user's request
// (owner decision DK-31, H1): one closing event per document, in order, or
// one event for all of them (a combined prompt, such as a save-changes
// list).
enum class DockClosePrompt : std::uint8_t { each, combined };

// What opens an auto-hidden panel's flyout from its side tab (owner decision
// DK-27, E1-1): resting the pointer on the tab for DockSite's hover delay or
// clicking it (the default), or only clicking it.
enum class DockFlyoutOpening : std::uint8_t { hover_or_click, click };

} // namespace gui_forms
