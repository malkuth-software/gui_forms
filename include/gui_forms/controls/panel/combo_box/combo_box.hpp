#pragma once

#include "gui_forms/controls/panel/list_box/list_box.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/panel/text_items_capability/text_items_capability.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Whether a ComboBox's field takes text, as Windows Forms' ComboBoxStyle
// (its Simple style, a list always shown under the field, is not offered).
enum class ComboBoxStyle : std::uint8_t {
    drop_down_list,  // the field shows the selected item and takes no text
    drop_down,       // the field is a TextBox: any text, or an item from the list
};

// How a drop_down-style ComboBox's list follows what the user types.
enum class ComboBoxFilter : std::uint8_t {
    none,       // the list shows every item
    prefix,     // the list shows the items that start with the typed text
    substring,  // the list shows the items that contain the typed text
};

class ComboBox final : public Panel, public TextItemsCapability<ComboBox> {
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
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept {
        return selected_index_;
    }
    void set_selected_index(std::optional<std::size_t> index);
    [[nodiscard]] std::string_view selected_text() const noexcept;
    [[nodiscard]] std::string_view placeholder_text() const noexcept {
        return placeholder_;
    }
    void set_placeholder_text(GUI_FORMS_LOCALIZABLE(placeholder) std::string text);
    [[nodiscard]] bool dropped_down() const noexcept { return dropped_down_; }
    void set_dropped_down(bool dropped_down);
    [[nodiscard]] std::size_t maximum_drop_down_items() const noexcept {
        return maximum_drop_down_items_;
    }
    void set_maximum_drop_down_items(std::size_t count);
    // Zero tracks the control width. A positive value supplies the desired
    // overlay width while host placement still constrains it to the work area.
    [[nodiscard]] double drop_down_width() const noexcept {
        return drop_down_width_;
    }
    void set_drop_down_width(double width);
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);

    [[nodiscard]] ComboBoxStyle drop_down_style() const noexcept { return drop_down_style_; }
    // Switching to drop_down creates the editor with the selected item's
    // text; switching back removes it and keeps the selection. Either
    // closes an open list.
    void set_drop_down_style(ComboBoxStyle style);
    // The field's text: the editor's in the drop_down style, the selected
    // item's otherwise.
    [[nodiscard]] std::string text() const;
    // In the drop_down style, sets the editor's text and selects the item it
    // equals (ignoring ASCII case), or clears the selection. In the
    // drop_down_list style, selects the first item equal to `text`, or clears
    // the selection. Invalid UTF-8 throws std::invalid_argument.
    void set_text(std::string text);
    // The field's TextBox in the drop_down style, and null otherwise.
    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept { return editor_; }

    // Searching: in the drop_down style, typing opens the list showing only
    // the items that match the typed text (ignoring ASCII case), with the
    // matched text highlighted. While a filter is set the editor does not
    // auto-complete. The setting is kept in the drop_down_list style and
    // acts there once the style is drop_down. Changing it closes the list.
    [[nodiscard]] ComboBoxFilter drop_down_filter() const noexcept { return drop_down_filter_; }
    void set_drop_down_filter(ComboBoxFilter filter);
    // The items the open list shows, as indices into items(), in row order;
    // empty while the list is closed. Rows keep their item's identity: a
    // row's semantic ID ends in its item's index, and the selection is
    // always an index into items().
    [[nodiscard]] std::span<const std::size_t> drop_down_items() const noexcept { return rows_; }

    // Auto-complete for the drop_down style's editor, as Windows Forms'
    // AutoCompleteMode, AutoCompleteSource and AutoCompleteCustomSource.
    // list_items offers the items; custom_source the collection below; the
    // host sources report unsupported (see TextBox). The settings are kept
    // in the drop_down_list style, where nothing can be typed, and act when
    // the style becomes drop_down.
    [[nodiscard]] AutoCompleteMode auto_complete_mode() const noexcept { return auto_complete_mode_; }
    void set_auto_complete_mode(AutoCompleteMode mode);
    [[nodiscard]] AutoCompleteSource auto_complete_source() const noexcept {
        return auto_complete_source_;
    }
    void set_auto_complete_source(AutoCompleteSource source);
    // Never null; shared as TextBox's is. Null throws std::invalid_argument.
    [[nodiscard]] const std::shared_ptr<AutoCompleteStringCollection>&
    auto_complete_custom_source() const noexcept {
        return auto_complete_custom_source_;
    }
    void set_auto_complete_custom_source(std::shared_ptr<AutoCompleteStringCollection> source);

    [[nodiscard]] Event<std::optional<std::size_t>>& selected_index_changed() noexcept {
        return selected_index_changed_;
    }
    [[nodiscard]] Event<bool>& drop_down_changed() noexcept {
        return drop_down_changed_;
    }
    [[nodiscard]] Event<>& items_changed() noexcept { return items_changed_; }
    // Raised after text() changes, with the new text: in the drop_down
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
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry well;
        PartGeometry content;
        PartGeometry divider;
        PartGeometry drop_down;
        PartGeometry disclosure;
    };
    struct States final {
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
    [[nodiscard]] BindingValue text_property_value() const;
    void set_text_property(const BindingValue& value);
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
    void editor_text_changed(const std::string& text);
    void editor_focus_changed(bool focused);
    // Selects the first item equal to the editor's text, ignoring ASCII case,
    // or clears the selection; the text stays as typed.
    void match_editor_text();
    // Up, Down, Home or End: selects the previous, next, first or last
    // item, counting from the first when nothing is selected. Returns false
    // for another key or without items.
    bool step_selection(std::uint32_t key);

    // An edit's selection and selected text before the store, taken by
    // prepare_edit for written.
    std::optional<std::size_t> edit_selection_{};
    std::string edit_former_text_;
    std::optional<std::size_t> selected_index_;
    std::string placeholder_;
    FontSpec font_{FontRole::content, 12.0, 400, false};
    std::size_t maximum_drop_down_items_{8U};
    double drop_down_width_{};
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
    ComboBoxStyle drop_down_style_{ComboBoxStyle::drop_down_list};
    std::shared_ptr<TextBox> editor_;
    SubscriptionToken editor_text_;
    SubscriptionToken editor_focus_;
    bool synchronizing_text_{};
    // Set while this control moves the open list's row itself.
    bool synchronizing_list_{};
    // Set while set_text gives the editor its text: that is not typing.
    bool programmatic_text_{};
    ComboBoxFilter drop_down_filter_{ComboBoxFilter::none};
    // The open list's rows (indices into items_), the matched text of each
    // row while filtering, and the typed text they were computed for.
    std::vector<std::size_t> rows_;
    std::vector<TextMatchRange> row_matches_;
    std::string filter_pattern_;
    AutoCompleteMode auto_complete_mode_{AutoCompleteMode::none};
    AutoCompleteSource auto_complete_source_{AutoCompleteSource::none};
    std::shared_ptr<AutoCompleteStringCollection> auto_complete_custom_source_;
    // The items as candidates, for the list_items source; rebuilt with them.
    std::shared_ptr<AutoCompleteStringCollection> list_items_source_;
    Event<std::optional<std::size_t>> selected_index_changed_;
    Event<bool> drop_down_changed_;
    Event<> items_changed_;
    Event<const std::string&> text_changed_;
    bool closing_overlay_{};
};

} // namespace gui_forms
