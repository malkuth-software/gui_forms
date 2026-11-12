#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <optional>

namespace gui_forms {

// The list controls' base, as Windows Forms' ListControl: ListBox (and
// CheckedListBox through it) and ComboBox derive from it, so a ListControl
// pointer holds any of them. It cannot be made on its own (its constructor
// is protected).
//
// It carries the selected index and its change event (Windows Forms'
// SelectedIndex and SelectedIndexChanged; owner-accepted base proposal,
// CP29 I5). ListBox's selected index is its first selected item; it raises
// selected_index_changed after selection_changed, when the first selected
// item moved, and keeps select_index for extended and toggled selection.
// ComboBox's is its selected_index cell, which it keeps spelled as a cell
// (a ComboBox names the cell; selected_index() is read through a
// ListControl). The data source, display and value members Windows Forms
// adds here have no GUI.Forms counterpart yet, and the items stay on each
// leaf, as in Windows Forms. It declares no virtual function of its own.
class GUI_FORMS_API ListControl : public Panel {
public:
    ~ListControl() override;

    // The selected item's index, or none without a selection.
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept { return selected_index_value_; }
    // Raised with the new index after the selected index changes.
    [[nodiscard]] Event<std::optional<std::size_t>>& selected_index_changed() noexcept {
        return selected_index_changed_;
    }

protected:
    explicit ListControl(StableId stable_id);

    // Records the leaf's selected index before the leaf raises its own
    // selection events; returns whether it changed. The leaf raises
    // selected_index_changed (publish_change) when it did, after them.
    bool record_selected_index(std::optional<std::size_t> index) noexcept;

    Event<std::optional<std::size_t>> selected_index_changed_;

private:
    std::optional<std::size_t> selected_index_value_{};
};

} // namespace gui_forms
