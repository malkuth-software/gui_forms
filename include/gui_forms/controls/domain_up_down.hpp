#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/up_down_base.hpp"
#include "gui_forms/controls/text_items_capability.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/domain_up_down_capabilities.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// A text field with up and down arrows that steps through an ordered list of
// strings, as Windows Forms' DomainUpDown. The items are shown top to bottom:
// the up arrow moves to the previous item and the down arrow to the next.
//
// The selection follows the text. Choosing an item puts its text in the
// field; text typed into an editable field selects the item it equals
// (ignoring ASCII case) and clears the selection when it equals none. A
// read-only field takes no text: a typed character selects the next item
// that starts with it (ignoring ASCII case, wrapping past the end), and
// otherwise the value changes only by stepping or set_selected_index.
class GUI_FORMS_API DomainUpDown final : public UpDownBase,
                           public TextItemsCapability<DomainUpDown>,
                           public DomainUpDownCapabilities<DomainUpDown> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit DomainUpDown(StableId stable_id);
    ~DomainUpDown() override;
    void initialize_control_tree();

    // The items in display order (`items`, TextItemsCapability, edited in
    // place): while sorted is true, every edit is followed by sorting
    // (ASCII case folded, then byte order), as Windows Forms' sorted
    // DomainUpDown, so an inserted item need not stay at its position.
    // After each edit the text is kept and the selection re-matched to it:
    // erasing the selected item clears the selection unless another item
    // equals the text.
    // The TextItemsCapability hooks.
    void prepare_edit(const decltype(items)& cell, const CollectionEdit& edit, std::span<std::string> incoming);
    void written(const decltype(items)& cell, const CollectionEdit& edit);

    // The previous item (up) or the next (down). With no selection, up
    // selects the last item and down the first. No effect without items.
    void up_button();
    void down_button();

    // Raised after the selection changes, with the new selection.
    [[nodiscard]] Event<std::optional<std::size_t>>& selected_index_changed() noexcept {
        return selected_index_changed_;
    }

    void arrange(Rect final_bounds) override;
    // The scroll bars, then the spin column's halves as this control's
    // arrow and glyph parts.
    void on_paint_overlay(Painter& painter, Rect local_damage) override;
    void on_key_preview(KeyEvent& event) override;
    void on_pointer_preview(PointerEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct GUI_FORMS_API Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry field;
        PartGeometry up_arrow;
        PartGeometry down_arrow;
        PartGeometry up_glyph;
        PartGeometry down_glyph;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool read_only{};
        bool invalid{};
        bool pending{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The `items` property (text_items_property.hpp).
    [[nodiscard]] BindingValue items_property_value() const;
    void set_items_property(const BindingValue& value);
    void reset_items_property();
    [[nodiscard]] bool should_serialize_items_property() const noexcept;
    struct EditorChangeCallback final {
        std::weak_ptr<DomainUpDown> target;
        void operator()(const std::string&) const;
    };
    struct SpinnerStepCallback final {
        std::weak_ptr<DomainUpDown> target;
        void operator()(int direction) const;
    };

    // The editor's edit filter: refuses every edit while read-only and looks
    // up the item a typed character starts.
    void filter_edit(TextEdit& edit);
    // The next item after the selection (wrapping, the selected item last)
    // whose text starts with `prefix`, ignoring ASCII case.
    [[nodiscard]] std::optional<std::size_t> next_item_starting_with(std::string_view prefix) const;
    // Moves the selection by `offset` items (-1 up, +1 down).
    void step(std::ptrdiff_t offset);
    void select(std::optional<std::size_t> index, bool show_text);
    void match_text();
    void sort_items();
    [[nodiscard]] std::optional<std::size_t> matching_index(std::string_view candidate) const;

    std::optional<std::size_t> selected_index_{};
    bool synchronizing_{};
    Event<std::optional<std::size_t>> selected_index_changed_;
    // property_codemod begin DomainUpDown (tools/property_families/domain_up_down.json; generated, do not edit)
public:
    // DomainUpDown's property table: Panel's entries, then its own.
    static constexpr std::size_t domain_up_down_property_count = Panel::panel_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, domain_up_down_property_count>& domain_up_down_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_text_changed();

private:
    friend struct DomainUpDownSelectedIndexCapability<DomainUpDown>;
    friend struct DomainUpDownTextCapability<DomainUpDown>;
    friend struct DomainUpDownWrapCapability<DomainUpDown>;
    friend struct DomainUpDownReadOnlyCapability<DomainUpDown>;
    friend struct DomainUpDownSortedCapability<DomainUpDown>;
    friend struct DomainUpDownButtonWidthCapability<DomainUpDown>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(selected_index)& cell, std::optional<std::size_t> incoming);
    [[nodiscard]] std::optional<std::size_t> read_view(const decltype(selected_index)& cell) const;
    void write_view(const decltype(text)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(text)& cell) const;
    void prepare_write(const decltype(wrap)& cell, bool& incoming);
    void written(const decltype(wrap)& cell);
    void prepare_write(const decltype(read_only)& cell, bool& incoming);
    void written(const decltype(read_only)& cell);
    void prepare_write(const decltype(sorted)& cell, bool& incoming);
    void written(const decltype(sorted)& cell);
    void prepare_write(const decltype(button_width)& cell, double& incoming);
    void written(const decltype(button_width)& cell);
    // property_codemod end DomainUpDown
};

} // namespace gui_forms
