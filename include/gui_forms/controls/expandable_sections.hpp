#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/value.hpp"
#include "gui_forms/controls/expandable_sections_capabilities.hpp"

#include <span>
#include <vector>

namespace gui_forms {

struct SectionItem final {
    int id{};
    GUI_FORMS_LOCALIZABLE(text) std::string label{};
    Control::Ptr content{};
    Value<bool>* expanded{}; // Borrowed; a missing model uses retained local state.
};

class ExpandableSections final : public Panel, public ExpandableSectionsCapabilities<ExpandableSections> {
public:
    explicit ExpandableSections(StableId id);
    ~ExpandableSections() override;
    void set_items(std::vector<SectionItem> items);
    [[nodiscard]] std::span<const SectionItem> items() const noexcept;
    [[nodiscard]] bool expanded(int index) const;
    void set_expanded(int index, bool expanded);
    [[nodiscard]] Event<int, bool>& toggled() noexcept { return toggled_; }
    [[nodiscard]] Control* part(std::string_view name, int index) const;
    void arrange(Rect bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    struct Entry;
    struct Revision;
    struct HeadingChange;
    [[nodiscard]] Entry& entry(int index) const;
    void on_toggled(int id, bool expanded);
    std::shared_ptr<Revision> revision_{};
    Event<int, bool> toggled_{};
    bool replacing_{};
    // property_codemod begin ExpandableSections (tools/property_families/expandable_sections.json; generated, do not edit)
private:
    friend struct ExpandableSectionsSingleOpenCapability<ExpandableSections>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(single_open)& cell, bool& incoming);
    void written(const decltype(single_open)& cell);
    // property_codemod end ExpandableSections
};

} // namespace gui_forms
