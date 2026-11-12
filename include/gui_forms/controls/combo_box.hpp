#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/combo_box_values.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/text_items_capability.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/window/native_popup/popup_lift.hpp"
#include "gui_forms/controls/combo_box_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class GUI_FORMS_API ComboBox final : public ListControl, public TextItemsCapability<ComboBox>, public ComboBoxCapabilities<ComboBox> {
public:
    explicit ComboBox(StableId stable_id);
    ~ComboBox() override;

    // The items (`items`, TextItemsCapability) are edited in place:
    // `(*combo).items = {...}`, `items.append(x)`, `items.erase(i, n)`.
    // Items must be valid UTF-8 (std::invalid_argument, nothing changes).
    // The selection follows its item through an edit (the editor's text in
    // the drop_down style); erasing the selected item clears it.
    // The TextItemsCapability hooks: validate and remap the selection
    // (prepare_edit), then refresh the list and raise the changes (written).
    void prepare_edit(const decltype(items)& cell, const CollectionEdit& edit, std::span<std::string> incoming);
    void written(const decltype(items)& cell, const CollectionEdit& edit);
    [[nodiscard]] std::string_view selected_text() const noexcept;

    // The field's TextBox in the drop_down style, and null otherwise.
    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept { return editor_; }
    // Focus on the field goes to its edit part (null without one).
    [[nodiscard]] Ptr focus_target() const override { return editor_; }

    // The items the open list shows, as indices into items(), in row order;
    // empty while the list is closed. Rows keep their item's identity: a
    // row's semantic ID ends in its item's index, and the selection is
    // always an index into items().
    [[nodiscard]] std::span<const std::size_t> drop_down_items() const& noexcept { return rows_; }
    std::span<const std::size_t> drop_down_items() const&& = delete;

    [[nodiscard]] Event<std::optional<std::size_t>>& selected_index_changed() noexcept {
        return selected_index_changed_;
    }
    [[nodiscard]] Event<bool>& drop_down_changed() noexcept {
        return drop_down_changed_;
    }
    [[nodiscard]] Event<>& items_changed() noexcept { return items_changed_; }
    // Raised after text changes, with the new text: in the drop_down
    // style for every change of the editor's text, otherwise after the
    // selection changes.
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct GUI_FORMS_API Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry well;
        PartGeometry content;
        PartGeometry divider;
        PartGeometry drop_down;
        PartGeometry disclosure;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool hot{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool open{};
        bool closed{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    [[nodiscard]] BindingValue items_property_value() const;
    void set_items_property(const BindingValue& value);
    void reset_items_property();
    [[nodiscard]] bool should_serialize_items_property() const noexcept;
    [[nodiscard]] BindingValue selected_index_property_value() const;
    void set_selected_index_property(const BindingValue& value);
    void reset_text_property();
    [[nodiscard]] bool should_serialize_text_property() const noexcept;
    void overlay_selection_changed(const ListSelectionChange& change);
    // Opens the list on the items matching `pattern` (every item for an
    // empty pattern or no filter); nothing opens when none match.
    void open_drop_down(std::string_view pattern);
    // open_drop_down's drop_down-style list, `width` by `height`, anchored
    // under the field.
    void open_field_list(double width, double height);
    // Empties rows_, row_matches_ and filter_pattern_, as a closed list has.
    void clear_rows() noexcept;
    // Fills rows_ and row_matches_ for `pattern`.
    void compute_rows(std::string_view pattern);
    // Shows rows_ in the open list, with their matches and the selected
    // item's row, and sizes the drop_down style's list to them.
    void show_rows();
    // Recomputes the open list's rows after the items or the typed text
    // changed; closes the list when nothing matches.
    void refresh_open_rows(std::string_view pattern);
    [[nodiscard]] std::optional<std::size_t> row_of_item(std::optional<std::size_t> item) const noexcept;
    [[nodiscard]] double drop_down_height(std::size_t rows) const;
    // Up or Down through the open list's rows; from no selection, either
    // goes to the first row.
    bool step_row(std::uint32_t key);
    void close_drop_down();
    void commit_overlay_selection(std::size_t index);
    void on_overlay_revoked();
    void on_anchored_dismissal(OverlayDismissReason reason);
    // The drop_down style's editor: created, configured and removed with
    // the style, observed through its text and focus events.
    void create_editor();
    void remove_editor();
    void configure_editor_auto_complete();
    void rebuild_list_items_source();
    void editor_text_changed(const std::string& typed_text);
    void editor_focus_changed(bool focused);
    // Selects the first item equal to the editor's text, ignoring ASCII case,
    // or clears the selection; the text stays as typed.
    void match_editor_text();
    // Up, Down, Home or End: selects the previous, next, first or last
    // item, counting from the first when nothing is selected. Returns false
    // for another key or without items.
    bool step_selection(std::uint32_t key);
    // Lifts the open list into the owner's picker Popup at `face`, watching
    // for the Popup window's close; false, changing nothing, when it cannot.
    [[nodiscard]] bool lift_list(const std::shared_ptr<ListBox>& list, Rect face, PopupLiftInput input);
    void release_lift() noexcept;
    // The host closed a Popup window: the picker's closes the list.
    void on_popup_window_closed(std::size_t slot);

    // An edit's selection and selected text before the store, taken by
    // prepare_edit for written.
    std::optional<std::size_t> edit_selection_{};
    std::string edit_former_text_;
    std::optional<std::size_t> selected_index_;
    bool dropped_down_{};
    bool focused_{};
    std::shared_ptr<Panel> overlay_layer_;
    std::shared_ptr<ListBox> overlay_list_;
    std::uint64_t overlay_scope_{};
    OverlayToken overlay_token_;
    SubscriptionToken overlay_selection_;
    SubscriptionToken overlay_activation_;
    SubscriptionToken overlay_dismissal_;
    SubscriptionToken overlay_revocation_;
    // The open list's Popup while it shows outside the owner's layer.
    PopupLift lift_;
    SubscriptionToken lift_closed_;
    std::shared_ptr<TextBox> editor_;
    SubscriptionToken editor_text_;
    SubscriptionToken editor_focus_;
    bool synchronizing_text_{};
    // Set while this control moves the open list's row itself.
    bool synchronizing_list_{};
    // Set while a text write gives the editor its text: that is not typing.
    bool programmatic_text_{};
    // The open list's rows (indices into items), the matched text of each
    // row while filtering, and the typed text they were computed for.
    std::vector<std::size_t> rows_;
    std::vector<TextMatchRange> row_matches_;
    std::string filter_pattern_;
    // The items as candidates, for the list_items source; rebuilt with them.
    std::shared_ptr<AutoCompleteStringCollection> list_items_source_;
    Event<std::optional<std::size_t>> selected_index_changed_;
    Event<bool> drop_down_changed_;
    Event<> items_changed_;
    Event<const std::string&> text_changed_;
    bool closing_overlay_{};
    // property_codemod begin ComboBox (tools/property_families/combo_box.json; generated, do not edit)
public:
    // ComboBox's property table: Panel's entries, then its own.
    static constexpr std::size_t combo_box_property_count = Panel::panel_property_count + 6U;
    [[nodiscard]] static const std::array<PropertyEntry, combo_box_property_count>& combo_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_selected_index_changed();
    virtual void on_text_changed();
    virtual void on_font_changed();
    virtual void on_drop_down_style_changed();

private:
    friend struct ComboBoxSelectedIndexCapability<ComboBox>;
    friend struct ComboBoxTextCapability<ComboBox>;
    friend struct ComboBoxPlaceholderTextCapability<ComboBox>;
    friend struct ComboBoxDroppedDownCapability<ComboBox>;
    friend struct ComboBoxMaximumDropDownItemsCapability<ComboBox>;
    friend struct ComboBoxDropDownWidthCapability<ComboBox>;
    friend struct FontCapability<ComboBox>;
    friend struct ComboBoxDropDownStyleCapability<ComboBox>;
    friend struct ComboBoxDropDownFilterCapability<ComboBox>;
    friend struct ComboBoxAutoCompleteModeCapability<ComboBox>;
    friend struct ComboBoxAutoCompleteSourceCapability<ComboBox>;
    friend struct ComboBoxAutoCompleteCustomSourceCapability<ComboBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(selected_index)& cell, std::optional<std::size_t> incoming);
    [[nodiscard]] std::optional<std::size_t> read_view(const decltype(selected_index)& cell) const;
    void write_view(const decltype(text)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(text)& cell) const;
    void prepare_write(const decltype(placeholder_text)& cell, std::string& incoming);
    void written(const decltype(placeholder_text)& cell);
    void write_view(const decltype(dropped_down)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(dropped_down)& cell) const;
    void prepare_write(const decltype(maximum_drop_down_items)& cell, std::size_t& incoming);
    void written(const decltype(maximum_drop_down_items)& cell);
    void prepare_write(const decltype(drop_down_width)& cell, double& incoming);
    void written(const decltype(drop_down_width)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(drop_down_style)& cell, ComboBoxStyle& incoming);
    void written(const decltype(drop_down_style)& cell);
    void prepare_write(const decltype(drop_down_filter)& cell, ComboBoxFilter& incoming);
    void written(const decltype(drop_down_filter)& cell);
    void prepare_write(const decltype(auto_complete_mode)& cell, AutoCompleteMode& incoming);
    void written(const decltype(auto_complete_mode)& cell);
    void prepare_write(const decltype(auto_complete_source)& cell, AutoCompleteSource& incoming);
    void written(const decltype(auto_complete_source)& cell);
    void prepare_write(const decltype(auto_complete_custom_source)& cell, std::shared_ptr<AutoCompleteStringCollection>& incoming);
    void written(const decltype(auto_complete_custom_source)& cell);
    // property_codemod end ComboBox
};

} // namespace gui_forms
