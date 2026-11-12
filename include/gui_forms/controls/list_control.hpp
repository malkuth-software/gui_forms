#pragma once

#include "gui_forms/controls/panel.hpp"

namespace gui_forms {

// The list controls' base, as Windows Forms' ListControl: ListBox (and
// CheckedListBox through it) and ComboBox derive from it, so a ListControl
// pointer holds any of them. It cannot be made on its own (its constructor
// is protected).
//
// It carries only what those share today, and that is nothing beyond Panel.
// Windows Forms puts the data source, display and value members and an
// abstract SelectedIndex here; GUI.Forms has no data source on either list
// yet, and the two spell the selection differently (ListBox reads it with
// selected_index() over a multiple selection, ComboBox holds it in its
// selected_index property cell), so no common member exists to move up. The
// items stay on each leaf, as in Windows Forms. It declares no virtual
// function of its own: nothing varies between the leaves through it.
class ListControl : public Panel {
public:
    ~ListControl() override;

protected:
    explicit ListControl(StableId stable_id);
};

} // namespace gui_forms
