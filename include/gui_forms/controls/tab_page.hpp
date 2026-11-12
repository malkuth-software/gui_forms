#pragma once

#include "gui_forms/controls/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/tab_page_capabilities.hpp"

#include <string>

namespace gui_forms {

// One page of a TabControl: a panel with the text and image its tab shows.
class TabPage final : public Panel, public TabPageCapabilities<TabPage> {
public:
    explicit TabPage(StableId stable_id, std::string initial_text = {});

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

private:
    void invalidate_tab();

    std::string image_key_{};
    int image_index_{-1};
    // property_codemod begin TabPage (tools/property_families/tab_page.json; generated, do not edit)
private:
    friend struct TextCapability<TabPage>;
    friend struct TabPageImageKeyCapability<TabPage>;
    friend struct TabPageImageIndexCapability<TabPage>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void write_view(const decltype(image_key)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(image_key)& cell) const;
    void write_view(const decltype(image_index)& cell, int incoming);
    [[nodiscard]] int read_view(const decltype(image_index)& cell) const;
    // property_codemod end TabPage
};

} // namespace gui_forms
