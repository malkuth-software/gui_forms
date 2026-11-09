#pragma once

#include "gui_forms/controls/panel/group_box/group_box_capabilities/group_box_capabilities.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <span>
#include <string>
#include <vector>

namespace gui_forms {

class GroupBox : public Panel, public GroupBoxCapabilities<GroupBox> {
public:
    explicit GroupBox(StableId stable_id, std::string initial_text = {});

    // Raised after `text` changes, with the new caption (Windows Forms'
    // TextChanged).
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members: a face, the frame and the caption.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry caption;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    [[nodiscard]] std::string caption_text() const;
    // The background block behind the caption; its width follows the
    // estimated caption width, as paint has always used.
    [[nodiscard]] Rect caption_bounds() const;

private:
    [[nodiscard]] bool mnemonic_matches(
        char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;
    Event<const std::string&> text_changed_{};
    // property_codemod begin GroupBox (tools/property_families/group_box.json; generated, do not edit)
public:
    // GroupBox's property table: Panel's entries, then its own.
    static constexpr std::size_t group_box_property_count = Panel::panel_property_count + 2U;
    [[nodiscard]] static const std::array<PropertyEntry, group_box_property_count>& group_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_text_changed();
    virtual void on_font_changed();

private:
    friend struct TextCapability<GroupBox>;
    friend struct FontCapability<GroupBox>;
    friend struct GroupBoxUseMnemonicCapability<GroupBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(use_mnemonic)& cell, bool& incoming);
    void written(const decltype(use_mnemonic)& cell);
    // property_codemod end GroupBox
};

} // namespace gui_forms
