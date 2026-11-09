#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/panel/scaled_panel/scaled_panel_capabilities/scaled_panel_capabilities.hpp"

#include <optional>
#include <unordered_map>

namespace gui_forms {

class ScaledPanel : public Panel, public ScaledPanelCapabilities<ScaledPanel> {
public:
    explicit ScaledPanel(StableId stable_id, Size initial_design_size = {1.0, 1.0});

    void add_at(Control::Ptr child, Rect design_bounds);
    void set_design_bounds(const Control& child, Rect design_bounds);
    [[nodiscard]] std::optional<Rect> design_bounds(const Control& child) const;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

private:
    using SlotMap = std::unordered_map<std::uint64_t, Rect>;
    void reconcile_slots();
    SlotMap slots_;
    // property_codemod begin ScaledPanel (tools/property_families/scaled_panel.json; generated, do not edit)
public:
    // ScaledPanel's property table: Panel's entries, then its own.
    static constexpr std::size_t scaled_panel_property_count = Panel::panel_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, scaled_panel_property_count>& scaled_panel_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ScaledPanelDesignSizeCapability<ScaledPanel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(design_size)& cell, Size& incoming);
    void written(const decltype(design_size)& cell);
    // property_codemod end ScaledPanel
};

} // namespace gui_forms
