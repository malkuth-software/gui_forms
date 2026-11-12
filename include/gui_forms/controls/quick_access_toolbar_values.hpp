#pragma once

// The value types of QuickAccessToolbar's properties, apart from the control
// so its capabilities header can name them.

#include <cstdint>

namespace gui_forms {

// Where the toolbar sits: on the ribbon's tab row beside the File tab, or in
// its own row below the ribbon (owner decision RB-12). The toolbar only
// records the choice; whoever places the toolbar applies it (the Ribbon
// does not host the toolbar yet).
enum class QuickAccessToolbarPlacement : std::uint8_t { above_ribbon, below_ribbon };

} // namespace gui_forms
