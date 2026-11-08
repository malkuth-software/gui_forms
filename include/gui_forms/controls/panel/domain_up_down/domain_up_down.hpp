#pragma once

#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

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
class DomainUpDown final : public Panel {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit DomainUpDown(StableId stable_id);
    ~DomainUpDown() override;
    void initialize_control_tree();

    // The items in display order: kept sorted (ASCII case folded, then byte
    // order) while sorted() is true, in insertion order otherwise.
    [[nodiscard]] std::span<const std::string> items() const noexcept { return items_; }
    // Replaces the items. The text is kept and the selection re-matched to it.
    void set_items(GUI_FORMS_LOCALIZABLE(items) std::vector<std::string> items);
    void add_item(GUI_FORMS_LOCALIZABLE(items) std::string item);
    // Throws std::out_of_range for an index past the end. Removing the
    // selected item clears the selection and keeps the text.
    void remove_item_at(std::size_t index);
    void clear_items();

    // The selected item, or none when the text equals no item.
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept {
        return selected_index_;
    }
    // Selects an item and shows its text, or clears the selection and keeps
    // the text. Throws std::out_of_range for an index past the end.
    void set_selected_index(std::optional<std::size_t> index);
    [[nodiscard]] std::string text() const;
    // Sets the field's text and re-matches the selection to it. Allowed on a
    // read-only field: read-only stops typing, not the program.
    void set_text(std::string text);

    // Stepping past either end continues at the other while wrap is on.
    [[nodiscard]] bool wrap() const noexcept { return wrap_; }
    void set_wrap(bool wrap);
    [[nodiscard]] bool read_only() const noexcept { return read_only_; }
    void set_read_only(bool read_only);
    [[nodiscard]] bool sorted() const noexcept { return sorted_; }
    void set_sorted(bool sorted);
    [[nodiscard]] double button_width() const noexcept { return button_width_; }
    void set_button_width(double width);

    // The previous item (up) or the next (down). With no selection, up
    // selects the last item and down the first. No effect without items.
    void up_button();
    void down_button();

    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept { return editor_; }
    // Raised after the selection changes, with the new selection.
    [[nodiscard]] Event<std::optional<std::size_t>>& selected_index_changed() noexcept {
        return selected_index_changed_;
    }

    void arrange(Rect final_bounds) override;
    void on_key_preview(KeyEvent& event) override;
    void on_pointer_preview(PointerEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry field;
        PartGeometry up_arrow;
        PartGeometry down_arrow;
        PartGeometry up_glyph;
        PartGeometry down_glyph;
    };
    struct States final {
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
    [[nodiscard]] std::optional<std::size_t> matching_index(std::string_view text) const;

    std::shared_ptr<TextBox> editor_;
    Control::Ptr spinner_;
    SubscriptionToken editor_change_;
    SubscriptionToken spinner_step_;
    std::vector<std::string> items_;
    std::optional<std::size_t> selected_index_{};
    double button_width_{18.0};
    bool wrap_{};
    bool read_only_{};
    bool sorted_{};
    bool synchronizing_{};
    Event<std::optional<std::size_t>> selected_index_changed_;
};

} // namespace gui_forms
