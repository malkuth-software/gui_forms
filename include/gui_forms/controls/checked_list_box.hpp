#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/checked_list_box_capabilities.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

struct GUI_FORMS_API ItemCheckEvent final {
    std::size_t index{};
    CheckState current_state{CheckState::unchecked};
    CheckState new_state{CheckState::unchecked};
    bool cancel{};
};

class GUI_FORMS_API CheckedListBox final : public ListBox, public CheckedListBoxCapabilities<CheckedListBox> {
public:
    explicit CheckedListBox(StableId stable_id);

    // Appends an item with its check state (the `items` cell's insertions
    // add unchecked items).
    void add_item(GUI_FORMS_LOCALIZABLE(items) std::string item, CheckState state);
    [[nodiscard]] CheckState item_check_state(std::size_t index) const;
    [[nodiscard]] bool item_checked(std::size_t index) const;
    void set_item_check_state(std::size_t index, CheckState state);
    void set_item_checked(std::size_t index, bool checked);
    void toggle_item(std::size_t index);
    [[nodiscard]] std::vector<std::size_t> checked_indices() const;
    [[nodiscard]] Event<ItemCheckEvent&>& item_checking() noexcept {
        return item_checking_;
    }
    [[nodiscard]] Event<std::size_t, CheckState>& item_check_state_changed()
        noexcept { return item_check_state_changed_; }

    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children()
        const override;
    bool on_semantic_child_action(std::string_view stable_id,
                                  SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(
        std::size_t item) const override;
    // Stage T1 item records: ListBox's row parts and states plus the check.
    struct GUI_FORMS_API ItemParts final {
        PartGeometry row;
        PartGeometry label;
        PartGeometry row_focus;
        PartGeometry check_well;
        PartGeometry check;
    };
    struct GUI_FORMS_API ItemStates final {
        bool selected{};
        bool hot{};
        bool current{};
        bool checked{};
        bool indeterminate{};
    };
    struct GUI_FORMS_API Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_key_down(KeyEventArgs& arguments) override;

    void on_items_changing(const CollectionEdit& edit) override;
    void on_items_changed(const CollectionEdit& edit) noexcept override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    [[nodiscard]] double row_text_left() const noexcept override;
    void paint_row_adornment(Painter& painter, std::size_t index,
                             Rect row_bounds, bool selected,
                             bool focused) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;

private:
    // on_mouse_down's handling: ListBox's, then the pressed item's toggle.
    void toggle_on_press(MouseEventArgs& arguments);
    std::vector<CheckState> check_states_;
    Event<ItemCheckEvent&> item_checking_;
    Event<std::size_t, CheckState> item_check_state_changed_;
    // property_codemod begin CheckedListBox (tools/property_families/checked_list_box.json; generated, do not edit)
private:
    friend struct CheckedListBoxCheckOnClickCapability<CheckedListBox>;
    friend struct CheckedListBoxIndicatorSizeCapability<CheckedListBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(check_on_click)& cell, bool& incoming);
    void written(const decltype(check_on_click)& cell);
    void prepare_write(const decltype(indicator_size)& cell, double& incoming);
    void written(const decltype(indicator_size)& cell);
    // property_codemod end CheckedListBox
};

} // namespace gui_forms
