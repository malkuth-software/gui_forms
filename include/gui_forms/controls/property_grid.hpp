#pragma once

#include "gui_forms/controls/property_list.hpp"
#include "gui_forms/inspection/property_editor_registry/property_editor_registry.hpp"
#include "gui_forms/inspection/property_value_converter_registry/property_value_converter_registry.hpp"
#include "gui_forms/controls/property_grid_capabilities.hpp"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace gui_forms {

class PropertyGrid final : public Panel, public PropertyGridCapabilities<PropertyGrid> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit PropertyGrid(StableId stable_id);
    ~PropertyGrid() override;
    void initialize_control_tree();

    [[nodiscard]] Control::Ptr selected_object() const noexcept;
    void set_selected_object(Control::Ptr object);
    [[nodiscard]] std::vector<Control::Ptr> selected_objects() const;
    void set_selected_objects(std::vector<Control::Ptr> objects);
    void refresh_properties();

    [[nodiscard]] std::shared_ptr<PropertyList> property_list() const noexcept;
    [[nodiscard]] Control::Ptr editor(std::string_view property_name) const;
    [[nodiscard]] std::shared_ptr<Button> reset_button(
        std::string_view property_name) const;
    [[nodiscard]] std::optional<PropertyDescriptor> selected_descriptor(
        std::string_view property_name) const;
    [[nodiscard]] std::optional<PropertyValueOrigin> selected_origin(
        std::string_view property_name) const;
    bool set_property_expanded(std::string_view property_name, bool expanded);
    [[nodiscard]] std::optional<bool> property_expanded(
        std::string_view property_name) const;

    bool try_set_property_value(std::string_view property_name,
                                BindingValue value);
    bool try_set_property_text(std::string_view property_name,
                               std::string_view text);
    bool activate_property_editor(std::string_view property_name);
    bool reset_property(std::string_view property_name);
    bool insert_collection_item(std::string_view property_name,
                                std::size_t index, BindingValue value);
    bool remove_collection_item(std::string_view property_name,
                                std::size_t index);
    bool move_collection_item(std::string_view property_name,
                              std::size_t from, std::size_t to);
    [[nodiscard]] std::optional<PropertyGridEditError> last_error() const;

    [[nodiscard]] Event<Control::Ptr>& selected_object_changed() noexcept {
        return selected_object_changed_;
    }
    [[nodiscard]] Event<const PropertyGridValueChange&>&
    property_value_changed() noexcept { return property_value_changed_; }
    [[nodiscard]] Event<const PropertyGridEditError&>& edit_failed() noexcept {
        return edit_failed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Event<Control::Ptr> selected_object_changed_;
    Event<const PropertyGridValueChange&> property_value_changed_;
    Event<const PropertyGridEditError&> edit_failed_;
    // property_codemod begin PropertyGrid (tools/property_families/property_grid.json; generated, do not edit)
public:
    // PropertyGrid's property table: Panel's entries, then its own.
    static constexpr std::size_t property_grid_property_count = Panel::panel_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, property_grid_property_count>& property_grid_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct PropertyGridPropertySortCapability<PropertyGrid>;
    friend struct PropertyGridConverterRegistryCapability<PropertyGrid>;
    friend struct PropertyGridEditorRegistryCapability<PropertyGrid>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(property_sort)& cell, PropertySort incoming);
    [[nodiscard]] PropertySort read_view(const decltype(property_sort)& cell) const;
    void write_view(const decltype(converter_registry)& cell, std::shared_ptr<PropertyValueConverterRegistry> incoming);
    [[nodiscard]] std::shared_ptr<PropertyValueConverterRegistry> read_view(const decltype(converter_registry)& cell) const;
    void write_view(const decltype(editor_registry)& cell, std::shared_ptr<PropertyEditorRegistry> incoming);
    [[nodiscard]] std::shared_ptr<PropertyEditorRegistry> read_view(const decltype(editor_registry)& cell) const;
    // property_codemod end PropertyGrid
};

} // namespace gui_forms
