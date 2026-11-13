#pragma once

#include <cstdint>

namespace gui_forms {

// How a ChoiceGroup draws its choices (W-4; owner decision D6: the
// segmented control is a ChoiceGroup presentation, not a control of its
// own). Selection, keyboard and semantics are the same in both.
enum class ChoiceGroupPresentation : std::uint8_t {
    buttons,    // a row of separate buttons, the selected one drawn selected
    segmented,  // one track divided into segments, a selection indicator
                // behind the selected segment
};

} // namespace gui_forms
