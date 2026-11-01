#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/value.hpp"

#include <span>
#include <vector>

namespace gui_forms {

struct SectionItem final {
    int id{};
    GUI_FORMS_LOCALIZABLE(text) std::string label{};
    Control::Ptr content{};
    Value<bool>* expanded{}; // Borrowed; a missing model uses retained local state.
};

class ExpandableSections final : public Panel {
public:
    explicit ExpandableSections(StableId id);
    ~ExpandableSections() override;
    void set_items(std::vector<SectionItem> items);
    [[nodiscard]] std::span<const SectionItem> items() const noexcept;
    [[nodiscard]] bool expanded(int index) const;
    void set_expanded(int index, bool expanded);
    void set_single_open(bool enabled);
    [[nodiscard]] bool single_open() const noexcept { return single_open_; }
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
    bool single_open_{};
    bool replacing_{};
};

} // namespace gui_forms
