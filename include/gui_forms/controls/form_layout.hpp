#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/form_layout_capabilities.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// A form of labelled rows (the ImGui-class demonstrator's I9): a caption
// column beside a column of controls, each row as tall as its control (or
// its caption, if taller) or as the height add_row gave it. The caption
// column is as wide as the widest caption, or caption_width when set; the
// controls take the rest of the width. Captions are Label children the
// layout makes and owns, aligned by caption_alignment; each caption names
// its control to assistive technology (it becomes the control's accessible
// name when the control has none of its own). Children added with add_child
// rather than add_row are not rows: they keep their own requested bounds.
// A row's control removed other than by remove_row (remove_child, or its
// disposal) leaves an empty row: the row and its caption take no space
// until remove_row of that control, or forget_empty_rows, drops it. A row
// whose control is hidden (visible false) takes no space either, and comes
// back when the control is shown.
//
// Threads and memory: the UI thread; one row record per row.
// ic: gui_forms::FormLayout: generated from include/gui_forms/controls/form_layout.ic.toml by tools/ic_declarations.py;
// ic: edit the declaration, not these lines. Thread: ui, unless a call says otherwise.
class GUI_FORMS_API FormLayout final : public Panel, public FormLayoutCapabilities<FormLayout> {
public:
    explicit FormLayout(StableId stable_id);

    // Appends a row: a caption made here (stable ID `<control's ID>.caption`)
    // and `control`, both added as children. `height` is the row's height in
    // logical units, or 0 for the control's measured height. Throws
    // std::invalid_argument, changing nothing, for a null control, one with
    // a parent, or a negative or non-finite height. Returns the caption.
    std::shared_ptr<Label> add_row(std::string caption, Control::Ptr control, double height = 0.0);
    // Removes the row holding `control`, its caption and the control; false
    // when no row holds it.
    bool remove_row(const Control& control);
    [[nodiscard]] std::size_t row_count() const noexcept { return rows_.size(); }
    // Removes every empty row and its caption; returns how many.
    std::size_t forget_empty_rows();
    // The caption of the row holding `control`, or null.
    [[nodiscard]] std::shared_ptr<Label> caption_of(const Control& control) const;
    // The caption column's width at the last layout.
    [[nodiscard]] double used_caption_width() const noexcept { return used_caption_width_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

private:
    struct Row final {
        std::shared_ptr<Label> caption{};
        Control::WeakPtr control{};
        double height{};
    };

    // The caption column's width for `available` (caption_width, or the
    // widest caption's measure).
    [[nodiscard]] double caption_column(Size available) const;
    // The row's height with the control `width` wide; 0 for an empty or
    // hidden row.
    [[nodiscard]] double row_height(const Row& row, double width) const;
    // The row's control while it is still this layout's child, else null.
    [[nodiscard]] Control::Ptr row_control(const Row& row) const;
    // row_control while the control is also visible: a hidden control's row
    // and its caption take no space.
    [[nodiscard]] Control::Ptr shown_control(const Row& row) const;
    // Removes rows_[index], its caption and (when still a child) its control.
    void erase_row(std::size_t index);

    std::vector<Row> rows_{};
    double used_caption_width_{};
    // property_codemod begin FormLayout (tools/property_families/form_layout.json; generated, do not edit)
public:
    // FormLayout's property table: Panel's entries, then its own.
    static constexpr std::size_t form_layout_property_count = Panel::panel_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, form_layout_property_count>& form_layout_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct FormLayoutCaptionWidthCapability<FormLayout>;
    friend struct FormLayoutCaptionAlignmentCapability<FormLayout>;
    friend struct FormLayoutRowSpacingCapability<FormLayout>;
    friend struct FormLayoutColumnSpacingCapability<FormLayout>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    // ic: Requires finite(incoming) and incoming >= 0 and incoming <= 4096, else failure invalid_argument, unchanged.
    void prepare_write(const decltype(caption_width)& cell, double& incoming);
    void written(const decltype(caption_width)& cell);
    void prepare_write(const decltype(caption_alignment)& cell, HorizontalAlignment& incoming);
    void written(const decltype(caption_alignment)& cell);
    // ic: Requires finite(incoming) and incoming >= 0 and incoming <= 256, else failure invalid_argument, unchanged.
    void prepare_write(const decltype(row_spacing)& cell, double& incoming);
    void written(const decltype(row_spacing)& cell);
    // ic: Requires finite(incoming) and incoming >= 0 and incoming <= 256, else failure invalid_argument, unchanged.
    void prepare_write(const decltype(column_spacing)& cell, double& incoming);
    void written(const decltype(column_spacing)& cell);
    // property_codemod end FormLayout
};

} // namespace gui_forms
