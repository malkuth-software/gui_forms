#pragma once

#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/wizard_page_capabilities.hpp"

namespace gui_forms {

// One page of a Wizard (the superset's P1 rest): a Panel whose controls are
// the page's content, with the title and description the wizard shows in
// its header and step indicator, and can_advance, the page's validation
// gate, which enables or disables the wizard's Next (or Finish) button
// while the page is shown. A WizardPage outside a Wizard is an ordinary
// Panel.
class WizardPage final : public Panel, public WizardPageCapabilities<WizardPage> {
public:
    explicit WizardPage(StableId stable_id);

private:
    // Tells the owning wizard, if any, that the page's title, description
    // or gate changed.
    void notify_wizard();
    // property_codemod begin WizardPage (tools/property_families/wizard_page.json; generated, do not edit)
public:
    // WizardPage's property table: Panel's entries, then its own.
    static constexpr std::size_t wizard_page_property_count = Panel::panel_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, wizard_page_property_count>& wizard_page_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct WizardPageTitleCapability<WizardPage>;
    friend struct WizardPageDescriptionCapability<WizardPage>;
    friend struct WizardPageCanAdvanceCapability<WizardPage>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(description)& cell, std::string& incoming);
    void written(const decltype(description)& cell);
    void prepare_write(const decltype(can_advance)& cell, bool& incoming);
    void written(const decltype(can_advance)& cell);
    // property_codemod end WizardPage
};

} // namespace gui_forms
