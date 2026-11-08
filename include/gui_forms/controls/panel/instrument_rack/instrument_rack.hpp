#pragma once

#include "gui_forms/input_controls.hpp"
#include "gui_forms/instrument/instrument_types.hpp"
#include "gui_forms/controls/panel/instrument_rack/instrument_rack_capabilities/instrument_rack_capabilities.hpp"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace gui_forms {

// A bounded retained rack for compact editing instruments.
class InstrumentRack final : public Panel, public InstrumentRackCapabilities<InstrumentRack> {
public:
    explicit InstrumentRack(StableId stable_id);
    ~InstrumentRack() override;

    [[nodiscard]] const std::vector<InstrumentModuleSpec>& modules() const noexcept;
    void set_modules(std::vector<InstrumentModuleSpec> modules);
    [[nodiscard]] Control::Ptr field_editor(std::string_view module_id,
                                            std::string_view field_id) const;
    [[nodiscard]] std::optional<Rect> module_bounds(
        std::string_view module_id) const noexcept;

    bool set_module_enabled(std::string_view module_id, bool enabled);
    bool set_module_state(std::string_view module_id,
                          InstrumentModuleState state,
                          std::string status_text);
    bool set_field_value(std::string_view module_id,
                         std::string_view field_id,
                         std::string value);
    bool set_field_validation(std::string_view module_id,
                              std::string_view field_id,
                              std::string message);

    void set_action_content(Control::Ptr content,
                            double minimum_width = 170.0);
    [[nodiscard]] Control::Ptr action_content() const noexcept;

    [[nodiscard]] double content_height() const noexcept;
    [[nodiscard]] double preferred_height(double available_width) const;

    [[nodiscard]] Event<const InstrumentFieldChange&>& field_changed() noexcept {
        return field_changed_;
    }
    [[nodiscard]] Event<const InstrumentFieldChange&>& field_committed() noexcept {
        return field_committed_;
    }
    [[nodiscard]] Event<const InstrumentModuleToggle&>& module_toggled() noexcept {
        return module_toggled_;
    }
    [[nodiscard]] Event<const InstrumentModuleRequest&>& remove_requested() noexcept {
        return remove_requested_;
    }
    [[nodiscard]] Event<const InstrumentModuleMoveRequest&>&
    move_requested() noexcept { return move_requested_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_pointer(PointerEvent& event) override;
    void on_pointer_bubble(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Event<const InstrumentFieldChange&> field_changed_;
    Event<const InstrumentFieldChange&> field_committed_;
    Event<const InstrumentModuleToggle&> module_toggled_;
    Event<const InstrumentModuleRequest&> remove_requested_;
    Event<const InstrumentModuleMoveRequest&> move_requested_;
    // property_codemod begin InstrumentRack (tools/property_families/instrument_rack.json; generated, do not edit)
private:
    friend struct InstrumentRackModuleWidthCapability<InstrumentRack>;
    friend struct InstrumentRackModuleHeightCapability<InstrumentRack>;
    friend struct InstrumentRackRackGapCapability<InstrumentRack>;
    friend struct InstrumentRackScrollOffsetCapability<InstrumentRack>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(module_width)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(module_width)& cell) const;
    void write_view(const decltype(module_height)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(module_height)& cell) const;
    void write_view(const decltype(rack_gap)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(rack_gap)& cell) const;
    void write_view(const decltype(scroll_offset)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(scroll_offset)& cell) const;
    // property_codemod end InstrumentRack
};

} // namespace gui_forms
