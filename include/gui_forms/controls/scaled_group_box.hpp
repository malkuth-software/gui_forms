#pragma once

#include "gui_forms/controls/group_box.hpp"
#include "gui_forms/controls/scaled_group_box_capabilities.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace gui_forms {

class ScaledGroupBox final : public GroupBox, public ScaledGroupBoxCapabilities<ScaledGroupBox> {
public:
    explicit ScaledGroupBox(StableId stable_id, std::string initial_text = {},
                            Size initial_design_size = {1.0, 1.0});

    void add_at(Control::Ptr child, Rect design_bounds);
    void set_design_bounds(const Control& child, Rect design_bounds);
    [[nodiscard]] std::optional<Rect> design_bounds(const Control& child) const;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

private:
    using SlotMap = std::unordered_map<std::uint64_t, Rect>;
    void reconcile_slots();
    SlotMap slots_;
    // property_codemod begin ScaledGroupBox (tools/property_families/scaled_group_box.json; generated, do not edit)
public:
    // ScaledGroupBox's property table: GroupBox's entries, then its own.
    static constexpr std::size_t scaled_group_box_property_count = GroupBox::group_box_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, scaled_group_box_property_count>& scaled_group_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ScaledGroupBoxDesignSizeCapability<ScaledGroupBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(design_size)& cell, Size& incoming);
    void written(const decltype(design_size)& cell);
    // property_codemod end ScaledGroupBox
};

} // namespace gui_forms
