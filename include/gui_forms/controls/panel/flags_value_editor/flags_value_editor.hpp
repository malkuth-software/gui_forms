#pragma once

#include "gui_forms/inspection/inspection_types.hpp"
#include "gui_forms/window/native_popup/popup_lift.hpp"
#include "gui_forms/controls/panel/flags_value_editor/flags_value_editor_capabilities/flags_value_editor_capabilities.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// A flags value editor. Code gives it a descriptor and a value; a form
// document, which makes it from its stable ID alone, writes its form
// properties instead, in any order: `enumeration-name` (`Flags` until
// written), `flag-names` (each name the next bit: 1, 2, 4 and so on),
// `none-name` (the name of no flag; empty for none) and `checked-flags`
// (names among `flag-names`). Until `flag-names` is written the editor
// holds no flags, shows no value and opens no list.
class FlagsValueEditor final : public Panel, public FlagsValueEditorCapabilities<FlagsValueEditor> {
public:
    explicit FlagsValueEditor(StableId stable_id);
    FlagsValueEditor(StableId stable_id, PropertyEnumDescriptor initial_descriptor,
                     PropertyEnumValue initial_value);

    [[nodiscard]] Event<const PropertyEnumValue&>& value_changed() noexcept {
        return value_changed_;
    }
    [[nodiscard]] Event<bool>& drop_down_changed() noexcept {
        return drop_down_changed_;
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view action_value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry content;
        PartGeometry divider;
        PartGeometry drop_down;
        PartGeometry disclosure;
    };
    struct States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool open{};
        bool closed{};
        bool invalid{};
        bool pending{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    void open_drop_down();
    void close_drop_down();
    void on_overlay_revoked();
    // The host closed a Popup window: the picker's closes the list.
    void on_popup_window_closed(std::size_t slot);
    void synchronize_overlay();
    void apply_overlay_choice(std::size_t overlay_index, CheckState state);
    // The form properties (see the class comment): each getter reads the
    // descriptor and value; each setter records its part and applies the
    // whole once `flag-names` is written.
    [[nodiscard]] BindingValue enumeration_name_property() const;
    void set_enumeration_name_property(const BindingValue& binding);
    [[nodiscard]] BindingValue flag_names_property() const;
    void set_flag_names_property(const BindingValue& binding);
    [[nodiscard]] BindingValue none_name_property() const;
    void set_none_name_property(const BindingValue& binding);
    [[nodiscard]] BindingValue checked_flags_property() const;
    void set_checked_flags_property(const BindingValue& binding);
    void apply_form_flags();

    PropertyEnumDescriptor descriptor_;
    PropertyEnumValue value_;
    // What the form properties have written so far; applied together by
    // apply_form_flags once form_flag_names_ is not empty.
    std::string form_enumeration_name_;
    std::vector<std::string> form_flag_names_;
    std::string form_none_name_;
    std::vector<std::string> form_checked_flags_;
    std::shared_ptr<Panel> overlay_layer_;
    std::shared_ptr<CheckedListBox> overlay_list_;
    std::vector<std::size_t> overlay_choice_indices_;
    std::uint64_t overlay_scope_{};
    OverlayToken overlay_token_;
    SubscriptionToken overlay_check_;
    SubscriptionToken overlay_dismissal_;
    SubscriptionToken overlay_revocation_;
    // The list's Popup while it shows outside the owner's layer (owner,
    // 2026-11-08: drop-downs are Popups before 0.5).
    PopupLift lift_;
    SubscriptionToken lift_closed_;
    bool dropped_down_{};
    bool focused_{};
    bool synchronizing_overlay_{};
    bool closing_overlay_{};
    Event<const PropertyEnumValue&> value_changed_;
    Event<bool> drop_down_changed_;
    // property_codemod begin FlagsValueEditor (tools/property_families/flags_value_editor.json; generated, do not edit)
public:
    // FlagsValueEditor's property table: Panel's entries, then its own.
    static constexpr std::size_t flags_value_editor_property_count = Panel::panel_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, flags_value_editor_property_count>& flags_value_editor_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct FlagsValueEditorDescriptorCapability<FlagsValueEditor>;
    friend struct FlagsValueEditorValueCapability<FlagsValueEditor>;
    friend struct FlagsValueEditorDroppedDownCapability<FlagsValueEditor>;
    friend struct FlagsValueEditorOverlayWidthCapability<FlagsValueEditor>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(descriptor)& cell, PropertyEnumDescriptor incoming);
    [[nodiscard]] PropertyEnumDescriptor read_view(const decltype(descriptor)& cell) const;
    void write_view(const decltype(value)& cell, PropertyEnumValue incoming);
    [[nodiscard]] PropertyEnumValue read_view(const decltype(value)& cell) const;
    void write_view(const decltype(dropped_down)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(dropped_down)& cell) const;
    void prepare_write(const decltype(overlay_width)& cell, double& incoming);
    void written(const decltype(overlay_width)& cell);
    // property_codemod end FlagsValueEditor
};

} // namespace gui_forms
